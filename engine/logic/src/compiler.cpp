#include "engine/logic/compiler.h"

#include <array>
#include <charconv>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <system_error>
#include <utility>

#include "common.h"

namespace engine::logic {

// ---------------------------------------------------------------------------------------
// Builders
// ---------------------------------------------------------------------------------------

namespace ir {

Expr literal(reflect::Value value) {
    Expr expr;
    expr.kind = ExprKind::Literal;
    expr.literal = std::move(value);
    return expr;
}

Expr property(std::string object, std::string property_id) {
    Expr expr;
    expr.kind = ExprKind::Property;
    expr.object = std::move(object);
    expr.member = std::move(property_id);
    return expr;
}

Expr call(std::string object, std::string function_id, std::vector<Expr> arguments) {
    Expr expr;
    expr.kind = ExprKind::Call;
    expr.object = std::move(object);
    expr.member = std::move(function_id);
    expr.operands = std::move(arguments);
    return expr;
}

Expr variable(std::string variable_id) {
    Expr expr;
    expr.kind = ExprKind::Variable;
    expr.member = std::move(variable_id);
    return expr;
}

Expr event_value(std::string parameter_id) {
    Expr expr;
    expr.kind = ExprKind::EventValue;
    expr.member = std::move(parameter_id);
    return expr;
}

Expr negate(Expr operand) {
    Expr expr;
    expr.kind = ExprKind::Not;
    expr.operands.push_back(std::move(operand));
    return expr;
}

Expr all(std::vector<Expr> operands) {
    Expr expr;
    expr.kind = ExprKind::All;
    expr.operands = std::move(operands);
    return expr;
}

Expr any(std::vector<Expr> operands) {
    Expr expr;
    expr.kind = ExprKind::Any;
    expr.operands = std::move(operands);
    return expr;
}

Expr compare(CompareOp op, Expr left, Expr right) {
    Expr expr;
    expr.kind = ExprKind::Compare;
    expr.compare = op;
    expr.operands.push_back(std::move(left));
    expr.operands.push_back(std::move(right));
    return expr;
}

Expr arithmetic(ArithmeticOp op, Expr left, Expr right) {
    Expr expr;
    expr.kind = ExprKind::Arithmetic;
    expr.arithmetic = op;
    expr.operands.push_back(std::move(left));
    expr.operands.push_back(std::move(right));
    return expr;
}

Expr with_id(Expr expr, std::uint64_t id) {
    expr.id = BlockId{id};
    return expr;
}

namespace {

Stmt statement(StmtKind kind, std::uint64_t id) {
    Stmt stmt;
    stmt.kind = kind;
    stmt.id = BlockId{id};
    return stmt;
}

} // namespace

Stmt do_action(std::uint64_t id, std::string object, std::string function_id,
               std::vector<Expr> arguments) {
    Stmt stmt = statement(StmtKind::Do, id);
    stmt.object = std::move(object);
    stmt.member = std::move(function_id);
    stmt.arguments = std::move(arguments);
    return stmt;
}

Stmt set(std::uint64_t id, std::string object, std::string property_id, Expr value) {
    Stmt stmt = statement(StmtKind::Set, id);
    stmt.object = std::move(object);
    stmt.member = std::move(property_id);
    stmt.arguments.push_back(std::move(value));
    return stmt;
}

Stmt set_variable(std::uint64_t id, std::string variable_id, Expr value) {
    Stmt stmt = statement(StmtKind::SetVariable, id);
    stmt.member = std::move(variable_id);
    stmt.arguments.push_back(std::move(value));
    return stmt;
}

Stmt wait(std::uint64_t id, Expr seconds) {
    Stmt stmt = statement(StmtKind::Wait, id);
    stmt.arguments.push_back(std::move(seconds));
    return stmt;
}

Stmt wait_until(std::uint64_t id, Expr condition) {
    Stmt stmt = statement(StmtKind::WaitUntil, id);
    stmt.arguments.push_back(std::move(condition));
    return stmt;
}

Stmt if_then(std::uint64_t id, Expr condition, std::vector<Stmt> body,
             std::vector<Stmt> otherwise) {
    Stmt stmt = statement(StmtKind::If, id);
    stmt.arguments.push_back(std::move(condition));
    stmt.body = std::move(body);
    stmt.otherwise = std::move(otherwise);
    return stmt;
}

Stmt repeat(std::uint64_t id, Expr count, std::vector<Stmt> body) {
    Stmt stmt = statement(StmtKind::Repeat, id);
    stmt.arguments.push_back(std::move(count));
    stmt.body = std::move(body);
    return stmt;
}

Stmt for_each_tagged(std::uint64_t id, std::string tag, std::vector<Stmt> body) {
    Stmt stmt = statement(StmtKind::ForEachTagged, id);
    stmt.member = std::move(tag);
    stmt.body = std::move(body);
    return stmt;
}

Stmt run(std::uint64_t id, std::string sequence_name) {
    Stmt stmt = statement(StmtKind::Run, id);
    stmt.member = std::move(sequence_name);
    return stmt;
}

Stmt log(std::uint64_t id, Expr value) {
    Stmt stmt = statement(StmtKind::Log, id);
    stmt.arguments.push_back(std::move(value));
    return stmt;
}

} // namespace ir

// ---------------------------------------------------------------------------------------
// Compiler
// ---------------------------------------------------------------------------------------

namespace {

using common::expected_text;
using common::format_number;
using common::quote;
using common::range_text;

/// Deeper nesting than this is refused. It keeps the compiler's recursion bounded for any
/// input, and stays below the Luau compiler's own limits.
constexpr int max_depth = 64;

/// In a Rule about a tag ("When an object tagged enemy is touched"), the name of the tagged
/// object whose event it is.
constexpr std::string_view it_name = "it";

/// In a Rule whose event names another object (what touched, interacted or walked in), that
/// object: the event's value of this name.
constexpr std::string_view who_name = "who";

/// What a slot accepts. Literals are also checked against `limits`.
struct Slot {
    reflect::ValueKind kind = reflect::ValueKind::Bool;
    const reflect::ChoiceInfo* choice{nullptr};
    reflect::NumericLimits limits;
};

/// The static type of an expression.
struct Type {
    reflect::ValueKind kind = reflect::ValueKind::Bool;
    const reflect::ChoiceInfo* choice{nullptr};
};

[[nodiscard]] bool is_numeric(reflect::ValueKind kind) noexcept {
    return kind == reflect::ValueKind::Int || kind == reflect::ValueKind::Float;
}

/// A Luau string literal holding exactly `text`. Every byte that is not printable ASCII is
/// written as a decimal escape, so user text can never end the string, start a comment or
/// add a line (which would also shift the debug map).
[[nodiscard]] std::string lua_string(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (byte >= 0x20 && byte < 0x7F) {
            out += c;
        } else {
            // Always three digits, so a digit that follows cannot extend the escape.
            std::array<char, 5> escape{'\\', static_cast<char>('0' + byte / 100),
                                       static_cast<char>('0' + (byte / 10) % 10),
                                       static_cast<char>('0' + byte % 10), '\0'};
            out += escape.data();
        }
    }
    out += '"';
    return out;
}

/// A Luau number literal. Negative numbers are parenthesised, so that "x - -1" can never
/// turn into a "--" comment.
[[nodiscard]] std::string lua_number(double value) {
    std::array<char, 64> buffer{};
    const auto [end, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    const std::string digits = ec == std::errc{} ? std::string(buffer.data(), end) : "0";
    return value < 0 ? "(" + digits + ")" : digits;
}

[[nodiscard]] std::string lua_integer(std::int64_t value) {
    const std::string digits = std::to_string(value);
    return value < 0 ? "(" + digits + ")" : digits;
}

/// How a literal value reads in a message.
[[nodiscard]] std::string describe(const reflect::Value& value) {
    if (const bool* flag = std::get_if<bool>(&value)) {
        return *flag ? "yes" : "no";
    }
    if (const std::int64_t* number = std::get_if<std::int64_t>(&value)) {
        return std::to_string(*number);
    }
    if (const double* real = std::get_if<double>(&value)) {
        return format_number(*real);
    }
    return quote(std::get<std::string>(value));
}

/// The value a variable starts with when none is given. Empty for an empty choice list.
[[nodiscard]] std::optional<reflect::Value> zero_value(reflect::ValueKind kind,
                                                       const reflect::ChoiceInfo* choice) {
    switch (kind) {
    case reflect::ValueKind::Bool:
        return reflect::Value{false};
    case reflect::ValueKind::Int:
        return reflect::Value{std::int64_t{0}};
    case reflect::ValueKind::Float:
        return reflect::Value{0.0};
    case reflect::ValueKind::Text:
        return reflect::Value{std::string()};
    case reflect::ValueKind::Choice:
        if (choice == nullptr || choice->entries.empty()) {
            return std::nullopt;
        }
        return reflect::Value{choice->entries.front().value};
    }
    return std::nullopt;
}

[[nodiscard]] Type literal_type(const reflect::Value& value) {
    switch (value.index()) {
    case 0:
        return Type{reflect::ValueKind::Bool, nullptr};
    case 1:
        return Type{reflect::ValueKind::Int, nullptr};
    case 2:
        return Type{reflect::ValueKind::Float, nullptr};
    default:
        return Type{reflect::ValueKind::Text, nullptr};
    }
}

[[nodiscard]] std::string compare_symbol(CompareOp op) {
    switch (op) {
    case CompareOp::Equal:
        return "==";
    case CompareOp::NotEqual:
        return "~=";
    case CompareOp::Less:
        return "<";
    case CompareOp::LessEqual:
        return "<=";
    case CompareOp::Greater:
        return ">";
    case CompareOp::GreaterEqual:
        return ">=";
    }
    return "==";
}

[[nodiscard]] std::string arithmetic_symbol(ArithmeticOp op) {
    switch (op) {
    case ArithmeticOp::Add:
        return "+";
    case ArithmeticOp::Subtract:
        return "-";
    case ArithmeticOp::Multiply:
        return "*";
    case ArithmeticOp::Divide:
        return "/";
    }
    return "+";
}

/// An object as the compiler sees it: the types of all its parts (an entity has one per
/// component). Members are looked up across all of them, in order.
struct Object {
    std::vector<const reflect::TypeInfo*> types;

    [[nodiscard]] const reflect::PropertyInfo* find_property(std::string_view id) const {
        for (const reflect::TypeInfo* type : types) {
            if (const reflect::PropertyInfo* found = type->find_property(id)) {
                return found;
            }
        }
        return nullptr;
    }
    [[nodiscard]] const reflect::FunctionInfo* find_function(std::string_view id) const {
        for (const reflect::TypeInfo* type : types) {
            if (const reflect::FunctionInfo* found = type->find_function(id)) {
                return found;
            }
        }
        return nullptr;
    }
    [[nodiscard]] const reflect::EventInfo* find_event(std::string_view id) const {
        for (const reflect::TypeInfo* type : types) {
            if (const reflect::EventInfo* found = type->find_event(id)) {
                return found;
            }
        }
        return nullptr;
    }
};

struct VariableInfo {
    const Variable* variable{nullptr};
    const reflect::ChoiceInfo* choice{nullptr};
};

class Compiler {
public:
    Compiler(const LogicAsset& asset, const reflect::Registry& registry, const ObjectTypes& objects,
             CompileOptions options)
        : asset_(asset), registry_(registry), objects_(objects), options_(options) {}

    CompiledLogic run() {
        check_names_and_ids();
        // The variables are also a script-private global, so saved games can read and
        // restore them (Vm::variables()). On one line, so line numbers stay put.
        // _BRAINS lists each Brain's update function, for Vm::update() to call.
        emit(BlockId{}, "local vars = {}; _VARS = vars; _BRAINS = {}");
        variables();
        for (const Sequence& sequence : asset_.sequences) {
            compile_sequence(sequence);
        }
        for (const Rule& rule : asset_.rules) {
            compile_rule(rule);
        }
        for (const Brain& brain : asset_.brains) {
            compile_brain(brain);
        }

        CompiledLogic result;
        result.errors = std::move(errors_);
        if (result.errors.empty()) {
            for (const std::string& line : lines_) {
                result.source += line;
                result.source += '\n';
            }
            result.line_blocks = std::move(line_blocks_);
        }
        return result;
    }

private:
    // --- Output -------------------------------------------------------------------------

    void error(BlockId block, std::string message) {
        errors_.push_back(Diagnostic{block, std::move(message)});
    }

    void emit(BlockId block, const std::string& code) {
        lines_.push_back(std::string(static_cast<std::size_t>(indent_) * 2, ' ') + code);
        line_blocks_.push_back(block);
    }

    [[nodiscard]] std::string trace() const { return options_.trace ? "sx.trace() " : ""; }

    // --- Lookups ------------------------------------------------------------------------

    /// The types an object is made of, or nothing after reporting that it does not exist.
    /// Which of the Rule's event values is "who", the object the event names; none outside
    /// a Rule, or if its event names no object.
    [[nodiscard]] std::optional<std::size_t> who_parameter() const {
        if (event_ == nullptr) {
            return std::nullopt;
        }
        for (std::size_t i = 0; i < event_->parameters.size(); ++i) {
            if (event_->parameters[i].id == who_name &&
                event_->parameters[i].kind == reflect::ValueKind::Text) {
                return i;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Object> object_type(BlockId block, const std::string& object) {
        // "who": the object the Rule's event names. It could be any, so anything a part
        // can do is accepted here; what that object lacks is reported when the step runs.
        if (object == who_name) {
            if (!who_parameter()) {
                error(block, "\"who\" is the object an event names, such as what touched or "
                             "interacted; this Rule's event names none.");
                return std::nullopt;
            }
            return Object{registry_.types()};
        }
        // "it": in a Rule about a tag ("#enemy"), the tagged object whose event it is.
        if (object == it_name && it_object_.empty()) {
            error(block, "\"it\" is the object in a Rule about a tag, such as \"When an object "
                         "tagged enemy is touched\"; here it means nothing.");
            return std::nullopt;
        }
        const std::string& name = object == it_name ? it_object_ : object;
        if (object.starts_with('#') && object != rule_object_) {
            error(block, "A step cannot act on every object tagged " + quote(object.substr(1)) +
                             " at once; a Rule about the tag acts on \"it\", the one whose "
                             "event it is.");
            return std::nullopt;
        }
        Object found;
        if (objects_) {
            found.types = objects_(name);
            std::erase(found.types, nullptr);
        }
        if (found.types.empty()) {
            error(block, name.starts_with('#')
                             ? "No object is tagged " + quote(name.substr(1)) + "."
                             : "There is no object called " + quote(name) + ".");
            return std::nullopt;
        }
        return found;
    }

    /// The Lua for naming `object`: its name as a string, or, for "it" in a Rule about a
    /// tag, the handler's own `it` (the GUID of the object whose event it is), which stays
    /// right across waits.
    [[nodiscard]] std::string object_code(const std::string& object) const {
        if (object == who_name) {
            if (const std::optional<std::size_t> who = who_parameter()) {
                return "p" + std::to_string(*who + 1);
            }
        }
        return object == it_name && !it_object_.empty() ? std::string("it") : lua_string(object);
    }

    // --- Asset-level checks -------------------------------------------------------------

    void check_names_and_ids() {
        if (!common::is_valid_script_name(asset_.name)) {
            error(BlockId{}, "The logic asset's name " + quote(asset_.name) +
                                 " is not valid: use 1 to 128 letters, digits, '_', '-', '.' "
                                 "or '/'.");
        }
        for (const Sequence& sequence : asset_.sequences) {
            claim_id(sequence.id, true);
            if (!reflect::is_valid_id(sequence.name)) {
                error(sequence.id, "The sequence name " + quote(sequence.name) +
                                       " is not valid: use lowercase letters, digits and "
                                       "underscores, starting with a letter.");
            } else if (!sequences_.insert(sequence.name).second) {
                error(sequence.id,
                      "There is already a sequence called " + quote(sequence.name) + ".");
            }
            claim_ids(sequence.steps);
        }
        for (const Rule& rule : asset_.rules) {
            claim_id(rule.id, true);
            for (const Expr& condition : rule.conditions) {
                claim_ids(condition);
            }
            claim_ids(rule.actions);
            claim_ids(rule.otherwise);
        }
        std::set<std::string, std::less<>> brains;
        for (const Brain& brain : asset_.brains) {
            claim_id(brain.id, true);
            if (!reflect::is_valid_id(brain.name)) {
                error(brain.id, "The brain name " + quote(brain.name) +
                                    " is not valid: use lowercase letters, digits and "
                                    "underscores, starting with a letter.");
            } else if (!brains.insert(brain.name).second) {
                error(brain.id, "There is already a brain called " + quote(brain.name) + ".");
            }
            if (brain.states.empty()) {
                error(brain.id, "The brain " + quote(brain.name) + " has no states.");
            }
            std::set<std::string, std::less<>> states;
            for (const BrainState& state : brain.states) {
                claim_id(state.id, true);
                if (!reflect::is_valid_id(state.name)) {
                    error(state.id, "The state name " + quote(state.name) +
                                        " is not valid: use lowercase letters, digits and "
                                        "underscores, starting with a letter.");
                } else if (!states.insert(state.name).second) {
                    error(state.id, "There is already a state called " + quote(state.name) + ".");
                }
            }
            if (!brain.initial.empty() && !states.contains(brain.initial)) {
                error(brain.id, "The brain starts in " + quote(brain.initial) +
                                    ", which is not one of its states.");
            }
            const auto check_transition = [&](const Transition& transition) {
                claim_id(transition.id, true);
                for (const Expr& condition : transition.conditions) {
                    claim_ids(condition);
                }
                if (!states.contains(transition.to)) {
                    error(transition.id,
                          "There is no state called " + quote(transition.to) + " to go to.");
                }
                if (!std::isfinite(transition.min_time) || transition.min_time < 0.0) {
                    error(transition.id,
                          "The time in the state before leaving must be 0 seconds or more.");
                }
            };
            for (const Transition& transition : brain.from_any) {
                check_transition(transition);
            }
            for (const BrainState& state : brain.states) {
                for (const Transition& transition : state.transitions) {
                    check_transition(transition);
                }
                for (const std::vector<Stmt>* section :
                     {&state.on_enter, &state.while_in, &state.on_exit}) {
                    claim_ids(*section);
                    no_waiting(*section, 0);
                }
            }
        }
    }

    /// Brains run to the end in every update, so their steps cannot wait, directly or by
    /// running a sequence (which may wait).
    void no_waiting(const std::vector<Stmt>& list, int depth) {
        if (depth > max_depth) {
            return; // reported when compiled
        }
        for (const Stmt& stmt : list) {
            if (stmt.kind == StmtKind::Wait || stmt.kind == StmtKind::WaitUntil) {
                error(stmt.id, "A brain cannot wait. Start a sequence from a rule instead.");
            } else if (stmt.kind == StmtKind::Run) {
                error(stmt.id, "A brain cannot run a sequence, which may wait. Start it from a "
                               "rule instead.");
            }
            no_waiting(stmt.body, depth + 1);
            no_waiting(stmt.otherwise, depth + 1);
        }
    }

    void claim_id(BlockId id, bool required) {
        if (!id.is_valid()) {
            if (required) {
                error(id, "A block has no id. Every statement, sequence and rule needs one.");
            }
            return;
        }
        if (!ids_.insert(id.value).second) {
            error(id, "Two blocks share the id " + std::to_string(id.value) + ".");
        }
    }

    // Blocks nested deeper than max_depth are not visited: compiling them reports the
    // nesting, and the recursion here must stay bounded too.
    void claim_ids(const Expr& expr, int depth = 0) {
        if (depth > max_depth) {
            return;
        }
        claim_id(expr.id, false);
        for (const Expr& operand : expr.operands) {
            claim_ids(operand, depth + 1);
        }
    }

    void claim_ids(const std::vector<Stmt>& statements, int depth = 0) {
        if (depth > max_depth) {
            return;
        }
        for (const Stmt& stmt : statements) {
            claim_id(stmt.id, true);
            for (const Expr& argument : stmt.arguments) {
                claim_ids(argument);
            }
            claim_ids(stmt.body, depth + 1);
            claim_ids(stmt.otherwise, depth + 1);
        }
    }

    void variables() {
        for (const Variable& variable : asset_.variables) {
            if (!reflect::is_valid_id(variable.id)) {
                error(BlockId{}, "The variable name " + quote(variable.id) +
                                     " is not valid: use lowercase letters, digits and "
                                     "underscores, starting with a letter.");
                continue;
            }
            if (variables_.contains(variable.id)) {
                error(BlockId{}, "There is already a variable called " + quote(variable.id) + ".");
                continue;
            }
            VariableInfo info{&variable, nullptr};
            if (variable.kind == reflect::ValueKind::Choice) {
                info.choice = registry_.find_choice_list(variable.choice_list);
                if (info.choice == nullptr) {
                    error(BlockId{}, "The variable " + quote(variable.id) +
                                         " uses the choice list " + quote(variable.choice_list) +
                                         ", which does not exist.");
                    continue;
                }
            }
            variables_.emplace(variable.id, info);
            const Slot slot{variable.kind, info.choice, {}};
            const std::optional<reflect::Value> start =
                variable.initial ? variable.initial : zero_value(variable.kind, info.choice);
            if (!start) {
                error(BlockId{}, "The variable " + quote(variable.id) +
                                     " needs a starting value: its choice list is empty.");
                continue;
            }
            const std::optional<std::string> initial = literal_code(
                BlockId{}, *start, slot, "The starting value of " + quote(variable.id));
            if (initial) {
                emit(BlockId{}, "vars[" + lua_string(variable.id) + "] = " + *initial);
            }
        }
    }

    // --- Expressions --------------------------------------------------------------------

    /// The code for a literal that must fit `slot`, or nothing after reporting why not.
    std::optional<std::string> literal_code(BlockId block, const reflect::Value& value,
                                            const Slot& slot, const std::string& what) {
        reflect::Value converted = value;
        const std::string mismatch = what + " must be " + expected_text(slot.kind, slot.choice) +
                                     ", but is " + describe(value) + ".";
        if (slot.kind == reflect::ValueKind::Choice) {
            const reflect::ChoiceEntry* entry = nullptr;
            if (const std::string* id = std::get_if<std::string>(&value)) {
                entry = slot.choice != nullptr ? slot.choice->find_entry(*id) : nullptr;
            } else if (const std::int64_t* number = std::get_if<std::int64_t>(&value)) {
                entry = slot.choice != nullptr ? slot.choice->find_entry(*number) : nullptr;
            }
            if (entry == nullptr) {
                error(block, mismatch);
                return std::nullopt;
            }
            return lua_string(entry->id);
        }
        if (slot.kind == reflect::ValueKind::Float) {
            if (const std::int64_t* number = std::get_if<std::int64_t>(&value)) {
                converted = static_cast<double>(*number);
            }
        }
        if (const double* real = std::get_if<double>(&converted);
            real != nullptr && !std::isfinite(*real)) {
            error(block, what + " must be a finite number, but is " + describe(value) + ".");
            return std::nullopt;
        }
        reflect::ParameterInfo spec;
        spec.kind = slot.kind;
        spec.limits = slot.limits;
        switch (reflect::check_argument(spec, converted)) {
        case reflect::SetResult::Ok:
            break;
        case reflect::SetResult::OutOfRange:
            error(block,
                  what + " " + range_text(slot.limits) + ", but is " + describe(value) + ".");
            return std::nullopt;
        default:
            error(block, mismatch);
            return std::nullopt;
        }
        if (const bool* flag = std::get_if<bool>(&converted)) {
            return *flag ? "true" : "false";
        }
        if (const std::int64_t* number = std::get_if<std::int64_t>(&converted)) {
            return lua_integer(*number);
        }
        if (const double* real = std::get_if<double>(&converted)) {
            return lua_number(*real);
        }
        return lua_string(std::get<std::string>(converted));
    }

    /// The code for `expr` placed in `slot`, or nothing after reporting why not. `what`
    /// names the slot for messages, for example "'Amount' of 'Heal'".
    std::optional<std::string> slot_code(BlockId block, const Expr& expr, const Slot& slot,
                                         const std::string& what) {
        const BlockId where = expr.id.is_valid() ? expr.id : block;
        if (expr.kind == ExprKind::Literal) {
            return literal_code(where, expr.literal, slot, what);
        }
        Type type;
        std::optional<std::string> code = expr_code(block, expr, type);
        if (!code) {
            return std::nullopt;
        }
        const bool fits =
            (type.kind == slot.kind &&
             (slot.kind != reflect::ValueKind::Choice || type.choice == slot.choice)) ||
            (slot.kind == reflect::ValueKind::Float && type.kind == reflect::ValueKind::Int);
        if (!fits) {
            error(where, what + " must be " + expected_text(slot.kind, slot.choice) +
                             ", but the value given is " + expected_text(type.kind, type.choice) +
                             ".");
            return std::nullopt;
        }
        return code;
    }

    /// The code and type of `expr`, or nothing after reporting why it is invalid.
    std::optional<std::string> expr_code(BlockId block, const Expr& expr, Type& type) {
        const BlockId where = expr.id.is_valid() ? expr.id : block;
        if (depth_ >= max_depth) {
            error(where, "This block is nested too deeply.");
            return std::nullopt;
        }
        ++depth_;
        std::optional<std::string> code = expr_code_inner(where, expr, type);
        --depth_;
        return code;
    }

    std::optional<std::string> expr_code_inner(BlockId where, const Expr& expr, Type& type) {
        switch (expr.kind) {
        case ExprKind::Literal: {
            type = literal_type(expr.literal);
            const Slot slot{type.kind, nullptr, {}};
            return literal_code(where, expr.literal, slot, "This value");
        }
        case ExprKind::Property:
            return property_code(where, expr, type);
        case ExprKind::Call:
            return call_code(where, expr, type);
        case ExprKind::Variable: {
            const auto found = variables_.find(expr.member);
            if (found == variables_.end()) {
                error(where, "There is no variable called " + quote(expr.member) + ".");
                return std::nullopt;
            }
            type = Type{found->second.variable->kind, found->second.choice};
            return "vars[" + lua_string(expr.member) + "]";
        }
        case ExprKind::EventValue:
            return event_value_code(where, expr, type);
        case ExprKind::Not: {
            if (expr.operands.size() != 1) {
                error(where, "'Not' needs exactly one condition.");
                return std::nullopt;
            }
            const std::optional<std::string> operand = condition_code(where, expr.operands[0]);
            type = Type{reflect::ValueKind::Bool, nullptr};
            return operand ? std::optional<std::string>("(not " + *operand + ")") : std::nullopt;
        }
        case ExprKind::All:
        case ExprKind::Any:
            return junction_code(where, expr, type);
        case ExprKind::Compare:
            return compare_code(where, expr, type);
        case ExprKind::Arithmetic:
            return arithmetic_code(where, expr, type);
        }
        error(where, "This block is of an unknown kind.");
        return std::nullopt;
    }

    std::optional<std::string> condition_code(BlockId block, const Expr& expr) {
        return slot_code(block, expr, Slot{reflect::ValueKind::Bool, nullptr, {}}, "A condition");
    }

    std::optional<std::string> property_code(BlockId where, const Expr& expr, Type& type) {
        const std::optional<Object> object = object_type(where, expr.object);
        if (!object) {
            return std::nullopt;
        }
        const reflect::PropertyInfo* property = object->find_property(expr.member);
        if (property == nullptr) {
            error(where,
                  quote(expr.object) + " has no property called " + quote(expr.member) + ".");
            return std::nullopt;
        }
        type = Type{property->kind, property->choice};
        return "sx.get(" + object_code(expr.object) + ", " + lua_string(expr.member) + ")";
    }

    /// The code for the arguments of `function`, starting with ", ", or nothing.
    std::optional<std::string> arguments_code(BlockId where, const reflect::FunctionInfo& function,
                                              const std::vector<Expr>& arguments) {
        const std::string name = quote(function.meta.display_name);
        const std::size_t needed = function.parameters.size();
        if (arguments.size() != needed) {
            error(where, name + " needs " + std::to_string(needed) +
                             (needed == 1 ? " value" : " values") + ", but has " +
                             std::to_string(arguments.size()) + ".");
            return std::nullopt;
        }
        std::string code;
        bool ok = true;
        for (std::size_t i = 0; i < needed; ++i) {
            const reflect::ParameterInfo& parameter = function.parameters[i];
            const Slot slot{parameter.kind, parameter.choice, parameter.limits};
            const std::optional<std::string> argument = slot_code(
                where, arguments[i], slot, quote(parameter.meta.display_name) + " of " + name);
            if (argument) {
                code += ", " + *argument;
            } else {
                ok = false;
            }
        }
        return ok ? std::optional<std::string>(code) : std::nullopt;
    }

    std::optional<std::string> call_code(BlockId where, const Expr& expr, Type& type) {
        const std::optional<Object> object = object_type(where, expr.object);
        if (!object) {
            return std::nullopt;
        }
        const reflect::FunctionInfo* function = object->find_function(expr.member);
        if (function == nullptr) {
            error(where, quote(expr.object) + " has no condition or value called " +
                             quote(expr.member) + ".");
            return std::nullopt;
        }
        if (function->role == reflect::FunctionRole::Action || !function->result_kind) {
            error(where, quote(function->meta.display_name) +
                             " is an action: it does something but gives no value, so it "
                             "cannot be used here.");
            return std::nullopt;
        }
        const std::optional<std::string> arguments =
            arguments_code(where, *function, expr.operands);
        if (!arguments) {
            return std::nullopt;
        }
        type = Type{*function->result_kind, function->result_choice};
        return "sx.call(" + object_code(expr.object) + ", " + lua_string(expr.member) + *arguments +
               ")";
    }

    std::optional<std::string> event_value_code(BlockId where, const Expr& expr, Type& type) {
        if (event_ != nullptr) {
            for (std::size_t i = 0; i < event_->parameters.size(); ++i) {
                const reflect::ParameterInfo& parameter = event_->parameters[i];
                if (parameter.id == expr.member) {
                    type = Type{parameter.kind, parameter.choice};
                    return "p" + std::to_string(i + 1);
                }
            }
        }
        error(where, quote(expr.member) + " is not a value of this rule's event.");
        return std::nullopt;
    }

    std::optional<std::string> junction_code(BlockId where, const Expr& expr, Type& type) {
        const bool all = expr.kind == ExprKind::All;
        if (expr.operands.size() < 2) {
            error(where,
                  std::string(all ? "'All of'" : "'Any of'") + " needs at least two conditions.");
            return std::nullopt;
        }
        std::string code = "(";
        bool ok = true;
        for (std::size_t i = 0; i < expr.operands.size(); ++i) {
            const std::optional<std::string> operand = condition_code(where, expr.operands[i]);
            if (!operand) {
                ok = false;
                continue;
            }
            code += (i == 0 ? "" : (all ? " and " : " or ")) + *operand;
        }
        type = Type{reflect::ValueKind::Bool, nullptr};
        return ok ? std::optional<std::string>(code + ")") : std::nullopt;
    }

    std::optional<std::string> compare_code(BlockId where, const Expr& expr, Type& type) {
        if (expr.operands.size() != 2) {
            error(where, "A comparison needs exactly two values.");
            return std::nullopt;
        }
        const Expr& left = expr.operands[0];
        const Expr& right = expr.operands[1];
        type = Type{reflect::ValueKind::Bool, nullptr};

        std::optional<std::string> left_code;
        std::optional<std::string> right_code;
        Type left_type;
        Type right_type;
        // A literal takes the type of the other side, so that "facing is 'north'" checks
        // 'north' against the facing choice list.
        if (left.kind == ExprKind::Literal && right.kind != ExprKind::Literal) {
            right_code = expr_code(where, right, right_type);
            if (!right_code) {
                return std::nullopt;
            }
            left_type = right_type;
            left_code = literal_for_comparison(where, left, right_type);
        } else if (right.kind == ExprKind::Literal && left.kind != ExprKind::Literal) {
            left_code = expr_code(where, left, left_type);
            if (!left_code) {
                return std::nullopt;
            }
            right_type = left_type;
            right_code = literal_for_comparison(where, right, left_type);
        } else {
            left_code = expr_code(where, left, left_type);
            right_code = expr_code(where, right, right_type);
        }
        if (!left_code || !right_code) {
            return std::nullopt;
        }

        const bool both_numeric = is_numeric(left_type.kind) && is_numeric(right_type.kind);
        const bool same =
            left_type.kind == right_type.kind &&
            (left_type.kind != reflect::ValueKind::Choice || left_type.choice == right_type.choice);
        if (!both_numeric && !same) {
            error(where, "These two values cannot be compared: one is " +
                             expected_text(left_type.kind, left_type.choice) + ", the other " +
                             expected_text(right_type.kind, right_type.choice) + ".");
            return std::nullopt;
        }
        const bool ordering =
            expr.compare != CompareOp::Equal && expr.compare != CompareOp::NotEqual;
        if (ordering && !both_numeric) {
            error(where, "Only numbers can be compared with less than or greater than.");
            return std::nullopt;
        }
        return "(" + *left_code + " " + compare_symbol(expr.compare) + " " + *right_code + ")";
    }

    std::optional<std::string> literal_for_comparison(BlockId where, const Expr& literal,
                                                      const Type& other) {
        // Numbers compare with numbers of either kind.
        const reflect::ValueKind kind =
            is_numeric(other.kind) ? reflect::ValueKind::Float : other.kind;
        return slot_code(where, literal, Slot{kind, other.choice, {}}, "The value compared");
    }

    std::optional<std::string> arithmetic_code(BlockId where, const Expr& expr, Type& type) {
        if (expr.operands.size() != 2) {
            error(where, "A calculation needs exactly two numbers.");
            return std::nullopt;
        }
        Type left_type;
        Type right_type;
        const std::optional<std::string> left = expr_code(where, expr.operands[0], left_type);
        const std::optional<std::string> right = expr_code(where, expr.operands[1], right_type);
        if (!left || !right) {
            return std::nullopt;
        }
        if (!is_numeric(left_type.kind) || !is_numeric(right_type.kind)) {
            error(where, "Only numbers can be used in a calculation.");
            return std::nullopt;
        }
        const bool whole = left_type.kind == reflect::ValueKind::Int &&
                           right_type.kind == reflect::ValueKind::Int &&
                           expr.arithmetic != ArithmeticOp::Divide;
        type = Type{whole ? reflect::ValueKind::Int : reflect::ValueKind::Float, nullptr};
        return "(" + *left + " " + arithmetic_symbol(expr.arithmetic) + " " + *right + ")";
    }

    // --- Statements ---------------------------------------------------------------------

    void statements(const std::vector<Stmt>& list) {
        for (const Stmt& stmt : list) {
            statement(stmt);
        }
    }

    /// The single value most statements take, or null after reporting it is missing.
    const Expr* single_argument(const Stmt& stmt) {
        if (stmt.arguments.size() != 1) {
            error(stmt.id, "This block needs exactly one value.");
            return nullptr;
        }
        return &stmt.arguments[0];
    }

    void statement(const Stmt& stmt) {
        if (indent_ >= max_depth) {
            error(stmt.id, "This block is nested too deeply.");
            return;
        }
        switch (stmt.kind) {
        case StmtKind::Do:
            do_statement(stmt);
            return;
        case StmtKind::Set:
            set_statement(stmt);
            return;
        case StmtKind::SetVariable:
            set_variable_statement(stmt);
            return;
        case StmtKind::Wait:
            if (const Expr* seconds = single_argument(stmt)) {
                reflect::NumericLimits limits;
                limits.min = 0.0;
                const std::optional<std::string> code =
                    slot_code(stmt.id, *seconds, Slot{reflect::ValueKind::Float, nullptr, limits},
                              "The time to wait");
                if (code) {
                    emit(stmt.id, trace() + "sx.wait(" + *code + ")");
                }
            }
            return;
        case StmtKind::WaitUntil:
            // Checked now, then once per update: a condition that already holds does not
            // wait at all.
            if (const Expr* condition = single_argument(stmt)) {
                const std::optional<std::string> code = condition_code(stmt.id, *condition);
                if (code) {
                    emit(stmt.id, trace() + "while not (" + *code + ") do sx.wait(0) end");
                }
            }
            return;
        case StmtKind::If:
            if_statement(stmt);
            return;
        case StmtKind::Repeat:
            repeat_statement(stmt);
            return;
        case StmtKind::ForEachTagged:
            for_each_tagged_statement(stmt);
            return;
        case StmtKind::Run:
            if (!sequences_.contains(stmt.member)) {
                error(stmt.id, "There is no sequence called " + quote(stmt.member) + ".");
                return;
            }
            emit(stmt.id, trace() + sequence_entry(stmt.member) + "()");
            return;
        case StmtKind::Log:
            if (const Expr* value = single_argument(stmt)) {
                Type type;
                const std::optional<std::string> code = expr_code(stmt.id, *value, type);
                if (code) {
                    emit(stmt.id, trace() + "sx.log(" + *code + ")");
                }
            }
            return;
        }
        error(stmt.id, "This block is of an unknown kind.");
    }

    void do_statement(const Stmt& stmt) {
        const std::optional<Object> object = object_type(stmt.id, stmt.object);
        if (!object) {
            return;
        }
        const reflect::FunctionInfo* function = object->find_function(stmt.member);
        if (function == nullptr) {
            error(stmt.id,
                  quote(stmt.object) + " has no action called " + quote(stmt.member) + ".");
            return;
        }
        if (function->role != reflect::FunctionRole::Action) {
            error(stmt.id, quote(function->meta.display_name) +
                               (function->role == reflect::FunctionRole::Condition
                                    ? " is a condition: it answers yes or no and changes "
                                      "nothing. Use it in an If."
                                    : " is a value: it changes nothing. Use it in another "
                                      "block's slot."));
            return;
        }
        const std::optional<std::string> arguments =
            arguments_code(stmt.id, *function, stmt.arguments);
        if (arguments) {
            emit(stmt.id, trace() + "sx.call(" + object_code(stmt.object) + ", " +
                              lua_string(stmt.member) + *arguments + ")");
        }
    }

    void set_statement(const Stmt& stmt) {
        const std::optional<Object> object = object_type(stmt.id, stmt.object);
        if (!object) {
            return;
        }
        const reflect::PropertyInfo* property = object->find_property(stmt.member);
        if (property == nullptr) {
            error(stmt.id,
                  quote(stmt.object) + " has no property called " + quote(stmt.member) + ".");
            return;
        }
        const std::string what = quote(property->meta.display_name) + " of " + quote(stmt.object);
        if (reflect::has_flag(property->flags, reflect::property_flag::read_only)) {
            error(stmt.id, what + " cannot be changed.");
            return;
        }
        const Expr* value = single_argument(stmt);
        if (value == nullptr) {
            return;
        }
        const std::optional<std::string> code = slot_code(
            stmt.id, *value, Slot{property->kind, property->choice, property->limits}, what);
        if (code) {
            emit(stmt.id, trace() + "sx.set(" + object_code(stmt.object) + ", " +
                              lua_string(stmt.member) + ", " + *code + ")");
        }
    }

    void set_variable_statement(const Stmt& stmt) {
        const auto found = variables_.find(stmt.member);
        if (found == variables_.end()) {
            error(stmt.id, "There is no variable called " + quote(stmt.member) + ".");
            return;
        }
        const Expr* value = single_argument(stmt);
        if (value == nullptr) {
            return;
        }
        const VariableInfo& info = found->second;
        const std::optional<std::string> code =
            slot_code(stmt.id, *value, Slot{info.variable->kind, info.choice, {}},
                      "The variable " + quote(stmt.member));
        if (code) {
            emit(stmt.id, trace() + "vars[" + lua_string(stmt.member) + "] = " + *code);
        }
    }

    void if_statement(const Stmt& stmt) {
        const Expr* condition = single_argument(stmt);
        if (condition == nullptr) {
            return;
        }
        const std::optional<std::string> code = condition_code(stmt.id, *condition);
        emit(stmt.id, trace() + "if " + code.value_or("false") + " then");
        ++indent_;
        statements(stmt.body);
        --indent_;
        if (!stmt.otherwise.empty()) {
            emit(stmt.id, "else");
            ++indent_;
            statements(stmt.otherwise);
            --indent_;
        }
        emit(stmt.id, "end");
    }

    /// for each object tagged `stmt.member`, in turn as "it" (a Lua local shadowing any
    /// outer it, so a Rule about a tag may loop over another tag).
    void for_each_tagged_statement(const Stmt& stmt) {
        if (!reflect::is_valid_id(stmt.member)) {
            error(stmt.id, "The tag " + quote(stmt.member) +
                               " is not valid: use lowercase letters, digits and underscores.");
            return;
        }
        const std::string group = "#" + stmt.member;
        rule_object_ = group; // checked as a tag may be
        const std::optional<Object> tagged = object_type(stmt.id, group);
        rule_object_.clear();
        if (!tagged) {
            return;
        }
        emit(stmt.id,
             trace() + "for _, it in ipairs(sx.tagged(" + lua_string(stmt.member) + ")) do");
        const std::string outer = it_object_;
        it_object_ = group;
        ++indent_;
        statements(stmt.body);
        --indent_;
        it_object_ = outer;
        emit(stmt.id, "end");
    }

    void repeat_statement(const Stmt& stmt) {
        const Expr* count = single_argument(stmt);
        if (count == nullptr) {
            return;
        }
        reflect::NumericLimits limits;
        limits.min = 0.0;
        const std::optional<std::string> code =
            slot_code(stmt.id, *count, Slot{reflect::ValueKind::Int, nullptr, limits},
                      "The number of repeats");
        emit(stmt.id, trace() + "for _ = 1, " + code.value_or("0") + " do");
        ++indent_;
        statements(stmt.body);
        --indent_;
        emit(stmt.id, "end");
    }

    // --- Sequences and rules ------------------------------------------------------------

    void compile_sequence(const Sequence& sequence) {
        if (!reflect::is_valid_id(sequence.name)) {
            return; // Already reported; the name cannot be used as a function name.
        }
        emit(sequence.id, "function " + sequence_entry(sequence.name) + "()");
        ++indent_;
        if (options_.trace) {
            emit(sequence.id, "sx.trace()");
        }
        statements(sequence.steps);
        --indent_;
        emit(sequence.id, "end");
    }

    void compile_rule(const Rule& rule) {
        event_ = nullptr;
        it_object_.clear();
        std::size_t payload = 0;
        if (!reflect::is_valid_id(rule.event)) {
            error(rule.id, "The event name " + quote(rule.event) + " is not valid.");
            return;
        }
        if (!rule.object.empty()) {
            rule_object_ = rule.object;
            const std::optional<Object> object = object_type(rule.id, rule.object);
            rule_object_.clear();
            if (!object) {
                return;
            }
            event_ = object->find_event(rule.event);
            if (event_ == nullptr) {
                error(rule.id,
                      quote(rule.object) + " has no event called " + quote(rule.event) + ".");
                return;
            }
            payload = event_->parameters.size();
        }
        if (rule.conditions.empty() && !rule.otherwise.empty()) {
            error(rule.id, "This rule has no conditions, so its Otherwise actions can never run.");
        }

        std::string parameters;
        for (std::size_t i = 0; i < payload; ++i) {
            parameters += (i == 0 ? "p" : ", p") + std::to_string(i + 1);
        }
        // About a tag: the tagged object comes after the event's values, as "it".
        if (rule.object.starts_with('#')) {
            parameters += parameters.empty() ? "it" : ", it";
            it_object_ = rule.object;
        }
        emit(rule.id, "sx.on(" + lua_string(event_key(rule.object, rule.event)) + ", function(" +
                          parameters + ")");
        ++indent_;
        if (options_.trace) {
            emit(rule.id, "sx.trace()");
        }
        if (rule.conditions.empty()) {
            statements(rule.actions);
        } else {
            // One condition per line, so that an error in a condition points at it.
            for (std::size_t i = 0; i < rule.conditions.size(); ++i) {
                const Expr& condition = rule.conditions[i];
                const std::optional<std::string> code = condition_code(rule.id, condition);
                emit(condition.id.is_valid() ? condition.id : rule.id,
                     std::string(i == 0 ? "if " : "  and ") + code.value_or("false"));
            }
            emit(rule.id, "then");
            ++indent_;
            statements(rule.actions);
            --indent_;
            if (!rule.otherwise.empty()) {
                emit(rule.id, "else");
                ++indent_;
                statements(rule.otherwise);
                --indent_;
            }
            emit(rule.id, "end");
        }
        --indent_;
        emit(rule.id, "end)");
        event_ = nullptr;
        it_object_.clear();
    }

    // --- Brains -------------------------------------------------------------------------

    /// Emits "if"/"elseif" tests for `transitions`, each setting `to` when it holds.
    /// `skip_current` leaves out a transition towards the state the brain is in.
    void transition_tests(const std::string& brain, const std::vector<Transition>& transitions,
                          bool skip_current) {
        bool first = true;
        for (const Transition& transition : transitions) {
            std::string test = std::string(first ? "if " : "elseif ") + "vars[" +
                               lua_string("@brain." + brain + ".time") +
                               "] >= " + format_number(transition.min_time);
            if (skip_current) {
                test += " and s ~= " + lua_string(transition.to);
            }
            emit(transition.id, test);
            for (const Expr& condition : transition.conditions) {
                const std::optional<std::string> code = condition_code(transition.id, condition);
                emit(condition.id.is_valid() ? condition.id : transition.id,
                     "  and " + code.value_or("false"));
            }
            emit(transition.id, "then");
            ++indent_;
            emit(transition.id, "to = " + lua_string(transition.to));
            --indent_;
            first = false;
        }
        if (!first) {
            emit(BlockId{}, "end");
        }
    }

    /// A local function running one section of every state, chosen by the state's name.
    void state_sections(const Brain& brain, const char* suffix,
                        std::vector<Stmt> BrainState::*section) {
        emit(brain.id, "local function brain_" + brain.name + "_" + suffix + "(s)");
        ++indent_;
        bool first = true;
        for (const BrainState& state : brain.states) {
            if ((state.*section).empty()) {
                continue;
            }
            emit(state.id, std::string(first ? "if" : "elseif") +
                               " s == " + lua_string(state.name) + " then");
            ++indent_;
            statements(state.*section);
            --indent_;
            first = false;
        }
        if (!first) {
            emit(brain.id, "end");
        }
        --indent_;
        emit(brain.id, "end");
    }

    void compile_brain(const Brain& brain) {
        if (!reflect::is_valid_id(brain.name) || brain.states.empty()) {
            return; // Already reported.
        }
        event_ = nullptr;
        const std::string& start =
            brain.initial.empty() ? brain.states.front().name : brain.initial;
        const std::string state = "vars[" + lua_string("@brain." + brain.name + ".state") + "]";
        const std::string time = "vars[" + lua_string("@brain." + brain.name + ".time") + "]";
        const std::string on = "vars[" + lua_string("@brain." + brain.name + ".on") + "]";
        // The brain's own variables: saved and restored with the asset's (Vm::variables()).
        emit(brain.id, state + " = " + lua_string(start));
        emit(brain.id, time + " = 0");
        emit(brain.id, on + " = false");
        state_sections(brain, "enter", &BrainState::on_enter);
        state_sections(brain, "exit", &BrainState::on_exit);
        state_sections(brain, "while", &BrainState::while_in);

        const std::string entry = "brain_" + brain.name;
        emit(brain.id, "function " + entry + "(dt)");
        ++indent_;
        if (options_.trace) {
            emit(brain.id, "sx.trace()");
        }
        emit(brain.id, "if not " + on + " then");
        ++indent_;
        emit(brain.id, on + " = true");
        emit(brain.id, entry + "_enter(" + state + ")");
        --indent_;
        emit(brain.id, "end");
        emit(brain.id, time + " = " + time + " + dt");
        emit(brain.id, "local s = " + state);
        emit(brain.id, "local to = nil");
        transition_tests(brain.name, brain.from_any, true);
        emit(brain.id, "if to == nil then");
        ++indent_;
        bool first = true;
        for (const BrainState& from : brain.states) {
            if (from.transitions.empty()) {
                continue;
            }
            emit(from.id,
                 std::string(first ? "if" : "elseif") + " s == " + lua_string(from.name) + " then");
            ++indent_;
            transition_tests(brain.name, from.transitions, false);
            --indent_;
            first = false;
        }
        if (!first) {
            emit(brain.id, "end");
        }
        --indent_;
        emit(brain.id, "end");
        emit(brain.id, "if to ~= nil then");
        ++indent_;
        emit(brain.id, entry + "_exit(s)");
        emit(brain.id, state + " = to");
        emit(brain.id, time + " = 0");
        emit(brain.id, entry + "_enter(to)");
        --indent_;
        emit(brain.id, "end");
        emit(brain.id, entry + "_while(" + state + ")");
        --indent_;
        emit(brain.id, "end");
        emit(brain.id, "_BRAINS[" + lua_string(brain.name) + "] = " + entry);
    }

    const LogicAsset& asset_;
    const reflect::Registry& registry_;
    const ObjectTypes& objects_;
    CompileOptions options_;

    std::vector<Diagnostic> errors_;
    std::vector<std::string> lines_;
    std::vector<BlockId> line_blocks_;
    int indent_ = 0;
    int depth_ = 0;
    std::set<std::uint64_t> ids_;
    std::set<std::string, std::less<>> sequences_;
    std::map<std::string, VariableInfo, std::less<>> variables_;
    const reflect::EventInfo* event_{nullptr};
    /// In a Rule about a tag: its object ("#enemy"), which "it" stands for there.
    std::string it_object_;
    /// While a Rule's own object is checked: it, which may be a tag ("#enemy").
    std::string rule_object_;
};

} // namespace

BlockId CompiledLogic::block_at_line(int line) const noexcept {
    if (line < 1 || static_cast<std::size_t>(line) > line_blocks.size()) {
        return BlockId{};
    }
    return line_blocks[static_cast<std::size_t>(line) - 1];
}

CompiledLogic compile(const LogicAsset& asset, const reflect::Registry& registry,
                      const ObjectTypes& objects, CompileOptions options) {
    return Compiler(asset, registry, objects, options).run();
}

std::string sequence_entry(std::string_view sequence_name) {
    return "seq_" + std::string(sequence_name);
}

std::string event_key(std::string_view object, std::string_view event) {
    return object.empty() ? std::string(event) : std::string(object) + "." + std::string(event);
}

} // namespace engine::logic
