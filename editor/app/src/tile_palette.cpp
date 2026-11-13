#include "tile_palette.h"

#include "engine/assets/assets.h"
#include "engine/scene2d/scene2d.h"

#include <QButtonGroup>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPixmap>
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
    const std::array<std::pair<QString, QString>, 5> tools{
        std::pair{tr("Pencil"), tr("Paint cell by cell along the stroke")},
        std::pair{tr("Rectangle"), tr("Drag out a rectangle: it is painted on release")},
        std::pair{tr("Fill"), tr("Click a cell: every cell of the same tile joined to it")},
        std::pair{tr("Raise"),
                  tr("Diorama: each tile the stroke passes rises half a tile (the right button "
                     "lowers it)")},
        std::pair{tr("Lower"),
                  tr("Diorama: each tile the stroke passes sinks half a tile (the right button "
                     "raises it)")}};
    for (int i = 0; i < 5; ++i) {
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
        button->setCheckable(true);
        button->setObjectName(QStringLiteral("tile:%1").arg(tile));
        connect(button, &QToolButton::clicked, this, [this, tile] {
            choose(chosen_ == tile ? std::nullopt : std::optional<std::int32_t>(tile));
        });
        const int index = static_cast<int>(buttons_.size());
        grid_->addWidget(button, index / per_row, index % per_row);
        buttons_.push_back(button);
    };
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const QImage cut =
                image.copy(column * static_cast<int>(size), row * static_cast<int>(size),
                           static_cast<int>(size), static_cast<int>(size));
            auto* button = new QToolButton(tiles_);
            // Whole-number scale, so pixel art stays crisp.
            const int scale = std::max(1, 32 / static_cast<int>(size));
            button->setIcon(QPixmap::fromImage(cut.scaled(cut.size() * scale)));
            button->setIconSize(cut.size() * scale);
            button->setToolTip(tr("Tile %1").arg((row * columns) + column));
            add(button, (row * columns) + column);
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
}

void TilePalette::choose(std::optional<std::int32_t> tile) {
    chosen_ = tile;
    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        const bool erase = i + 1 == buttons_.size();
        const std::int32_t number = erase ? -1 : static_cast<std::int32_t>(i);
        buttons_[i]->setChecked(tile.has_value() && *tile == number);
    }
    Q_EMIT chosen_changed(tile);
}

} // namespace editor
