#pragma once

// Look presets (plan/02 D7, plan/06 §3.10): named looks a creator picks from a gallery
// rather than setting a dozen numbers: the HD-2D look's tilt-shift and soft glow, a dreamy
// haze, moonlight, sunset, crisp retro colour, noir. Hovering one previews it; choosing
// one is one undo step. Their numbers are original to Sparxel.
//
// See docs/dev/editor.md.

#include "engine/render/render.h"

#include <span>
#include <string_view>

namespace editor {

struct LookPreset {
    std::string_view name;
    std::string_view description;
    engine::render::Look look;
};

/// Every preset, in the order the gallery shows them; the first changes nothing.
[[nodiscard]] std::span<const LookPreset> look_presets();

} // namespace editor
