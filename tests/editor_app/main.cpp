// Runs the editor's tests inside a QApplication, on the offscreen platform unless another
// is chosen, so they need no display.

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <QApplication>
#include <cstdio>
#include <exception>

namespace {

int run(int argc, char** argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    const QApplication app(argc, argv);
    doctest::Context context(argc, argv);
    return context.run();
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
        std::fputs("An unexpected error stopped the tests.\n", stderr);
    }
    return 1;
}
