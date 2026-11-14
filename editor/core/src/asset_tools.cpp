#include "editor/core/asset_tools.h"

#include "engine/serialize/json.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>
#include <utility>

namespace editor {

namespace fs = std::filesystem;
namespace serialize = engine::serialize;

namespace {

constexpr std::string_view project_file = "project.sparxel.json";

/// Files that may name other assets.
constexpr std::array<std::string_view, 6> text_kinds = {".dscene",  ".drules", ".dtalk",
                                                        ".dprefab", ".dtable", ".dsprite"};
/// Files moved together when one of them is renamed.
constexpr std::array<std::string_view, 5> picture_kinds = {".png", ".sheet", ".dsprite",
                                                           ".aseprite", ".ase"};
/// Tables the game itself reads by name.
constexpr std::array<std::string_view, 8> reserved_tables = {
    "data/achievements", "data/enemies", "data/items",  "data/levels",
    "data/party",        "data/skills",  "data/states", "data/translations"};

std::string read_all(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string generic(const fs::path& path) {
    const std::u8string text = path.generic_u8string();
    return {text.begin(), text.end()};
}

fs::path native(std::string_view path) {
    return fs::path(std::u8string(path.begin(), path.end()));
}

bool has_kind(const fs::path& path, auto kinds) {
    const std::string extension = generic(path.extension());
    return std::ranges::find(kinds, extension) != kinds.end();
}

/// The asset's GUID from its .meta, or empty.
std::string guid_of(const fs::path& assets, std::string_view asset) {
    std::vector<serialize::Issue> ignored;
    const std::optional<serialize::Json> meta =
        serialize::parse(read_all(assets / native(std::string(asset) + ".meta")), ignored);
    if (meta && meta->is_object() && meta->contains("guid") && (*meta)["guid"].is_string()) {
        return (*meta)["guid"].get<std::string>();
    }
    return {};
}

/// The text files that may name an asset: relative to assets/ (and the project file).
std::vector<std::pair<std::string, fs::path>> text_files(const fs::path& project) {
    std::vector<std::pair<std::string, fs::path>> files;
    const fs::path assets = project / "assets";
    std::error_code failed;
    for (auto it = fs::recursive_directory_iterator(assets, failed);
         !failed && it != fs::recursive_directory_iterator(); it.increment(failed)) {
        if (generic(it->path().filename()).starts_with('.')) {
            if (it->is_directory(failed)) {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (it->is_regular_file(failed) && has_kind(it->path(), text_kinds)) {
            files.emplace_back(generic(it->path().lexically_relative(assets)), it->path());
        }
    }
    if (fs::exists(project / project_file, failed)) {
        files.emplace_back(std::string(project_file), project / project_file);
    }
    std::ranges::sort(files);
    return files;
}

std::size_t count_of(std::string_view text, std::string_view token) {
    std::size_t count = 0;
    for (std::size_t at = text.find(token); !token.empty() && at != std::string_view::npos;
         at = text.find(token, at + token.size())) {
        ++count;
    }
    return count;
}

/// The ways an asset is named: its GUID, and its path in quotes (a table also without
/// its extension).
std::vector<std::string> tokens_for(const fs::path& assets, std::string_view asset) {
    std::vector<std::string> tokens;
    if (std::string guid = guid_of(assets, asset); !guid.empty()) {
        tokens.push_back(std::move(guid));
    }
    tokens.push_back("\"" + std::string(asset) + "\"");
    if (asset.ends_with(".dtable")) {
        tokens.push_back("\"" + std::string(asset.substr(0, asset.size() - 7)) + "\"");
    }
    return tokens;
}

/// In a scene or prefab: the names of the entities whose text holds any of `tokens`.
std::vector<std::string> entities_naming(std::string_view text,
                                         const std::vector<std::string>& tokens) {
    std::vector<std::string> names;
    std::vector<serialize::Issue> ignored;
    const std::optional<serialize::Json> json = serialize::parse(text, ignored);
    if (!json || !json->is_object() || !json->contains("entities") ||
        !(*json)["entities"].is_array()) {
        return names;
    }
    for (const serialize::Json& entity : (*json)["entities"]) {
        const std::string dumped = serialize::to_text(entity);
        const bool uses = std::ranges::any_of(
            tokens, [&dumped](const std::string& t) { return count_of(dumped, t) > 0; });
        if (uses && entity.is_object() && entity.contains("name") && entity["name"].is_string()) {
            names.push_back(entity["name"].get<std::string>());
        }
    }
    return names;
}

/// Writes `text` over `path` through a temporary file, so a failure leaves it whole.
bool write_whole(const fs::path& path, const std::string& text) {
    fs::path temporary = path;
    temporary += ".renaming";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << text;
        if (!out) {
            return false;
        }
    }
    std::error_code failed;
    fs::rename(temporary, path, failed);
    return !failed;
}

} // namespace

std::vector<AssetUse> find_asset_usages(const fs::path& project, std::string_view asset) {
    const fs::path assets = project / "assets";
    const std::vector<std::string> tokens = tokens_for(assets, asset);
    const std::vector<std::string> own = asset_group(project, asset);
    std::vector<AssetUse> uses;
    for (const auto& [relative, path] : text_files(project)) {
        if (std::ranges::find(own, relative) != own.end()) {
            continue;
        }
        const std::string text = read_all(path);
        std::size_t count = 0;
        for (const std::string& token : tokens) {
            count += count_of(text, token);
        }
        if (count == 0) {
            continue;
        }
        AssetUse use{relative, {}, count};
        if (relative.ends_with(".dscene") || relative.ends_with(".dprefab")) {
            use.where = entities_naming(text, tokens);
        }
        uses.push_back(std::move(use));
    }
    return uses;
}

std::vector<std::string> asset_group(const fs::path& project, std::string_view asset) {
    const fs::path assets = project / "assets";
    const fs::path path = native(asset);
    std::vector<std::string> group{std::string(asset)};
    if (!has_kind(path, picture_kinds)) {
        return group;
    }
    const fs::path folder = path.parent_path();
    for (const std::string_view kind : picture_kinds) {
        fs::path sibling = folder / path.stem();
        sibling += native(kind);
        const std::string relative = generic(sibling);
        std::error_code failed;
        if (relative != asset && fs::is_regular_file(assets / sibling, failed)) {
            group.push_back(relative);
        }
    }
    return group;
}

bool rename_asset(const fs::path& project, std::string_view asset, std::string_view name,
                  std::vector<std::string>& changed, std::string& error) {
    const fs::path assets = project / "assets";
    const fs::path path = native(asset);
    std::error_code failed;
    if (asset.empty() || !fs::is_regular_file(assets / path, failed) ||
        asset.find("..") != std::string_view::npos) {
        error = "There is no such asset.";
        return false;
    }
    const bool plain = !name.empty() && std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '-' || c == ' ';
    });
    if (!plain || name.front() == ' ' || name.back() == ' ') {
        error = "A name is letters, digits, spaces, - and _.";
        return false;
    }
    if (asset.ends_with(".dtable") &&
        std::ranges::find(reserved_tables, asset.substr(0, asset.size() - 7)) !=
            reserved_tables.end()) {
        error = "The game reads this table by its name, so it keeps it.";
        return false;
    }
    // Every file of the group, from and to; none of the new names may be taken.
    std::vector<std::pair<std::string, std::string>> moves;
    for (const std::string& from : asset_group(project, asset)) {
        const fs::path old_path = native(from);
        fs::path to = old_path.parent_path() / native(name);
        to += old_path.extension();
        moves.emplace_back(from, generic(to));
        if (fs::exists(assets / to, failed)) {
            error = "There is already a " + generic(to) + ".";
            return false;
        }
    }
    if (moves.front().first == moves.front().second) {
        error = "That is its name already.";
        return false;
    }

