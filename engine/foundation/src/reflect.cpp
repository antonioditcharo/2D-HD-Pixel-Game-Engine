#include "engine/foundation/reflect.h"

#include <algorithm>

namespace engine::reflect {

namespace {

[[nodiscard]] bool is_lower_letter(char c) noexcept {
    return c >= 'a' && c <= 'z';
}

[[nodiscard]] bool is_digit(char c) noexcept {
    return c >= '0' && c <= '9';
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

    switch (property.kind) {
    case ValueKind::Bool:
        return std::holds_alternative<bool>(value) ? SetResult::Ok : SetResult::WrongKind;
    case ValueKind::Text:
        return std::holds_alternative<std::string>(value) ? SetResult::Ok : SetResult::WrongKind;
    case ValueKind::Choice: {
        const std::int64_t* number = std::get_if<std::int64_t>(&value);
        if (number == nullptr) {
            return SetResult::WrongKind;
        }
        if (property.choice == nullptr || property.choice->find_entry(*number) == nullptr) {
            return SetResult::UnknownChoice;
        }
        return SetResult::Ok;
    }
    case ValueKind::Int:
    case ValueKind::Float: {
        double number = 0.0;
        if (property.kind == ValueKind::Int) {
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
        if ((property.limits.min && number < *property.limits.min) ||
            (property.limits.max && number > *property.limits.max)) {
            return SetResult::OutOfRange;
        }
        return SetResult::Ok;
    }
    }
    return SetResult::WrongKind;
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
