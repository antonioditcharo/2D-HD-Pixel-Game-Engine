// Runs the editor's tests inside a QApplication, on the offscreen platform unless another
// is chosen, so they need no display.

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <QApplication>

int main(int argc, char** argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    const QApplication app(argc, argv);
    doctest::Context context(argc, argv);
    return context.run();
}
