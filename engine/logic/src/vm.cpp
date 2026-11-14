#include "engine/logic/vm.h"

#include "engine/logic/compiler.h"

#include <lua.h>
#include <luacode.h>
#include <lualib.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iterator>
#include <map>
#include <system_error>
#include <utility>

#include "common.h"

namespace engine::logic {

namespace {

namespace reflect = engine::reflect;
using common::expected_text;
using common::format_number;
using common::is_valid_script_name;
using common::quote;
using common::range_text;

/// A wait shorter than this counts as over. It absorbs rounding when many small frame
/// times are subtracted from a wait, so that "wait 1 second" at 10 updates of 0.1 s
/// resumes on the 10th update and not the 11th.
constexpr double wait_epsilon = 1e-9;

/// The text the interrupt raises. It is replaced by a plain-language message when the
/// error is reported.
constexpr const char* budget_message = "ran too long without waiting";

struct Script {
    ScriptId id;
    std::string name;
    lua_State* thread{nullptr}; ///< Holds the script's own globals.
    int ref{LUA_NOREF};
};

struct Task {
    TaskId id;
    const Script* script{nullptr};
    lua_State* thread{nullptr};
    int ref{LUA_NOREF};
    double wait_left = 0.0;
    std::uint64_t born = 0; ///< The update count when the task was created.
    bool finished = false;
};

/// A function registered with sx.on.
struct Handler {
    const Script* script{nullptr};
    int ref{LUA_NOREF};
};

/// An event emitted while a script was running, delivered in the next update.
struct PendingEvent {
    std::string key;
    std::vector<reflect::Value> payload;
    std::vector<const reflect::ChoiceInfo*> choices;
};

struct Binding {
    reflect::ObjectRef object;
    const reflect::TypeInfo* type{nullptr};
};

/// A short description of the script value at `index`, for messages.
[[nodiscard]] std::string given_text(lua_State* state, int index) {
    switch (lua_type(state, index)) {
    case LUA_TNONE:
    case LUA_TNIL:
        return "nothing";
    case LUA_TBOOLEAN:
        return lua_toboolean(state, index) != 0 ? "true" : "false";
    case LUA_TNUMBER:
        return format_number(lua_tonumber(state, index));
    case LUA_TINTEGER:
        return std::to_string(lua_tointeger64(state, index, nullptr));
    case LUA_TSTRING: {
        std::size_t length = 0;
        const char* text = lua_tolstring(state, index, &length);
        constexpr std::size_t max_shown = 40;
        const std::string shown(text, std::min(length, max_shown));
        return quote(length > max_shown ? shown + "..." : shown);
    }
    case LUA_TTABLE:
        return "a table";
    case LUA_TFUNCTION:
        return "a function";
    default:
        return lua_typename(state, lua_type(state, index));
    }
}

/// Raises a script error with `message`. The current line is added by Luau.
[[noreturn]] void raise(lua_State* state, const std::string& message) {
    luaL_error(state, "%s", message.c_str());
}

[[nodiscard]] std::string_view string_at(lua_State* state, int index) {
    std::size_t length = 0;
    const char* text = lua_tolstring(state, index, &length);
    return {text, length};
}

/// Pushes a reflected value. Choices are pushed as their entry id.
void push_value(lua_State* state, const reflect::Value& value, const reflect::ChoiceInfo* choice) {
    if (const bool* flag = std::get_if<bool>(&value)) {
        lua_pushboolean(state, *flag ? 1 : 0);
    } else if (const std::int64_t* number = std::get_if<std::int64_t>(&value)) {
        const reflect::ChoiceEntry* entry =
            choice != nullptr ? choice->find_entry(*number) : nullptr;
        if (entry != nullptr) {
            lua_pushlstring(state, entry->id.data(), entry->id.size());
        } else {
            lua_pushnumber(state, static_cast<double>(*number));
        }
    } else if (const double* real = std::get_if<double>(&value)) {
        lua_pushnumber(state, *real);
    } else if (const std::string* text = std::get_if<std::string>(&value)) {
        lua_pushlstring(state, text->data(), text->size());
    }
}

enum class Conversion : std::uint8_t { Ok, WrongKind, UnknownChoice, NotWhole };

/// Converts the script value at `index` to the given kind.
[[nodiscard]] Conversion to_value(lua_State* state, int index, reflect::ValueKind kind,
                                  const reflect::ChoiceInfo* choice, reflect::Value& out) {
    const int type = lua_type(state, index);
    switch (kind) {
    case reflect::ValueKind::Bool:
        if (type != LUA_TBOOLEAN) {
            return Conversion::WrongKind;
        }
        out = lua_toboolean(state, index) != 0;
        return Conversion::Ok;
    case reflect::ValueKind::Float:
        if (type == LUA_TINTEGER) {
            out = static_cast<double>(lua_tointeger64(state, index, nullptr));
            return Conversion::Ok;
        }
        if (type != LUA_TNUMBER) {
            return Conversion::WrongKind;
        }
        out = lua_tonumber(state, index);
        return Conversion::Ok;
    case reflect::ValueKind::Text:
        if (type != LUA_TSTRING) {
            return Conversion::WrongKind;
        }
        out = std::string(string_at(state, index));
        return Conversion::Ok;
    case reflect::ValueKind::Choice:
        if (type == LUA_TSTRING) {
            const reflect::ChoiceEntry* entry =
                choice != nullptr ? choice->find_entry(string_at(state, index)) : nullptr;
            if (entry == nullptr) {
                return Conversion::UnknownChoice;
            }
            out = entry->value;
            return Conversion::Ok;
        }
        [[fallthrough]];
    case reflect::ValueKind::Int: {
        if (type == LUA_TINTEGER) {
            out = lua_tointeger64(state, index, nullptr);
            return Conversion::Ok;
        }
        if (type != LUA_TNUMBER) {
            return Conversion::WrongKind;
        }
        const double number = lua_tonumber(state, index);
        // The int64 range is [-2^63, 2^63); both bounds are exact doubles.
        constexpr double limit = 9223372036854775808.0;
        if (!std::isfinite(number) || std::trunc(number) != number || number < -limit ||
            number >= limit) {
            return Conversion::NotWhole;
        }
        out = static_cast<std::int64_t>(number);
        return Conversion::Ok;
    }
    }
    return Conversion::WrongKind;
}

/// The message for a value that does not fit a parameter or property. `what` names it, for
/// example "'Amount' of 'Heal'".
[[nodiscard]] std::string misfit_message(const std::string& what, reflect::ValueKind kind,
                                         const reflect::NumericLimits& limits,
                                         const reflect::ChoiceInfo* choice,
                                         reflect::SetResult result, const std::string& given) {
    switch (result) {
    case reflect::SetResult::OutOfRange:
        return what + " " + range_text(limits) + ", but was given " + given + ".";
    case reflect::SetResult::UnknownChoice:
        return what + " must be " + expected_text(kind, choice) + ", but was given " + given + ".";
    case reflect::SetResult::NotRepresentable:
        return what + " cannot hold " + given + ": it is too large or too small.";
    case reflect::SetResult::ReadOnly:
        return what + " cannot be changed.";
    default:
        return what + " must be " + expected_text(kind, choice) + ", but was given " + given + ".";
    }
}

[[nodiscard]] reflect::SetResult to_set_result(Conversion conversion) noexcept {
    switch (conversion) {
    case Conversion::Ok:
        return reflect::SetResult::Ok;
    case Conversion::UnknownChoice:
        return reflect::SetResult::UnknownChoice;
    case Conversion::NotWhole:
    case Conversion::WrongKind:
        return reflect::SetResult::WrongKind;
    }
    return reflect::SetResult::WrongKind;
}

[[nodiscard]] reflect::SetResult to_set_result(reflect::CallError error) noexcept {
    switch (error) {
    case reflect::CallError::OutOfRange:
        return reflect::SetResult::OutOfRange;
    case reflect::CallError::UnknownChoice:
        return reflect::SetResult::UnknownChoice;
    case reflect::CallError::NotRepresentable:
        return reflect::SetResult::NotRepresentable;
    default:
        return reflect::SetResult::WrongKind;
    }
}

/// Splits "name:12: message" into the line and the message. Leaves the line at 0 if the
/// text does not start with the script's name.
void split_location(std::string_view text, std::string_view script, int& line,
                    std::string& message) {
    line = 0;
    message = std::string(text);
    if (text.size() <= script.size() + 1 || text.substr(0, script.size()) != script ||
        text[script.size()] != ':') {
        return;
    }
    const std::string_view rest = text.substr(script.size() + 1);
    int value = 0;
    const auto [end, ec] = std::from_chars(rest.data(), rest.data() + rest.size(), value);
    if (ec != std::errc{} || end == rest.data()) {
        return;
    }
    std::string_view after(end, static_cast<std::size_t>(rest.data() + rest.size() - end));
    if (after.empty() || after.front() != ':') {
        return;
    }
    after.remove_prefix(1);
    if (!after.empty() && after.front() == ' ') {
        after.remove_prefix(1);
    }
    line = value;
    message = std::string(after);
}

/// The current line of the innermost script function on `thread`'s stack, or 0.
[[nodiscard]] int current_line(lua_State* thread) {
    lua_Debug info{};
    for (int level = 0; lua_getinfo(thread, level, "l", &info) != 0; ++level) {
        if (info.currentline > 0) {
            return info.currentline;
        }
    }
    return 0;
}

} // namespace

// ---------------------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------------------

struct Vm::Impl {
    Impl(const reflect::Registry& registry_in, VmLimits limits_in)
        : registry(registry_in), limits(limits_in) {}

