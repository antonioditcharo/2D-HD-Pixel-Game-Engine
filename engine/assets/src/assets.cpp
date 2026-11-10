#include "engine/assets/assets.h"

#include "engine/serialize/json.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <system_error>
#include <utility>

namespace engine::assets {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view meta_suffix = ".meta";

[[nodiscard]] bool ends_with(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

[[nodiscard]] std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    });
    return text;
}

/// The importer for a new asset, from its extension.
[[nodiscard]] std::string importer_for(std::string_view path) {
    return ends_with(lowercase(std::string(path)), ".png") ? "image" : "file";
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>> read_file(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    if (in.bad()) {
        return std::nullopt;
    }
    return bytes;
}

[[nodiscard]] bool write_file(const fs::path& file, std::string_view text) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

[[nodiscard]] std::string meta_text(Guid guid, std::string_view importer) {
    serialize::Json json = serialize::Json::object();
    json["format"] = meta_format;
    json["version"] = meta_version;
    json["guid"] = guid.to_string();
    json["importer"] = importer;
    return serialize::to_text(json);
}

struct Meta {
    Guid guid;
    std::string importer;
};

/// Reads a ".meta" file, or reports why it cannot be used.
[[nodiscard]] std::optional<Meta> read_meta(const fs::path& file, std::string& problem) {
    const std::optional<std::vector<std::uint8_t>> bytes = read_file(file);
    if (!bytes) {
        problem = "The .meta file cannot be read.";
        return std::nullopt;
    }
    std::vector<serialize::Issue> issues;
    const std::optional<serialize::Json> json = serialize::parse(
        std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), issues);
    if (!json || !json->is_object()) {
        problem =
            "The .meta file is not valid JSON, so the asset was skipped. Fix or delete the .meta "
            "file (deleting it gives the asset a new GUID).";
        return std::nullopt;
    }
    const auto format = json->find("format");
    const auto guid = json->find("guid");
    if (format == json->end() || !format->is_string() ||
        format->get_ref<const std::string&>() != meta_format || guid == json->end() ||
        !guid->is_string()) {
        problem = "The .meta file does not hold a Sparxel GUID, so the asset was skipped.";
        return std::nullopt;
    }
    const std::optional<Guid> parsed = Guid::parse(guid->get_ref<const std::string&>());
    if (!parsed) {
        problem = "The .meta file's GUID is not 32 hexadecimal digits, so the asset was skipped.";
        return std::nullopt;
    }
    Meta meta;
    meta.guid = *parsed;
    if (const auto importer = json->find("importer");
        importer != json->end() && importer->is_string()) {
        meta.importer = importer->get<std::string>();
    }
    return meta;
}

/// One asset file found by a scan.
struct Found {
    std::string path;
    fs::path file;
    std::uint64_t size = 0;
    fs::file_time_type modified;
};

} // namespace

std::uint64_t content_hash(std::span<const std::uint8_t> bytes) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

AssetDatabase::AssetDatabase(fs::path root) : root_(std::move(root)) {}

