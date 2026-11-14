#include "engine/foundation/reflect.h"

#include <algorithm>
#include <cmath>

namespace engine::reflect {

namespace {

[[nodiscard]] bool is_lower_letter(char c) noexcept {
    return c >= 'a' && c <= 'z';
}

[[nodiscard]] bool is_digit(char c) noexcept {
    return c >= '0' && c <= '9';
}

/// Checks a value against a kind, numeric limits and (for choices) a choice list.
[[nodiscard]] SetResult check_spec(ValueKind kind, const NumericLimits& limits,
                                   const ChoiceInfo* choice, const Value& value) noexcept {
    switch (kind) {
    case ValueKind::Bool:
        return std::holds_alternative<bool>(value) ? SetResult::Ok : SetResult::WrongKind;
    case ValueKind::Text:
        return std::holds_alternative<std::string>(value) ? SetResult::Ok : SetResult::WrongKind;
    case ValueKind::Choice: {
        const std::int64_t* number = std::get_if<std::int64_t>(&value);
        if (number == nullptr) {
            return SetResult::WrongKind;
        }
        if (choice == nullptr || choice->find_entry(*number) == nullptr) {
            return SetResult::UnknownChoice;
        }
        return SetResult::Ok;
    }
    case ValueKind::Int:
    case ValueKind::Float: {
        double number = 0.0;
        if (kind == ValueKind::Int) {
            const std::int64_t* v = std::get_if<std::int64_t>(&value);
            if (v == nullptr) {
                return SetResult::WrongKind;
            }
            number = static_cast<double>(*v);
        } else {
            const double* v = std::get_if<double>(&value);
            if (v == nullptr) {
                return SetResult::WrongKind;
            }
            number = *v;
        }
        if ((limits.min && number < *limits.min) || (limits.max && number > *limits.max)) {
            return SetResult::OutOfRange;
        }
        return SetResult::Ok;
    }
    }
    return SetResult::WrongKind;
}

[[nodiscard]] const ParameterInfo* find_in(const std::vector<ParameterInfo>& list,
                                           std::string_view id) noexcept {
    const auto it =
        std::find_if(list.begin(), list.end(), [&](const ParameterInfo& p) { return p.id == id; });
    return it == list.end() ? nullptr : &*it;
}

[[nodiscard]] std::string default_block_text(const Metadata& meta,
                                             const std::vector<ParameterInfo>& parameters) {
    std::string text = meta.display_name;
    for (const ParameterInfo& parameter : parameters) {
        text += " {" + parameter.id + "}";
    }
    return text;
}

/// Returns an empty string if `text` is valid block text for `parameters`, otherwise a
/// description of the problem.
[[nodiscard]] std::string block_text_problem(std::string_view text,
                                             const std::vector<ParameterInfo>& parameters) {
    std::size_t position = 0;
    while (position < text.size()) {
        const std::size_t open = text.find_first_of("{}", position);
        if (open == std::string_view::npos) {
            break;
        }
        if (text[open] == '}') {
            return "has a '}' without a matching '{'.";
        }
        const std::size_t close = text.find_first_of("{}", open + 1);
        if (close == std::string_view::npos || text[close] == '{') {
            return "has a '{' without a matching '}'.";
        }
        const std::string_view name = text.substr(open + 1, close - open - 1);
        if (find_in(parameters, name) == nullptr) {
            return "names '" + std::string(name) + "', which is not a parameter.";
        }
        position = close + 1;
    }
    return {};
}

} // namespace

bool is_valid_id(std::string_view id) noexcept {
    if (id.empty() || !is_lower_letter(id.front())) {
        return false;
    }
    return std::all_of(id.begin(), id.end(),
                       [](char c) { return is_lower_letter(c) || is_digit(c) || c == '_'; });
}

std::string display_name_from_id(std::string_view id) {
    std::string name;
    name.reserve(id.size());
    bool pending_space = false;
    for (const char c : id) {
        if (c == '_') {
            pending_space = !name.empty();
            continue;
        }
        if (pending_space) {
            name.push_back(' ');
            pending_space = false;
        }
        name.push_back(c);
    }
    if (!name.empty() && is_lower_letter(name.front())) {
        name.front() = static_cast<char>(name.front() - 'a' + 'A');
    }
    return name;
}

std::string_view kind_display_name(ValueKind kind) noexcept {
    switch (kind) {
    case ValueKind::Bool:
        return "Yes/No";
    case ValueKind::Int:
        return "Whole number";
    case ValueKind::Float:
        return "Number";
    case ValueKind::Text:
        return "Text";
    case ValueKind::Choice:
        return "Choice";
    }
    return "Unknown";
}

std::string_view unit_symbol(Unit unit) noexcept {
    switch (unit) {
    case Unit::None:
        return "";
    case Unit::Pixels:
        return "px";
    case Unit::Tiles:
        return "tiles";
    case Unit::Seconds:
        return "s";
    case Unit::Degrees:
        return "°";
    case Unit::Percent:
        return "%";
    }
    return "";
}

const ChoiceEntry* ChoiceInfo::find_entry(std::string_view entry_id) const noexcept {
    const auto it = std::find_if(entries.begin(), entries.end(),
                                 [&](const ChoiceEntry& e) { return e.id == entry_id; });
    return it == entries.end() ? nullptr : &*it;
}

const ChoiceEntry* ChoiceInfo::find_entry(std::int64_t value) const noexcept {
    const auto it = std::find_if(entries.begin(), entries.end(),
                                 [&](const ChoiceEntry& e) { return e.value == value; });
    return it == entries.end() ? nullptr : &*it;
}

const PropertyInfo* TypeInfo::find_property(std::string_view property_id) const noexcept {
    const auto it = std::find_if(properties.begin(), properties.end(),
                                 [&](const PropertyInfo& p) { return p.id == property_id; });
    return it == properties.end() ? nullptr : &*it;
}

SetResult check_value(const PropertyInfo& property, const Value& value) noexcept {
    if (has_flag(property.flags, property_flag::read_only)) {
        return SetResult::ReadOnly;
    }
    return check_spec(property.kind, property.limits, property.choice, value);
}

SetResult check_argument(const ParameterInfo& parameter, const Value& value) noexcept {
    return check_spec(parameter.kind, parameter.limits, parameter.choice, value);
}

std::optional<Value> get_property(ObjectRef object, const PropertyInfo& property) {
    if (object.is_null() || property.owner_key != object.key() || property.get == nullptr) {
        return std::nullopt;
    }
    return property.get(object.object());
}

SetResult set_property(ObjectRef object, const PropertyInfo& property, const Value& value) {
    if (object.is_null() || property.owner_key != object.key() || property.set == nullptr) {
        return SetResult::WrongType;
    }
    if (const SetResult checked = check_value(property, value); checked != SetResult::Ok) {
        return checked;
    }
    return property.set(object.object(), value) ? SetResult::Ok : SetResult::NotRepresentable;
}

CallResult call(ObjectRef object, const FunctionInfo& function, std::span<const Value> arguments) {
    if (object.is_null() || function.owner_key != object.key() || function.invoke == nullptr) {
        return CallResult{CallError::WrongType, 0, std::nullopt};
    }
    if (CallResult checked = check_arguments(function, arguments); !checked.ok()) {
        return checked;
    }
    return function.invoke(object.object(), arguments);
}

CallResult check_arguments(const FunctionInfo& function,
                           std::span<const Value> arguments) noexcept {
    if (arguments.size() != function.parameters.size()) {
        return CallResult{CallError::WrongArgumentCount, 0, std::nullopt};
    }
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        switch (check_argument(function.parameters[i], arguments[i])) {
        case SetResult::Ok:
            break;
        case SetResult::OutOfRange:
            return CallResult{CallError::OutOfRange, i, std::nullopt};
        case SetResult::UnknownChoice:
            return CallResult{CallError::UnknownChoice, i, std::nullopt};
        default:
            return CallResult{CallError::WrongKind, i, std::nullopt};
        }
    }
    return CallResult{};
}

