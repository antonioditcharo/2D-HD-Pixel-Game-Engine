#include "engine/game/behaviours.h"

#include "engine/game/hud.h"
#include "engine/game/particles.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

namespace engine::game {

// ---------------------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------------------

void register_behaviours(reflect::Registry& registry) {
    register_hud(registry);
    registry.add_type<Tags>("tags")
        .display_name("Tags")
        .category("General")
        .tooltip("Words that group objects, so one Rule can be about all of them: \"When an "
                 "object tagged enemy is touched\".")
        .property<&Tags::tags>("tags")
        .tooltip("Separated by commas: enemy, flying. In a Rule about a tag, \"it\" is the "
                 "tagged object whose event it is.");
    registry.add_choice_list<Facing>("facing")
        .display_name("Facing")
        .entry(Facing::Down, "down")
        .entry(Facing::Left, "left")
        .entry(Facing::Right, "right")
        .entry(Facing::Up, "up");
    registry.add_choice_list<WalkMode>("walk_mode")
        .display_name("Walking")
        .entry(WalkMode::Stand, "stand")
        .entry(WalkMode::Wander, "wander")
        .entry(WalkMode::Patrol, "patrol")
        .entry(WalkMode::Follow, "follow")
        .entry(WalkMode::GoTo, "go_to");
    auto walker = registry.add_type<Walker>("walker")
                      .display_name("Walker")
                      .category("Movement")
                      .tooltip("Its Top-down Mover walks by itself: wandering, on patrol, "
                               "following someone, or where logic sends it, finding its way "
                               "around walls. Never the player.");
    walker.property<&Walker::mode>("mode").display_name("Walking").tooltip(
        "Stand, wander near where it started, patrol a route, follow someone, or go to a "
        "point.");
    walker.property<&Walker::enabled>("enabled").tooltip("Turn off to make it stand still.");
    walker.property<&Walker::wander_radius>("wander_radius")
        .range(0.0, 32.0)
        .step(0.5)
        .unit(reflect::Unit::Tiles)
        .display_name("Wander within")
        .tooltip("Wandering: how far from where it started it goes.");
    walker.property<&Walker::rest>("rest")
        .range(0.0, 60.0)
        .step(0.25)
        .unit(reflect::Unit::Seconds)
        .display_name("Rest")
        .tooltip("Wandering and patrolling: how long it stands between walks.");
    walker.property<&Walker::route>("route").display_name("Route").tooltip(
        "Patrolling: the points it walks to in turn, in tiles: 2,3; 8,3; 8,6");
    walker.property<&Walker::target>("target").display_name("Follow").tooltip(
        "Following: the name of who it follows, such as hero.");
    walker.property<&Walker::distance>("distance")
        .range(0.0, 16.0)
        .step(0.25)
        .unit(reflect::Unit::Tiles)
        .display_name("Keep within")
        .tooltip("Following: how close it comes.");
    walker.property<&Walker::goal_x>("goal_x")
        .unit(reflect::Unit::Tiles)
        .display_name("Go to across")
        .tooltip("Going to a point: where, across.");
    walker.property<&Walker::goal_y>("goal_y")
        .unit(reflect::Unit::Tiles)
        .display_name("Go to down")
        .tooltip("Going to a point: where, down.");
    walker.property<&Walker::homed>("homed")
        .flags(reflect::property_flag::hidden)
        .tooltip("Whether where it started is known.");
    walker.property<&Walker::home_x>("home_x")
        .flags(reflect::property_flag::hidden)
        .tooltip("Where it started, across.");
    walker.property<&Walker::home_y>("home_y")
        .flags(reflect::property_flag::hidden)
        .tooltip("Where it started, down.");
    walker.property<&Walker::waiting>("waiting")
        .flags(reflect::property_flag::hidden)
        .tooltip("How long it still rests.");
    walker.property<&Walker::seed>("seed")
        .flags(reflect::property_flag::hidden)
        .tooltip("Its random choices.");
    walker.property<&Walker::leg>("leg")
        .flags(reflect::property_flag::hidden)
        .tooltip("The point of its route it walks to next.");
    walker.action<&Walker::walk_to>("walk_to", {"x", "y"})
        .text("Walk to {x}, {y}")
        .tooltip("Walks there by itself, around walls, then stands and says it arrived.");
    walker.action<&Walker::follow>("follow", {"name"})
        .text("Follow {name}")
        .tooltip("Keeps near someone, such as hero.");
    walker.action<&Walker::wander>("wander").text("Wander").tooltip(
        "Walks to places near where it is now, resting between walks.");
    walker.action<&Walker::patrol>("patrol").text("Patrol").tooltip(
        "Walks its route again from the first point.");
    walker.action<&Walker::stand>("stand").text("Stand still").tooltip("Stops walking.");
    walker.event<>("arrived")
        .text("When it arrives")
        .tooltip("Happens when it reaches where Walk to sent it.");

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
    mover.property<&TopDownMover::footsteps>("footsteps")
        .display_name("Footsteps")
        .tooltip("The player's steps: a sound played as they walk, where the ground has no "
                 "Footsteps of its own (a tilemap's). Empty for silence.");
    mover.property<&TopDownMover::step_length>("step_length")
        .range(0.1, 8.0)
        .step(0.1)
        .unit(reflect::Unit::Tiles)
        .display_name("Step length")
        .tooltip("Tiles walked between footsteps.");
    mover.property<&TopDownMover::moving>("moving")
        .flags(reflect::property_flag::read_only)
        .tooltip("Whether it is walking.");

    auto jumping = registry.add_type<Jumping>("jumping")
                       .display_name("Jumping")
                       .category("Movement")
                       .tooltip("Seen from the side: with a Top-down Mover, it walks left and "
                                "right, falls, lands and jumps.");
    jumping.property<&Jumping::gravity>("gravity")
        .range(0.0, 400.0)
        .step(5.0)
        .tooltip("How fast it falls faster, in tiles per second, every second.");
    jumping.property<&Jumping::jump_height>("jump_height")
        .range(0.0, 64.0)
        .step(0.5)
        .unit(reflect::Unit::Tiles)
        .display_name("Jump height")
        .tooltip("How high a jump reaches.");
    jumping.property<&Jumping::max_fall_speed>("max_fall_speed")
        .range(0.0, 400.0)
        .step(1.0)
        .flags(reflect::property_flag::advanced)
        .display_name("Fastest fall")
        .tooltip("The fastest it falls, in tiles per second.");
    jumping.property<&Jumping::fall_speed>("fall_speed")
        .range(-400.0, 400.0)
        .flags(reflect::property_flag::hidden)
        .tooltip("How fast it is falling, in tiles per second (rising below 0).");
    jumping.property<&Jumping::on_ground>("on_ground")
        .flags(reflect::property_flag::read_only)
        .display_name("On the ground")
        .tooltip("Whether it stands on something.");

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

    auto encounter = registry.add_type<EncounterArea>("encounter_area")
                         .display_name("Encounter Area")
                         .category("Events")
                         .tooltip("Random battles while the player walks inside it.");
    encounter.property<&EncounterArea::width>("width")
        .range(0.0, 256.0)
        .step(0.5)
        .unit(reflect::Unit::Tiles)
        .tooltip("How wide the area is, centred on the position.");
    encounter.property<&EncounterArea::height>("height")
        .range(0.0, 256.0)
        .step(0.5)
        .unit(reflect::Unit::Tiles)
        .tooltip("How tall the area is, upwards from the position.");
    encounter.property<&EncounterArea::groups>("groups")
        .display_name("Enemy groups")
        .tooltip("Who it may be: groups separated by |, enemies (rows of data/enemies) by "
                 "commas: slime, slime | bat");
    encounter.property<&EncounterArea::steps_min>("steps_min")
        .range(1.0, 1000.0)
        .step(1.0)
        .unit(reflect::Unit::Tiles)
        .display_name("At least")
        .tooltip("Tiles walked inside between battles, at least.");
    encounter.property<&EncounterArea::steps_max>("steps_max")
        .range(1.0, 1000.0)
        .step(1.0)
        .unit(reflect::Unit::Tiles)
        .display_name("At most")
        .tooltip("Tiles walked inside between battles, at most.");
    encounter.property<&EncounterArea::enabled>("enabled").tooltip(
        "Turn off for no battles here, such as after a boss is beaten.");
    encounter.property<&EncounterArea::background>("background")
        .display_name("Battle background")
        .tooltip("A picture behind its battles, by its path in assets/; empty for the world.");
    encounter.property<&EncounterArea::walked>("walked")
        .flags(reflect::property_flag::hidden)
        .tooltip("Tiles walked since the last battle.");
    encounter.property<&EncounterArea::next>("next")
        .flags(reflect::property_flag::hidden)
        .tooltip("Tiles to walk before the next battle.");
    encounter.property<&EncounterArea::seed>("seed")
        .flags(reflect::property_flag::hidden)
        .tooltip("Its random choices.");

    auto enemy = registry.add_type<EnemyOnMap>("enemy_on_map")
                     .display_name("Enemy on the Map")
                     .category("Events")
                     .tooltip("A monster the player sees: walking into it starts a battle, and "
                              "once beaten it is gone.");
    enemy.property<&EnemyOnMap::enemies>("enemies").tooltip(
        "Who the battle is against: rows of data/enemies, separated by commas: slime, bat");
    enemy.property<&EnemyOnMap::width>("width")
        .range(0.0, 64.0)
        .step(0.5)
        .unit(reflect::Unit::Tiles)
        .tooltip("How wide the box the player walks into is, centred on the position.");
    enemy.property<&EnemyOnMap::height>("height")
        .range(0.0, 64.0)
        .step(0.5)
        .unit(reflect::Unit::Tiles)
        .tooltip("How tall the box is, upwards from the position.");
    enemy.property<&EnemyOnMap::background>("background")
        .display_name("Battle background")
        .tooltip("A picture behind the battle, by its path in assets/; empty for the world.");
    enemy.property<&EnemyOnMap::beaten>("beaten").tooltip(
        "Beaten: hidden, and no more battles. Turn off to bring it back.");
    enemy.property<&EnemyOnMap::inside>("inside")
        .flags(reflect::property_flag::hidden)
        .tooltip("Whether the player is in its box.");
    enemy.event<>("defeated")
        .text("When it is beaten")
        .tooltip("The battle it started was won: drop something, open a door.");

    auto prefab = registry.add_type<PrefabLink>("prefab_link")
                      .display_name("Prefab")
                      .category("Objects")
                      .tooltip("This is a copy of a prefab, a reusable object; Edit > Update "
                               "copies of this prefab makes it match the prefab again.");
    prefab.property<&PrefabLink::prefab>("prefab")
        .flags(reflect::property_flag::read_only)
        .display_name("Prefab")
        .tooltip("The prefab this is a copy of.");

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

    auto source = registry.add_type<SoundSource>("sound_source")
                      .display_name("Sound Source")
                      .category("Sound")
                      .tooltip("A sound on repeat where this is, such as a waterfall or a fire: "
                               "louder the closer the player comes, and from its side.");
    source.property<&SoundSource::sound>("sound").tooltip("The sound to repeat.");
    source.property<&SoundSource::volume>("volume").range(0.0, 1.0).step(0.05).tooltip(
        "How loud it is right beside it.");
    source.property<&SoundSource::range>("range")
        .range(0.5, 64.0)
        .step(0.5)
        .unit(reflect::Unit::Tiles)
        .tooltip("How far it carries: silent this far away.");
    source.property<&SoundSource::playing>("playing").tooltip(
        "Turn off to silence it, and on again to start it.");
    source.property<&SoundSource::fade>("fade")
        .range(0.0, 10.0)
        .step(0.1)
        .unit(reflect::Unit::Seconds)
        .tooltip("How long it takes to fade in when it starts playing, and out when it stops "
                 "(or its entity goes). 0 starts and stops it at once.");

    registry.add_choice_list<physics::BodyType>("body_type")
        .display_name("Body type")
        .entry(physics::BodyType::Static, "static", "Fixed", "Never moves.")
        .entry(physics::BodyType::Dynamic, "dynamic", "Moving",
               "Falls, slides and bounces, and can be pushed.")
        .entry(physics::BodyType::Kinematic, "kinematic", "Driven",
               "Moves only where logic puts it, pushing what is in the way.");
    registry.add_choice_list<physics::ShapeKind>("body_shape")
        .display_name("Shape")
        .entry(physics::ShapeKind::Box, "box")
        .entry(physics::ShapeKind::Circle, "circle");
    auto simulated = registry.add_type<PhysicsBody>("physics_body")
                         .display_name("Physics Body")
                         .category("Physics")
                         .tooltip("Falls, slides, bounces and pushes, with real physics.");
    simulated.property<&PhysicsBody::type>("type").tooltip("How it moves.");
    simulated.property<&PhysicsBody::shape>("shape").tooltip("A box or a circle.");
    simulated.property<&PhysicsBody::width>("width")
        .range(0.02, 64.0)
        .step(0.125)
        .unit(reflect::Unit::Tiles)
        .tooltip("A box's width, centred on the position.");
    simulated.property<&PhysicsBody::height>("height")
        .range(0.02, 64.0)
        .step(0.125)
        .unit(reflect::Unit::Tiles)
        .tooltip("A box's height, upwards from the position.");
    simulated.property<&PhysicsBody::radius>("radius")
        .range(0.01, 32.0)
        .step(0.125)
        .unit(reflect::Unit::Tiles)
        .tooltip("A circle's radius; it sits on the position.");
    simulated.property<&PhysicsBody::density>("density")
        .range(0.001, 1000.0)
        .flags(reflect::property_flag::advanced)
        .tooltip("How heavy it is for its size.");
    simulated.property<&PhysicsBody::friction>("friction")
        .range(0.0, 10.0)
        .step(0.1)
        .flags(reflect::property_flag::advanced)
        .tooltip("How much it grips what it slides on.");
    simulated.property<&PhysicsBody::bounciness>("bounciness")
        .range(0.0, 1.0)
        .step(0.1)
        .tooltip("0 does not bounce; 1 bounces back as high as it fell.");
    simulated.property<&PhysicsBody::damping>("damping")
        .range(0.0, 100.0)
        .step(0.5)
        .tooltip("Slows it down over time. In top-down games, so pushed things stop.");
    simulated.property<&PhysicsBody::gravity_scale>("gravity_scale")
        .range(-100.0, 100.0)
        .flags(reflect::property_flag::advanced)
        .display_name("Gravity scale")
        .tooltip("How strongly gravity pulls it: 0 floats, 1 is normal.");
    simulated.action<&PhysicsBody::push>("push", {"x", "y"})
        .text("Push it by ({x}, {y})")
        .tooltip("An instant push. Heavier things move less.");
    simulated.event<std::string>("touched", {"who"})
        .text("When it touches {who}")
        .tooltip("Happens when it starts touching something: another body or a wall.");
}

Heard heard(const SoundSource& source, double x, double y, std::array<double, 2> ear) noexcept {
    if (!source.playing || !(source.range > 0.0) || !std::isfinite(source.volume)) {
        return Heard{};
    }
    const double dx = x - ear[0];
    const double distance = std::hypot(dx, y - ear[1]);
    const double level =
        std::clamp(source.volume, 0.0, 1.0) * std::clamp(1.0 - (distance / source.range), 0.0, 1.0);
    return Heard{level, std::clamp(dx / full_pan_tiles, -1.0, 1.0)};
}

std::optional<std::array<double, 2>> player_position(const world::World& world) {
    const reflect::TypeInfo* mover_type = world.registry().find_type<TopDownMover>();
    if (mover_type == nullptr) {
        return std::nullopt;
    }
    std::optional<std::array<double, 2>> found;
    world.each(*mover_type, [&](world::Entity entity, reflect::ObjectRef) {
        const scene2d::Transform2D* transform = world.get<scene2d::Transform2D>(entity);
        const Walker* walker = world.get<Walker>(entity);
        if (!found && transform != nullptr && (walker == nullptr || !walker->enabled)) {
            found = std::array<double, 2>{transform->x, transform->y};
        }
    });
    return found;
}

bool register_behaviour_components(world::World& world) {
    const bool mover = world.register_component<TopDownMover>();
    const bool walker = world.register_component<Walker>();
    const bool particles = world.register_component<ParticleEmitter>();
    const bool prefab = world.register_component<PrefabLink>();
    const bool encounter = world.register_component<EncounterArea>();
    const bool walk = world.register_component<WalkAnimation>();
    const bool body = world.register_component<Body>();
    const bool trigger = world.register_component<Trigger>();
    const bool interactable = world.register_component<Interactable>();
    const bool simulated = world.register_component<PhysicsBody>();
    const bool sound = world.register_component<SoundSource>();
    const bool jumping = world.register_component<Jumping>();
    const bool enemy = world.register_component<EnemyOnMap>() && world.register_component<Tags>();
    const bool hud = register_hud_components(world);
    return mover && walker && particles && prefab && encounter && walk && body && trigger &&
           interactable && simulated && sound && jumping && enemy && hud;
}

// ---------------------------------------------------------------------------------------
// Movement
// ---------------------------------------------------------------------------------------

void fall(Jumping& jumping, bool jump, double seconds, const std::function<double(double)>& move) {
    const auto finite_or = [](double value, double otherwise) {
        return std::isfinite(value) ? value : otherwise;
    };
    const double gravity = std::max(finite_or(jumping.gravity, 0.0), 0.0);
    const double height = std::max(finite_or(jumping.jump_height, 0.0), 0.0);
    const double fastest = std::max(finite_or(jumping.max_fall_speed, 0.0), 0.0);
    double speed = finite_or(jumping.fall_speed, 0.0);
    if (jump && jumping.on_ground) {
        speed = -std::sqrt(2.0 * gravity * height); // v^2 = 2 g h: it rises `height`
    }
    speed = std::min(speed + (gravity * seconds), fastest);
    const double wanted = speed * seconds;
    const double moved = wanted != 0.0 ? move(wanted) : 0.0;
    // Short of the way by more than rounding: something is in the way.
    const bool stopped = std::abs(moved) < std::abs(wanted) - 1e-9;
    if (wanted > 0.0) {
        jumping.on_ground = stopped; // landed, or still standing
    } else if (wanted < 0.0) {
        jumping.on_ground = false; // rising
    } // not moving at all (no gravity): as it was
    jumping.fall_speed = stopped ? 0.0 : speed;
}

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

