#include "editor/core/looks.h"

#include <array>

namespace editor {

namespace {

engine::render::Look make(bool tilt, float blur, float bloom, float threshold, float radius,
                          float exposure, float contrast, float saturation, float vignette, float r,
                          float g, float b) {
    engine::render::Look look;
    look.tilt_shift = tilt;
    look.blur = blur;
    look.bloom = bloom;
    look.bloom_threshold = threshold;
    look.bloom_radius = radius;
    look.exposure = exposure;
    look.contrast = contrast;
    look.saturation = saturation;
    look.vignette = vignette;
    look.tint_r = r;
    look.tint_g = g;
    look.tint_b = b;
    return look;
}

} // namespace

std::span<const LookPreset> look_presets() {
    static const std::array<LookPreset, 7> presets{{
        {"Plain", "No processing: the pixels as drawn.", engine::render::Look{}},
        {"HD-2D",
         "A miniature diorama: tilt-shift blur above and below, a soft glow, rich "
         "colour, darker corners.",
         make(true, 3.0F, 0.6F, 0.7F, 6.0F, 1.05F, 1.1F, 1.15F, 0.25F, 1.0F, 1.0F, 1.0F)},
        {"Dreamy", "A gentle haze: strong glow, soft contrast, a faint violet.",
         make(true, 4.0F, 1.0F, 0.6F, 8.0F, 1.1F, 0.95F, 1.05F, 0.2F, 1.03F, 1.0F, 1.05F)},
        {"Moonlit", "Night: dim, cool and blue, with lights glowing.",
         make(false, 3.0F, 0.4F, 0.65F, 6.0F, 0.85F, 1.05F, 0.7F, 0.35F, 0.8F, 0.9F, 1.15F)},
        {"Sunset", "Evening gold: warm light, a glow, deep colour.",
         make(false, 3.0F, 0.7F, 0.7F, 6.0F, 1.05F, 1.1F, 1.2F, 0.3F, 1.12F, 0.92F, 0.8F)},
        {"Crisp retro", "Sharp and bright: punchy contrast and colour, no glow.",
         make(false, 3.0F, 0.0F, 0.75F, 6.0F, 1.0F, 1.2F, 1.25F, 0.1F, 1.0F, 1.0F, 1.0F)},
        {"Noir", "Black and white, hard contrast, dark corners.",
         make(false, 3.0F, 0.0F, 0.75F, 6.0F, 1.0F, 1.3F, 0.0F, 0.45F, 1.0F, 1.0F, 1.0F)},
    }};
    return presets;
}

} // namespace editor