std::string_view role_display_name(FunctionRole role) noexcept {
    switch (role) {
    case FunctionRole::Action:
        return "Action";
    case FunctionRole::Condition:
        return "Condition";
    case FunctionRole::Getter:
        return "Value";
    }
    return "Unknown";
}

const ParameterInfo* FunctionInfo::find_parameter(std::string_view parameter_id) const noexcept {
    return find_in(parameters, parameter_id);
}

const ParameterInfo* EventInfo::find_parameter(std::string_view parameter_id) const noexcept {
    return find_in(parameters, parameter_id);
}

const FunctionInfo* TypeInfo::find_function(std::string_view function_id) const noexcept {
    const auto it = std::find_if(functions.begin(), functions.end(),
                                 [&](const FunctionInfo& f) { return f.id == function_id; });
    return it == functions.end() ? nullptr : &*it;
}

const EventInfo* TypeInfo::find_event(std::string_view event_id) const noexcept {
    const auto it = std::find_if(events.begin(), events.end(),
                                 [&](const EventInfo& e) { return e.id == event_id; });
    return it == events.end() ? nullptr : &*it;
}

bool TypeInfo::has_member(std::string_view member_id) const noexcept {
    return find_property(member_id) != nullptr || find_function(member_id) != nullptr ||
           find_event(member_id) != nullptr;
}

