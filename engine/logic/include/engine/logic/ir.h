#pragma once

// The Logic IR: the typed form every no-code logic asset (Rules, Sequences, later Links and
// Brains) is stored as and compiled from. The editor edits this structure directly, so a
// program that does not type-check can be shown and fixed but is never run.
//
// See docs/dev/logic-runtime.md §7 for the compiler and the debug map.

#include "engine/foundation/reflect.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine::logic {

/// Identifies one block, so that errors, live highlighting and breakpoints can point at
/// it. Unique within a logic asset. 0 means "no id" and is allowed only on expressions.
struct BlockId {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool is_valid() const noexcept { return value != 0; }
    friend constexpr bool operator==(BlockId, BlockId) = default;
};

// ---------------------------------------------------------------------------------------
// Expressions: the values that fill a block's slots
// ---------------------------------------------------------------------------------------

enum class ExprKind : std::uint8_t {
    Literal,    ///< A constant: `literal`.
    Property,   ///< A property of an object: `object`, `member`.
    Call,       ///< A condition or value getter of an object: `object`, `member`, `operands`.
    Variable,   ///< A variable of the asset: `member`.
    EventValue, ///< A value the rule's event carries: `member` is the parameter id.
    Not,        ///< "not": one operand.
    All,        ///< "and": two or more operands.
    Any,        ///< "or": two or more operands.
    Compare,    ///< Two operands compared with `compare`.
    Arithmetic, ///< Two numbers combined with `arithmetic`.
};

enum class CompareOp : std::uint8_t { Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual };
enum class ArithmeticOp : std::uint8_t { Add, Subtract, Multiply, Divide };

struct Expr {
    ExprKind kind = ExprKind::Literal;
    BlockId id; ///< Optional; lets a condition be highlighted and explained on its own.
    /// For a choice slot, a literal may be the entry id as text ("north") or its value.
    reflect::Value literal;
    std::string object;
    std::string member;
    CompareOp compare = CompareOp::Equal;
    ArithmeticOp arithmetic = ArithmeticOp::Add;
    std::vector<Expr> operands;

    friend bool operator==(const Expr&, const Expr&) = default;
};

// ---------------------------------------------------------------------------------------
// Statements: the "Do" blocks and steps
// ---------------------------------------------------------------------------------------

enum class StmtKind : std::uint8_t {
    Do,          ///< Run action `member` of `object` with `arguments`.
    Set,         ///< Set property `member` of `object` to `arguments[0]`.
    SetVariable, ///< Set variable `member` to `arguments[0]`.
    Wait,        ///< Wait `arguments[0]` seconds.
    If,          ///< If `arguments[0]`, run `body`, otherwise run `otherwise`.
    Repeat,      ///< Run `body` `arguments[0]` times.
    Run,         ///< Run sequence `member` and wait until it ends.
    Log,         ///< Write `arguments[0]` to the log.
    WaitUntil,   ///< Wait until condition `arguments[0]` holds, checked once per update.
    /// Run `body` once for each object tagged `member` ("enemy"), in turn: inside it, the
    /// object "it" is that one.
    ForEachTagged,
};

struct Stmt {
    StmtKind kind = StmtKind::Do;
    BlockId id;
    std::string object;
    std::string member;
    std::vector<Expr> arguments;
    std::vector<Stmt> body;
    std::vector<Stmt> otherwise;

    friend bool operator==(const Stmt&, const Stmt&) = default;
};

// ---------------------------------------------------------------------------------------
// Assets
// ---------------------------------------------------------------------------------------

/// A variable shared by everything in one logic asset.
struct Variable {
    std::string id;
    reflect::ValueKind kind = reflect::ValueKind::Int;
    std::string choice_list; ///< The registered choice list, when kind is Choice.
    /// Must fit the kind; for choices, an entry id or value. Empty means no, 0, empty text
    /// or the first entry of the choice list.
    std::optional<reflect::Value> initial;

    friend bool operator==(const Variable&, const Variable&) = default;
};

/// A list of steps that can wait (layer 4).
struct Sequence {
    BlockId id;
    std::string name; ///< An id: how other blocks and the host start it.
    std::vector<Stmt> steps;

    friend bool operator==(const Sequence&, const Sequence&) = default;
};

