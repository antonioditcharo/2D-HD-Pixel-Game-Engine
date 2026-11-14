#include "sprite_studio.h"

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

namespace editor {

namespace fs = std::filesystem;
using sprite::Color;

namespace {

QString text_of(const std::string& s) {
    return QString::fromStdString(s);
}

QString path_text(const fs::path& path) {
    const std::u8string text = path.u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(text.data()), // NOLINT
                             static_cast<qsizetype>(text.size()));
}

QColor qcolor(Color c) {
    return {c.r, c.g, c.b, c.a};
}

Color color_of(const QColor& c) {
    return {static_cast<std::uint8_t>(c.red()), static_cast<std::uint8_t>(c.green()),
            static_cast<std::uint8_t>(c.blue()), static_cast<std::uint8_t>(c.alpha())};
}

QImage qimage(const sprite::Image& image) {
    if (image.rgba.empty()) {
        return {};
    }
    return QImage(image.rgba.data(), static_cast<int>(image.width), static_cast<int>(image.height),
                  static_cast<qsizetype>(image.width) * 4, QImage::Format_RGBA8888)
        .copy();
}

/// A small square of a colour over a checkerboard, so see-through colours show.
QIcon swatch(Color color, int size) {
    QPixmap pixmap(size, size);
    QPainter painter(&pixmap);
    const int half = size / 2;
    painter.fillRect(0, 0, size, size, QColor(204, 204, 204));
    painter.fillRect(0, 0, half, half, QColor(255, 255, 255));
    painter.fillRect(half, half, size - half, size - half, QColor(255, 255, 255));
    painter.fillRect(0, 0, size, size, qcolor(color));
    painter.setPen(QColor(40, 40, 40));
    painter.drawRect(0, 0, size - 1, size - 1);
    return QIcon(pixmap);
}

struct ToolInfo {
    StudioTool tool;
    const char* name;
    const char* tip;
    const char* key;
};

constexpr std::array<ToolInfo, 11> tool_infos{{
    {StudioTool::Pencil, QT_TRANSLATE_NOOP("SpriteStudio", "Pencil"),
     QT_TRANSLATE_NOOP("SpriteStudio", "Draw pixels (B)"), "B"},
    {StudioTool::Eraser, QT_TRANSLATE_NOOP("SpriteStudio", "Eraser"),
     QT_TRANSLATE_NOOP("SpriteStudio", "Clear pixels (E)"), "E"},
    {StudioTool::Line, QT_TRANSLATE_NOOP("SpriteStudio", "Line"),
     QT_TRANSLATE_NOOP("SpriteStudio", "Drag a straight line (L)"), "L"},
    {StudioTool::Rectangle, QT_TRANSLATE_NOOP("SpriteStudio", "Rectangle"),
     QT_TRANSLATE_NOOP("SpriteStudio", "Drag a rectangle's outline (U)"), "U"},
    {StudioTool::FilledRectangle, QT_TRANSLATE_NOOP("SpriteStudio", "Filled rectangle"),
     QT_TRANSLATE_NOOP("SpriteStudio", "Drag a filled rectangle (Shift+U)"), "Shift+U"},
    {StudioTool::Ellipse, QT_TRANSLATE_NOOP("SpriteStudio", "Ellipse"),
     QT_TRANSLATE_NOOP("SpriteStudio", "Drag an ellipse's outline (O)"), "O"},
    {StudioTool::FilledEllipse, QT_TRANSLATE_NOOP("SpriteStudio", "Filled ellipse"),
     QT_TRANSLATE_NOOP("SpriteStudio", "Drag a filled ellipse (Shift+O)"), "Shift+O"},
    {StudioTool::Fill, QT_TRANSLATE_NOOP("SpriteStudio", "Fill"),
     QT_TRANSLATE_NOOP("SpriteStudio", "Fill the joined pixels of one colour (G)"), "G"},
    {StudioTool::Select, QT_TRANSLATE_NOOP("SpriteStudio", "Select"),
     QT_TRANSLATE_NOOP("SpriteStudio", "Drag to select a rectangle; drawing stays inside it (M)"),
     "M"},
    {StudioTool::Move, QT_TRANSLATE_NOOP("SpriteStudio", "Move"),
     QT_TRANSLATE_NOOP("SpriteStudio", "Drag the selected pixels, or the whole layer (V)"), "V"},
    {StudioTool::Picker, QT_TRANSLATE_NOOP("SpriteStudio", "Pick colour"),
     QT_TRANSLATE_NOOP("SpriteStudio",
                       "Click to take a colour from the sprite (I, or Alt-click with any tool)"),
     "I"},
}};

std::optional<sprite::Tool> model_tool(StudioTool tool) {
    if (tool == StudioTool::Picker) {
        return std::nullopt;
    }
    return static_cast<sprite::Tool>(static_cast<std::uint8_t>(tool));
}

} // namespace

// ---------------------------------------------------------------------------------------
// Canvas
// ---------------------------------------------------------------------------------------

SpriteCanvas::SpriteCanvas(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("sprite_canvas"));
    setAccessibleName(tr("Sprite canvas"));
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(200, 200);
}

void SpriteCanvas::set_editor(sprite::Editor* editor) {
    editor_ = editor;
    fitted_ = false;
    fit();
}

void SpriteCanvas::set_colors(Color primary, Color secondary) noexcept {
    primary_ = primary;
    secondary_ = secondary;
}

void SpriteCanvas::set_onion_skin(bool on) {
    onion_ = on;
    update();
}

void SpriteCanvas::set_grid(bool on) {
    grid_ = on;
    update();
}

void SpriteCanvas::fit() {
    if (editor_ == nullptr || width() <= 0 || height() <= 0) {
        return;
    }
    const sprite::Sprite& s = editor_->sprite();
    const int across = std::max(1, (width() - 16) / std::max(s.width, 1));
    const int down = std::max(1, (height() - 16) / std::max(s.height, 1));
    zoom_ = std::clamp(std::min(across, down), 1, 64);
    origin_ = QPointF(std::floor((width() - s.width * zoom_) / 2.0),
                      std::floor((height() - s.height * zoom_) / 2.0));
    fitted_ = true;
    update();
}

void SpriteCanvas::set_zoom(int zoom) {
    zoom_about(zoom, QPointF(width() / 2.0, height() / 2.0));
}

void SpriteCanvas::zoom_about(int zoom, QPointF anchor) {
    zoom = std::clamp(zoom, 1, 64);
    if (zoom == zoom_) {
        return;
    }
    // The point of the sprite under the anchor stays under it.
    const QPointF sprite_point = (anchor - origin_) / zoom_;
    zoom_ = zoom;
    origin_ = QPointF(std::round(anchor.x() - sprite_point.x() * zoom_),
                      std::round(anchor.y() - sprite_point.y() * zoom_));
    update();
}

