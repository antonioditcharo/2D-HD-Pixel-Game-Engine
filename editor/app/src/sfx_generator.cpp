#include "sfx_generator.h"

#include "engine/audio/audio.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <fstream>
#include <string>
#include <utility>

namespace editor {

namespace fs = std::filesystem;

SfxGenerator::SfxGenerator(fs::path sounds, QWidget* parent)
    : QDialog(parent), sounds_(std::move(sounds)) {
    setObjectName(QStringLiteral("sfx_generator"));
    setWindowTitle(tr("Sound effect generator"));
    auto* column = new QVBoxLayout(this);

    // A sound of each kind, a click away, as in sfxr.
    column->addWidget(new QLabel(tr("Make a sound of a kind (each click, another one):"), this));
    auto* kinds = new QGridLayout;
    int at = 0;
    for (const sfx::Preset preset : sfx::presets) {
        auto* button = new QPushButton(
            QString::fromUtf8(sfx::preset_name(preset).data(),
                              static_cast<qsizetype>(sfx::preset_name(preset).size())),
            this);
        button->setObjectName(QStringLiteral("preset:%1").arg(static_cast<int>(preset)));
        connect(button, &QPushButton::clicked, this, [this, preset] { make(preset); });
        kinds->addWidget(button, at / 4, at % 4);
        ++at;
    }
    column->addLayout(kinds);

    auto* form = new QFormLayout;
    wave_ = new QComboBox(this);
    wave_->setObjectName(QStringLiteral("sfx_wave"));
    wave_->addItems({tr("Square"), tr("Saw"), tr("Sine"), tr("Triangle"), tr("Noise")});
    form->addRow(tr("Wave"), wave_);
    connect(wave_, &QComboBox::currentIndexChanged, this, [this] { read_params(); });
    const auto field = [&](const QString& label, const QString& tip, double sfx::Params::*member,
                           double low, double high, double step, int decimals) {
        auto* box = new QDoubleSpinBox(this);
        box->setRange(low, high);
        box->setSingleStep(step);
        box->setDecimals(decimals);
        box->setToolTip(tip);
        form->addRow(label, box);
        fields_.emplace_back(box, member);
        connect(box, &QDoubleSpinBox::valueChanged, this, [this] { read_params(); });
    };
    field(tr("Attack (s)"), tr("How long it takes to rise to full volume"), &sfx::Params::attack, 0,
          sfx::longest, 0.01, 3);
    field(tr("Sustain (s)"), tr("How long it stays at full volume"), &sfx::Params::sustain, 0,
          sfx::longest, 0.01, 3);
    field(tr("Decay (s)"), tr("How long it takes to fade away"), &sfx::Params::decay, 0,
          sfx::longest, 0.01, 3);
    field(tr("Pitch (Hz)"), tr("How high it starts"), &sfx::Params::frequency, 20, 4000, 10, 0);
    field(tr("Slide (octaves/s)"), tr("The pitch moves up (above 0) or down (below 0)"),
          &sfx::Params::slide, -8, 8, 0.1, 2);
    field(tr("Vibrato depth"), tr("How much the pitch wobbles"), &sfx::Params::vibrato_depth, 0,
          0.5, 0.01, 2);
    field(tr("Vibrato speed"), tr("Wobbles a second"), &sfx::Params::vibrato_speed, 0, 40, 0.5, 1);
    field(tr("Duty"), tr("Square waves: thin and nasal (low) to full (0.5)"), &sfx::Params::duty,
          0.05, 0.95, 0.05, 2);
    field(tr("Duty sweep"), tr("How the duty changes a second"), &sfx::Params::duty_sweep, -2, 2,
          0.05, 2);
    field(tr("Jump to"),
          tr("The pitch jumps to this many times itself (a coin's two notes); 1 "
             "for none"),
          &sfx::Params::arpeggio, 0.25, 4, 0.05, 2);
    field(tr("Jump after (s)"), tr("When the pitch jumps"), &sfx::Params::arpeggio_time, 0,
          sfx::longest, 0.01, 3);
    field(tr("Brightness"), tr("1 lets every pitch through; lower is duller"),
          &sfx::Params::lowpass, 0.01, 1, 0.05, 2);
    field(tr("Volume"), tr("How loud"), &sfx::Params::volume, 0, 1, 0.05, 2);
    column->addLayout(form);

    auto* buttons = new QHBoxLayout;
    auto* play_button = new QPushButton(tr("Play"), this);
    play_button->setObjectName(QStringLiteral("sfx_play"));
    connect(play_button, &QPushButton::clicked, this, [this] { play(); });
    auto* mutate_button = new QPushButton(tr("Mutate"), this);
    mutate_button->setToolTip(tr("A variation on this sound"));
    connect(mutate_button, &QPushButton::clicked, this, [this] { mutate(); });
    name_ = new QLineEdit(QStringLiteral("coin"), this);
    name_->setObjectName(QStringLiteral("sfx_name"));
    name_->setToolTip(tr("Saved as assets/sfx/<name>.wav"));
    auto* save_button = new QPushButton(tr("Save to the project"), this);
    save_button->setObjectName(QStringLiteral("sfx_save"));
    connect(save_button, &QPushButton::clicked, this, [this] {
        std::string error;
        if (!save(name_->text(), error)) {
            setWindowTitle(tr("Sound effect generator — %1").arg(QString::fromStdString(error)));
        }
    });
    buttons->addWidget(play_button);
    buttons->addWidget(mutate_button);
    buttons->addStretch();
    buttons->addWidget(new QLabel(tr("Name"), this));
    buttons->addWidget(name_);
    buttons->addWidget(save_button);
    column->addLayout(buttons);

    // Sound, if there is a device; the generator still makes and saves without one.
    if (!QApplication::platformName().startsWith(QStringLiteral("offscreen"))) {
        std::string error;
        audio_ = engine::audio::AudioEngine::create(engine::audio::AudioOptions{}, error);
    }
    params_ = sfx::make(sfx::Preset::Coin, seed_);
    show_params();
}

SfxGenerator::~SfxGenerator() = default;

void SfxGenerator::show_params() {
    filling_ = true;
    wave_->setCurrentIndex(static_cast<int>(params_.wave));
    for (const auto& [box, member] : fields_) {
        box->setValue(params_.*member);
    }
    filling_ = false;
}

void SfxGenerator::read_params() {
    if (filling_) {
        return;
    }
    params_.wave = static_cast<sfx::Wave>(std::max(wave_->currentIndex(), 0));
    for (const auto& [box, member] : fields_) {
        params_.*member = box->value();
    }
    params_ = sfx::clamped(params_);
}

void SfxGenerator::make(sfx::Preset preset) {
    ++seed_;
    params_ = sfx::make(preset, seed_ * 2654435761U);
    show_params();
    const std::string_view name = sfx::preset_name(preset);
    QString file_name =
        QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size())).toLower();
    file_name.remove(QLatin1Char('-'));
    name_->setText(file_name);
    play();
}

