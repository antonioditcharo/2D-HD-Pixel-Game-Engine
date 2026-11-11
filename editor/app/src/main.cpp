// sparxel-editor: the editor. Opens the project folder given, if any.
//
//   sparxel-editor [project folder]
//
// See docs/dev/editor.md.

#include <QApplication>
#include <QDir>
#include <filesystem>

#include "main_window.h"

namespace {

std::filesystem::path path_of(const QString& text) {
    const QByteArray utf8 = text.toUtf8();
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

} // namespace

int main(int argc, char** argv) {
    const QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Sparxel Editor"));
    // The player beside the editor, as installed; else the one this build made.
    std::filesystem::path player = path_of(QApplication::applicationDirPath()) / "sparxel-player";
#ifdef _WIN32
    player += ".exe";
#endif
    if (!std::filesystem::exists(player)) {
        player = SPARXEL_DEFAULT_PLAYER;
    }
    editor::MainWindow window(player);
    const QStringList arguments = QApplication::arguments();
    if (arguments.size() > 1) {
        window.open_project(path_of(arguments.at(1)));
    }
    window.show();
    return QApplication::exec();
}
