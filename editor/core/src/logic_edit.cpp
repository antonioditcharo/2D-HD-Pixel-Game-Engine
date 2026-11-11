// Editing logic in the Document: the values written into steps, and adding, deleting and
// moving steps. Every edit is a step of the Document's undo, and checked by the compiler.

#include "engine/foundation/text.h"
#include "engine/game/logic_world.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <set>
#include <utility>
#include <variant>

#include "document_impl.h"
#include "editor/core/document.h"
#include "editor/core/logic_text.h"

namespace editor {

namespace logic = engine::logic;
namespace reflect = engine::reflect;
namespace world = engine::world;
using logic::BlockId;
using logic::Stmt;
using logic::StmtKind;
namespace fs = std::filesystem;

namespace {

/// What the helpers below look objects up in: the registry and the scene.
struct Lookup {
    const reflect::Registry& registry;
    const world::World& world;
};

bool is_game_object(std::string_view object) {
    return std::find(game_objects.begin(), game_objects.end(), object) != game_objects.end();
}

/// What `object` is made of: one of the game's objects by name, or a named entity's
/// components, as the game resolves them (game::object_types()).
std::vector<const reflect::TypeInfo*> types_of(const Lookup& d, std::string_view object) {
    if (is_game_object(object)) {
        const reflect::TypeInfo* type = d.registry.find_type(object);
        return type != nullptr ? std::vector<const reflect::TypeInfo*>{type}
                               : std::vector<const reflect::TypeInfo*>{};
    }
    return engine::game::object_types(d.world)(object);
}

const reflect::FunctionInfo* find_function(const Lookup& d, std::string_view object,
                                           std::string_view member) {
    for (const reflect::TypeInfo* type : types_of(d, object)) {
        if (const reflect::FunctionInfo* f = type->find_function(member)) {
            return f;
        }
    }
    return nullptr;
}

const reflect::PropertyInfo* find_property(const Lookup& d, std::string_view object,
                                           std::string_view member) {
    for (const reflect::TypeInfo* type : types_of(d, object)) {
        if (const reflect::PropertyInfo* p = type->find_property(member)) {
            return p;
        }
    }
    return nullptr;
}

/// Where a step is: the list holding it and its place in it.
struct Place {
    std::vector<Stmt>* list = nullptr;
    std::size_t index = 0;
};

Place find_in(std::vector<Stmt>& list, BlockId id) {
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (list[i].id == id) {
            return Place{&list, i};
        }
        if (Place inside = find_in(list[i].body, id); inside.list != nullptr) {
            return inside;
        }
        if (Place inside = find_in(list[i].otherwise, id); inside.list != nullptr) {
            return inside;
        }
    }
    return {};
}

/// Every top-level list of steps in an asset.
std::vector<std::vector<Stmt>*> step_lists(logic::LogicAsset& asset) {
    std::vector<std::vector<Stmt>*> lists;
    for (logic::Rule& rule : asset.rules) {
        lists.push_back(&rule.actions);
        lists.push_back(&rule.otherwise);
    }
    for (logic::Sequence& sequence : asset.sequences) {
        lists.push_back(&sequence.steps);
    }
    for (logic::Brain& brain : asset.brains) {
        for (logic::BrainState& state : brain.states) {
            lists.push_back(&state.on_enter);
            lists.push_back(&state.while_in);
            lists.push_back(&state.on_exit);
        }
    }
    return lists;
}

Place find_step(logic::LogicAsset& asset, BlockId id) {
    if (!id.is_valid()) {
        return {};
    }
    for (std::vector<Stmt>* list : step_lists(asset)) {
        if (Place found = find_in(*list, id); found.list != nullptr) {
            return found;
        }
    }
    return {};
}

std::vector<Stmt>* header_list(logic::LogicAsset& asset, BlockId id);

/// Where a step added at `after` goes: first inside it if it is an If or a Repeat, after it
/// if it is another step, first under it if it is a Rule, Sequence or Brain state.
Place where_to_add(logic::LogicAsset& asset, BlockId after) {
    if (const Place place = find_step(asset, after); place.list != nullptr) {
        Stmt& s = (*place.list)[place.index];
        if (s.kind == StmtKind::If || s.kind == StmtKind::Repeat) {
            return Place{&s.body, 0};
        }
        return Place{place.list, place.index + 1};
    }
    return Place{header_list(asset, after), 0};
}

/// The steps a new step goes first into when added under a header block: a Rule's
/// actions, a Sequence's steps, a Brain state's steps on entering.
std::vector<Stmt>* header_list(logic::LogicAsset& asset, BlockId id) {
    for (logic::Rule& rule : asset.rules) {
        if (rule.id == id) {
            return &rule.actions;
        }
    }
    for (logic::Sequence& sequence : asset.sequences) {
        if (sequence.id == id) {
            return &sequence.steps;
        }
    }
    for (logic::Brain& brain : asset.brains) {
        for (logic::BrainState& state : brain.states) {
            if (state.id == id) {
                return &state.on_enter;
            }
        }
    }
    return nullptr;
}

void highest(const logic::Expr& e, std::uint64_t& top) {
    top = std::max(top, e.id.value);
    for (const logic::Expr& operand : e.operands) {
        highest(operand, top);
    }
}

void highest(const std::vector<Stmt>& list, std::uint64_t& top) {
    for (const Stmt& s : list) {
        top = std::max(top, s.id.value);
        for (const logic::Expr& e : s.arguments) {
            highest(e, top);
        }
        highest(s.body, top);
        highest(s.otherwise, top);
    }
}

/// A block id no block of `asset` uses yet.
BlockId next_id(const logic::LogicAsset& asset) {
    std::uint64_t top = 0;
    for (const logic::Rule& rule : asset.rules) {
        top = std::max(top, rule.id.value);
        for (const logic::Expr& e : rule.conditions) {
            highest(e, top);
        }
        highest(rule.actions, top);
        highest(rule.otherwise, top);
    }
    for (const logic::Sequence& sequence : asset.sequences) {
        top = std::max(top, sequence.id.value);
        highest(sequence.steps, top);
    }
    for (const logic::Brain& brain : asset.brains) {
        top = std::max(top, brain.id.value);
        for (const logic::BrainState& state : brain.states) {
            top = std::max(top, state.id.value);
            highest(state.on_enter, top);
            highest(state.while_in, top);
            highest(state.on_exit, top);
            for (const logic::Transition& go : state.transitions) {
                top = std::max(top, go.id.value);
                for (const logic::Expr& e : go.conditions) {
                    highest(e, top);
                }
            }
        }
        for (const logic::Transition& go : brain.from_any) {
            top = std::max(top, go.id.value);
            for (const logic::Expr& e : go.conditions) {
                highest(e, top);
            }
        }
    }
    return BlockId{top + 1};
}

/// The value a new block's slot starts with: the parameter's default, if it fits.
reflect::Value starting_value(const reflect::ParameterInfo& p) {
    const reflect::Value& d = p.default_value;
    switch (p.kind) {
    case reflect::ValueKind::Bool:
        return std::holds_alternative<bool>(d) ? d : reflect::Value{false};
    case reflect::ValueKind::Int:
        return std::holds_alternative<std::int64_t>(d) ? d : reflect::Value{std::int64_t{0}};
    case reflect::ValueKind::Float:
        return std::holds_alternative<double>(d) ? d : reflect::Value{0.0};
    case reflect::ValueKind::Text:
        return std::holds_alternative<std::string>(d) ? d : reflect::Value{std::string()};
    case reflect::ValueKind::Choice:
        if (std::holds_alternative<std::int64_t>(d) || std::holds_alternative<std::string>(d)) {
            return d;
        }
        return p.choice != nullptr && !p.choice->entries.empty()
                   ? reflect::Value{p.choice->entries.front().id}
                   : reflect::Value{std::int64_t{0}};
    }
    return d;
}

/// `value` as a slot of `slot`'s kind, or nothing (with `error`) if it does not fit.
std::optional<reflect::Value> fit(const LogicSlot& slot, const reflect::Value& value,
                                  std::string& error) {
    const auto number = [&value]() -> std::optional<double> {
        if (const auto* d = std::get_if<double>(&value)) {
            return *d;
        }
        if (const auto* i = std::get_if<std::int64_t>(&value)) {
            return static_cast<double>(*i);
        }
        return std::nullopt;
    };
    const auto in_range = [&slot, &error](double n) {
        const bool low = slot.limits.min.has_value() && n < *slot.limits.min;
        const bool high = slot.limits.max.has_value() && n > *slot.limits.max;
        if (!std::isfinite(n) || low || high) {
            error = slot.label + " must be a number";
            if (slot.limits.min && slot.limits.max) {
                error += " from " + engine::text::format_number(*slot.limits.min) + " to " +
                         engine::text::format_number(*slot.limits.max);
            } else if (slot.limits.min) {
                error += " of at least " + engine::text::format_number(*slot.limits.min);
            } else if (slot.limits.max) {
                error += " of at most " + engine::text::format_number(*slot.limits.max);
            }
            error += ".";
            return false;
        }
        return true;
    };
    switch (slot.kind) {
    case reflect::ValueKind::Bool:
        if (std::holds_alternative<bool>(value)) {
            return value;
        }
        error = slot.label + " must be yes or no.";
        return std::nullopt;
    case reflect::ValueKind::Int: {
        const std::optional<double> n = number();
        if (!n || std::trunc(*n) != *n) {
            error = slot.label + " must be a whole number.";
            return std::nullopt;
        }
        if (!in_range(*n)) {
            return std::nullopt;
        }
        return reflect::Value{static_cast<std::int64_t>(*n)};
    }
    case reflect::ValueKind::Float: {
        const std::optional<double> n = number();
        if (!n) {
            error = slot.label + " must be a number.";
            return std::nullopt;
        }
        if (!in_range(*n)) {
            return std::nullopt;
        }
        return reflect::Value{*n};
    }
    case reflect::ValueKind::Text:
        if (std::holds_alternative<std::string>(value)) {
            return value;
        }
        error = slot.label + " must be text.";
        return std::nullopt;
    case reflect::ValueKind::Choice: {
        const reflect::ChoiceEntry* entry = nullptr;
        if (slot.choice != nullptr) {
            if (const auto* id = std::get_if<std::string>(&value)) {
                entry = slot.choice->find_entry(*id);
            } else if (const auto* v = std::get_if<std::int64_t>(&value)) {
                entry = slot.choice->find_entry(*v);
            }
        }
        if (entry == nullptr) {
            error = slot.label + " must be one of its choices.";
            return std::nullopt;
        }
        return reflect::Value{entry->id};
    }
    }
    return std::nullopt;
}

LogicSlot slot(std::size_t argument, std::string label, reflect::ValueKind kind,
               const reflect::Value& value) {
    LogicSlot s;
    s.argument = argument;
    s.label = std::move(label);
    s.kind = kind;
    s.value = value;
    return s;
}

/// The value a new variable, or a value compared with one, starts at: no, 0 or "".
reflect::Value zero_of(reflect::ValueKind kind) {
    switch (kind) {
    case reflect::ValueKind::Bool:
        return reflect::Value{false};
    case reflect::ValueKind::Int:
        return reflect::Value{std::int64_t{0}};
    case reflect::ValueKind::Float:
        return reflect::Value{0.0};
    case reflect::ValueKind::Text:
    case reflect::ValueKind::Choice:
        break;
    }
    return reflect::Value{std::string()};
}

const logic::Variable* find_variable(const logic::LogicAsset& asset, std::string_view name) {
    for (const logic::Variable& v : asset.variables) {
        if (v.id == name) {
            return &v;
        }
    }
    return nullptr;
}

/// The comparison operators as the editor writes them, in CompareOp's order.
constexpr std::array<std::string_view, 6> comparison_signs{"=",      "\u2260", "<",
                                                           "\u2264", ">",      "\u2265"};

/// `sign` as an operator: one of comparison_signs, or "!=", "<=", ">=".
std::optional<logic::CompareOp> comparison_of(std::string_view sign) {
    for (std::size_t i = 0; i < comparison_signs.size(); ++i) {
        if (sign == comparison_signs.at(i)) {
            return static_cast<logic::CompareOp>(i);
        }
    }
    if (sign == "!=") {
        return logic::CompareOp::NotEqual;
    }
    if (sign == "<=") {
        return logic::CompareOp::LessEqual;
    }
    if (sign == ">=") {
        return logic::CompareOp::GreaterEqual;
    }
    return std::nullopt;
}

/// True if `e` is a variable compared with a written value, as add_logic_comparison()
/// makes them.
bool is_comparison(const logic::Expr& e) {
    if (e.kind != logic::ExprKind::Compare || e.operands.size() != 2) {
        return false;
    }
    const logic::ExprKind left = e.operands[0].kind;
    return left == logic::ExprKind::Variable || left == logic::ExprKind::Property ||
           left == logic::ExprKind::EventValue;
}

/// The written number inside a value: the value itself if it is written, or the number
/// added to (or taken from...) something, as set_logic_arithmetic() makes it. Null if
/// nothing in it is written.
logic::Expr* written(logic::Expr& e) {
    if (e.kind == logic::ExprKind::Literal) {
        return &e;
    }
    if (e.kind == logic::ExprKind::Arithmetic && e.operands.size() == 2 &&
        e.operands[1].kind == logic::ExprKind::Literal) {
        return &e.operands[1];
    }
    return nullptr;
}

/// Writes `value` into a slot's value: into its written number if it has one, or as a
/// written value instead of where it came from.
void write_value(logic::Expr& e, reflect::Value value) {
    if (logic::Expr* number = written(e)) {
        number->literal = std::move(value);
        return;
    }
    const BlockId id = e.id;
    e = logic::ir::literal(std::move(value));
    e.id = id;
}

/// The arithmetic operators as the editor writes them, in ArithmeticOp's order.
constexpr std::array<std::string_view, 4> arithmetic_signs{"+", "\u2212", "\u00d7", "\u00f7"};

struct Header;
Header find_header(logic::LogicAsset& asset, BlockId id);

/// The value behind slot `argument` of `block` (as logic_slots() numbers them), or null
/// for slots that are not values (names, a Run's Sequence, a variable chosen, operators).
logic::Expr* slot_target(logic::LogicAsset& asset, BlockId block, std::size_t argument);

/// Calls `visit` on `e` and every expression inside it.
template <typename Visit>
void each_expr(logic::Expr& e, const Visit& visit) {
    visit(e);
    for (logic::Expr& operand : e.operands) {
        each_expr(operand, visit);
    }
}

/// Calls `visit` on every expression of `asset`: conditions, and the values of steps.
template <typename Visit>
void each_expr(logic::LogicAsset& asset, const Visit& visit);

/// Where a block that is not a step is. A state's and a "Go to"'s Brain is set too, and
/// a "Go to"'s state when it leads out of one (not for "from any state").
struct Header {
    logic::Rule* rule = nullptr;
    logic::Sequence* sequence = nullptr;
    logic::Brain* brain = nullptr;
    logic::BrainState* state = nullptr;
    logic::Transition* transition = nullptr;
    std::vector<logic::Transition>* transitions = nullptr;
    logic::Expr* condition = nullptr;
    std::vector<logic::Expr>* conditions = nullptr;
    /// When the condition is one of an "any of these" (or "all of these") group: the group.
    logic::Expr* group = nullptr;
    std::size_t index = 0; ///< In whichever list holds the block.

