#pragma once

// The tile palette: the tiles of the selected tilemap's tileset, to paint with. Choosing a
// tile puts the scene view in paint mode (left button paints, right button erases), with
// the tool chosen above the tiles: a pencil, a rectangle or a fill;
// choosing it again, Escape, or selecting something else leaves it.
//
// See docs/dev/editor.md.

#include <QWidget>

#include "editor/core/document.h"

class QGridLayout;
class QLabel;
class QToolButton;

namespace editor {

class TilePalette : public QWidget {
    Q_OBJECT

public:
    explicit TilePalette(QWidget* parent = nullptr);

    /// Shows the tiles of `entity`'s tilemap, or a hint if it has none.
    void show_tilemap(Document* document, engine::Guid entity);
    /// The tile chosen, or nullopt when not painting.
    [[nodiscard]] std::optional<std::int32_t> chosen() const noexcept { return chosen_; }
    /// Chooses a tile (-1 erases), or stops painting with nullopt.
    void choose(std::optional<std::int32_t> tile);

    /// The tool chosen: 0 pencil, 1 rectangle, 2 fill, 3 raise, 4 lower, 5 scatter.
    [[nodiscard]] int tool() const noexcept { return tool_; }
    /// The tiles Ctrl+clicked together, painted at random for each cell; empty for one.
    [[nodiscard]] const std::vector<std::int32_t>& variants() const noexcept { return variants_; }
    /// Ctrl+click: adds a tile to (or takes it from) the variants painted at random.
    void toggle_variant(std::int32_t tile);
    /// Right-click ▸ Blocks: how much of its square `tile` blocks on the shown map, kept in
    /// its Tile shapes, one undo step. False if no solid map is shown.
    bool set_tile_shape(std::int32_t tile, engine::scene2d::TileShape shape);

Q_SIGNALS:
    void chosen_changed(std::optional<std::int32_t> tile);
    void tool_changed(int tool);
    void variants_changed(const std::vector<std::int32_t>& tiles);
    void density_changed(int density);
    /// The shown map was changed here (a tile's shape).
    void edited();

private:
    void show_checks();
    Document* document_ = nullptr;
    engine::Guid map_;
    std::optional<std::int32_t> chosen_;
    std::vector<std::int32_t> variants_;
    int tool_ = 0;
    QWidget* tools_ = nullptr;
    QLabel* hint_ = nullptr;
    QWidget* tiles_ = nullptr;
    QGridLayout* grid_ = nullptr;
    std::vector<QToolButton*> buttons_; ///< The tiles' buttons; the last one erases.
    std::vector<std::int32_t> numbers_; ///< The tile each button paints (-1: erase).
};

} // namespace editor
