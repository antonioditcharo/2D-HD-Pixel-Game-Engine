#pragma once

// The "audio" object logic uses for sound: "Play sound [x]", "Play music [x] fading over
// [n] seconds", "Stop the music" and the volumes. Sounds are asset files (WAV, Ogg Vorbis,
// FLAC or MP3), named by GUID (what the editor writes) or by their path in assets/; each is
// read and decoded the first time it plays.
//
// See docs/dev/audio.md §4.

#include "engine/assets/assets.h"
#include "engine/audio/audio.h"
#include "engine/foundation/reflect.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::game {

class AudioObject {
public:
    AudioObject() = default;
    /// Without an engine (sound turned off, or no audio device), every action does nothing.
    AudioObject(audio::AudioEngine* engine, const assets::AssetDatabase* assets)
        : engine_(engine), assets_(assets) {}

    void play_sound(const std::string& sound);
    void play_music(const std::string& music, double fade_seconds);
    void stop_music(double fade_seconds);
    /// 0 to 100.
    void set_music_volume(std::int64_t percent);
    void set_sound_volume(std::int64_t percent);

    /// Forgets sounds whose files changed or went away, so they are read again next time.
    void apply(const assets::AssetChanges& changes);
    /// Sounds that could not be played, oldest first, in plain language.
    [[nodiscard]] std::vector<std::string> take_problems();

private:
    /// The engine key of a sound named by GUID or path, loading it if needed; empty after
    /// recording a problem.
    std::string prepare(const std::string& sound);

    audio::AudioEngine* engine_{nullptr};
    const assets::AssetDatabase* assets_{nullptr};
    std::vector<std::string> problems_;
};

/// Registers AudioObject as "audio".
void register_audio_types(reflect::Registry& registry);

} // namespace engine::game