    // The references first, read before anything moves.
    std::vector<std::pair<std::string, std::string>> rewrites;
    for (const auto& [from, to] : moves) {
        rewrites.emplace_back("\"" + from + "\"", "\"" + to + "\"");
        if (from.ends_with(".dtable")) {
            rewrites.emplace_back("\"" + from.substr(0, from.size() - 7) + "\"",
                                  "\"" + to.substr(0, to.size() - 7) + "\"");
        }
    }
    std::vector<std::pair<fs::path, std::string>> rewritten;
    for (const auto& [relative, file] : text_files(project)) {
        std::string text = read_all(file);
        bool any = false;
        for (const auto& [old_text, new_text] : rewrites) {
            for (std::size_t at = text.find(old_text); at != std::string::npos;
                 at = text.find(old_text, at + new_text.size())) {
                text.replace(at, old_text.size(), new_text);
                any = true;
            }
        }
        if (any) {
            // A file of the group itself is rewritten where it lands.
            const auto moved = std::ranges::find_if(
                moves, [&relative](const auto& m) { return m.first == relative; });
            rewritten.emplace_back(moved != moves.end() ? assets / native(moved->second) : file,
                                   std::move(text));
            changed.push_back(moved != moves.end() ? moved->second : relative);
        }
    }

    // Then the files, each with its .meta (so its GUID, and what uses it by GUID, stay).
    for (std::size_t i = 0; i < moves.size(); ++i) {
        const auto& [from, to] = moves[i];
        fs::rename(assets / native(from), assets / native(to), failed);
        if (!failed && fs::exists(assets / native(from + ".meta"))) {
            fs::rename(assets / native(from + ".meta"), assets / native(to + ".meta"), failed);
        }
        if (failed) {
            // Put back what moved, so the project is as it was.
            for (std::size_t j = 0; j <= i; ++j) {
                std::error_code ignored;
                fs::rename(assets / native(moves[j].second), assets / native(moves[j].first),
                           ignored);
                fs::rename(assets / native(moves[j].second + ".meta"),
                           assets / native(moves[j].first + ".meta"), ignored);
            }
            changed.clear();
            error = "The asset could not be renamed: " + failed.message();
            return false;
        }
    }
    for (const auto& [file, text] : rewritten) {
        if (!write_whole(file, text)) {
            error = "Renamed, but " + generic(file.filename()) + " could not be updated.";
            return false;
        }
    }
    return true;
}

} // namespace editor
