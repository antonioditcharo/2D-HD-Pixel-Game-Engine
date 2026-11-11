// sparxel-export: exports a project as a game folder that plays when its executable is
// opened.
//
//   sparxel-export <project folder> <output folder> [--player path/to/sparxel-player]
//
// See docs/dev/export.md.

#include <cstdio>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

#include "editor/exporter/exporter.h"

namespace {

int run(int argc, char** argv) {
    editor::ExportOptions options;
    options.player = SPARXEL_DEFAULT_PLAYER;
    std::vector<std::string_view> folders;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--player" && i + 1 < argc) {
            options.player = argv[++i];
        } else if (arg.starts_with("--")) {
            folders.clear();
            break;
        } else {
            folders.push_back(arg);
        }
    }
    if (folders.size() != 2) {
        std::fputs("Usage: sparxel-export <project folder> <output folder> "
                   "[--player path/to/sparxel-player]\n",
                   stderr);
        return 2;
    }
    options.project = folders[0];
    options.output = folders[1];
    std::vector<engine::serialize::Issue> issues;
    const std::optional<editor::ExportReport> report = editor::export_game(options, issues);
    for (const engine::serialize::Issue& issue : issues) {
        std::fprintf(stderr, "%s%s%s\n", issue.path.c_str(), issue.path.empty() ? "" : ": ",
                     issue.message.c_str());
    }
    if (!report.has_value()) {
        std::fputs("The game was not exported.\n", stderr);
        return 1;
    }
    const editor::ExportReport done = report.value_or(editor::ExportReport{});
    const std::u8string executable = done.executable.u8string();
    std::printf("Exported %zu files. Play the game with %s\n", done.files.size(),
                std::string(executable.begin(), executable.end()).c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "The game was not exported: %s\n", error.what());
    } catch (...) {
        std::fputs("The game was not exported: an unexpected error.\n", stderr);
    }
    return 1;
}
