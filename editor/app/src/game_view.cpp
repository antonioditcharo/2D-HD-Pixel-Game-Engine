#include "game_view.h"

#include <QKeyEvent>
#include <QPainter>
#include <QTimer>
#include <algorithm>
#include <array>
#include <utility>
#include <vector>

namespace editor {

namespace game = engine::game;
namespace input = engine::input;

std::optional<input::Key> engine_key(int qt_key) {
    if (qt_key >= Qt::Key_A && qt_key <= Qt::Key_Z) {
        return static_cast<input::Key>(static_cast<int>(input::Key::A) + (qt_key - Qt::Key_A));
    }
    if (qt_key >= Qt::Key_0 && qt_key <= Qt::Key_9) {
        return static_cast<input::Key>(static_cast<int>(input::Key::Num0) + (qt_key - Qt::Key_0));
    }
    if (qt_key >= Qt::Key_F1 && qt_key <= Qt::Key_F12) {
        return static_cast<input::Key>(static_cast<int>(input::Key::F1) + (qt_key - Qt::Key_F1));
    }
    static constexpr std::array<std::pair<int, input::Key>, 12> others{{
        {Qt::Key_Up, input::Key::Up},
        {Qt::Key_Down, input::Key::Down},
        {Qt::Key_Left, input::Key::Left},
        {Qt::Key_Right, input::Key::Right},
        {Qt::Key_Space, input::Key::Space},
        {Qt::Key_Return, input::Key::Enter},
        {Qt::Key_Enter, input::Key::Enter},
        {Qt::Key_Escape, input::Key::Escape},
        {Qt::Key_Tab, input::Key::Tab},
        {Qt::Key_Backspace, input::Key::Backspace},
        {Qt::Key_Shift, input::Key::LeftShift},
        {Qt::Key_Control, input::Key::LeftCtrl},
    }};
    for (const auto& [qt, key] : others) {
        if (qt == qt_key) {
            return key;
        }
    }
    if (qt_key == Qt::Key_Alt) {
        return input::Key::LeftAlt;
    }
    return std::nullopt;
}

GameView::GameView(QWidget* parent) : QWidget(parent), timer_(new QTimer(this)) {
    setObjectName(QStringLiteral("game_view"));
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(false);
    timer_->setInterval(16);
    connect(timer_, &QTimer::timeout, this, [this] {
        const double seconds = static_cast<double>(clock_.restart()) / 1000.0;
        // A long pause (the window dragged, a breakpoint) is not caught up all at once.
        advance(std::min(seconds, 0.25));
    });
}

GameView::~GameView() = default;

bool GameView::start(const std::filesystem::path& folder, bool sound, std::string& error) {
    stop();
    keys_ = input::Recording{};
    held_.clear();
    game::RuntimeOptions options;
    options.headless = true;
    options.debug = true; // F1, F2, F3, as when testing in the player
    options.replay = &keys_;
    options.save_folder = folder / ".sparxel" / "test-saves";
    options.audio = sound ? game::AudioMode::Device : game::AudioMode::Headless;
    std::vector<engine::serialize::Issue> issues;
    runtime_ = game::Runtime::open(folder, options, issues);
    if (runtime_ == nullptr) {
        error = issues.empty() ? std::string("The game could not start.")
                               : issues.front().path + ": " + issues.front().message;
        return false;
    }
    clock_.start();
    timer_->start();
    setFocus();
    advance(0.0);
    return true;
}

void GameView::stop() {
    timer_->stop();
    runtime_.reset();
    frame_ = QImage();
    update();
}

void GameView::advance(double seconds) {
    if (runtime_ == nullptr) {
        return;
    }
    if (!runtime_->frame(seconds)) {
        stop();
        Q_EMIT ended();
        return;
    }
    if (engine::render::RenderTarget* target = runtime_->target()) {
        const std::vector<std::uint8_t> pixels = target->read_pixels();
        const auto w = static_cast<int>(target->width());
        const auto h = static_cast<int>(target->height());
        if (pixels.size() == static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4) {
            frame_ =
                QImage(pixels.data(), w, h, static_cast<qsizetype>(w) * 4, QImage::Format_RGBA8888)
                    .copy();
        }
    }
    update();
}

void GameView::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    if (frame_.isNull()) {
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, tr("Game > Play in the editor (F6)"));
        return;
    }
    // Whole-number scale, so pixel art stays crisp, centred.
    const int scale = std::max(1, std::min(width() / frame_.width(), height() / frame_.height()));
    const QSize size = frame_.size() * scale;
    const QPoint at((width() - size.width()) / 2, (height() - size.height()) / 2);
    painter.drawImage(QRect(at, size), frame_);
    if (!hasFocus()) {
        painter.setPen(QColor(255, 210, 122));
        painter.drawText(rect().adjusted(8, 8, -8, -8), Qt::AlignTop | Qt::AlignLeft,
                         tr("Click to play with the keyboard"));
    }
}

void GameView::press(int qt_key, bool down) {
    const std::optional<input::Key> key = engine_key(qt_key);
    if (runtime_ == nullptr || !key) {
        return;
    }
    const auto at = std::find(held_.begin(), held_.end(), *key);
    if (down == (at != held_.end())) {
        return; // already so
    }
    if (down) {
        held_.push_back(*key);
    } else {
        held_.erase(at);
    }
    // For the next step the runtime takes.
    keys_.record(runtime_->step_count(), input::Binding::key(*key), down);
}

void GameView::keyPressEvent(QKeyEvent* event) {
    if (!event->isAutoRepeat()) {
        press(event->key(), true);
    }
    event->accept();
}

void GameView::keyReleaseEvent(QKeyEvent* event) {
    if (!event->isAutoRepeat()) {
        press(event->key(), false);
    }
    event->accept();
}

void GameView::focusOutEvent(QFocusEvent* event) {
    // Nothing stays held while the keyboard is elsewhere.
    for (const input::Key key : std::vector<input::Key>(held_)) {
        keys_.record(runtime_ != nullptr ? runtime_->step_count() : 0, input::Binding::key(key),
                     false);
    }
    held_.clear();
    update();
    QWidget::focusOutEvent(event);
}

} // namespace editor
