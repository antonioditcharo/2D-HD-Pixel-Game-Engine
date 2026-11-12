#include "scene_view.h"

#include "engine/scene2d/scene2d.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

namespace editor {

namespace scene2d = engine::scene2d;

SceneView::SceneView(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 180);
    setAcceptDrops(true); // pictures dragged from the assets list
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
    group_.clear();
    if (entity.is_valid()) {
        group_.push_back(entity);
    }
    update();
}

void SceneView::set_group(std::vector<engine::Guid> entities) {
    group_ = std::move(entities);
    selection_ = group_.empty() ? engine::Guid{} : group_.front();
    update();
}

void SceneView::set_painting(engine::Guid tilemap, std::optional<std::int32_t> tile) {
    paint_map_ = tilemap;
    paint_tile_ = tile;
    setCursor(tile ? Qt::CrossCursor : Qt::ArrowCursor);
}

bool SceneView::diorama() const {
    return document_ != nullptr &&
           document_->settings().style == engine::game::RenderStyle::Diorama;
}

std::array<double, 2> SceneView::ndc(QPoint widget_point) const {
    if (image_.isNull()) {
        return {0.0, 0.0};
    }
    const double s = scale();
    const QPoint o = origin();
    const double ix = (widget_point.x() - o.x() + 0.5) / s;
    const double iy = (widget_point.y() - o.y() + 0.5) / s;
    return {((ix / image_.width()) * 2.0) - 1.0, 1.0 - ((iy / image_.height()) * 2.0)};
}

QPoint SceneView::from_ndc(double ndc_x, double ndc_y) const {
    const double s = scale();
    const QPoint o = origin();
    return {o.x() + static_cast<int>(std::lround((ndc_x + 1.0) / 2.0 * image_.width() * s)),
            o.y() + static_cast<int>(std::lround((1.0 - ndc_y) / 2.0 * image_.height() * s))};
}

std::optional<QPointF> SceneView::map_point(QPoint widget_point) const {
    if (document_ == nullptr || image_.isNull()) {
        return std::nullopt;
    }
    if (!diorama()) {
        return to_map(widget_point);
    }
    const std::array<double, 2> at = ndc(widget_point);
    const auto hit = scene2d::ground_hit(scene2d::diorama_ray(
        diorama_camera(document_->settings(), camera_x_, camera_y_),
        static_cast<float>(image_.width()) / static_cast<float>(image_.height()), at[0], at[1]));
    if (!hit) {
        return std::nullopt;
    }
    return QPointF((*hit)[0], (*hit)[1]);
}

void SceneView::paint_at(QPoint widget_point) {
    if (document_ == nullptr) {
        return;
    }
    const std::optional<QPointF> at = map_point(widget_point);
    if (at && document_->paint_tile(paint_map_, at->x(), at->y(), stroke_tile_)) {
        Q_EMIT moved();
        refresh();
    }
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
    // A rectangle being dragged out, before it is painted.
    if (drag_ == Drag::Paint && paint_tool_ == PaintTool::Rectangle) {
        painter.setPen(QPen(QColor(255, 255, 255), 1, Qt::DashLine));
        painter.drawRect(QRect(drag_from_, rect_to_).normalized());
    }
    // Each selected entity's outline (the first brighter): in 3D, around the billboard
    // as the camera sees it.
    for (const engine::Guid guid : group_) {
        const engine::world::Entity e =
            document_ != nullptr ? document_->world().find(guid) : engine::world::Entity{};
        if (!e.is_valid()) {
            continue;
        }
        painter.setPen(QPen(guid == selection_ ? QColor(255, 220, 60) : QColor(200, 170, 50), 2));
        if (diorama()) {
            if (const auto rect = diorama_sprite_rect(
                    document_->world(), document_->assets(), e,
                    diorama_camera(document_->settings(), camera_x_, camera_y_),
                    static_cast<float>(image_.width()) / static_cast<float>(image_.height()))) {
                painter.drawRect(
                    QRect(from_ndc((*rect)[0], (*rect)[3]), from_ndc((*rect)[2], (*rect)[1])));
            }
        } else if (const auto box = sprite_bounds(document_->world(), document_->assets(), e,
                                                  document_->settings().ppu)) {
            painter.drawRect(QRect(from_map(box->left, box->top),
                                   from_map(box->right, box->bottom) - QPoint(1, 1)));
        }
    }
}

