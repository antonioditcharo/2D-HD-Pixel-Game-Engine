#pragma once

// The scene view: the scene drawn as the game draws it (editor::Preview), at the largest
// whole-number scale that fits, with the selection outlined. In Classic 2D, clicking
// selects what is drawn on top and dragging moves it; dragging with the right or middle
// button, or the arrow keys, moves the camera.
//
// See docs/dev/editor.md.

#include "engine/game/hud.h"

#include <QImage>
#include <QPoint>
#include <QRectF>
#include <QWidget>
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "editor/core/document.h"
#include "editor/core/preview.h"

class QPainter;

namespace editor {

/// Whether `guids` holds `guid`. (By comparison, not std::find: Microsoft's library under
/// clang-cl cannot search 16-byte values its fast way, and stops the build.)
[[nodiscard]] inline bool contains(const std::vector<engine::Guid>& guids, engine::Guid guid) {
    return std::find_if(guids.begin(), guids.end(), [guid](engine::Guid g) { return g == guid; }) !=
           guids.end();
}

class SceneView : public QWidget {
    Q_OBJECT

public:
    explicit SceneView(QWidget* parent = nullptr);

    /// Shows `document`, with the camera on the middle of its map.
    void set_document(Document* document);
    void set_selection(engine::Guid entity);
    /// Several entities selected together: each is outlined, and dragging any of them
    /// moves them all. The first is the one `selected` reported.
    void set_group(std::vector<engine::Guid> entities);
    /// Paint mode: the left button paints `tile` into `tilemap`, the right button erases;
    /// nullopt leaves it.
    void set_painting(engine::Guid tilemap, std::optional<std::int32_t> tile);
    [[nodiscard]] bool painting() const noexcept { return paint_tile_.has_value(); }
    /// How paint mode paints: cell by cell along the stroke, the rectangle between where
    /// the button went down and where it came up, or the whole area joined to the cell.
    /// Raise and Lower build Diorama terrain: each cell the stroke passes rises (or sinks)
    /// by half a tile; the right button does the opposite.
    /// Scatter paints some of the cells around the cursor, at random, as many in a hundred
    /// as the density: rocks and flowers sprinkled rather than laid in rows.
    enum class PaintTool : std::uint8_t { Pencil, Rectangle, Fill, Raise, Lower, Scatter };
    /// The Scatter tool's density: cells painted in a hundred, 1 to 100.
    void set_scatter_density(int density) noexcept { density_ = std::clamp(density, 1, 100); }
    void set_paint_tool(PaintTool tool) { paint_tool_ = tool; }
    [[nodiscard]] PaintTool paint_tool() const noexcept { return paint_tool_; }
    /// Snapping: what is dragged lands on whole tiles (the entity grabbed; others
    /// selected with it move by as much, keeping their places around it).
    void set_snap(bool snap) noexcept { snap_ = snap; }
    [[nodiscard]] bool snap() const noexcept { return snap_; }
    /// How finely snapping divides a tile: 1 (whole tiles), 2 (half tiles) or 4 (quarter
    /// tiles). Any other number is refused, changing nothing.
    bool set_snap_divisions(int divisions) noexcept;
    [[nodiscard]] int snap_divisions() const noexcept { return snap_divisions_; }
    /// The move handles of the selection: an arrow across (x) and one down (y) from the
    /// first selected entity's position, along the ground in the Diorama style. Dragging
    /// an arrow's head moves the selection along that axis only. Nothing when nothing
    /// movable is selected, or while painting.
    ///
    /// Two more, in either style: a round knob above the picture, which turns it about
    /// its position as it is dragged round (to 15 degrees at a time with snapping on), and
    /// a square at its top-right corner, which scales it evenly as it is dragged away from
    /// or towards its position (to halves with snapping on). Each drag is one undo step.
    /// In the Diorama style they sit around the billboard as the camera shows it, and the
    /// turn is in the billboard's own plane.
    struct Handles {
        QRect across;
        QRect down;
        QPoint origin;
        QRect turn;
        QRect stretch;
        /// In Classic 2D, a ring on the pivot (the position): dragged onto another point
        /// of the picture, it moves the pivot there, the picture staying where it is (to
        /// halves of the picture with snapping on). Empty in the Diorama style.
        QRect pivot;
    };
    [[nodiscard]] std::optional<Handles> handles() const;
    /// The point of this widget showing map point (x, y): on the map in Classic 2D, on the
    /// ground through the camera in the Diorama style (nothing if it is behind it).
    [[nodiscard]] std::optional<QPoint> on_screen(double x, double y) const;
    /// Draws the scene again (after an edit).
    void refresh();
    /// Draws again the pictures that `changes` modified (hot reload), then the scene.
    void assets_changed(const engine::assets::AssetChanges& changes);
    /// Shows `look` in place of the project's while a preset is hovered; nothing to stop.
    void preview_look(std::optional<engine::render::Look> look);

    /// Where the camera looks, in tiles.
    [[nodiscard]] double camera_x() const noexcept { return camera_x_; }
    [[nodiscard]] double camera_y() const noexcept { return camera_y_; }
    void set_camera(double x, double y);
    /// The last image drawn, at the game's resolution.
    [[nodiscard]] const QImage& image() const noexcept { return image_; }
    /// The map point under a point of this widget.
    [[nodiscard]] QPointF to_map(QPoint widget_point) const;
    /// The point of this widget showing map point (x, y), as to_map() reverses it.
    [[nodiscard]] QPoint from_map(double x, double y) const;
    /// The map point under a point of this widget, in either style: in Classic 2D as
    /// to_map(); in the Diorama style, where the ray through it meets the ground.
    [[nodiscard]] std::optional<QPointF> map_point(QPoint widget_point) const;
    /// The point of this widget showing image point (ndc_x, ndc_y), from -1 to 1, y up.
    [[nodiscard]] QPoint from_ndc(double ndc_x, double ndc_y) const;