QPoint SpriteCanvas::pixel_at(QPointF widget_point) const {
    const QPointF p = (widget_point - origin_) / zoom_;
    return {static_cast<int>(std::floor(p.x())), static_cast<int>(std::floor(p.y()))};
}

QPointF SpriteCanvas::point_of(int x, int y) const {
    return origin_ + QPointF(x * zoom_, y * zoom_);
}

void SpriteCanvas::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (!fitted_) {
        fit();
    }
}

void SpriteCanvas::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(46, 46, 52));
    if (editor_ == nullptr) {
        return;
    }
    const sprite::Sprite& s = editor_->sprite();
    const QRectF area(origin_, QSizeF(s.width * zoom_, s.height * zoom_));
    // A checkerboard of fixed squares on the screen, behind the sprite.
    painter.save();
    painter.setClipRect(area);
    constexpr int check = 8;
    for (int y = static_cast<int>(area.top()); y < area.bottom(); y += check) {
        for (int x = static_cast<int>(area.left()); x < area.right(); x += check) {
            const bool dark = (((x - static_cast<int>(area.left())) / check) +
                               ((y - static_cast<int>(area.top())) / check)) %
                                  2 ==
                              1;
            painter.fillRect(x, y, check, check,
                             dark ? QColor(191, 191, 191) : QColor(232, 232, 232));
        }
    }
    painter.restore();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    const std::size_t frame = editor_->frame();
    if (onion_) {
        painter.setOpacity(0.25);
        if (frame > 0) {
            painter.drawImage(area, qimage(s.flatten(frame - 1)));
        }
        if (frame + 1 < s.frames.size()) {
            painter.drawImage(area, qimage(s.flatten(frame + 1)));
        }
        painter.setOpacity(1.0);
    }
    painter.drawImage(area, qimage(s.flatten(frame)));
    // The pixel grid, once pixels are big enough to see it.
    if (grid_ && zoom_ >= 8) {
        painter.setPen(QColor(0, 0, 0, 40));
        for (int x = 1; x < s.width; ++x) {
            painter.drawLine(point_of(x, 0), point_of(x, s.height));
        }
        for (int y = 1; y < s.height; ++y) {
            painter.drawLine(point_of(0, y), point_of(s.width, y));
        }
    }
    painter.setPen(QColor(20, 20, 20));
    painter.drawRect(area.adjusted(-1, -1, 0, 0));
    // The pixel under the cursor.
    if (hover_.x() >= 0 && hover_.y() >= 0 && hover_.x() < s.width && hover_.y() < s.height) {
        painter.setPen(QColor(255, 255, 255, 160));
        painter.drawRect(QRectF(point_of(hover_.x(), hover_.y()), QSizeF(zoom_ - 1, zoom_ - 1)));
    }
    // The selection: a dashed line, black over white so it shows on any colour.
    const sprite::Rect& sel = editor_->selection();
    if (!sel.empty()) {
        const QRectF box(point_of(sel.x, sel.y), QSizeF(sel.width * zoom_, sel.height * zoom_));
        painter.setPen(QPen(Qt::white, 1));
        painter.drawRect(box);
        painter.setPen(QPen(Qt::black, 1, Qt::DashLine));
        painter.drawRect(box);
    }
}

void SpriteCanvas::mousePressEvent(QMouseEvent* event) {
    setFocus();
    if (editor_ == nullptr) {
        return;
    }
    if (event->button() == Qt::MiddleButton || (space_ && event->button() == Qt::LeftButton)) {
        panning_ = true;
        pan_from_ = event->position();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (event->button() != Qt::LeftButton && event->button() != Qt::RightButton) {
        return;
    }
    const bool first = event->button() == Qt::LeftButton;
    const QPoint at = pixel_at(event->position());
    if (tool_ == StudioTool::Picker || (event->modifiers() & Qt::AltModifier) != 0) {
        const Color picked = editor_->sprite().shown(editor_->frame(), at.x(), at.y());
        Q_EMIT this->picked(picked, first);
        return;
    }
    std::string error;
    const sprite::Tool tool = *model_tool(tool_);
    if (!editor_->begin(tool, first ? primary_ : secondary_, brush_, at.x(), at.y(), error)) {
        Q_EMIT problem(text_of(error));
        return;
    }
    update();
}

void SpriteCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (editor_ == nullptr) {
        return;
    }
    if (panning_) {
        origin_ += event->position() - pan_from_;
        pan_from_ = event->position();
        update();
        return;
    }
    const QPoint at = pixel_at(event->position());
    if (at != hover_) {
        hover_ = at;
        const sprite::Sprite& s = editor_->sprite();
        const bool on = at.x() >= 0 && at.y() >= 0 && at.x() < s.width && at.y() < s.height;
        Q_EMIT hovered(on ? at.x() : -1, on ? at.y() : -1);
        update();
    }
    if (editor_->dragging()) {
        editor_->drag(at.x(), at.y());
        if (tool_ == StudioTool::Select) {
            Q_EMIT selection_changed();
        }
        update();
    }
}

void SpriteCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (panning_ && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        panning_ = false;
        unsetCursor();
        return;
    }
    if (editor_ != nullptr && editor_->dragging()) {
        const bool changed = editor_->end();
        update();
        if (changed) {
            Q_EMIT edited();
        } else if (tool_ == StudioTool::Select) {
            Q_EMIT selection_changed();
        }
    }
}

void SpriteCanvas::wheelEvent(QWheelEvent* event) {
    const int steps = event->angleDelta().y() / 120;
    if (steps == 0) {
        return;
    }
    // Zooming in doubles at small sizes and adds at large ones, as pixel editors do.
    int zoom = zoom_;
    for (int i = 0; i < std::abs(steps); ++i) {
        if (steps > 0) {
            zoom = zoom < 4 ? zoom + 1 : zoom + std::max(1, zoom / 4);
        } else {
            zoom = zoom <= 4 ? zoom - 1 : zoom - std::max(1, zoom / 5);
        }
    }
    zoom_about(zoom, event->position());
    event->accept();
}

void SpriteCanvas::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        space_ = true;
        setCursor(Qt::OpenHandCursor);
        return;
    }
    QWidget::keyPressEvent(event);
}

void SpriteCanvas::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        space_ = false;
        unsetCursor();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

// ---------------------------------------------------------------------------------------
// The window
// ---------------------------------------------------------------------------------------

