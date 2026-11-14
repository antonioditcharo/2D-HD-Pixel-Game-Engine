// Exporting: the executable's name, exactly what is written, and every refusal, which
// leaves nothing behind. The sample is exported from a copy, so the repository is never
// written to. tests/exporter/export_and_play.cmake then plays an exported game.

#include "engine/foundation/guid.h"
#include "engine/game/runtime.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "editor/exporter/exporter.h"

namespace fs = std::filesystem;
using engine::serialize::Issue;

namespace {

struct TempFolder {
    TempFolder() {
        path =
            fs::temp_directory_path() / ("sparxel-export-" + engine::Guid::generate().to_string());
        fs::create_directories(path);
    }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;
    TempFolder(TempFolder&&) = delete;
    TempFolder& operator=(TempFolder&&) = delete;
    ~TempFolder() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
    fs::path path;
};

std::string read(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string messages(const std::vector<Issue>& issues) {
    std::string text;
    for (const Issue& issue : issues) {
        text += issue.path + ": " + issue.message + "\n";
    }
    return text;
}

/// A copy of the sample, and options exporting it into `out` beside it.
struct Setup {
    Setup() {
        fs::copy(fs::path(SPARXEL_SAMPLES_DIR) / "hello", project, fs::copy_options::recursive);
        options.project = project;
        options.player = SPARXEL_PLAYER;
        options.output = folder.path / "out";
    }
    TempFolder folder;
    fs::path project = folder.path / "hello";
    editor::ExportOptions options;
};

} // namespace

TEST_CASE("The executable is named after the game, safely") {
    CHECK(editor::executable_name("Hello Sparxel") == "Hello Sparxel");
    CHECK(editor::executable_name("Who? Me: <the/hero>|*\"") == "Who Me thehero");
    CHECK(editor::executable_name("  Quest.. ") == "Quest");
    CHECK(editor::executable_name("Caf\xC3\xA9") == "Caf\xC3\xA9");
    CHECK(editor::executable_name("tab\there") == "tabhere");
    for (const char* reserved : {"con", "NUL", "Com3", "lpt9", "", "???", " . "}) {
        CAPTURE(reserved);
        CHECK(editor::executable_name(reserved) == "game");
    }
    CHECK(editor::executable_name("COM0") == "COM0");
    CHECK(editor::executable_name("Console") == "Console");
}

TEST_CASE("Exporting the sample writes the player, its licences and the game, nothing else") {
    const Setup s;
    std::ofstream(s.project / "assets" / ".notes") << "private";
    fs::create_directories(s.project / "assets" / ".git");
    std::ofstream(s.project / "assets" / ".git" / "HEAD") << "ref";
    std::ofstream(s.project / "todo.txt") << "outside assets/";
    // A Sprite Studio source, which the game never reads: its sheet goes, it does not.
    std::ofstream(s.project / "assets" / "art" / "hero.dsprite") << "{}";
    std::vector<Issue> issues;
    const std::optional<editor::ExportReport> exported = editor::export_game(s.options, issues);
    INFO(messages(issues));
    REQUIRE(exported.has_value());
    CHECK(issues.empty());
    const editor::ExportReport report = exported.value_or(editor::ExportReport{});

    const std::string exe = "Hello Sparxel" + fs::path(SPARXEL_PLAYER).extension().string();
    CHECK(report.executable == s.options.output / exe);
    std::vector<std::string> written;
    written.reserve(report.files.size());
    for (const fs::path& file : report.files) {
        written.push_back(file.generic_string());
    }
    std::vector<std::string> expected{exe, "LICENSE.txt", "THIRD_PARTY_NOTICES.md",
                                      "game/project.sparxel.json"};
    for (const char* asset : {"art/elder.png", "art/hero.png", "art/tiles.png", "logic/main.drules",
                              "logic/road.drules", "maps/road.dscene", "maps/village.dscene",
                              "sfx/chime.wav", "sfx/stream.wav"}) {
        expected.push_back(std::string("game/assets/") + asset);
        expected.push_back(std::string("game/assets/") + asset + ".meta");
    }
    // Shared libraries beside the player (on Windows, SDL3.dll) go too.
    for (const fs::directory_entry& entry :
         fs::directory_iterator(fs::path(SPARXEL_PLAYER).parent_path())) {
        if (entry.path().extension() == ".dll") {
            expected.push_back(entry.path().filename().string());
        }
    }
    std::sort(written.begin(), written.end());
    std::sort(expected.begin(), expected.end());
    CHECK(written == expected);

    // Every file is there, byte for byte, and nothing else is.
    std::size_t on_disk = 0;
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(s.options.output)) {
        on_disk += entry.is_regular_file() ? 1 : 0;
    }
    CHECK(on_disk == expected.size());
    CHECK(read(report.executable) == read(SPARXEL_PLAYER));
    CHECK(read(s.options.output / "game/assets/logic/main.drules") ==
          read(s.project / "assets/logic/main.drules"));
    CHECK_FALSE(fs::exists(s.options.output / "game/assets/.notes"));
    CHECK_FALSE(fs::exists(s.options.output / "game/assets/.git"));
    CHECK_FALSE(fs::exists(s.options.output / "game/todo.txt"));
    CHECK_FALSE(fs::exists(s.options.output / "game/assets/art/hero.dsprite"));
    // The exported project opens, as the player will open it.
    engine::game::RuntimeOptions check;
    check.graphics = false;
    check.audio = engine::game::AudioMode::Off;
    issues.clear();
    CHECK(engine::game::Runtime::open(s.options.output / "game", check, issues) != nullptr);
    CHECK(issues.empty());
}

