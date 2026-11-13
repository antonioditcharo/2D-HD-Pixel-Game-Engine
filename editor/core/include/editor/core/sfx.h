#pragma once

// The sound effect generator's model (plan/02 J2): retro sound effects made from a few
// numbers, as sfxr and its descendants make them: a wave (square, saw, sine, triangle or
// noise) whose pitch starts somewhere and slides, wobbles or jumps, shaped by a volume
// envelope. Presets (coin, jump, hit...) give a sound of that kind, a little different for
// each seed, so "another one" is a click away. No user interface: tested on its own.
//
// See docs/dev/audio.md.

#include <cstdint>
#include <string_view>
#include <vector>

namespace editor::sfx {

enum class Wave : std::uint8_t { Square, Saw, Sine, Triangle, Noise };

enum class Preset : std::uint8_t { Coin, Jump, Hit, Explosion, PowerUp, Laser, Blip };

/// Every preset, in the order the generator shows them.
inline constexpr Preset presets[] = {Preset::Coin,    Preset::Jump,  Preset::Hit, Preset::Explosion,
                                     Preset::PowerUp, Preset::Laser, Preset::Blip};

/// A preset's name, such as "Coin" or "Power-up".
[[nodiscard]] std::string_view preset_name(Preset preset) noexcept;

struct Params {
    Wave wave = Wave::Square;
    double attack = 0.0;        ///< Seconds rising to full volume.
    double sustain = 0.1;       ///< Seconds at full volume.
    double decay = 0.2;         ///< Seconds falling to silence.
    double frequency = 440;     ///< Hz at the start, 20 to 4,000.
    double slide = 0.0;         ///< Octaves a second the pitch moves, up or down.
    double vibrato_depth = 0.0; ///< Of the pitch, 0 to 0.5.
    double vibrato_speed = 0.0; ///< Wobbles a second.
    double duty = 0.5;          ///< Square: how much of each wave is high, 0.05 to 0.95.
    double duty_sweep = 0.0;    ///< Change of duty a second.
    double arpeggio = 1.0;      ///< Pitch times this after `arpeggio_time` (1: no jump).
    double arpeggio_time = 0.0; ///< Seconds.
    double lowpass = 1.0;       ///< 1 lets every pitch through; lower is duller.
    double volume = 0.6;        ///< 0 to 1.
};

/// The longest sound made, in seconds.
inline constexpr double longest = 5.0;

/// A sound of the preset's kind; each seed gives a different one of that kind.
[[nodiscard]] Params make(Preset preset, std::uint32_t seed);
/// The same sound, a little changed: every number nudged by up to a tenth.
[[nodiscard]] Params mutate(const Params& params, std::uint32_t seed);
/// Every number kept within its range (and the length within `longest`).
[[nodiscard]] Params clamped(const Params& params);
/// The sound: mono samples from -1 to 1 at `sample_rate`. The same numbers always give the
/// same samples.
[[nodiscard]] std::vector<float> synthesize(const Params& params,
                                            std::uint32_t sample_rate = 44100);
/// Mono 16-bit PCM WAV file bytes of the samples.
[[nodiscard]] std::vector<std::uint8_t> to_wav(const std::vector<float>& samples,
                                               std::uint32_t sample_rate = 44100);

} // namespace editor::sfx
