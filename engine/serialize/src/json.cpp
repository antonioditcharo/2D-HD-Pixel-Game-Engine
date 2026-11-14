#include "engine/serialize/json.h"

#include "engine/foundation/text.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <utility>

#include "detail.h"

namespace engine::serialize {

namespace detail {

/// Appends a key to a JSON Pointer, escaping '~' and '/' as RFC 6901 requires.
std::string child(std::string_view path, std::string_view key) {
    std::string out(path);
    out += '/';
    for (const char c : key) {
        if (c == '~') {
            out += "~0";
        } else if (c == '/') {
            out += "~1";
        } else {
            out += c;
        }
    }
    return out;
}

std::string child(std::string_view path, std::size_t index) {
    return std::string(path) + "/" + std::to_string(index);
}

/// The value of a whole number above 0, whether JSON holds it as signed or unsigned
/// (parsed text gives unsigned, values built in code are often signed).
std::optional<std::uint64_t> positive_integer(const Json& json) {
    if (json.is_number_unsigned()) {
        const auto value = json.get<std::uint64_t>();
        return value > 0 ? std::optional<std::uint64_t>(value) : std::nullopt;
    }
    if (json.is_number_integer()) {
        const auto value = json.get<std::int64_t>();
        return value > 0 ? std::optional<std::uint64_t>(static_cast<std::uint64_t>(value))
                         : std::nullopt;
    }
    return std::nullopt;
}

/// How a JSON value reads in a message.
std::string describe(const Json& json) {
    if (json.is_string()) {
        return text::quote(json.get_ref<const std::string&>());
    }
    if (json.is_null()) {
        return "nothing";
    }
    if (json.is_object()) {
        return "a group of values";
    }
    if (json.is_array()) {
        return "a list";
    }
    return json.dump();
}

} // namespace detail

namespace {

using detail::child;
using detail::describe;
using detail::positive_integer;

using text::expected_text;
using text::quote;
using text::range_text;

/// Deeper nesting than this is refused when reading, as the compiler refuses it.
constexpr int max_depth = 64;

// ---------------------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------
// Values
// ---------------------------------------------------------------------------------------

[[nodiscard]] Json value_to_json(const reflect::Value& value, const reflect::ChoiceInfo* choice) {
    if (const bool* flag = std::get_if<bool>(&value)) {
        return *flag;
    }
    if (const std::int64_t* number = std::get_if<std::int64_t>(&value)) {
        const reflect::ChoiceEntry* entry =
            choice != nullptr ? choice->find_entry(*number) : nullptr;
        if (entry != nullptr) {
            return entry->id;
        }
        return *number;
    }
    if (const double* real = std::get_if<double>(&value)) {
        return *real;
    }
    return std::get<std::string>(value);
}

enum class Read : std::uint8_t { Ok, WrongKind, UnknownChoice };

/// Reads a JSON value as the given kind. Whole numbers may be written as 3 or 3.0.
[[nodiscard]] Read json_to_value(const Json& json, reflect::ValueKind kind,
                                 const reflect::ChoiceInfo* choice, reflect::Value& out) {
    switch (kind) {
    case reflect::ValueKind::Bool:
        if (!json.is_boolean()) {
            return Read::WrongKind;
        }
        out = json.get<bool>();
        return Read::Ok;
    case reflect::ValueKind::Int:
        if (json.is_number_unsigned()) {
            const auto number = json.get<std::uint64_t>();
            if (number > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
                return Read::WrongKind;
            }
            out = static_cast<std::int64_t>(number);
            return Read::Ok;
        }
        if (json.is_number_integer()) {
            out = json.get<std::int64_t>();
            return Read::Ok;
        }
        if (json.is_number_float()) {
            const auto number = json.get<double>();
            constexpr double limit = 9223372036854775808.0; // 2^63, exact as a double
            if (!std::isfinite(number) || std::trunc(number) != number || number < -limit ||
                number >= limit) {
                return Read::WrongKind;
            }
            out = static_cast<std::int64_t>(number);
            return Read::Ok;
        }
        return Read::WrongKind;
    case reflect::ValueKind::Float:
        if (!json.is_number()) {
            return Read::WrongKind;
        }
        out = json.get<double>();
        return Read::Ok;
    case reflect::ValueKind::Text:
        if (!json.is_string()) {
            return Read::WrongKind;
        }
        out = json.get<std::string>();
        return Read::Ok;
    case reflect::ValueKind::Choice: {
        if (!json.is_string()) {
            return Read::WrongKind;
        }
        const reflect::ChoiceEntry* entry =
            choice != nullptr ? choice->find_entry(json.get_ref<const std::string&>()) : nullptr;
        if (entry == nullptr) {
            return Read::UnknownChoice;
        }
        out = entry->value;
        return Read::Ok;
    }
    }
    return Read::WrongKind;
}

// ---------------------------------------------------------------------------------------
// Logic: names of kinds and operators in files
// ---------------------------------------------------------------------------------------

template <typename E, std::size_t N>
using Names = std::array<std::pair<E, std::string_view>, N>;

constexpr Names<reflect::ValueKind, 5> value_kind_names{{
    {reflect::ValueKind::Bool, "yes_no"},
    {reflect::ValueKind::Int, "whole_number"},
    {reflect::ValueKind::Float, "number"},
    {reflect::ValueKind::Text, "text"},
    {reflect::ValueKind::Choice, "choice"},
}};

constexpr Names<logic::ExprKind, 10> expr_kind_names{{
    {logic::ExprKind::Literal, "literal"},
    {logic::ExprKind::Property, "property"},
    {logic::ExprKind::Call, "call"},
    {logic::ExprKind::Variable, "variable"},
    {logic::ExprKind::EventValue, "event_value"},
    {logic::ExprKind::Not, "not"},
    {logic::ExprKind::All, "all"},
    {logic::ExprKind::Any, "any"},
    {logic::ExprKind::Compare, "compare"},
    {logic::ExprKind::Arithmetic, "arithmetic"},
}};

constexpr Names<logic::StmtKind, 10> stmt_kind_names{{
    {logic::StmtKind::Do, "do"},
    {logic::StmtKind::Set, "set"},
    {logic::StmtKind::SetVariable, "set_variable"},
    {logic::StmtKind::Wait, "wait"},
    {logic::StmtKind::WaitUntil, "wait_until"},
    {logic::StmtKind::If, "if"},
    {logic::StmtKind::Repeat, "repeat"},
    {logic::StmtKind::Run, "run"},
    {logic::StmtKind::Log, "log"},
    {logic::StmtKind::ForEachTagged, "for_each_tagged"},
}};

constexpr Names<logic::CompareOp, 6> compare_names{{
    {logic::CompareOp::Equal, "equal"},
    {logic::CompareOp::NotEqual, "not_equal"},
    {logic::CompareOp::Less, "less"},
    {logic::CompareOp::LessEqual, "less_equal"},
    {logic::CompareOp::Greater, "greater"},
    {logic::CompareOp::GreaterEqual, "greater_equal"},
}};

constexpr Names<logic::ArithmeticOp, 4> arithmetic_names{{
    {logic::ArithmeticOp::Add, "add"},
    {logic::ArithmeticOp::Subtract, "subtract"},
    {logic::ArithmeticOp::Multiply, "multiply"},
    {logic::ArithmeticOp::Divide, "divide"},
}};

template <typename E, std::size_t N>
[[nodiscard]] std::string_view name_of(const Names<E, N>& names, E value) {
    for (const auto& [key, name] : names) {
        if (key == value) {
            return name;
        }
    }
    return "unknown";
}

template <typename E, std::size_t N>
[[nodiscard]] std::optional<E> value_of(const Names<E, N>& names, std::string_view name) {
    for (const auto& [key, text] : names) {
        if (text == name) {
            return key;
        }
    }
    return std::nullopt;
}

template <typename E, std::size_t N>
[[nodiscard]] std::string list_of(const Names<E, N>& names) {
    std::string out;
    for (std::size_t i = 0; i < N; ++i) {
        out += (i == 0 ? "" : ", ") + std::string(names[i].second);
    }
    return out;
}

// ---------------------------------------------------------------------------------------
// Logic: writing
// ---------------------------------------------------------------------------------------

/// A literal in a logic file. Its JSON type gives its kind: true/false, 3 (whole), 3.0
/// (number) or "text".
[[nodiscard]] Json literal_to_json(const reflect::Value& value) {
    return value_to_json(value, nullptr);
}

[[nodiscard]] Json expr_to_json(const logic::Expr& expr) {
    Json out = Json::object();
    out["kind"] = name_of(expr_kind_names, expr.kind);
    if (expr.id.is_valid()) {
        out["block"] = expr.id.value;
    }
    if (expr.kind == logic::ExprKind::Literal) {
        out["value"] = literal_to_json(expr.literal);
    }
    if (!expr.object.empty()) {
        out["object"] = expr.object;
    }
    if (!expr.member.empty()) {
        out["member"] = expr.member;
    }
    if (expr.kind == logic::ExprKind::Compare) {
        out["op"] = name_of(compare_names, expr.compare);
    }
    if (expr.kind == logic::ExprKind::Arithmetic) {
        out["op"] = name_of(arithmetic_names, expr.arithmetic);
    }
    if (!expr.operands.empty()) {
        Json operands = Json::array();
        for (const logic::Expr& operand : expr.operands) {
            operands.push_back(expr_to_json(operand));
        }
        out["operands"] = std::move(operands);
    }
    return out;
}

[[nodiscard]] Json exprs_to_json(const std::vector<logic::Expr>& exprs) {
    Json out = Json::array();
    for (const logic::Expr& expr : exprs) {
        out.push_back(expr_to_json(expr));
    }
    return out;
}

[[nodiscard]] Json stmts_to_json(const std::vector<logic::Stmt>& stmts);

[[nodiscard]] Json stmt_to_json(const logic::Stmt& stmt) {
    Json out = Json::object();
    out["kind"] = name_of(stmt_kind_names, stmt.kind);
    out["block"] = stmt.id.value;
    if (!stmt.object.empty()) {
        out["object"] = stmt.object;
    }
    if (!stmt.member.empty()) {
        out["member"] = stmt.member;
    }
    if (!stmt.arguments.empty()) {
        out["values"] = exprs_to_json(stmt.arguments);
    }
    if (!stmt.body.empty()) {
        out["body"] = stmts_to_json(stmt.body);
    }
    if (!stmt.otherwise.empty()) {
        out["otherwise"] = stmts_to_json(stmt.otherwise);
    }
    return out;
}

Json stmts_to_json(const std::vector<logic::Stmt>& stmts) {
    Json out = Json::array();
    for (const logic::Stmt& stmt : stmts) {
        out.push_back(stmt_to_json(stmt));
    }
    return out;
}

// ---------------------------------------------------------------------------------------
// Logic: reading
// ---------------------------------------------------------------------------------------

/// Reads one logic document, collecting issues. A problem that would lose a block makes
/// the whole read fail; an unknown key is only reported.
class LogicReader {
public:
    explicit LogicReader(std::vector<Issue>& issues) : issues_(issues) {}

