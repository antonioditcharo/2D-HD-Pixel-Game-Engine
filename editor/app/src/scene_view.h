#pragma once

// The scene view: the scene drawn as the game draws it (editor::Preview), at the largest
// whole-number scale that fits, with the selection outlined. In Classic 2D, clicking
// selects what is drawn on top and dragging moves it; dragging with the right or middle
// button, or the arrow keys, moves the camera.
//
// See docs/dev/editor.md.

#include <QImage>
#include <QPoint>
#include <QWidget>
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>

#include "editor/core/document.h"
#include "editor/core/preview.h"

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
    enum class PaintTool : std::uint8_t { Pencil, Rectangle, Fill };
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
    };
    [[nodiscard]] std::optional<Handles> handles() const;
    /// The point of this widget showing map point (x, y): on the map in Classic 2D, on the
    /// ground through the camera in the Diorama style (nothing if it is behind it).
    [[nodiscard]] std::optional<QPoint> on_screen(double x, double y) const;
    /// Draws the scene again (after an edit).
    void refresh();

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

Q_SIGNALS:
    void selected(engine::Guid entity);
    /// Ctrl- or Shift-click: `entity` joins the selection, or leaves it.
    void toggled(engine::Guid entity);
    /// A box was dragged from where nothing can be moved: these pictures are in it.
    void boxed(std::vector<engine::Guid> entities);
    /// The scene was changed here: an entity dragged, or tiles painted.
    void moved();
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
    [[nodiscard]] int scale() const;
    [[nodiscard]] QPoint origin() const;

    Document* document_ = nullptr;
    std::unique_ptr<Preview> preview_;
    QImage image_;
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
        Stretch
    } drag_ = Drag::None;
    /// For a drag by the turning or scaling handle: the Sprite's rotation and scales when
    /// it began, and the pivot on this widget.
    double turn_from_ = 0.0;
    double scale_x_from_ = 1.0;
    double scale_y_from_ = 1.0;
    QPointF pivot_;
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