SpriteStudio::SpriteStudio(QWidget* parent) : QMainWindow(parent) {
    setObjectName(QStringLiteral("sprite_studio"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(1200, 800);
    build();
}

SpriteStudio::~SpriteStudio() = default;

void SpriteStudio::build() {
    canvas_ = new SpriteCanvas(this);
    setCentralWidget(canvas_);
    player_ = new QTimer(this);
    player_->setSingleShot(true);
    connect(player_, &QTimer::timeout, this, [this] { step_playback(); });

    auto* colors = new QDockWidget(tr("Colours"), this);
    colors->setObjectName(QStringLiteral("colors_dock"));
    colors->setWidget(build_colors());
    addDockWidget(Qt::RightDockWidgetArea, colors);
    auto* layers = new QDockWidget(tr("Layers"), this);
    layers->setObjectName(QStringLiteral("layers_dock"));
    layers->setWidget(build_layers());
    addDockWidget(Qt::RightDockWidgetArea, layers);
    auto* timeline = new QDockWidget(tr("Timeline"), this);
    timeline->setObjectName(QStringLiteral("timeline_dock"));
    timeline->setWidget(build_timeline());
    addDockWidget(Qt::BottomDockWidgetArea, timeline);

    build_tools();
    build_menus();
    position_ = new QLabel(this);
    statusBar()->addPermanentWidget(position_);

    connect(canvas_, &SpriteCanvas::edited, this, [this] { refresh(); });
    connect(canvas_, &SpriteCanvas::selection_changed, this, [this] { canvas_->update(); });
    connect(canvas_, &SpriteCanvas::problem, this, &SpriteStudio::report);
    connect(canvas_, &SpriteCanvas::picked, this, [this](Color color, bool first) {
        if (first) {
            set_primary(color);
        } else {
            set_secondary(color);
        }
    });
    connect(canvas_, &SpriteCanvas::hovered, this, [this](int x, int y) {
        position_->setText(x < 0 ? QString() : tr("%1, %2").arg(x).arg(y));
    });
}

void SpriteStudio::build_tools() {
    QToolBar* tools = addToolBar(tr("Tools"));
    tools->setObjectName(QStringLiteral("sprite_tools"));
    addToolBar(Qt::LeftToolBarArea, tools);
    tools->setToolButtonStyle(Qt::ToolButtonTextOnly);
    tool_group_ = new QActionGroup(this);
    for (const ToolInfo& info : tool_infos) {
        QAction* action = tools->addAction(tr(info.name));
        action->setObjectName(QStringLiteral("tool:%1").arg(static_cast<int>(info.tool)));
        action->setToolTip(tr(info.tip));
        action->setShortcut(QKeySequence(QString::fromLatin1(info.key)));
        action->setCheckable(true);
        action->setChecked(info.tool == tool_);
        tool_group_->addAction(action);
        connect(action, &QAction::triggered, this, [this, tool = info.tool] { set_tool(tool); });
        tool_actions_.push_back(action);
    }

    // How the tools draw.
    QToolBar* options = addToolBar(tr("Brush"));
    options->setObjectName(QStringLiteral("brush_options"));
    options->addWidget(new QLabel(tr(" Size "), options));
    auto* size = new QSpinBox(options);
    size->setObjectName(QStringLiteral("brush_size"));
    size->setRange(1, 32);
    size->setToolTip(tr("Width of the brush, in pixels"));
    options->addWidget(size);
    connect(size, &QSpinBox::valueChanged, this, [this](int value) {
        brush_.size = value;
        canvas_->set_brush(brush_);
    });
    const auto toggle = [&](const QString& label, const QString& tip, const QString& name, bool on,
                            bool sprite::Brush::*field) {
        auto* box = new QCheckBox(label, options);
        box->setObjectName(name);
        box->setToolTip(tip);
        box->setChecked(on);
        options->addWidget(box);
        connect(box, &QCheckBox::toggled, this, [this, field](bool value) {
            brush_.*field = value;
            canvas_->set_brush(brush_);
        });
    };
    toggle(tr("Pixel-perfect"),
           tr("One-pixel strokes leave out the corners of L shapes, for clean curves"),
           QStringLiteral("pixel_perfect"), brush_.pixel_perfect, &sprite::Brush::pixel_perfect);
    toggle(tr("Fill all of a colour"),
           tr("Fill paints every pixel of the clicked colour, joined or not"),
           QStringLiteral("fill_all"), brush_.fill_all, &sprite::Brush::fill_all);
    toggle(tr("Mirror across"), tr("Everything drawn is mirrored left to right"),
           QStringLiteral("mirror_x"), brush_.mirror_x, &sprite::Brush::mirror_x);
    toggle(tr("Mirror down"), tr("Everything drawn is mirrored top to bottom"),
           QStringLiteral("mirror_y"), brush_.mirror_y, &sprite::Brush::mirror_y);
    toggle(tr("Dither"), tr("Only every other pixel, as a checkerboard: gradients and textures"),
           QStringLiteral("dither"), brush_.dither, &sprite::Brush::dither);
    toggle(tr("Shade"),
           tr("Each pixel drawn over moves one palette step towards the colour drawn with: "
              "shading along a ramp"),
           QStringLiteral("shade"), brush_.shade, &sprite::Brush::shade);
}

void SpriteStudio::build_menus() {
    QMenu* file = menuBar()->addMenu(tr("&File"));
    QAction* save_action = file->addAction(tr("&Save"), this, [this] { save(); });
    save_action->setShortcut(QKeySequence::Save);
    file->addSeparator();
    file->addAction(tr("&Close"), this, [this] { close(); })->setShortcut(QKeySequence::Close);

    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    undo_ = edit->addAction(tr("&Undo"), this, [this] { undo(); });
    undo_->setShortcut(QKeySequence::Undo);
    redo_ = edit->addAction(tr("&Redo"), this, [this] { redo(); });
    redo_->setShortcut(QKeySequence::Redo);
    edit->addSeparator();
    edit->addAction(tr("Cu&t"), this,
                    [this] { apply([this](std::string& e) { return editor_->cut(e); }); })
        ->setShortcut(QKeySequence::Cut);
    edit->addAction(tr("&Copy"), this,
                    [this] {
                        if (editor_ != nullptr) {
                            editor_->copy();
                        }
                    })
        ->setShortcut(QKeySequence::Copy);
    edit->addAction(tr("&Paste"), this,
                    [this] { apply([this](std::string& e) { return editor_->paste(e); }); })
        ->setShortcut(QKeySequence::Paste);
    edit->addAction(tr("C&lear"), this,
                    [this] { apply([this](std::string& e) { return editor_->clear(e); }); })
        ->setShortcut(QKeySequence::Delete);
    edit->addSeparator();
    edit->addAction(tr("Select &all"), this,
                    [this] {
                        if (editor_ != nullptr) {
                            editor_->select_all();
                            canvas_->update();
                        }
                    })
        ->setShortcut(QKeySequence::SelectAll);
    edit->addAction(tr("&Deselect"), this,
                    [this] {
                        if (editor_ != nullptr) {
                            editor_->deselect();
                            canvas_->update();
                        }
                    })
        ->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
    edit->addSeparator();
    edit->addAction(tr("Flip &horizontally"), this,
                    [this] { apply([this](std::string& e) { return editor_->flip(true, e); }); })
        ->setShortcut(QKeySequence(QStringLiteral("Shift+H")));
    edit->addAction(tr("Flip &vertically"), this,
                    [this] { apply([this](std::string& e) { return editor_->flip(false, e); }); })
        ->setShortcut(QKeySequence(QStringLiteral("Shift+V")));
    edit->addAction(tr("Swap the colours"), this, [this] { swap_colors(); })
        ->setShortcut(QKeySequence(QStringLiteral("X")));

    QMenu* sprite_menu = menuBar()->addMenu(tr("&Sprite"));
    sprite_menu->addAction(tr("Canvas &size…"), this, [this] {
        if (editor_ == nullptr) {
            return;
        }
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Canvas size"));
        auto* form = new QFormLayout(&dialog);
        auto* w = new QSpinBox(&dialog);
        auto* h = new QSpinBox(&dialog);
        for (QSpinBox* box : {w, h}) {
            box->setRange(1, sprite::max_side);
        }
        w->setValue(editor_->sprite().width);
        h->setValue(editor_->sprite().height);
        auto* anchor = new QComboBox(&dialog);
        anchor->addItems(
            {tr("Keep the pixels at the top left"), tr("Keep the pixels in the middle")});
        form->addRow(tr("Width"), w);
        form->addRow(tr("Height"), h);
        form->addRow(tr("Pixels"), anchor);
        auto* buttons =
            new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted) {
            const auto where =
                anchor->currentIndex() == 1 ? sprite::Anchor::Center : sprite::Anchor::TopLeft;
            apply(
                [&](std::string& e) { return editor_->resize(w->value(), h->value(), where, e); });
            canvas_->fit();
        }
    });
    sprite_menu->addAction(tr("Frames &across in the sheet…"), this, [this] {
        if (editor_ == nullptr) {
            return;
        }
        bool ok = false;
        const int columns = QInputDialog::getInt(
            this, tr("Sprite sheet"), tr("Frames across in the saved sheet (0: as many as fit):"),
            editor_->sprite().sheet_columns, 0, static_cast<int>(sprite::max_frames), 1, &ok);
        if (ok) {
            apply([&](std::string& e) { return editor_->set_sheet_columns(columns, e); });
        }
    });

    QMenu* view = menuBar()->addMenu(tr("&View"));
    view->addAction(tr("Zoom &in"), this, [this] { canvas_->set_zoom(canvas_->zoom() * 2); })
        ->setShortcut(QKeySequence::ZoomIn);
    view->addAction(tr("Zoom &out"), this, [this] { canvas_->set_zoom(canvas_->zoom() / 2); })
        ->setShortcut(QKeySequence::ZoomOut);
    view->addAction(tr("&Fit the window"), this, [this] { canvas_->fit(); })
        ->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));
    QAction* grid = view->addAction(tr("Pixel &grid"));
    grid->setCheckable(true);
    grid->setChecked(true);
    grid->setShortcut(QKeySequence(QStringLiteral("Ctrl+'")));
    connect(grid, &QAction::toggled, canvas_, &SpriteCanvas::set_grid);
    view->addAction(onion_);
    for (QDockWidget* dock : findChildren<QDockWidget*>()) {
        view->addAction(dock->toggleViewAction());
    }
}

