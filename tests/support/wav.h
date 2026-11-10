#pragma once

// Writes small WAV files for audio tests, so expected levels are exact: a 16-bit sample of
// 16384 is exactly 0.5.

#include <cstdint>
#include <vector>

namespace test_support {

/// A stereo 16-bit WAV file of `frames` frames at `rate`, every sample `value`.
inline std::vector<std::uint8_t> wav(std::uint32_t frames, std::int16_t value,
                                     std::uint32_t rate = 48000) {
    const std::uint32_t data_size = frames * 2 * 2;
    std::vector<std::uint8_t> out;
    const auto text = [&out](const char* four) { out.insert(out.end(), four, four + 4); };
    const auto u32 = [&out](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFU));
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
    u16(2); // channels
    u32(rate);
    u32(rate * 4); // bytes per second
    u16(4);        // bytes per frame
    u16(16);       // bits per sample
    text("data");
    u32(data_size);
    for (std::uint32_t i = 0; i < frames * 2; ++i) {
        u16(static_cast<std::uint16_t>(value));
    }
    return out;
}

} // namespace test_support
