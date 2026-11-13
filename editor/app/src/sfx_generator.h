#pragma once

// The sound effect generator (plan/02 J2, plan/06 §3.8), laid out as sfxr and its peers lay
// it out: a button for each kind of sound (Coin, Jump, Hit...) that makes one and plays it,
// Mutate for a variation, the wave and every number to shape it, Play, and Save, which
// writes a WAV file into the project's sounds.
//
// See docs/dev/audio.md.

#include <QDialog>
#include <filesystem>
#include <memory>

#include "editor/core/sfx.h"

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;

namespace engine::audio {
class AudioEngine;
}

namespace editor {

class SfxGenerator : public QDialog {
    Q_OBJECT

public:
    /// Saves into `sounds`, the project's assets/sfx folder.
    explicit SfxGenerator(std::filesystem::path sounds, QWidget* parent = nullptr);
    ~SfxGenerator() override;
    SfxGenerator(const SfxGenerator&) = delete;
    SfxGenerator& operator=(const SfxGenerator&) = delete;
    SfxGenerator(SfxGenerator&&) = delete;
    SfxGenerator& operator=(SfxGenerator&&) = delete;

    [[nodiscard]] const sfx::Params& params() const noexcept { return params_; }
    /// Makes a sound of `preset` (a new one each time) and shows and plays it.
    void make(sfx::Preset preset);
    /// A variation on the sound as it is.
    void mutate();
    /// Plays the sound; false if there is no sound device.
    bool play();
    /// Writes the sound as assets/sfx/<name>.wav. False, with `error`, for a name that is
    /// empty or not a plain file name, one taken, or a file that cannot be written.
    bool save(const QString& name, std::string& error);
    [[nodiscard]] QLineEdit* name_field() const noexcept { return name_; }

Q_SIGNALS:
    /// A sound was written to the project.
    void saved(const std::filesystem::path& file);

private:
    void show_params();
    void read_params();

    std::filesystem::path sounds_;
    sfx::Params params_;
    std::uint32_t seed_ = 1;
    bool filling_ = false;
    std::unique_ptr<engine::audio::AudioEngine> audio_;
    QComboBox* wave_ = nullptr;
    std::vector<std::pair<QDoubleSpinBox*, double sfx::Params::*>> fields_;
    QLineEdit* name_ = nullptr;
};

} // namespace editor
