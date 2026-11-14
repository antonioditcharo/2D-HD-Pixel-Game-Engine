#include "scene_view.h"

#include "engine/game/hud.h"
#include "engine/scene2d/scene2d.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QUrl>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

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
    if (paint_tool_ == PaintTool::Scatter) {
        if (!at) {
            return;
        }
        // The 3 x 3 cells around the cursor, each considered once a stroke.
        bool painted = false;
        const auto cx = static_cast<std::int64_t>(std::floor(at->x()));
        const auto cy = static_cast<std::int64_t>(std::floor(at->y()));
        for (std::int64_t dy = -1; dy <= 1; ++dy) {
            for (std::int64_t dx = -1; dx <= 1; ++dx) {
                const std::pair<std::int64_t, std::int64_t> cell{cx + dx, cy + dy};
                if (std::find(raised_.begin(), raised_.end(), cell) != raised_.end()) {
                    continue;
                }
                raised_.push_back(cell);
                scatter_seed_ ^= scatter_seed_ << 13U;
                scatter_seed_ ^= scatter_seed_ >> 17U;
                scatter_seed_ ^= scatter_seed_ << 5U;
                if (static_cast<int>(scatter_seed_ % 100U) < density_ &&
                    document_->paint_tile(paint_map_, static_cast<double>(cell.first) + 0.5,
                                          static_cast<double>(cell.second) + 0.5, stroke_tile_)) {
                    painted = true;
                }
            }
        }
        if (painted) {
            Q_EMIT moved();
            refresh();
        }
        return;
    }
    if (paint_tool_ == PaintTool::Raise || paint_tool_ == PaintTool::Lower) {
        if (!at) {
            return;
        }
        // Each cell once a stroke, so holding the button over it does not run away.
        const std::pair<std::int64_t, std::int64_t> tile{
            static_cast<std::int64_t>(std::floor(at->x())),
            static_cast<std::int64_t>(std::floor(at->y()))};
        if (std::find(raised_.begin(), raised_.end(), tile) != raised_.end()) {
            return;
        }
        raised_.push_back(tile);
        const double up = paint_tool_ == PaintTool::Raise ? 0.5 : -0.5;
        if (document_->raise_tile(paint_map_, at->x(), at->y(), stroke_tile_ < 0 ? -up : up)) {
            Q_EMIT moved();
            refresh();
        }
        return;
    }
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

void SceneView::assets_changed(const engine::assets::AssetChanges& changes) {
    if (preview_ != nullptr) {
        preview_->assets_changed(changes);
    }
    refresh();
}