AssetChanges AssetDatabase::refresh() {
    issues_.clear();
    std::vector<Found> files;
    std::set<std::string> metas; ///< Paths of ".meta" files, relative.

    std::error_code ec;
    if (!fs::is_directory(root_, ec)) {
        issues_.push_back(
            AssetIssue{"", "The assets folder " + root_.generic_string() + " does not exist."});
    } else {
        fs::recursive_directory_iterator it(root_, fs::directory_options::skip_permission_denied,
                                            ec);
        for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            const fs::path& entry = it->path();
            const std::string name = entry.filename().string();
            if (!name.empty() && name.front() == '.') {
                if (it->is_directory(ec)) {
                    it.disable_recursion_pending();
                }
                continue;
            }
            if (!it->is_regular_file(ec)) {
                continue;
            }
            const std::string relative = entry.lexically_relative(root_).generic_string();
            if (ends_with(relative, meta_suffix)) {
                metas.insert(relative);
                continue;
            }
            Found found;
            found.path = relative;
            found.file = entry;
            found.size = static_cast<std::uint64_t>(it->file_size(ec));
            found.modified = it->last_write_time(ec);
            files.push_back(std::move(found));
        }
        if (ec) {
            issues_.push_back(
                AssetIssue{"", "Scanning the assets folder stopped early: " + ec.message()});
        }
    }
    std::sort(files.begin(), files.end(),
              [](const Found& a, const Found& b) { return a.path < b.path; });

    // Content hashes, re-read only when the size or time changed.
    const auto hash_of = [&](const Found& found,
                             std::optional<Guid> guid) -> std::optional<std::uint64_t> {
        if (guid) {
            const auto known = known_.find(*guid);
            if (known != known_.end() && known->second.record.path == found.path &&
                known->second.record.size == found.size &&
                known->second.modified == found.modified) {
                return known->second.record.content_hash;
            }
        }
        const std::optional<std::vector<std::uint8_t>> bytes = read_file(found.file);
        if (!bytes) {
            issues_.push_back(
                AssetIssue{found.path, "The file cannot be read, so it was skipped."});
            return std::nullopt;
        }
        return content_hash(*bytes);
    };

    // Assets with a readable .meta, grouped by GUID to find duplicates.
    std::map<Guid, std::vector<std::pair<const Found*, Meta>>> by_guid;
    std::vector<const Found*> without_meta;
    for (const Found& found : files) {
        const std::string meta_path = found.path + std::string(meta_suffix);
        if (!metas.contains(meta_path)) {
            without_meta.push_back(&found);
            continue;
        }
        std::string problem;
        const std::optional<Meta> meta = read_meta(root_ / fs::path(meta_path), problem);
        if (!meta) {
            issues_.push_back(AssetIssue{meta_path, problem});
            continue;
        }
        by_guid[meta->guid].emplace_back(&found, *meta);
    }

    std::unordered_map<Guid, Known, GuidHash> next;
    const auto keep = [&](const Found& found, Guid guid, std::string importer, std::uint64_t hash) {
        Known entry;
        entry.record.guid = guid;
        entry.record.path = found.path;
        entry.record.importer = importer.empty() ? importer_for(found.path) : std::move(importer);
        entry.record.size = found.size;
        entry.record.content_hash = hash;
        entry.modified = found.modified;
        next.emplace(guid, std::move(entry));
    };
    const auto give_new_guid = [&](const Found& found,
                                   const std::string& importer) -> std::optional<Guid> {
        Guid guid = Guid::generate();
        while (by_guid.contains(guid) || known_.contains(guid) || next.contains(guid)) {
            guid = Guid::generate();
        }
        const std::string meta_path = found.path + std::string(meta_suffix);
        if (!write_file(root_ / fs::path(meta_path), meta_text(guid, importer))) {
            issues_.push_back(AssetIssue{
                meta_path, "The .meta file cannot be written, so the asset was skipped."});
            return std::nullopt;
        }
        return guid;
    };

    std::vector<std::pair<const Found*, Meta>> recopied; ///< Copies that need a new GUID.
    for (auto& [guid, holders] : by_guid) {
        // The holder already known keeps the GUID; otherwise the first by path does.
        std::size_t keeper = 0;
        if (const auto known = known_.find(guid); known != known_.end()) {
            for (std::size_t i = 0; i < holders.size(); ++i) {
                if (holders[i].first->path == known->second.record.path) {
                    keeper = i;
                }
            }
        }
        for (std::size_t i = 0; i < holders.size(); ++i) {
            if (i != keeper) {
                recopied.push_back(holders[i]);
                continue;
            }
            const std::optional<std::uint64_t> hash = hash_of(*holders[i].first, guid);
            if (hash) {
                keep(*holders[i].first, guid, holders[i].second.importer, *hash);
            }
        }
    }
    for (const auto& [found, meta] : recopied) {
        const std::optional<std::uint64_t> hash = hash_of(*found, std::nullopt);
        const std::string importer =
            meta.importer.empty() ? importer_for(found->path) : meta.importer;
        const std::optional<Guid> guid = hash ? give_new_guid(*found, importer) : std::nullopt;
        if (guid) {
            keep(*found, *guid, importer, *hash);
            const auto original = next.find(meta.guid);
            const std::string other =
                original == next.end() ? "another asset" : original->second.record.path;
            issues_.push_back(AssetIssue{found->path + std::string(meta_suffix),
                                         "This .meta file had the same GUID as " + other +
                                             "'s, probably from a copy. It was given a new GUID."});
        }
    }

    // Files without a .meta: a vanished asset with the same content was moved here without
    // its .meta and keeps its GUID; anything else is new.
    std::multimap<std::uint64_t, Guid> vanished;
    for (const auto& [guid, entry] : known_) {
        if (!next.contains(guid)) {
            vanished.emplace(entry.record.content_hash, guid);
        }
    }
    for (const Found* found : without_meta) {
        const std::optional<std::uint64_t> hash = hash_of(*found, std::nullopt);
        if (!hash) {
            continue;
        }
        std::optional<Guid> guid;
        std::string importer = importer_for(found->path);
        for (auto [it, end] = vanished.equal_range(*hash); it != end; ++it) {
            const Known& old = known_.at(it->second);
            if (old.record.size != found->size) {
                continue;
            }
            const std::string meta_path = found->path + std::string(meta_suffix);
            importer = old.record.importer;
            if (!write_file(root_ / fs::path(meta_path), meta_text(it->second, importer))) {
                issues_.push_back(AssetIssue{
                    meta_path, "The .meta file cannot be written, so the asset was skipped."});
                break;
            }
            guid = it->second;
            // The .meta left at the old place now names an asset that is here instead.
            const std::string stale = old.record.path + std::string(meta_suffix);
            if (metas.contains(stale)) {
                std::string problem;
                const std::optional<Meta> stale_meta = read_meta(root_ / fs::path(stale), problem);
                if (stale_meta && stale_meta->guid == it->second) {
                    std::error_code remove_error;
                    fs::remove(root_ / fs::path(stale), remove_error);
                    metas.erase(stale);
                }
            }
            vanished.erase(it);
            break;
        }
        if (!guid) {
            guid = give_new_guid(*found, importer);
        }
        if (guid) {
            keep(*found, *guid, importer, *hash);
        }
    }

    // .meta files whose asset is gone are reported and left alone.
    for (const std::string& meta : metas) {
        const std::string asset = meta.substr(0, meta.size() - meta_suffix.size());
        const bool has_asset = std::any_of(files.begin(), files.end(),
                                           [&](const Found& f) { return f.path == asset; });
        if (!has_asset) {
            issues_.push_back(
                AssetIssue{meta, "This .meta file has no asset next to it. Delete it if the asset "
                                 "was removed on purpose."});
        }
    }

    AssetChanges changes;
    for (const auto& [guid, entry] : next) {
        const auto old = known_.find(guid);
        if (old == known_.end()) {
            changes.added.push_back(guid);
            continue;
        }
        if (old->second.record.path != entry.record.path) {
            changes.moved.push_back(guid);
        }
        if (old->second.record.content_hash != entry.record.content_hash ||
            old->second.record.size != entry.record.size) {
            changes.modified.push_back(guid);
        }
    }
    for (const auto& [guid, entry] : known_) {
        if (!next.contains(guid)) {
            changes.removed.push_back(guid);
        }
    }
    for (std::vector<Guid>* list :
         {&changes.added, &changes.modified, &changes.moved, &changes.removed}) {
        std::sort(list->begin(), list->end());
    }
    known_ = std::move(next);
    std::sort(issues_.begin(), issues_.end(),
              [](const AssetIssue& a, const AssetIssue& b) { return a.path < b.path; });
    return changes;
}