/// "When [event], if [conditions], do [actions], otherwise [actions]" (layer 3).
struct Rule {
    BlockId id;
    /// The object whose event this is, for example "chest". Empty for game-wide events
    /// such as "game_start", which carry no values.
    std::string object;
    std::string event;
    std::vector<Expr> conditions; ///< All must be true.
    std::vector<Stmt> actions;
    std::vector<Stmt> otherwise;

    friend bool operator==(const Rule&, const Rule&) = default;
};

/// A way out of a Brain state: taken when every condition holds (layer 5).
struct Transition {
    BlockId id;
    std::string to;               ///< The state it leads to.
    std::vector<Expr> conditions; ///< All must hold; with none, only the time counts.
    double min_time = 0.0;        ///< Seconds in the state before it may be taken.

    friend bool operator==(const Transition&, const Transition&) = default;
};

/// One state of a Brain: steps on entering, on every update while in it, and on leaving,
/// and the transitions out, the first that holds winning. The steps cannot wait.
struct BrainState {
    BlockId id;
    std::string name; ///< An id, unique in its Brain.
    std::vector<Stmt> on_enter;
    std::vector<Stmt> while_in;
    std::vector<Stmt> on_exit;
    std::vector<Transition> transitions; ///< In priority order.

    friend bool operator==(const BrainState&, const BrainState&) = default;
};

/// A state machine (layer 5): for characters, enemy AI, bosses and game flow.
struct Brain {
    BlockId id;
    std::string name;    ///< An id, unique in its asset.
    std::string initial; ///< The state it starts in; empty for the first.
    std::vector<BrainState> states;
    /// "From any state" transitions, such as "HP at most 0: Dead", checked before the
    /// current state's own; never taken towards the state the Brain is already in.
    std::vector<Transition> from_any;

    friend bool operator==(const Brain&, const Brain&) = default;
};

/// One logic asset: what one file in a project holds, and what compiles to one script.
struct LogicAsset {
    std::string name; ///< The script name, as passed to Vm::load().
    std::vector<Variable> variables;
    std::vector<Sequence> sequences;
    std::vector<Rule> rules; ///< In the order they run for the same event.
    std::vector<Brain> brains;

    friend bool operator==(const LogicAsset&, const LogicAsset&) = default;
};

// ---------------------------------------------------------------------------------------
// Builders, for code and tests that create IR by hand
// ---------------------------------------------------------------------------------------

namespace ir {

[[nodiscard]] Expr literal(reflect::Value value);
[[nodiscard]] Expr property(std::string object, std::string property_id);
[[nodiscard]] Expr call(std::string object, std::string function_id,
                        std::vector<Expr> arguments = {});
[[nodiscard]] Expr variable(std::string variable_id);
[[nodiscard]] Expr event_value(std::string parameter_id);
[[nodiscard]] Expr negate(Expr operand);
[[nodiscard]] Expr all(std::vector<Expr> operands);
[[nodiscard]] Expr any(std::vector<Expr> operands);
[[nodiscard]] Expr compare(CompareOp op, Expr left, Expr right);
[[nodiscard]] Expr arithmetic(ArithmeticOp op, Expr left, Expr right);
/// Gives an expression a block id.
[[nodiscard]] Expr with_id(Expr expr, std::uint64_t id);

[[nodiscard]] Stmt do_action(std::uint64_t id, std::string object, std::string function_id,
                             std::vector<Expr> arguments = {});
[[nodiscard]] Stmt set(std::uint64_t id, std::string object, std::string property_id, Expr value);
[[nodiscard]] Stmt set_variable(std::uint64_t id, std::string variable_id, Expr value);
[[nodiscard]] Stmt wait(std::uint64_t id, Expr seconds);
[[nodiscard]] Stmt wait_until(std::uint64_t id, Expr condition);
[[nodiscard]] Stmt if_then(std::uint64_t id, Expr condition, std::vector<Stmt> body,
                           std::vector<Stmt> otherwise = {});
[[nodiscard]] Stmt repeat(std::uint64_t id, Expr count, std::vector<Stmt> body);
[[nodiscard]] Stmt run(std::uint64_t id, std::string sequence_name);
[[nodiscard]] Stmt log(std::uint64_t id, Expr value);
[[nodiscard]] Stmt for_each_tagged(std::uint64_t id, std::string tag, std::vector<Stmt> body);

} // namespace ir

} // namespace engine::logic
