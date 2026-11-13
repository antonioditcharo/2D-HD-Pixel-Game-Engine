#include "editor/exporter/exporter.h"

#include "engine/game/runtime.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <memory>
#include <system_error>
#include <utility>

namespace editor {

namespace fs = std::filesystem;
namespace game = engine::game;
using engine::serialize::Issue;

namespace {

/// The licence files that go everywhere the player goes.
constexpr std::array<std::string_view, 2> licence_files{"LICENSE.txt", "THIRD_PARTY_NOTICES.md"};

/// A path as UTF-8 text, on every system (Windows' narrow strings are not UTF-8).
std::string utf8(const fs::path& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

/// A path from UTF-8 text.
fs::path from_utf8(std::string_view text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

/// Shared libraries the player needs at run time sit beside it.
bool is_shared_library(const fs::path& file) {
    const std::string name = utf8(file.filename());
    const std::string extension = utf8(file.extension());
    return extension == ".dll" || extension == ".dylib" || extension == ".so" ||
           name.find(".so.") != std::string::npos;
}

/// True if `inner` is `outer` or inside it, comparing the real paths.
bool is_within(const fs::path& inner, const fs::path& outer) {
    std::error_code error;
    const fs::path a = fs::weakly_canonical(inner, error);
    if (error) {
        return false;
    }
    const fs::path b = fs::weakly_canonical(outer, error);
    if (error) {
        return false;
    }
    auto [end_b, end_a] = std::mismatch(b.begin(), b.end(), a.begin(), a.end());
    static_cast<void>(end_a);
    return end_b == b.end();
}

class Writer {
public:
    Writer(fs::path output, std::vector<Issue>& issues)
        : output_(std::move(output)), issues_(issues) {}

    /// Copies `from` to `relative` inside the output. Reports and returns false on failure.
    bool copy(const fs::path& from, const fs::path& relative) {
        std::error_code error;
        const fs::path to = output_ / relative;
        fs::create_directories(to.parent_path(), error);
        if (!error) {
            fs::copy_file(from, to, fs::copy_options::overwrite_existing, error);
        }
        if (error) {
            issues_.push_back(Issue{utf8(relative.generic_u8string()),
                                    "Could not copy " + utf8(from) + ": " + error.message()});
            return false;
        }
        files_.push_back(relative);
        return true;
    }

    [[nodiscard]] std::vector<fs::path> take_files() { return std::move(files_); }

private:
    fs::path output_;
    std::vector<Issue>& issues_;
    std::vector<fs::path> files_;
};

} // namespace

std::string executable_name(std::string_view game_name) {
    std::string name;
    for (const char c : game_name) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 32 || std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos) {
            continue;
        }
        name += c;
    }
    // Windows drops trailing dots and spaces; leading spaces only confuse.
    while (!name.empty() && (name.back() == '.' || name.back() == ' ')) {
        name.pop_back();
    }
    const std::size_t first = name.find_first_not_of(' ');
    name = first == std::string::npos ? std::string() : name.substr(first);
    std::string upper;
    for (const char c : name) {
        upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    constexpr std::array<std::string_view, 6> reserved{"CON", "PRN", "AUX", "NUL", "COM", "LPT"};
    const bool is_reserved =
        std::any_of(reserved.begin(), reserved.end(), [&upper](std::string_view r) {
            if (upper == r) {
                return true;
            }
            // COM1 to COM9 and LPT1 to LPT9.
            return (r == "COM" || r == "LPT") && upper.size() == 4 && upper.starts_with(r) &&
                   upper[3] >= '1' && upper[3] <= '9';
        });
    return name.empty() || is_reserved ? std::string("game") : name;
}

std::optional<ExportReport> export_game(const ExportOptions& options, std::vector<Issue>& issues) {
    const std::size_t issues_before = issues.size();
    const auto refuse = [&issues](std::string path, std::string message) {
        issues.push_back(Issue{std::move(path), std::move(message)});
        return std::nullopt;
    };
    std::error_code error;

    // 1. Where it goes.
    if (options.output.empty()) {
        return refuse("", "Choose a folder to export the game to.");
    }
    if (fs::exists(options.output, error)) {
        if (!fs::is_directory(options.output, error) || !fs::is_empty(options.output, error)) {
            return refuse("", "The export folder " + utf8(options.output) +
                                  " must be new or empty, so nothing in it is overwritten.");
        }
    }
    if (is_within(options.output, options.project)) {
        return refuse("", "The export folder must not be inside the project.");
    }

    // 2. What is shipped with the game: the player, its libraries, its licence files.
    if (!fs::is_regular_file(options.player, error)) {
        return refuse("", "The player " + utf8(options.player) + " was not found.");
    }
    const fs::path player_folder = options.player.parent_path();
    for (const std::string_view licence : licence_files) {
        if (!fs::is_regular_file(player_folder / licence, error)) {
            return refuse("", "The licence file " + std::string(licence) +
                                  " must be beside the player: the engine's licence asks "
                                  "every copy to carry it.");
        }
    }

    // 3. The project must open as the player would open it, with no issue at all.
    {
        game::RuntimeOptions check;
        check.graphics = false;
        check.audio = game::AudioMode::Off;
        std::vector<Issue> found;
        const std::unique_ptr<game::Runtime> runtime =
            game::Runtime::open(options.project, check, found);
        if (runtime == nullptr || !found.empty()) {
            issues.insert(issues.end(), found.begin(), found.end());
            if (runtime != nullptr || found.empty()) {
                issues.push_back(Issue{"", "Fix these problems before exporting."});
            } else {
                issues.push_back(Issue{"", "The game cannot start. Fix these problems "
                                           "before exporting."});
            }
            return std::nullopt;
        }
    }
    std::vector<Issue> settings_issues;
    const std::optional<game::ProjectSettings> settings =
        game::read_project(options.project, settings_issues);
    if (!settings) {
        issues.insert(issues.end(), settings_issues.begin(), settings_issues.end());
        return std::nullopt;
    }

    // 4. Copy. A failure removes what was written, so no half-exported game is left.
    const bool created = !fs::exists(options.output, error);
    fs::create_directories(options.output, error);
    if (error) {
        return refuse("", "Could not create the export folder " + utf8(options.output) + ": " +
                              error.message());
    }
    Writer writer(options.output, issues);
    const fs::path executable =
        from_utf8(executable_name(settings->name)).concat(options.player.extension().u8string());
    bool ok = writer.copy(options.player, executable);
    for (const fs::directory_entry& entry : fs::directory_iterator(player_folder, error)) {
        if (ok && entry.is_regular_file(error) && is_shared_library(entry.path())) {
            ok = writer.copy(entry.path(), entry.path().filename());
        }
    }
    for (const std::string_view licence : licence_files) {
        ok = ok && writer.copy(player_folder / licence, fs::path(licence));
    }
    ok = ok &&
         writer.copy(options.project / game::project_file, fs::path("game") / game::project_file);
    // The assets, with their .meta files (which hold the GUIDs everything refers to);
    // hidden files and folders, such as version control's, are left out.
    const fs::path assets = options.project / "assets";
    if (ok && fs::is_directory(assets, error)) {
        std::vector<fs::path> sources;
        for (auto it = fs::recursive_directory_iterator(assets, error);
             !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
            const std::string name = utf8(it->path().filename());
            if (name.starts_with('.')) {
                if (it->is_directory(error)) {
                    it.disable_recursion_pending();
                }
                continue;
            }
            // Sprite Studio's sources (.dsprite, with their .meta) stay with the project:
            // the game draws the sprite sheet saved beside each.
            if (name.ends_with(".dsprite") || name.ends_with(".dsprite.meta")) {
                continue;
            }
            if (it->is_regular_file(error)) {
                sources.push_back(it->path());
            }
        }
        if (error) {
            ok = false;
            issues.push_back(
                Issue{"assets", "Could not read the project's assets: " + error.message()});
        }
        std::sort(sources.begin(), sources.end());
        for (const fs::path& source : sources) {
            if (!ok) {
                break;
            }
            ok = writer.copy(source,
                             fs::path("game") / "assets" / fs::relative(source, assets, error));
        }
    }
    if (!ok) {
        const std::vector<fs::path> written = writer.take_files();
        std::error_code ignored;
        if (created) {
            fs::remove_all(options.output, ignored);
        } else {
            for (const fs::path& file : written) {
                fs::remove(options.output / file, ignored);
            }
        }
        if (issues.size() == issues_before) {
            issues.push_back(Issue{"", "The export failed."});
        }
        return std::nullopt;
    }
    // The player must stay runnable on systems with permission bits.
    fs::permissions(options.output / executable,
                    fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add, error);
    ExportReport report;
    report.executable = options.output / executable;
    report.files = writer.take_files();
    return report;
}

} // namespace editor