    [[nodiscard]] bool read_asset(const Json& json, logic::LogicAsset& out) {
        if (!object(json, "",
                    {"format", "version", "name", "variables", "sequences", "rules", "brains"})) {
            return false;
        }
        bool ok = string_field(json, "", "name", out.name, true);
        ok = list(json, "", "variables",
                  [&](const Json& item, const std::string& path) {
                      logic::Variable variable;
                      const bool read = read_variable(item, path, variable);
                      out.variables.push_back(std::move(variable));
                      return read;
                  }) &&
             ok;
        ok = list(json, "", "sequences",
                  [&](const Json& item, const std::string& path) {
                      logic::Sequence sequence;
                      const bool read = read_sequence(item, path, sequence);
                      out.sequences.push_back(std::move(sequence));
                      return read;
                  }) &&
             ok;
        ok = list(json, "", "rules",
                  [&](const Json& item, const std::string& path) {
                      logic::Rule rule;
                      const bool read = read_rule(item, path, rule);
                      out.rules.push_back(std::move(rule));
                      return read;
                  }) &&
             ok;
        ok = list(json, "", "brains",
                  [&](const Json& item, const std::string& path) {
                      logic::Brain brain;
                      const bool read = read_brain(item, path, brain);
                      out.brains.push_back(std::move(brain));
                      return read;
                  }) &&
             ok;
        return ok;
    }

private:
    void issue(std::string path, std::string message) {
        issues_.push_back(Issue{std::move(path), std::move(message)});
    }

