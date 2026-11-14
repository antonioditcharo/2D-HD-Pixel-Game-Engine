#pragma once

// Writes small Aseprite files for tests: one layer, one raw cel a frame, all of one colour,
// and optionally a tag over every frame.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace test_support {

inline std::vector<std::uint8_t> aseprite(std::uint16_t width, std::uint16_t height,
                                          const std::vector<std::array<std::uint8_t, 4>>& frames,
                                          const std::string& tag = {}) {
    std::vector<std::uint8_t> out;
    const auto u8 = [&out](std::uint32_t v) { out.push_back(static_cast<std::uint8_t>(v)); };
    const auto u16 = [&u8](std::uint32_t v) {
        u8(v & 0xFFU);
        u8((v >> 8U) & 0xFFU);
    };
    const auto u32 = [&u16](std::uint32_t v) {
        u16(v & 0xFFFFU);
        u16(v >> 16U);
    };
    const auto put = [&out](std::size_t at, std::uint32_t v) {
        for (std::size_t i = 0; i < 4; ++i) {
            out[at + i] = static_cast<std::uint8_t>(v >> (8U * i));
        }
    };
    u32(0);
    u16(0xA5E0);
    u16(static_cast<std::uint32_t>(frames.size()));
    u16(width);
    u16(height);
    u16(32);
    u32(1);
    while (out.size() < 128) {
        u8(0);
    }
    for (std::size_t f = 0; f < frames.size(); ++f) {
        const std::size_t frame_start = out.size();
        const std::uint32_t chunks = f == 0 ? (tag.empty() ? 2U : 3U) : 1U;
        u32(0);
        u16(0xF1FA);
        u16(chunks);
        u16(100);
        u16(0);
        u32(chunks);
        const auto chunk_start = [&](std::uint32_t type) {
            const std::size_t start = out.size();
            u32(0);
            u16(type);
            return start;
        };
        if (f == 0) {
            const std::size_t layer = chunk_start(0x2004);
            u16(1); // visible
            for (int i = 0; i < 5; ++i) {
                u16(0);
            }
            u8(255);
            u8(0);
            u8(0);
            u8(0);
            u16(5);
            for (const char c : std::string("layer")) {
                u8(static_cast<std::uint8_t>(c));
            }
            put(layer, static_cast<std::uint32_t>(out.size() - layer));
        }
        const std::size_t cel = chunk_start(0x2005);
        u16(0);
        u16(0);
        u16(0);
        u8(255);
        u16(0); // raw
        u16(0);
        for (int i = 0; i < 5; ++i) {
            u8(0);
        }
        u16(width);
        u16(height);
        for (std::uint32_t i = 0; i < std::uint32_t{width} * height; ++i) {
            for (const std::uint8_t c : frames[f]) {
                u8(c);
            }
        }
        put(cel, static_cast<std::uint32_t>(out.size() - cel));
        if (f == 0 && !tag.empty()) {
            const std::size_t tags = chunk_start(0x2018);
            u16(1);
            for (int i = 0; i < 8; ++i) {
                u8(0);
            }
            u16(0);
            u16(static_cast<std::uint32_t>(frames.size() - 1));
            u8(0);
            for (int i = 0; i < 12; ++i) {
                u8(0);
            }
            u16(static_cast<std::uint32_t>(tag.size()));
            for (const char c : tag) {
                u8(static_cast<std::uint8_t>(c));
            }
            put(tags, static_cast<std::uint32_t>(out.size() - tags));
        }
        put(frame_start, static_cast<std::uint32_t>(out.size() - frame_start));
    }
    put(0, static_cast<std::uint32_t>(out.size()));
    return out;
}

} // namespace test_support