std::string block_text(const FunctionInfo& function) {
    return function.text.empty() ? default_block_text(function.meta, function.parameters)
                                 : function.text;
}

std::string block_text(const EventInfo& event) {
    return event.text.empty() ? default_block_text(event.meta, event.parameters) : event.text;
}

// ---------------------------------------------------------------------------------------
// BlockBuilder and ParameterBuilder
// ---------------------------------------------------------------------------------------

Metadata* BlockBuilder::meta() const noexcept {
    if (type_ == nullptr) {
        return nullptr;
    }
    if (is_event_) {
        return member_ < type_->events.size() ? &type_->events[member_].meta : nullptr;
    }
    return member_ < type_->functions.size() ? &type_->functions[member_].meta : nullptr;
}

std::vector<ParameterInfo>* BlockBuilder::parameters() const noexcept {
    if (type_ == nullptr) {
        return nullptr;
    }
    if (is_event_) {
        return member_ < type_->events.size() ? &type_->events[member_].parameters : nullptr;
    }
    return member_ < type_->functions.size() ? &type_->functions[member_].parameters : nullptr;
}

std::string BlockBuilder::where() const {
    std::string text = "Type '" + (type_ != nullptr ? type_->name : std::string()) + "', ";
    if (is_event_ && type_ != nullptr && member_ < type_->events.size()) {
        return text + "event '" + type_->events[member_].id + "': ";
    }
    if (!is_event_ && type_ != nullptr && member_ < type_->functions.size()) {
        return text + "function '" + type_->functions[member_].id + "': ";
    }
    return text;
}

BlockBuilder& BlockBuilder::display_name(std::string_view text) {
    if (Metadata* m = meta()) {
        m->display_name = text;
    }
    return *this;
}

BlockBuilder& BlockBuilder::category(std::string_view text) {
    if (Metadata* m = meta()) {
        m->category = text;
    }
    return *this;
}

BlockBuilder& BlockBuilder::tooltip(std::string_view text) {
    if (Metadata* m = meta()) {
        m->tooltip = text;
    }
    return *this;
}

BlockBuilder& BlockBuilder::icon(std::string_view name) {
    if (Metadata* m = meta()) {
        m->icon = name;
    }
    return *this;
}

BlockBuilder& BlockBuilder::text(std::string_view block_text) {
    const std::vector<ParameterInfo>* list = parameters();
    if (list == nullptr) {
        return *this;
    }
    if (const std::string problem = block_text_problem(block_text, *list); !problem.empty()) {
        registry_->report(ErrorCode::InvalidBlockText,
                          where() + "the block text '" + std::string(block_text) + "' " + problem);
        return *this;
    }
    if (is_event_) {
        type_->events[member_].text = block_text;
    } else {
        type_->functions[member_].text = block_text;
    }
    return *this;
}