    /// Checks that `json` is an object, and reports keys outside `known`.
    bool object(const Json& json, const std::string& path,
                std::initializer_list<std::string_view> known) {
        if (!json.is_object()) {
            issue(path, "This must be a group of named values (a JSON object), but is " +
                            describe(json) + ".");
            return false;
        }
        for (const auto& [key, value] : json.items()) {
            bool found = false;
            for (const std::string_view name : known) {
                found = found || name == key;
            }
            if (!found) {
                issue(child(path, key), "Unknown entry " + quote(key) + ". It was ignored.");
            }
        }
        return true;
    }

    bool string_field(const Json& json, const std::string& path, const char* key, std::string& out,
                      bool required) {
        const auto found = json.find(key);
        if (found == json.end()) {
            if (required) {
                issue(child(path, key), "This entry is missing.");
            }
            return !required;
        }
        if (!found->is_string()) {
            issue(child(path, key), "This must be text, but is " + describe(*found) + ".");
            return false;
        }
        out = found->get<std::string>();
        return true;
    }

    /// Reads a block id. Required ids must be present; every id must be a positive whole
    /// number.
    bool block_field(const Json& json, const std::string& path, logic::BlockId& out,
                     bool required) {
        const auto found = json.find("block");
        if (found == json.end()) {
            if (required) {
                issue(child(path, "block"), "This block has no id.");
            }
            return !required;
        }
        const std::optional<std::uint64_t> id = positive_integer(*found);
        if (!id) {
            issue(child(path, "block"),
                  "A block id must be a whole number above 0, but is " + describe(*found) + ".");
            return false;
        }
        out = logic::BlockId{*id};
        return true;
    }