    [[nodiscard]] bool found() const noexcept {
        return rule != nullptr || sequence != nullptr || brain != nullptr;
    }
};

bool find_condition(std::vector<logic::Expr>& conditions, BlockId id, Header& h) {
    for (std::size_t i = 0; i < conditions.size(); ++i) {
        logic::Expr& c = conditions[i];
        if (c.id == id) {
            h.condition = &c;
            h.conditions = &conditions;
            h.index = i;
            return true;
        }
        // Inside a group of conditions: one level down.
        if (c.kind == logic::ExprKind::Any || c.kind == logic::ExprKind::All) {
            for (std::size_t j = 0; j < c.operands.size(); ++j) {
                if (c.operands[j].id == id) {
                    h.condition = &c.operands[j];
                    h.conditions = &c.operands;
                    h.group = &c;
                    h.index = j;
                    return true;
                }
            }
        }
    }
    return false;
}

/// The test inside a condition: itself, or what its "not" turns around.
logic::Expr& tested(logic::Expr& e) {
    return e.kind == logic::ExprKind::Not && !e.operands.empty() ? e.operands[0] : e;
}
const logic::Expr& tested(const logic::Expr& e) {
    return e.kind == logic::ExprKind::Not && !e.operands.empty() ? e.operands[0] : e;
}

/// Turns a condition into its opposite, or back, keeping its block id on the outside.
void flip(logic::Expr& e) {
    if (e.kind == logic::ExprKind::Not && !e.operands.empty()) {
        logic::Expr inner = std::move(e.operands[0]);
        inner.id = e.id;
        e = std::move(inner);
        return;
    }
    logic::Expr turned;
    turned.kind = logic::ExprKind::Not;
    turned.id = e.id;
    logic::Expr inner = std::move(e);
    inner.id = BlockId{};
    turned.operands.push_back(std::move(inner));
    e = std::move(turned);
}

bool find_transition(std::vector<logic::Transition>& list, BlockId id, Header& h) {
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (list[i].id == id) {
            h.transition = &list[i];
            h.transitions = &list;
            h.index = i;
            return true;
        }
        if (find_condition(list[i].conditions, id, h)) {
            h.transition = &list[i];
            return true;
        }
    }
    return false;
}

