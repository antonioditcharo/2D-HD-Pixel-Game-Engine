#include "tile_palette.h"

#include "engine/assets/assets.h"
#include "engine/assets/autotile.h"
#include "engine/scene2d/scene2d.h"

#include <QApplication>
#include <QButtonGroup>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QPixmap>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <string>
#include <utility>

namespace editor {

namespace scene2d = engine::scene2d;

TilePalette::TilePalette(QWidget* parent) : QWidget(parent) {
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(4, 4, 4, 4);
    hint_ = new QLabel(tr("Select a tilemap to paint its tiles."), this);
    hint_->setWordWrap(true);
    column->addWidget(hint_);
    // The tools: one at a time.
    tools_ = new QWidget(this);
    // Painting tools on the first row; the Diorama's terrain brushes on the second, so the
    // panel stays as narrow as it was.
    auto* rows = new QGridLayout(tools_);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setHorizontalSpacing(2);
    auto* group = new QButtonGroup(this);
    const std::array<std::pair<QString, QString>, 6> tools{
        std::pair{tr("Pencil"), tr("Paint cell by cell along the stroke")},
        std::pair{tr("Rectangle"), tr("Drag out a rectangle: it is painted on release")},
        std::pair{tr("Fill"), tr("Click a cell: every cell of the same tile joined to it")},
        std::pair{tr("Raise"),
                  tr("Diorama: each tile the stroke passes rises half a tile (the right button "
                     "lowers it)")},
        std::pair{tr("Lower"),
                  tr("Diorama: each tile the stroke passes sinks half a tile (the right button "
                     "raises it)")},
        std::pair{tr("Scatter"), tr("Paints some of the cells around the cursor, at random: "
                                    "flowers and rocks sprinkled. Ctrl+click tiles for a mix")}};
    for (int i = 0; i < 6; ++i) {
        auto* button = new QToolButton(tools_);
        button->setText(tools.at(static_cast<std::size_t>(i)).first);
        button->setToolTip(tools.at(static_cast<std::size_t>(i)).second);
        button->setCheckable(true);
        button->setChecked(i == 0);
        button->setObjectName(QStringLiteral("paint_tool:%1").arg(i));
        group->addButton(button, i);
        rows->addWidget(button, i / 3, i % 3);
    }
    rows->setColumnStretch(3, 1);
    auto* density = new QSpinBox(tools_);
    density->setObjectName(QStringLiteral("scatter_density"));
    density->setRange(1, 100);
    density->setValue(25);
    density->setSuffix(QStringLiteral("%"));
    density->setToolTip(tr("Scatter: how many of the cells around the cursor it paints"));
    rows->addWidget(new QLabel(tr("Density"), tools_), 2, 0);
    rows->addWidget(density, 2, 1, 1, 2);
    connect(density, &QSpinBox::valueChanged, this, [this](int v) { Q_EMIT density_changed(v); });
    connect(group, &QButtonGroup::idClicked, this, [this](int id) {
        tool_ = id;
        Q_EMIT tool_changed(id);
    });
    tools_->hide();
    column->addWidget(tools_);
    tiles_ = new QWidget(this);
    grid_ = new QGridLayout(tiles_);
    grid_->setSpacing(2);
    column->addWidget(tiles_);
    column->addStretch();
}

bool TilePalette::set_tile_shape(std::int32_t tile, scene2d::TileShape shape) {
    const engine::world::Entity shown =
        document_ != nullptr ? document_->world().find(map_) : engine::world::Entity{};
    const auto* tilemap =
        shown.is_valid() ? document_->world().get<scene2d::Tilemap>(shown) : nullptr;
    const engine::reflect::TypeInfo* type =
        document_ != nullptr ? document_->registry().find_type<scene2d::Tilemap>() : nullptr;
    if (tilemap == nullptr || type == nullptr || !tilemap->solid || tile < 0) {
        return false;
    }
    // The others as they were, this one changed (left out when it blocks all of it).
    std::string ignored;
    std::string text;
    constexpr std::array<std::string_view, 6> names{"full",   "none", "top",
                                                    "bottom", "left", "right"};
    const auto add = [&text, &names](std::int32_t number, scene2d::TileShape given) {
        if (given == scene2d::TileShape::Full) {
            return;
        }
        text += (text.empty() ? "" : ", ") + std::to_string(number) + " " +
                std::string(names.at(static_cast<std::size_t>(given)));
    };
    for (const auto& [number, given] : scene2d::parse_tile_shapes(tilemap->shapes, ignored)) {
        if (number != tile) {
            add(number, given);
        }
    }
    add(tile, shape);
    std::string error;
    if (!document_->set_property(map_, *type, *type->find_property("shapes"),
                                 engine::reflect::Value{text}, error)) {
        return false;
    }
    document_->end_merge();
    Q_EMIT edited();
    return true;
}

void TilePalette::show_tilemap(Document* document, engine::Guid entity) {
    choose(std::nullopt);
    // Later, not now (this may run from a button's own signal), and out of the palette
    // at once, so the old buttons are never found or shown again.
    for (QToolButton* button : buttons_) {
        grid_->removeWidget(button);
        button->hide();
        button->setParent(nullptr);
        button->deleteLater();
    }
    buttons_.clear();
    numbers_.clear();
    document_ = document;
    map_ = entity;
    const engine::world::Entity e =
        document != nullptr ? document->world().find(entity) : engine::world::Entity{};
    const auto* map = e.is_valid() ? document->world().get<scene2d::Tilemap>(e) : nullptr;
    const std::optional<engine::Guid> tileset =
        map != nullptr ? engine::Guid::parse(map->tileset) : std::nullopt;
    const std::optional<std::vector<std::uint8_t>> bytes =
        tileset ? document->assets().read(*tileset) : std::nullopt;
    std::string error;
    const std::optional<engine::assets::Image> picture =
        bytes ? engine::assets::decode_png(*bytes, error) : std::nullopt;
    if (!picture) {
        hint_->setText(map == nullptr ? tr("Select a tilemap to paint its tiles.")
                                      : tr("This tilemap has no tileset picture to paint with."));
        hint_->show();
        tools_->hide();
        return;
    }
    tools_->show();
    hint_->setText(tr("Left button paints, right button erases. Esc stops."));
    const QImage image = QImage(picture->rgba.data(), static_cast<int>(picture->width),
                                static_cast<int>(picture->height), QImage::Format_RGBA8888)
                             .copy();
    const std::uint32_t size =
        map->tile_size == 0 ? std::max(document->settings().ppu, 1U) : map->tile_size;
    const int columns = static_cast<int>(picture->width / size);
    const int rows = static_cast<int>(picture->height / size);
    constexpr int per_row = 6;
    const auto add = [this](QToolButton* button, std::int32_t tile) {
        numbers_.push_back(tile);
        button->setCheckable(true);
        button->setObjectName(QStringLiteral("tile:%1").arg(tile));
        connect(button, &QToolButton::clicked, this, [this, tile] {
            // Ctrl+click: several tiles, painted at random.
            if (QApplication::keyboardModifiers().testFlag(Qt::ControlModifier) && tile >= 0) {
                toggle_variant(tile);
                return;
            }
            if (!variants_.empty()) {
                variants_.clear();
                Q_EMIT variants_changed(variants_);
            }
            choose(chosen_ == tile ? std::nullopt : std::optional<std::int32_t>(tile));
        });
        // Right-click: how much of its square the tile blocks, on a solid map.
        if (tile >= 0) {
            button->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(button, &QWidget::customContextMenuRequested, this,
                    [this, button, tile](const QPoint& at) {
                        const engine::world::Entity shown = document_ != nullptr
                                                                ? document_->world().find(map_)
                                                                : engine::world::Entity{};
                        const auto* tilemap = shown.is_valid()
                                                  ? document_->world().get<scene2d::Tilemap>(shown)
                                                  : nullptr;
                        if (tilemap == nullptr) {
                            return;
                        }
                        std::string ignored;
                        scene2d::TileShape now = scene2d::TileShape::Full;
                        for (const auto& [number, shape] :
                             scene2d::parse_tile_shapes(tilemap->shapes, ignored)) {
                            if (number == tile) {
                                now = shape;
                            }
                        }
                        QMenu menu(this);
                        QMenu* blocks = menu.addMenu(tr("Blocks"));
                        blocks->setEnabled(tilemap->solid);
                        if (!tilemap->solid) {
                            blocks->setTitle(tr("Blocks (on a solid map)"));
                        }
                        const std::array<std::pair<scene2d::TileShape, QString>, 6> shapes{{
                            {scene2d::TileShape::Full, tr("All of it")},
                            {scene2d::TileShape::Top, tr("The top half")},
                            {scene2d::TileShape::Bottom, tr("The bottom half")},
                            {scene2d::TileShape::Left, tr("The left half")},
                            {scene2d::TileShape::Right, tr("The right half")},
                            {scene2d::TileShape::None, tr("Nothing")},
                        }};
                        for (const auto& [shape, label] : shapes) {
                            QAction* choice = blocks->addAction(label);
                            choice->setCheckable(true);
                            choice->setChecked(shape == now);
                            const scene2d::TileShape picked = shape;
                            connect(choice, &QAction::triggered, this,
                                    [this, tile, picked] { set_tile_shape(tile, picked); });
                        }
                        menu.exec(button->mapToGlobal(at));
                    });
        }
        const int index = static_cast<int>(buttons_.size());
        grid_->addWidget(button, index / per_row, index % per_row);
        buttons_.push_back(button);
    };
    const auto button_for = [&](int tile, const QString& tip) {
        const int column = tile % std::max(columns, 1);
        const int row = tile / std::max(columns, 1);
        const QImage cut = image.copy(column * static_cast<int>(size), row * static_cast<int>(size),
                                      static_cast<int>(size), static_cast<int>(size));
        auto* button = new QToolButton(tiles_);
        // Whole-number scale, so pixel art stays crisp.
        const int scale = std::max(1, 32 / static_cast<int>(size));
        button->setIcon(QPixmap::fromImage(cut.scaled(cut.size() * scale)));
        button->setIconSize(cut.size() * scale);
        button->setToolTip(tip);
        add(button, tile);
    };
    if (map->autotile) {
        // One per terrain: its preview, the last of its 48 tiles. Painting picks the shapes.
        hint_->setText(tr("Auto-tile: paint a terrain and its edges and corners follow. Right "
                          "button erases. Esc stops."));
        const int shapes = static_cast<int>(engine::assets::autotile_shapes);
        for (int terrain = 0; (terrain + 1) * shapes <= columns * rows; ++terrain) {
            button_for((terrain * shapes) + shapes - 1, tr("Terrain %1").arg(terrain + 1));
        }
    } else {
        for (int tile = 0; tile < columns * rows; ++tile) {
            button_for(tile, tr("Tile %1").arg(tile));
        }
    }
    auto* erase = new QToolButton(tiles_);
    erase->setText(tr("Erase"));
    erase->setToolTip(tr("Paint with nothing: removes tiles"));
    // The eraser is chosen as tile -1; its button is the last.
    erase->setCheckable(true);
    erase->setObjectName(QStringLiteral("tile:erase"));
    connect(erase, &QToolButton::clicked, this,
            [this] { choose(chosen_ == -1 ? std::nullopt : std::optional<std::int32_t>(-1)); });
    grid_->addWidget(erase, (static_cast<int>(buttons_.size()) / per_row) + 1, 0, 1, per_row);
    buttons_.push_back(erase);
    numbers_.push_back(-1);
}

void TilePalette::choose(std::optional<std::int32_t> tile) {
    chosen_ = tile;
    if (!tile || *tile < 0) {
        variants_.clear();
    }
    show_checks();
    Q_EMIT chosen_changed(tile);
}

void TilePalette::show_checks() {
    for (std::size_t i = 0; i < buttons_.size() && i < numbers_.size(); ++i) {
        const bool variant =
            std::find(variants_.begin(), variants_.end(), numbers_[i]) != variants_.end();
        buttons_[i]->setChecked((chosen_.has_value() && *chosen_ == numbers_[i]) || variant);
    }
}

void TilePalette::toggle_variant(std::int32_t tile) {
    if (variants_.empty() && chosen_.has_value() && *chosen_ >= 0 && *chosen_ != tile) {
        variants_.push_back(*chosen_);
    }
    if (const auto at = std::find(variants_.begin(), variants_.end(), tile);
        at != variants_.end()) {
        variants_.erase(at);
    } else {
        variants_.push_back(tile);
    }
    Q_EMIT variants_changed(variants_);
    // Painting goes on with one of them chosen.
    if (!variants_.empty() &&
        (!chosen_ || std::find(variants_.begin(), variants_.end(), *chosen_) == variants_.end())) {
        choose(variants_.front());
    } else {
        show_checks();
    }
}

} // namespace editor
