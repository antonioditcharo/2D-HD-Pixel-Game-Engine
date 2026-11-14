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
#include "engine/world/world.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace engine::game {

class AudioObject {
public:
    AudioObject() = default;
    /// Without an engine (sound turned off, or no audio device), every action does nothing.
    /// With a world, sounds can be played where someone is, heard from the player.
    AudioObject(audio::AudioEngine* engine, const assets::AssetDatabase* assets,
                const world::World* world = nullptr)
        : engine_(engine), assets_(assets), world_(world) {}

    void play_sound(const std::string& sound);
    /// Plays a sound at `pitch` (1 as recorded), for voice blips; not a logic action.
    void play_voice(const std::string& sound, double pitch);
    /// Plays a sound where `where` (an entity's name) is: quieter the further it is from
    /// the player, silent hearing_range tiles away, and from the side it is on.
    void play_sound_at(const std::string& sound, const std::string& where);
    /// How far sounds carry, in tiles, and how far to one side a sound is fully on that
    /// side's speaker.
    static constexpr double hearing_range = 16.0;
    static constexpr double full_pan = 8.0;
    void play_music(const std::string& music, double fade_seconds);
    void stop_music(double fade_seconds);
    /// 0 to 100.
    void set_music_volume(std::int64_t percent);
    void set_sound_volume(std::int64_t percent);

    /// Keeps every Sound Source of the world playing as it should be heard now: started
    /// when it first plays, louder or quieter and to one side as the player moves, and
    /// stopped when it is turned off or its entity goes. Called once a step. `gain` (0 to 1)
    /// scales them all: they fade with the picture when the scene changes.
    void update_sources(double gain = 1.0, double seconds = 1.0 / 60.0);
    /// Stops every Sound Source (when the scene is replaced).
    void stop_sources();

    /// Forgets sounds whose files changed or went away, so they are read again next time.
    void apply(const assets::AssetChanges& changes);
    /// Sounds that could not be played, oldest first, in plain language.
    [[nodiscard]] std::vector<std::string> take_problems();

private:
    /// The engine key of a sound named by GUID or path, loading it if needed (decoded
    /// whole, or `stream`ed for music, whichever use comes first); empty after recording a
    /// problem.
    std::string prepare(const std::string& sound, bool stream = false);

    audio::AudioEngine* engine_{nullptr};
    const assets::AssetDatabase* assets_{nullptr};
    const world::World* world_{nullptr};
    std::vector<std::string> problems_;
    /// A Sound Source that is playing: the engine's loop, and the sound it repeats (as
    /// the component names it), so a changed sound starts again. A loop of 0 is a sound
    /// that could not be played, reported once.
    struct Source {
        std::uint64_t loop = 0;
        std::string sound;
        double faded = 0.0;  ///< 0 to 1: how far it has faded in.
        double fade = 0.0;   ///< Seconds to fade fully.
        double volume = 0.0; ///< As last heard, before fading, to fade out from.
        double pan = 0.0;
    };
    std::map<Guid, Source> sources_;
    std::vector<Source> fading_out_; ///< Stopped, fading to silence.
};

/// Registers AudioObject as "audio".
void register_audio_types(reflect::Registry& registry);

} // namespace engine::game
