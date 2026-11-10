# The Logic Runtime

> **Status: v0.1, the script host.** A sandboxed Luau VM that runs tasks with waits, calls
> into the reflection registry, and enforces memory and execution budgets. Every behaviour
> below is covered by tests. The Logic IR and the compiler from no-code blocks come next
> (§7).
>
> Code: [`engine/logic/include/engine/logic/vm.h`](../../engine/logic/include/engine/logic/vm.h)
> and [`src/vm.cpp`](../../engine/logic/src/vm.cpp). Tests:
> [`tests/logic/vm_test.cpp`](../../tests/logic/vm_test.cpp).

## 1. What it is for

All no-code logic (Rules, Sequences, Brains) compiles to Luau [S71] and runs here
(ADR-007, `plan/03` §6). This module is the lowest part of that path: it hosts the VM,
runs code, pauses and resumes Sequences, and gives scripts controlled access to the
engine through the reflection registry ([reflection.md](reflection.md)).

Users never see Luau. They see blocks such as "Heal [20] HP" and "Wait [0.5] seconds". The
compiler (next increment) turns those into calls to the script API below.

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

- **Objects** are named by `Vm::bind()`. Names follow the id rules of the registry.
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

## 7. Next increments

In order:

1. **Logic IR and compiler** for Rules and Sequences, with the debug map from source lines
   to block ids (`plan/03` §6). Spike L (`plan/08` Phase 0).
2. **Events and the message bus:** firing the registry's events and running the Rules
   whose "When" matches (`plan/05` §5, steps 3 and 7).
3. **Object handles:** GUID-based references from the world instead of names bound by hand.
4. **Stress test** from ADR-007: 5,000 rules and 200 concurrent sequences, with timings.
5. **Brain runner** (state machines) and **"why didn't this fire?"** support.
6. **Bytecode caching** per asset, so exported games skip compilation.
