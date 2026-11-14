#pragma once

// Behaviours (plan/05 §3.1): components that give an entity a capability with no logic to
// write. Moving and turning with the player's input, walk animations, blocking, trigger
// areas, and things the player can interact with. The runtime runs their systems in each
// fixed step.
//
// See docs/dev/game.md §4.

#include "engine/foundation/guid.h"
#include "engine/foundation/reflect.h"
#include "engine/input/input.h"
#include "engine/physics/physics.h"
#include "engine/scene2d/scene2d.h"
#include "engine/world/world.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine::game {

/// The four directions a character can face, in the row order of a character sheet.
enum class Facing : std::uint8_t { Down, Left, Right, Up };

/// "Top-down Mover": moves the entity with the move_up, move_down, move_left and
/// move_right actions. Diagonals are as fast as straight lines. Solid tilemaps and Solid
/// Bodies stop it: its hitbox, centred on the bottom of its position (the feet), slides
/// along them.
struct TopDownMover {
    double speed = 4.0; ///< Tiles per second.
    bool enabled = true;
    double hitbox_width = 0.75; ///< In tiles.
    double hitbox_height = 0.5; ///< In tiles, upwards from the position.
    Facing facing = Facing::Down;
    bool moving = false; ///< A move action was held in the last step (read-only).
    /// The player's footsteps: a sound in assets/ played as they walk, where the ground
    /// (a tilemap's Footsteps) has none of its own; empty for silence.
    std::string footsteps;
    double step_length = 0.6; ///< Tiles walked between footsteps.
};

/// What a Walker does.
enum class WalkMode : std::uint8_t {
    Stand,  ///< Stays where it is.
    Wander, ///< Walks to places near where it started, resting between walks.
    Patrol, ///< Walks a route of points in turn, resting at each, round and round.
    Follow, ///< Keeps near another entity, finding its way around walls.
    GoTo,   ///< Walks to one point, then stands and says it arrived.
};

/// "Walker": the entity's Top-down Mover walks by itself, not with the player's keys: a
/// villager who wanders, a guard on patrol, a friend who follows, someone sent somewhere
/// by logic. It finds its way around solid tiles and Solid Bodies (grid A*, find_path()).
/// A Walker is never the player: the camera does not follow it, it does not interact, and
/// it does not set off Trigger Areas (so it cannot walk through a door). Everyone stands
/// still while a message is open.
struct Walker {
    WalkMode mode = WalkMode::Wander;
    bool enabled = true;
    double wander_radius = 3.0; ///< Wander: how far from where it started, in tiles.
    double rest = 1.5;          ///< Wander and Patrol: seconds standing between walks.
    std::string route;          ///< Patrol: points in tiles, "x,y; x,y; ...".
    std::string target;         ///< Follow: the name of the entity to follow.
    double distance = 1.5;      ///< Follow: how close it comes, in tiles.
    double goal_x = 0.0;        ///< Go to: where, in tiles.
    double goal_y = 0.0;
    // Hidden, saved with the scene and games.
    bool homed = false; ///< Whether `home_x`, `home_y` (where it started) are set.
    double home_x = 0.0;
    double home_y = 0.0;
    double waiting = 0.0;   ///< Seconds still to rest.
    std::uint32_t seed = 0; ///< Wander's random choices; 0 takes one from the GUID.
    std::uint32_t leg = 0;  ///< Patrol: the point it walks to next.
    // Not saved: worked out again after loading.
    std::vector<std::array<double, 2>> path; ///< Points still to reach, nearest first.
    double step_dx = 0.0; ///< Where it walks in this step, for move_characters().
    double step_dy = 0.0;
    double step_scale = 1.0; ///< Of a full step: less on reaching a point.
    double repath = 0.0;     ///< Follow: seconds until the way is found again.
    double stuck = 0.0;      ///< Seconds it has not moved while it should.
    double last_x = 0.0;
    double last_y = 0.0;

    /// Walks to (x, y) in tiles, then stands and says it arrived.
    void walk_to(double x, double y);
    /// Follows the entity named `name`.
    void follow(const std::string& name);
    /// Wanders near where it is now.
    void wander();
    /// Walks its route again, from its first point.
    void patrol();
    /// Stands still.
    void stand();
};

