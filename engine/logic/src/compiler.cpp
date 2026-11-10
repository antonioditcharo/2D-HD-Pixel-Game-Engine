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
using common::quoted;
using common::range_text;

/// Deeper nesting than this is refused. It keeps the compiler's recursion bounded for any
/// input, and stays below the Luau compiler's own limits.
constexpr int max_depth = 64;

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
    return quoted(std::get<std::string>(value));
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
        emit(BlockId{}, "local vars = {}");
        variables();
        for (const Sequence& sequence : asset_.sequences) {
            compile_sequence(sequence);
        }
        for (const Rule& rule : asset_.rules) {
            compile_rule(rule);
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

    [[nodiscard]] const reflect::TypeInfo* object_type(BlockId block, const std::string& object) {
        const reflect::TypeInfo* type = objects_ ? objects_(object) : nullptr;
        if (type == nullptr) {
            error(block, "There is no object called " + quoted(object) + ".");
        }
        return type;
    }

    // --- Asset-level checks -------------------------------------------------------------

    void check_names_and_ids() {
        if (!common::is_valid_script_name(asset_.name)) {
            error(BlockId{}, "The logic asset's name " + quoted(asset_.name) +
                                 " is not valid: use 1 to 128 letters, digits, '_', '-', '.' "
                                 "or '/'.");
        }
        for (const Sequence& sequence : asset_.sequences) {
            claim_id(sequence.id, true);
            if (!reflect::is_valid_id(sequence.name)) {
                error(sequence.id, "The sequence name " + quoted(sequence.name) +
                                       " is not valid: use lowercase letters, digits and "
                                       "underscores, starting with a letter.");
            } else if (!sequences_.insert(sequence.name).second) {
                error(sequence.id,
                      "There is already a sequence called " + quoted(sequence.name) + ".");
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
                error(BlockId{}, "The variable name " + quoted(variable.id) +
                                     " is not valid: use lowercase letters, digits and "
                                     "underscores, starting with a letter.");
                continue;
            }
            if (variables_.contains(variable.id)) {
                error(BlockId{}, "There is already a variable called " + quoted(variable.id) + ".");
                continue;
            }
            VariableInfo info{&variable, nullptr};
            if (variable.kind == reflect::ValueKind::Choice) {
                info.choice = registry_.find_choice_list(variable.choice_list);
                if (info.choice == nullptr) {
                    error(BlockId{}, "The variable " + quoted(variable.id) +
                                         " uses the choice list " + quoted(variable.choice_list) +
                                         ", which does not exist.");
                    continue;
                }
            }
            variables_.emplace(variable.id, info);
            const Slot slot{variable.kind, info.choice, {}};
            const std::optional<reflect::Value> start =
                variable.initial ? variable.initial : zero_value(variable.kind, info.choice);
            if (!start) {
                error(BlockId{}, "The variable " + quoted(variable.id) +
                                     " needs a starting value: its choice list is empty.");
                continue;
            }
            const std::optional<std::string> initial = literal_code(
                BlockId{}, *start, slot, "The starting value of " + quoted(variable.id));
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
                error(where, "There is no variable called " + quoted(expr.member) + ".");
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
        const reflect::TypeInfo* object = object_type(where, expr.object);
        if (object == nullptr) {
            return std::nullopt;
        }
        const reflect::PropertyInfo* property = object->find_property(expr.member);
        if (property == nullptr) {
            error(where,
                  quoted(expr.object) + " has no property called " + quoted(expr.member) + ".");
            return std::nullopt;
        }
        type = Type{property->kind, property->choice};
        return "sx.get(" + lua_string(expr.object) + ", " + lua_string(expr.member) + ")";
    }

    /// The code for the arguments of `function`, starting with ", ", or nothing.
    std::optional<std::string> arguments_code(BlockId where, const reflect::FunctionInfo& function,
                                              const std::vector<Expr>& arguments) {
        const std::string name = quoted(function.meta.display_name);
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
                where, arguments[i], slot, quoted(parameter.meta.display_name) + " of " + name);
            if (argument) {
                code += ", " + *argument;
            } else {
                ok = false;
            }
        }
        return ok ? std::optional<std::string>(code) : std::nullopt;
    }

    std::optional<std::string> call_code(BlockId where, const Expr& expr, Type& type) {
        const reflect::TypeInfo* object = object_type(where, expr.object);
        if (object == nullptr) {
            return std::nullopt;
        }
        const reflect::FunctionInfo* function = object->find_function(expr.member);
        if (function == nullptr) {
            error(where, quoted(expr.object) + " has no condition or value called " +
                             quoted(expr.member) + ".");
            return std::nullopt;
        }
        if (function->role == reflect::FunctionRole::Action || !function->result_kind) {
            error(where, quoted(function->meta.display_name) +
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
        return "sx.call(" + lua_string(expr.object) + ", " + lua_string(expr.member) + *arguments +
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
        error(where, quoted(expr.member) + " is not a value of this rule's event.");
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
        case StmtKind::If:
            if_statement(stmt);
            return;
        case StmtKind::Repeat:
            repeat_statement(stmt);
            return;
        case StmtKind::Run:
            if (!sequences_.contains(stmt.member)) {
                error(stmt.id, "There is no sequence called " + quoted(stmt.member) + ".");
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
        const reflect::TypeInfo* object = object_type(stmt.id, stmt.object);
        if (object == nullptr) {
            return;
        }
        const reflect::FunctionInfo* function = object->find_function(stmt.member);
        if (function == nullptr) {
            error(stmt.id,
                  quoted(stmt.object) + " has no action called " + quoted(stmt.member) + ".");
            return;
        }
        if (function->role != reflect::FunctionRole::Action) {
            error(stmt.id, quoted(function->meta.display_name) +
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
            emit(stmt.id, trace() + "sx.call(" + lua_string(stmt.object) + ", " +
                              lua_string(stmt.member) + *arguments + ")");
        }
    }

    void set_statement(const Stmt& stmt) {
        const reflect::TypeInfo* object = object_type(stmt.id, stmt.object);
        if (object == nullptr) {
            return;
        }
        const reflect::PropertyInfo* property = object->find_property(stmt.member);
        if (property == nullptr) {
            error(stmt.id,
                  quoted(stmt.object) + " has no property called " + quoted(stmt.member) + ".");
            return;
        }
        const std::string what = quoted(property->meta.display_name) + " of " + quoted(stmt.object);
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
            emit(stmt.id, trace() + "sx.set(" + lua_string(stmt.object) + ", " +
                              lua_string(stmt.member) + ", " + *code + ")");
        }
    }

    void set_variable_statement(const Stmt& stmt) {
        const auto found = variables_.find(stmt.member);
        if (found == variables_.end()) {
            error(stmt.id, "There is no variable called " + quoted(stmt.member) + ".");
            return;
        }
        const Expr* value = single_argument(stmt);
        if (value == nullptr) {
            return;
        }
        const VariableInfo& info = found->second;
        const std::optional<std::string> code =
            slot_code(stmt.id, *value, Slot{info.variable->kind, info.choice, {}},
                      "The variable " + quoted(stmt.member));
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
        std::size_t payload = 0;
        if (!reflect::is_valid_id(rule.event)) {
            error(rule.id, "The event name " + quoted(rule.event) + " is not valid.");
            return;
        }
        if (!rule.object.empty()) {
            const reflect::TypeInfo* object = object_type(rule.id, rule.object);
            if (object == nullptr) {
                return;
            }
            event_ = object->find_event(rule.event);
            if (event_ == nullptr) {
                error(rule.id,
                      quoted(rule.object) + " has no event called " + quoted(rule.event) + ".");
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
