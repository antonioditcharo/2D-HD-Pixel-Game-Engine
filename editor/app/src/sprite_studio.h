#pragma once

// Sprite Studio (plan/06 §3.3): a window to draw and animate pixel-art sprites, laid out as
// pixel artists know from Aseprite and its peers: the tools down the left, the canvas in the
// middle, colours, the palette and the layers on the right, and the timeline of frames, with
// tags, onion skin and playback, along the bottom. Shortcuts follow theirs (B pencil,
// E eraser, G fill, L line, U rectangle, M select, V move, I pick a colour, X swaps the
// colours, Alt-click picks).
//
// Saving writes the .dsprite and the sprite sheet PNG the game draws (editor/core/sprite.h).
//
// See docs/dev/sprite-studio.md.

#include <QMainWindow>
#include <QWidget>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "editor/core/sprite.h"

class QAction;
class QActionGroup;
class QCheckBox;
class QLabel;
class QListWidget;
class QSlider;
class QSpinBox;
class QTableWidget;
class QTimer;
class QToolButton;
class QTreeWidget;
class QGridLayout;

namespace editor {

/// The tools of the tool bar: the model's, and picking a colour.
enum class StudioTool : std::uint8_t {
    Pencil,
    Eraser,
    Line,
    Rectangle,
    FilledRectangle,
    Ellipse,
    FilledEllipse,
    Fill,
    Select,
    Move,
    Picker,
};

/// The canvas: the current frame, magnified to whole pixels over a checkerboard, with the
/// frames either side faintly behind it (onion skin), a pixel grid when zoomed in, and the
/// selection. The left button draws with the first colour, the right with the second; the
/// middle button (or Space and a drag) moves the view, and the wheel zooms about the cursor.
class SpriteCanvas : public QWidget {
    Q_OBJECT

public:
    explicit SpriteCanvas(QWidget* parent = nullptr);

    void set_editor(sprite::Editor* editor);
    void set_tool(StudioTool tool) noexcept { tool_ = tool; }
    void set_brush(const sprite::Brush& brush) noexcept { brush_ = brush; }
    void set_colors(sprite::Color primary, sprite::Color secondary) noexcept;
    void set_onion_skin(bool on);
    void set_grid(bool on);
    [[nodiscard]] int zoom() const noexcept { return zoom_; }
    /// Whole-number magnification, 1 to 64, about the middle of the view.
    void set_zoom(int zoom);
    /// The largest zoom at which the whole sprite fits, centred.
    void fit();
    /// The pixel of the sprite under a point of the widget.
    [[nodiscard]] QPoint pixel_at(QPointF widget_point) const;
    /// The point of the widget at the top-left corner of a pixel.
    [[nodiscard]] QPointF point_of(int x, int y) const;

Q_SIGNALS:
    /// A drag ended that changed the sprite.
    void edited();
    /// The selection changed (Select tool).
    void selection_changed();
    /// A colour was picked: into the first colour (left button) or the second.
    void picked(sprite::Color color, bool primary);
    void problem(const QString& message);
    /// The pixel under the cursor, for the status bar (-1 off the canvas).
    void hovered(int x, int y);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void zoom_about(int zoom, QPointF anchor);

    sprite::Editor* editor_ = nullptr;
    StudioTool tool_ = StudioTool::Pencil;
    sprite::Brush brush_;
    sprite::Color primary_{0, 0, 0, 255};
    sprite::Color secondary_{255, 255, 255, 255};
    bool onion_ = false;
    bool grid_ = true;
    bool fitted_ = false;
    int zoom_ = 8;
    QPointF origin_; ///< Where the sprite's top-left corner is in the widget.
    bool panning_ = false;
    bool space_ = false;
    QPointF pan_from_;
    QPoint hover_{-1, -1};
};

/// The Sprite Studio window for one sprite file.
class SpriteStudio : public QMainWindow {
    Q_OBJECT

public:
    explicit SpriteStudio(QWidget* parent = nullptr);
    ~SpriteStudio() override;
    SpriteStudio(const SpriteStudio&) = delete;
    SpriteStudio& operator=(const SpriteStudio&) = delete;
    SpriteStudio(SpriteStudio&&) = delete;
    SpriteStudio& operator=(SpriteStudio&&) = delete;

