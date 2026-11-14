#pragma once

// The Recolour dialog (plan/02 B4): a palette swap picked by eye. Every colour of a sprite's
// picture is shown beside the colour it will be drawn as; one slider shifts them all round
// the colour wheel (a red slime from a green one), and any single one can be picked.
//
// See docs/dev/editor.md (Recolouring a sprite).

#include "engine/assets/assets.h"
#include "engine/scene2d/scene2d.h"

#include <QDialog>
#include <QImage>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

class QLabel;
class QPushButton;
class QSlider;

namespace editor {

class RecolorDialog final : public QDialog {
    Q_OBJECT

public:
    /// For `picture`'s colours, starting from `recolor` (a Sprite's).
    RecolorDialog(const engine::assets::Image& picture, const std::string& recolor,
                  QWidget* parent = nullptr);

    /// The colours of the picture, in the order first seen.
    [[nodiscard]] const std::vector<std::array<std::uint8_t, 3>>& colors() const noexcept {
        return colors_;
    }
    /// What colour `index` is drawn as.
    [[nodiscard]] std::array<std::uint8_t, 3> target(std::size_t index) const;
    /// Every colour turned `degrees` round the colour wheel (lightness kept). 0 is none.
    void set_hue_shift(int degrees);
    /// Colour `index` drawn as `rgb`.
    void set_target(std::size_t index, std::array<std::uint8_t, 3> rgb);
    /// Every colour as it is again.
    void reset();
    /// The Sprite's recolor text for the choice (only the colours that change).
    [[nodiscard]] std::string text() const;
    /// The picture as it will be drawn.
    [[nodiscard]] const QImage& preview() const noexcept { return preview_; }

private:
    void refresh();

    engine::assets::Image picture_;
    std::vector<std::array<std::uint8_t, 3>> colors_;
    std::vector<std::array<std::uint8_t, 3>> targets_;
    std::vector<QPushButton*> buttons_;
    QImage preview_;
    QLabel* before_ = nullptr;
    QLabel* after_ = nullptr;
    QSlider* hue_ = nullptr;
};

} // namespace editor
