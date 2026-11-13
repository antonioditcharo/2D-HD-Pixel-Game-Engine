#include "engine/game/camera.h"

#include "engine/game/behaviours.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace engine::game {

namespace {

/// How fast the look-ahead turns to a new way, in tiles per second.
constexpr double ahead_turn_speed = 6.0;

double finite_or(double value, double fallback) {
    return std::isfinite(value) ? value : fallback;
}

/// A number from -1 to 1 for a step and a channel: the same every run.
double wobble(std::uint64_t step, std::uint64_t channel) {
    std::uint64_t h = (step * 0x9E3779B97F4A7C15ULL) ^ (channel * 0xC2B2AE3D27D4EB4FULL);
    h ^= h >> 31U;
    h *= 0xBF58476D1CE4E5B9ULL;
    h ^= h >> 29U;
    return (static_cast<double>(h % 20001ULL) / 10000.0) - 1.0;
}

/// The entity the camera follows, and where it is; nothing if there is none.
std::optional<std::array<double, 2>> followed(const CameraObject& camera, const world::World& world,
                                              world::Entity* entity = nullptr) {
    if (camera.follow.empty()) {
        return player_position(world);
    }
    const std::vector<world::Entity> named = world.find_named(camera.follow);
    if (named.empty()) {
        return player_position(world);
    }
    const scene2d::Transform2D* at = world.get<scene2d::Transform2D>(named.front());
    if (at == nullptr) {
        return std::nullopt;
    }
    if (entity != nullptr) {
        *entity = named.front();
    }
    return std::array<double, 2>{at->x, at->y};
}

} // namespace

void CameraObject::follow_entity(const std::string& name) {
    follow = name;
    fixed = false;
}

void CameraObject::follow_player() {
    follow.clear();
    fixed = false;
}

void CameraObject::look_at(double to_x, double to_y) {
    if (std::isfinite(to_x) && std::isfinite(to_y)) {
        fixed = true;
        fixed_x = to_x;
        fixed_y = to_y;
    }
}

void CameraObject::shake(double strength, double seconds) {
    if (!std::isfinite(strength) || !std::isfinite(seconds) || strength <= 0.0 || seconds <= 0.0) {
        return;
    }
    shake_strength = std::min(strength, 4.0);
    shake_total = std::min(seconds, 10.0);
    shake_left = shake_total;
}

void register_camera_types(reflect::Registry& registry) {
    auto camera = registry.add_type<CameraObject>("camera")
                      .display_name("Camera")
                      .category("Visuals")
                      .tooltip("What the game's view follows, and how.");
    camera.property<&CameraObject::smoothing>("smoothing")
        .range(0.0, 2.0)
        .step(0.05)
        .unit(reflect::Unit::Seconds)
        .display_name("Smoothing")
        .tooltip("About how long the camera takes to catch up: 0 stays exactly on the player; "
                 "0.15 glides.");
    camera.property<&CameraObject::dead_zone>("dead_zone")
        .range(0.0, 16.0)
        .step(0.25)
        .unit(reflect::Unit::Tiles)
        .display_name("Dead zone")
        .tooltip("How far the player can move in the middle of the view before the camera "
                 "moves.");
    camera.property<&CameraObject::look_ahead>("look_ahead")
        .range(0.0, 8.0)
        .step(0.25)
        .unit(reflect::Unit::Tiles)
        .display_name("Look ahead")
        .tooltip("How far the camera looks ahead of the player, the way they walk.");
    camera.property<&CameraObject::follow>("follow").display_name("Follows").tooltip(
        "The name of who the camera follows; empty for the player.");
    camera.action<&CameraObject::follow_entity>("follow_entity", {"name"})
        .text("Camera follows {name}")
        .tooltip("The view follows someone else, such as a character in a cutscene.");
    camera.action<&CameraObject::follow_player>("follow_player")
        .text("Camera follows the player")
        .tooltip("The view follows the player again.");
    camera.action<&CameraObject::look_at>("look_at", {"x", "y"})
        .text("Camera looks at {x}, {y}")
        .tooltip("The view goes to a place, in tiles, and stays there.");
    camera.action<&CameraObject::shake>("shake", {"strength", "seconds"})
        .text("Shake the camera by {strength} for {seconds} seconds")
        .tooltip("The view shakes, fading out: an explosion, a quake, a heavy landing.")
        .param("strength")
        .range(0.0, 4.0)
        .param("seconds")
        .range(0.0, 10.0);
}