    world.each(*mover_type, [&](world::Entity entity, reflect::ObjectRef ref) {
        TopDownMover* mover = ref.as<TopDownMover>();
        scene2d::Transform2D* transform = world.get<scene2d::Transform2D>(entity);
        if (mover == nullptr || transform == nullptr) {
            return;
        }
        WalkAnimation* walk = world.get<WalkAnimation>(entity);
        scene2d::Sprite* sprite = world.get<scene2d::Sprite>(entity);
        // A mover with a dynamic Physics Body is moved by the simulation (PhysicsLink).
        const PhysicsBody* simulated = world.get<PhysicsBody>(entity);
        const bool by_physics =
            simulated != nullptr && simulated->type == physics::BodyType::Dynamic;
        std::vector<scene2d::Box> others;
        if (!by_physics) {
            others.reserve(obstacles.size());
            for (const Obstacle& obstacle : obstacles) {
                if (obstacle.entity != entity) {
                    others.push_back(obstacle.box);
                }
            }
        }
        const auto sweep_by = [&](scene2d::Axis axis, double wanted) {
            const scene2d::Box box = hitbox(*mover, transform->x, transform->y);
            const double allowed = scene2d::sweep_boxes(
                box, axis, scene2d::sweep(world, box, axis, wanted, ppu), others);
            (axis == scene2d::Axis::X ? transform->x : transform->y) += allowed;
            return allowed;
        };
        // A Walker walks where it decided (steer_walkers()), not with the keys.
        double mx = dx;
        double my = dy;
        double scale = 1.0;
        const Walker* walker = world.get<Walker>(entity);
        const bool steered = walker != nullptr && walker->enabled;
        if (steered) {
            mx = walker->step_dx;
            my = walker->step_dy;
            scale = std::clamp(walker->step_scale, 0.0, 1.0);
        }
        const double mlength = std::sqrt((mx * mx) + (my * my));
        if (Jumping* jumping = world.get<Jumping>(entity); jumping != nullptr) {
            // Seen from the side: left and right with the move actions, up and down by
            // falling and jumping.
            mover->moving = mover->enabled && mx != 0.0;
            if (mover->moving) {
                mover->facing = facing_for(mover->facing, mx, 0.0);
            }
            if (by_physics) {
                jumping->on_ground = false;
            } else {
                if (mover->moving) {
                    sweep_by(scene2d::Axis::X,
                             (mx > 0.0 ? 1.0 : -1.0) * mover->speed * seconds * scale);
                }
                fall(*jumping,
                     mover->enabled && !steered &&
                         (input.pressed("jump") || input.pressed("move_up")),
                     seconds, [&](double wanted) { return sweep_by(scene2d::Axis::Y, wanted); });
            }
            if (walk != nullptr && sprite != nullptr) {
                update_walk(*mover, *walk, *sprite, seconds);
            }
            return;
        }
        mover->moving = mover->enabled && mlength > 0.0;
        if (mover->moving) {
            mover->facing = facing_for(mover->facing, mx, my);
        }
        if (mover->moving && !by_physics) {
            const double step = mover->speed * seconds * scale / mlength;
            // One axis at a time, so a diagonal into a wall slides along it.
            for (const scene2d::Axis axis : {scene2d::Axis::X, scene2d::Axis::Y}) {
                sweep_by(axis, (axis == scene2d::Axis::X ? mx : my) * step);
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
            const Walker* walker = world.get<Walker>(entity);
            if (mover != nullptr && at != nullptr && (walker == nullptr || !walker->enabled)) {
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

std::optional<std::string> update_encounters(world::World& world, double walked,
                                             std::string* background) {
    const reflect::TypeInfo* type = world.registry().find_type<EncounterArea>();
    const std::optional<std::array<double, 2>> player = player_position(world);
    if (type == nullptr || !player || !std::isfinite(walked) || walked <= 0.0) {
        return std::nullopt;
    }
    std::optional<std::string> found;
    world.each(*type, [&](world::Entity entity, reflect::ObjectRef ref) {
        EncounterArea* area = ref.as<EncounterArea>();
        const scene2d::Transform2D* at = world.get<scene2d::Transform2D>(entity);
        if (found || area == nullptr || at == nullptr || !area->enabled) {
            return;
        }
        const scene2d::Box box = feet_box(at->x, at->y, area->width, area->height);
        const double px = (*player)[0];
        const double py = (*player)[1];
        if (px < box.left || px >= box.right || py <= box.top || py > box.bottom) {
            return;
        }
        if (area->seed == 0) {
            std::uint32_t hash = 2166136261U;
            for (const char c : world.guid(entity).to_string()) {
                hash = (hash ^ static_cast<std::uint8_t>(c)) * 16777619U;
            }
            area->seed = hash == 0 ? 1U : hash;
        }
        const auto random = [area] {
            area->seed ^= area->seed << 13U;
            area->seed ^= area->seed >> 17U;
            area->seed ^= area->seed << 5U;
            return static_cast<double>(area->seed % 10000U) / 10000.0;
        };
        const double low = std::max(1.0, std::min(area->steps_min, area->steps_max));
        const double high = std::max(low, area->steps_max);
        if (!(area->next > 0.0)) {
            area->next = low + ((high - low) * random());
        }
        area->walked += walked;
        if (area->walked < area->next) {
            return;
        }
        area->walked = 0.0;
        area->next = 0.0;
        // One of its groups, at random.
        std::vector<std::string> groups;
        std::string group;
        for (const char c : area->groups + "|") {
            if (c == '|') {
                if (group.find_first_not_of(" \t") != std::string::npos) {
                    groups.push_back(group);
                }
                group.clear();
            } else {
                group += c;
            }
        }
        if (!groups.empty()) {
            found = groups[static_cast<std::size_t>(random() * static_cast<double>(groups.size())) %
                           groups.size()];
            if (background != nullptr) {
                *background = area->background;
            }
        }
    });
    return found;
}

std::optional<world::Entity> update_enemies_on_map(world::World& world) {
    const reflect::TypeInfo* type = world.registry().find_type<EnemyOnMap>();
    const std::optional<std::array<double, 2>> player = player_position(world);
    if (type == nullptr) {
        return std::nullopt;
    }
    std::optional<world::Entity> found;
    world.each(*type, [&](world::Entity entity, reflect::ObjectRef ref) {
        EnemyOnMap* enemy = ref.as<EnemyOnMap>();
        const scene2d::Transform2D* at = world.get<scene2d::Transform2D>(entity);
        if (enemy == nullptr || at == nullptr) {
            return;
        }
        bool inside = false;
        if (player && !enemy->beaten) {
            const scene2d::Box box = feet_box(at->x, at->y, enemy->width, enemy->height);
            inside = (*player)[0] >= box.left && (*player)[0] < box.right &&
                     (*player)[1] > box.top && (*player)[1] <= box.bottom;
        }
        if (inside && !enemy->inside && !found) {
            found = entity;
        }
        enemy->inside = inside;
    });
    return found;
}

world::Entity find_interaction(const world::World& world, world::Entity mover_entity) {
    const TopDownMover* mover = world.get<TopDownMover>(mover_entity);
    const scene2d::Transform2D* at = world.get<scene2d::Transform2D>(mover_entity);
    const reflect::TypeInfo* type = world.registry().find_type<Interactable>();
    const Walker* walker = world.get<Walker>(mover_entity);
    if (mover == nullptr || at == nullptr || type == nullptr ||
        (walker != nullptr && walker->enabled)) {
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

// ---------------------------------------------------------------------------------------
// Physics
// ---------------------------------------------------------------------------------------

namespace {

/// From a body's position (the bottom of its shape) up to its centre.
double centre_offset(const PhysicsBody& body) {
    return body.shape == physics::ShapeKind::Circle ? std::max(body.radius, 0.0)
                                                    : std::max(body.height, 0.0) / 2.0;
}

} // namespace

PhysicsLink::PhysicsLink(double gravity_x, double gravity_y)
    : simulation_(physics::PhysicsWorld::create(gravity_x, gravity_y)) {}

std::uint64_t PhysicsLink::user_for(const Guid& guid) {
    if (const auto found = user_ids_.find(guid); found != user_ids_.end()) {
        return found->second;
    }
    const std::uint64_t user = next_user_++;
    user_ids_.emplace(guid, user);
    users_.emplace(user, guid);
    return user;
}

void PhysicsLink::build(world::World& world, std::uint32_t ppu) {
    simulation_->clear();
    linked_.clear();
    walls_.clear();
    built_ = true;
    // Solid tilemaps: one static box per run of solid tiles in a row.
    const reflect::TypeInfo* tilemap_type = world.registry().find_type<scene2d::Tilemap>();
    if (tilemap_type == nullptr) {
        return;
    }
    world.each(*tilemap_type, [&](world::Entity entity, reflect::ObjectRef ref) {
        const scene2d::Tilemap* tilemap = ref.as<scene2d::Tilemap>();
        const scene2d::Transform2D* at = world.get<scene2d::Transform2D>(entity);
        std::string error;
        const scene2d::TileGrid* grid =
            tilemap != nullptr && tilemap->solid ? tilemap->grid(error) : nullptr;
        if (grid == nullptr || at == nullptr) {
            return;
        }
        const std::uint32_t units = std::max<std::uint32_t>(ppu, 1);
        const double tile =
            static_cast<double>(tilemap->tile_size == 0 ? units : tilemap->tile_size) /
            static_cast<double>(units);
        const std::uint64_t user = user_for(world.guid(entity));
        for (std::uint32_t y = 0; y < grid->height; ++y) {
            std::uint32_t x = 0;
            while (x < grid->width) {
                if (grid->at(x, y) < 0) {
                    ++x;
                    continue;
                }
                const std::uint32_t start = x;
                while (x < grid->width && grid->at(x, y) >= 0) {
                    ++x;
                }
                physics::BodyDef wall;
                wall.type = physics::BodyType::Static;
                wall.width = static_cast<double>(x - start) * tile;
                wall.height = tile;
                wall.x = at->x + (static_cast<double>(start) * tile) + (wall.width / 2.0);
                wall.y = at->y + ((static_cast<double>(y) + 0.5) * tile);
                wall.user = user;
                walls_.push_back(simulation_->add(wall));
            }
        }
    });
}

std::vector<std::pair<world::Entity, world::Entity>>
PhysicsLink::step(world::World& world, const input::InputState& input, double seconds,
                  std::uint32_t ppu) {
    std::vector<std::pair<world::Entity, world::Entity>> touches;
    const reflect::TypeInfo* type = world.registry().find_type<PhysicsBody>();
    if (simulation_ == nullptr || type == nullptr) {
        return touches;
    }
    if (!built_) {
        build(world, ppu);
    }
    // Bodies for new Physics Bodies; the simulation follows Positions it does not drive.
    const double dx =
        (input.held("move_right") ? 1.0 : 0.0) - (input.held("move_left") ? 1.0 : 0.0);
    const double dy = (input.held("move_down") ? 1.0 : 0.0) - (input.held("move_up") ? 1.0 : 0.0);
    const double length = std::sqrt((dx * dx) + (dy * dy));
    std::vector<Guid> present;
    world.each(*type, [&](world::Entity entity, reflect::ObjectRef ref) {
        PhysicsBody* body = ref.as<PhysicsBody>();
        const scene2d::Transform2D* at = world.get<scene2d::Transform2D>(entity);
        if (body == nullptr || at == nullptr) {
            return;
        }
        const Guid guid = world.guid(entity);
        present.push_back(guid);
        const double offset = centre_offset(*body);
        auto found = linked_.find(guid);
        if (found == linked_.end()) {
            physics::BodyDef def;
            def.type = body->type;
            def.shape = body->shape;
            def.x = at->x;
            def.y = at->y - offset;
            def.width = body->width;
            def.height = body->height;
            def.radius = body->radius;
            def.density = body->density;
            def.friction = body->friction;
            def.restitution = body->bounciness;
            def.damping = body->damping;
            def.gravity_scale = body->gravity_scale;
            def.user = user_for(guid);
            const physics::BodyId id = simulation_->add(def);
            if (!id.is_valid()) {
                return;
            }
            found = linked_.emplace(guid, Linked{id, offset, body->type}).first;
        }
        const Linked& link = found->second;
        if (link.type == physics::BodyType::Static) {
            simulation_->set_position(link.body, at->x, at->y - link.centre_offset);
        } else if (link.type == physics::BodyType::Kinematic) {
            // Driven: move to where logic put it within this step, pushing on the way.
            const std::optional<physics::BodyState> now = simulation_->state(link.body);
            if (now && seconds > 0.0) {
                simulation_->set_velocity(link.body, (at->x - now->x) / seconds,
                                          (at->y - link.centre_offset - now->y) / seconds);
            }
        } else if (const TopDownMover* mover = world.get<TopDownMover>(entity); mover != nullptr) {
            // A walking body: its input sets its speed; the simulation does the rest.
            const double speed = mover->enabled && length > 0.0 ? mover->speed / length : 0.0;
            simulation_->set_velocity(link.body, dx * speed, dy * speed);
        }
        if (body->push_x != 0.0 || body->push_y != 0.0) {
            simulation_->push(link.body, body->push_x, body->push_y);
            body->push_x = 0.0;
            body->push_y = 0.0;
        }
    });
    // Bodies whose entity or component went away.
    std::sort(present.begin(), present.end());
    for (auto it = linked_.begin(); it != linked_.end();) {
        if (!std::binary_search(present.begin(), present.end(), it->first)) {
            simulation_->remove(it->second.body);
            it = linked_.erase(it);
        } else {
            ++it;
        }
    }

    simulation_->step(seconds);

    // Dynamic bodies move their entities.
    for (const auto& [guid, link] : linked_) {
        if (link.type != physics::BodyType::Dynamic) {
            continue;
        }
        const world::Entity entity = world.find(guid);
        scene2d::Transform2D* at = world.get<scene2d::Transform2D>(entity);
        const std::optional<physics::BodyState> state = simulation_->state(link.body);
        if (at != nullptr && state) {
            at->x = state->x;
            at->y = state->y + link.centre_offset;
        }
    }
    for (const physics::Contact& contact : simulation_->contacts()) {
        const auto a = users_.find(contact.a);
        const auto b = users_.find(contact.b);
        if (a != users_.end() && b != users_.end()) {
            const world::Entity first = world.find(a->second);
            const world::Entity second = world.find(b->second);
            if (world.is_alive(first) && world.is_alive(second)) {
                touches.emplace_back(first, second);
            }
        }
    }
    return touches;
}

} // namespace engine::game
