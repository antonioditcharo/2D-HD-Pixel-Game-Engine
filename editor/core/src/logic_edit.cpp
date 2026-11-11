// Editing logic in the Document: the values written into steps, and adding, deleting and
// moving steps. Every edit is a step of the Document's undo, and checked by the compiler.

#include "engine/foundation/text.h"
#include "engine/game/logic_world.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

} // namespace

std::vector<LogicSlot> Document::logic_slots(std::string_view path, BlockId step) const {
    Impl& d = *impl_;
    LogicFile* file = d.logic_file(path);
    const Place place = file != nullptr ? find_step(file->asset, step) : Place{};
    if (place.list == nullptr) {
        return {};
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
    case StmtKind::If:
    case StmtKind::WaitUntil:
    case StmtKind::Run:
        break; // conditions and sequence names are not values typed in
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
        error = "That step has no such value to change.";
        return false;
    }
    std::optional<reflect::Value> fitted = fit(*found, value, error);
    if (!fitted) {
        return false;
    }
    LogicFile* file = d.logic_file(path);
    const Place place = find_step(file->asset, step);
    engine::serialize::Json before = d.snapshot();
    (*place.list)[place.index].arguments[argument].literal = std::move(*fitted);
    d.record("Change " + found->label + " in " + std::string(path), std::move(before),
             engine::Guid{},
             "logic/" + std::string(path) + "/" + std::to_string(step.value) + "/" +
                 std::to_string(argument));
    return true;
}

std::vector<LogicAction> Document::logic_actions() const {
    const Impl& d = *impl_;
    std::set<std::string> objects(game_objects.begin(), game_objects.end());
    for (const world::Entity e : entities()) {
        const std::string_view name = d.world->name(e);
        if (!name.empty()) {
            objects.emplace(name);
        }
    }
    std::vector<LogicAction> actions;
    for (const std::string& object : objects) {
        std::vector<LogicAction> mine;
        for (const reflect::TypeInfo* type : types_of(Lookup{d.registry, *d.world}, object)) {
            for (const reflect::FunctionInfo& f : type->functions) {
                if (f.role == reflect::FunctionRole::Action) {
                    mine.push_back(LogicAction{object, f.id, reflect::block_text(f)});
                }
            }
        }
        std::sort(mine.begin(), mine.end(),
                  [](const LogicAction& a, const LogicAction& b) { return a.text < b.text; });
        actions.insert(actions.end(), mine.begin(), mine.end());
    }
    return actions;
}

BlockId Document::add_logic_step(std::string_view path, BlockId after, const LogicAction& action,
                                 std::string& error) {
    Impl& d = *impl_;
    LogicFile* file = d.logic_file(path);
    if (file == nullptr) {
        error = "There is no logic file " + std::string(path) + ".";
        return BlockId{};
    }
    const reflect::FunctionInfo* f =
        find_function(Lookup{d.registry, *d.world}, action.object, action.member);
    if (f == nullptr || f->role != reflect::FunctionRole::Action) {
        error = action.object + " cannot " + action.member + ".";
        return BlockId{};
    }
    std::vector<Stmt>* list = nullptr;
    std::size_t at = 0;
    if (const Place place = find_step(file->asset, after); place.list != nullptr) {
        list = place.list;
        at = place.index + 1;
    } else {
        list = header_list(file->asset, after);
    }
    if (list == nullptr) {
        error = "Steps go under a Rule, a Sequence or a Brain state, or after another step.";
        return BlockId{};
    }
    Stmt step;
    step.kind = StmtKind::Do;
    step.id = next_id(file->asset);
    step.object = action.object;
    step.member = action.member;
    for (const reflect::ParameterInfo& p : f->parameters) {
        step.arguments.push_back(logic::ir::literal(starting_value(p)));
    }
    const BlockId id = step.id;
    engine::serialize::Json before = d.snapshot();
    list->insert(list->begin() + static_cast<std::ptrdiff_t>(at), std::move(step));
    d.record("Add " + f->meta.display_name + " step", std::move(before));
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
