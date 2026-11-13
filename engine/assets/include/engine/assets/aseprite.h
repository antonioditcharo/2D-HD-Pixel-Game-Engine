#pragma once

// Aseprite files (plan/02 B12): ".aseprite" and ".ase", as Aseprite saves them, read without
// Aseprite: every frame put together from its visible layers into one sprite sheet, with
// each frame's duration and the animation tags. Follows Aseprite's published file format.
//
// See docs/dev/sprite-studio.md (Aseprite files).

#include "engine/assets/assets.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace engine::assets {

struct AsepriteTag {
    std::string name;
    std::uint32_t from = 0; ///< First frame, from 0.
    std::uint32_t to = 0;   ///< Last frame, included.
    /// 0 forward, 1 reverse, 2 ping-pong, 3 ping-pong starting backwards.
    std::uint8_t direction = 0;
};

struct AsepriteSheet {
    Image sheet; ///< The frames side by side, left to right.
    std::uint32_t frame_width = 0;
    std::uint32_t frame_height = 0;
    std::vector<std::uint32_t> durations_ms; ///< One per frame.
    std::vector<AsepriteTag> tags;
};

/// Reads an Aseprite file's bytes. Visible layers are drawn bottom to top with their
/// opacity and each cel's (blend modes other than normal are drawn as normal; tilemap
/// layers are passed over). Nothing, with `error` in plain language, for a file that is not
/// one or that is damaged.
[[nodiscard]] std::optional<AsepriteSheet> read_aseprite(std::span<const std::uint8_t> bytes,
                                                         std::string& error);

} // namespace engine::assets