ParameterBuilder BlockBuilder::param(std::string_view id) const {
    constexpr std::size_t no_parameter = static_cast<std::size_t>(-1);
    const std::vector<ParameterInfo>* list = parameters();
    if (list == nullptr) {
        return ParameterBuilder(registry_, type_, is_event_, member_, no_parameter);
    }
    const ParameterInfo* found = find_in(*list, id);
    if (found == nullptr) {
        registry_->report(ErrorCode::UnknownParameter,
                          where() + "there is no parameter '" + std::string(id) + "'.");
        return ParameterBuilder(registry_, type_, is_event_, member_, no_parameter);
    }
    return ParameterBuilder(registry_, type_, is_event_, member_,
                            static_cast<std::size_t>(found - list->data()));
}

std::vector<ParameterInfo>* ParameterBuilder::list() const noexcept {
    return BlockBuilder(registry_, type_, is_event_, member_).parameters();
}

ParameterInfo* ParameterBuilder::info() const noexcept {
    std::vector<ParameterInfo>* parameters = list();
    if (parameters == nullptr || index_ >= parameters->size()) {
        return nullptr;
    }
    return &(*parameters)[index_];
}

std::string ParameterBuilder::where() const {
    const ParameterInfo* p = info();
    return BlockBuilder(registry_, type_, is_event_, member_).where() + "parameter '" +
           (p != nullptr ? p->id : std::string()) + "': ";
}

ParameterBuilder& ParameterBuilder::display_name(std::string_view text) {
    if (ParameterInfo* p = info()) {
        p->meta.display_name = text;
    }
    return *this;
}

ParameterBuilder& ParameterBuilder::tooltip(std::string_view text) {
    if (ParameterInfo* p = info()) {
        p->meta.tooltip = text;
    }
    return *this;
}

ParameterBuilder& ParameterBuilder::unit(Unit value) {
    if (ParameterInfo* p = info()) {
        p->unit = value;
    }
    return *this;
}

ParameterBuilder& ParameterBuilder::step(double value) {
    if (ParameterInfo* p = info()) {
        p->limits.step = value;
    }
    return *this;
}

ParameterBuilder& ParameterBuilder::range(double min, double max) {
    ParameterInfo* p = info();
    if (p == nullptr) {
        return *this;
    }
    if (p->kind != ValueKind::Int && p->kind != ValueKind::Float) {
        registry_->report(ErrorCode::InvalidRange,
                          where() + "a range only applies to numeric parameters.");
        return *this;
    }
    if (min > max) {
        registry_->report(ErrorCode::InvalidRange, where() + "min is greater than max.");
        return *this;
    }
    p->limits.min = min;
    p->limits.max = max;
    // Move a default that falls outside the new range to the nearest bound.
    if (check_argument(*p, p->default_value) == SetResult::OutOfRange) {
        if (p->kind == ValueKind::Int) {
            const auto current = std::get<std::int64_t>(p->default_value);
            p->default_value = static_cast<double>(current) < min
                                   ? Value{static_cast<std::int64_t>(std::ceil(min))}
                                   : Value{static_cast<std::int64_t>(std::floor(max))};
        } else {
            const double current = std::get<double>(p->default_value);
            p->default_value = Value{current < min ? min : max};
        }
    }
    return *this;
}

ParameterBuilder& ParameterBuilder::default_value(const Value& value) {
    ParameterInfo* p = info();
    if (p == nullptr) {
        return *this;
    }
    if (check_argument(*p, value) != SetResult::Ok) {
        registry_->report(ErrorCode::InvalidDefault,
                          where() + "the default does not fit the parameter's kind, range or "
                                    "choices.");
        return *this;
    }
    p->default_value = value;
    return *this;
}

ParameterBuilder ParameterBuilder::param(std::string_view id) const {
    return BlockBuilder(registry_, type_, is_event_, member_).param(id);
}

void Registry::report(ErrorCode code, std::string message) {
    errors_.push_back(RegistrationError{code, std::move(message)});
}