Header find_header(logic::LogicAsset& asset, BlockId id) {
    Header h;
    if (!id.is_valid()) {
        return h;
    }
    for (std::size_t i = 0; i < asset.rules.size(); ++i) {
        logic::Rule& rule = asset.rules[i];
        if (rule.id == id) {
            h.rule = &rule;
            h.index = i;
            return h;
        }
        if (find_condition(rule.conditions, id, h)) {
            h.rule = &rule;
            return h;
        }
    }
    for (std::size_t i = 0; i < asset.sequences.size(); ++i) {
        if (asset.sequences[i].id == id) {
            h.sequence = &asset.sequences[i];
            h.index = i;
            return h;
        }
    }
    for (std::size_t b = 0; b < asset.brains.size(); ++b) {
        logic::Brain& brain = asset.brains[b];
        if (brain.id == id) {
            h.brain = &brain;
            h.index = b;
            return h;
        }
        for (std::size_t i = 0; i < brain.states.size(); ++i) {
            logic::BrainState& state = brain.states[i];
            if (state.id == id) {
                h.brain = &brain;
                h.state = &state;
                h.index = i;
                return h;
            }
            if (find_transition(state.transitions, id, h)) {
                h.brain = &brain;
                h.state = &state;
                return h;
            }
        }
        if (find_transition(brain.from_any, id, h)) {
            h.brain = &brain;
            return h;
        }
    }
    return h;
}

/// Calls `visit` on every step of `list`, inside ones too.
template <typename Visit>
void each_step(std::vector<Stmt>& list, const Visit& visit) {
    for (Stmt& s : list) {
        visit(s);
        each_step(s.body, visit);
        each_step(s.otherwise, visit);
    }
}

template <typename Visit>
void each_expr(logic::LogicAsset& asset, const Visit& visit) {
    for (logic::Rule& rule : asset.rules) {
        for (logic::Expr& e : rule.conditions) {
            each_expr(e, visit);
        }
    }
    for (logic::Brain& brain : asset.brains) {
        for (logic::BrainState& state : brain.states) {
            for (logic::Transition& go : state.transitions) {
                for (logic::Expr& e : go.conditions) {
                    each_expr(e, visit);
                }
            }
        }
        for (logic::Transition& go : brain.from_any) {
            for (logic::Expr& e : go.conditions) {
                each_expr(e, visit);
            }
        }
    }
    for (std::vector<Stmt>* list : step_lists(asset)) {
        each_step(*list, [&visit](Stmt& s) {
            for (logic::Expr& e : s.arguments) {
                each_expr(e, visit);
            }
        });
    }
}

/// The slots of a comparison made by add_logic_comparison(): its operator, and the value
/// compared with, of the variable's kind.
void comparison_slots(std::optional<reflect::ValueKind> kind, const logic::Expr& c,
                      std::vector<LogicSlot>& slots) {
    LogicSlot sign;
    sign.argument = 0;
    sign.label = "Is";
    sign.kind = reflect::ValueKind::Text;
    sign.value =
        reflect::Value{std::string(comparison_signs.at(static_cast<std::size_t>(c.compare)))};
    slots.push_back(std::move(sign));
    LogicSlot with;
    with.argument = 1;
    with.label = "Than";
    with.kind = kind.value_or(reflect::ValueKind::Text);
    with.value = c.operands[1].literal;
    slots.push_back(std::move(with));
}

/// "sequence_1", or the first "<stem>_<n>" for which `taken` is false.
template <typename Taken>
std::string free_name(const std::string& stem, const Taken& taken) {
    for (int n = 1;; ++n) {
        std::string name = stem + "_" + std::to_string(n);
        if (!taken(name)) {
            return name;
        }
    }
}

constexpr std::string_view bad_name =
    "Names use lower-case letters, digits and _, and start with a letter.";

/// The default value of each parameter of `f`, as literals.
std::vector<logic::Expr> starting_operands(const reflect::FunctionInfo& f) {
    std::vector<logic::Expr> operands;
    operands.reserve(f.parameters.size());
    for (const reflect::ParameterInfo& p : f.parameters) {
        operands.push_back(logic::ir::literal(starting_value(p)));
    }
    return operands;
}

logic::Expr* slot_target(logic::LogicAsset& asset, BlockId block, std::size_t argument) {
    const auto in_test = [argument](logic::Expr& test) -> logic::Expr* {
        logic::Expr& t = tested(test);
        if (is_comparison(t)) {
            return argument == 1 ? &t.operands[1] : nullptr;
        }
        if (t.kind == logic::ExprKind::Call && argument < t.operands.size()) {
            return &t.operands[argument];
        }
        return nullptr;
    };
    if (const Place place = find_step(asset, block); place.list != nullptr) {
        Stmt& s = (*place.list)[place.index];
        switch (s.kind) {
        case StmtKind::If:
        case StmtKind::WaitUntil:
            return s.arguments.empty() ? nullptr : in_test(s.arguments[0]);
        case StmtKind::Run:
            return nullptr;
        case StmtKind::SetVariable:
            return argument == 0 && !s.arguments.empty() ? &s.arguments[0] : nullptr;
        case StmtKind::Do:
        case StmtKind::Set:
        case StmtKind::Wait:
        case StmtKind::Repeat:
        case StmtKind::Log:
            break;
        }
        return argument < s.arguments.size() ? &s.arguments[argument] : nullptr;
    }
    const Header h = find_header(asset, block);
    return h.condition != nullptr ? in_test(*h.condition) : nullptr;
}

/// The Rule `block` is, or is inside (a condition or a step of it); null if none.
logic::Rule* rule_of(logic::LogicAsset& asset, BlockId block) {
    for (logic::Rule& rule : asset.rules) {
        Header inside;
        if (rule.id == block || find_condition(rule.conditions, block, inside) ||
            find_in(rule.actions, block).list != nullptr ||
            find_in(rule.otherwise, block).list != nullptr) {
            return &rule;
        }
    }
    return nullptr;
}

