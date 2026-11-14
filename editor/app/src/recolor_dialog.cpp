#include "recolor_dialog.h"

#include <QColor>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include <algorithm>

namespace editor {

namespace scene2d = engine::scene2d;

namespace {

QColor qcolor(const std::array<std::uint8_t, 3>& rgb) {
    return {rgb[0], rgb[1], rgb[2]};
}

std::array<std::uint8_t, 3> rgb_of(const QColor& c) {
    return {static_cast<std::uint8_t>(c.red()), static_cast<std::uint8_t>(c.green()),
            static_cast<std::uint8_t>(c.blue())};
}

/// `picture` as a QImage, scaled up `times` so its pixels can be seen.
QPixmap shown(const engine::assets::Image& picture, int times) {
    const QImage image(picture.rgba.data(), static_cast<int>(picture.width),
                       static_cast<int>(picture.height), static_cast<int>(picture.width * 4),
                       QImage::Format_RGBA8888);
    return QPixmap::fromImage(image.copy().scaled(image.width() * times, image.height() * times,
                                                  Qt::KeepAspectRatio, Qt::FastTransformation));
}

void paint_swatch(QPushButton* button, const QColor& c) {
    button->setStyleSheet(
        QStringLiteral("background: %1; border: 1px solid #000;").arg(c.name(QColor::HexRgb)));
    button->setToolTip(c.name(QColor::HexRgb));
}

} // namespace

RecolorDialog::RecolorDialog(const engine::assets::Image& picture, const std::string& recolor,
                             QWidget* parent)
    : QDialog(parent), picture_(picture), colors_(scene2d::image_colors(picture)) {
    setObjectName(QStringLiteral("recolor_dialog"));
    setWindowTitle(tr("Recolour"));
    targets_ = colors_;
    for (const scene2d::ColorSwap& swap : scene2d::parse_recolor(recolor)) {
        if (const auto at = std::ranges::find(colors_, swap.from); at != colors_.end()) {
            targets_.at(static_cast<std::size_t>(at - colors_.begin())) = swap.to;
        }
    }

    auto* column = new QVBoxLayout(this);
    auto* about = new QLabel(
        tr("The same picture, drawn in other colours: a red slime from a green one, a guard "
           "in another uniform. The picture itself is not changed."),
        this);
    about->setWordWrap(true);
    column->addWidget(about);
    auto* pictures = new QHBoxLayout();
    before_ = new QLabel(this);
    after_ = new QLabel(this);
    after_->setObjectName(QStringLiteral("recolor_preview"));
    pictures->addWidget(before_);
    pictures->addWidget(new QLabel(QStringLiteral("→"), this));
    pictures->addWidget(after_);
    pictures->addStretch();
    column->addLayout(pictures);

    auto* shift = new QHBoxLayout();
    shift->addWidget(new QLabel(tr("Shift every colour:"), this));
    hue_ = new QSlider(Qt::Horizontal, this);
    hue_->setObjectName(QStringLiteral("hue_shift"));
    hue_->setRange(0, 359);
    hue_->setToolTip(tr("Turns every colour round the colour wheel, keeping how light it is"));
    shift->addWidget(hue_);
    auto* plain = new QPushButton(tr("As it is"), this);
    shift->addWidget(plain);
    column->addLayout(shift);
    connect(hue_, &QSlider::valueChanged, this, [this](int degrees) { set_hue_shift(degrees); });
    connect(plain, &QPushButton::clicked, this, [this] { reset(); });

    // Each colour, and what it becomes: click to pick another.
    auto* grid_holder = new QWidget(this);
    auto* grid = new QGridLayout(grid_holder);
    for (std::size_t i = 0; i < colors_.size(); ++i) {
        // A pair: the colour, an arrow, and the button of what it becomes.
        auto* pair = new QWidget(grid_holder);
        auto* across = new QHBoxLayout(pair);
        across->setContentsMargins(0, 0, 8, 0);
        across->setSpacing(4);
        auto* from = new QLabel(pair);
        from->setFixedSize(20, 20);
        from->setStyleSheet(QStringLiteral("background: %1; border: 1px solid #000;")
                                .arg(qcolor(colors_[i]).name(QColor::HexRgb)));
        auto* to = new QPushButton(pair);
        to->setFixedSize(28, 20);
        buttons_.push_back(to);
        across->addWidget(from);
        across->addWidget(new QLabel(QStringLiteral("→"), pair));
        across->addWidget(to);
        grid->addWidget(pair, static_cast<int>(i / 4), static_cast<int>(i % 4));
        connect(to, &QPushButton::clicked, this, [this, i] {
            const QColor chosen =
                QColorDialog::getColor(qcolor(targets_.at(i)), this, tr("Drawn as"));
            if (chosen.isValid()) {
                set_target(i, rgb_of(chosen));
            }
        });
    }
    grid->setColumnStretch(4, 1);
    grid->setRowStretch((static_cast<int>(colors_.size()) / 4) + 1, 1);
    auto* scroll = new QScrollArea(this);
    scroll->setWidget(grid_holder);
    scroll->setWidgetResizable(true);
    scroll->setMinimumHeight(120);
    column->addWidget(scroll);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    column->addWidget(buttons);
    resize(520, 420);

    const int times = picture_.width > 0 ? std::max(1, 96 / static_cast<int>(picture_.width)) : 1;
    before_->setPixmap(shown(picture_, std::min(times, 8)));
    refresh();
}

std::array<std::uint8_t, 3> RecolorDialog::target(std::size_t index) const {
    return targets_.at(index);
}

void RecolorDialog::set_hue_shift(int degrees) {
    if (hue_->value() != degrees) {
        const QSignalBlocker quiet(hue_);
        hue_->setValue(degrees);
    }
    for (std::size_t i = 0; i < colors_.size(); ++i) {
        const QColor c = qcolor(colors_[i]);
        const int hue = c.hslHue();
        // Greys have no hue to turn.
        targets_[i] =
            hue < 0
                ? colors_[i]
                : rgb_of(QColor::fromHsl((hue + degrees) % 360, c.hslSaturation(), c.lightness()));
    }
    refresh();
}

void RecolorDialog::set_target(std::size_t index, std::array<std::uint8_t, 3> rgb) {
    targets_.at(index) = rgb;
    refresh();
}

void RecolorDialog::reset() {
    targets_ = colors_;
    const QSignalBlocker quiet(hue_);
    hue_->setValue(0);
    refresh();
}

std::string RecolorDialog::text() const {
    std::vector<scene2d::ColorSwap> swaps;
    for (std::size_t i = 0; i < colors_.size(); ++i) {
        if (targets_[i] != colors_[i]) {
            swaps.push_back(scene2d::ColorSwap{colors_[i], targets_[i]});
        }
    }
    return scene2d::recolor_text(swaps);
}

void RecolorDialog::refresh() {
    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        paint_swatch(buttons_[i], qcolor(targets_[i]));
    }
    engine::assets::Image drawn = picture_;
    scene2d::recolor_image(drawn, scene2d::parse_recolor(text()));
    const int times = drawn.width > 0 ? std::max(1, 96 / static_cast<int>(drawn.width)) : 1;
    const QPixmap pixmap = shown(drawn, std::min(times, 8));
    preview_ = pixmap.toImage();
    after_->setPixmap(pixmap);
}

} // namespace editor