QWidget* SpriteStudio::build_colors() {
    auto* panel = new QWidget(this);
    auto* column = new QVBoxLayout(panel);
    auto* row = new QHBoxLayout;
    const auto chooser = [&](QToolButton*& button, const QString& name, const QString& tip,
                             bool first) {
        button = new QToolButton(panel);
        button->setObjectName(name);
        button->setToolTip(tip);
        button->setIconSize(QSize(32, 32));
        row->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, first] {
            const QColor chosen =
                QColorDialog::getColor(qcolor(first ? primary_ : secondary_), this,
                                       tr("Choose a colour"), QColorDialog::ShowAlphaChannel);
            if (chosen.isValid()) {
                if (first) {
                    set_primary(color_of(chosen));
                } else {
                    set_secondary(color_of(chosen));
                }
            }
        });
    };
    chooser(primary_button_, QStringLiteral("primary_color"),
            tr("The first colour: the left button draws with it"), true);
    chooser(secondary_button_, QStringLiteral("secondary_color"),
            tr("The second colour: the right button draws with it"), false);
    auto* swap = new QToolButton(panel);
    swap->setText(tr("Swap"));
    swap->setToolTip(tr("The colours change places (X)"));
    connect(swap, &QToolButton::clicked, this, [this] { swap_colors(); });
    row->addWidget(swap);
    row->addStretch();
    column->addLayout(row);
    auto* hint = new QLabel(tr("Palette: click for the first colour, right-click for the second; "
                               "hold the button down to change or remove a colour"),
                            panel);
    hint->setWordWrap(true);
    column->addWidget(hint);
    palette_ = new QWidget(panel);
    palette_grid_ = new QGridLayout(palette_);
    palette_grid_->setSpacing(1);
    palette_grid_->setContentsMargins(0, 0, 0, 0);
    column->addWidget(palette_);
    auto* buttons = new QHBoxLayout;
    auto* add = new QPushButton(tr("Add the first colour"), panel);
    add->setObjectName(QStringLiteral("add_color"));
    connect(add, &QPushButton::clicked, this, [this] {
        if (editor_ != nullptr && editor_->add_color(primary_)) {
            refresh();
        }
    });
    auto* from = new QPushButton(tr("From the sprite"), panel);
    from->setToolTip(tr("Replace the palette with the colours the sprite uses"));
    connect(from, &QPushButton::clicked, this,
            [this] { apply([this](std::string& e) { return editor_->palette_from_sprite(e); }); });
    buttons->addWidget(add);
    buttons->addWidget(from);
    column->addLayout(buttons);
    column->addStretch();
    return panel;
}