/// The way for `mover` (on entity `self`) from (x, y) to (to_x, to_y), in tiles, around
/// solid tiles and enabled Solid Bodies (but `self`'s and `ignore`'s): the points to walk
/// through, nearest first, ending at the goal. Walks tile by tile in eight directions,
/// never cutting a wall's corner, the shortest way (A*). Empty if already there; nothing
/// if the goal cannot be reached (or is more than 64 tiles away, beyond what is searched).
[[nodiscard]] std::optional<std::vector<std::array<double, 2>>>
find_path(const world::World& world, const TopDownMover& mover, world::Entity self, double x,
          double y, double to_x, double to_y, std::uint32_t ppu, world::Entity ignore = {});

/// Decides where every enabled Walker walks in the next step of `seconds` (Stand while
/// `paused`, as when a message is open). Returns the Walkers that arrived where they were
/// sent, for their "arrived" event. Run it before move_characters().
std::vector<world::Entity> steer_walkers(world::World& world, double seconds, std::uint32_t ppu,
                                         bool paused);

/// "Jumping": the game seen from the side. With a Top-down Mover on the same entity, the
/// move actions walk it left and right only; it falls, lands on solid tiles and Solid
/// Bodies, and jumps when Jump (or Up) is pressed while it stands on something. Make its
/// mover's hitbox as tall as the character, since it bumps its head too.
struct Jumping {
    double gravity = 40.0;        ///< How fast falling speeds up, in tiles per second, per second.
    double jump_height = 3.0;     ///< How high a jump reaches, in tiles.
    double max_fall_speed = 20.0; ///< In tiles per second.
    double fall_speed = 0.0;      ///< Downwards, in tiles per second; below 0 rising (hidden).
    bool on_ground = false;       ///< Standing on something, after the last step (read-only).
};

/// One step of `seconds` for `jumping`: if `jump` and it stands on something, it leaps up
/// fast enough to rise jump_height; then gravity speeds its fall (up to max_fall_speed)
/// and it moves down by that much (up, rising) through `move`, which moves it as far as it
/// can towards a distance and returns how far it went. Stopped going down, it has landed:
/// on_ground, and no longer falling. Stopped going up, it bumped its head and falls.
/// Numbers that are not finite count as 0 (gravity, height, fall speed) or no limit.
void fall(Jumping& jumping, bool jump, double seconds, const std::function<double(double)>& move);

/// The direction to face when moving by (dx, dy), from `current`: unchanged when not
/// moving; on a diagonal, unchanged if `current` is one of its two directions, otherwise
/// the horizontal one.
[[nodiscard]] Facing facing_for(Facing current, double dx, double dy) noexcept;

/// "Walk Animation": shows a character sheet's row for the mover's facing (down, left,
/// right, up, from the top), cycling through its frames while moving and showing the
/// standing frame when not. The Sprite's frame size must be set to one cell of the sheet.
/// Do not combine it with an Animation on the same entity: the Animation would win.
struct WalkAnimation {
    std::uint32_t frames_per_direction = 3;
    double frames_per_second = 6.0;
    std::uint32_t standing_frame = 1; ///< Counted from 0 within the row.
    /// Back and forth (0, 1, 2, 1, 0, …) rather than starting over (0, 1, 2, 0, …).
    bool ping_pong = true;
    double elapsed = 0.0; ///< Seconds walked since the last stop (hidden).
};

/// Advances `walk` by `seconds` for a mover and sets `sprite`'s frame.
void update_walk(const TopDownMover& mover, WalkAnimation& walk, scene2d::Sprite& sprite,
                 double seconds);

/// "Solid Body": blocks Top-down Movers, like a wall that can stand anywhere (a villager,
/// a crate, a sign).
struct Body {
    double width = 0.75; ///< In tiles.
    double height = 0.5; ///< In tiles, upwards from the position.
    bool enabled = true;
};

/// "Trigger Area": tells Rules when a Top-down Mover walks in ("entered") or out
/// ("exited"), with the mover's name as "who". For doors, cutscene zones and traps.
struct Trigger {
    double width = 1.0;  ///< In tiles.
    double height = 1.0; ///< In tiles, upwards from the position.
    bool enabled = true;
    /// The movers inside after the last update, sorted. Not reflected, so not saved.
    std::vector<Guid> inside;
};