bool Registry::can_register(std::string_view name, const void* key) {
    const std::string quoted = "'" + std::string(name) + "'";
    if (!is_valid_id(name)) {
        report(ErrorCode::InvalidId, "Name " + quoted +
                                         " must be lowercase letters, digits and underscores, "
                                         "starting with a letter.");
        return false;
    }
    if (find_type(name) != nullptr || find_choice_list(name) != nullptr) {
        report(ErrorCode::DuplicateType, "The name " + quoted + " is already registered.");
        return false;
    }
    const bool key_taken = std::any_of(keys_.begin(), keys_.end(),
                                       [&](const KeyEntry& entry) { return entry.key == key; });
    if (key_taken) {
        report(ErrorCode::DuplicateType,
               "The C++ type registered as " + quoted + " is already registered.");
        return false;
    }
    const TypeId id = type_id_from_name(name);
    const bool id_taken =
        find_type(id) != nullptr ||
        std::any_of(choices_.begin(), choices_.end(),
                    [&](const std::unique_ptr<ChoiceInfo>& c) { return c->id == id; });
    if (id_taken) {
        report(ErrorCode::TypeIdCollision,
               "The name " + quoted + " has the same TypeId as an already registered name.");
        return false;
    }
    return true;
}

TypeInfo* Registry::create_type(std::string_view name, const void* key) {
    if (!can_register(name, key)) {
        return nullptr;
    }
    auto info = std::make_unique<TypeInfo>();
    info->id = type_id_from_name(name);
    info->name = name;
    info->meta.display_name = display_name_from_id(name);
    TypeInfo* raw = info.get();
    types_.push_back(std::move(info));
    keys_.push_back(KeyEntry{key, raw, nullptr});
    return raw;
}

ChoiceInfo* Registry::create_choice_list(std::string_view name, const void* key) {
    if (!can_register(name, key)) {
        return nullptr;
    }
    auto info = std::make_unique<ChoiceInfo>();
    info->id = type_id_from_name(name);
    info->name = name;
    info->meta.display_name = display_name_from_id(name);
    ChoiceInfo* raw = info.get();
    choices_.push_back(std::move(info));
    keys_.push_back(KeyEntry{key, nullptr, raw});
    return raw;
}

const TypeInfo* Registry::find_type(std::string_view name) const noexcept {
    const auto it =
        std::find_if(types_.begin(), types_.end(),
                     [&](const std::unique_ptr<TypeInfo>& t) { return t->name == name; });
    return it == types_.end() ? nullptr : it->get();
}

const TypeInfo* Registry::find_type(TypeId id) const noexcept {
    const auto it = std::find_if(types_.begin(), types_.end(),
                                 [&](const std::unique_ptr<TypeInfo>& t) { return t->id == id; });
    return it == types_.end() ? nullptr : it->get();
}

const ChoiceInfo* Registry::find_choice_list(std::string_view name) const noexcept {
    const auto it =
        std::find_if(choices_.begin(), choices_.end(),
                     [&](const std::unique_ptr<ChoiceInfo>& c) { return c->name == name; });
    return it == choices_.end() ? nullptr : it->get();
}

const TypeInfo* Registry::find_type_by_key(const void* key) const noexcept {
    const auto it =
        std::find_if(keys_.begin(), keys_.end(), [&](const KeyEntry& e) { return e.key == key; });
    return it == keys_.end() ? nullptr : it->type;
}

const ChoiceInfo* Registry::find_choice_by_key(const void* key) const noexcept {
    const auto it =
        std::find_if(keys_.begin(), keys_.end(), [&](const KeyEntry& e) { return e.key == key; });
    return it == keys_.end() ? nullptr : it->choice;
}

std::vector<const TypeInfo*> Registry::types() const {
    std::vector<const TypeInfo*> result;
    result.reserve(types_.size());
    for (const auto& t : types_) {
        result.push_back(t.get());
    }
    return result;
}

std::vector<const ChoiceInfo*> Registry::choice_lists() const {
    std::vector<const ChoiceInfo*> result;
    result.reserve(choices_.size());
    for (const auto& c : choices_) {
        result.push_back(c.get());
    }
    return result;
}

} // namespace engine::reflect
