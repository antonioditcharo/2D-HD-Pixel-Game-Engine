# The Logic Runtime

> **Status: v0.2.** The script host (a sandboxed Luau VM with tasks, waits, events and
> budgets) and the compiler from the Logic IR (Rules and Sequences) to Luau, with a debug
> map from lines to blocks and live tracing. Every behaviour below is covered by tests.
> §8 has the measured scale, §9 what comes next.
>
> Code: [`vm.h`](../../engine/logic/include/engine/logic/vm.h),
> [`ir.h`](../../engine/logic/include/engine/logic/ir.h) and
> [`compiler.h`](../../engine/logic/include/engine/logic/compiler.h) in
> `engine/logic/include/engine/logic/`. Tests:
> [`vm_test.cpp`](../../tests/logic/vm_test.cpp) and
> [`compiler_test.cpp`](../../tests/logic/compiler_test.cpp) in `tests/logic/`.

## 1. What it is for

All no-code logic (Rules, Sequences, Brains) compiles to Luau [S71] and runs here
(ADR-007, `plan/03` §6). This module is the lowest part of that path: it hosts the VM,
runs code, pauses and resumes Sequences, and gives scripts controlled access to the
engine through the reflection registry ([reflection.md](reflection.md)).

Users never see Luau. They see blocks such as "Heal [20] HP" and "Wait [0.5] seconds". The
editor stores those as the Logic IR, and the compiler (§7) turns the IR into calls to the
script API below.

## 2. Using it

```cpp
#include "engine/logic/vm.h"

engine::reflect::Registry registry;
register_characters(registry); // Npc with a "heal" action, see reflection.md

engine::logic::Vm vm(registry);
vm.set_log_handler([](std::string_view script, std::string_view text) { /* to the editor log */ });

Npc elder;
vm.bind("elder", engine::reflect::ObjectRef::of(elder));

auto script = vm.load("village/intro", R"(
    function intro()
        sx.call("elder", "say", "You're finally awake...")
        sx.wait(1.5)
        sx.call("elder", "heal", 20)
    end
)");
auto task = vm.start(*script, "intro"); // runs until the first wait

// Every frame:
vm.update(frame_seconds);               // resumes tasks whose wait is over
for (const auto& error : vm.errors()) { /* show on the block, see §5 */ }
```

`ObjectRef` is a type-erased, non-owning reference to an object of any registered type. It
was added to the reflection registry for this module (`reflect.h`), together with
`get_property`, `set_property` and `call` overloads that take one.

## 3. The script API

| Function | Does |
|---|---|
| `sx.call(object, function, ...)` | Calls an action, condition or value getter. Conditions and getters return their value |
| `sx.get(object, property)` | Reads a property |
| `sx.set(object, property, value)` | Writes a property, after the same checks as the inspector (kind, range, choices, read-only) |
| `sx.wait(seconds)` | Pauses the task. It resumes in the first `update()` where the time is over |
| `sx.log(...)`, `print(...)` | Sends the values, separated by spaces, to the log handler |
| `sx.on(event, function)` | Runs the function as a new task each time the event is emitted, with the event's values as arguments |
| `sx.trace()` | Reports the line it is called from to the trace handler, for live highlighting |

- **Objects** are named by `Vm::bind()`, whose names follow the id rules of the registry,
  or found by an object resolver (`Vm::set_object_resolver()`), such as the world's
  entities ([game.md](game.md)). An object may have several parts, such as an entity's
  components: the first part with the member asked for is used.
- **Values** convert by the parameter's kind:

  | Kind | Script value |
  |---|---|
  | Yes/No | `true` or `false` only (not 0 or 1) |
  | Whole number | A number without a fraction, within the 64-bit range |
  | Number | Any number |
  | Text | A string only (numbers are not converted) |
  | Choice | The entry id as text (`"north"`), or its numeric value. Results come back as the id |

- Whole numbers come back to scripts as Luau numbers, which are exact up to 2^53.

## 4. Tasks and time

- **A task** is one Luau coroutine: a Sequence, a function started by `start()`, or a
  script's top level. Each starts at once and runs until it waits or ends.