/// "Interactable": tells Rules when the player presses Interact while facing it within
/// reach ("interacted", with the mover's name as "who"). For people to talk to, signs and
/// chests.
/// "Encounter Area": random battles while the player walks inside it, as classic RPGs have
/// them in grass and caves. It covers `width` x `height` tiles centred on the bottom of its
/// position; every `steps_min` to `steps_max` tiles walked inside, a battle starts against
/// one of its `groups` ("slime, slime | bat": groups separated by |, enemies by commas).
struct EncounterArea {
    double width = 4.0;
    double height = 4.0;
    std::string groups;
    double steps_min = 12.0; ///< Tiles walked between battles, at least...
    double steps_max = 24.0; ///< ...and at most.
    bool enabled = true;
    std::string background; ///< A picture in assets/ behind its battles; empty for the world.
    double walked = 0.0;    ///< Tiles walked inside since the last battle (hidden; saved).
    double next = 0.0;      ///< Tiles to walk before the next one; 0 picks (hidden; saved).
    std::uint32_t seed = 0; ///< Its random choices (hidden; saved).
};

/// One step: the player (player_position()) walked `walked` tiles; every enabled Encounter
/// Area the player is inside counts them, and the first whose count comes due gives the
/// enemies of one of its groups, picked at random, for a battle. Nothing otherwise.
/// With `background`, the area's Battle background goes there.
[[nodiscard]] std::optional<std::string> update_encounters(world::World& world, double walked,
                                                           std::string* background = nullptr);

/// "Tags": words that group objects, "enemy, flying", so one Rule can be about all of them:
/// "When an object tagged enemy is touched" (logic names the group "#enemy", and the object
/// whose event it is "it"). As GDevelop's object groups and Construct's families are.
struct Tags {
    std::string tags; ///< Separated by commas or spaces; letters, digits and _, any case.
};

/// "Enemy on the Map": a monster the player sees, as in many RPGs, that starts a battle when
/// the player walks into it, and is gone once beaten.
struct EnemyOnMap {
    std::string enemies;    ///< Who the battle is against: rows of data/enemies, by commas.
    double width = 1.0;     ///< The box the player walks into, in tiles, centred...
    double height = 1.0;    ///< ...and upwards from the position.
    std::string background; ///< A picture in assets/ behind the battle; empty for the world.
    bool beaten = false;    ///< Beaten: hidden, and no more battles (saved).
    bool inside = false;    ///< The player is in its box (hidden; saved).
};

/// One step: the first Enemy on the Map not beaten whose box the player walked into this
/// step (they were outside before). Nothing otherwise, or for the player standing in one.
[[nodiscard]] std::optional<world::Entity> update_enemies_on_map(world::World& world);

/// "Prefab": this entity (and what is under it) is a copy of a prefab, a reusable object
/// saved as a ".dprefab" file; the editor updates copies from it.
struct PrefabLink {
    std::string prefab; ///< The GUID of the prefab file.
};

struct Interactable {
    double width = 1.0;  ///< In tiles.
    double height = 1.0; ///< In tiles, upwards from the position.
    bool enabled = true;
};

/// "Sound Source": a sound on repeat where the entity is (a waterfall, a fire, a crowd),
/// heard from the player: full volume beside it, fading to silence `range` tiles away,
/// and from the side it is on.
struct SoundSource {
    std::string sound;   ///< The GUID of a sound asset (or its path in assets/).
    double volume = 1.0; ///< Beside it, 0 to 1.
    double range = 8.0;  ///< How far it carries, in tiles.
    bool playing = true;
    double fade = 0.5; ///< Seconds it takes to fade in when it starts, and out when it stops.
};

/// How loud (0 to `source.volume`) and from which side (-1 left to 1 right) a sound source
/// at (x, y) is heard from `ear`. Silent when it is not playing, or `range` away or more.
struct Heard {
    double volume = 0.0;
    double pan = 0.0;
};
[[nodiscard]] Heard heard(const SoundSource& source, double x, double y,
                          std::array<double, 2> ear) noexcept;
/// How far to one side, in tiles, a sound is fully on that side's speaker.
inline constexpr double full_pan_tiles = 8.0;

