#pragma once

// The game's camera (plan/02 D10): what it follows, and how. By default it sits on the
// player, as it always did; a creator softens it with smoothing, lets the player move a
// little without it (a dead zone), has it look ahead the way the player walks, sends it to
// follow someone else or look at a place, and shakes it, as the camera rigs of other
// engines do.
//
// Logic reaches it as the "camera" object. See docs/dev/game.md.

#include "engine/foundation/reflect.h"
#include "engine/world/world.h"

#include <array>
#include <cstdint>
#include <string>

namespace engine::game {

struct CameraObject {
    /// About how long the camera takes to catch up, in seconds; 0 keeps it exactly on what
    /// it follows.
    double smoothing = 0.0;
    /// A box, in tiles across and down, around the middle of the view in which what it
    /// follows moves without the camera moving; 0 for none.
    double dead_zone = 0.0;
    /// How far, in tiles, the camera looks ahead of what it follows, the way it walks.
    double look_ahead = 0.0;
    /// The name of the entity it follows; empty for the player.
    std::string follow;

    /// Follows the entity named `name` (empty: the player).
    void follow_entity(const std::string& name);
    /// Follows the player again.
    void follow_player();
    /// Stops following, and looks at (x, y), in tiles.
    void look_at(double x, double y);
    /// Shakes the view by up to `strength` tiles, fading out over `seconds`.
    void shake(double strength, double seconds);

    // The camera's state, not set by creators.
    bool placed = false; ///< False until it first knows where to be: it then jumps there.
    double x = 0.0;      ///< Where it looks, in tiles, before shaking.
    double y = 0.0;
    double ahead_x = 0.0; ///< How far it looks ahead now (it turns smoothly).
    double ahead_y = 0.0;
    bool fixed = false; ///< Looking at a place rather than following.
    double fixed_x = 0.0;
    double fixed_y = 0.0;
    double shake_strength = 0.0;
    double shake_left = 0.0; ///< Seconds of shaking left.
    double shake_total = 0.0;
};

/// Registers CameraObject as "camera".
void register_camera_types(reflect::Registry& registry);

/// Where the camera wants to be now: the place it looks at, else the entity it follows (or
/// the player), with its look-ahead. Nothing if there is nothing to follow.
[[nodiscard]] std::optional<std::array<double, 2>> camera_target(const CameraObject& camera,
                                                                 const world::World& world);

/// One fixed step of `seconds`: the camera moves towards its target, through its dead zone
/// and smoothing (or jumps there the first time, and after a scene changes), and its shake
/// runs down.
void update_camera(CameraObject& camera, const world::World& world, double seconds);

/// Where the camera looks in step `step` of the game: where it is (or its target, if it has
/// not been placed yet), shaken. The same step always shakes the same way.
[[nodiscard]] std::array<double, 2> camera_view(const CameraObject& camera,
                                                const world::World& world, std::uint64_t step);

} // namespace engine::game