- **`update(seconds)`** subtracts the time from every waiting task, then resumes those
  whose wait is over, **in the order they were started**. This is step 5 of the frame
  order in `plan/05` §5.
- `wait(0)` resumes in the next update. A wait counts as over when less than a nanosecond
  is left, so ten updates of 0.1 s end a one-second wait despite rounding.
- **Each script has its own globals**, so two scripts cannot see or break each other's
  variables. Tasks of one script share that script's globals.
- `sx.wait` works only in the task itself, not in a coroutine the script made or a
  callback such as a `table.sort` comparison: there would be no task to pause.
- **A task never resumes in the update that started it,** whatever started it. So
  `wait(0)` always means "the next update".
- **`stop_all()`** stops every waiting task and drops undelivered events, keeping Rules
  registered: what loading a saved game needs.

### Variables for saved games

A logic asset's variables live in one table per script. Compiled code also keeps it as a
script-private global, `_VARS` (on the first line, so no line numbers move), so the host
can read and restore it: `scripts()` lists the loaded scripts, `variables(script)` gives
each variable's value sorted by name (choices as their entry id; whole numbers as Int),
and `set_variable(script, name, value)` restores one. It refuses unknown variables and
values of another kind, so a save cannot add variables a script never declared.

### Events

- **`Vm::emit(object, event, payload)`** starts every function registered for the event
  with `sx.on`, in registration order (load order, then order within a script). This is
  how Rules run (step 3 of `plan/05` §5).
- For an object's event, the object must be bound and its type must have the event; the
  payload is checked against the event's parameters like a function's arguments. An empty
  object means a game-wide event, such as `game_start`, with any payload.
- **Events emitted while a script runs**, for example by a C++ action such as "Open
  chest" that announces "opened", are queued. They are delivered at the start of the next
  `update()`, before waiting tasks resume. Events those deliveries emit wait for the update
  after, so two rules cannot trigger each other forever within one frame (`plan/05` §5,
  step 7).

## 5. Safety

| Protection | How | Verified by |
|---|---|---|
| **Sandbox** | Luau's `luaL_sandbox` and `luaL_sandboxthread` [S71]: the standard library and `sx` are read-only. There is no file, network, `require` or `loadstring` access | "The sandbox protects the libraries and keeps scripts apart" |
| **Endless loops** | Luau calls an interrupt hook at every safepoint (loop back edge, call and return). Each run of a task may pass `steps_per_run` safepoints (default 1,000,000) before the next wait. Over that, the task is stopped with a Budget error | "An endless loop is stopped and reported" |
| **`pcall` cannot swallow the stop** | Once the budget is used up, every later safepoint raises again, so the stop escapes a `pcall` that catches it | Same test (the `sneaky` task) |
| **Frame budget** | All tasks together may pass `steps_per_frame` safepoints per `update()` (default 10,000,000). When used up, the remaining due tasks resume in the next update, in order. Nothing is stopped | "The frame budget defers tasks" |
| **Memory** | A custom allocator counts every byte. While a script runs, allocations beyond `memory_bytes` (default 64 MB) fail and the task is stopped with a Memory error. The host then collects the garbage the task left, because Luau scripts cannot (`collectgarbage` does not exist in Luau) | "Scripts that use too much memory are stopped, and the Vm keeps working" |
| **Re-entrancy** | A bound C++ function that calls back into the running Vm (`start`, `update`, `bind`, ...) is refused | "Bound functions cannot call back into the running Vm" |

The memory limit applies to script execution only. Work the host does (creating threads,
loading bytecode) is never refused, so the total can go slightly above the limit; it
cannot grow without a script running.

### Errors

Problems never crash the game. The task that caused one is stopped, the error is added to
`Vm::errors()`, and everything else keeps running.

| Field | Contents |
|---|---|
| `kind` | `Compile`, `Runtime`, `Budget` or `Memory` |
| `script` | The name given to `load()` |
| `line` | The line in the script's source. The compiler's debug map (next increment) turns it into the block to highlight |
| `message` | Plain language, for example *"'Amount' of 'Heal' must be between 1 and 999, but was given 5000."* |

