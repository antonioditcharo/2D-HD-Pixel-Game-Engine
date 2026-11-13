# Input

> **Status: v0.1.** Named actions bound to keys and gamepad buttons, an RPG preset,
> per-step pressed and released detection, exact recording and replay, operating-system
> events through SDL, and conditions and an event for no-code logic. Every behaviour below
> is covered by tests.
>
> Code: [`engine/input/include/engine/input/input.h`](../../engine/input/include/engine/input/input.h),
> [`src/input.cpp`](../../engine/input/src/input.cpp) and
> [`src/sdl_events.cpp`](../../engine/input/src/sdl_events.cpp). Tests:
> [`tests/input/input_test.cpp`](../../tests/input/input_test.cpp).

## 1. Actions, not keys

Games ask *"was Interact pressed?"*, never *"is Z down?"* (`plan/02` U1). Keys and gamepad
buttons are **bound** to named actions in an `InputMap`. Players can then rebind controls,
and the same game works with keyboard and gamepad without any extra logic.

| Action (RPG preset) | Keyboard | Gamepad |
|---|---|---|
| `move_up`, `move_down`, `move_left`, `move_right` | Arrows and WASD | D-pad |
| `interact` | Z, Enter, Space | South (A / Cross) |
| `jump` (games seen from the side: [game.md](game.md), Jumping; Up jumps too) | Space | South (A / Cross) |
| `cancel` | X, Backspace | East (B / Circle) |
| `menu` | Escape | Start |

Keys are named by position on a US layout, so WASD stays under the same fingers on other
layouts. `binding_name()` names a control as players read it (*Z*, *Enter*, *Up*, *Pad A*),
and `InputState::pressed_bindings()` lists the controls that went down this step, for
"press a key" while the player rebinds one on the game's Options page ([menus.md](menus.md)). Gamepad buttons are named by position (South, East, ...), not by label.

## 2. State per step

```cpp
input::InputState state(input::InputMap::rpg_preset());

// Every fixed step:
state.begin_step();
running = input::pump_events(state);   // operating-system events, through SDL
if (state.pressed("interact")) { ... }  // went down during this step
if (state.held("move_left")) { ... }    // down now, by any of its controls
```

- An action is **held** while any of its controls is down. With both W and Up held,
  letting go of W keeps `move_up` held.
- **Pressed** and **released** are edges within the step: an action pressed with two keys
  at once is pressed once. Key repeats from the operating system are ignored.
- `set_map()` rebinds while the game runs.

## 3. Recording and replay

`pump_events(state, &recording, step)` records every change with its step number, and
`recording.replay(step, state)` feeds it back. With the fixed-step clock
([game.md](game.md)), a replay reproduces the same states step for step, as the
determinism goal (`plan/03` §7, N6) requires. A test replays a recording twice and
checks that both runs match an expected trace.

## 4. In no-code logic

`input::register_types()` registers the `input` object for the logic runtime
([logic-runtime.md](logic-runtime.md)). Bind an `InputObject` to the VM as `input`:

| Block | Kind |
|---|---|
| "{action} is held" | Condition |
| "{action} was pressed" | Condition |
| "{action} was released" | Condition |
| "When {action} is pressed" | Event, carrying the action |

## 5. Next increments

1. **Analogue input:** sticks and triggers as axes, with dead zones.
2. **Mouse** buttons and position, for menus.
3. **The rebinding screen,** generated from the map (`plan/02` U2).
4. **Saving recordings** with a save game or a bug report.
