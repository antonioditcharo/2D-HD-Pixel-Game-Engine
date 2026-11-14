#pragma once

// The "lighting" object logic uses to light a Diorama scene: how bright the ambient light
// and the sun are, and where the sun is, which sets the way shadows fall. Rules and Brains
// set them for dusk, night, or walking into a cave; the project file gives the starting
// values. Lights (scene2d's Light) add to them.
//
// See docs/dev/game.md §3 and docs/dev/scene2d.md §7.

#include "engine/foundation/reflect.h"
#include "engine/scene2d/scene2d.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace engine::game {

struct LightingObject {
    double ambient = 0.55; ///< Light from everywhere, 0 to 2: the darkness of shadows.
    double sun = 0.45;     ///< The sun's (or moon's) strength, 0 to 2.
    /// Where the sun is: degrees clockwise from north (up the map), 0 to 360, and degrees
    /// above the horizon, 5 to 90 (scene2d::sun_direction()).
    double sun_angle = scene2d::default_sun_angle;
    double sun_height = scene2d::default_sun_height;
    /// The light's colour, each channel 0 to 2 (1 changes nothing), in both styles.
    double tint_r = 1.0;
    double tint_g = 1.0;
    double tint_b = 1.0;
    /// The time of day, in hours from 0 to 24.
    double time_of_day = 12.0;
    /// How many real minutes a game day lasts; 0 stops the clock.
    double day_minutes = 0.0;
    /// Whether the light (the sun, its strength, the ambient light and the tint) follows the
    /// time of day; otherwise logic sets them itself.
    bool follows_time = false;
    /// The weather: "clear", "rain", "snow" or "fog", and how strong, 0 to 1.
    std::string weather = "clear";
    double weather_strength = 0.0;

    /// Sets the weather; it eases in over a few seconds. Unknown kinds are clear.
    void set_weather(const std::string& kind, double strength);
    [[nodiscard]] bool raining() const noexcept {
        return weather == "rain" && weather_strength > 0.0;
    }
    /// Sets the time of day and lets the light follow it.
    void set_time(double hours);
    /// Sets the light's colour; logic's own, not following the time.
    void set_tint(double red, double green, double blue);
};

/// The light at `hours` (0 to 24): a dark blue night, a warm low sun at dawn and dusk, a
/// bright high one at noon, blended between. The sun rises in the east (90 degrees) at 6
/// and sets in the west (270) at 18; at night the moon lights from high in the south.
[[nodiscard]] LightingObject daylight(double hours);
/// How bright the Classic 2D style draws the world under `lighting`: its ambient and sun
/// light together, 1 at most (the default 0.55 and 0.45 make 1, as at noon; night's 0.4).
/// The Diorama style lights with them directly instead.
[[nodiscard]] inline double classic_brightness(const LightingObject& lighting) {
    const double sum = lighting.ambient + lighting.sun;
    return std::isfinite(sum) ? std::clamp(sum, 0.05, 1.0) : 1.0;
}
/// One step of `seconds`: the clock runs (if it does) and, following the time, the light.
void advance_day(LightingObject& lighting, double seconds);

/// Registers LightingObject as "lighting".
void register_lighting_types(reflect::Registry& registry);

} // namespace engine::game