    /// Edits a .dsprite file. False, with `error`, if it cannot be read.
    bool open(const std::filesystem::path& file, std::string& error);
    /// Edits a sprite made from a PNG sprite sheet, cut into frames of `frame_width` ×
    /// `frame_height` (0 for the whole picture), to be saved as `file` (a .dsprite beside
    /// it, whose sheet replaces the PNG).
    bool import(const std::filesystem::path& png, int frame_width, int frame_height,
                std::string& error);
    /// Edits `sprite`, to be saved as `file`.
    void edit(sprite::Sprite sprite, const std::filesystem::path& file);

    [[nodiscard]] const std::filesystem::path& file() const noexcept { return file_; }
    [[nodiscard]] sprite::Editor* editor() const noexcept { return editor_.get(); }
    [[nodiscard]] SpriteCanvas* canvas() const noexcept { return canvas_; }
    [[nodiscard]] QTableWidget* timeline() const noexcept { return timeline_; }
    [[nodiscard]] QTreeWidget* layers() const noexcept { return layers_; }
    [[nodiscard]] QListWidget* tags() const noexcept { return tags_; }

    /// The tool that drags on the canvas do.
    void set_tool(StudioTool tool);
    [[nodiscard]] StudioTool tool() const noexcept { return tool_; }
    void set_primary(sprite::Color color);
    void set_secondary(sprite::Color color);
    [[nodiscard]] sprite::Color primary() const noexcept { return primary_; }
    [[nodiscard]] sprite::Color secondary() const noexcept { return secondary_; }
    /// The first and second colours change places.
    void swap_colors();
    [[nodiscard]] sprite::Brush& brush() noexcept { return brush_; }

    // The commands, for menus, buttons and tests.
    bool save();
    void undo();
    void redo();
    /// Plays the frames (looping in the tag of the current frame, if any), or stops.
    void set_playing(bool playing);
    [[nodiscard]] bool playing() const noexcept;
    /// Quiet: problems go to the status bar only, never to a dialog.
    void set_quiet(bool quiet) noexcept { quiet_ = quiet; }

    /// Shows everything again after a change.
    void refresh();

Q_SIGNALS:
    /// The sprite and its sheet were written to disk.
    void saved(const std::filesystem::path& file);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void build();
    void build_tools();
    void build_menus();
    QWidget* build_colors();
    QWidget* build_layers();
    QWidget* build_timeline();
    void refresh_palette();
    void refresh_layers();
    void refresh_timeline();
    /// Only what follows the current layer and frame, without rebuilding the lists.
    void refresh_current();
    void refresh_title();
    void report(const QString& message);
    /// Runs an edit that may be refused, and shows the result.
    void apply(const std::function<bool(std::string&)>& edit);
    void step_playback();
    void edit_tag(int index);

    std::unique_ptr<sprite::Editor> editor_;
    std::filesystem::path file_;
    StudioTool tool_ = StudioTool::Pencil;
    sprite::Brush brush_;
    sprite::Color primary_{0, 0, 0, 255};
    sprite::Color secondary_{255, 255, 255, 255};
    bool quiet_ = false;
    bool refreshing_ = false;
    int ping_pong_ = 1;

    SpriteCanvas* canvas_ = nullptr;
    QToolButton* primary_button_ = nullptr;
    QToolButton* secondary_button_ = nullptr;
    QWidget* palette_ = nullptr;
    QGridLayout* palette_grid_ = nullptr;
    QTreeWidget* layers_ = nullptr;
    QSlider* opacity_ = nullptr;
    QTableWidget* timeline_ = nullptr;
    QListWidget* tags_ = nullptr;
    QSpinBox* duration_ = nullptr;
    QCheckBox* all_frames_ = nullptr;
    QLabel* position_ = nullptr;
    QTimer* player_ = nullptr;
    QAction* undo_ = nullptr;
    QAction* redo_ = nullptr;
    QAction* play_ = nullptr;
    QAction* onion_ = nullptr;
    std::vector<QAction*> tool_actions_;
    QActionGroup* tool_group_ = nullptr; ///< The tools, one at a time.
};

} // namespace editor
