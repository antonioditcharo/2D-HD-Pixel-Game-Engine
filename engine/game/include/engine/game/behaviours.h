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
#include "engine/scene2d/scene2d.h"
#include "engine/world/world.h"

#include <cstdint>
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
};

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
struct Interactable {
    double width = 1.0;  ///< In tiles.
    double height = 1.0; ///< In tiles, upwards from the position.
    bool enabled = true;
};

/// How far in front of a mover's hitbox Interact reaches, in tiles.
inline constexpr double interact_reach = 0.5;

/// A box of `width` x `height` tiles centred on the bottom of (x, y), the convention every
/// behaviour's box follows.
[[nodiscard]] scene2d::Box feet_box(double x, double y, double width, double height) noexcept;
/// The box a mover at (x, y) occupies.
[[nodiscard]] scene2d::Box hitbox(const TopDownMover& mover, double x, double y) noexcept;

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

/// The Interactable that `mover` would reach with Interact: the nearest enabled one (not
/// the mover itself) whose box overlaps the mover's hitbox moved interact_reach tiles the
/// way it faces. Invalid if there is none.
[[nodiscard]] world::Entity find_interaction(const world::World& world, world::Entity mover);

} // namespace engine::game
