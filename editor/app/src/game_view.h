#pragma once

// The Game view (plan/03 §8): the project played inside the editor's window, as engines
// let you test without leaving them. The runtime runs offscreen on a timer; its frames are
// shown pixel-perfect, scaled by whole numbers; the keys pressed here are its keyboard.
//
// See docs/dev/editor.md (Playing in the editor).

#include "engine/game/runtime.h"
#include "engine/input/input.h"

#include <QElapsedTimer>
#include <QImage>
#include <QWidget>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

class QTimer;

namespace editor {

/// The engine's key for a Qt key, if it has one.
[[nodiscard]] std::optional<engine::input::Key> engine_key(int qt_key);

class GameView final : public QWidget {
    Q_OBJECT

public:
    explicit GameView(QWidget* parent = nullptr);
    ~GameView() override;
    GameView(const GameView&) = delete;
    GameView& operator=(const GameView&) = delete;
    GameView(GameView&&) = delete;
    GameView& operator=(GameView&&) = delete;

    /// Starts the project at `folder` (as saved), its saves kept in `.sparxel/test-saves`.
    /// With `sound` off (tests), it plays silently. False, with `error`, if it cannot run.
    bool start(const std::filesystem::path& folder, bool sound, std::string& error);
    /// Stops it.
    void stop();
    [[nodiscard]] bool playing() const noexcept { return runtime_ != nullptr; }
    /// Runs `seconds` of the game at once, then shows it (the timer does this as time
    /// passes).
    void advance(double seconds);
    /// The last frame shown.
    [[nodiscard]] const QImage& frame() const noexcept { return frame_; }
    [[nodiscard]] engine::game::Runtime* runtime() const noexcept { return runtime_.get(); }

Q_SIGNALS:
    /// The game asked to quit (its title or pause menu), or could not go on.
    void ended();

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    void press(int qt_key, bool down);

    std::unique_ptr<engine::game::Runtime> runtime_;
    engine::input::Recording keys_; ///< What is pressed here, by step, for the runtime.
    std::vector<engine::input::Key> held_;
    QTimer* timer_ = nullptr;
    QElapsedTimer clock_;
    QImage frame_;
};

} // namespace editor