const AssetRecord* AssetDatabase::find(Guid guid) const {
    const auto found = known_.find(guid);
    return found == known_.end() ? nullptr : &found->second.record;
}

const AssetRecord* AssetDatabase::find(std::string_view path) const {
    for (const auto& [guid, entry] : known_) {
        if (entry.record.path == path) {
            return &entry.record;
        }
    }
    return nullptr;
}

std::vector<AssetRecord> AssetDatabase::assets() const {
    std::vector<AssetRecord> records;
    records.reserve(known_.size());
    for (const auto& [guid, entry] : known_) {
        records.push_back(entry.record);
    }
    std::sort(records.begin(), records.end(),
              [](const AssetRecord& a, const AssetRecord& b) { return a.path < b.path; });
    return records;
}

std::optional<std::vector<std::uint8_t>> AssetDatabase::read(Guid guid) const {
    const AssetRecord* record = find(guid);
    if (record == nullptr) {
        return std::nullopt;
    }
    return read_file(root_ / fs::path(record->path));
}

// ---------------------------------------------------------------------------------------
// Images
// ---------------------------------------------------------------------------------------

std::optional<Image> decode_png(std::span<const std::uint8_t> bytes, std::string& error) {
    if (bytes.empty()) {
        error = "The image file is empty.";
        return std::nullopt;
    }
    SDL_IOStream* stream = SDL_IOFromConstMem(bytes.data(), bytes.size());
    SDL_Surface* loaded = stream == nullptr ? nullptr : SDL_LoadPNG_IO(stream, true);
    if (loaded == nullptr) {
        error = std::string("The image is not a valid PNG: ") + SDL_GetError();
        return std::nullopt;
    }
    SDL_Surface* rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (rgba == nullptr) {
        error = std::string("The image could not be converted to RGBA: ") + SDL_GetError();
        return std::nullopt;
    }
    Image image;
    image.width = static_cast<std::uint32_t>(rgba->w);
    image.height = static_cast<std::uint32_t>(rgba->h);
    const std::size_t row = std::size_t{image.width} * 4;
    image.rgba.resize(row * image.height);
    const auto* source = static_cast<const std::uint8_t*>(rgba->pixels);
    for (std::uint32_t y = 0; y < image.height; ++y) {
        std::copy_n(source + static_cast<std::size_t>(y) * static_cast<std::size_t>(rgba->pitch),
                    row, image.rgba.begin() + static_cast<std::ptrdiff_t>(y * row));
    }
    SDL_DestroySurface(rgba);
    return image;
}

