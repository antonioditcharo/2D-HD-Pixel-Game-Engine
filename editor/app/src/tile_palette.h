#pragma once

// The tile palette: the tiles of the selected tilemap's tileset, to paint with. Choosing a
// tile puts the scene view in paint mode (left button paints, right button erases);
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

Q_SIGNALS:
    void chosen_changed(std::optional<std::int32_t> tile);

private:
    std::optional<std::int32_t> chosen_;
    QLabel* hint_ = nullptr;
    QWidget* tiles_ = nullptr;
    QGridLayout* grid_ = nullptr;
    std::vector<QToolButton*> buttons_; ///< Index = tile number; the last one erases.
};

} // namespace editor