    /// Calls `read` for each item of the optional list `key`.
    template <typename Read>
    bool list(const Json& json, const std::string& path, const char* key, Read&& read) {
        const auto found = json.find(key);
        if (found == json.end()) {
            return true;
        }
        const std::string list_path = child(path, key);
        if (!found->is_array()) {
            issue(list_path, "This must be a list, but is " + describe(*found) + ".");
            return false;
        }
        bool ok = true;
        for (std::size_t i = 0; i < found->size(); ++i) {
            ok = read((*found)[i], child(list_path, i)) && ok;
        }
        return ok;
    }

    template <typename E, std::size_t N>
    bool name_field(const Json& json, const std::string& path, const char* key,
                    const Names<E, N>& names, E& out) {
        std::string name;
        if (!string_field(json, path, key, name, true)) {
            return false;
        }
        const std::optional<E> value = value_of(names, name);
        if (!value) {
            issue(child(path, key), quote(name) + " is not one of: " + list_of(names) + ".");
            return false;
        }
        out = *value;
        return true;
    }

    bool read_literal(const Json& json, const std::string& path, reflect::Value& out) {
        if (json.is_boolean()) {
            out = json.get<bool>();
        } else if (json.is_number_unsigned() &&
                   json.get<std::uint64_t>() >
                       static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            issue(path, "This whole number is too large.");
            return false;
        } else if (json.is_number_integer()) {
            out = json.get<std::int64_t>();
        } else if (json.is_number_float()) {
            out = json.get<double>();
        } else if (json.is_string()) {
            out = json.get<std::string>();
        } else {
            issue(path, "A value must be yes/no, a number or text, but is " + describe(json) + ".");
            return false;
        }
        return true;
    }

