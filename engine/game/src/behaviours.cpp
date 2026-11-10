#include "engine/game/behaviours.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <tuple>
#include <utility>

namespace engine::game {

// ---------------------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------------------

void register_behaviours(reflect::Registry& registry) {
    registry.add_choice_list<Facing>("facing")
        .display_name("Facing")
        .entry(Facing::Down, "down")
        .entry(Facing::Left, "left")
        .entry(Facing::Right, "right")
        .entry(Facing::Up, "up");
    auto mover = registry.add_type<TopDownMover>("top_down_mover")
                     .display_name("Top-down Mover")
                     .category("Movement")
                     .tooltip("Walks in eight directions with the move actions.");
    mover.property<&TopDownMover::speed>("speed").range(0.0, 64.0).step(0.5).tooltip(
        "How fast it walks, in tiles per second.");
    mover.property<&TopDownMover::enabled>("enabled").tooltip(
        "Turn off to stop the player moving, for example in a cutscene.");
    mover.property<&TopDownMover::hitbox_width>("hitbox_width")
        .range(0.0, 16.0)
        .step(0.125)
        .unit(reflect::Unit::Tiles)
        .flags(reflect::property_flag::advanced)
        .display_name("Hitbox width")
        .tooltip("How wide the feet are, for bumping into walls.");
    mover.property<&TopDownMover::hitbox_height>("hitbox_height")
        .range(0.0, 16.0)
        .step(0.125)
        .unit(reflect::Unit::Tiles)
        .flags(reflect::property_flag::advanced)
        .display_name("Hitbox height")
        .tooltip("How deep the feet are, for bumping into walls.");
    mover.property<&TopDownMover::facing>("facing").tooltip("The direction it looks.");
    mover.property<&TopDownMover::moving>("moving")
        .flags(reflect::property_flag::read_only)
        .tooltip("Whether it is walking.");

    auto walk = registry.add_type<WalkAnimation>("walk_animation")
                    .display_name("Walk Animation")
                    .category("Movement")
                    .tooltip("Shows a character sheet's walk cycle in the direction it faces.");
    walk.property<&WalkAnimation::frames_per_direction>("frames_per_direction")
        .range(1, 64)
        .display_name("Frames per direction")
        .tooltip("How many frames each row of the character sheet has.");
    walk.property<&WalkAnimation::frames_per_second>("frames_per_second")
        .range(0.0, 60.0)
        .step(1.0)
        .display_name("Speed")
        .tooltip("Frames per second while walking.");
    walk.property<&WalkAnimation::standing_frame>("standing_frame")
        .range(0, 63)
        .display_name("Standing frame")
        .tooltip("The frame of the row shown when standing still, counting from 0.");
    walk.property<&WalkAnimation::ping_pong>("ping_pong")
        .flags(reflect::property_flag::advanced)
        .display_name("Back and forth")
        .tooltip("Step through the frames and back (1, 2, 3, 2) instead of starting over.");
    walk.property<&WalkAnimation::elapsed>("elapsed")
        .unit(reflect::Unit::Seconds)
        .flags(reflect::property_flag::hidden)
        .tooltip("How long it has been walking.");

    auto body = registry.add_type<Body>("body")
                    .display_name("Solid Body")
                    .category("Movement")
                    .tooltip("Blocks walking characters, like a wall that can stand anywhere.");
    body.property<&Body::width>("width")
        .range(0.0, 64.0)
        .step(0.125)
        .unit(reflect::Unit::Tiles)
        .tooltip("How wide the blocking area is, centred on the position.");
    body.property<&Body::height>("height")
        .range(0.0, 64.0)
        .step(0.125)
        .unit(reflect::Unit::Tiles)
        .tooltip("How deep the blocking area is, upwards from the position.");
    body.property<&Body::enabled>("enabled").tooltip("Turn off to let characters walk through.");

    auto trigger = registry.add_type<Trigger>("trigger")
                       .display_name("Trigger Area")
                       .category("Events")
                       .tooltip("Tells Rules when a character walks in or out.");
    trigger.property<&Trigger::width>("width")
        .range(0.0, 256.0)
        .step(0.125)
        .unit(reflect::Unit::Tiles)
        .tooltip("How wide the area is, centred on the position.");
    trigger.property<&Trigger::height>("height")
        .range(0.0, 256.0)
        .step(0.125)
        .unit(reflect::Unit::Tiles)
        .tooltip("How deep the area is, upwards from the position.");
    trigger.property<&Trigger::enabled>("enabled").tooltip(
        "Turn off to ignore characters; anyone inside leaves.");
    trigger.event<std::string>("entered", {"who"})
        .text("When {who} walks in")
        .tooltip("Happens when a walking character enters the area.");
    trigger.event<std::string>("exited", {"who"})
        .text("When {who} walks out")
        .tooltip("Happens when a walking character leaves the area.");

    auto interactable = registry.add_type<Interactable>("interactable")
                            .display_name("Interactable")
                            .category("Events")
                            .tooltip("Something the player can talk to or use with Interact.");
    interactable.property<&Interactable::width>("width")
        .range(0.0, 64.0)
        .step(0.125)
        .unit(reflect::Unit::Tiles)
        .tooltip("How wide the area that can be reached is, centred on the position.");
    interactable.property<&Interactable::height>("height")
        .range(0.0, 64.0)
        .step(0.125)
        .unit(reflect::Unit::Tiles)
        .tooltip("How deep the area that can be reached is, upwards from the position.");
    interactable.property<&Interactable::enabled>("enabled").tooltip(
        "Turn off to stop the player interacting with it.");
    interactable.event<std::string>("interacted", {"who"})
        .text("When {who} interacts with it")
        .tooltip("Happens when the player presses Interact while facing it, close by.");
}

bool register_behaviour_components(world::World& world) {
    const bool mover = world.register_component<TopDownMover>();
    const bool walk = world.register_component<WalkAnimation>();
    const bool body = world.register_component<Body>();
    const bool trigger = world.register_component<Trigger>();
    const bool interactable = world.register_component<Interactable>();
    return mover && walk && body && trigger && interactable;
}

// ---------------------------------------------------------------------------------------
// Movement
// ---------------------------------------------------------------------------------------

Facing facing_for(Facing current, double dx, double dy) noexcept {
    const bool left = dx < 0.0;
    const bool right = dx > 0.0;
    const bool up = dy < 0.0;
    const bool down = dy > 0.0;
    if (!left && !right && !up && !down) {
        return current;
    }
    const bool keeps = (current == Facing::Left && left) || (current == Facing::Right && right) ||
                       (current == Facing::Up && up) || (current == Facing::Down && down);
    if (keeps) {
        return current;
    }
    if (left || right) {
        return left ? Facing::Left : Facing::Right;
    }
    return up ? Facing::Up : Facing::Down;
}

void update_walk(const TopDownMover& mover, WalkAnimation& walk, scene2d::Sprite& sprite,
                 double seconds) {
    const std::uint32_t count = std::max<std::uint32_t>(walk.frames_per_direction, 1);
    const auto row = static_cast<std::uint32_t>(mover.facing);
    std::uint32_t column = std::min(walk.standing_frame, count - 1);
    if (mover.moving) {
        if (std::isfinite(seconds) && seconds > 0.0) {
            walk.elapsed += seconds;
        }
        const double fps =
            std::isfinite(walk.frames_per_second) ? std::max(walk.frames_per_second, 0.0) : 0.0;
        // Ping-pong over n frames has a period of 2n - 2: 0, 1, ..., n-1, n-2, ..., 1.
        const std::uint32_t period = walk.ping_pong && count > 2 ? (2 * count) - 2 : count;
        double step = std::floor((walk.elapsed * fps) + 1e-9);
        step =
            std::isfinite(step) && step >= 0.0 ? std::fmod(step, static_cast<double>(period)) : 0.0;
        const auto index = static_cast<std::uint32_t>(step);
        column = index < count ? index : period - index;
    } else {
        walk.elapsed = 0.0;
    }
    sprite.frame = (row * count) + column;
}

scene2d::Box feet_box(double x, double y, double width, double height) noexcept {
    const double half = std::max(width, 0.0) / 2.0;
    return scene2d::Box{x - half, y - std::max(height, 0.0), x + half, y};
}

scene2d::Box hitbox(const TopDownMover& mover, double x, double y) noexcept {
    return feet_box(x, y, mover.hitbox_width, mover.hitbox_height);
}

void move_characters(world::World& world, const input::InputState& input, double seconds,
                     std::uint32_t ppu) {
    const reflect::TypeInfo* mover_type = world.registry().find_type<TopDownMover>();
    if (mover_type == nullptr) {
        return;
    }
    // Bodies where they stand at the start of the step.
    struct Obstacle {
        world::Entity entity;
        scene2d::Box box;
    };
    std::vector<Obstacle> obstacles;
    if (const reflect::TypeInfo* body_type = world.registry().find_type<Body>()) {
        world.each(*body_type, [&](world::Entity entity, reflect::ObjectRef ref) {
            const Body* body = ref.as<Body>();
            const scene2d::Transform2D* at = world.get<scene2d::Transform2D>(entity);
            if (body != nullptr && at != nullptr && body->enabled) {
                obstacles.push_back(
                    Obstacle{entity, feet_box(at->x, at->y, body->width, body->height)});
            }
        });
    }
    const double dx =
        (input.held("move_right") ? 1.0 : 0.0) - (input.held("move_left") ? 1.0 : 0.0);
    const double dy = (input.held("move_down") ? 1.0 : 0.0) - (input.held("move_up") ? 1.0 : 0.0);
    const double length = std::sqrt((dx * dx) + (dy * dy));

    world.each(*mover_type, [&](world::Entity entity, reflect::ObjectRef ref) {
        TopDownMover* mover = ref.as<TopDownMover>();
        scene2d::Transform2D* transform = world.get<scene2d::Transform2D>(entity);
        if (mover == nullptr || transform == nullptr) {
            return;
        }
        WalkAnimation* walk = world.get<WalkAnimation>(entity);
        scene2d::Sprite* sprite = world.get<scene2d::Sprite>(entity);
        mover->moving = mover->enabled && length > 0.0;
        if (mover->moving) {
            mover->facing = facing_for(mover->facing, dx, dy);
            std::vector<scene2d::Box> others;
            others.reserve(obstacles.size());
            for (const Obstacle& obstacle : obstacles) {
                if (obstacle.entity != entity) {
                    others.push_back(obstacle.box);
                }
            }
            const double step = mover->speed * seconds / length;
            // One axis at a time, so a diagonal into a wall slides along it.
            for (const scene2d::Axis axis : {scene2d::Axis::X, scene2d::Axis::Y}) {
                const bool x_axis = axis == scene2d::Axis::X;
                const scene2d::Box box = hitbox(*mover, transform->x, transform->y);
                const double wanted = (x_axis ? dx : dy) * step;
                const double allowed = scene2d::sweep_boxes(
                    box, axis, scene2d::sweep(world, box, axis, wanted, ppu), others);
                (x_axis ? transform->x : transform->y) += allowed;
            }
        }
        if (walk != nullptr && sprite != nullptr) {
            update_walk(*mover, *walk, *sprite, seconds);
        }
    });
}

// ---------------------------------------------------------------------------------------
// Triggers and interaction
// ---------------------------------------------------------------------------------------

namespace {

struct Placed {
    world::Entity entity;
    Guid guid;
    scene2d::Box box;
};

/// Every Top-down Mover's hitbox, sorted by GUID.
std::vector<Placed> movers(const world::World& world) {
    std::vector<Placed> found;
    if (const reflect::TypeInfo* type = world.registry().find_type<TopDownMover>()) {
        world.each(*type, [&](world::Entity entity, reflect::ObjectRef ref) {
            const TopDownMover* mover = ref.as<TopDownMover>();
            const scene2d::Transform2D* at = world.get<scene2d::Transform2D>(entity);
            if (mover != nullptr && at != nullptr) {
                found.push_back(Placed{entity, world.guid(entity), hitbox(*mover, at->x, at->y)});
            }
        });
    }
    std::sort(found.begin(), found.end(),
              [](const Placed& a, const Placed& b) { return a.guid < b.guid; });
    return found;
}

} // namespace

std::vector<TriggerEvent> update_triggers(world::World& world) {
    std::vector<TriggerEvent> events;
    const reflect::TypeInfo* type = world.registry().find_type<Trigger>();
    if (type == nullptr) {
        return events;
    }
    const std::vector<Placed> walkers = movers(world);
    std::vector<std::pair<Guid, TriggerEvent>> keyed_exits;
    std::vector<std::pair<Guid, TriggerEvent>> keyed_entries;
    world.each(*type, [&](world::Entity entity, reflect::ObjectRef ref) {
        Trigger* trigger = ref.as<Trigger>();
        const scene2d::Transform2D* at = world.get<scene2d::Transform2D>(entity);
        if (trigger == nullptr) {
            return;
        }
        std::vector<Guid> now;
        if (at != nullptr && trigger->enabled) {
            const scene2d::Box area = feet_box(at->x, at->y, trigger->width, trigger->height);
            for (const Placed& walker : walkers) {
                if (walker.entity != entity && scene2d::overlaps(area, walker.box)) {
                    now.push_back(walker.guid); // sorted, as walkers are
                }
            }
        }
        const Guid trigger_guid = world.guid(entity);
        for (const Guid& was : trigger->inside) {
            if (!std::binary_search(now.begin(), now.end(), was)) {
                keyed_exits.emplace_back(trigger_guid,
                                         TriggerEvent{entity, world.find(was), false});
            }
        }
        for (const Guid& is : now) {
            if (!std::binary_search(trigger->inside.begin(), trigger->inside.end(), is)) {
                keyed_entries.emplace_back(trigger_guid,
                                           TriggerEvent{entity, world.find(is), true});
            }
        }
        trigger->inside = std::move(now);
    });
    for (auto* list : {&keyed_exits, &keyed_entries}) {
        std::sort(list->begin(), list->end(),
                  [&world](const std::pair<Guid, TriggerEvent>& a,
                           const std::pair<Guid, TriggerEvent>& b) {
                      const Guid a_who = world.guid(a.second.who);
                      const Guid b_who = world.guid(b.second.who);
                      return std::tie(a.first, a_who) < std::tie(b.first, b_who);
                  });
        for (const auto& [key, event] : *list) {
            if (world.is_alive(event.who)) {
                events.push_back(event);
            }
        }
    }
    return events;
}

world::Entity find_interaction(const world::World& world, world::Entity mover_entity) {
    const TopDownMover* mover = world.get<TopDownMover>(mover_entity);
    const scene2d::Transform2D* at = world.get<scene2d::Transform2D>(mover_entity);
    const reflect::TypeInfo* type = world.registry().find_type<Interactable>();
    if (mover == nullptr || at == nullptr || type == nullptr) {
        return {};
    }
    scene2d::Box probe = hitbox(*mover, at->x, at->y);
    double shift_x = 0.0;
    double shift_y = 0.0;
    switch (mover->facing) {
    case Facing::Down:
        shift_y = interact_reach;
        break;
    case Facing::Left:
        shift_x = -interact_reach;
        break;
    case Facing::Right:
        shift_x = interact_reach;
        break;
    case Facing::Up:
        shift_y = -interact_reach;
        break;
    }
    probe = scene2d::Box{probe.left + shift_x, probe.top + shift_y, probe.right + shift_x,
                         probe.bottom + shift_y};
    const double probe_x = (probe.left + probe.right) / 2.0;
    const double probe_y = (probe.top + probe.bottom) / 2.0;

    world::Entity best;
    double best_distance = 0.0;
    Guid best_guid;
    world.each(*type, [&](world::Entity entity, reflect::ObjectRef ref) {
        const Interactable* target = ref.as<Interactable>();
        const scene2d::Transform2D* where = world.get<scene2d::Transform2D>(entity);
        if (target == nullptr || where == nullptr || !target->enabled || entity == mover_entity) {
            return;
        }
        const scene2d::Box box = feet_box(where->x, where->y, target->width, target->height);
        if (!scene2d::overlaps(probe, box)) {
            return;
        }
        const double cx = ((box.left + box.right) / 2.0) - probe_x;
        const double cy = ((box.top + box.bottom) / 2.0) - probe_y;
        const double distance = (cx * cx) + (cy * cy);
        const Guid guid = world.guid(entity);
        if (!best.is_valid() || distance < best_distance ||
            (distance == best_distance && guid < best_guid)) {
            best = entity;
            best_distance = distance;
            best_guid = guid;
        }
    });
    return best;
}

} // namespace engine::game