void SfxGenerator::mutate() {
    ++seed_;
    params_ = sfx::mutate(params_, seed_ * 2246822519U);
    show_params();
    play();
}

bool SfxGenerator::play() {
    if (audio_ == nullptr) {
        return false;
    }
    std::vector<std::uint8_t> wav = sfx::to_wav(sfx::synthesize(params_));
    return audio_->load("sfx_preview", std::move(wav)) && audio_->play_sound("sfx_preview");
}

bool SfxGenerator::save(const QString& name, std::string& error) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed.contains(QLatin1Char('/')) ||
        trimmed.contains(QLatin1Char('\\')) || trimmed.startsWith(QLatin1Char('.'))) {
        error = "A sound needs a name without slashes, such as coin.";
        return false;
    }
    const QByteArray utf8 = (trimmed + QStringLiteral(".wav")).toUtf8();
    const fs::path file = sounds_ / fs::path(std::u8string(utf8.begin(), utf8.end()));
    std::error_code ignored;
    if (fs::exists(file, ignored)) {
        error = "There is a sound called " + trimmed.toStdString() + " already.";
        return false;
    }
    fs::create_directories(sounds_, ignored);
    const std::vector<std::uint8_t> bytes = sfx::to_wav(sfx::synthesize(params_));
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), // NOLINT
              static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        error = "The sound could not be written.";
        return false;
    }
    out.close();
    Q_EMIT saved(file);
    return true;
}

} // namespace editor
