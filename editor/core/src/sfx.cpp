#include "editor/core/sfx.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace editor::sfx {

namespace {

constexpr double tau = 6.283185307179586;

/// xorshift32: a seed's numbers are the same every time.
struct Random {
    std::uint32_t state;
    explicit Random(std::uint32_t seed) : state(seed == 0 ? 0x9E3779B9U : seed) {}
    double next() {
        state ^= state << 13U;
        state ^= state >> 17U;
        state ^= state << 5U;
        return static_cast<double>(state % 1000000U) / 1000000.0;
    }
    double between(double low, double high) { return low + ((high - low) * next()); }
    bool chance(double p) { return next() < p; }
};

} // namespace

std::string_view preset_name(Preset preset) noexcept {
    switch (preset) {
    case Preset::Coin:
        return "Coin";
    case Preset::Jump:
        return "Jump";
    case Preset::Hit:
        return "Hit";
    case Preset::Explosion:
        return "Explosion";
    case Preset::PowerUp:
        return "Power-up";
    case Preset::Laser:
        return "Laser";
    case Preset::Blip:
        return "Blip";
    }
    return "Sound";
}

Params make(Preset preset, std::uint32_t seed) {
    Random r(seed);
    Params p;
    switch (preset) {
    case Preset::Coin:
        p.wave = Wave::Square;
        p.frequency = r.between(800, 1400);
        p.sustain = r.between(0.04, 0.1);
        p.decay = r.between(0.1, 0.25);
        p.arpeggio = r.between(1.3, 1.6);
        p.arpeggio_time = r.between(0.04, 0.08);
        p.duty = r.between(0.3, 0.5);
        break;
    case Preset::Jump:
        p.wave = Wave::Square;
        p.frequency = r.between(250, 450);
        p.slide = r.between(1.0, 2.5);
        p.duty = r.between(0.3, 0.6);
        p.sustain = r.between(0.05, 0.15);
        p.decay = r.between(0.1, 0.25);
        break;
    case Preset::Hit:
        p.wave = r.chance(0.5) ? Wave::Noise : Wave::Saw;
        p.frequency = r.between(300, 800);
        p.slide = r.between(-3.0, -1.5);
        p.sustain = r.between(0.01, 0.04);
        p.decay = r.between(0.1, 0.2);
        break;
    case Preset::Explosion:
        p.wave = Wave::Noise;
        p.frequency = r.between(80, 200);
        p.slide = r.between(-0.6, 0.0);
        p.sustain = r.between(0.1, 0.3);
        p.decay = r.between(0.3, 0.6);
        p.lowpass = r.between(0.3, 0.7);
        p.volume = 0.8;
        break;
    case Preset::PowerUp:
        p.wave = r.chance(0.5) ? Wave::Square : Wave::Saw;
        p.frequency = r.between(300, 500);
        p.slide = r.between(1.0, 2.0);
        p.vibrato_depth = r.between(0.05, 0.15);
        p.vibrato_speed = r.between(8, 15);
        p.sustain = r.between(0.2, 0.3);
        p.decay = r.between(0.2, 0.3);
        break;
    case Preset::Laser: {
        constexpr std::array<Wave, 3> waves{Wave::Square, Wave::Saw, Wave::Sine};
        p.wave = waves.at(static_cast<std::size_t>(r.between(0.0, 2.999)));
        p.frequency = r.between(800, 1500);
        p.slide = r.between(-4.0, -2.0);
        p.duty = r.between(0.2, 0.5);
        p.duty_sweep = r.between(-0.5, 0.5);
        p.sustain = r.between(0.05, 0.15);
        p.decay = r.between(0.05, 0.2);
        break;
    }
    case Preset::Blip:
        p.wave = r.chance(0.7) ? Wave::Square : Wave::Sine;
        p.frequency = r.between(500, 1000);
        p.duty = r.between(0.3, 0.5);
        p.sustain = r.between(0.03, 0.06);
        p.decay = r.between(0.02, 0.06);
        break;
    }
    return clamped(p);
}

Params mutate(const Params& params, std::uint32_t seed) {
    Random r(seed);
    Params p = params;
    const auto nudge = [&r](double& value, double range) { value += r.between(-0.1, 0.1) * range; };
    nudge(p.attack, 0.2);
    nudge(p.sustain, 0.3);
    nudge(p.decay, 0.5);
    p.frequency *= 1.0 + r.between(-0.1, 0.1);
    nudge(p.slide, 2.0);
    nudge(p.vibrato_depth, 0.2);
    nudge(p.vibrato_speed, 10.0);
    nudge(p.duty, 0.5);
    nudge(p.duty_sweep, 1.0);
    nudge(p.lowpass, 0.5);
    return clamped(p);
}

