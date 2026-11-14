// sparxel-editor: the editor. Opens the project folder given, if any.
//
//   sparxel-editor [project folder] [--check]
//
// See docs/dev/editor.md.

#include <QApplication>
#include <QDir>
#include <QSettings>
#include <cstdio>
#include <exception>
#include <filesystem>

#include "main_window.h"

namespace {

std::filesystem::path path_of(const QString& text) {
    const QByteArray utf8 = text.toUtf8();
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

} // namespace

namespace {

int run(int argc, char** argv) {
    const QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Sparxel"));
    QApplication::setApplicationName(QStringLiteral("Sparxel Editor"));
    // The player packaged with the editor (in its own folder, so exported games get only
    // the player's libraries), or beside it; else the one this build made.
    const std::filesystem::path here = path_of(QApplication::applicationDirPath());
#ifdef _WIN32
    const char* const name = "sparxel-player.exe";
#else
    const char* const name = "sparxel-player";
#endif
    std::filesystem::path player = here / "player" / name;
    if (!std::filesystem::exists(player)) {
        player = here / name;
    }
    if (!std::filesystem::exists(player)) {
        player = SPARXEL_DEFAULT_PLAYER;
    }
    editor::MainWindow window(player);
    // The recent projects and the panels' places are kept in the user's settings (the
    // registry on Windows).
    QSettings settings;
    window.set_settings(&settings);
    // The sample village, packaged beside the editor, to try from the start page.
    const std::filesystem::path sample = here / "samples" / "hello";
    std::error_code missing;
    if (std::filesystem::exists(sample / "project.sparxel.json", missing)) {
        window.set_sample(sample);
    }
    QStringList arguments = QApplication::arguments();
    // --check: start, open the project given (if any) and leave at once, saying by the
    // exit code whether that worked. For checking a packaged editor without a person.
    const bool check = arguments.removeAll(QStringLiteral("--check")) > 0;
    window.set_quiet(check);
    bool opened = true;
    if (arguments.size() > 1) {
        opened = window.open_project(path_of(arguments.at(1)));
    }
    if (check) {
        return opened && std::filesystem::exists(player) ? 0 : 1;
    }
    // Where the panels were last time.
    window.restore_layout(settings);
    window.show();
    const int result = QApplication::exec();
    window.save_layout(settings);
    return result;
}

} // namespace

int main(int argc, char** argv) {
    // Nothing the program does should throw past here; if it does, say so and stop.
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::fputs(e.what(), stderr);
        std::fputs("\n", stderr);
    } catch (...) {
        std::fputs("An unexpected error stopped the editor.\n", stderr);
    }
    return 1;
}