std::vector<std::uint8_t> encode_png(const Image& image) {
    if (image.width == 0 || image.height == 0 ||
        image.rgba.size() != std::size_t{image.width} * image.height * 4) {
        return {};
    }
    // SDL only reads the pixels, but its API takes a non-const pointer.
    std::vector<std::uint8_t> pixels = image.rgba;
    SDL_Surface* surface = SDL_CreateSurfaceFrom(
        static_cast<int>(image.width), static_cast<int>(image.height), SDL_PIXELFORMAT_RGBA32,
        pixels.data(), static_cast<int>(image.width * 4));
    SDL_IOStream* stream = SDL_IOFromDynamicMem();
    std::vector<std::uint8_t> out;
    if (surface != nullptr && stream != nullptr && SDL_SavePNG_IO(surface, stream, false)) {
        const auto* data = static_cast<const std::uint8_t*>(SDL_GetPointerProperty(
            SDL_GetIOProperties(stream), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr));
        const Sint64 size = SDL_GetIOSize(stream);
        if (data != nullptr && size > 0) {
            out.assign(data, data + size);
        }
    }
    if (stream != nullptr) {
        SDL_CloseIO(stream);
    }
    if (surface != nullptr) {
        SDL_DestroySurface(surface);
    }
    return out;
}

} // namespace engine::assets