    bool read_variable(const Json& json, const std::string& path, logic::Variable& out) {
        if (!object(json, path, {"id", "kind", "choice_list", "initial"})) {
            return false;
        }
        bool ok = string_field(json, path, "id", out.id, true);
        ok = name_field(json, path, "kind", value_kind_names, out.kind) && ok;
        ok = string_field(json, path, "choice_list", out.choice_list, false) && ok;
        if (const auto initial = json.find("initial"); initial != json.end()) {
            reflect::Value value;
            ok = read_literal(*initial, child(path, "initial"), value) && ok;
            out.initial = std::move(value);
        }
        return ok;
    }

    bool read_expr(const Json& json, const std::string& path, logic::Expr& out, int depth) {
        if (depth > max_depth) {
            issue(path, "This block is nested too deeply.");
            return false;
        }
        if (!object(json, path, {"kind", "block", "value", "object", "member", "op", "operands"})) {
            return false;
        }
        bool ok = name_field(json, path, "kind", expr_kind_names, out.kind);
        ok = block_field(json, path, out.id, false) && ok;
        if (const auto value = json.find("value"); value != json.end()) {
            ok = read_literal(*value, child(path, "value"), out.literal) && ok;
        } else if (ok && out.kind == logic::ExprKind::Literal) {
            issue(child(path, "value"), "This entry is missing.");
            ok = false;
        }
        ok = string_field(json, path, "object", out.object, false) && ok;
        ok = string_field(json, path, "member", out.member, false) && ok;
        if (out.kind == logic::ExprKind::Compare) {
            ok = name_field(json, path, "op", compare_names, out.compare) && ok;
        } else if (out.kind == logic::ExprKind::Arithmetic) {
            ok = name_field(json, path, "op", arithmetic_names, out.arithmetic) && ok;
        }
        ok = list(json, path, "operands",
                  [&](const Json& item, const std::string& item_path) {
                      logic::Expr operand;
                      const bool read = read_expr(item, item_path, operand, depth + 1);
                      out.operands.push_back(std::move(operand));
                      return read;
                  }) &&
             ok;
        return ok;
    }

    bool exprs(const Json& json, const std::string& path, const char* key,
               std::vector<logic::Expr>& out, int depth) {
        return list(json, path, key, [&](const Json& item, const std::string& item_path) {
            logic::Expr value;
            const bool read = read_expr(item, item_path, value, depth);
            out.push_back(std::move(value));
            return read;
        });
    }

    bool stmts(const Json& json, const std::string& path, const char* key,
               std::vector<logic::Stmt>& out, int depth) {
        return list(json, path, key, [&](const Json& item, const std::string& item_path) {
            logic::Stmt value;
            const bool read = read_stmt(item, item_path, value, depth);
            out.push_back(std::move(value));
            return read;
        });
    }

    bool read_stmt(const Json& json, const std::string& path, logic::Stmt& out, int depth) {
        if (depth > max_depth) {
            issue(path, "This block is nested too deeply.");
            return false;
        }
        if (!object(json, path,
                    {"kind", "block", "object", "member", "values", "body", "otherwise"})) {
            return false;
        }
        bool ok = name_field(json, path, "kind", stmt_kind_names, out.kind);
        ok = block_field(json, path, out.id, true) && ok;
        ok = string_field(json, path, "object", out.object, false) && ok;
        ok = string_field(json, path, "member", out.member, false) && ok;
        ok = exprs(json, path, "values", out.arguments, 0) && ok;
        ok = stmts(json, path, "body", out.body, depth + 1) && ok;
        ok = stmts(json, path, "otherwise", out.otherwise, depth + 1) && ok;
        return ok;
    }

    bool read_sequence(const Json& json, const std::string& path, logic::Sequence& out) {
        if (!object(json, path, {"block", "name", "steps"})) {
            return false;
        }
        bool ok = block_field(json, path, out.id, true);
        ok = string_field(json, path, "name", out.name, true) && ok;
        ok = stmts(json, path, "steps", out.steps, 0) && ok;
        return ok;
    }

    bool read_rule(const Json& json, const std::string& path, logic::Rule& out) {
        if (!object(json, path, {"block", "object", "event", "if", "do", "otherwise"})) {
            return false;
        }
        bool ok = block_field(json, path, out.id, true);
        ok = string_field(json, path, "object", out.object, false) && ok;
        ok = string_field(json, path, "event", out.event, true) && ok;
        ok = exprs(json, path, "if", out.conditions, 0) && ok;
        ok = stmts(json, path, "do", out.actions, 0) && ok;
        ok = stmts(json, path, "otherwise", out.otherwise, 0) && ok;
        return ok;
    }