QWidget* SpriteStudio::build_layers() {
    auto* panel = new QWidget(this);
    auto* column = new QVBoxLayout(panel);
    layers_ = new QTreeWidget(panel);
    layers_->setObjectName(QStringLiteral("sprite_layers"));
    layers_->setAccessibleName(tr("Layers"));
    layers_->setColumnCount(3);
    layers_->setHeaderLabels({tr("Shown"), tr("Locked"), tr("Layer")});
    layers_->setRootIsDecorated(false);
    layers_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    layers_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    layers_->header()->setStretchLastSection(true);
    layers_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    column->addWidget(layers_);
    // The list shows the top layer first, as it is drawn.
    const auto layer_of = [](const QTreeWidgetItem* item) {
        return static_cast<std::size_t>(item->data(0, Qt::UserRole).toInt());
    };
    connect(layers_, &QTreeWidget::currentItemChanged, this,
            [this, layer_of](QTreeWidgetItem* item) {
                if (refreshing_ || item == nullptr || editor_ == nullptr) {
                    return;
                }
                editor_->set_layer(layer_of(item));
                refresh_current();
            });
    connect(layers_, &QTreeWidget::itemChanged, this,
            [this, layer_of](QTreeWidgetItem* item, int column_changed) {
                if (refreshing_ || editor_ == nullptr) {
                    return;
                }
                const std::size_t layer = layer_of(item);
                std::string error;
                bool changed = false;
                if (column_changed == 0) {
                    changed = editor_->set_layer_visible(layer, item->checkState(0) == Qt::Checked);
                } else if (column_changed == 1) {
                    changed = editor_->set_layer_locked(layer, item->checkState(1) == Qt::Checked);
                } else {
                    const QByteArray name = item->text(2).trimmed().toUtf8();
                    changed = editor_->rename_layer(layer, name.toStdString(), error);
                    editor_->end_merge();
                }
                if (!error.empty()) {
                    report(text_of(error));
                }
                if (changed || !error.empty()) {
                    // Refreshing inside the item's own signal would delete it under Qt.
                    QTimer::singleShot(0, this, [this] { refresh(); });
                }
            });
    auto* opacity_row = new QHBoxLayout;
    opacity_row->addWidget(new QLabel(tr("Opacity"), panel));
    opacity_ = new QSlider(Qt::Horizontal, panel);
    opacity_->setObjectName(QStringLiteral("layer_opacity"));
    opacity_->setRange(0, 255);
    opacity_row->addWidget(opacity_);
    column->addLayout(opacity_row);
    connect(opacity_, &QSlider::valueChanged, this, [this](int value) {
        if (!refreshing_ && editor_ != nullptr &&
            editor_->set_layer_opacity(editor_->layer(), value)) {
            canvas_->update();
            refresh_title();
        }
    });
    connect(opacity_, &QSlider::sliderReleased, this, [this] {
        if (editor_ != nullptr) {
            editor_->end_merge();
        }
    });
    auto* buttons = new QGridLayout;
    const auto button = [&](const QString& label, const QString& tip, int r, int c, auto edit) {
        auto* b = new QPushButton(label, panel);
        b->setToolTip(tip);
        buttons->addWidget(b, r, c);
        connect(b, &QPushButton::clicked, this, [this, edit] { apply(edit); });
    };
    button(tr("New"), tr("An empty layer above this one"), 0, 0,
           [this](std::string& e) { return editor_->add_layer(e); });
    button(tr("Duplicate"), tr("A copy of this layer above it"), 0, 1,
           [this](std::string& e) { return editor_->duplicate_layer(e); });
    button(tr("Delete"), tr("Delete this layer"), 0, 2,
           [this](std::string& e) { return editor_->remove_layer(e); });
    button(tr("Up"), tr("Move this layer up, in front of the one above"), 1, 0,
           [this](std::string& e) { return editor_->move_layer(true, e); });
    button(tr("Down"), tr("Move this layer down, behind the one below"), 1, 1,
           [this](std::string& e) { return editor_->move_layer(false, e); });
    button(tr("Merge down"), tr("Draw this layer onto the one below it"), 1, 2,
           [this](std::string& e) { return editor_->merge_down(e); });
    column->addLayout(buttons);
    return panel;
}