/// The kind of value `left` (a variable, a property or an event value) holds, seen from
/// `block`; nothing if it is not known.
std::optional<reflect::ValueKind> kind_of(const Lookup& look, logic::LogicAsset& asset,
                                          BlockId block, const logic::Expr& left) {
    switch (left.kind) {
    case logic::ExprKind::Variable:
        if (const logic::Variable* v = find_variable(asset, left.member)) {
            return v->kind;
        }
        return std::nullopt;
    case logic::ExprKind::Property:
        if (const reflect::PropertyInfo* p = find_property(look, left.object, left.member)) {
            return p->kind;
        }
        return std::nullopt;
    case logic::ExprKind::EventValue:
        if (const logic::Rule* rule = rule_of(asset, block); rule != nullptr) {
            for (const reflect::TypeInfo* type : types_of(look, rule->object)) {
                if (const reflect::EventInfo* event = type->find_event(rule->event)) {
                    if (const reflect::ParameterInfo* p = event->find_parameter(left.member)) {
                        return p->kind;
                    }
                }
            }
        }
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

} // namespace

std::vector<LogicSlot> Document::header_slots(logic::LogicAsset& asset, BlockId block) const {
    const Impl& d = *impl_;
    const Header h = find_header(asset, block);
    std::vector<LogicSlot> slots;
    if (h.condition != nullptr && is_comparison(tested(*h.condition))) {
        const logic::Expr& c = tested(*h.condition);
        comparison_slots(kind_of(Lookup{d.registry, *d.world}, asset, block, c.operands[0]), c,
                         slots);
    } else if (h.condition != nullptr) {
        const logic::Expr& c = tested(*h.condition);
        const reflect::FunctionInfo* f =
            c.kind == logic::ExprKind::Call
                ? find_function(Lookup{d.registry, *d.world}, c.object, c.member)
                : nullptr;
        if (f != nullptr) {
            for (std::size_t i = 0; i < f->parameters.size() && i < c.operands.size(); ++i) {
                const reflect::ParameterInfo& p = f->parameters[i];
                LogicSlot one = slot(i, p.meta.display_name, p.kind, c.operands[i].literal);
                one.limits = p.limits;
                one.choice = p.choice;
                slots.push_back(std::move(one));
            }
        }
    } else if (h.transition != nullptr) {
        slots.push_back(slot(0, "Go to", reflect::ValueKind::Text, h.transition->to));
        LogicSlot after =
            slot(1, "After seconds", reflect::ValueKind::Float, h.transition->min_time);
        after.limits.min = 0.0;
        slots.push_back(std::move(after));
    } else if (h.state != nullptr) {
        slots.push_back(slot(0, "Name", reflect::ValueKind::Text, h.state->name));
    } else if (h.brain != nullptr) {
        slots.push_back(slot(0, "Name", reflect::ValueKind::Text, h.brain->name));
    } else if (h.sequence != nullptr) {
        slots.push_back(slot(0, "Name", reflect::ValueKind::Text, h.sequence->name));
    }
    return slots;
}

std::vector<LogicSlot> Document::logic_slots(std::string_view path, BlockId block) const {
    std::vector<LogicSlot> slots = unfilled_slots(path, block);
    LogicFile* file = impl_->logic_file(path);
    if (file == nullptr) {
        return slots;
    }
    // Where each value comes from: written, written beside something (talks + 1), or from
    // a variable, an event value or a property.
    for (LogicSlot& slot : slots) {
        logic::Expr* value = slot_target(file->asset, block, slot.argument);
        if (value == nullptr) {
            continue;
        }
        if (logic::Expr* number = written(*value)) {
            slot.typed = true;
            slot.value = number->literal;
            if (number != value) {
                slot.from =
                    describe_value(value->operands[0], *this) + " " +
                    std::string(arithmetic_signs.at(static_cast<std::size_t>(value->arithmetic)));
            }
        } else {
            slot.typed = false;
            slot.from = describe_value(*value, *this);
        }
    }
    return slots;
}

std::vector<LogicSlot> Document::unfilled_slots(std::string_view path, BlockId step) const {
    Impl& d = *impl_;
    LogicFile* file = d.logic_file(path);
    if (file == nullptr) {
        return {};
    }
    const Place place = find_step(file->asset, step);
    if (place.list == nullptr) {
        return header_slots(file->asset, step);
    }
    const Stmt& s = (*place.list)[place.index];
    // Every value is a slot; whether it is written or comes from elsewhere is filled
    // in at the end (fill_sources()).
    const auto literal = [&s](std::size_t i) { return i < s.arguments.size(); };
    std::vector<LogicSlot> slots;
    switch (s.kind) {
    case StmtKind::Do:
        if (const reflect::FunctionInfo* f =
                find_function(Lookup{d.registry, *d.world}, s.object, s.member)) {
            for (std::size_t i = 0; i < f->parameters.size(); ++i) {
                if (!literal(i)) {
                    continue;
                }
                const reflect::ParameterInfo& p = f->parameters[i];
                LogicSlot one = slot(i, p.meta.display_name, p.kind, s.arguments[i].literal);
                one.limits = p.limits;
                one.choice = p.choice;
                slots.push_back(std::move(one));
            }
        }
        break;
    case StmtKind::Set:
        if (const reflect::PropertyInfo* p =
                find_property(Lookup{d.registry, *d.world}, s.object, s.member);
            p != nullptr && literal(0)) {
            LogicSlot one = slot(0, p->meta.display_name, p->kind, s.arguments[0].literal);
            one.limits = p->limits;
            one.choice = p->choice;
            slots.push_back(std::move(one));
        }
        break;
    case StmtKind::SetVariable:
        slots.push_back(slot(1, "Variable", reflect::ValueKind::Text, s.member));
        for (const logic::Variable& v : file->asset.variables) {
            if (v.id == s.member && literal(0) && v.kind != reflect::ValueKind::Choice) {
                slots.push_back(slot(0, "Value", v.kind, s.arguments[0].literal));
            }
        }
        break;
    case StmtKind::Wait:
        if (literal(0)) {
            LogicSlot one = slot(0, "Seconds", reflect::ValueKind::Float, s.arguments[0].literal);
            one.limits.min = 0.0;
            slots.push_back(std::move(one));
        }
        break;
    case StmtKind::Repeat:
        if (literal(0)) {
            LogicSlot one = slot(0, "Times", reflect::ValueKind::Int, s.arguments[0].literal);
            one.limits.min = 0.0;
            slots.push_back(std::move(one));
        }
        break;
    case StmtKind::Log:
        if (literal(0)) {
            slots.push_back(slot(0, "Text", reflect::ValueKind::Text, s.arguments[0].literal));
        }
        break;
    case StmtKind::Run:
        slots.push_back(slot(0, "Sequence", reflect::ValueKind::Text, s.member));
        break;
    case StmtKind::If:
    case StmtKind::WaitUntil:
        // A variable compared with a value: the operator and the value.
        if (!s.arguments.empty() && is_comparison(tested(s.arguments[0]))) {
            const logic::Expr& c = tested(s.arguments[0]);
            comparison_slots(
                kind_of(Lookup{d.registry, *d.world}, file->asset, step, c.operands[0]), c, slots);
            break;
        }
        // The values of its condition, when it is one of an object's conditions.
        if (!s.arguments.empty() && tested(s.arguments[0]).kind == logic::ExprKind::Call) {
            const logic::Expr& c = tested(s.arguments[0]);
            if (const reflect::FunctionInfo* f =
                    find_function(Lookup{d.registry, *d.world}, c.object, c.member)) {
                for (std::size_t i = 0; i < f->parameters.size() && i < c.operands.size(); ++i) {
                    const reflect::ParameterInfo& p = f->parameters[i];
                    LogicSlot one = slot(i, p.meta.display_name, p.kind, c.operands[i].literal);
                    one.limits = p.limits;
                    one.choice = p.choice;
                    slots.push_back(std::move(one));
                }
            }
        }
        break;
    }
    return slots;
}

bool Document::set_logic_slot(std::string_view path, BlockId step, std::size_t argument,
                              const reflect::Value& value, std::string& error) {
    Impl& d = *impl_;
    const std::vector<LogicSlot> slots = logic_slots(path, step);
    const auto found = std::find_if(slots.begin(), slots.end(), [argument](const LogicSlot& s) {
        return s.argument == argument;
    });
    if (found == slots.end()) {
        error = "That block has no such value to change.";
        return false;
    }
    std::optional<reflect::Value> fitted = fit(*found, value, error);
    if (!fitted) {
        return false;
    }
    LogicFile* file = d.logic_file(path);
    logic::LogicAsset& asset = file->asset;
    const Place place = find_step(asset, step);
    const Header h = place.list == nullptr ? find_header(asset, step) : Header{};
    // Names: valid, free, and followed by what refers to them.
    const auto* text = std::get_if<std::string>(&*fitted);
    if (place.list != nullptr && (*place.list)[place.index].kind == StmtKind::Run &&
        (text == nullptr ||
         std::none_of(asset.sequences.begin(), asset.sequences.end(),
                      [text](const logic::Sequence& q) { return q.name == *text; }))) {
        error = "There is no Sequence called " + (text != nullptr ? *text : std::string()) + ".";
        return false;
    }
    // A variable set: it must exist. A comparison's operator: one of the signs.
    Stmt* setting =
        place.list != nullptr && (*place.list)[place.index].kind == StmtKind::SetVariable
            ? &(*place.list)[place.index]
            : nullptr;
    const logic::Variable* chosen = setting != nullptr && argument == 1 && text != nullptr
                                        ? find_variable(asset, *text)
                                        : nullptr;
    if (setting != nullptr && argument == 1 && chosen == nullptr) {
        error = "There is no variable called " + (text != nullptr ? *text : std::string()) +
                " in this file.";
        return false;
    }
    logic::Expr* comparison = nullptr;
    if (place.list != nullptr && ((*place.list)[place.index].kind == StmtKind::If ||
                                  (*place.list)[place.index].kind == StmtKind::WaitUntil)) {
        logic::Expr& test = tested((*place.list)[place.index].arguments[0]);
        comparison = is_comparison(test) ? &test : nullptr;
    } else if (h.condition != nullptr && is_comparison(tested(*h.condition))) {
        comparison = &tested(*h.condition);
    }
    std::optional<logic::CompareOp> sign;
    if (comparison != nullptr && argument == 0) {
        sign = text != nullptr ? comparison_of(*text) : std::nullopt;
        if (!sign) {
            error = "Is must be one of = \u2260 < \u2264 > \u2265.";
            return false;
        }
    }
    const bool naming = place.list == nullptr && h.condition == nullptr &&
                        (h.transition == nullptr || argument == 0);
    if (naming && (text == nullptr || !reflect::is_valid_id(*text))) {
        error = h.transition != nullptr ? "There is no state called that in this Brain."
                                        : std::string(bad_name);
        return false;
    }
    if (naming && h.transition != nullptr &&
        std::none_of(h.brain->states.begin(), h.brain->states.end(),
                     [text](const logic::BrainState& st) { return st.name == *text; })) {
        error = "There is no state called " + *text + " in this Brain.";
        return false;
    }
    if (naming && h.transition == nullptr) {
        const bool taken =
            h.state != nullptr   ? std::any_of(h.brain->states.begin(), h.brain->states.end(),
                                               [&](const logic::BrainState& st) {
                                                 return &st != h.state && st.name == *text;
                                             })
            : h.brain != nullptr ? std::any_of(asset.brains.begin(), asset.brains.end(),
                                               [&](const logic::Brain& b) {
                                                   return &b != h.brain && b.name == *text;
                                               })
                                 : std::any_of(asset.sequences.begin(), asset.sequences.end(),
                                               [&](const logic::Sequence& q) {
                                                   return &q != h.sequence && q.name == *text;
                                               });
        if (taken) {
            error = "That name is already used here.";
            return false;
        }
    }
    engine::serialize::Json before = d.snapshot();
    if (comparison != nullptr) {
        if (sign) {
            comparison->compare = *sign;
        } else {
            write_value(comparison->operands[1], std::move(*fitted));
        }
    } else if (setting != nullptr && argument == 1) {
        // Another variable; a value of another kind starts again from its zero.
        const logic::Variable* was = find_variable(asset, setting->member);
        setting->member = chosen->id;
        if (was == nullptr || was->kind != chosen->kind) {
            setting->arguments.assign(1, logic::ir::literal(zero_of(chosen->kind)));
        }
    } else if (place.list != nullptr && (*place.list)[place.index].kind == StmtKind::Run) {
        (*place.list)[place.index].member = *text;
    } else if (place.list != nullptr && ((*place.list)[place.index].kind == StmtKind::If ||
                                         (*place.list)[place.index].kind == StmtKind::WaitUntil)) {
        write_value(tested((*place.list)[place.index].arguments[0]).operands[argument],
                    std::move(*fitted));
    } else if (place.list != nullptr) {
        write_value((*place.list)[place.index].arguments[argument], std::move(*fitted));
    } else if (h.condition != nullptr) {
        write_value(tested(*h.condition).operands[argument], std::move(*fitted));
    } else if (h.transition != nullptr) {
        if (argument == 0) {
            h.transition->to = *text;
        } else {
            h.transition->min_time = std::get<double>(*fitted);
        }
    } else if (h.state != nullptr) {
        const std::string old = h.state->name;
        h.state->name = *text;
        if (h.brain->initial == old) {
            h.brain->initial = *text;
        }
        for (logic::BrainState& st : h.brain->states) {
            for (logic::Transition& go : st.transitions) {
                go.to = go.to == old ? *text : go.to;
            }
        }
        for (logic::Transition& go : h.brain->from_any) {
            go.to = go.to == old ? *text : go.to;
        }
    } else if (h.brain != nullptr) {
        h.brain->name = *text;
    } else if (h.sequence != nullptr) {
        const std::string old = h.sequence->name;
        h.sequence->name = *text;
        for (std::vector<Stmt>* list : step_lists(asset)) {
            each_step(*list, [&](Stmt& st) {
                if (st.kind == StmtKind::Run && st.member == old) {
                    st.member = *text;
                }
            });
        }
    }
    d.record("Change " + found->label + " in " + std::string(path), std::move(before),
             engine::Guid{},
             "logic/" + std::string(path) + "/" + std::to_string(step.value) + "/" +
                 std::to_string(argument));
    return true;
}

std::vector<std::string> Document::logic_objects() const {
    const Impl& d = *impl_;
    std::set<std::string> objects(game_objects.begin(), game_objects.end());
    for (const world::Entity e : entities()) {
        const std::string_view name = d.world->name(e);
        if (!name.empty()) {
            objects.emplace(name);
        }
    }
    return {objects.begin(), objects.end()};
}

std::vector<LogicAction> Document::logic_functions(reflect::FunctionRole role) const {
    const Impl& d = *impl_;
    std::vector<LogicAction> found;
    for (const std::string& object : logic_objects()) {
        std::vector<LogicAction> mine;
        for (const reflect::TypeInfo* type : types_of(Lookup{d.registry, *d.world}, object)) {
            for (const reflect::FunctionInfo& f : type->functions) {
                if (f.role == role) {
                    mine.push_back(LogicAction{object, f.id, reflect::block_text(f)});
                }
            }
        }
        std::sort(mine.begin(), mine.end(),
                  [](const LogicAction& a, const LogicAction& b) { return a.text < b.text; });
        found.insert(found.end(), mine.begin(), mine.end());
    }
    return found;
}

std::vector<LogicAction> Document::logic_actions() const {
    // The built-in steps first (object ""), then the objects' actions.
    std::vector<LogicAction> actions{LogicAction{"", "log", "Log {text}"},
                                     LogicAction{"", "repeat", "Repeat {times} times"},
                                     LogicAction{"", "run", "Run {sequence} and wait for it"},
                                     LogicAction{"", "set_variable", "Set {variable} to {value}"},
                                     LogicAction{"", "wait", "Wait {seconds} seconds"}};
    std::vector<LogicAction> objects = logic_functions(reflect::FunctionRole::Action);
    actions.insert(actions.end(), objects.begin(), objects.end());
    return actions;
}

BlockId Document::add_logic_step(std::string_view path, BlockId after, const LogicAction& action,
                                 std::string& error, bool otherwise) {
    Impl& d = *impl_;
    LogicFile* file = d.logic_file(path);
    if (file == nullptr) {
        error = "There is no logic file " + std::string(path) + ".";
        return BlockId{};
    }
    const reflect::FunctionInfo* f =
        action.object.empty()
            ? nullptr
            : find_function(Lookup{d.registry, *d.world}, action.object, action.member);
    const bool built_in =
        action.object.empty() &&
        (action.member == "log" || action.member == "run" || action.member == "wait" ||
         action.member == "repeat" || action.member == "set_variable");
    if (built_in && action.member == "set_variable" && file->asset.variables.empty()) {
        error = "There is no variable to set yet: add one first.";
        return BlockId{};
    }
    if (!built_in && (f == nullptr || f->role != reflect::FunctionRole::Action)) {
        error = action.object + " cannot " + action.member + ".";
        return BlockId{};
    }
    if (built_in && action.member == "run" && file->asset.sequences.empty()) {
        error = "There is no Sequence to run yet: add one first.";
        return BlockId{};
    }
    Place place = where_to_add(file->asset, after);
    if (otherwise) {
        // First in the Otherwise of the If or the Rule.
        place = Place{};
        if (const Place step = find_step(file->asset, after);
            step.list != nullptr && (*step.list)[step.index].kind == StmtKind::If) {
            place = Place{&(*step.list)[step.index].otherwise, 0};
        } else if (const Header h = find_header(file->asset, after);
                   h.rule != nullptr && h.condition == nullptr) {
            place = Place{&h.rule->otherwise, 0};
        }
        if (place.list == nullptr) {
            error = "Only an If or a Rule has an Otherwise.";
            return BlockId{};
        }
    }
    std::vector<Stmt>* list = place.list;
    const std::size_t at = place.index;
    if (list == nullptr) {
        error = "Steps go under a Rule, a Sequence or a Brain state, or after another step.";
        return BlockId{};
    }
    const std::uint64_t next = next_id(file->asset).value;
    Stmt step;
    std::string label;
    if (built_in && action.member == "log") {
        step = logic::ir::log(next, logic::ir::literal(reflect::Value{std::string()}));
        label = "Log";
    } else if (built_in && action.member == "set_variable") {
        const logic::Variable& first = file->asset.variables.front();
        step = logic::ir::set_variable(next, first.id, logic::ir::literal(zero_of(first.kind)));
        label = "Set variable";
    } else if (built_in && action.member == "repeat") {
        step = logic::ir::repeat(next, logic::ir::literal(reflect::Value{std::int64_t{2}}), {});
        label = "Repeat";
    } else if (built_in && action.member == "run") {
        step = logic::ir::run(next, file->asset.sequences.front().name);
        label = "Run";
    } else if (built_in) {
        step = logic::ir::wait(next, logic::ir::literal(reflect::Value{1.0}));
        label = "Wait";
    } else {
        step.kind = StmtKind::Do;
        step.id = BlockId{next};
        step.object = action.object;
        step.member = action.member;
        step.arguments = starting_operands(*f);
        label = f->meta.display_name;
    }
    const BlockId id = step.id;
    engine::serialize::Json before = d.snapshot();
    list->insert(list->begin() + static_cast<std::ptrdiff_t>(at), std::move(step));
    d.record("Add " + label + " step", std::move(before));
    return id;
}

bool Document::remove_logic_step(std::string_view path, BlockId step) {
    Impl& d = *impl_;
    LogicFile* file = d.logic_file(path);
    const Place place = file != nullptr ? find_step(file->asset, step) : Place{};
    if (place.list == nullptr) {
        return false;
    }
    engine::serialize::Json before = d.snapshot();
    place.list->erase(place.list->begin() + static_cast<std::ptrdiff_t>(place.index));
    d.record("Delete a step", std::move(before));
    return true;
}

bool Document::move_logic_step(std::string_view path, BlockId step, int by) {
    Impl& d = *impl_;
    LogicFile* file = d.logic_file(path);
    const Place place = file != nullptr ? find_step(file->asset, step) : Place{};
    if (place.list == nullptr || (by != -1 && by != 1)) {
        return false;
    }
    if ((by < 0 && place.index == 0) || (by > 0 && place.index + 1 >= place.list->size())) {
        return false; // already first or last
    }
    engine::serialize::Json before = d.snapshot();
    const std::size_t other = by < 0 ? place.index - 1 : place.index + 1;
    std::swap((*place.list)[place.index], (*place.list)[other]);
    d.record(by < 0 ? "Move a step up" : "Move a step down", std::move(before));
    return true;
}

bool Document::move_logic_step_to(std::string_view path, BlockId step, BlockId target,
                                  std::string& error) {
    Impl& d = *impl_;
    LogicFile* file = d.logic_file(path);
    const Place from = file != nullptr ? find_step(file->asset, step) : Place{};
    if (from.list == nullptr) {
        error = "Only steps can be moved.";
        return false;
    }
    if (target == step) {
        return true; // where it is
    }
    // Not into itself: a step inside the one moved cannot be its new place.
    Stmt& moved = (*from.list)[from.index];
    if (find_in(moved.body, target).list != nullptr ||
        find_in(moved.otherwise, target).list != nullptr) {
        error = "A step cannot go inside itself.";
        return false;
    }
    if (where_to_add(file->asset, target).list == nullptr) {
        error = "Steps go under a Rule, a Sequence or a Brain state, or after another step.";
        return false;
    }
    engine::serialize::Json before = d.snapshot();
    Stmt taken = std::move(moved);
    from.list->erase(from.list->begin() + static_cast<std::ptrdiff_t>(from.index));
    // Found again: taking the step out may have moved where it goes.
    const Place to = where_to_add(file->asset, target);
    if (to.list == nullptr) {
        // Not expected (the target is not inside what moved), but never lose the step.
        from.list->insert(from.list->begin() + static_cast<std::ptrdiff_t>(from.index),
                          std::move(taken));
        error = "That step cannot go there.";
        return false;
    }
    to.list->insert(to.list->begin() + static_cast<std::ptrdiff_t>(to.index), std::move(taken));
    d.record("Move a step", std::move(before));
    return true;
}

bool Document::is_logic_step(std::string_view path, BlockId block) const {
    LogicFile* file = impl_->logic_file(path);
    return file != nullptr && find_step(file->asset, block).list != nullptr;
}

BlockId Document::add_logic_branch(std::string_view path, BlockId after,
                                   const LogicAction& condition, bool wait_until,
                                   std::string& error) {
    Impl& d = *impl_;
    LogicFile* file = d.logic_file(path);
    if (file == nullptr) {
        error = "There is no logic file " + std::string(path) + ".";
        return BlockId{};
    }
    const reflect::FunctionInfo* f =
        find_function(Lookup{d.registry, *d.world}, condition.object, condition.member);
    if (f == nullptr || f->role != reflect::FunctionRole::Condition) {
        error = condition.object + " has no condition " + condition.member + ".";
        return BlockId{};
    }
    const Place place = where_to_add(file->asset, after);
    if (place.list == nullptr) {
        error = "Steps go under a Rule, a Sequence or a Brain state, or after another step.";
        return BlockId{};
    }
    const std::uint64_t next = next_id(file->asset).value;
    logic::Expr test = logic::ir::call(condition.object, condition.member, starting_operands(*f));
    Stmt step = wait_until ? logic::ir::wait_until(next, std::move(test))
                           : logic::ir::if_then(next, std::move(test), {}, {});
    engine::serialize::Json before = d.snapshot();
    place.list->insert(place.list->begin() + static_cast<std::ptrdiff_t>(place.index),
                       std::move(step));
    d.record(wait_until ? "Add a Wait until step" : "Add an If step", std::move(before));
    return BlockId{next};
}

bool Document::add_logic_variable(std::string_view path, reflect::ValueKind kind,
                                  std::string& error) {
    Impl& d = *impl_;
    LogicFile* file = d.logic_file(path);
    if (file == nullptr) {
        error = "There is no logic file " + std::string(path) + ".";
        return false;
    }
    if (kind == reflect::ValueKind::Choice) {
        error = "A variable holds yes/no, a whole number, a number or text.";
        return false;
    }
    logic::Variable v;
    v.id = free_name("variable", [file](const std::string& name) {
        return find_variable(file->asset, name) != nullptr;
    });
    v.kind = kind;
    v.initial = zero_of(kind);
    engine::serialize::Json before = d.snapshot();
    file->asset.variables.push_back(std::move(v));
    d.record("Add a variable", std::move(before));
    return true;
}

bool Document::rename_logic_variable(std::string_view path, std::string_view name,
                                     std::string_view new_name, std::string& error) {
    Impl& d = *impl_;
    LogicFile* file = d.logic_file(path);
    if (file == nullptr || find_variable(file->asset, name) == nullptr) {
        error = "There is no variable " + std::string(name) + ".";
        return false;
    }
    if (!reflect::is_valid_id(new_name)) {
        error = std::string(bad_name);
        return false;
    }
    if (new_name != name && find_variable(file->asset, new_name) != nullptr) {
        error = "That name is already used here.";
        return false;
    }
    const std::string old(name); // `name` may point into what is renamed
    const std::string renamed(new_name);
    engine::serialize::Json before = d.snapshot();
    for (logic::Variable& v : file->asset.variables) {
        if (v.id == old) {
            v.id = renamed;
        }
    }
    // Everything that reads or sets it follows.
    each_expr(file->asset, [&](logic::Expr& e) {
        if (e.kind == logic::ExprKind::Variable && e.member == old) {
            e.member = renamed;
        }
    });
    for (std::vector<Stmt>* list : step_lists(file->asset)) {
        each_step(*list, [&](Stmt& s) {
            if (s.kind == StmtKind::SetVariable && s.member == old) {
                s.member = renamed;
            }
        });
    }
    d.record("Rename a variable", std::move(before), engine::Guid{},
             "logic/" + std::string(path) + "/variable");
    return true;
}

bool Document::set_logic_variable_start(std::string_view path, std::string_view name,
                                        const reflect::Value& value, std::string& error) {
    Impl& d = *impl_;
    LogicFile* file = d.logic_file(path);
    const logic::Variable* v = file != nullptr ? find_variable(file->asset, name) : nullptr;
    if (v == nullptr) {
        error = "There is no variable " + std::string(name) + ".";
        return false;
    }
    LogicSlot start;
    start.label = "Its start";
    start.kind = v->kind;
    std::optional<reflect::Value> fitted = fit(start, value, error);
    if (!fitted) {
        return false;
    }
    const std::string key = "logic/" + std::string(path) + "/variable/" + std::string(name);
    engine::serialize::Json before = d.snapshot();
    for (logic::Variable& each : file->asset.variables) {
        if (each.id == name) {
            each.initial = std::move(*fitted);
        }
    }
    d.record("Change where a variable starts", std::move(before), engine::Guid{}, key);
    return true;
}

bool Document::remove_logic_variable(std::string_view path, std::string_view name,
                                     std::string& error) {
    Impl& d = *impl_;
    LogicFile* file = d.logic_file(path);
    if (file == nullptr || find_variable(file->asset, name) == nullptr) {
        error = "There is no variable " + std::string(name) + ".";
        return false;
    }
    bool used = false;
    each_expr(file->asset, [&](logic::Expr& e) {
        used = used || (e.kind == logic::ExprKind::Variable && e.member == name);
    });
    for (std::vector<Stmt>* list : step_lists(file->asset)) {
        each_step(*list, [&](Stmt& s) {
            used = used || (s.kind == StmtKind::SetVariable && s.member == name);
        });
    }
    if (used) {
        error = std::string(name) + " is still used: delete the blocks that use it first.";
        return false;
    }
    const std::string gone(name);
    engine::serialize::Json before = d.snapshot();
    std::erase_if(file->asset.variables,
                  [&gone](const logic::Variable& v) { return v.id == gone; });
    d.record("Delete a variable", std::move(before));
    return true;
}

BlockId Document::add_logic_comparison(std::string_view path, BlockId block,
                                       std::string_view variable, std::string& error) {
    LogicFile* file = impl_->logic_file(path);
    const logic::Variable* v = file != nullptr ? find_variable(file->asset, variable) : nullptr;
    if (v == nullptr) {
        error = "There is no variable " + std::string(variable) + ".";
        return BlockId{};
    }
    return add_logic_comparison(
        path, block,
        LogicSource{LogicSource::Kind::Variable, "", std::string(variable), v->id, v->kind}, error);
}

BlockId Document::add_logic_comparison(std::string_view path, BlockId block,
                                       const LogicSource& left, std::string& error) {
    Impl& d = *impl_;
    LogicFile* file = d.logic_file(path);
    const std::vector<LogicSource> comparable = logic_comparables(path, block);
    const auto found =
        std::find_if(comparable.begin(), comparable.end(), [&](const LogicSource& s) {
            return s.kind == left.kind && s.object == left.object && s.member == left.member;
        });
    if (file == nullptr || found == comparable.end()) {
        error = left.member + " cannot be compared here.";
        return BlockId{};
    }
    const BlockId id = next_id(file->asset);
    logic::Expr value = found->kind == LogicSource::Kind::Variable
                            ? logic::ir::variable(found->member)
                        : found->kind == LogicSource::Kind::Property
                            ? logic::ir::property(found->object, found->member)
                            : logic::ir::event_value(found->member);
    logic::Expr test = logic::ir::compare(logic::CompareOp::Equal, std::move(value),
                                          logic::ir::literal(zero_of(found->value)));
    // On a Rule or a "Go to" (or one of their conditions): a condition of it.
    const Header h = find_header(file->asset, block);
    std::vector<logic::Expr>* list = h.transition != nullptr ? &h.transition->conditions
                                     : h.rule != nullptr     ? &h.rule->conditions
                                                             : nullptr;
    if (list != nullptr && list->size() == 1 && list->front().kind == logic::ExprKind::Any) {
        list = &list->front().operands;
    }
    if (list != nullptr) {
        engine::serialize::Json before = d.snapshot();
        test.id = id;
        list->push_back(std::move(test));
        d.record("Add a condition", std::move(before));
        return id;
    }
    // Anywhere else steps go: an If step testing it.
    const Place place = where_to_add(file->asset, block);
    if (place.list == nullptr) {
        error = "Steps go under a Rule, a Sequence or a Brain state, or after another step.";
        return BlockId{};
    }
    engine::serialize::Json before = d.snapshot();
    place.list->insert(place.list->begin() + static_cast<std::ptrdiff_t>(place.index),
                       logic::ir::if_then(id.value, std::move(test), {}, {}));
    d.record("Add an If step", std::move(before));
    return id;
}

std::vector<LogicSource> Document::logic_sources(std::string_view path, BlockId block,
                                                 std::size_t argument) const {
    LogicFile* file = impl_->logic_file(path);
    const std::vector<LogicSlot> slots = logic_slots(path, block);
    const auto slot = std::find_if(slots.begin(), slots.end(), [argument](const LogicSlot& s) {
        return s.argument == argument;
    });
    if (file == nullptr || slot == slots.end() ||
        slot_target(file->asset, block, argument) == nullptr) {
        return {};
    }
    std::vector<LogicSource> sources{
        LogicSource{LogicSource::Kind::Written, "", "", "A value written here", slot->kind}};
    std::vector<LogicSource> others = sources_of(path, block, slot->kind);
    sources.insert(sources.end(), others.begin(), others.end());
    return sources;
}

std::vector<LogicSource> Document::logic_comparables(std::string_view path, BlockId block) const {
    std::vector<LogicSource> all = sources_of(path, block, std::nullopt);
    std::erase_if(all, [](const LogicSource& s) { return s.value == reflect::ValueKind::Choice; });
    return all;
}

std::vector<LogicSource> Document::sources_of(std::string_view path, BlockId block,
                                              std::optional<reflect::ValueKind> wanted) const {
    const Impl& d = *impl_;
    LogicFile* file = impl_->logic_file(path);
    std::vector<LogicSource> sources;
    if (file == nullptr) {
        return sources;
    }
    const auto fits = [&wanted](reflect::ValueKind kind) { return !wanted || *wanted == kind; };
    for (const logic::Variable& v : file->asset.variables) {
        if (fits(v.kind)) {
            sources.push_back(LogicSource{LogicSource::Kind::Variable, "", v.id, v.id, v.kind});
        }
    }
    // The values the Rule's event carries, for blocks inside a Rule.
    if (const logic::Rule* rule = rule_of(file->asset, block);
        rule != nullptr && !rule->object.empty()) {
        for (const reflect::TypeInfo* type : types_of(Lookup{d.registry, *d.world}, rule->object)) {
            if (const reflect::EventInfo* event = type->find_event(rule->event)) {
                for (const reflect::ParameterInfo& p : event->parameters) {
                    if (fits(p.kind)) {
                        sources.push_back(LogicSource{LogicSource::Kind::EventValue, "", p.id,
                                                      "[" + p.id + "] (the event's)", p.kind});
                    }
                }
            }
        }
    }
    // Properties of what logic can name.
    for (const std::string& object : logic_objects()) {
        for (const reflect::TypeInfo* type : types_of(Lookup{d.registry, *d.world}, object)) {
            for (const reflect::PropertyInfo& p : type->properties) {
                if (fits(p.kind) && !reflect::has_flag(p.flags, reflect::property_flag::hidden)) {
                    sources.push_back(LogicSource{LogicSource::Kind::Property, object, p.id,
                                                  p.meta.display_name + " of " + object, p.kind});
                }
            }
        }
    }
    return sources;
}

bool Document::set_logic_source(std::string_view path, BlockId block, std::size_t argument,
                                const LogicSource& source, std::string& error) {
    Impl& d = *impl_;
    const std::vector<LogicSource> offered = logic_sources(path, block, argument);
    const bool fits = std::any_of(offered.begin(), offered.end(), [&](const LogicSource& s) {
        return s.kind == source.kind && s.object == source.object && s.member == source.member;
    });
    if (!fits) {
        error = "That cannot give this value.";
        return false;
    }
    const std::vector<LogicSlot> slots = logic_slots(path, block);
    const auto slot = std::find_if(slots.begin(), slots.end(), [argument](const LogicSlot& s) {
        return s.argument == argument;
    });
    LogicFile* file = d.logic_file(path);
    logic::Expr* value = slot_target(file->asset, block, argument);
    engine::serialize::Json before = d.snapshot();
    const BlockId id = value->id;
    switch (source.kind) {
    case LogicSource::Kind::Written:
        if (slot->typed) {
            return true; // written already
        }
        *value = logic::ir::literal(slot->choice != nullptr && !slot->choice->entries.empty()
                                        ? reflect::Value{slot->choice->entries.front().id}
                                        : zero_of(slot->kind));
        break;
    case LogicSource::Kind::Variable:
        *value = logic::ir::variable(source.member);
        break;
    case LogicSource::Kind::EventValue:
        *value = logic::ir::event_value(source.member);
        break;
    case LogicSource::Kind::Property:
        *value = logic::ir::property(source.object, source.member);
        break;
    }
    value->id = id;
    d.record("Change where a value comes from", std::move(before));
    return true;
}

bool Document::set_logic_arithmetic(std::string_view path, BlockId block, std::size_t argument,
                                    std::optional<logic::ArithmeticOp> op, std::string& error) {
    Impl& d = *impl_;
    LogicFile* file = d.logic_file(path);
    logic::Expr* value = file != nullptr ? slot_target(file->asset, block, argument) : nullptr;
    const std::vector<LogicSlot> slots = logic_slots(path, block);
    const auto slot = std::find_if(slots.begin(), slots.end(), [argument](const LogicSlot& s) {
        return s.argument == argument;
    });
    if (value == nullptr || slot == slots.end()) {
        error = "That block has no such value.";
        return false;
    }
    if (slot->kind != reflect::ValueKind::Int && slot->kind != reflect::ValueKind::Float) {
        error = "Only numbers can be added to.";
        return false;
    }
    const bool arithmetic = value->kind == logic::ExprKind::Arithmetic;
    if (!arithmetic && value->kind == logic::ExprKind::Literal) {
        error = "Take the value from somewhere first: a written number is just written.";
        return false;
    }
    engine::serialize::Json before = d.snapshot();
    const BlockId id = value->id;
    if (!op) {
        if (!arithmetic) {
            return true;
        }
        logic::Expr base = std::move(value->operands[0]);
        *value = std::move(base);
    } else if (arithmetic) {
        value->arithmetic = *op;
    } else {
        logic::Expr base = std::move(*value);
        base.id = BlockId{};
        *value = logic::ir::arithmetic(*op, std::move(base),
                                       logic::ir::literal(slot->kind == reflect::ValueKind::Int
                                                              ? reflect::Value{std::int64_t{1}}
                                                              : reflect::Value{1.0}));
    }
    value->id = id;
    d.record("Change a value's sum", std::move(before));
    return true;
}

bool Document::is_logic_test(std::string_view path, BlockId block) const {
    LogicFile* file = impl_->logic_file(path);
    if (file == nullptr) {
        return false;
    }
    if (const Place place = find_step(file->asset, block); place.list != nullptr) {
        const Stmt& s = (*place.list)[place.index];
        return (s.kind == StmtKind::If || s.kind == StmtKind::WaitUntil) && !s.arguments.empty();
    }
    return find_header(file->asset, block).condition != nullptr;
}

bool Document::toggle_logic_not(std::string_view path, BlockId block) {
    Impl& d = *impl_;
    if (!is_logic_test(path, block)) {
        return false;
    }
    LogicFile* file = d.logic_file(path);
    engine::serialize::Json before = d.snapshot();
    if (const Place place = find_step(file->asset, block); place.list != nullptr) {
        flip((*place.list)[place.index].arguments[0]);
    } else {
        flip(*find_header(file->asset, block).condition);
    }
    d.record("Turn a condition around", std::move(before));
    return true;
}

std::optional<bool> Document::logic_any(std::string_view path, BlockId rule) const {
    LogicFile* file = impl_->logic_file(path);
    const Header h = file != nullptr ? find_header(file->asset, rule) : Header{};
    if (h.rule == nullptr || h.condition != nullptr) {
        return std::nullopt;
    }
    return h.rule->conditions.size() == 1 &&
           h.rule->conditions.front().kind == logic::ExprKind::Any;
}

bool Document::set_logic_any(std::string_view path, BlockId rule, bool any, std::string& error) {
    Impl& d = *impl_;
    const std::optional<bool> now = logic_any(path, rule);
    if (!now) {
        error = "Only a Rule has conditions to match.";
        return false;
    }
    if (*now == any) {
        return true;
    }
    LogicFile* file = d.logic_file(path);
    std::vector<logic::Expr>& conditions = find_header(file->asset, rule).rule->conditions;
    if (any && conditions.size() < 2) {
        error = "A Rule needs two conditions or more to match any of them.";
        return false;
    }
    engine::serialize::Json before = d.snapshot();
    if (any) {
        // Its id first, while the conditions' ids are still in the asset to be seen.
        const BlockId id = next_id(file->asset);
        logic::Expr group = logic::ir::any(std::move(conditions));
        group.id = id;
        conditions = {std::move(group)};
    } else {
        std::vector<logic::Expr> each = std::move(conditions.front().operands);
        conditions = std::move(each);
    }
    d.record(any ? "Match any condition" : "Match all conditions", std::move(before));
    return true;
}

std::vector<LogicEvent> Document::logic_events() const {
    const Impl& d = *impl_;
    std::vector<LogicEvent> events{LogicEvent{"", "game_start", "When the game starts"}};
    for (const std::string& object : logic_objects()) {
        std::vector<LogicEvent> mine;
        for (const reflect::TypeInfo* type : types_of(Lookup{d.registry, *d.world}, object)) {
            for (const reflect::EventInfo& e : type->events) {
                mine.push_back(LogicEvent{object, e.id, reflect::block_text(e)});
            }
        }
        std::sort(mine.begin(), mine.end(),
                  [](const LogicEvent& a, const LogicEvent& b) { return a.text < b.text; });
        events.insert(events.end(), mine.begin(), mine.end());
    }
    return events;
}

std::vector<LogicAction> Document::logic_conditions() const {
    return logic_functions(reflect::FunctionRole::Condition);
}

BlockId Document::add_logic_rule(std::string_view path, const LogicEvent& event,
                                 std::string& error) {
    Impl& d = *impl_;
    LogicFile* file = d.logic_file(path);
    if (file == nullptr) {
        error = "There is no logic file " + std::string(path) + ".";
        return BlockId{};
    }
    const bool game_wide = event.object.empty() && event.event == "game_start";
    bool known = game_wide;
    for (const reflect::TypeInfo* type : types_of(Lookup{d.registry, *d.world}, event.object)) {
        known = known || type->find_event(event.event) != nullptr;
    }
    if (!known) {
        error = (event.object.empty() ? std::string("The game") : event.object) + " has no event " +
                event.event + ".";
        return BlockId{};
    }
    logic::Rule rule;
    rule.id = next_id(file->asset);
    rule.object = event.object;
    rule.event = event.event;
    engine::serialize::Json before = d.snapshot();
    file->asset.rules.push_back(rule);
    d.record("Add a Rule", std::move(before));
    return rule.id;
}

BlockId Document::add_logic_condition(std::string_view path, BlockId block,
                                      const LogicAction& condition, std::string& error) {
    Impl& d = *impl_;
    LogicFile* file = d.logic_file(path);
    const Header h = file != nullptr ? find_header(file->asset, block) : Header{};
    std::vector<logic::Expr>* list = h.transition != nullptr ? &h.transition->conditions
                                     : h.rule != nullptr     ? &h.rule->conditions
                                                             : nullptr;
    // A Rule matching any of its conditions keeps them in one group: add to it.
    if (list != nullptr && list->size() == 1 && list->front().kind == logic::ExprKind::Any) {
        list = &list->front().operands;
    }
    if (list == nullptr) {
        error = "Conditions go on a Rule or a “Go to”.";
        return BlockId{};
    }
    const reflect::FunctionInfo* f =
        find_function(Lookup{d.registry, *d.world}, condition.object, condition.member);
    if (f == nullptr || f->role != reflect::FunctionRole::Condition) {
        error = condition.object + " has no condition " + condition.member + ".";
        return BlockId{};
    }
    logic::Expr e = logic::ir::call(condition.object, condition.member, starting_operands(*f));
    e.id = next_id(file->asset);
    const BlockId id = e.id;
    engine::serialize::Json before = d.snapshot();
    list->push_back(std::move(e));
    d.record("Add a condition", std::move(before));
    return id;
}

BlockId Document::add_logic_sequence(std::string_view path, std::string& error) {
    Impl& d = *impl_;
    LogicFile* file = d.logic_file(path);
    if (file == nullptr) {
        error = "There is no logic file " + std::string(path) + ".";
        return BlockId{};
    }
    logic::Sequence sequence;
    sequence.id = next_id(file->asset);
    sequence.name = free_name("sequence", [file](const std::string& name) {
        return std::any_of(file->asset.sequences.begin(), file->asset.sequences.end(),
                           [&name](const logic::Sequence& q) { return q.name == name; });
    });
    engine::serialize::Json before = d.snapshot();
    file->asset.sequences.push_back(sequence);
    d.record("Add a Sequence", std::move(before));
    return sequence.id;
}

BlockId Document::add_logic_brain(std::string_view path, std::string& error) {
    Impl& d = *impl_;
    LogicFile* file = d.logic_file(path);
    if (file == nullptr) {
        error = "There is no logic file " + std::string(path) + ".";
        return BlockId{};
    }
    logic::Brain brain;
    brain.id = next_id(file->asset);
    brain.name = free_name("brain", [file](const std::string& name) {
        return std::any_of(file->asset.brains.begin(), file->asset.brains.end(),
                           [&name](const logic::Brain& b) { return b.name == name; });
    });
    logic::BrainState idle;
    idle.id = BlockId{brain.id.value + 1};
    idle.name = "idle";
    brain.states.push_back(idle);
    engine::serialize::Json before = d.snapshot();
    file->asset.brains.push_back(brain);
    d.record("Add a Brain", std::move(before));
    return brain.id;
}

BlockId Document::add_logic_state(std::string_view path, BlockId block, std::string& error) {
    Impl& d = *impl_;
    LogicFile* file = d.logic_file(path);
    const Header h = file != nullptr ? find_header(file->asset, block) : Header{};
    if (h.brain == nullptr) {
        error = "States go in a Brain.";
        return BlockId{};
    }
    logic::BrainState state;
    state.id = next_id(file->asset);
    state.name = free_name("state", [&h](const std::string& name) {
        return std::any_of(h.brain->states.begin(), h.brain->states.end(),
                           [&name](const logic::BrainState& st) { return st.name == name; });
    });
    engine::serialize::Json before = d.snapshot();
    h.brain->states.push_back(state);
    d.record("Add a state", std::move(before));
    return state.id;
}

BlockId Document::add_logic_transition(std::string_view path, BlockId block, std::string& error) {
    Impl& d = *impl_;
    LogicFile* file = d.logic_file(path);
    const Header h = file != nullptr ? find_header(file->asset, block) : Header{};
    if (h.state == nullptr) {
        error = "A “Go to” leads out of a state.";
        return BlockId{};
    }
    // Towards the state after this one, or the first.
    const auto here = static_cast<std::size_t>(h.state - h.brain->states.data());
    logic::Transition go;
    go.id = next_id(file->asset);
    go.to = h.brain->states[(here + 1) % h.brain->states.size()].name;
    go.min_time = 1.0;
    engine::serialize::Json before = d.snapshot();
    h.state->transitions.push_back(go);
    d.record("Add a Go to", std::move(before));
    return go.id;
}

bool Document::remove_logic_block(std::string_view path, BlockId block) {
    Impl& d = *impl_;
    LogicFile* file = d.logic_file(path);
    if (file == nullptr) {
        return false;
    }
    if (is_logic_step(path, block)) {
        return remove_logic_step(path, block);
    }
    const Header h = find_header(file->asset, block);
    if (!h.found()) {
        return false;
    }
    const auto at = static_cast<std::ptrdiff_t>(h.index);
    engine::serialize::Json before = d.snapshot();
    std::string what;
    if (h.condition != nullptr) {
        h.conditions->erase(h.conditions->begin() + at);
        // A group of one is just that condition.
        if (h.group != nullptr && h.group->operands.size() == 1) {
            logic::Expr last = std::move(h.group->operands[0]);
            *h.group = std::move(last);
        }
        what = "condition";
    } else if (h.transition != nullptr) {
        h.transitions->erase(h.transitions->begin() + at);
        what = "Go to";
    } else if (h.state != nullptr) {
        h.brain->states.erase(h.brain->states.begin() + at);
        what = "state";
    } else if (h.brain != nullptr) {
        file->asset.brains.erase(file->asset.brains.begin() + at);
        what = "Brain";
    } else if (h.sequence != nullptr) {
        file->asset.sequences.erase(file->asset.sequences.begin() + at);
        what = "Sequence";
    } else {
        file->asset.rules.erase(file->asset.rules.begin() + at);
        what = "Rule";
    }
    d.record("Delete a " + what, std::move(before));
    return true;
}

bool Document::is_logic_block(std::string_view path, BlockId block) const {
    LogicFile* file = impl_->logic_file(path);
    return file != nullptr && (find_step(file->asset, block).list != nullptr ||
                               find_header(file->asset, block).found());
}

bool Document::create_logic_file(std::string_view path, std::string& error) {
    Impl& d = *impl_;
    const fs::path file(path);
    const std::string stem = file.stem().string();
    if (file.extension() != ".drules" || !reflect::is_valid_id(stem) || file.is_absolute() ||
        std::any_of(file.begin(), file.end(), [](const fs::path& part) { return part == ".."; })) {
        error = "A logic file is named like logic/quests.drules: its name of lower-case "
                "letters, digits and _, in assets/.";
        return false;
    }
    std::error_code ignored;
    if (d.logic_file(path) != nullptr || d.assets->find(path) != nullptr ||
        fs::exists(d.folder / "assets" / file, ignored)) {
        error = "There is already a file " + std::string(path) + ".";
        return false;
    }
    engine::serialize::Json before = d.snapshot();
    LogicFile created{std::string(path), logic::LogicAsset{}};
    created.asset.name = stem;
    d.logic.push_back(std::move(created));
    std::sort(d.logic.begin(), d.logic.end(),
              [](const LogicFile& a, const LogicFile& b) { return a.path < b.path; });
    d.record("Create " + std::string(path), std::move(before));
    return true;
}

std::vector<logic::Diagnostic> Document::check_logic(std::string_view path) const {
    const Impl& d = *impl_;
    const LogicFile* file = d.logic_file(path);
    if (file == nullptr) {
        return {logic::Diagnostic{BlockId{}, "There is no logic file " + std::string(path) + "."}};
    }
    const Lookup look{d.registry, *d.world};
    const logic::ObjectTypes objects = [&look](std::string_view object) {
        return types_of(look, object);
    };
    return logic::compile(file->asset, d.registry, objects).errors;
}

} // namespace editor