void SceneView::preview_look(std::optional<engine::render::Look> look) {
    if (preview_ != nullptr) {
        preview_->set_look_override(look);
    }
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

void SceneView::paint_hud(QPainter& painter, QPoint origin_at, int s) const {
    if (document_ == nullptr) {
        return;
    }
    const engine::world::World& world = document_->world();
    hud_items_.clear();
    const auto remember = [&](engine::world::Entity e, const engine::reflect::TypeInfo* type,
                              const QRectF& at, engine::game::ScreenAnchor anchor, double x,
                              double y) {
        hud_items_.push_back(HudItem{world.guid(e), type, at, anchor, x, y});
    };
    const auto sw = static_cast<double>(image_.width());
    const auto sh = static_cast<double>(image_.height());
    // Where a w x h thing goes on the game's screen, as the game places it.
    const auto place = [&](engine::game::ScreenAnchor anchor, double x, double y, double w,
                           double h) {
        const int column = static_cast<int>(anchor) % 3;
        const int row = static_cast<int>(anchor) / 3;
        const double px = column == 0   ? x
                          : column == 1 ? std::floor((sw - w) / 2.0) + x
                                        : sw - w - x;
        const double py = row == 0 ? y : row == 1 ? std::floor((sh - h) / 2.0) + y : sh - h - y;
        return QRectF(origin_at.x() + (px * s), origin_at.y() + (py * s), w * s, h * s);
    };
    QFont font = painter.font();
    font.setPixelSize(std::max(8, 7 * s));
    // Hard-edged, as the game's pixel font is.
    font.setStyleStrategy(QFont::NoAntialias);
    painter.setFont(font);
    const QFontMetricsF metrics(font);

    // Screen Layouts: where their children's elements go (engine/game/hud.h's lay_out(), as
    // the game places them), or that they are hidden. Such an element is clicked and
    // dragged as its layout.
    const auto size_of = [&](engine::world::Entity e) -> std::optional<std::array<float, 2>> {
        if (const auto* picture = world.get<engine::game::ScreenPicture>(e);
            picture != nullptr && picture->visible) {
            const QImage source = hud_picture(picture->image);
            if (!source.isNull()) {
                return std::array<float, 2>{
                    static_cast<float>(picture->width > 0.0 ? picture->width : source.width()),
                    static_cast<float>(picture->height > 0.0 ? picture->height : source.height())};
            }
        }
        if (const auto* bar = world.get<engine::game::ScreenBar>(e);
            bar != nullptr && bar->visible) {
            return std::array<float, 2>{static_cast<float>(bar->width + 2.0),
                                        static_cast<float>(bar->height + 2.0)};
        }
        if (const auto* label = world.get<engine::game::ScreenText>(e);
            label != nullptr && label->visible) {
            const QString words =
                QString::fromUtf8(label->text.data(), static_cast<qsizetype>(label->text.size()));
            const double size = label->size >= 2 ? 2.0 : 1.0;
            return std::array<float, 2>{
                static_cast<float>(metrics.horizontalAdvance(words) * size / s),
                static_cast<float>(9.0 * size)};
        }
        return std::nullopt;
    };
    struct Laid {
        std::array<float, 2> at;
        engine::world::Entity layout;
    };
    std::unordered_map<std::uint64_t, Laid> laid;
    std::unordered_set<std::uint64_t> hidden;
    const auto* layout_type = world.registry().find_type<engine::game::ScreenLayout>();
    if (layout_type != nullptr) {
        world.each(*layout_type, [&](engine::world::Entity e, engine::reflect::ObjectRef ref) {
            const auto* layout = ref.as<engine::game::ScreenLayout>();
            if (layout == nullptr) {
                return;
            }
            std::vector<std::uint64_t> members;
            std::vector<std::array<float, 2>> sizes;
            for (const engine::world::Entity child : world.children(e)) {
                if (!layout->visible) {
                    hidden.insert(child.id);
                } else if (const std::optional<std::array<float, 2>> size = size_of(child)) {
                    members.push_back(child.id);
                    sizes.push_back(*size);
                }
            }
            const std::vector<std::array<float, 2>> at = engine::game::lay_out(
                *layout, sizes, static_cast<float>(sw), static_cast<float>(sh));
            for (std::size_t i = 0; i < members.size(); ++i) {
                laid[members[i]] = Laid{at[i], e};
            }
        });
    }
    // Where an element is drawn, and remembered for clicking: by its layout, if it has one.
    const auto position = [&](engine::world::Entity e, const engine::reflect::TypeInfo* type,
                              engine::game::ScreenAnchor anchor, double x, double y, double w,
                              double h) {
        if (const auto found = laid.find(e.id); found != laid.end()) {
            const QRectF at(origin_at.x() + (static_cast<double>(found->second.at[0]) * s),
                            origin_at.y() + (static_cast<double>(found->second.at[1]) * s), w * s,
                            h * s);
            const auto* layout = world.get<engine::game::ScreenLayout>(found->second.layout);
            remember(found->second.layout, layout_type, at, layout->anchor, layout->x, layout->y);
            return at;
        }
        const QRectF at = place(anchor, x, y, w, h);
        remember(e, type, at, anchor, x, y);
        return at;
    };

    // Screen Pictures first, under the bars and words, as the game draws them.
    if (const auto* type = world.registry().find_type<engine::game::ScreenPicture>()) {
        world.each(*type, [&](engine::world::Entity e, engine::reflect::ObjectRef ref) {
            if (hidden.contains(e.id)) {
                return;
            }
            const auto* picture = ref.as<engine::game::ScreenPicture>();
            if (picture == nullptr || !picture->visible) {
                return;
            }
            const QImage source = hud_picture(picture->image);
            if (source.isNull()) {
                return;
            }
            const auto pw = static_cast<float>(source.width());
            const auto ph = static_cast<float>(source.height());
            const double w = picture->width > 0.0 ? picture->width : pw;
            const double h = picture->height > 0.0 ? picture->height : ph;
            const QRectF at = position(e, type, picture->anchor, picture->x, picture->y, w, h);
            painter.save();
            painter.setOpacity(std::clamp(picture->opacity, 0.0, 1.0));
            // The same pieces as the game's (engine/game/hud.h), in screen pixels.
            for (const engine::game::SlicePiece& piece : engine::game::nine_slice(
                     pw, ph,
                     engine::render::Rect{0.0F, 0.0F, static_cast<float>(w), static_cast<float>(h)},
                     static_cast<float>(picture->border))) {
                const auto scaled = [s](float v) { return static_cast<double>(v) * s; };
                const QRectF to(at.x() + scaled(piece.destination.x),
                                at.y() + scaled(piece.destination.y), scaled(piece.destination.w),
                                scaled(piece.destination.h));
                painter.drawImage(
                    to, source,
                    QRectF(piece.source.x, piece.source.y, piece.source.w, piece.source.h));
            }
            painter.restore();
        });
    }
    if (const auto* type = world.registry().find_type<engine::game::ScreenText>()) {
        world.each(*type, [&](engine::world::Entity e, engine::reflect::ObjectRef ref) {
            if (hidden.contains(e.id)) {
                return;
            }
            const auto* label = ref.as<engine::game::ScreenText>();
            if (label == nullptr || !label->visible) {
                return;
            }
            const QString words =
                QString::fromUtf8(label->text.data(), static_cast<qsizetype>(label->text.size()));
            const double size = label->size >= 2 ? 2.0 : 1.0;
            const QRectF at = position(e, type, label->anchor, label->x, label->y,
                                       metrics.horizontalAdvance(words) * size / s, 9.0 * size);
            painter.setPen(QColor(0, 0, 0, 180));
            painter.drawText(at.translated(s, s), Qt::AlignLeft | Qt::AlignVCenter, words);
            painter.setPen(Qt::white);
            painter.drawText(at, Qt::AlignLeft | Qt::AlignVCenter, words);
        });
    }
    if (const auto* type = world.registry().find_type<engine::game::ScreenBar>()) {
        world.each(*type, [&](engine::world::Entity e, engine::reflect::ObjectRef ref) {
            if (hidden.contains(e.id)) {
                return;
            }
            const auto* bar = ref.as<engine::game::ScreenBar>();
            if (bar == nullptr || !bar->visible) {
                return;
            }
            const QRectF at =
                position(e, type, bar->anchor, bar->x, bar->y, bar->width + 2.0, bar->height + 2.0);
            painter.fillRect(at, QColor(230, 232, 240));
            const QRectF inside = at.adjusted(s, s, -s, -s);
            painter.fillRect(inside, QColor(18, 20, 40));
            const QColor fill(
                QString::fromUtf8(bar->color.data(), static_cast<qsizetype>(bar->color.size())));
            painter.fillRect(
                QRectF(inside.topLeft(), QSizeF(inside.width() / 2.0, inside.height())),
                fill.isValid() ? fill : QColor(220, 70, 70));
        });
    }
    for (const HudItem& item : hud_items_) {
        if (item.entity == selection_) {
            painter.save();
            painter.setPen(QPen(QColor(255, 210, 122), 1, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(item.at.adjusted(-2, -2, 1, 1));
            painter.restore();
        }
    }
}

QImage SceneView::hud_picture(const std::string& guid) const {
    const std::optional<engine::Guid> id = engine::Guid::parse(guid);
    const engine::assets::AssetRecord* record =
        id && document_ != nullptr ? document_->assets().find(*id) : nullptr;
    if (record == nullptr) {
        return {};
    }
    const std::filesystem::path file =
        document_->assets().root() /
        std::filesystem::path(std::u8string(record->path.begin(), record->path.end()));
    std::error_code failed;
    const std::filesystem::file_time_type changed = std::filesystem::last_write_time(file, failed);
    if (const auto found = hud_pictures_.find(guid);
        found != hud_pictures_.end() && found->second.first == changed) {
        return found->second.second;
    }
    const QImage image(QString::fromStdU16String(file.u16string()));
    hud_pictures_[guid] = {changed, image};
    return image;
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
    paint_hud(painter, o, s);
    // A rectangle being dragged out, before it is painted or selected with.
    if ((drag_ == Drag::Paint && paint_tool_ == PaintTool::Rectangle) ||
        (drag_ == Drag::Box && boxing_)) {
        painter.setPen(QPen(QColor(255, 255, 255), 1, Qt::DashLine));
        painter.drawRect(QRect(drag_from_, rect_to_).normalized());
    }
    // The move handles: an arrow across and one down.
    if (const std::optional<Handles> h = handles()) {
        painter.setPen(QPen(QColor(230, 70, 60), 2));
        painter.drawLine(h->origin, h->across.center());
        painter.fillRect(h->across, QColor(230, 70, 60));
        painter.setPen(QPen(QColor(70, 200, 90), 2));
        painter.drawLine(h->origin, h->down.center());
        painter.fillRect(h->down, QColor(70, 200, 90));
        // Turning: a blue knob on a stalk from the position; scaling: a yellow square.
        if (!h->turn.isNull()) {
            painter.setPen(QPen(QColor(80, 150, 240), 1, Qt::DotLine));
            painter.drawLine(h->origin, h->turn.center());
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(80, 150, 240));
            painter.drawEllipse(h->turn);
            painter.setBrush(Qt::NoBrush);
        }
        if (!h->stretch.isNull()) {
            painter.fillRect(h->stretch, QColor(240, 200, 60));
        }
        // The pivot: a white ring round the position, with a dot in it.
        if (!h->pivot.isNull()) {
            painter.setPen(QPen(QColor(255, 255, 255), 2));
            painter.drawEllipse(h->pivot.adjusted(1, 1, -1, -1));
            painter.fillRect(QRect(h->origin - QPoint(1, 1), QSize(2, 2)), QColor(255, 255, 255));
        }
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

bool SceneView::start_drag(engine::Guid grabbed, QPoint at) {
    // Characters and props are dragged; maps stay where they are.
    dragged_.clear();
    for (const engine::Guid member : group_) {
        const engine::world::Entity m = document_->world().find(member);
        const auto* where =
            m.is_valid() ? document_->world().get<scene2d::Transform2D>(m) : nullptr;
        if (where != nullptr && document_->world().get<scene2d::Sprite>(m) != nullptr) {
            dragged_.emplace_back(member, QPointF(where->x, where->y));
        }
    }
    const std::optional<QPointF> ground = map_point(at);
    const bool movable =
        std::any_of(dragged_.begin(), dragged_.end(),
                    [grabbed](const auto& member) { return member.first == grabbed; });
    if (!movable || !ground) {
        return false;
    }
    drag_ = Drag::Entity;
    grabbed_ = grabbed;
    ground_from_ = *ground;
    document_->end_merge();
    return true;
}

bool SceneView::set_snap_divisions(int divisions) noexcept {
    if (divisions != 1 && divisions != 2 && divisions != 4) {
        return false;
    }
    snap_divisions_ = divisions;
    return true;
}

double SceneView::on_pixel(double v) const {
    const double ppu =
        document_ != nullptr ? static_cast<double>(std::max(1U, document_->settings().ppu)) : 1.0;
    return std::round(v * ppu) / ppu;
}

std::optional<QPoint> SceneView::on_screen(double x, double y) const {
    if (document_ == nullptr || image_.isNull()) {
        return std::nullopt;
    }
    if (!diorama()) {
        return from_map(x, y);
    }
    const engine::render::Mat4 vp = scene2d::diorama_view_projection(
        diorama_camera(document_->settings(), camera_x_, camera_y_),
        static_cast<float>(image_.width()) / static_cast<float>(image_.height()));
    const auto clip =
        vp.transform(engine::render::Vec3{static_cast<float>(x), 0.0F, static_cast<float>(y)});
    if (!(clip[3] > 0.0F)) {
        return std::nullopt;
    }
    return from_ndc(clip[0] / clip[3], clip[1] / clip[3]);
}

std::optional<SceneView::Handles> SceneView::handles() const {
    if (document_ == nullptr || image_.isNull() || paint_tile_) {
        return std::nullopt;
    }
    const engine::world::Entity e = document_->world().find(selection_);
    const auto* at = e.is_valid() ? document_->world().get<scene2d::Transform2D>(e) : nullptr;
    if (at == nullptr || document_->world().get<scene2d::Sprite>(e) == nullptr) {
        return std::nullopt;
    }
    constexpr double length = 34.0; ///< From the position to the middle of an arrow's head.
    constexpr int head = 10;
    // Each arrow points the way one tile across, or down, goes on screen.
    const std::optional<QPoint> origin = on_screen(at->x, at->y);
    const std::optional<QPoint> across = on_screen(at->x + 1.0, at->y);
    const std::optional<QPoint> down = on_screen(at->x, at->y + 1.0);
    if (!origin || !across || !down) {
        return std::nullopt;
    }
    const auto tip = [&](QPoint toward) -> std::optional<QRect> {
        const QPointF way = QPointF(toward - *origin);
        const double size = std::hypot(way.x(), way.y());
        if (!(size > 0.5)) {
            return std::nullopt; // seen end on: no way to drag along it
        }
        const QPoint middle = *origin + (way * (length / size)).toPoint();
        return QRect(middle - QPoint(head / 2, head / 2), QSize(head, head));
    };
    const std::optional<QRect> across_head = tip(*across);
    const std::optional<QRect> down_head = tip(*down);
    if (!across_head || !down_head) {
        return std::nullopt;
    }
    Handles found{*across_head, *down_head, *origin, QRect(), QRect(), QRect()};
    // Turning and scaling: by the box around the picture as drawn (in 3D, around the
    // billboard as the camera sees it).
    std::optional<std::pair<QPoint, QPoint>> top; // its top's middle and right end
    if (diorama()) {
        if (const auto rect = diorama_sprite_rect(
                document_->world(), document_->assets(), e,
                diorama_camera(document_->settings(), camera_x_, camera_y_),
                static_cast<float>(image_.width()) / static_cast<float>(image_.height()))) {
            const auto [left, bottom, right, upper] = *rect; // y up
            top.emplace(from_ndc((left + right) / 2.0, upper), from_ndc(right, upper));
        }
    } else if (const std::optional<scene2d::Box> box = sprite_bounds(
                   document_->world(), document_->assets(), e, document_->settings().ppu)) {
        top.emplace(from_map((box->left + box->right) / 2.0, box->top),
                    from_map(box->right, box->top));
    }
    if (!diorama()) {
        constexpr int ring = 12;
        found.pivot = QRect(found.origin - QPoint(ring / 2, ring / 2), QSize(ring, ring));
    }
    if (top) {
        found.turn = QRect(top->first + QPoint(-(head / 2), -18 - (head / 2)), QSize(head, head));
        found.stretch =
            QRect(top->second + QPoint(6 - (head / 2), -6 - (head / 2)), QSize(head, head));
    }
    return found;
}

void SceneView::mousePressEvent(QMouseEvent* event) {
    setFocus();
    drag_from_ = event->position().toPoint();
    if (paint_tile_ && document_ != nullptr &&
        (event->button() == Qt::LeftButton || event->button() == Qt::RightButton)) {
        drag_ = Drag::Paint;
        stroke_tile_ = event->button() == Qt::LeftButton ? *paint_tile_ : -1;
        document_->end_merge(); // each stroke is one undo step
        raised_.clear();
        rect_to_ = drag_from_;
        if (paint_tool_ == PaintTool::Fill) {
            drag_ = Drag::None; // one click, no stroke
            const std::optional<QPointF> at = map_point(drag_from_);
            if (at && document_->fill_tiles(paint_map_, at->x(), at->y(), stroke_tile_)) {
                Q_EMIT moved();
                refresh();
            }
        } else if (paint_tool_ == PaintTool::Pencil || paint_tool_ == PaintTool::Raise ||
                   paint_tool_ == PaintTool::Lower || paint_tool_ == PaintTool::Scatter) {
            paint_at(drag_from_);
        }
        return;
    }
    // On a move handle's head: the selection moves along that axis only.
    if (event->button() == Qt::LeftButton && document_ != nullptr && !image_.isNull()) {
        if (const std::optional<Handles> h = handles()) {
            const QPoint at = event->position().toPoint();
            if (h->across.contains(at) || h->down.contains(at)) {
                if (start_drag(selection_, at)) {
                    axis_ = h->across.contains(at) ? Axis::Across : Axis::Down;
                }
                return;
            }
            if (h->pivot.contains(at)) {
                const engine::world::Entity e = document_->world().find(selection_);
                if (const auto corners = e.is_valid() ? sprite_corners_on_map(
                                                            document_->world(), document_->assets(),
                                                            e, document_->settings().ppu)
                                                      : std::nullopt) {
                    corners_ = *corners;
                    drag_ = Drag::Pivot;
                    document_->end_merge();
                    return;
                }
            }
            if (h->turn.contains(at) || h->stretch.contains(at)) {
                // Every selected picture, the one the handles belong to first.
                shaped_.clear();
                std::vector<engine::Guid> members{selection_};
                for (const engine::Guid member : group_) {
                    if (member != selection_) {
                        members.push_back(member);
                    }
                }
                QPointF sum;
                for (const engine::Guid member : members) {
                    const engine::world::Entity m = document_->world().find(member);
                    const auto* sprite =
                        m.is_valid() ? document_->world().get<scene2d::Sprite>(m) : nullptr;
                    const auto* where =
                        m.is_valid() ? document_->world().get<scene2d::Transform2D>(m) : nullptr;
                    if (sprite != nullptr && where != nullptr) {
                        const auto finite_or = [](double v, double otherwise) {
                            return std::isfinite(v) ? v : otherwise;
                        };
                        shaped_.push_back(Document::SpriteShape{
                            member, where->x, where->y, finite_or(sprite->rotation, 0.0),
                            finite_or(sprite->scale_x, 1.0), finite_or(sprite->scale_y, 1.0)});
                        sum += QPointF(where->x, where->y);
                    }
                }
                if (!shaped_.empty() && shaped_.front().entity == selection_) {
                    middle_ = sum / static_cast<double>(shaped_.size());
                    const std::optional<QPoint> middle = on_screen(middle_.x(), middle_.y());
                    drag_ = h->turn.contains(at) ? Drag::Turn : Drag::Stretch;
                    pivot_ = shaped_.size() > 1 && middle ? QPointF(*middle) : QPointF(h->origin);
                    document_->end_merge();
                    return;
                }
            }
        }
    }
    axis_ = Axis::Free;
    // On an on-screen element (the topmost): selected, and dragged across the screen.
    if (event->button() == Qt::LeftButton && document_ != nullptr && !image_.isNull() &&
        (event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) == 0) {
        for (auto item = hud_items_.rbegin(); item != hud_items_.rend(); ++item) {
            if (item->component != nullptr && item->at.contains(QPointF(drag_from_))) {
                hud_grabbed_ = *item;
                selection_ = item->entity;
                group_.assign(1U, item->entity);
                Q_EMIT selected(item->entity);
                drag_ = Drag::Hud;
                document_->end_merge();
                update();
                return;
            }
        }
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
        const bool in_group = group_.size() > 1 && guid.is_valid() && contains(group_, guid);
        if (!in_group) {
            selection_ = guid;
            group_.assign(guid.is_valid() ? 1U : 0U, guid);
            Q_EMIT selected(guid);
        }
        if (!start_drag(guid, drag_from_)) {
            // Nothing to move here (the ground, or nothing): a drag draws a box to select
            // with; a click stays a click.
            drag_ = Drag::Box;
            rect_to_ = drag_from_;
            boxing_ = false;
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
    if (drag_ == Drag::Hud) {
        const int s = scale();
        const double dx = std::round(static_cast<double>(delta.x()) / s);
        const double dy = std::round(static_cast<double>(delta.y()) / s);
        const int column = static_cast<int>(hud_grabbed_.anchor) % 3;
        const int row = static_cast<int>(hud_grabbed_.anchor) / 3;
        const double x = hud_grabbed_.x + (column == 2 ? -dx : dx);
        const double y = hud_grabbed_.y + (row == 2 ? -dy : dy);
        std::string error;
        if (hud_grabbed_.component != nullptr &&
            document_->place_on_screen(hud_grabbed_.entity, *hud_grabbed_.component, x, y, error)) {
            Q_EMIT moved();
            update();
        }
        return;
    }
    if (drag_ == Drag::Box) {
        // Only a move with the button held, and beyond a few pixels, makes it a box (a
        // move Qt makes up when widgets change under the cursor has no button held).
        if ((event->buttons() & Qt::LeftButton) != 0 &&
            (boxing_ || (event->position().toPoint() - drag_from_).manhattanLength() > 6)) {
            boxing_ = true;
            rect_to_ = event->position().toPoint();
            update();
        }
        return;
    }
    if (drag_ == Drag::Pivot) {
        // Where the cursor is in the picture's own frame, from 0 to 1 across its top edge
        // (a) and down its left edge (b): q - c0 = a (c1 - c0) + b (c3 - c0).
        const QPointF q = to_map(event->position().toPoint());
        const auto& c0 = corners_[0]; // top-left
        const auto& c1 = corners_[1]; // top-right
        const auto& c3 = corners_[3]; // bottom-left
        const double ux = c1[0] - c0[0];
        const double uy = c1[1] - c0[1];
        const double vx = c3[0] - c0[0];
        const double vy = c3[1] - c0[1];
        const double qx = q.x() - c0[0];
        const double qy = q.y() - c0[1];
        const double det = (ux * vy) - (uy * vx);
        if (std::abs(det) > 1e-12) {
            double a = ((qx * vy) - (qy * vx)) / det;
            double b = ((ux * qy) - (uy * qx)) / det;
            if (snap_) {
                a = std::round(a * 2.0) / 2.0;
                b = std::round(b * 2.0) / 2.0;
            }
            if (document_->set_sprite_pivot(selection_, a, b)) {
                Q_EMIT moved();
                refresh();
            }
        }
        return;
    }
    if (drag_ == Drag::Turn || drag_ == Drag::Stretch) {
        const QPointF from = QPointF(drag_from_) - pivot_;
        const QPointF now = event->position() - pivot_;
        bool changed = false;
        if (drag_ == Drag::Turn) {
            // By as much as the cursor went round the position: 0 degrees is straight up,
            // clockwise as seen.
            const auto bearing = [](QPointF v) {
                return std::atan2(v.x(), -v.y()) * 180.0 / 3.14159265358979323846;
            };
            // The one grabbed lands on 15 degrees with snapping; the others turn as much.
            const double grabbed = shaped_.front().rotation;
            double degrees = grabbed + (bearing(now) - bearing(from));
            if (snap_) {
                degrees = std::round(degrees / 15.0) * 15.0;
            }
            if (shaped_.size() == 1) {
                changed = document_->turn_sprite(selection_, degrees);
            } else {
                // The group swings round its middle, each picture turning as much.
                const double turn = degrees - grabbed;
                const double radians = turn * 3.14159265358979323846 / 180.0;
                const double c = std::cos(radians);
                const double s = std::sin(radians);
                std::vector<Document::SpriteShape> turned = shaped_;
                for (Document::SpriteShape& shape : turned) {
                    const double dx = shape.x - middle_.x();
                    const double dy = shape.y - middle_.y();
                    shape.x = on_pixel(middle_.x() + (dx * c) - (dy * s));
                    shape.y = on_pixel(middle_.y() + (dx * s) + (dy * c));
                    shape.rotation += turn;
                }
                changed = document_->shape_sprites(turned, true);
            }
        } else {
            // By as much as the cursor went away from the position, the same both ways.
            const double start = std::hypot(from.x(), from.y());
            const double factor = start > 1.0 ? std::hypot(now.x(), now.y()) / start : 1.0;
            // The one grabbed lands on halves with snapping; the others grow as much.
            double by = factor;
            const double across = std::abs(shaped_.front().scale_x);
            if (snap_ && across > 0.0) {
                by = std::clamp(std::round(across * factor * 2.0) / 2.0, 0.5, 16.0) / across;
            }
            const auto scaled = [by](double from_scale) {
                const double sign = from_scale < 0.0 ? -1.0 : 1.0;
                return sign * std::clamp(std::abs(from_scale) * by, 0.0625, 16.0);
            };
            if (shaped_.size() == 1) {
                changed = document_->scale_sprite(selection_, scaled(shaped_.front().scale_x),
                                                  scaled(shaped_.front().scale_y));
            } else {
                // The group spreads from its middle (or draws in), each picture as much.
                std::vector<Document::SpriteShape> grown = shaped_;
                for (Document::SpriteShape& shape : grown) {
                    shape.x = on_pixel(middle_.x() + ((shape.x - middle_.x()) * by));
                    shape.y = on_pixel(middle_.y() + ((shape.y - middle_.y()) * by));
                    shape.scale_x = scaled(shape.scale_x);
                    shape.scale_y = scaled(shape.scale_y);
                }
                changed = document_->shape_sprites(grown, false);
            }
        }
        if (changed) {
            Q_EMIT moved();
            refresh();
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
        QPointF moved_by = *ground - ground_from_;
        if (axis_ == Axis::Across) {
            moved_by.setY(0.0);
        } else if (axis_ == Axis::Down) {
            moved_by.setX(0.0);
        }
        if (snap_) {
            // By as much as puts the grabbed entity on the nearest point of the grid.
            const double n = snap_divisions_;
            const auto on_grid = [n](double v) { return std::round(v * n) / n; };
            for (const auto& [member, from] : dragged_) {
                if (member == grabbed_) {
                    moved_by = QPointF(on_grid(from.x() + moved_by.x()) - from.x(),
                                       on_grid(from.y() + moved_by.y()) - from.y());
                }
            }
        }
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

std::vector<engine::Guid> SceneView::pictures_in(QRect band) const {
    std::vector<engine::Guid> found;
    if (document_ == nullptr || image_.isNull()) {
        return found;
    }
    const engine::world::World& world = document_->world();
    for (const engine::world::Entity e : document_->entities()) {
        if (world.get<scene2d::Sprite>(e) == nullptr ||
            world.get<scene2d::Transform2D>(e) == nullptr) {
            continue;
        }
        // Where the picture is drawn on this widget, as its outline is.
        QRect drawn;
        if (diorama()) {
            const auto rect = diorama_sprite_rect(
                world, document_->assets(), e,
                diorama_camera(document_->settings(), camera_x_, camera_y_),
                static_cast<float>(image_.width()) / static_cast<float>(image_.height()));
            if (!rect) {
                continue;
            }
            drawn = QRect(from_ndc((*rect)[0], (*rect)[3]), from_ndc((*rect)[2], (*rect)[1]));
        } else {
            const auto box =
                sprite_bounds(world, document_->assets(), e, document_->settings().ppu);
            if (!box) {
                continue;
            }
            drawn = QRect(from_map(box->left, box->top), from_map(box->right, box->bottom));
        }
        if (drawn.normalized().intersects(band)) {
            found.push_back(world.guid(e));
        }
    }
    return found;
}

void SceneView::mouseReleaseEvent(QMouseEvent* event) {
    if (drag_ == Drag::Box) {
        drag_ = Drag::None;
        // A click selected what it was on already.
        if (boxing_) {
            boxing_ = false;
            Q_EMIT boxed(pictures_in(QRect(drag_from_, rect_to_).normalized()));
        }
        update();
        return;
    }
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
    if ((drag_ == Drag::Entity || drag_ == Drag::Paint || drag_ == Drag::Turn ||
         drag_ == Drag::Stretch || drag_ == Drag::Pivot || drag_ == Drag::Hud) &&
        document_ != nullptr) {
        document_->end_merge();
        if (drag_ != Drag::Paint) {
            drag_ = Drag::None;
            Q_EMIT drag_finished();
        }
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
        // A scene becomes a door to it; anything else is placed as itself.
        const engine::Guid placed_entity =
            path.ends_with(".dscene") ? document_->place_door(path, at->x(), at->y(), error)
                                      : document_->place_asset(path, at->x(), at->y(), error);
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
