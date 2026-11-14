#pragma once

// The logic runtime's script host: a sandboxed Luau VM that runs the code generated from
// no-code Rules and Sequences (and, later, optional Luau script blocks), with memory and
// execution budgets, waits, and access to reflected objects.
//
// See docs/dev/logic-runtime.md for the design, the script API and the rules.

#include "engine/foundation/reflect.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine::logic {

/// Identifies a loaded script. Never reused within one Vm.
struct ScriptId {
    std::uint32_t value = 0;

    [[nodiscard]] constexpr bool is_valid() const noexcept { return value != 0; }
    friend constexpr bool operator==(ScriptId, ScriptId) = default;
};

/// Identifies a task: one running Sequence or function. Never reused within one Vm.
struct TaskId {
    std::uint32_t value = 0;

    [[nodiscard]] constexpr bool is_valid() const noexcept { return value != 0; }
    friend constexpr bool operator==(TaskId, TaskId) = default;
};

/// What went wrong in a script.
enum class ScriptErrorKind : std::uint8_t {
    Compile, ///< The source does not compile.
    Runtime, ///< An error while running, including wrong use of the script API.
    Budget,  ///< The task ran too long without waiting and was stopped.
    Memory,  ///< The scripts used more memory than allowed.
};

/// A problem reported by a script. The task that caused it is stopped; everything else
/// keeps running.
struct ScriptError {
    ScriptErrorKind kind = ScriptErrorKind::Runtime;
    std::string script;  ///< The script's name, as given to Vm::load().
    int line = 0;        ///< 1-based line in the script's source; 0 if unknown.
    std::string message; ///< Plain language, without the "script:line:" prefix.
};

/// Limits that keep user logic from freezing or exhausting the game.
struct VmLimits {
    /// Memory for all scripts together, in bytes. Allocations made by scripts beyond
    /// this fail with a Memory error.
    std::size_t memory_bytes = std::size_t{64} * 1024 * 1024;
    /// How many safepoints (loop iterations, calls and returns) one task may pass between
    /// two waits. A task that goes over is stopped with a Budget error: this is what
    /// catches endless loops.
    std::uint64_t steps_per_run = 1'000'000;
    /// How many safepoints all tasks together may pass in one update(). When used up, the
    /// remaining tasks that are due resume in the next update() instead, in their order.
    /// No task is stopped because of this limit.
    std::uint64_t steps_per_frame = 10'000'000;
};

/// The sandboxed script host.
///
/// Scripts see the Luau standard library (read-only), `print`, and the `sx` table:
///
/// | Function | Does |
/// |---|---|
/// | `sx.call(object, function, ...)` | Calls an action, condition or value getter |
/// | `sx.get(object, property)` | Reads a property |
/// | `sx.set(object, property, value)` | Writes a property |
/// | `sx.wait(seconds)` | Pauses the task; it resumes in a later update() |
/// | `sx.log(...)` | Sends text to the log handler, like `print` |
/// | `sx.on(event, function)` | Runs the function as a new task each time the event is emitted |
/// | `sx.trace()` | Reports the current line to the trace handler (live highlighting) |
/// | `sx.tagged(tag)` | A list of the names of the objects with a tag (set_group_resolver) |
///
/// Objects are named with bind(), or found by an object resolver (set_object_resolver), for
/// example the entities of a world. An object may have several parts, such as an entity's
/// components: `sx.call`, `sx.get` and `sx.set` use the first part that has the member.
/// Choices are passed and returned as entry ids, for example "north".
///
/// A Vm is not thread-safe, and bound C++ functions must not call back into the Vm that
/// is running them: such calls are refused.
class Vm {
public:
    explicit Vm(const reflect::Registry& registry, VmLimits limits = {});
    Vm(const Vm&) = delete;
    Vm& operator=(const Vm&) = delete;
    Vm(Vm&&) = delete;
    Vm& operator=(Vm&&) = delete;
    ~Vm();

    /// Makes `object` available to scripts under `name`. Fails if the name is not a valid
    /// id, is already bound, or the object is null or of an unregistered type. The object
    /// must stay alive until it is unbound or the Vm is destroyed.
    [[nodiscard]] bool bind(std::string_view name, reflect::ObjectRef object);
    /// Removes a binding. Returns false if there was none.
    bool unbind(std::string_view name);