    const reflect::Registry& registry;
    VmLimits limits;
    lua_State* state{nullptr};

    std::size_t memory_used = 0;
    bool enforce_memory = false; ///< Only script execution is limited, not host calls.

    std::uint64_t steps_left = 0; ///< In the current run.
    bool budget_hit = false;      ///< The current run used up its steps.
    std::uint64_t frame_steps = 0;
    std::uint64_t steps_last_update = 0;

    bool busy = false;    ///< A script is running; the Vm refuses re-entrant calls.
    bool collect = false; ///< A task ran out of memory: collect garbage once it is released.
    Task* current{nullptr};

    std::vector<std::unique_ptr<Script>> scripts;
    std::vector<std::unique_ptr<Task>> tasks; ///< In start order.
    std::map<std::string, Binding, std::less<>> bindings;
    Vm::ObjectResolver resolver;
    Vm::GroupResolver groups;

    /// The parts of the object called `name`: its binding, or what the resolver finds.
    /// Empty if there is no such object.
    [[nodiscard]] std::vector<Binding> resolve(std::string_view name) const {
        if (const auto found = bindings.find(name); found != bindings.end()) {
            return {found->second};
        }
        std::vector<Binding> parts;
        if (resolver) {
            for (const reflect::ObjectRef part : resolver(name)) {
                const reflect::TypeInfo* type = part.is_null() ? nullptr : registry.find_type(part);
                if (type != nullptr) {
                    parts.push_back(Binding{part, type});
                }
            }
        }
        return parts;
    }
    std::vector<ScriptError> errors;
    std::function<void(std::string_view, std::string_view)> log;
    std::function<void(std::string_view, int)> trace;
    std::map<std::string, std::vector<Handler>, std::less<>> handlers; ///< By event key.
    std::vector<PendingEvent> pending_events;
    std::uint64_t update_count = 0;
    std::uint32_t next_script = 1;
    std::uint32_t next_task = 1;