TEST_CASE("A game with a name beyond ASCII gets an executable of that name") {
    const Setup s;
    std::string project = read(s.project / engine::game::project_file);
    const std::string from = "\"Hello Sparxel\"";
    project.replace(project.find(from), from.size(), "\"Caf\xC3\xA9 Quest\"");
    std::ofstream(s.project / engine::game::project_file, std::ios::binary) << project;
    std::vector<Issue> issues;
    const std::optional<editor::ExportReport> exported = editor::export_game(s.options, issues);
    INFO(messages(issues));
    REQUIRE(exported.has_value());
    const editor::ExportReport report = exported.value_or(editor::ExportReport{});
    const std::u8string expected =
        u8"Caf\u00e9 Quest" + fs::path(SPARXEL_PLAYER).extension().u8string();
    CHECK(report.executable.filename().u8string() == expected);
    CHECK(fs::is_regular_file(s.options.output / fs::path(expected)));
}

TEST_CASE("Exporting is refused, writing nothing, when something is wrong") {
    const auto refused = [](const editor::ExportOptions& options, const std::string& message) {
        std::vector<Issue> issues;
        CHECK_FALSE(editor::export_game(options, issues).has_value());
        INFO(messages(issues));
        REQUIRE_FALSE(issues.empty());
        CHECK(issues.back().message.find(message) != std::string::npos);
    };

    SUBCASE("An output folder that is not empty") {
        const Setup s;
        fs::create_directories(s.options.output);
        std::ofstream(s.options.output / "mine.txt") << "keep me";
        refused(s.options, "must be new or empty");
        CHECK(read(s.options.output / "mine.txt") == "keep me");
        CHECK(std::distance(fs::directory_iterator(s.options.output), fs::directory_iterator()) ==
              1);
    }
    SUBCASE("An output folder inside the project") {
        Setup s;
        s.options.output = s.project / "build";
        refused(s.options, "must not be inside the project");
        CHECK_FALSE(fs::exists(s.options.output));
    }
    SUBCASE("No player") {
        Setup s;
        s.options.player = s.folder.path / "nowhere" / "sparxel-player";
        refused(s.options, "was not found");
        CHECK_FALSE(fs::exists(s.options.output));
    }
    SUBCASE("A player without its licence files") {
        Setup s;
        fs::create_directories(s.folder.path / "bare");
        const fs::path bare = s.folder.path / "bare" / fs::path(SPARXEL_PLAYER).filename();
        fs::copy_file(SPARXEL_PLAYER, bare);
        s.options.player = bare;
        refused(s.options, "must be beside the player");
        CHECK_FALSE(fs::exists(s.options.output));
    }
    SUBCASE("A game that cannot start") {
        const Setup s;
        fs::remove(s.project / "assets/maps/village.dscene");
        refused(s.options, "The game cannot start");
        CHECK_FALSE(fs::exists(s.options.output));
    }
    SUBCASE("A project with any issue, even an unknown setting") {
        const Setup s;
        std::string project = read(s.project / engine::game::project_file);
        project.insert(project.find('{') + 1, "\n  \"shiny\": true,");
        std::ofstream(s.project / engine::game::project_file, std::ios::binary) << project;
        refused(s.options, "Fix these problems before exporting");
        CHECK_FALSE(fs::exists(s.options.output));
    }
    SUBCASE("No output folder") {
        Setup s;
        s.options.output.clear();
        refused(s.options, "Choose a folder");
    }
}