    /// Finds objects that are not bound by name, such as entities in a world: given the name
    /// a script used, returns the parts that make up that object (for an entity, one per
    /// component), or nothing if there is no such object. Called at every access, so it may
    /// return different addresses each time, as world storage moves. Parts of unregistered
    /// types are ignored.
    using ObjectResolver = std::function<std::vector<reflect::ObjectRef>(std::string_view name)>;
    /// Sets the resolver, used for names that bind() does not know.
    void set_object_resolver(ObjectResolver resolver);
    /// Lists the names of the objects with a tag, for `sx.tagged(tag)` (the "For each
    /// object tagged" step). Without one, every tag has none.
    using GroupResolver = std::function<std::vector<std::string>(std::string_view tag)>;
    void set_group_resolver(GroupResolver resolver);

    /// The types an object is made of: its bound type, or the types of the parts the resolver
    /// returns. Empty if there is no such object. Suits the compiler's ObjectTypes.
    [[nodiscard]] std::vector<const reflect::TypeInfo*> object_types(std::string_view name) const;

    /// Compiles `source` and runs its top level as a task, which may wait. Each script has
    /// its own globals. Returns nothing, and reports an error, if the source does not
    /// compile or its top level fails before finishing or waiting for the first time.
    [[nodiscard]] std::optional<ScriptId> load(std::string_view name, std::string_view source);

    /// Starts a task that calls the global function `function` of `script` with
    /// `arguments`. The task runs at once until it waits or finishes. Returns nothing if
    /// the function does not exist, or if the task fails before waiting (the error is
    /// reported). A task that finishes at once still gets an id.
    [[nodiscard]] std::optional<TaskId> start(ScriptId script, std::string_view function,
                                              std::span<const reflect::Value> arguments = {});

    /// Advances time by `seconds`, delivers the events emitted while scripts were running,
    /// runs every Brain once (each takes at most one transition, then runs its state), then
    /// resumes the tasks whose wait is over, in the order they were started. A task never
    /// resumes in the update that started it.
    void update(double seconds);

    /// Emits an event: every function registered for it with `sx.on` starts as a task, in
    /// registration order, with the payload as its arguments. `object` names a bound
    /// object whose type has the event; the payload must then match the event's
    /// parameters. An empty `object` emits a game-wide event, such as "game_start". An
    /// `object` starting with '#' is a group (the objects with a tag, "#enemy"): after the
    /// event's values comes one more, the text naming the member whose event it is (for
    /// a Rule's "it").
    ///
    /// Called while a script runs (from a bound C++ function), the event is delivered at
    /// the start of the next update() instead. Returns false, and does nothing, if the
    /// object, event or payload is not valid.
    bool emit(std::string_view object, std::string_view event,
              std::span<const reflect::Value> payload = {});

    /// True while the task is waiting or running.
    [[nodiscard]] bool is_running(TaskId task) const noexcept;
    /// Stops a waiting task. Returns false if it is not running.
    bool stop(TaskId task);
    /// Stops every waiting task and drops the events waiting to be delivered, for example
    /// when a saved game is loaded. Rules stay registered. Returns false, doing nothing,
    /// while a script is running.
    bool stop_all();
    /// The number of tasks that are waiting.
    [[nodiscard]] std::size_t task_count() const noexcept;

    /// Receives `print` and `sx.log` output. Without a handler, the output is dropped.
    void
    set_log_handler(std::function<void(std::string_view script, std::string_view text)> handler);
    /// Receives each `sx.trace()` call with the line it was made on. Map the line to a
    /// block with CompiledLogic::block_at_line().
    void set_trace_handler(std::function<void(std::string_view script, int line)> handler);

    /// The names of the loaded scripts, in load order.
    [[nodiscard]] std::vector<std::string> scripts() const;
    /// The variables of a script compiled from a logic asset, sorted by name, for saved
    /// games. Yes/no values come back as Bool, text and choices (their entry id) as Text,
    /// whole numbers as Int and other numbers as Float. Empty for an unknown script.
    [[nodiscard]] std::vector<std::pair<std::string, reflect::Value>>
    variables(std::string_view script) const;
    /// Sets an existing variable of a script, as variables() reported it. Fails, changing
    /// nothing, for an unknown script or variable, a value of another kind than the one
    /// it holds (Int and Float count as one), or while a script is running.
    bool set_variable(std::string_view script, std::string_view name, const reflect::Value& value);

    /// Every problem reported since the last clear_errors(), oldest first.
    [[nodiscard]] const std::vector<ScriptError>& errors() const noexcept;
    void clear_errors() noexcept;

    /// Bytes currently allocated by the VM, including the standard library.
    [[nodiscard]] std::size_t memory_used() const noexcept;
    /// Safepoints passed by all tasks during the last update().
    [[nodiscard]] std::uint64_t steps_last_update() const noexcept;

    /// The internal state. Public only so that the script API's C functions can reach it.
    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace engine::logic