QWidget* SpriteStudio::build_timeline() {
    auto* panel = new QWidget(this);
    auto* column = new QVBoxLayout(panel);
    auto* controls = new QHBoxLayout;
    play_ = new QAction(tr("Play"), this);
    play_->setObjectName(QStringLiteral("play_frames"));
    play_->setCheckable(true);
    play_->setShortcut(QKeySequence(Qt::Key_Return));
    play_->setToolTip(tr("Play the frames, looping in the current frame's tag (Enter)"));
    connect(play_, &QAction::toggled, this, [this](bool on) { set_playing(on); });
    addAction(play_);
    onion_ = new QAction(tr("Onion skin"), this);
    onion_->setObjectName(QStringLiteral("onion_skin"));
    onion_->setCheckable(true);
    onion_->setToolTip(tr("Show the frames before and after this one faintly behind it"));
    connect(onion_, &QAction::toggled, canvas_, &SpriteCanvas::set_onion_skin);
    const auto tool_button = [&](QAction* action) {
        auto* b = new QToolButton(panel);
        b->setDefaultAction(action);
        controls->addWidget(b);
    };
    const auto action = [&](const QString& label, const QString& tip, const QKeySequence& key,
                            auto run) {
        auto* a = new QAction(label, this);
        a->setToolTip(tip);
        a->setShortcut(key);
        addAction(a);
        connect(a, &QAction::triggered, this, run);
        tool_button(a);
        return a;
    };
    action(tr("◀"), tr("The frame before (,)"), QKeySequence(QStringLiteral(",")), [this] {
        if (editor_ != nullptr) {
            const std::size_t count = editor_->sprite().frames.size();
            editor_->set_frame((editor_->frame() + count - 1) % count);
            refresh_current();
        }
    });
    tool_button(play_);
    action(tr("▶"), tr("The frame after (.)"), QKeySequence(QStringLiteral(".")), [this] {
        if (editor_ != nullptr) {
            editor_->set_frame((editor_->frame() + 1) % editor_->sprite().frames.size());
            refresh_current();
        }
    });
    action(tr("New frame"), tr("An empty frame after this one (Alt+N)"),
           QKeySequence(QStringLiteral("Alt+N")),
           [this] { apply([this](std::string& e) { return editor_->add_frame(false, e); }); });
    action(tr("Duplicate"), tr("A copy of this frame after it (Alt+D)"),
           QKeySequence(QStringLiteral("Alt+D")),
           [this] { apply([this](std::string& e) { return editor_->add_frame(true, e); }); });
    action(tr("Delete"), tr("Delete this frame"), QKeySequence(),
           [this] { apply([this](std::string& e) { return editor_->remove_frame(e); }); });
    action(tr("Earlier"), tr("Move this frame one place earlier"), QKeySequence(),
           [this] { apply([this](std::string& e) { return editor_->move_frame(true, e); }); });
    action(tr("Later"), tr("Move this frame one place later"), QKeySequence(),
           [this] { apply([this](std::string& e) { return editor_->move_frame(false, e); }); });
    tool_button(onion_);
    controls->addWidget(new QLabel(tr("Shows for"), panel));
    duration_ = new QSpinBox(panel);
    duration_->setObjectName(QStringLiteral("frame_duration"));
    duration_->setRange(1, 65535);
    duration_->setSuffix(tr(" ms"));
    duration_->setToolTip(tr("How long this frame shows when playing"));
    controls->addWidget(duration_);
    all_frames_ = new QCheckBox(tr("every frame"), panel);
    all_frames_->setToolTip(tr("Set every frame's time at once"));
    controls->addWidget(all_frames_);
    connect(duration_, &QSpinBox::valueChanged, this, [this](int value) {
        if (refreshing_ || editor_ == nullptr) {
            return;
        }
        std::string error;
        if (editor_->set_frame_duration(editor_->frame(), value, error, all_frames_->isChecked())) {
            refresh_timeline();
            refresh_title();
        } else if (!error.empty()) {
            report(text_of(error));
        }
    });
    connect(duration_, &QSpinBox::editingFinished, this, [this] {
        if (editor_ != nullptr) {
            editor_->end_merge();
        }
    });
    controls->addStretch();
    column->addLayout(controls);

    auto* row = new QHBoxLayout;
    timeline_ = new QTableWidget(panel);
    timeline_->setObjectName(QStringLiteral("sprite_timeline"));
    timeline_->setAccessibleName(tr("Timeline: layers down, frames across"));
    timeline_->setSelectionMode(QAbstractItemView::SingleSelection);
    timeline_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    timeline_->horizontalHeader()->setDefaultSectionSize(44);
    timeline_->verticalHeader()->setDefaultSectionSize(22);
    connect(timeline_, &QTableWidget::currentCellChanged, this, [this](int r, int c) {
        if (refreshing_ || editor_ == nullptr || r < 0 || c < 0) {
            return;
        }
        const std::size_t count = editor_->sprite().layers.size();
        editor_->set_layer(count - 1 - static_cast<std::size_t>(r));
        editor_->set_frame(static_cast<std::size_t>(c));
        refresh_current();
    });
    row->addWidget(timeline_, 3);

    // Tags: the animation clips the game plays.
    auto* tag_column = new QVBoxLayout;
    tag_column->addWidget(new QLabel(tr("Tags (animations)"), panel));
    tags_ = new QListWidget(panel);
    tags_->setObjectName(QStringLiteral("sprite_tags"));
    tags_->setToolTip(tr("Double-click a tag to change it"));
    tag_column->addWidget(tags_);
    connect(tags_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* item) { edit_tag(tags_->row(item)); });
    auto* tag_buttons = new QHBoxLayout;
    auto* add_tag = new QPushButton(tr("Tag frames…"), panel);
    add_tag->setObjectName(QStringLiteral("add_tag"));
    add_tag->setToolTip(tr("Name a run of frames, such as walk or idle"));
    connect(add_tag, &QPushButton::clicked, this, [this] { edit_tag(-1); });
    auto* remove_tag = new QPushButton(tr("Delete tag"), panel);
    connect(remove_tag, &QPushButton::clicked, this, [this] {
        if (editor_ != nullptr && tags_->currentRow() >= 0 &&
            editor_->remove_tag(static_cast<std::size_t>(tags_->currentRow()))) {
            refresh();
        }
    });
    tag_buttons->addWidget(add_tag);
    tag_buttons->addWidget(remove_tag);
    tag_column->addLayout(tag_buttons);
    row->addLayout(tag_column, 1);
    column->addLayout(row);
    return panel;
}