    void report(ScriptErrorKind kind, std::string script, int line, std::string message) {
        errors.push_back(ScriptError{kind, std::move(script), line, std::move(message)});
    }

    /// Creates a task on a new thread of `script`, taking the function on top of the
    /// script thread's stack.
    Task& create_task(const Script& script) {
        lua_State* thread = lua_newthread(script.thread);
        const int ref = lua_ref(script.thread, -1);
        lua_pop(script.thread, 1);
        lua_xmove(script.thread, thread, 1);
        auto task = std::make_unique<Task>();
        task->id = TaskId{next_task++};
        task->born = update_count;
        task->script = &script;
        task->thread = thread;
        task->ref = ref;
        tasks.push_back(std::move(task));
        return *tasks.back();
    }

    /// Resumes `task` with `arguments` values on its stack. Returns true if it is waiting.
    bool resume(Task& task, int arguments) {
        busy = true;
        current = &task;
        steps_left = limits.steps_per_run;
        budget_hit = false;
        enforce_memory = true;
        const int status = lua_resume(task.thread, nullptr, arguments);
        enforce_memory = false;
        current = nullptr;
        busy = false;

        if (budget_hit) {
            report_budget(task, status);
            return false;
        }
        if (status == LUA_YIELD) {
            lua_settop(task.thread, 0);
            return true;
        }
        if (status == LUA_OK) {
            return false;
        }
        report_failure(task, status);
        return false;
    }

    void report_budget(const Task& task, int status) {
        int line = 0;
        if (status != LUA_OK && status != LUA_YIELD && lua_type(task.thread, -1) == LUA_TSTRING) {
            std::string ignored;
            split_location(string_at(task.thread, -1), task.script->name, line, ignored);
        }
        if (line == 0) {
            line = current_line(task.thread);
        }
        report(ScriptErrorKind::Budget, task.script->name, line,
               "Stopped because it ran too long without waiting. Check for a loop that never "
               "ends, or add a wait inside it.");
    }

    void report_failure(const Task& task, int status) {
        const std::string& name = task.script->name;
        if (status == LUA_ERRMEM) {
            collect = true;
            report(ScriptErrorKind::Memory, name, current_line(task.thread),
                   "Stopped because the scripts used more than " +
                       format_number(static_cast<double>(limits.memory_bytes) / (1024.0 * 1024.0)) +
                       " MB of memory.");
            return;
        }
        int line = 0;
        std::string message;
        if (lua_type(task.thread, -1) == LUA_TSTRING) {
            split_location(string_at(task.thread, -1), name, line, message);
        } else {
            message = "The script raised an error that is not text.";
        }
        if (line == 0) {
            line = current_line(task.thread);
        }
        report(ScriptErrorKind::Runtime, name, line, std::move(message));
    }

    void release(Task& task) {
        lua_unref(state, task.ref);
        task.ref = LUA_NOREF;
        task.finished = true;
    }

    /// Removes finished tasks. After a memory error, also frees the memory the failed task
    /// held, so that other scripts have room again: scripts cannot collect garbage
    /// themselves, because Luau has no collectgarbage().
    void remove_finished() {
        std::erase_if(tasks, [](const std::unique_ptr<Task>& task) { return task->finished; });
        if (collect) {
            collect = false;
            lua_gc(state, LUA_GCCOLLECT, 0);
        }
    }

    /// Starts every handler of `event` as a task with the payload as arguments.
    void dispatch(const PendingEvent& event) {
        const auto found = handlers.find(event.key);
        if (found == handlers.end()) {
            return;
        }
        // A handler may register more handlers while it runs, so iterate over a copy.
        const std::vector<Handler> list = found->second;
        const int count = static_cast<int>(event.payload.size());
        for (const Handler& handler : list) {
            lua_getref(handler.script->thread, handler.ref);
            Task& task = create_task(*handler.script);
            if (lua_checkstack(task.thread, count) == 0) {
                release(task);
                continue;
            }
            for (std::size_t i = 0; i < event.payload.size(); ++i) {
                push_value(task.thread, event.payload[i], event.choices[i]);
            }
            if (!resume(task, count)) {
                release(task);
            }
        }
        remove_finished();
    }

    /// Drops the handlers a script registered, when its load fails.
    void remove_handlers(const Script& script) {
        for (auto& [key, list] : handlers) {
            std::erase_if(list, [&](const Handler& handler) {
                if (handler.script != &script) {
                    return false;
                }
                lua_unref(state, handler.ref);
                return true;
            });
        }
    }