Messages use display names from the registry, so they read like the blocks.

## 6. Rules and design decisions

| Rule | Why |
|---|---|
| **The VM is reached only through `sx`.** Scripts never touch C++ objects directly. | Every access goes through the registry's checks, so a script cannot put a value into a state the inspector would reject. |
| **Luau errors are C++ exceptions** (`LUA_USE_LONGJMP=0`, Luau's default). | Destructors run when a script error unwinds through host code, so the host functions can use ordinary RAII types. |
| **Budgets count safepoints, not time.** | Counting is deterministic: the same script stops at the same place on every machine, so tests and replays agree. A time-based frame budget can be added once Spike R gives real frame timings. |
| **A failed script top level drops the script.** | Its functions may be half defined. `load()` returns nothing so the caller cannot start them. |
| **Each `Vm` is independent.** | Several can exist side by side, for example one for the editor's preview and one for play mode (tested). |
| **The VM is not thread-safe.** | Logic runs on the game thread in a fixed order (`plan/05` §5). |
| **Luau is linked statically, VM and compiler only.** | Exported games need the compiler only while bytecode is not cached (`plan/03` §6); the analysis and code-generation libraries are not linked. |

## 7. The Logic IR and the compiler

### The IR (`ir.h`)

A `LogicAsset` is what one logic file in a project holds, and compiles to one script:

| Part | Holds |
|---|---|
| `Variable` | An id, a kind (choices name their choice list) and an optional starting value. Shared by everything in the asset |
| `Sequence` | A name and a list of steps (layer 4 of `plan/05` §3) |
| `Rule` | When (an object's event, or a game-wide event), If (conditions, all must hold), Do and Otherwise (layer 3) |
| `Brain` | A state machine (layer 5): states with On Enter, While In and On Exit steps and transitions out, plus "from any state" transitions; see Brains below |

| Statement | Does |
|---|---|
| `Do` | Runs an action of an object |
| `Set` | Sets a property of an object |
| `SetVariable` | Sets a variable |
| `Wait` | Waits a number of seconds |
| `WaitUntil` | Waits until a condition holds: checked at once (no wait if it already holds), then once per update. Compiles to `while not (…) do sx.wait(0) end` |
| `If` | Runs one list of steps or the other |
| `Repeat` | Runs steps a number of times |
| `Run` | Runs another sequence and waits until it ends |
| `Log` | Writes a value to the log |

Expressions fill the slots: a constant, a property, a condition or value of an object, a
variable, a value the rule's event carries, not / all of / any of, comparisons, and
`+ - × ÷`. Builder functions in `engine::logic::ir` create all of these in code.

Every statement, sequence and rule has a **block id**, unique in the asset; expressions may
have one, so a single condition can be highlighted and explained.

### Brains

A **Brain** (`plan/05` §3.5) is a state machine for characters, enemy AI, bosses and game
flow:

- Each **state** has **On Enter**, **While In** and **On Exit** steps, and **transitions**
  out. A transition names the state it leads to, has conditions (all must hold; none
  means only the time counts) and a minimum **time in the state** in seconds. The first
  transition that holds wins, so their order is their priority.
- **From any state** transitions, such as *"HP at most 0: Dead"*, are checked before the
  current state's own, and never towards the state the Brain is already in.
- **When it runs:** every `Vm::update()` runs every Brain once, after events are delivered
  and before waiting Sequences resume (`plan/05` §5, step 4). On its first run a Brain
  enters its starting state (the first, or `initial`). Then it adds the update's time to
  its time in the state, takes at most one transition (On Exit, switch, time back to 0,
  On Enter), and runs the While In steps of the state it is in. Scripts run in load
  order, Brains by name, so the order is fixed.
- **Brains cannot wait.** A Wait or Wait until step, or Running a sequence (which may wait), anywhere in
  a Brain is a compile error that says to start the sequence from a rule.
- **Saved with the game:** a Brain's state, time and whether it has started are variables
  of its script (`@brain.<name>.state`, `.time`, `.on`), so `variables()` and saved games
  include them; restoring a state resumes there without running On Enter again (tested).
- Compiled code: three local functions (enter, exit, while) choose the steps by state
  name, and `brain_<name>(dt)` does the rest; `_BRAINS`, a script-private table set on the
  first line, lists them for `update()`.

Not yet: hierarchical states, templates (Patrol, Chase, Attack, Return), and the live view
that lights the active state (`plan/05` §3.5).

### Checking

`compile(asset, registry, objects)` checks the whole asset against the reflection registry
before generating anything. `objects` gives the types each object name is made of, for
example `Vm::object_types` or `game::object_types(world)`. Every problem becomes a `Diagnostic` on the block, in the same plain
language as runtime errors. Examples from the tests:

- *"'Amount' of 'Add gold' must be between 1 and 999, but is 5000."*
- *"'Has item' is a condition: it answers yes or no and changes nothing. Use it in an If."*
- *"These two values cannot be compared: one is one of: north, east, south, west, the other
  yes or no (true or false)."*
- *"This rule has no conditions, so its Otherwise actions can never run."*

What is checked: names and ids; objects, properties, functions and events exist; a
function's role suits its place (actions in Do, conditions and values in slots); argument
counts; kinds, with whole numbers accepted where numbers are; constants against ranges and
choice lists; read-only properties; variables and sequences exist; nesting depth (64
levels). An asset with any error produces no source, so it cannot be run by mistake.

### Code generation

- Calls go through `sx.call`, `sx.get` and `sx.set`, so the VM checks values at run time
  too: a variable can hold a value that no constant check could catch.
- **One block per line.** `line_blocks` maps every line of the source to its block, and
  `block_at_line()` turns the line of a runtime error, or of a trace, into the block to
  highlight.
- **Text cannot escape.** Every byte of user text that is not printable ASCII is written as
  a three-digit escape, so text cannot end its string, start a comment or add a line (a
  test feeds it `"); print("pwned") --` with a newline).
- Sequences become global functions named by `sequence_entry()` (`seq_<name>`), for
  `Vm::start()`. Rules become `sx.on` registrations keyed by `event_key()` ("chest.opened").
- **Tracing:** with `CompileOptions::trace`, every block first calls `sx.trace()`. The
  trace handler receives the line, and the debug map gives the block: this is the
  editor's live highlight (`plan/05` §6).

## 8. Measured scale

The stress test from ADR-007 ([`stress_test.cpp`](../../tests/logic/stress_test.cpp)): 50
objects, 5,000 rules (100 per object event) and 200 sequences that each act once per frame
for 60 frames. The test checks that every rule and step ran exactly once, and prints the
timings. Ranges over four runs of the `RelWithDebInfo` build on a shared 4-core 2.1 GHz
Xeon server VM, which is slower and noisier than a gaming PC:

| Step | Time |
|---|---|
| Compile the asset (31,201 lines of Luau) | 10–22 ms |
| Load it (Luau compiles, then the top level registers 5,000 rules) | 35–55 ms |
| Fire all 5,000 rules once | 2.5–4.5 ms |
| One frame of 200 sequences resuming | 0.04–0.14 ms (worst frame) |
| VM memory afterwards | about 6.3 MiB |

- **Sequences are cheap:** 200 concurrent ones cost under 1% of a 60 fps frame.
- **Rules cost about 0.5–0.9 µs each** when fired, because each runs as its own task. A
  game rarely fires more than a few hundred in one frame. If profiling of real projects
  shows otherwise, rules that never wait can run without a coroutine (§9).
- **Loading is the slowest step**, and it happens once per map. Bytecode caching (§9)
  removes the compile part.

The test does not assert timings, because CI machines vary; it fails only on wrong
results.

## 9. Next increments

In order:

1. **More blocks:** parallel steps, For each, and text joining.
2. **Brain runner** (state machines) and **"why didn't this fire?"** support, which
   re-evaluates a rule's conditions one by one using their block ids.
3. **Bytecode caching** per asset, so exported games skip compilation.
4. **Faster rules:** run rules that cannot wait without creating a coroutine, if
   profiling shows the need.
