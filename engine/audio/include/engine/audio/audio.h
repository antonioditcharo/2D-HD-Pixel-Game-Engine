#pragma once

// Audio (ADR-009): sound effects and music on miniaudio, mixed into two buses, Music and
// Sounds, under a master volume. Sounds come from encoded bytes in memory (WAV, Ogg Vorbis,
// FLAC or MP3), so the asset database stays the only thing that reads files. miniaudio types
// never appear here.
//
// See docs/dev/audio.md.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine::audio {

struct AudioOptions {
    /// No sound card: the mix is read with AudioEngine::render(), for tests and tools.
    bool headless = false;
    std::uint32_t sample_rate = 48000;
    std::uint32_t channels = 2;
};

enum class Bus : std::uint8_t { Music, Sounds };

class AudioEngine {
public:
    /// Null, with `error`, if no audio device can be opened (or, headless, if miniaudio
    /// cannot start).
    [[nodiscard]] static std::unique_ptr<AudioEngine> create(const AudioOptions& options,
                                                             std::string& error);

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;
    AudioEngine(AudioEngine&&) = delete;
    AudioEngine& operator=(AudioEngine&&) = delete;
    ~AudioEngine();

    /// Decodes a sound's file bytes (WAV, Ogg Vorbis, FLAC or MP3) and keeps it under `key`,
    /// such as its asset GUID. A key already loaded is replaced (for hot reload); sounds
    /// playing from the old data stop. False, with last_error(), if the bytes cannot be decoded.
    bool load(std::string_view key, std::vector<std::uint8_t> bytes);
    /// Keeps a sound's file bytes under `key` to be decoded as it plays, for long music:
    /// only the file is held in memory, not the decoded sound. Each voice playing it
    /// decodes its own way through, looping by seeking back. False, with last_error(), if
    /// the bytes are not a sound. A key already loaded either way is replaced.
    bool load_stream(std::string_view key, std::vector<std::uint8_t> bytes);
    [[nodiscard]] bool is_loaded(std::string_view key) const;
    /// Forgets a sound, stopping it wherever it plays.
    void unload(std::string_view key);

    /// Plays a loaded sound once on the Sounds bus. Volume 1 is as recorded; pitch 1 is the
    /// recorded speed (2 is an octave up and twice as fast); pan -1 is the left speaker
    /// only, 0 both, 1 the right only.
    bool play_sound(std::string_view key, double volume = 1.0, double pitch = 1.0,
                    double pan = 0.0);
    /// Plays a loaded sound as music on the Music bus, looping. The music playing before
    /// fades out over `fade_seconds` while this one fades in (0 cuts). Playing the music
    /// already playing does nothing.
    bool play_music(std::string_view key, double fade_seconds = 0.0);
    /// Fades the music out over `fade_seconds` (0 stops at once).
    void stop_music(double fade_seconds = 0.0);
    /// The key of the music playing, or empty.
    [[nodiscard]] const std::string& music() const noexcept;

    /// Volumes from 0 (silent) to 1 (full); values outside are clamped.
    void set_volume(Bus bus, double volume);
    [[nodiscard]] double volume(Bus bus) const noexcept;
    /// The player's own volume for a bus (from the game's Options), 0 to 1: it scales the
    /// game's, so logic changing the music volume does not undo the player's choice.
    void set_player_volume(Bus bus, double volume);
    [[nodiscard]] double player_volume(Bus bus) const noexcept;
    /// Ducking: the music lowered to `level` (0 to 1) of itself on top of the other
    /// volumes, as while a character speaks; 1 for none.
    void set_music_duck(double level);
    [[nodiscard]] double music_duck() const noexcept;
    void set_master_volume(double volume);
    [[nodiscard]] double master_volume() const noexcept;

    /// Starts a loaded sound on repeat on the Sounds bus (a waterfall, a fire), at
    /// `volume` and `pan` as play_sound() takes them. Returns what names it to set_loop()
    /// and stop_loop(), or 0 if it could not start.
    [[nodiscard]] std::uint64_t play_loop(std::string_view key, double volume = 1.0,
                                          double pan = 0.0);
    /// Changes a looping sound's volume and pan, as its listener moves. Nothing for an
    /// unknown loop.
    void set_loop(std::uint64_t loop, double volume, double pan);
    /// Stops a looping sound. Nothing for an unknown loop.
    void stop_loop(std::uint64_t loop);
    /// Looping sounds playing.
    [[nodiscard]] std::size_t playing_loops() const;

    /// One-shot sounds still playing.
    [[nodiscard]] std::size_t playing_sounds() const;

    /// Headless only: mixes the next frames into `out`, interleaved, channels() floats per
    /// frame. False on a device-backed engine, which mixes by itself.
    bool render(std::span<float> out);

    [[nodiscard]] std::uint32_t sample_rate() const noexcept;
    [[nodiscard]] std::uint32_t channels() const noexcept;
    [[nodiscard]] const std::string& last_error() const noexcept;

    struct Impl;

private:
    explicit AudioEngine(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace engine::audio
