#include "scene_view.h"

#include "engine/scene2d/scene2d.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <string>

namespace editor {

namespace scene2d = engine::scene2d;

SceneView::SceneView(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 180);
    setAccessibleName(tr("Scene"));
    std::string error;
    preview_ = Preview::create(error);
    if (preview_ == nullptr) {
        image_ = QImage();
        setToolTip(tr("The scene cannot be drawn: %1").arg(QString::fromStdString(error)));
    }
}

void SceneView::set_document(Document* document) {
    document_ = document;
    selection_ = engine::Guid{};
    camera_x_ = 0.0;
    camera_y_ = 0.0;
    if (document_ != nullptr) {
        if (const auto bounds =
                scene2d::map_bounds(document_->world(), document_->settings().ppu)) {
            camera_x_ = (bounds->left + bounds->right) / 2.0;
            camera_y_ = (bounds->top + bounds->bottom) / 2.0;
        }
    }
    refresh();
}

void SceneView::set_selection(engine::Guid entity) {
    selection_ = entity;
    update();
}

void SceneView::set_camera(double x, double y) {
    camera_x_ = x;
    camera_y_ = y;
    refresh();
}

void SceneView::refresh() {
    if (document_ == nullptr || preview_ == nullptr) {
        image_ = QImage();
        update();
        return;
    }
    std::string error;
    const std::optional<engine::assets::Image> frame =
        preview_->render(*document_, camera_x_, camera_y_, error);
    if (!frame) {
        image_ = QImage();
        Q_EMIT problem(QString::fromStdString(error));
    } else {
        image_ = QImage(frame->rgba.data(), static_cast<int>(frame->width),
                        static_cast<int>(frame->height), QImage::Format_RGBA8888)
                     .copy(); // the pixels outlive `frame`
    }
    update();
}

int SceneView::scale() const {
    if (image_.isNull()) {
        return 1;
    }
    return std::max(1, std::min(width() / image_.width(), height() / image_.height()));
}

QPoint SceneView::origin() const {
    const int s = scale();
    return {(width() - (image_.width() * s)) / 2, (height() - (image_.height() * s)) / 2};
}

QPointF SceneView::to_map(QPoint widget_point) const {
    if (document_ == nullptr || image_.isNull()) {
        return {};
    }
    const double ppu = std::max(1U, document_->settings().ppu);
    const double s = scale();
    const QPoint o = origin();
    const double image_x = (widget_point.x() - o.x()) / s;
    const double image_y = (widget_point.y() - o.y()) / s;
    return {camera_x_ + ((image_x - (image_.width() / 2.0)) / ppu),
            camera_y_ + ((image_y - (image_.height() / 2.0)) / ppu)};
}

QPoint SceneView::from_map(double x, double y) const {
    if (document_ == nullptr || image_.isNull()) {
        return {};
    }
    const double ppu = std::max(1U, document_->settings().ppu);
    const double s = scale();
    const QPoint o = origin();
    return {
        o.x() + static_cast<int>(std::lround(((x - camera_x_) * ppu + image_.width() / 2.0) * s)),
        o.y() + static_cast<int>(std::lround(((y - camera_y_) * ppu + image_.height() / 2.0) * s))};
}

void SceneView::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(24, 24, 28));
    if (image_.isNull()) {
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter,
                         document_ == nullptr ? tr("Open a project to see its scene.")
                                              : tr("The scene cannot be drawn."));
        return;
    }
    const int s = scale();
    const QPoint o = origin();
    painter.drawImage(QRect(o, image_.size() * s), image_);
    // The selection's outline, in Classic 2D where the picture is where the map says.
    if (document_ != nullptr &&
        document_->settings().style == engine::game::RenderStyle::Classic2D) {
        const engine::world::Entity e = document_->world().find(selection_);
        if (e.is_valid()) {
            if (const auto box = sprite_bounds(document_->world(), document_->assets(), e,
                                               document_->settings().ppu)) {
                painter.setPen(QPen(QColor(255, 220, 60), 2));
                painter.drawRect(QRect(from_map(box->left, box->top),
                                       from_map(box->right, box->bottom) - QPoint(1, 1)));
            }
        }
    }
}

void SceneView::mousePressEvent(QMouseEvent* event) {
    setFocus();
    drag_from_ = event->position().toPoint();
    if (event->button() == Qt::LeftButton && document_ != nullptr &&
        document_->settings().style == engine::game::RenderStyle::Classic2D) {
        const QPointF at = to_map(drag_from_);
        const engine::world::Entity e = pick(document_->world(), document_->assets(), at.x(),
                                             at.y(), document_->settings().ppu);
        const engine::Guid guid = e.is_valid() ? document_->world().guid(e) : engine::Guid{};
        selection_ = guid;
        Q_EMIT selected(guid);
        // Characters and props are dragged; maps stay where they are.
        const auto* transform =
            e.is_valid() ? document_->world().get<scene2d::Transform2D>(e) : nullptr;
        if (transform != nullptr && document_->world().get<scene2d::Sprite>(e) != nullptr) {
            drag_ = Drag::Entity;
            entity_from_ = QPointF(transform->x, transform->y);
            document_->end_merge();
        }
        update();
    } else if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton) {
        drag_ = Drag::Camera;
    }
}

void SceneView::mouseMoveEvent(QMouseEvent* event) {
    if (document_ == nullptr || drag_ == Drag::None) {
        return;
    }
    const double ppu = std::max(1U, document_->settings().ppu);
    const double per_pixel = 1.0 / (ppu * scale());
    const QPoint delta = event->position().toPoint() - drag_from_;
    if (drag_ == Drag::Entity) {
        // To whole pixels of the picture, as the game draws it.
        const double x = std::round((entity_from_.x() + delta.x() * per_pixel) * ppu) / ppu;
        const double y = std::round((entity_from_.y() + delta.y() * per_pixel) * ppu) / ppu;
        if (document_->move_entity(selection_, x, y)) {
            Q_EMIT moved();
            refresh();
        }
    } else {
        drag_from_ = event->position().toPoint();
        set_camera(camera_x_ - delta.x() * per_pixel, camera_y_ - delta.y() * per_pixel);
    }
}

void SceneView::mouseReleaseEvent(QMouseEvent* /*event*/) {
    if (drag_ == Drag::Entity && document_ != nullptr) {
        document_->end_merge();
    }
    drag_ = Drag::None;
}

void SceneView::keyPressEvent(QKeyEvent* event) {
    const double step = 1.0; // a tile
    switch (event->key()) {
    case Qt::Key_Left:
        set_camera(camera_x_ - step, camera_y_);
        break;
    case Qt::Key_Right:
        set_camera(camera_x_ + step, camera_y_);
        break;
    case Qt::Key_Up:
        set_camera(camera_x_, camera_y_ - step);
        break;
    case Qt::Key_Down:
        set_camera(camera_x_, camera_y_ + step);
        break;
    default:
        QWidget::keyPressEvent(event);
    }
}

} // namespace editor