    bool read_transition(const Json& json, const std::string& path, logic::Transition& out) {
        if (!object(json, path, {"block", "to", "if", "after"})) {
            return false;
        }
        bool ok = block_field(json, path, out.id, true);
        ok = string_field(json, path, "to", out.to, true) && ok;
        ok = exprs(json, path, "if", out.conditions, 0) && ok;
        if (const auto after = json.find("after"); after != json.end()) {
            if (after->is_number()) {
                out.min_time = after->get<double>();
            } else {
                issue(child(path, "after"),
                      "This must be a number of seconds, but is " + describe(*after) + ".");
                ok = false;
            }
        }
        return ok;
    }

    bool transitions(const Json& json, const std::string& path, const char* key,
                     std::vector<logic::Transition>& out) {
        return list(json, path, key, [&](const Json& item, const std::string& item_path) {
            logic::Transition transition;
            const bool read = read_transition(item, item_path, transition);
            out.push_back(std::move(transition));
            return read;
        });
    }

    bool read_brain(const Json& json, const std::string& path, logic::Brain& out) {
        if (!object(json, path, {"block", "name", "initial", "states", "from_any"})) {
            return false;
        }
        bool ok = block_field(json, path, out.id, true);
        ok = string_field(json, path, "name", out.name, true) && ok;
        ok = string_field(json, path, "initial", out.initial, false) && ok;
        ok = list(json, path, "states",
                  [&](const Json& item, const std::string& item_path) {
                      logic::BrainState state;
                      bool read = object(item, item_path,
                                         {"block", "name", "enter", "while", "exit", "go"});
                      if (read) {
                          read = block_field(item, item_path, state.id, true);
                          read = string_field(item, item_path, "name", state.name, true) && read;
                          read = stmts(item, item_path, "enter", state.on_enter, 0) && read;
                          read = stmts(item, item_path, "while", state.while_in, 0) && read;
                          read = stmts(item, item_path, "exit", state.on_exit, 0) && read;
                          read = transitions(item, item_path, "go", state.transitions) && read;
                      }
                      out.states.push_back(std::move(state));
                      return read;
                  }) &&
             ok;
        ok = transitions(json, path, "from_any", out.from_any) && ok;
        return ok;
    }