void SpriteStudio::edit_tag(int index) {
    if (editor_ == nullptr) {
        return;
    }
    const sprite::Sprite& s = editor_->sprite();
    sprite::Tag tag;
    if (index >= 0 && static_cast<std::size_t>(index) < s.tags.size()) {
        tag = s.tags[static_cast<std::size_t>(index)];
    } else {
        index = -1;
        tag.name = "walk";
        tag.from = 0;
        tag.to = s.frames.size() - 1;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(index < 0 ? tr("Tag frames") : tr("Change the tag"));
    auto* form = new QFormLayout(&dialog);
    auto* name = new QLineEdit(text_of(tag.name), &dialog);
    auto* from = new QSpinBox(&dialog);
    auto* to = new QSpinBox(&dialog);
    for (QSpinBox* box : {from, to}) {
        box->setRange(1, static_cast<int>(s.frames.size()));
    }
    from->setValue(static_cast<int>(tag.from) + 1);
    to->setValue(static_cast<int>(tag.to) + 1);
    auto* direction = new QComboBox(&dialog);
    direction->addItems({tr("Forward"), tr("Backward"), tr("Forward and back (ping-pong)")});
    direction->setCurrentIndex(static_cast<int>(tag.direction));
    form->addRow(tr("Name"), name);
    form->addRow(tr("From frame"), from);
    form->addRow(tr("To frame"), to);
    form->addRow(tr("Plays"), direction);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    tag.name = name->text().trimmed().toStdString();
    tag.from = static_cast<std::size_t>(from->value() - 1);
    tag.to = static_cast<std::size_t>(to->value() - 1);
    tag.direction = static_cast<sprite::Direction>(direction->currentIndex());
    apply([&](std::string& error) {
        if (index >= 0) {
            return editor_->set_tag(static_cast<std::size_t>(index), tag, error);
        }
        const std::optional<std::size_t> added =
            editor_->add_tag(tag.name, tag.from, tag.to, error);
        if (added && tag.direction != sprite::Direction::Forward) {
            sprite::Tag made = editor_->sprite().tags[*added];
            made.direction = tag.direction;
            static_cast<void>(editor_->set_tag(*added, made, error));
        }
        return added.has_value();
    });
}

// ----- Opening and saving -----

void SpriteStudio::edit(sprite::Sprite sprite, const fs::path& file) {
    set_playing(false);
    editor_ = std::make_unique<sprite::Editor>(std::move(sprite));
    file_ = file;
    if (!editor_->sprite().palette.empty()) {
        primary_ = editor_->sprite().palette.front();
        secondary_ = editor_->sprite().palette.size() > 1 ? editor_->sprite().palette[1]
                                                          : Color{255, 255, 255, 255};
    }
    canvas_->set_editor(editor_.get());
    canvas_->set_colors(primary_, secondary_);
    canvas_->set_brush(brush_);
    refresh();
}

bool SpriteStudio::open(const fs::path& file, std::string& error) {
    std::optional<sprite::Sprite> loaded = sprite::load(file, error);
    if (!loaded) {
        return false;
    }
    edit(std::move(*loaded), file);
    return true;
}

bool SpriteStudio::import(const fs::path& png, int frame_width, int frame_height,
                          std::string& error) {
    std::ifstream in(png, std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                          std::istreambuf_iterator<char>());
    if (!in && bytes.empty()) {
        error = "The picture cannot be opened.";
        return false;
    }
    const std::optional<sprite::Image> image = engine::assets::decode_png(bytes, error);
    if (!image) {
        return false;
    }
    std::optional<sprite::Sprite> made =
        sprite::import_sheet(*image, frame_width, frame_height, error);
    if (!made) {
        return false;
    }
    fs::path file = png;
    file.replace_extension(".dsprite");
    edit(std::move(*made), file);
    return true;
}

bool SpriteStudio::save() {
    if (editor_ == nullptr) {
        return false;
    }
    std::string error;
    if (!sprite::save(editor_->sprite(), file_, error)) {
        report(tr("The sprite could not be saved: %1").arg(text_of(error)));
        return false;
    }
    editor_->mark_saved();
    refresh_title();
    statusBar()->showMessage(tr("Saved, with its sprite sheet"), 3000);
    Q_EMIT saved(file_);
    return true;
}

void SpriteStudio::undo() {
    if (editor_ != nullptr && editor_->undo()) {
        refresh();
    }
}

void SpriteStudio::redo() {
    if (editor_ != nullptr && editor_->redo()) {
        refresh();
    }
}

void SpriteStudio::apply(const std::function<bool(std::string&)>& edit) {
    if (editor_ == nullptr) {
        return;
    }
    std::string error;
    if (edit(error)) {
        refresh();
    } else if (!error.empty()) {
        report(text_of(error));
    }
}

void SpriteStudio::report(const QString& message) {
    statusBar()->showMessage(message, 8000);
    if (!quiet_ && !QApplication::platformName().startsWith(QStringLiteral("offscreen"))) {
        QMessageBox::warning(this, tr("Sprite Studio"), message);
    }
}

// ----- Tools and colours -----

void SpriteStudio::set_tool(StudioTool tool) {
    tool_ = tool;
    canvas_->set_tool(tool);
    for (QAction* action : tool_actions_) {
        if (action->objectName() == QStringLiteral("tool:%1").arg(static_cast<int>(tool))) {
            action->setChecked(true);
        }
    }
}

void SpriteStudio::set_primary(Color color) {
    primary_ = color;
    canvas_->set_colors(primary_, secondary_);
    primary_button_->setIcon(swatch(primary_, 32));
    primary_button_->setToolTip(tr("The first colour, %1: the left button draws with it")
                                    .arg(text_of(sprite::to_hex(primary_))));
}

void SpriteStudio::set_secondary(Color color) {
    secondary_ = color;
    canvas_->set_colors(primary_, secondary_);
    secondary_button_->setIcon(swatch(secondary_, 32));
    secondary_button_->setToolTip(tr("The second colour, %1: the right button draws with it")
                                      .arg(text_of(sprite::to_hex(secondary_))));
}

void SpriteStudio::swap_colors() {
    const Color first = primary_;
    set_primary(secondary_);
    set_secondary(first);
}

// ----- Playback -----

bool SpriteStudio::playing() const noexcept {
    return player_->isActive();
}

void SpriteStudio::set_playing(bool on) {
    if (editor_ == nullptr || !on) {
        player_->stop();
    } else {
        ping_pong_ = 1;
        player_->start(static_cast<int>(editor_->sprite().frames[editor_->frame()].duration_ms));
    }
    if (play_ != nullptr && play_->isChecked() != on) {
        const QSignalBlocker quiet(play_);
        play_->setChecked(on);
    }
    if (play_ != nullptr) {
        play_->setText(on ? tr("Stop") : tr("Play"));
    }
}

void SpriteStudio::step_playback() {
    if (editor_ == nullptr) {
        return;
    }
    const sprite::Sprite& s = editor_->sprite();
    const std::size_t now = editor_->frame();
    // The tag this frame is in, if any, else every frame, forward.
    std::size_t first = 0;
    std::size_t last = s.frames.size() - 1;
    sprite::Direction direction = sprite::Direction::Forward;
    for (const sprite::Tag& tag : s.tags) {
        if (now >= tag.from && now <= tag.to) {
            first = tag.from;
            last = tag.to;
            direction = tag.direction;
            break;
        }
    }
    std::size_t next = now;
    if (first != last) {
        switch (direction) {
        case sprite::Direction::Forward:
            next = now >= last ? first : now + 1;
            break;
        case sprite::Direction::Reverse:
            next = now <= first ? last : now - 1;
            break;
        case sprite::Direction::PingPong:
            if ((ping_pong_ > 0 && now >= last) || (ping_pong_ < 0 && now <= first)) {
                ping_pong_ = -ping_pong_;
            }
            next = ping_pong_ > 0 ? now + 1 : now - 1;
            break;
        }
    }
    editor_->set_frame(next);
    refresh_current();
    player_->start(static_cast<int>(s.frames[next].duration_ms));
}

// ----- Showing -----

void SpriteStudio::refresh() {
    if (editor_ == nullptr) {
        return;
    }
    refreshing_ = true;
    refresh_palette();
    refresh_layers();
    refresh_timeline();
    refreshing_ = false;
    canvas_->update();
    refresh_title();
}

void SpriteStudio::refresh_current() {
    if (editor_ == nullptr) {
        return;
    }
    const bool was = refreshing_;
    refreshing_ = true;
    const sprite::Sprite& s = editor_->sprite();
    for (int i = 0; i < layers_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = layers_->topLevelItem(i);
        if (static_cast<std::size_t>(item->data(0, Qt::UserRole).toInt()) == editor_->layer()) {
            layers_->setCurrentItem(item, 2);
        }
    }
    opacity_->setValue(s.layers[editor_->layer()].opacity);
    timeline_->setCurrentCell(static_cast<int>(s.layers.size()) - 1 -
                                  static_cast<int>(editor_->layer()),
                              static_cast<int>(editor_->frame()));
    duration_->setValue(static_cast<int>(s.frames[editor_->frame()].duration_ms));
    refreshing_ = was;
    canvas_->update();
}

void SpriteStudio::refresh_title() {
    if (editor_ == nullptr) {
        setWindowTitle(tr("Sprite Studio"));
        return;
    }
    const sprite::Sprite& s = editor_->sprite();
    setWindowTitle(tr("%1%2 (%3 × %4, %5 frames) — Sprite Studio")
                       .arg(path_text(file_.filename()))
                       .arg(editor_->modified() ? QStringLiteral(" *") : QString())
                       .arg(s.width)
                       .arg(s.height)
                       .arg(s.frames.size()));
    undo_->setEnabled(editor_->can_undo());
    redo_->setEnabled(editor_->can_redo());
    undo_->setText(editor_->can_undo() ? tr("&Undo %1").arg(text_of(editor_->undo_text()))
                                       : tr("&Undo"));
    redo_->setText(editor_->can_redo() ? tr("&Redo %1").arg(text_of(editor_->redo_text()))
                                       : tr("&Redo"));
}

void SpriteStudio::refresh_palette() {
    set_primary(primary_);
    set_secondary(secondary_);
    while (QLayoutItem* item = palette_grid_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const std::vector<Color>& colors = editor_->sprite().palette;
    constexpr int across = 8;
    for (std::size_t i = 0; i < colors.size(); ++i) {
        auto* button = new QToolButton(palette_);
        button->setObjectName(QStringLiteral("palette:%1").arg(i));
        button->setIcon(swatch(colors[i], 18));
        button->setIconSize(QSize(18, 18));
        button->setAutoRaise(true);
        button->setToolTip(text_of(sprite::to_hex(colors[i])));
        button->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(button, &QToolButton::clicked, this, [this, c = colors[i]] { set_primary(c); });
        // The right button: the second colour, as on the canvas.
        connect(button, &QToolButton::customContextMenuRequested, this,
                [this, c = colors[i]] { set_secondary(c); });
        palette_grid_->addWidget(button, static_cast<int>(i) / across,
                                 static_cast<int>(i) % across);
        auto* change = new QAction(tr("Change…"), button);
        connect(change, &QAction::triggered, this, [this, i] {
            const QColor chosen =
                QColorDialog::getColor(qcolor(editor_->sprite().palette[i]), this,
                                       tr("Change the colour"), QColorDialog::ShowAlphaChannel);
            if (chosen.isValid() && editor_->set_color(i, color_of(chosen))) {
                editor_->end_merge();
                refresh();
            }
        });
        auto* remove = new QAction(tr("Remove from the palette"), button);
        connect(remove, &QAction::triggered, this, [this, i] {
            if (editor_->remove_color(i)) {
                QTimer::singleShot(0, this, [this] { refresh(); });
            }
        });
        button->addAction(change);
        button->addAction(remove);
        button->setPopupMode(QToolButton::DelayedPopup);
    }
}

void SpriteStudio::refresh_layers() {
    const sprite::Sprite& s = editor_->sprite();
    layers_->clear();
    QTreeWidgetItem* current = nullptr;
    for (std::size_t i = s.layers.size(); i-- > 0;) {
        const sprite::Layer& layer = s.layers[i];
        auto* item = new QTreeWidgetItem(layers_);
        item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
        item->setData(0, Qt::UserRole, static_cast<int>(i));
        item->setCheckState(0, layer.visible ? Qt::Checked : Qt::Unchecked);
        item->setCheckState(1, layer.locked ? Qt::Checked : Qt::Unchecked);
        item->setText(2, text_of(layer.name));
        if (i == editor_->layer()) {
            current = item;
        }
    }
    layers_->setCurrentItem(current, 2);
    opacity_->setValue(s.layers[editor_->layer()].opacity);
    opacity_->setToolTip(tr("%1 of 255").arg(s.layers[editor_->layer()].opacity));
}

void SpriteStudio::refresh_timeline() {
    const bool was = refreshing_;
    refreshing_ = true;
    const sprite::Sprite& s = editor_->sprite();
    const int rows = static_cast<int>(s.layers.size());
    const int columns = static_cast<int>(s.frames.size());
    timeline_->setRowCount(rows);
    timeline_->setColumnCount(columns);
    QStringList frame_labels;
    for (int f = 0; f < columns; ++f) {
        // A frame's tags are named over it.
        QString label = QString::number(f + 1);
        for (const sprite::Tag& tag : s.tags) {
            if (static_cast<std::size_t>(f) == tag.from) {
                label = text_of(tag.name) + QLatin1Char('\n') + label;
            }
        }
        frame_labels << label;
    }
    timeline_->setHorizontalHeaderLabels(frame_labels);
    QStringList layer_labels;
    for (int r = 0; r < rows; ++r) {
        const std::size_t layer = s.layers.size() - 1 - static_cast<std::size_t>(r);
        layer_labels << text_of(s.layers[layer].name);
        for (int f = 0; f < columns; ++f) {
            const sprite::Cel& cel = s.cels[layer][static_cast<std::size_t>(f)];
            auto* item = new QTableWidgetItem(cel ? QStringLiteral("●") : QStringLiteral("○"));
            item->setTextAlignment(Qt::AlignCenter);
            item->setToolTip(tr("%1, frame %2: %3 ms")
                                 .arg(text_of(s.layers[layer].name))
                                 .arg(f + 1)
                                 .arg(s.frames[static_cast<std::size_t>(f)].duration_ms));
            timeline_->setItem(r, f, item);
        }
    }
    timeline_->setVerticalHeaderLabels(layer_labels);
    timeline_->setCurrentCell(rows - 1 - static_cast<int>(editor_->layer()),
                              static_cast<int>(editor_->frame()));
    duration_->setValue(static_cast<int>(s.frames[editor_->frame()].duration_ms));
    tags_->clear();
    const std::array<QString, 3> directions{tr("forward"), tr("backward"), tr("ping-pong")};
    for (const sprite::Tag& tag : s.tags) {
        tags_->addItem(tr("%1: frames %2 to %3, %4")
                           .arg(text_of(tag.name))
                           .arg(tag.from + 1)
                           .arg(tag.to + 1)
                           .arg(directions.at(static_cast<std::size_t>(tag.direction))));
    }
    refreshing_ = was;
}

void SpriteStudio::closeEvent(QCloseEvent* event) {
    set_playing(false);
    if (editor_ != nullptr && editor_->modified() &&
        !QApplication::platformName().startsWith(QStringLiteral("offscreen"))) {
        const auto answer = QMessageBox::question(
            this, tr("Sprite Studio"),
            tr("Save the changes to %1 first?").arg(path_text(file_.filename())),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !save())) {
            event->ignore();
            return;
        }
    }
    event->accept();
}

} // namespace editor
