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
#include <cstdint>
#include <memory>

#include "editor/core/document.h"
#include "editor/core/preview.h"

namespace editor {

class SceneView : public QWidget {
    Q_OBJECT

public:
    explicit SceneView(QWidget* parent = nullptr);

    /// Shows `document`, with the camera on the middle of its map.
    void set_document(Document* document);
    void set_selection(engine::Guid entity);
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

Q_SIGNALS:
    void selected(engine::Guid entity);
    /// An entity was dragged to a new place.
    void moved();
    void problem(const QString& message);

protected:
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
    double camera_x_ = 0.0;
    double camera_y_ = 0.0;
    // A drag in progress.
    enum class Drag : std::uint8_t { None, Entity, Camera } drag_ = Drag::None;
    QPoint drag_from_;
    QPointF entity_from_;
};

} // namespace editor