    /// An on-screen element (a Screen Text, Bar or Picture) where this view last drew it.
    struct HudItem {
        engine::Guid entity;
        const engine::reflect::TypeInfo* component = nullptr;
        QRectF at; ///< On this widget.
        engine::game::ScreenAnchor anchor = engine::game::ScreenAnchor::TopLeft;
        double x = 0.0;
        double y = 0.0;
    };
    /// The on-screen elements as last painted, the topmost last. Clicking one selects it,
    /// and dragging it moves it by its X and Y from its Place.
    [[nodiscard]] const std::vector<HudItem>& hud_items() const noexcept { return hud_items_; }

Q_SIGNALS:
    void selected(engine::Guid entity);
    /// Ctrl- or Shift-click: `entity` joins the selection, or leaves it.
    void toggled(engine::Guid entity);
    /// A box was dragged from where nothing can be moved: these pictures are in it.
    void boxed(std::vector<engine::Guid> entities);
    /// The scene was changed here: an entity dragged, or tiles painted.
    void moved();
    /// A drag that changed the scene (moving, turning, scaling, an on-screen element) ended:
    /// the inspector shows the values it left.
    void drag_finished();
    /// Escape was pressed while painting.
    void stop_painting();
    void problem(const QString& message);
    /// A picture dragged from the assets was placed here as a new entity.
    void placed(engine::Guid entity);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    /// Screen Text and Screen Bars where the game shows them, over the frame drawn at
    /// `origin_at` at `s` times: their words as written, bars half full.
    void paint_hud(QPainter& painter, QPoint origin_at, int s) const;
    /// The picture of a Screen Picture, by its GUID, read once (again when it changes).
    [[nodiscard]] QImage hud_picture(const std::string& guid) const;
    [[nodiscard]] int scale() const;
    [[nodiscard]] QPoint origin() const;

    Document* document_ = nullptr;
    /// The cells a Raise or Lower stroke has changed already: each once a stroke.
    std::vector<std::pair<std::int64_t, std::int64_t>> raised_;
    int density_ = 25;
    std::uint32_t scatter_seed_ = 1; ///< Each scatter stroke's random choices.
    std::unique_ptr<Preview> preview_;
    QImage image_;
    /// hud_picture()'s pictures, with when their files changed.
    mutable std::map<std::string, std::pair<std::filesystem::file_time_type, QImage>> hud_pictures_;
    engine::Guid selection_;
    std::vector<engine::Guid> group_; ///< Everything selected, `selection_` first.
    /// A group being dragged: each entity and where it was when the drag began.
    std::vector<std::pair<engine::Guid, QPointF>> dragged_;
    double camera_x_ = 0.0;
    double camera_y_ = 0.0;
    // A drag in progress.
    enum class Drag : std::uint8_t {
        None,
        Entity,
        Camera,
        Paint,
        Box,
        Turn,
        Stretch,
        Pivot,
        Hud
    } drag_ = Drag::None;
    /// paint_hud()'s elements, kept for clicking and dragging them.
    mutable std::vector<HudItem> hud_items_;
    /// The on-screen element being dragged, as it was when the drag began.
    HudItem hud_grabbed_;
    /// For a drag by the pivot ring: the picture's corners on the map when it began.
    std::array<std::array<double, 2>, 4> corners_{};
    /// For a drag by the turning or scaling handle: each selected picture's position,
    /// rotation and scales when it began (the one grabbed first); the middle of their
    /// positions on the map, which a group turns and scales about; and that point (the
    /// grabbed one's position, for one picture) on this widget.
    std::vector<Document::SpriteShape> shaped_;
    QPointF middle_;
    QPointF pivot_;
    /// A map coordinate to the nearest whole pixel of the pictures, as moves are.
    [[nodiscard]] double on_pixel(double v) const;
    /// The pictures (entities with a Position and a Sprite) whose drawn rectangles touch
    /// `band`, a rectangle of this widget, in the scene's order.
    [[nodiscard]] std::vector<engine::Guid> pictures_in(QRect band) const;
    /// Paints the drag's tile at a widget point.
    void paint_at(QPoint widget_point);
    engine::Guid paint_map_;
    std::optional<std::int32_t> paint_tile_;
    std::int32_t stroke_tile_ = -1; ///< What this drag paints: the tile, or -1 to erase.
    PaintTool paint_tool_ = PaintTool::Pencil;
    QPoint rect_to_; ///< Where a rectangle being dragged reaches now.
    /// A box to select with has been dragged out: the button moved with it held. A press
    /// and release alone are a click, wherever they land.
    bool boxing_ = false;
    bool snap_ = false;
    int snap_divisions_ = 1;
    engine::Guid grabbed_; ///< The entity a drag of entities began on.
    /// Which way a drag by a handle may go: freely, or along one axis.
    enum class Axis : std::uint8_t { Free, Across, Down } axis_ = Axis::Free;
    /// Starts dragging the selection (what of it can move); false if none can.
    bool start_drag(engine::Guid grabbed, QPoint at);
    QPoint drag_from_;
    QPointF ground_from_; ///< The map point under the cursor when a drag began.
    [[nodiscard]] bool diorama() const;
    /// The image point (-1 to 1, y up) under a point of this widget.
    [[nodiscard]] std::array<double, 2> ndc(QPoint widget_point) const;
};

} // namespace editor