Params clamped(const Params& params) {
    Params p = params;
    const auto fix = [](double& value, double low, double high, double fallback) {
        value = std::isfinite(value) ? std::clamp(value, low, high) : fallback;
    };
    fix(p.attack, 0.0, longest, 0.0);
    fix(p.sustain, 0.0, longest, 0.1);
    fix(p.decay, 0.0, longest, 0.2);
    const double total = p.attack + p.sustain + p.decay;
    if (total > longest) {
        const double share = longest / total;
        p.attack *= share;
        p.sustain *= share;
        p.decay *= share;
    }
    fix(p.frequency, 20.0, 4000.0, 440.0);
    fix(p.slide, -8.0, 8.0, 0.0);
    fix(p.vibrato_depth, 0.0, 0.5, 0.0);
    fix(p.vibrato_speed, 0.0, 40.0, 0.0);
    fix(p.duty, 0.05, 0.95, 0.5);
    fix(p.duty_sweep, -2.0, 2.0, 0.0);
    fix(p.arpeggio, 0.25, 4.0, 1.0);
    fix(p.arpeggio_time, 0.0, longest, 0.0);
    fix(p.lowpass, 0.01, 1.0, 1.0);
    fix(p.volume, 0.0, 1.0, 0.6);
    return p;
}

std::vector<float> synthesize(const Params& params, std::uint32_t sample_rate) {
    const Params p = clamped(params);
    const double rate = static_cast<double>(std::clamp<std::uint32_t>(sample_rate, 8000, 192000));
    const double total = p.attack + p.sustain + p.decay;
    const auto count = static_cast<std::size_t>(std::lround(total * rate));
    std::vector<float> out(count);
    Random noise(12345);
    double phase = 0.0; // 0 to 1 within a wave
    double noise_value = noise.between(-1.0, 1.0);
    double filtered = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double t = static_cast<double>(i) / rate;
        // The pitch: slid, wobbled, and jumped once at the arpeggio's time.
        double frequency = p.frequency * std::pow(2.0, p.slide * t);
        frequency *= 1.0 + (p.vibrato_depth * std::sin(tau * p.vibrato_speed * t));
        if (p.arpeggio_time > 0.0 && t >= p.arpeggio_time) {
            frequency *= p.arpeggio;
        }
        frequency = std::clamp(frequency, 20.0, 8000.0);
        const double before = phase;
        phase += frequency / rate;
        if (phase >= 1.0) {
            phase -= std::floor(phase);
        }
        double value = 0.0;
        switch (p.wave) {
        case Wave::Square: {
            const double duty = std::clamp(p.duty + (p.duty_sweep * t), 0.05, 0.95);
            value = phase < duty ? 1.0 : -1.0;
            break;
        }
        case Wave::Saw:
            value = (2.0 * phase) - 1.0;
            break;
        case Wave::Sine:
            value = std::sin(tau * phase);
            break;
        case Wave::Triangle:
            value = phase < 0.5 ? (4.0 * phase) - 1.0 : 3.0 - (4.0 * phase);
            break;
        case Wave::Noise:
            // A new random level every half wave: the pitch sets how rough it sounds.
            if ((before < 0.5) != (phase < 0.5)) {
                noise_value = noise.between(-1.0, 1.0);
            }
            value = noise_value;
            break;
        }
        // Duller with a lower low-pass: a one-pole filter.
        filtered += (value - filtered) * p.lowpass;
        // The envelope: up, held, down.
        double envelope = 1.0;
        if (t < p.attack) {
            envelope = t / p.attack;
        } else if (t >= p.attack + p.sustain) {
            envelope = p.decay > 0.0 ? 1.0 - ((t - p.attack - p.sustain) / p.decay) : 0.0;
        }
        out[i] = static_cast<float>(
            std::clamp(filtered * std::clamp(envelope, 0.0, 1.0) * p.volume, -1.0, 1.0));
    }
    return out;
}

std::vector<std::uint8_t> to_wav(const std::vector<float>& samples, std::uint32_t sample_rate) {
    const auto data_size = static_cast<std::uint32_t>(samples.size() * 2);
    std::vector<std::uint8_t> out;
    out.reserve(44 + data_size);
    const auto text = [&out](const char* four) { out.insert(out.end(), four, four + 4); };
    const auto u32 = [&out](std::uint32_t v) {
        for (unsigned i = 0; i < 4; ++i) {
            out.push_back(static_cast<std::uint8_t>((v >> (8U * i)) & 0xFFU));
        }
    };
    const auto u16 = [&out](std::uint16_t v) {
        out.push_back(static_cast<std::uint8_t>(v & 0xFFU));
        out.push_back(static_cast<std::uint8_t>((v >> 8U) & 0xFFU));
    };
    text("RIFF");
    u32(36 + data_size);
    text("WAVE");
    text("fmt ");
    u32(16);
    u16(1); // PCM
    u16(1); // mono
    u32(sample_rate);
    u32(sample_rate * 2);
    u16(2);
    u16(16);
    text("data");
    u32(data_size);
    for (const float sample : samples) {
        const auto value = static_cast<std::int16_t>(
            std::lround(std::clamp(static_cast<double>(sample), -1.0, 1.0) * 32767.0));
        u16(static_cast<std::uint16_t>(value));
    }
    return out;
}

} // namespace editor::sfx
