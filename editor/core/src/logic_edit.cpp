// Editing logic in the Document: the values written into steps, and adding, deleting and
// moving steps. Every edit is a step of the Document's undo, and checked by the compiler.

#include "engine/foundation/text.h"
#include "engine/game/logic_world.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <set>
#include <utility>
#include <variant>

#include "document_impl.h"
#include "editor/core/document.h"

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

} // namespace

std::vector<LogicSlot> Document::header_slots(logic::LogicAsset& asset, BlockId block) const {
    const Impl& d = *impl_;
    const Header h = find_header(asset, block);
    std::vector<LogicSlot> slots;
    if (h.condition != nullptr) {
        const logic::Expr& c = tested(*h.condition);
        const reflect::FunctionInfo* f =
            c.kind == logic::ExprKind::Call
                ? find_function(Lookup{d.registry, *d.world}, c.object, c.member)
                : nullptr;
        if (f != nullptr) {
            for (std::size_t i = 0; i < f->parameters.size() && i < c.operands.size(); ++i) {
                if (c.operands[i].kind != logic::ExprKind::Literal) {
                    continue;
                }
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

std::vector<LogicSlot> Document::logic_slots(std::string_view path, BlockId step) const {
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
    const auto literal = [&s](std::size_t i) {
        return i < s.arguments.size() && s.arguments[i].kind == logic::ExprKind::Literal;
    };
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
        // The values of its condition, when it is one of an object's conditions.
        if (!s.arguments.empty() && tested(s.arguments[0]).kind == logic::ExprKind::Call) {
            const logic::Expr& c = tested(s.arguments[0]);
            if (const reflect::FunctionInfo* f =
                    find_function(Lookup{d.registry, *d.world}, c.object, c.member)) {
                for (std::size_t i = 0; i < f->parameters.size() && i < c.operands.size(); ++i) {
                    if (c.operands[i].kind != logic::ExprKind::Literal) {
                        continue;
                    }
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
    if (place.list != nullptr && (*place.list)[place.index].kind == StmtKind::Run) {
        (*place.list)[place.index].member = *text;
    } else if (place.list != nullptr && ((*place.list)[place.index].kind == StmtKind::If ||
                                         (*place.list)[place.index].kind == StmtKind::WaitUntil)) {
        tested((*place.list)[place.index].arguments[0]).operands[argument].literal =
            std::move(*fitted);
    } else if (place.list != nullptr) {
        (*place.list)[place.index].arguments[argument].literal = std::move(*fitted);
    } else if (h.condition != nullptr) {
        tested(*h.condition).operands[argument].literal = std::move(*fitted);
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
        action.object.empty() && (action.member == "log" || action.member == "run" ||
                                  action.member == "wait" || action.member == "repeat");
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