std::optional<std::array<double, 2>> camera_target(const CameraObject& camera,
                                                   const world::World& world) {
    if (camera.fixed) {
        return std::array<double, 2>{camera.fixed_x, camera.fixed_y};
    }
    std::optional<std::array<double, 2>> at = followed(camera, world);
    if (at) {
        (*at)[0] += finite_or(camera.ahead_x, 0.0);
        (*at)[1] += finite_or(camera.ahead_y, 0.0);
    }
    return at;
}

void update_camera(CameraObject& camera, const world::World& world, double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) {
        seconds = 0.0;
    }
    camera.shake_left = std::max(0.0, camera.shake_left - seconds);
    // Look ahead the way what it follows faces while it walks.
    world::Entity entity;
    double want_x = 0.0;
    double want_y = 0.0;
    const double ahead = std::clamp(finite_or(camera.look_ahead, 0.0), 0.0, 8.0);
    if (!camera.fixed && ahead > 0.0 && followed(camera, world, &entity)) {
        if (!entity.is_valid()) {
            // The player: the first mover that is not walking by itself.
            if (const reflect::TypeInfo* type = world.registry().find_type<TopDownMover>()) {
                world.each(*type, [&](world::Entity e, reflect::ObjectRef) {
                    const Walker* walker = world.get<Walker>(e);
                    if (!entity.is_valid() && (walker == nullptr || !walker->enabled)) {
                        entity = e;
                    }
                });
            }
        }
        if (const TopDownMover* mover =
                entity.is_valid() ? world.get<TopDownMover>(entity) : nullptr;
            mover != nullptr && mover->moving) {
            switch (mover->facing) {
            case Facing::Left:
                want_x = -ahead;
                break;
            case Facing::Right:
                want_x = ahead;
                break;
            case Facing::Up:
                want_y = -ahead;
                break;
            case Facing::Down:
                want_y = ahead;
                break;
            }
        }
    }
    const double turn = ahead_turn_speed * seconds;
    const auto approach = [turn](double now, double want) {
        return now + std::clamp(want - now, -turn, turn);
    };
    if (!camera.placed) {
        camera.ahead_x = want_x;
        camera.ahead_y = want_y;
    } else {
        camera.ahead_x = approach(finite_or(camera.ahead_x, 0.0), want_x);
        camera.ahead_y = approach(finite_or(camera.ahead_y, 0.0), want_y);
    }
    const std::optional<std::array<double, 2>> target = camera_target(camera, world);
    if (!target) {
        return;
    }
    if (!camera.placed || !std::isfinite(camera.x) || !std::isfinite(camera.y)) {
        camera.x = (*target)[0];
        camera.y = (*target)[1];
        camera.placed = true;
        return;
    }
    // The dead zone: the camera moves only as far as keeps the target inside it.
    const double half = std::clamp(finite_or(camera.dead_zone, 0.0), 0.0, 16.0) / 2.0;
    double goal_x = camera.x;
    double goal_y = camera.y;
    if ((*target)[0] > camera.x + half) {
        goal_x = (*target)[0] - half;
    } else if ((*target)[0] < camera.x - half) {
        goal_x = (*target)[0] + half;
    }
    if ((*target)[1] > camera.y + half) {
        goal_y = (*target)[1] - half;
    } else if ((*target)[1] < camera.y - half) {
        goal_y = (*target)[1] + half;
    }
    // Smoothing: each step closes a share of the gap, the same at any frame rate.
    const double smoothing = std::clamp(finite_or(camera.smoothing, 0.0), 0.0, 2.0);
    const double share = smoothing > 0.0 ? 1.0 - std::exp(-seconds / smoothing) : 1.0;
    camera.x += (goal_x - camera.x) * share;
    camera.y += (goal_y - camera.y) * share;
}

std::array<double, 2> camera_view(const CameraObject& camera, const world::World& world,
                                  std::uint64_t step) {
    std::array<double, 2> at{camera.x, camera.y};
    if (!camera.placed) {
        at = camera_target(camera, world).value_or(std::array<double, 2>{0.0, 0.0});
    }
    if (camera.shake_left > 0.0 && camera.shake_total > 0.0) {
        const double strength = camera.shake_strength * (camera.shake_left / camera.shake_total);
        at[0] += strength * wobble(step, 1);
        at[1] += strength * wobble(step, 2);
    }
    return at;
}

} // namespace engine::game