    [[nodiscard]] Task* find_task(TaskId id) const noexcept {
        for (const auto& task : tasks) {
            if (task->id == id && !task->finished) {
                return task.get();
            }
        }
        return nullptr;
    }

    [[nodiscard]] const Script* find_script(ScriptId id) const noexcept {
        for (const auto& script : scripts) {
            if (script->id == id) {
                return script.get();
            }
        }
        return nullptr;
    }
};

namespace {

[[nodiscard]] Vm::Impl& impl_of(lua_State* state) {
    return *static_cast<Vm::Impl*>(lua_callbacks(state)->userdata);
}

/// Memory for the VM, counted against VmLimits::memory_bytes while scripts run.
void* allocate(void* user, void* block, std::size_t old_size, std::size_t new_size) {
    auto& impl = *static_cast<Vm::Impl*>(user);
    const std::size_t old_counted = block != nullptr ? old_size : 0;
    if (new_size == 0) {
        std::free(block); // NOLINT(cppcoreguidelines-no-malloc): Luau's allocator contract
        impl.memory_used -= old_counted;
        return nullptr;
    }
    if (impl.enforce_memory && new_size > old_counted &&
        impl.memory_used - old_counted + new_size > impl.limits.memory_bytes) {
        return nullptr;
    }
    void* grown = std::realloc(block, new_size); // NOLINT(cppcoreguidelines-no-malloc)
    if (grown == nullptr) {
        return nullptr;
    }
    impl.memory_used = impl.memory_used - old_counted + new_size;
    return grown;
}

/// Called by Luau at safepoints: loop back edges, calls and returns (gc < 0), and during
/// garbage collection (gc >= 0). Counts steps and stops runs that use up their budget.
void interrupt(lua_State* state, int gc) {
    if (gc >= 0) {
        return;
    }
    Vm::Impl& impl = impl_of(state);
    ++impl.frame_steps;
    if (impl.steps_left > 0) {
        --impl.steps_left;
        return;
    }
    // Keep raising at every later safepoint, so that a pcall in the script cannot swallow
    // the stop.
    impl.budget_hit = true;
    lua_rawcheckstack(state, 1);
    luaL_error(state, "%s", budget_message);
}

/// The parts of the object named by the first argument. Raises if there is none.
[[nodiscard]] std::vector<Binding> object_argument(lua_State* state, const char* function) {
    if (lua_type(state, 1) != LUA_TSTRING) {
        raise(state, std::string(function) +
                         " needs an object name as its first value, but was given " +
                         given_text(state, 1) + ".");
    }
    const Vm::Impl& impl = impl_of(state);
    const std::string_view name = string_at(state, 1);
    std::vector<Binding> parts = impl.resolve(name);
    if (parts.empty()) {
        raise(state, "There is no object called " + quote(name) + ".");
    }
    return parts;
}

[[nodiscard]] std::string_view name_argument(lua_State* state, int index, const char* function,
                                             const char* what) {
    if (lua_type(state, index) != LUA_TSTRING) {
        raise(state, std::string(function) + " needs " + what + " name, but was given " +
                         given_text(state, index) + ".");
    }
    return string_at(state, index);
}

/// sx.call(object, function, ...): runs an action, condition or value getter.
int script_call(lua_State* state) {
    const std::vector<Binding> parts = object_argument(state, "sx.call");
    const std::string_view object_name = string_at(state, 1);
    const std::string_view function_id = name_argument(state, 2, "sx.call", "a function");
    const Binding* owner = nullptr;
    const reflect::FunctionInfo* function = nullptr;
    for (const Binding& part : parts) {
        function = part.type->find_function(function_id);
        if (function != nullptr) {
            owner = &part;
            break;
        }
    }
    if (function == nullptr || owner == nullptr) {
        raise(state, quote(object_name) + " has no action, condition or value called " +
                         quote(function_id) + ".");
    }
    const std::string function_name = quote(function->meta.display_name);
    const std::size_t given = static_cast<std::size_t>(lua_gettop(state) - 2);
    const std::size_t needed = function->parameters.size();
    if (given != needed) {
        raise(state, function_name + " needs " + std::to_string(needed) +
                         (needed == 1 ? " value" : " values") + ", but was given " +
                         std::to_string(given) + ".");
    }

    std::vector<reflect::Value> arguments(needed);
    for (std::size_t i = 0; i < needed; ++i) {
        const reflect::ParameterInfo& parameter = function->parameters[i];
        const int index = static_cast<int>(i) + 3;
        const Conversion converted =
            to_value(state, index, parameter.kind, parameter.choice, arguments[i]);
        if (converted != Conversion::Ok) {
            raise(state, misfit_message(quote(parameter.meta.display_name) + " of " + function_name,
                                        parameter.kind, parameter.limits, parameter.choice,
                                        to_set_result(converted), given_text(state, index)));
        }
    }

    const reflect::CallResult result = reflect::call(owner->object, *function, arguments);
    if (!result.ok()) {
        if (result.error == reflect::CallError::WrongType ||
            result.error == reflect::CallError::WrongArgumentCount) {
            raise(state, function_name + " could not be run on " + quote(object_name) + ".");
        }
        const reflect::ParameterInfo& parameter = function->parameters[result.argument];
        raise(state, misfit_message(quote(parameter.meta.display_name) + " of " + function_name,
                                    parameter.kind, parameter.limits, parameter.choice,
                                    to_set_result(result.error),
                                    given_text(state, static_cast<int>(result.argument) + 3)));
    }
    if (result.value) {
        push_value(state, *result.value, function->result_choice);
        return 1;
    }
    return 0;
}

/// The part that has the property named by the second argument, and the property.
/// Raises if no part has it.
[[nodiscard]] std::pair<Binding, const reflect::PropertyInfo*>
property_argument(lua_State* state, const std::vector<Binding>& parts, const char* function) {
    const std::string_view property_id = name_argument(state, 2, function, "a property");
    for (const Binding& part : parts) {
        if (const reflect::PropertyInfo* property = part.type->find_property(property_id)) {
            return {part, property};
        }
    }
    raise(state,
          quote(string_at(state, 1)) + " has no property called " + quote(property_id) + ".");
}

/// sx.get(object, property): reads a property.
int script_get(lua_State* state) {
    const std::vector<Binding> parts = object_argument(state, "sx.get");
    const auto [binding, found] = property_argument(state, parts, "sx.get");
    const reflect::PropertyInfo& property = *found;
    const std::optional<reflect::Value> value = reflect::get_property(binding.object, property);
    if (!value) {
        raise(state, quote(property.meta.display_name) + " could not be read.");
    }
    push_value(state, *value, property.choice);
    return 1;
}

/// sx.set(object, property, value): writes a property.
int script_set(lua_State* state) {
    const std::vector<Binding> parts = object_argument(state, "sx.set");
    const auto [binding, found] = property_argument(state, parts, "sx.set");
    const reflect::PropertyInfo& property = *found;
    const std::string what =
        quote(property.meta.display_name) + " of " + quote(string_at(state, 1));
    if (lua_gettop(state) != 3) {
        raise(state, "sx.set needs an object, a property and one value.");
    }
    reflect::Value value;
    const Conversion converted = to_value(state, 3, property.kind, property.choice, value);
    reflect::SetResult result = to_set_result(converted);
    if (result == reflect::SetResult::Ok) {
        result = reflect::set_property(binding.object, property, value);
    }
    if (result != reflect::SetResult::Ok) {
        raise(state, misfit_message(what, property.kind, property.limits, property.choice, result,
                                    given_text(state, 3)));
    }
    return 0;
}

/// sx.wait(seconds): pauses the task.
int script_wait(lua_State* state) {
    const Vm::Impl& impl = impl_of(state);
    const double seconds = lua_type(state, 1) == LUA_TNUMBER ? lua_tonumber(state, 1) : -1.0;
    if (lua_gettop(state) != 1 || !std::isfinite(seconds) || seconds < 0.0) {
        raise(state, "sx.wait needs one number of seconds, 0 or more, but was given " +
                         given_text(state, 1) + ".");
    }
    Task* task = impl.current;
    if (task == nullptr || task->thread != state || lua_isyieldable(state) == 0) {
        raise(state, "sx.wait can only be used directly in a sequence, not inside a "
                     "coroutine or a callback.");
    }
    task->wait_left = seconds;
    return lua_yield(state, 0);
}

/// sx.log(...) and print(...): sends the values, separated by spaces, to the log handler.
int script_log(lua_State* state) {
    const Vm::Impl& impl = impl_of(state);
    std::string text;
    const int count = lua_gettop(state);
    for (int i = 1; i <= count; ++i) {
        std::size_t length = 0;
        const char* piece = luaL_tolstring(state, i, &length);
        if (i > 1) {
            text += ' ';
        }
        text.append(piece, length);
        lua_pop(state, 1);
    }
    if (impl.log) {
        const std::string_view script =
            impl.current != nullptr ? std::string_view(impl.current->script->name) : "";
        impl.log(script, text);
    }
    return 0;
}

/// sx.on(event, function): runs the function as a new task each time the event is emitted.
int script_on(lua_State* state) {
    Vm::Impl& impl = impl_of(state);
    if (lua_gettop(state) != 2 || lua_type(state, 1) != LUA_TSTRING ||
        lua_type(state, 2) != LUA_TFUNCTION) {
        raise(state, "sx.on needs an event name and a function.");
    }
    if (impl.current == nullptr) {
        raise(state, "sx.on can only be used while a script runs.");
    }
    const std::string key(string_at(state, 1));
    const int ref = lua_ref(state, 2);
    impl.handlers[key].push_back(Handler{impl.current->script, ref});
    return 0;
}

/// sx.tagged(tag): the names of the objects with the tag, as a list.
int script_tagged(lua_State* state) {
    const Vm::Impl& impl = impl_of(state);
    if (lua_gettop(state) != 1 || lua_type(state, 1) != LUA_TSTRING) {
        raise(state, "sx.tagged needs a tag.");
    }
    const std::vector<std::string> names =
        impl.groups ? impl.groups(string_at(state, 1)) : std::vector<std::string>{};
    lua_createtable(state, static_cast<int>(names.size()), 0);
    for (std::size_t i = 0; i < names.size(); ++i) {
        lua_pushlstring(state, names[i].data(), names[i].size());
        lua_rawseti(state, -2, static_cast<int>(i + 1));
    }
    return 1;
}

/// sx.trace(): reports the line it is called from to the trace handler.
int script_trace(lua_State* state) {
    const Vm::Impl& impl = impl_of(state);
    if (impl.trace && impl.current != nullptr) {
        impl.trace(impl.current->script->name, current_line(state));
    }
    return 0;
}

} // namespace

// ---------------------------------------------------------------------------------------
// Vm
// ---------------------------------------------------------------------------------------

Vm::Vm(const reflect::Registry& registry, VmLimits limits)
    : impl_(std::make_unique<Impl>(registry, limits)) {
    lua_State* state = lua_newstate(&allocate, impl_.get());
    if (state == nullptr) {
        return;
    }
    impl_->state = state;
    lua_callbacks(state)->userdata = impl_.get();

    luaL_openlibs(state);
    lua_pushcfunction(state, &script_log, "print");
    lua_setglobal(state, "print");

    constexpr std::array<std::pair<const char*, lua_CFunction>, 8> api{{
        {"call", &script_call},
        {"get", &script_get},
        {"set", &script_set},
        {"wait", &script_wait},
        {"log", &script_log},
        {"on", &script_on},
        {"trace", &script_trace},
        {"tagged", &script_tagged},
    }};
    lua_createtable(state, 0, static_cast<int>(api.size()));
    for (const auto& [name, function] : api) {
        lua_pushcfunction(state, function, name);
        lua_setfield(state, -2, name);
    }
    lua_setglobal(state, "sx");

    // Makes every library table, including sx, read-only.
    luaL_sandbox(state);
    lua_callbacks(state)->interrupt = &interrupt;
}

Vm::~Vm() {
    if (impl_->state != nullptr) {
        lua_close(impl_->state);
    }
}

bool Vm::bind(std::string_view name, reflect::ObjectRef object) {
    if (impl_->busy || !reflect::is_valid_id(name) || object.is_null()) {
        return false;
    }
    const reflect::TypeInfo* type = impl_->registry.find_type(object);
    if (type == nullptr || impl_->bindings.contains(name)) {
        return false;
    }
    impl_->bindings.emplace(std::string(name), Binding{object, type});
    return true;
}

bool Vm::unbind(std::string_view name) {
    if (impl_->busy) {
        return false;
    }
    const auto found = impl_->bindings.find(name);
    if (found == impl_->bindings.end()) {
        return false;
    }
    impl_->bindings.erase(found);
    return true;
}

void Vm::set_object_resolver(ObjectResolver resolver) {
    impl_->resolver = std::move(resolver);
}

void Vm::set_group_resolver(GroupResolver resolver) {
    impl_->groups = std::move(resolver);
}

std::vector<const reflect::TypeInfo*> Vm::object_types(std::string_view name) const {
    std::vector<const reflect::TypeInfo*> types;
    for (const Binding& part : impl_->resolve(name)) {
        types.push_back(part.type);
    }
    return types;
}

std::optional<ScriptId> Vm::load(std::string_view name, std::string_view source) {
    Impl& impl = *impl_;
    if (impl.busy || impl.state == nullptr) {
        return std::nullopt;
    }
    if (!is_valid_script_name(name)) {
        impl.report(ScriptErrorKind::Compile, std::string(name), 0,
                    "A script name must be 1 to 128 letters, digits, '_', '-', '.' or '/'.");
        return std::nullopt;
    }

    lua_CompileOptions options{};
    options.optimizationLevel = 1;
    options.debugLevel = 1; // Line numbers, for errors and the block debug map.
    std::size_t size = 0;
    const std::unique_ptr<char, decltype(&std::free)> bytecode(
        luau_compile(source.data(), source.size(), &options, &size), &std::free);
    if (bytecode == nullptr) {
        impl.report(ScriptErrorKind::Compile, std::string(name), 0,
                    "The script could not be compiled.");
        return std::nullopt;
    }

    auto script = std::make_unique<Script>();
    script->name = std::string(name);
    script->thread = lua_newthread(impl.state);
    script->ref = lua_ref(impl.state, -1);
    lua_pop(impl.state, 1);
    luaL_sandboxthread(script->thread);

    const std::string chunk_name = "=" + script->name;
    if (luau_load(script->thread, chunk_name.c_str(), bytecode.get(), size, 0) != 0) {
        int line = 0;
        std::string message;
        split_location(string_at(script->thread, -1), script->name, line, message);
        impl.report(ScriptErrorKind::Compile, script->name, line, std::move(message));
        lua_unref(impl.state, script->ref);
        return std::nullopt;
    }

    Task& task = impl.create_task(*script);
    const std::size_t errors_before = impl.errors.size();
    const bool waiting = impl.resume(task, 0);
    const bool failed = impl.errors.size() != errors_before;
    if (failed) {
        // The top level failed. No other task of this script can exist yet, so the script
        // and the handlers it registered are dropped.
        impl.remove_handlers(*script);
        lua_unref(impl.state, script->ref);
    }
    if (!waiting) {
        impl.release(task);
        impl.remove_finished();
    }
    if (failed) {
        return std::nullopt;
    }
    script->id = ScriptId{impl.next_script++};
    const ScriptId id = script->id;
    impl.scripts.push_back(std::move(script));
    return id;
}

std::optional<TaskId> Vm::start(ScriptId script_id, std::string_view function,
                                std::span<const reflect::Value> arguments) {
    Impl& impl = *impl_;
    if (impl.busy || impl.state == nullptr) {
        return std::nullopt;
    }
    const Script* script = impl.find_script(script_id);
    if (script == nullptr) {
        return std::nullopt;
    }
    const std::string function_name(function);
    lua_getfield(script->thread, LUA_GLOBALSINDEX, function_name.c_str());
    if (lua_type(script->thread, -1) != LUA_TFUNCTION) {
        lua_pop(script->thread, 1);
        impl.report(ScriptErrorKind::Runtime, script->name, 0,
                    "The script has no function called " + quote(function) + ".");
        return std::nullopt;
    }
    Task& task = impl.create_task(*script);
    if (arguments.size() > 200 ||
        lua_checkstack(task.thread, static_cast<int>(arguments.size())) == 0) {
        impl.release(task);
        impl.remove_finished();
        impl.report(ScriptErrorKind::Runtime, script->name, 0,
                    "Too many values were given to " + quote(function) + ".");
        return std::nullopt;
    }
    for (const reflect::Value& argument : arguments) {
        push_value(task.thread, argument, nullptr);
    }
    const TaskId id = task.id;
    const std::size_t errors_before = impl.errors.size();
    const bool waiting = impl.resume(task, static_cast<int>(arguments.size()));
    if (!waiting) {
        impl.release(task);
        impl.remove_finished();
    }
    if (impl.errors.size() != errors_before) {
        return std::nullopt;
    }
    return id;
}

void Vm::update(double seconds) {
    Impl& impl = *impl_;
    if (impl.busy || impl.state == nullptr) {
        return;
    }
    if (!std::isfinite(seconds) || seconds < 0.0) {
        seconds = 0.0;
    }
    impl.frame_steps = 0;
    ++impl.update_count;
    // Tasks created from here on are born in this update and wait for the next one.
    for (const auto& task : impl.tasks) {
        task->wait_left -= seconds;
    }

    // Events emitted while scripts ran are delivered first (plan/05 §5, step 3). Events
    // they emit in turn wait for the next update, so two rules cannot ping-pong forever
    // within one frame.
    std::vector<PendingEvent> due = std::move(impl.pending_events);
    impl.pending_events.clear();
    std::size_t delivered = 0;
    for (; delivered < due.size(); ++delivered) {
        if (impl.frame_steps >= impl.limits.steps_per_frame) {
            break;
        }
        impl.dispatch(due[delivered]);
    }
    // Events the frame budget did not reach stay first in line, in order.
    impl.pending_events.insert(
        impl.pending_events.begin(),
        std::make_move_iterator(due.begin() + static_cast<std::ptrdiff_t>(delivered)),
        std::make_move_iterator(due.end()));

    // Then every Brain takes at most one transition and runs its state (step 4): scripts
    // in load order, Brains by name. Brains cannot wait, so each run ends here.
    for (const auto& script : impl.scripts) {
        lua_State* thread = script->thread;
        lua_getfield(thread, LUA_GLOBALSINDEX, "_BRAINS");
        std::vector<std::string> brains;
        if (lua_type(thread, -1) == LUA_TTABLE) {
            lua_pushnil(thread);
            while (lua_next(thread, -2) != 0) {
                if (lua_type(thread, -2) == LUA_TSTRING && lua_type(thread, -1) == LUA_TFUNCTION) {
                    brains.emplace_back(string_at(thread, -2));
                }
                lua_pop(thread, 1);
            }
        }
        std::sort(brains.begin(), brains.end());
        for (const std::string& brain : brains) {
            if (impl.frame_steps >= impl.limits.steps_per_frame) {
                break;
            }
            lua_getfield(thread, -1, brain.c_str()); // the Brain's function, from _BRAINS
            Task& task = impl.create_task(*script);
            lua_pushnumber(task.thread, seconds);
            if (!impl.resume(task, 1)) {
                impl.release(task);
            }
        }
        lua_pop(thread, 1); // _BRAINS
    }

    // Then the tasks whose wait is over (step 5). Bound functions cannot call start(), so
    // only dispatch() above adds tasks, and those are skipped as born in this update.
    for (const auto& task : impl.tasks) {
        if (task->finished || task->born == impl.update_count || task->wait_left > wait_epsilon) {
            continue;
        }
        if (impl.frame_steps >= impl.limits.steps_per_frame) {
            break; // The rest resume in the next update, still in order.
        }
        if (!impl.resume(*task, 0)) {
            impl.release(*task);
        }
    }
    impl.steps_last_update = impl.frame_steps;
    impl.remove_finished();
}

bool Vm::emit(std::string_view object, std::string_view event,
              std::span<const reflect::Value> payload) {
    Impl& impl = *impl_;
    if (impl.state == nullptr || !reflect::is_valid_id(event)) {
        return false;
    }
    PendingEvent pending;
    pending.choices.assign(payload.size(), nullptr);
    if (!object.empty()) {
        const reflect::EventInfo* info = nullptr;
        for (const Binding& part : impl.resolve(object)) {
            info = part.type->find_event(event);
            if (info != nullptr) {
                break;
            }
        }
        // A group's events end with the member's name.
        const bool group = object.starts_with('#');
        const std::size_t values = payload.size() - (group && !payload.empty() ? 1U : 0U);
        if (info == nullptr || info->parameters.size() != values ||
            (group && (payload.empty() || !std::holds_alternative<std::string>(payload.back())))) {
            return false;
        }
        for (std::size_t i = 0; i < values; ++i) {
            if (reflect::check_argument(info->parameters[i], payload[i]) !=
                reflect::SetResult::Ok) {
                return false;
            }
            pending.choices[i] = info->parameters[i].choice;
        }
    }
    pending.key = event_key(object, event);
    pending.payload.assign(payload.begin(), payload.end());
    if (impl.busy) {
        impl.pending_events.push_back(std::move(pending));
    } else {
        impl.dispatch(pending);
    }
    return true;
}

std::vector<std::string> Vm::scripts() const {
    std::vector<std::string> names;
    names.reserve(impl_->scripts.size());
    for (const std::unique_ptr<Script>& script : impl_->scripts) {
        names.push_back(script->name);
    }
    return names;
}

namespace {

const Script* script_named(const std::vector<std::unique_ptr<Script>>& scripts,
                           std::string_view name) {
    for (const std::unique_ptr<Script>& script : scripts) {
        if (script->name == name) {
            return script.get();
        }
    }
    return nullptr;
}

/// A script value as a reflected value, if it is one a variable can hold.
std::optional<reflect::Value> variable_value(lua_State* state, int index) {
    switch (lua_type(state, index)) {
    case LUA_TBOOLEAN:
        return reflect::Value{lua_toboolean(state, index) != 0};
    case LUA_TINTEGER:
        return reflect::Value{static_cast<std::int64_t>(lua_tointeger64(state, index, nullptr))};
    case LUA_TNUMBER: {
        const double number = lua_tonumber(state, index);
        // Whole numbers that a double holds exactly come back as whole numbers.
        constexpr double exact = 9007199254740992.0; // 2^53
        if (std::isfinite(number) && std::floor(number) == number && std::abs(number) <= exact) {
            return reflect::Value{static_cast<std::int64_t>(number)};
        }
        return reflect::Value{number};
    }
    case LUA_TSTRING:
        return reflect::Value{std::string(string_at(state, index))};
    default:
        return std::nullopt;
    }
}

} // namespace

std::vector<std::pair<std::string, reflect::Value>> Vm::variables(std::string_view script) const {
    std::vector<std::pair<std::string, reflect::Value>> found;
    const Script* owner = script_named(impl_->scripts, script);
    if (owner == nullptr) {
        return found;
    }
    lua_State* state = owner->thread;
    lua_getfield(state, LUA_GLOBALSINDEX, "_VARS");
    if (lua_type(state, -1) == LUA_TTABLE) {
        lua_pushnil(state);
        while (lua_next(state, -2) != 0) {
            if (lua_type(state, -2) == LUA_TSTRING) {
                if (std::optional<reflect::Value> value = variable_value(state, -1)) {
                    found.emplace_back(std::string(string_at(state, -2)), std::move(*value));
                }
            }
            lua_pop(state, 1);
        }
    }
    lua_pop(state, 1);
    std::sort(found.begin(), found.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    return found;
}

bool Vm::set_variable(std::string_view script, std::string_view name, const reflect::Value& value) {
    if (impl_->busy) {
        return false;
    }
    const Script* owner = script_named(impl_->scripts, script);
    if (owner == nullptr) {
        return false;
    }
    lua_State* state = owner->thread;
    lua_getfield(state, LUA_GLOBALSINDEX, "_VARS");
    if (lua_type(state, -1) != LUA_TTABLE) {
        lua_pop(state, 1);
        return false;
    }
    lua_pushlstring(state, name.data(), name.size());
    lua_rawget(state, -2);
    const std::optional<reflect::Value> current = variable_value(state, -1);
    lua_pop(state, 1);
    const auto numeric = [](const reflect::Value& v) {
        return std::holds_alternative<std::int64_t>(v) || std::holds_alternative<double>(v);
    };
    const bool fits = current.has_value() &&
                      (current->index() == value.index() || (numeric(*current) && numeric(value)));
    if (fits) {
        lua_pushlstring(state, name.data(), name.size());
        push_value(state, value, nullptr);
        lua_rawset(state, -3);
    }
    lua_pop(state, 1);
    return fits;
}

bool Vm::is_running(TaskId task) const noexcept {
    return impl_->find_task(task) != nullptr;
}

bool Vm::stop(TaskId task_id) {
    Impl& impl = *impl_;
    if (impl.busy) {
        return false;
    }
    Task* task = impl.find_task(task_id);
    if (task == nullptr) {
        return false;
    }
    impl.release(*task);
    impl.remove_finished();
    return true;
}

bool Vm::stop_all() {
    Impl& impl = *impl_;
    if (impl.busy) {
        return false;
    }
    for (const std::unique_ptr<Task>& task : impl.tasks) {
        if (!task->finished) {
            impl.release(*task);
        }
    }
    impl.remove_finished();
    impl.pending_events.clear();
    return true;
}

std::size_t Vm::task_count() const noexcept {
    return static_cast<std::size_t>(
        std::count_if(impl_->tasks.begin(), impl_->tasks.end(),
                      [](const std::unique_ptr<Task>& task) { return !task->finished; }));
}

void Vm::set_log_handler(std::function<void(std::string_view, std::string_view)> handler) {
    impl_->log = std::move(handler);
}

void Vm::set_trace_handler(std::function<void(std::string_view, int)> handler) {
    impl_->trace = std::move(handler);
}

const std::vector<ScriptError>& Vm::errors() const noexcept {
    return impl_->errors;
}

void Vm::clear_errors() noexcept {
    impl_->errors.clear();
}

std::size_t Vm::memory_used() const noexcept {
    return impl_->memory_used;
}

std::uint64_t Vm::steps_last_update() const noexcept {
    return impl_->steps_last_update;
}

} // namespace engine::logic