void SceneView::mousePressEvent(QMouseEvent* event) {
    setFocus();
    drag_from_ = event->position().toPoint();
    if (paint_tile_ && document_ != nullptr &&
        (event->button() == Qt::LeftButton || event->button() == Qt::RightButton)) {
        drag_ = Drag::Paint;
        stroke_tile_ = event->button() == Qt::LeftButton ? *paint_tile_ : -1;
        document_->end_merge(); // each stroke is one undo step
        rect_to_ = drag_from_;
        if (paint_tool_ == PaintTool::Fill) {
            drag_ = Drag::None; // one click, no stroke
            const std::optional<QPointF> at = map_point(drag_from_);
            if (at && document_->fill_tiles(paint_map_, at->x(), at->y(), stroke_tile_)) {
                Q_EMIT moved();
                refresh();
            }
        } else if (paint_tool_ == PaintTool::Pencil) {
            paint_at(drag_from_);
        }
        return;
    }
    if (event->button() == Qt::LeftButton && document_ != nullptr && !image_.isNull()) {
        engine::world::Entity e;
        if (diorama()) {
            const std::array<double, 2> at = ndc(drag_from_);
            e = pick_diorama(document_->world(), document_->assets(),
                             diorama_camera(document_->settings(), camera_x_, camera_y_),
                             static_cast<float>(image_.width()) /
                                 static_cast<float>(image_.height()),
                             at[0], at[1]);
        } else {
            const QPointF at = to_map(drag_from_);
            e = pick(document_->world(), document_->assets(), at.x(), at.y(),
                     document_->settings().ppu);
        }
        const engine::Guid guid = e.is_valid() ? document_->world().guid(e) : engine::Guid{};
        // Ctrl or Shift: in or out of the selection, and no drag.
        if ((event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) != 0) {
            if (guid.is_valid()) {
                Q_EMIT toggled(guid);
            }
            update();
            return;
        }
        // On one of a group: the group stays, to be dragged together.
        const bool in_group = group_.size() > 1 && guid.is_valid() &&
                              std::find(group_.begin(), group_.end(), guid) != group_.end();
        if (!in_group) {
            selection_ = guid;
            group_.assign(guid.is_valid() ? 1U : 0U, guid);
            Q_EMIT selected(guid);
        }
        // Characters and props are dragged; maps stay where they are.
        dragged_.clear();
        for (const engine::Guid member : group_) {
            const engine::world::Entity m = document_->world().find(member);
            const auto* at =
                m.is_valid() ? document_->world().get<scene2d::Transform2D>(m) : nullptr;
            if (at != nullptr && document_->world().get<scene2d::Sprite>(m) != nullptr) {
                dragged_.emplace_back(member, QPointF(at->x, at->y));
            }
        }
        const std::optional<QPointF> ground = map_point(drag_from_);
        const bool movable =
            std::any_of(dragged_.begin(), dragged_.end(),
                        [guid](const auto& member) { return member.first == guid; });
        if (movable && ground) {
            drag_ = Drag::Entity;
            ground_from_ = *ground;
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
    if (drag_ == Drag::Paint) {
        if (paint_tool_ == PaintTool::Rectangle) {
            rect_to_ = event->position().toPoint(); // painted when the button comes up
            update();
        } else {
            paint_at(event->position().toPoint());
        }
        return;
    }
    if (drag_ == Drag::Entity) {
        // By as much as the map point under the cursor moved (on the ground, in 3D), to
        // whole pixels of the picture, as the game draws it.
        const std::optional<QPointF> ground = map_point(event->position().toPoint());
        if (!ground) {
            return;
        }
        const QPointF moved_by = *ground - ground_from_;
        std::vector<std::pair<engine::Guid, std::array<double, 2>>> to;
        to.reserve(dragged_.size());
        for (const auto& [member, from] : dragged_) {
            to.emplace_back(
                member, std::array<double, 2>{std::round((from.x() + moved_by.x()) * ppu) / ppu,
                                              std::round((from.y() + moved_by.y()) * ppu) / ppu});
        }
        if (document_->move_entities(to)) {
            Q_EMIT moved();
            refresh();
        }
    } else {
        drag_from_ = event->position().toPoint();
        set_camera(camera_x_ - delta.x() * per_pixel, camera_y_ - delta.y() * per_pixel);
    }
}

void SceneView::mouseReleaseEvent(QMouseEvent* event) {
    if (drag_ == Drag::Paint && paint_tool_ == PaintTool::Rectangle && document_ != nullptr) {
        const std::optional<QPointF> from = map_point(drag_from_);
        const std::optional<QPointF> to = map_point(event->position().toPoint());
        if (from && to &&
            document_->paint_tile_rect(paint_map_, from->x(), from->y(), to->x(), to->y(),
                                       stroke_tile_)) {
            Q_EMIT moved();
            refresh();
        }
        update();
    }
    if ((drag_ == Drag::Entity || drag_ == Drag::Paint) && document_ != nullptr) {
        document_->end_merge();
    }
    drag_ = Drag::None;
}

void SceneView::dragEnterEvent(QDragEnterEvent* event) {
    if (document_ != nullptr && event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void SceneView::dragMoveEvent(QDragMoveEvent* event) {
    if (document_ != nullptr && event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void SceneView::dropEvent(QDropEvent* event) {
    if (document_ == nullptr || !event->mimeData()->hasUrls()) {
        return;
    }
    const std::optional<QPointF> at = map_point(event->position().toPoint());
    if (!at) {
        Q_EMIT problem(tr("Drop it on the ground: that point is not on the map."));
        return;
    }
    // Both made absolute and canonical, so a project opened by a relative path or through
    // a link still matches the files the assets list offers.
    std::error_code ignored;
    const std::filesystem::path assets =
        std::filesystem::weakly_canonical(document_->folder() / "assets", ignored);
    for (const QUrl& url : event->mimeData()->urls()) {
        // The file's path in assets/, as the asset database names it.
        const std::filesystem::path file = std::filesystem::weakly_canonical(
            std::filesystem::path(url.toLocalFile().toStdU16String()), ignored);
        const std::filesystem::path relative = file.lexically_proximate(assets);
        const std::string path = relative.generic_string();
        if (url.toLocalFile().isEmpty() || path.empty() || path.starts_with("..")) {
            Q_EMIT problem(tr("Only files from the project's assets can be placed."));
            continue;
        }
        std::string error;
        const engine::Guid placed_entity = document_->place_asset(path, at->x(), at->y(), error);
        if (!placed_entity.is_valid()) {
            Q_EMIT problem(QString::fromStdString(error));
            continue;
        }
        selection_ = placed_entity;
        group_.assign(1, placed_entity);
        Q_EMIT placed(placed_entity);
    }
    event->acceptProposedAction();
    refresh();
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
    case Qt::Key_Escape:
        if (paint_tile_) {
            Q_EMIT stop_painting();
        }
        break;
    default:
        QWidget::keyPressEvent(event);
    }
}

} // namespace editor
