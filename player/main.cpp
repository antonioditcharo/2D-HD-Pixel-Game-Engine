// sparxel-player: opens a Sparxel project folder and plays it. This is what an exported
// game runs, and what the editor's Play button will start.
//
//   sparxel-player [project folder] [--headless] [--frames N] [--no-audio]
//                  [--screenshot file.png]
//
// With no folder, it plays the folder it is started in. Lines that logic writes with Log
// blocks are printed, and so are problems, so creators see them while testing.
//
// See docs/dev/player.md.

#include "engine/game/runtime.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace game = engine::game;

struct Arguments {
    fs::path project;
    bool headless = false;
    bool audio = true;
    std::optional<std::uint64_t> frames; ///< Stop after this many frames.
    fs::path screenshot;                 ///< Where to save the last frame, if anywhere.
};

void usage() {
    std::fputs("Usage: sparxel-player [project folder] [--headless] [--frames N] [--no-audio]\n"
               "                      [--screenshot file.png]\n"
               "  --headless   draw without a window (needs no display), at 60 frames a second\n"
               "               of game time, as fast as the computer can\n"
               "  --frames N   stop after N frames\n"
               "  --no-audio   play without sound\n"
               "  --screenshot file.png\n"
               "               save the last frame drawn as a PNG\n",
               stderr);
}

std::optional<Arguments> parse(int argc, char** argv) {
    Arguments args;
    args.project = fs::current_path();
    bool have_project = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--headless") {
            args.headless = true;
        } else if (arg == "--no-audio") {
            args.audio = false;
        } else if (arg == "--frames" && i + 1 < argc) {
            try {
                args.frames = std::stoull(argv[++i]);
            } catch (const std::exception&) {
                return std::nullopt;
            }
        } else if (arg == "--screenshot" && i + 1 < argc) {
            args.screenshot = fs::path(argv[++i]);
        } else if (arg == "--help" || arg == "-h" || arg.starts_with("--") || have_project) {
            return std::nullopt;
        } else {
            args.project = fs::path(arg);
            have_project = true;
        }
    }
    return args;
}

/// Prints the lines added to `lines` since `printed`, and moves `printed` on.
void print_new(const std::vector<std::string>& lines, std::size_t& printed, std::FILE* to,
               const char* prefix) {
    for (; printed < lines.size(); ++printed) {
        std::fprintf(to, "%s%s\n", prefix, lines[printed].c_str());
    }
}

} // namespace

int main(int argc, char** argv) {
    const std::optional<Arguments> args = parse(argc, argv);
    if (!args) {
        usage();
        return 2;
    }
    game::RuntimeOptions options;
    options.headless = args->headless;
    options.audio = args->audio
                        ? (args->headless ? game::AudioMode::Headless : game::AudioMode::Device)
                        : game::AudioMode::Off;
    std::vector<engine::serialize::Issue> issues;
    const std::unique_ptr<game::Runtime> runtime =
        game::Runtime::open(args->project, options, issues);
    for (const engine::serialize::Issue& issue : issues) {
        std::fprintf(stderr, "%s: %s\n", issue.path.empty() ? "project" : issue.path.c_str(),
                     issue.message.c_str());
    }
    if (runtime == nullptr) {
        std::fputs("The game cannot start.\n", stderr);
        return 1;
    }

    std::size_t logged = 0;
    std::size_t problems = 0;
    std::uint64_t frame = 0;
    auto last = std::chrono::steady_clock::now();
    for (;;) {
        // Headless runs use game time, so a run of N frames always plays the same.
        double seconds = 1.0 / 60.0;
        if (!args->headless) {
            const auto now = std::chrono::steady_clock::now();
            seconds = std::chrono::duration<double>(now - last).count();
            last = now;
        }
        const bool keep_running = runtime->frame(seconds);
        print_new(runtime->log(), logged, stdout, "");
        print_new(runtime->problems(), problems, stderr, "problem: ");
        ++frame;
        if (!keep_running || (args->frames && frame >= *args->frames)) {
            break;
        }
    }
    std::fflush(stdout);
    if (!args->screenshot.empty()) {
        engine::render::RenderTarget* target = runtime->target();
        engine::assets::Image image;
        if (target != nullptr) {
            image.width = target->width();
            image.height = target->height();
            image.rgba = target->read_pixels();
        }
        const std::vector<std::uint8_t> png =
            image.rgba.empty() ? std::vector<std::uint8_t>{} : engine::assets::encode_png(image);
        std::ofstream out(args->screenshot, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(png.data()),
                  static_cast<std::streamsize>(png.size()));
        if (png.empty() || !out) {
            std::fprintf(stderr, "The screenshot could not be saved to %s.\n",
                         args->screenshot.string().c_str());
            return 1;
        }
    }
    return 0;
}