/// "Physics Body": the entity takes part in the physics simulation (ADR-008). Dynamic bodies
/// fall, slide, bounce and are pushed; their Position follows the simulation. Static and
/// kinematic bodies follow their Position, so logic moves them. The shape is centred on the
/// bottom of the position, like every behaviour's box. Solid tilemaps are walls to it.
struct PhysicsBody {
    physics::BodyType type = physics::BodyType::Dynamic;
    physics::ShapeKind shape = physics::ShapeKind::Box;
    double width = 1.0; ///< In tiles, for boxes.
    double height = 1.0;
    double radius = 0.5; ///< In tiles, for circles.
    double density = 1.0;
    double friction = 0.6;
    double bounciness = 0.0;
    double damping = 0.0; ///< Slows it down: in top-down games, so pushed things stop.
    double gravity_scale = 1.0;
    // Pushes logic asked for, applied at the next step. Not reflected, so not saved.
    double push_x = 0.0;
    double push_y = 0.0;

    /// "Push it by (x, y)": an instant push, in mass times tiles per second.
    void push(double x, double y) {
        push_x += x;
        push_y += y;
    }
};

/// How far in front of a mover's hitbox Interact reaches, in tiles.
inline constexpr double interact_reach = 0.5;

/// A box of `width` x `height` tiles centred on the bottom of (x, y), the convention every
/// behaviour's box follows.
[[nodiscard]] scene2d::Box feet_box(double x, double y, double width, double height) noexcept;
/// The box a mover at (x, y) occupies.
[[nodiscard]] scene2d::Box hitbox(const TopDownMover& mover, double x, double y) noexcept;

/// Where the player is: the first Top-down Mover's position. The camera follows it, and
/// sounds are heard from it. Nothing if there is none.
[[nodiscard]] std::optional<std::array<double, 2>> player_position(const world::World& world);

/// Registers the behaviours and the "facing" choice list. Needs scene2d's types.
void register_behaviours(reflect::Registry& registry);
/// Makes the behaviours usable as components of `world`.
bool register_behaviour_components(world::World& world);

/// Moves every enabled Top-down Mover by one step of `seconds` with `input`, stopping at
/// solid tiles and enabled Bodies (at their positions when the step starts; a mover's own
/// Body does not block it), sets facing and moving, and updates Walk Animations.
void move_characters(world::World& world, const input::InputState& input, double seconds,
                     std::uint32_t ppu);

struct TriggerEvent {
    world::Entity trigger;
    world::Entity who;
    bool entered = true; ///< Else exited.
};

/// Compares every enabled Trigger with the Top-down Movers whose hitboxes overlap it, and
/// returns who entered and exited since the last call: exits first, then entries, each
/// sorted by trigger then mover GUID. A disabled trigger's occupants exit.
[[nodiscard]] std::vector<TriggerEvent> update_triggers(world::World& world);

/// Connects the world to a physics simulation, one fixed step at a time.
class PhysicsLink {
public:
    /// Gravity in tiles per second squared; (0, 0) for top-down games.
    PhysicsLink(double gravity_x, double gravity_y);

    /// Rebuilds the simulation from the world at the next step: after loading a save.
    void invalidate() noexcept { built_ = false; }

    /// One step: builds walls from solid tilemaps (once), adds and removes bodies as
    /// Physics Bodies appear and go, moves movers that have a dynamic body by their input,
    /// applies pushes, steps, writes dynamic bodies' positions back, and returns the pairs
    /// of entities that started touching (a wall is its tilemap's entity).
    std::vector<std::pair<world::Entity, world::Entity>>
    step(world::World& world, const input::InputState& input, double seconds, std::uint32_t ppu);

    [[nodiscard]] physics::PhysicsWorld* simulation() noexcept { return simulation_.get(); }

private:
    struct Linked {
        physics::BodyId body;
        double centre_offset = 0.0; ///< From the position up to the shape's centre.
        physics::BodyType type = physics::BodyType::Dynamic;
    };

    void build(world::World& world, std::uint32_t ppu);
    std::uint64_t user_for(const Guid& guid);

    std::unique_ptr<physics::PhysicsWorld> simulation_;
    bool built_ = false;
    std::unordered_map<Guid, Linked, GuidHash> linked_;
    std::vector<physics::BodyId> walls_;
    std::unordered_map<std::uint64_t, Guid> users_;
    std::unordered_map<Guid, std::uint64_t, GuidHash> user_ids_;
    std::uint64_t next_user_ = 1;
};

/// The Interactable that `mover` would reach with Interact: the nearest enabled one (not
/// the mover itself) whose box overlaps the mover's hitbox moved interact_reach tiles the
/// way it faces. Invalid if there is none.
[[nodiscard]] world::Entity find_interaction(const world::World& world, world::Entity mover);

} // namespace engine::game