    std::vector<Issue>& issues_;
};

/// Migrations from each older schema version to the next: entry i turns version i + 1 into
/// version i + 2. Version 1 is the first, so there are none yet. A change to the format
/// adds an entry here and raises logic_version.
using Migration = void (*)(Json& document);
constexpr std::array<Migration, logic_version - 1> logic_migrations{};

/// A copy of `json` brought from `version` up to logic_version.
[[nodiscard]] Json migrate_logic(const Json& json, [[maybe_unused]] std::uint64_t version) {
    Json document = json;
    // Compiled only once a migration exists: with none, MSVC proves the loop body
    // unreachable and warns (C4702).
    if constexpr (logic_version > 1) {
        for (std::uint64_t v = version; v < static_cast<std::uint64_t>(logic_version); ++v) {
            logic_migrations.at(static_cast<std::size_t>(v - 1))(document);
        }
    }
    return document;
}

} // namespace

// ---------------------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------------------

std::optional<Json> parse(std::string_view text, std::vector<Issue>& issues) {
    try {
        std::optional<Json> json;
        json.emplace(Json::parse(text.begin(), text.end()));
        return json;
    } catch (const std::exception& error) {
        // nlohmann's messages start with an "[json.exception.parse_error.101]" tag.
        std::string detail = error.what();
        if (const auto end = detail.find("] ");
            detail.rfind("[json.exception", 0) == 0 && end != std::string::npos) {
            detail.erase(0, end + 2);
        }
        issues.push_back(Issue{"", "The file is not valid JSON: " + detail});
        return std::nullopt;
    }
}

std::string to_text(const Json& json) {
    return json.dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
}

// ---------------------------------------------------------------------------------------
// Reflected objects
// ---------------------------------------------------------------------------------------

Json object_to_json(reflect::ObjectRef object, const reflect::TypeInfo& type) {
    Json out = Json::object();
    for (const reflect::PropertyInfo& property : type.properties) {
        if (reflect::has_flag(property.flags, reflect::property_flag::read_only)) {
            continue;
        }
        const std::optional<reflect::Value> value = reflect::get_property(object, property);
        if (!value) {
            return Json::object(); // Null reference or another type's description.
        }
        out[property.id] = value_to_json(*value, property.choice);
    }
    return out;
}

std::vector<Issue> object_from_json(const Json& json, reflect::ObjectRef object,
                                    const reflect::TypeInfo& type, std::string_view path) {
    std::vector<Issue> issues;
    if (!json.is_object()) {
        issues.push_back(Issue{std::string(path), "The properties of " +
                                                      quote(type.meta.display_name) +
                                                      " must be a group of named values, but are " +
                                                      describe(json) + "."});
        return issues;
    }
    for (const auto& [key, item] : json.items()) {
        const std::string where = child(path, key);
        const reflect::PropertyInfo* property = type.find_property(key);
        if (property == nullptr) {
            issues.push_back(Issue{where, quote(type.meta.display_name) +
                                              " has no property called " + quote(key) +
                                              ". It was ignored."});
            continue;
        }
        const std::string name = quote(property->meta.display_name);
        if (reflect::has_flag(property->flags, reflect::property_flag::read_only)) {
            issues.push_back(Issue{where, name + " cannot be changed, so it was not loaded."});
            continue;
        }
        reflect::Value value;
        const Read read = json_to_value(item, property->kind, property->choice, value);
        reflect::SetResult result = reflect::SetResult::Ok;
        if (read == Read::WrongKind) {
            result = reflect::SetResult::WrongKind;
        } else if (read == Read::UnknownChoice) {
            result = reflect::SetResult::UnknownChoice;
        } else {
            result = reflect::set_property(object, *property, value);
        }
        switch (result) {
        case reflect::SetResult::Ok:
            break;
        case reflect::SetResult::OutOfRange:
            issues.push_back(Issue{where, name + " " + range_text(property->limits) + ", but is " +
                                              describe(item) + ". It was not loaded."});
            break;
        case reflect::SetResult::WrongType:
            issues.push_back(Issue{where, name + " could not be loaded into this object."});
            break;
        case reflect::SetResult::NotRepresentable:
            issues.push_back(
                Issue{where, name + " cannot hold " + describe(item) +
                                 ": it is too large or too small. It was not loaded."});
            break;
        default:
            issues.push_back(
                Issue{where, name + " must be " + expected_text(property->kind, property->choice) +
                                 ", but is " + describe(item) + ". It was not loaded."});
            break;
        }
    }
    return issues;
}

// ---------------------------------------------------------------------------------------
// Logic assets
// ---------------------------------------------------------------------------------------

Json logic_to_json(const logic::LogicAsset& asset) {
    Json out = Json::object();
    out["format"] = logic_format;
    out["version"] = logic_version;
    out["name"] = asset.name;
    if (!asset.variables.empty()) {
        Json variables = Json::array();
        for (const logic::Variable& variable : asset.variables) {
            Json item = Json::object();
            item["id"] = variable.id;
            item["kind"] = name_of(value_kind_names, variable.kind);
            if (!variable.choice_list.empty()) {
                item["choice_list"] = variable.choice_list;
            }
            if (variable.initial) {
                item["initial"] = literal_to_json(*variable.initial);
            }
            variables.push_back(std::move(item));
        }
        out["variables"] = std::move(variables);
    }
    if (!asset.sequences.empty()) {
        Json sequences = Json::array();
        for (const logic::Sequence& sequence : asset.sequences) {
            Json item = Json::object();
            item["block"] = sequence.id.value;
            item["name"] = sequence.name;
            if (!sequence.steps.empty()) {
                item["steps"] = stmts_to_json(sequence.steps);
            }
            sequences.push_back(std::move(item));
        }
        out["sequences"] = std::move(sequences);
    }
    if (!asset.rules.empty()) {
        Json rules = Json::array();
        for (const logic::Rule& rule : asset.rules) {
            Json item = Json::object();
            item["block"] = rule.id.value;
            if (!rule.object.empty()) {
                item["object"] = rule.object;
            }
            item["event"] = rule.event;
            if (!rule.conditions.empty()) {
                item["if"] = exprs_to_json(rule.conditions);
            }
            if (!rule.actions.empty()) {
                item["do"] = stmts_to_json(rule.actions);
            }
            if (!rule.otherwise.empty()) {
                item["otherwise"] = stmts_to_json(rule.otherwise);
            }
            rules.push_back(std::move(item));
        }
        out["rules"] = std::move(rules);
    }
    if (!asset.brains.empty()) {
        const auto transitions_to_json = [](const std::vector<logic::Transition>& list) {
            Json items = Json::array();
            for (const logic::Transition& transition : list) {
                Json item = Json::object();
                item["block"] = transition.id.value;
                item["to"] = transition.to;
                if (!transition.conditions.empty()) {
                    item["if"] = exprs_to_json(transition.conditions);
                }
                if (transition.min_time != 0.0) {
                    item["after"] = transition.min_time;
                }
                items.push_back(std::move(item));
            }
            return items;
        };
        Json brains = Json::array();
        for (const logic::Brain& brain : asset.brains) {
            Json item = Json::object();
            item["block"] = brain.id.value;
            item["name"] = brain.name;
            if (!brain.initial.empty()) {
                item["initial"] = brain.initial;
            }
            Json states = Json::array();
            for (const logic::BrainState& state : brain.states) {
                Json entry = Json::object();
                entry["block"] = state.id.value;
                entry["name"] = state.name;
                if (!state.on_enter.empty()) {
                    entry["enter"] = stmts_to_json(state.on_enter);
                }
                if (!state.while_in.empty()) {
                    entry["while"] = stmts_to_json(state.while_in);
                }
                if (!state.on_exit.empty()) {
                    entry["exit"] = stmts_to_json(state.on_exit);
                }
                if (!state.transitions.empty()) {
                    entry["go"] = transitions_to_json(state.transitions);
                }
                states.push_back(std::move(entry));
            }
            item["states"] = std::move(states);
            if (!brain.from_any.empty()) {
                item["from_any"] = transitions_to_json(brain.from_any);
            }
            brains.push_back(std::move(item));
        }
        out["brains"] = std::move(brains);
    }
    return out;
}

LogicLoad logic_from_json(const Json& json) {
    LogicLoad result;
    if (!json.is_object()) {
        result.issues.push_back(Issue{"", "This is not a logic asset: it must be a group of named "
                                          "values (a JSON object)."});
        return result;
    }
    const auto format = json.find("format");
    if (format == json.end() || !format->is_string() ||
        format->get_ref<const std::string&>() != logic_format) {
        result.issues.push_back(Issue{"/format", "This is not a logic asset: its format is not " +
                                                     quote(logic_format) + "."});
        return result;
    }
    const auto version = json.find("version");
    const std::optional<std::uint64_t> parsed_version =
        version == json.end() ? std::nullopt : positive_integer(*version);
    if (!parsed_version) {
        result.issues.push_back(
            Issue{"/version", "The format version is missing or not a whole number above 0."});
        return result;
    }
    const std::uint64_t file_version = *parsed_version;
    if (file_version > static_cast<std::uint64_t>(logic_version)) {
        result.issues.push_back(
            Issue{"/version",
                  "This logic asset was saved by a newer version of Sparxel (format version " +
                      std::to_string(file_version) + "). This version reads up to " +
                      std::to_string(logic_version) + ". Update Sparxel to open it."});
        return result;
    }

    const Json document = migrate_logic(json, file_version);

    logic::LogicAsset asset;
    LogicReader reader(result.issues);
    if (reader.read_asset(document, asset)) {
        result.asset = std::move(asset);
    }
    return result;
}

} // namespace engine::serialize
