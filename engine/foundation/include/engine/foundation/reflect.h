#pragma once

// The reflection registry: engine features describe their data and functions once, with
// the metadata a non-programmer needs (display name, tooltip, range, unit, block text).
// Inspectors, no-code blocks, search, serialization and docs are generated from these
// descriptions.
//
// See docs/dev/reflection.md for the design, scope and usage.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace engine::reflect {

// ---------------------------------------------------------------------------------------
// Identifiers
// ---------------------------------------------------------------------------------------

/// A stable identifier for a registered type or choice list. It is a hash of the
/// registered name, so it is the same in every build and on every platform, which makes it
/// safe to store in project files.
struct TypeId {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool is_valid() const noexcept { return value != 0; }
    friend constexpr bool operator==(TypeId, TypeId) = default;
};

/// Computes the 64-bit FNV-1a hash of a registered name.
[[nodiscard]] constexpr TypeId type_id_from_name(std::string_view name) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char c : name) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 1099511628211ULL;
    }
    return TypeId{hash};
}

/// True if `id` is a valid identifier: lowercase letters, digits and underscores, starting
/// with a letter, for example "radius_tiles". Identifiers are stored in project files, so
/// they must never change once released; display names can.
[[nodiscard]] bool is_valid_id(std::string_view id) noexcept;

/// Turns an identifier into a readable default display name: "radius_tiles" becomes
/// "Radius tiles".
[[nodiscard]] std::string display_name_from_id(std::string_view id);

// ---------------------------------------------------------------------------------------
// Values, kinds and units
// ---------------------------------------------------------------------------------------

/// The kinds of value a property can hold, named as the no-code UI names them.
enum class ValueKind : std::uint8_t {
    Bool,   ///< "Yes/No"
    Int,    ///< "Whole number"
    Float,  ///< "Number"
    Text,   ///< "Text"
    Choice, ///< "Choice": one entry of a registered choice list (a C++ enum)
};

/// The plain-language name of a kind, as shown in the editor.
[[nodiscard]] std::string_view kind_display_name(ValueKind kind) noexcept;

/// What a number means. Units only affect how a value is shown and edited; the stored
/// value is never converted.
enum class Unit : std::uint8_t {
    None,
    Pixels,
    Tiles,
    Seconds,
    Degrees,
    Percent,
};

/// The short symbol shown after a value, for example "s" for seconds. Empty for None.
[[nodiscard]] std::string_view unit_symbol(Unit unit) noexcept;

/// A property value in type-erased form.
/// - Bool holds `bool`.
/// - Int and Choice hold `std::int64_t` (Choice holds the enum's numeric value).
/// - Float holds `double`.
/// - Text holds `std::string`.
using Value = std::variant<bool, std::int64_t, double, std::string>;

/// Optional property behaviors, as a set of bits. Combine with `|`, for example
/// `property_flag::advanced | property_flag::saved`.
struct PropertyFlags {
    std::uint8_t bits = 0;

    friend constexpr PropertyFlags operator|(PropertyFlags a, PropertyFlags b) noexcept {
        return PropertyFlags{static_cast<std::uint8_t>(a.bits | b.bits)};
    }
    friend constexpr bool operator==(PropertyFlags, PropertyFlags) = default;
};

/// The individual property flags.
namespace property_flag {
inline constexpr PropertyFlags none{0};
inline constexpr PropertyFlags advanced{1U << 0U};  ///< Shown only in the Advanced view.
inline constexpr PropertyFlags read_only{1U << 1U}; ///< Shown but not editable.
inline constexpr PropertyFlags hidden{1U << 2U};    ///< Not shown in the inspector at all.
inline constexpr PropertyFlags saved{1U << 3U};     ///< Saved with the game ("persistent").
} // namespace property_flag

/// True if every flag in `flag` is set in `flags`.
[[nodiscard]] constexpr bool has_flag(PropertyFlags flags, PropertyFlags flag) noexcept {
    return (flags.bits & flag.bits) == flag.bits;
}

// ---------------------------------------------------------------------------------------
// Descriptions
// ---------------------------------------------------------------------------------------

/// The human-facing description shared by types, properties and choice entries.
struct Metadata {
    std::string display_name; ///< Defaults to display_name_from_id(id).
    std::string category;     ///< Groups items in the inspector and block library.
    std::string tooltip;      ///< One or two plain-language sentences.
    std::string icon;         ///< Icon name from the editor's icon set.
};

/// Limits for Int and Float properties. Unset bounds mean "no limit".
struct NumericLimits {
    std::optional<double> min;
    std::optional<double> max;
    double step = 0.0; ///< Suggested increment for sliders and spin boxes; 0 = automatic.
};

/// One entry of a choice list, for example "north" of a Facing enum.
struct ChoiceEntry {
    std::string id;
    std::int64_t value = 0;
    Metadata meta;
};

/// A registered choice list (a C++ enum).
struct ChoiceInfo {
    TypeId id;
    std::string name;
    Metadata meta;
    std::vector<ChoiceEntry> entries; ///< In registration order.

    [[nodiscard]] const ChoiceEntry* find_entry(std::string_view entry_id) const noexcept;
    [[nodiscard]] const ChoiceEntry* find_entry(std::int64_t value) const noexcept;
};

/// One reflected property (a data member) of a registered type.
struct PropertyInfo {
    std::string id;
    ValueKind kind = ValueKind::Bool;
    Metadata meta;
    NumericLimits limits;
    Unit unit = Unit::None;
    PropertyFlags flags = property_flag::none;
    Value default_value;               ///< The member's value in a default-constructed object.
    const ChoiceInfo* choice{nullptr}; ///< The choice list, when kind is Choice.

    // Type-erased accessors generated from the member pointer. Use get_property() and
    // set_property() instead of calling these directly: those check the object's type,
    // the value's kind, ranges and choice entries first.
    const void* owner_key{nullptr};
    Value (*get)(const void* object){nullptr};
    bool (*set)(void* object, const Value& value){nullptr};
};

/// One parameter of an action, condition, value getter or event.
struct ParameterInfo {
    std::string id;
    ValueKind kind = ValueKind::Bool;
    Metadata meta;
    NumericLimits limits;
    Unit unit = Unit::None;
    Value default_value;               ///< What a new block starts with.
    const ChoiceInfo* choice{nullptr}; ///< The choice list, when kind is Choice.
};

/// What a reflected function is for in the no-code layer.
enum class FunctionRole : std::uint8_t {
    Action,    ///< "Do": changes something. Returns nothing.
    Condition, ///< "If": answers yes or no. Must not change anything (const).
    Getter,    ///< A value that can be used in other blocks. Must not change anything (const).
};

/// The plain-language name of a role: "Action", "Condition" or "Value".
[[nodiscard]] std::string_view role_display_name(FunctionRole role) noexcept;

/// Why a call failed. See call().
enum class CallError : std::uint8_t {
    None,
    WrongType,          ///< The function belongs to a different C++ type than the object.
    WrongArgumentCount, ///< Too many or too few arguments.
    WrongKind,          ///< An argument's kind does not match its parameter.
    OutOfRange,         ///< An argument is below min or above max.
    UnknownChoice,      ///< An argument is not one of its choice list's entries.
    NotRepresentable,   ///< An argument fits the kind but not the parameter's C++ type.
};

/// The outcome of call().
struct CallResult {
    CallError error = CallError::None;
    std::size_t argument = 0;   ///< For argument errors: the index of the offending argument.
    std::optional<Value> value; ///< For conditions and getters: the result.

    [[nodiscard]] bool ok() const noexcept { return error == CallError::None; }
};

/// A reflected member function: an action, condition or value getter.
struct FunctionInfo {
    std::string id;
    FunctionRole role = FunctionRole::Action;
    Metadata meta;
    /// How the block reads, for example "Flicker {times} times". Each "{id}" marks where a
    /// parameter goes. Empty means the default: see block_text().
    std::string text;
    std::vector<ParameterInfo> parameters; ///< In C++ parameter order.
    std::optional<ValueKind> result_kind;  ///< Bool for conditions; none for actions.
    const ChoiceInfo* result_choice{nullptr};

    // Type-erased invoker generated from the member function pointer. Use call() instead:
    // it checks the object's type and every argument first.
    const void* owner_key{nullptr};
    CallResult (*invoke)(void* object, std::span<const Value> arguments){nullptr};

    [[nodiscard]] const ParameterInfo* find_parameter(std::string_view parameter_id) const noexcept;
};

/// A reflected event ("When ..."): something a type announces, carrying values.
struct EventInfo {
    std::string id;
    Metadata meta;
    std::string text; ///< As FunctionInfo::text.
    std::vector<ParameterInfo> parameters;

    [[nodiscard]] const ParameterInfo* find_parameter(std::string_view parameter_id) const noexcept;
};

/// A registered type: its properties, functions and events.
struct TypeInfo {
    TypeId id;
    std::string name;
    Metadata meta;
    std::vector<PropertyInfo> properties; ///< In registration order.
    std::vector<FunctionInfo> functions;  ///< In registration order.
    std::vector<EventInfo> events;        ///< In registration order.

    [[nodiscard]] const PropertyInfo* find_property(std::string_view property_id) const noexcept;
    [[nodiscard]] const FunctionInfo* find_function(std::string_view function_id) const noexcept;
    [[nodiscard]] const EventInfo* find_event(std::string_view event_id) const noexcept;
    /// True if any property, function or event of this type uses `member_id`.
    [[nodiscard]] bool has_member(std::string_view member_id) const noexcept;
};

/// The text a block shows: the custom text if one was set, otherwise the display name
/// followed by a placeholder for each parameter, for example "Flicker {times} {interval}".
[[nodiscard]] std::string block_text(const FunctionInfo& function);
[[nodiscard]] std::string block_text(const EventInfo& event);

// ---------------------------------------------------------------------------------------
// Errors and results
// ---------------------------------------------------------------------------------------

/// Problems found while registering. Registration never throws or aborts: problems are
/// collected so a test can check that a registry is clean.
enum class ErrorCode : std::uint8_t {
    InvalidId,              ///< An id is not lowercase letters, digits and underscores.
    DuplicateType,          ///< A name or C++ type was registered twice.
    DuplicateProperty,      ///< Two properties of one type share an id.
    DuplicateChoice,        ///< Two entries of one choice list share an id or value.
    InvalidRange,           ///< min > max, or a range on a non-numeric property.
    DefaultOutOfRange,      ///< The member's default value is outside its range.
    UnknownChoiceList,      ///< A property's enum was not registered before the type using it.
    TypeIdCollision,        ///< Two different names hash to the same TypeId.
    DuplicateMember,        ///< A function or event id is already used by another member.
    ParameterCountMismatch, ///< The number of parameter ids differs from the C++ signature.
    UnknownParameter,       ///< param() named a parameter the function or event does not have.
    InvalidBlockText,       ///< Block text has unbalanced braces or names an unknown parameter.
    InvalidDefault,         ///< A parameter default does not fit its kind, range or choices.
};

struct RegistrationError {
    ErrorCode code;
    std::string message;
};

/// The outcome of set_property().
enum class SetResult : std::uint8_t {
    Ok,
    WrongType,        ///< The property belongs to a different C++ type than the object.
    ReadOnly,         ///< The property has property_flag::read_only.
    WrongKind,        ///< The value's kind does not match the property's kind.
    OutOfRange,       ///< Below min or above max.
    UnknownChoice,    ///< Not one of the choice list's entries.
    NotRepresentable, ///< Fits the kind but not the member's C++ type, e.g. 1000 in an int8.
};

// ---------------------------------------------------------------------------------------
// Implementation details used by the templates below
// ---------------------------------------------------------------------------------------

namespace detail {

// A unique address per C++ type, without RTTI. The variable is deliberately not const so
// that linkers cannot merge the identical objects of different types (MSVC /OPT:ICF).
template <typename T>
struct TypeKey {
    static inline char tag = 0;
};

template <typename T>
[[nodiscard]] const void* type_key() noexcept {
    return &TypeKey<std::remove_cvref_t<T>>::tag;
}

template <typename T>
struct MemberTraits;

template <typename Owner, typename Field>
struct MemberTraits<Field Owner::*> {
    using owner = Owner;
    using field = Field;
};

template <typename>
inline constexpr bool always_false = false;

template <typename F>
[[nodiscard]] consteval ValueKind kind_of_field() {
    if constexpr (std::is_same_v<F, bool>) {
        return ValueKind::Bool;
    } else if constexpr (std::is_enum_v<F>) {
        return ValueKind::Choice;
    } else if constexpr (std::is_integral_v<F>) {
        static_assert(!std::is_same_v<F, char> && !std::is_same_v<F, wchar_t> &&
                          !std::is_same_v<F, char8_t> && !std::is_same_v<F, char16_t> &&
                          !std::is_same_v<F, char32_t>,
                      "Character members cannot be reflected; use std::string for text or a "
                      "fixed-width integer such as std::int8_t for numbers.");
        static_assert(!(std::is_unsigned_v<F> && sizeof(F) >= sizeof(std::int64_t)),
                      "64-bit unsigned members cannot be reflected: values above the int64 "
                      "range could not be represented.");
        return ValueKind::Int;
    } else if constexpr (std::is_floating_point_v<F>) {
        return ValueKind::Float;
    } else if constexpr (std::is_same_v<F, std::string>) {
        return ValueKind::Text;
    } else {
        static_assert(always_false<F>,
                      "This member type cannot be reflected yet. Supported: bool, integers, "
                      "floating point, std::string and registered enums.");
        return ValueKind::Bool;
    }
}

template <typename F>
[[nodiscard]] Value to_value(const F& v) {
    if constexpr (std::is_same_v<F, bool> || std::is_same_v<F, std::string>) {
        return Value{v};
    } else if constexpr (std::is_enum_v<F> || std::is_integral_v<F>) {
        return Value{static_cast<std::int64_t>(v)};
    } else {
        return Value{static_cast<double>(v)};
    }
}

/// Converts a Value back to the C++ type F. Empty if the value has the wrong alternative or
/// does not fit F (for example 1000 into an int8).
template <typename F>
[[nodiscard]] std::optional<F> from_value(const Value& value) {
    if constexpr (std::is_same_v<F, bool> || std::is_same_v<F, std::string>) {
        const F* v = std::get_if<F>(&value);
        return v == nullptr ? std::nullopt : std::optional<F>(*v);
    } else if constexpr (std::is_enum_v<F> || std::is_integral_v<F>) {
        const std::int64_t* v = std::get_if<std::int64_t>(&value);
        using Storage = typename std::conditional_t<std::is_enum_v<F>, std::underlying_type<F>,
                                                    std::type_identity<F>>::type;
        if (v == nullptr || !std::in_range<Storage>(*v)) {
            return std::nullopt;
        }
        return static_cast<F>(static_cast<Storage>(*v));
    } else {
        const double* v = std::get_if<double>(&value);
        if (v == nullptr || *v > static_cast<double>(std::numeric_limits<F>::max()) ||
            *v < static_cast<double>(std::numeric_limits<F>::lowest())) {
            return std::nullopt;
        }
        return static_cast<F>(*v);
    }
}

template <auto Member>
[[nodiscard]] Value get_member(const void* object) {
    using Traits = MemberTraits<decltype(Member)>;
    return to_value(static_cast<const typename Traits::owner*>(object)->*Member);
}

template <auto Member>
[[nodiscard]] bool set_member(void* object, const Value& value) {
    using Traits = MemberTraits<decltype(Member)>;
    std::optional<typename Traits::field> converted = from_value<typename Traits::field>(value);
    if (!converted) {
        return false;
    }
    static_cast<typename Traits::owner*>(object)->*Member = std::move(*converted);
    return true;
}

template <typename T>
struct FunctionTraits;

template <typename R, typename C, bool IsConst, typename... A>
struct FunctionTraitsBase {
    using owner = C;
    using result = std::remove_cvref_t<R>;
    using arguments = std::tuple<std::remove_cvref_t<A>...>;
    static constexpr bool is_const = IsConst;
    static constexpr std::size_t arity = sizeof...(A);
};

template <typename R, typename C, typename... A>
struct FunctionTraits<R (C::*)(A...)> : FunctionTraitsBase<R, C, false, A...> {};
template <typename R, typename C, typename... A>
struct FunctionTraits<R (C::*)(A...) noexcept> : FunctionTraitsBase<R, C, false, A...> {};
template <typename R, typename C, typename... A>
struct FunctionTraits<R (C::*)(A...) const> : FunctionTraitsBase<R, C, true, A...> {};
template <typename R, typename C, typename... A>
struct FunctionTraits<R (C::*)(A...) const noexcept> : FunctionTraitsBase<R, C, true, A...> {};

/// Converts `value` into `out`. Returns false, leaving `out` unchanged, if it does not fit.
template <typename F>
[[nodiscard]] bool convert_into(const Value& value, F& out) {
    std::optional<F> converted = from_value<F>(value);
    if (!converted) {
        return false;
    }
    out = std::move(*converted);
    return true;
}

template <auto Fn, std::size_t... I>
[[nodiscard]] CallResult invoke_with(void* object, std::span<const Value> arguments,
                                     std::index_sequence<I...> /*indices*/) {
    using Traits = FunctionTraits<decltype(Fn)>;
    using Owner =
        std::conditional_t<Traits::is_const, const typename Traits::owner, typename Traits::owner>;
    constexpr std::size_t arity = sizeof...(I);
    if (arguments.size() != arity) {
        return CallResult{CallError::WrongArgumentCount, 0, std::nullopt};
    }
    Owner& target = *static_cast<Owner*>(object);
    if constexpr (arity == 0) {
        if constexpr (std::is_void_v<typename Traits::result>) {
            (target.*Fn)();
            return CallResult{};
        } else {
            return CallResult{CallError::None, 0, to_value((target.*Fn)())};
        }
    } else {
        using Arguments = typename Traits::arguments;
        // Every supported parameter type is default-constructible.
        Arguments converted{};
        std::size_t failed = arity;
        ((failed == arity && !convert_into(arguments[I], std::get<I>(converted)) ? (failed = I)
                                                                                 : failed),
         ...);
        if (failed != arity) {
            return CallResult{CallError::NotRepresentable, failed, std::nullopt};
        }
        if constexpr (std::is_void_v<typename Traits::result>) {
            (target.*Fn)(std::move(std::get<I>(converted))...);
            return CallResult{};
        } else {
            return CallResult{CallError::None, 0,
                              to_value((target.*Fn)(std::move(std::get<I>(converted))...))};
        }
    }
}

template <auto Fn>
[[nodiscard]] CallResult invoke_member(void* object, std::span<const Value> arguments) {
    return invoke_with<Fn>(object, arguments,
                           std::make_index_sequence<FunctionTraits<decltype(Fn)>::arity>{});
}

} // namespace detail

// ---------------------------------------------------------------------------------------
// Type-erased object references
// ---------------------------------------------------------------------------------------

/// A non-owning reference to an object of any registered type, without templates. The
/// logic runtime and the editor use it to hold objects whose C++ type they do not know.
/// The object must outlive the reference.
class ObjectRef {
public:
    ObjectRef() = default;

    /// Refers to `object`. Const objects cannot be referenced, because properties and
    /// actions can change them.
    template <typename T>
    [[nodiscard]] static ObjectRef of(T& object) noexcept {
        static_assert(!std::is_const_v<T>, "ObjectRef needs a non-const object.");
        return ObjectRef(static_cast<void*>(&object), detail::type_key<T>());
    }

    /// Refers to `object`, whose C++ type is the one `key` identifies. For containers that
    /// store objects by type key themselves, such as the ECS world, and know the key is
    /// right. Everyone else uses of(), which cannot get it wrong.
    [[nodiscard]] static ObjectRef from_key(void* object, const void* key) noexcept {
        return object == nullptr || key == nullptr ? ObjectRef() : ObjectRef(object, key);
    }

    /// The key of T, as stored in ObjectRef::key() and PropertyInfo::owner_key.
    template <typename T>
    [[nodiscard]] static const void* key_of() noexcept {
        return detail::type_key<T>();
    }

    [[nodiscard]] void* object() const noexcept { return object_; }
    /// Identifies the object's C++ type. Compare with PropertyInfo::owner_key and
    /// FunctionInfo::owner_key, or look up the type with Registry::find_type(ObjectRef).
    [[nodiscard]] const void* key() const noexcept { return key_; }
    [[nodiscard]] bool is_null() const noexcept { return object_ == nullptr; }

    /// The object as a T, or null if it is not a T.
    template <typename T>
    [[nodiscard]] T* as() const noexcept {
        return key_ == detail::type_key<T>() ? static_cast<T*>(object_) : nullptr;
    }

private:
    ObjectRef(void* object, const void* key) noexcept : object_(object), key_(key) {}

    void* object_{nullptr};
    const void* key_{nullptr};
};

// ---------------------------------------------------------------------------------------
// The registry and its builders
// ---------------------------------------------------------------------------------------

class Registry;

template <typename T>
class TypeBuilder;

/// Describes one parameter of a function or event. Returned by BlockBuilder::param().
class ParameterBuilder {
public:
    ParameterBuilder& display_name(std::string_view text);
    ParameterBuilder& tooltip(std::string_view text);
    ParameterBuilder& unit(Unit value);
    /// Sets both bounds. Only valid on Int and Float parameters. A default outside the new
    /// range is moved to the nearest bound.
    ParameterBuilder& range(double min, double max);
    ParameterBuilder& step(double value);
    /// Sets what a new block starts with. Must fit the parameter's kind, range and choices.
    ParameterBuilder& default_value(const Value& value);
    /// Selects another parameter of the same function or event.
    [[nodiscard]] ParameterBuilder param(std::string_view id) const;

private:
    friend class BlockBuilder;
    ParameterBuilder(Registry* registry, TypeInfo* type, bool is_event, std::size_t member,
                     std::size_t index)
        : registry_(registry), type_(type), is_event_(is_event), member_(member), index_(index) {}

    [[nodiscard]] std::vector<ParameterInfo>* list() const noexcept;
    [[nodiscard]] ParameterInfo* info() const noexcept;
    [[nodiscard]] std::string where() const;

    Registry* registry_;
    TypeInfo* type_;
    bool is_event_;
    std::size_t member_;
    std::size_t index_;
};

/// Describes one action, condition, getter or event. Returned by TypeBuilder::action(),
/// condition(), getter() and event().
class BlockBuilder {
public:
    BlockBuilder(Registry* registry, TypeInfo* type, bool is_event, std::size_t member)
        : registry_(registry), type_(type), is_event_(is_event), member_(member) {}

    BlockBuilder& display_name(std::string_view text);
    BlockBuilder& category(std::string_view text);
    BlockBuilder& tooltip(std::string_view text);
    BlockBuilder& icon(std::string_view name);
    /// Sets how the block reads, for example "Flicker {times} times, {interval} apart".
    /// Every "{id}" must name a parameter.
    BlockBuilder& text(std::string_view block_text);
    /// Selects a parameter by id, to describe it.
    [[nodiscard]] ParameterBuilder param(std::string_view id) const;

private:
    friend class ParameterBuilder;

    [[nodiscard]] Metadata* meta() const noexcept;
    [[nodiscard]] std::vector<ParameterInfo>* parameters() const noexcept;
    [[nodiscard]] std::string where() const;

    Registry* registry_;
    TypeInfo* type_;
    bool is_event_;
    std::size_t member_;
};

/// Describes one property. Returned by TypeBuilder::property(). Every setter returns the
/// builder, so a whole type can be described in one expression.
template <typename T>
class PropertyBuilder {
public:
    PropertyBuilder(TypeBuilder<T> type, std::size_t index) : type_(type), index_(index) {}

    PropertyBuilder& display_name(std::string_view text);
    PropertyBuilder& category(std::string_view text);
    PropertyBuilder& tooltip(std::string_view text);
    PropertyBuilder& icon(std::string_view name);
    PropertyBuilder& unit(Unit value);
    PropertyBuilder& flags(PropertyFlags value);
    /// Sets both bounds. Only valid on Int and Float properties.
    PropertyBuilder& range(double min, double max);
    PropertyBuilder& step(double value);

    /// Starts the next property of the same type.
    template <auto Member>
    PropertyBuilder property(std::string_view id) {
        return type_.template property<Member>(id);
    }

private:
    [[nodiscard]] PropertyInfo* info() const noexcept;

    TypeBuilder<T> type_;
    std::size_t index_;
};

/// Describes one type. Returned by Registry::add_type().
template <typename T>
class TypeBuilder {
public:
    TypeBuilder(Registry& registry, TypeInfo* info) : registry_(&registry), info_(info) {}

    TypeBuilder& display_name(std::string_view text);
    TypeBuilder& category(std::string_view text);
    TypeBuilder& tooltip(std::string_view text);
    TypeBuilder& icon(std::string_view name);

    /// Adds the data member `Member` (for example `&Light::radius`) as a property.
    template <auto Member>
    PropertyBuilder<T> property(std::string_view id);

    /// Adds the member function `Fn` as an action ("Do"). It must return void.
    /// `parameter_ids` names its parameters, in order.
    template <auto Fn>
    BlockBuilder action(std::string_view id,
                        std::initializer_list<std::string_view> parameter_ids = {});

    /// Adds the const member function `Fn` as a condition ("If"). It must return bool.
    template <auto Fn>
    BlockBuilder condition(std::string_view id,
                           std::initializer_list<std::string_view> parameter_ids = {});

    /// Adds the const member function `Fn` as a value getter. It must return a supported
    /// value type.
    template <auto Fn>
    BlockBuilder getter(std::string_view id,
                        std::initializer_list<std::string_view> parameter_ids = {});

    /// Adds an event ("When ...") whose payload values have the C++ types `Payload...`.
    template <typename... Payload>
    BlockBuilder event(std::string_view id,
                       std::initializer_list<std::string_view> parameter_ids = {});

private:
    friend class PropertyBuilder<T>;

    template <auto Fn>
    BlockBuilder add_function(FunctionRole role, std::string_view id,
                              std::initializer_list<std::string_view> parameter_ids);
    /// Checks a new function or event id; reports and returns false if it cannot be used.
    [[nodiscard]] bool can_add_member(std::string_view id, std::size_t parameter_count,
                                      std::size_t expected_count);
    template <typename P>
    void append_parameter(std::vector<ParameterInfo>& list, std::string_view id,
                          const std::string& where);

    Registry* registry_;
    TypeInfo* info_; ///< Null if the type failed to register; every call is then a no-op.
};

/// Describes one choice list. Returned by Registry::add_choice_list().
template <typename E>
class ChoiceBuilder {
public:
    ChoiceBuilder(Registry& registry, ChoiceInfo* info) : registry_(&registry), info_(info) {}

    ChoiceBuilder& display_name(std::string_view text);
    ChoiceBuilder& tooltip(std::string_view text);
    /// Adds an entry. The display name defaults to display_name_from_id(id).
    ChoiceBuilder& entry(E value, std::string_view id, std::string_view display_name = {},
                         std::string_view tooltip = {});

private:
    Registry* registry_;
    ChoiceInfo* info_;
};

/// Holds every reflected type and choice list.
///
/// Register choice lists (enums) before the types that use them. Descriptions are never
/// removed, and pointers returned by the find functions stay valid for the registry's
/// lifetime.
class Registry {
public:
    Registry() = default;
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;
    Registry(Registry&&) = delete;
    Registry& operator=(Registry&&) = delete;
    ~Registry() = default;

    /// Registers the C++ type T under `name`. T must be default-constructible, because
    /// property defaults are read from a default-constructed T.
    template <typename T>
    TypeBuilder<T> add_type(std::string_view name);

    /// Registers the enum E as a choice list under `name`.
    template <typename E>
    ChoiceBuilder<E> add_choice_list(std::string_view name);

    [[nodiscard]] const TypeInfo* find_type(std::string_view name) const noexcept;
    [[nodiscard]] const TypeInfo* find_type(TypeId id) const noexcept;
    template <typename T>
    [[nodiscard]] const TypeInfo* find_type() const noexcept {
        return find_type_by_key(detail::type_key<T>());
    }
    /// The type of the object `ref` refers to, or null if that type is not registered.
    [[nodiscard]] const TypeInfo* find_type(ObjectRef ref) const noexcept {
        return find_type_by_key(ref.key());
    }

    [[nodiscard]] const ChoiceInfo* find_choice_list(std::string_view name) const noexcept;
    template <typename E>
    [[nodiscard]] const ChoiceInfo* find_choice_list() const noexcept {
        return find_choice_by_key(detail::type_key<E>());
    }

    /// All registered types, in registration order.
    [[nodiscard]] std::vector<const TypeInfo*> types() const;
    /// All registered choice lists, in registration order.
    [[nodiscard]] std::vector<const ChoiceInfo*> choice_lists() const;

    /// Every problem found while registering. Empty means the registry is consistent.
    [[nodiscard]] const std::vector<RegistrationError>& errors() const noexcept { return errors_; }

private:
    template <typename T>
    friend class TypeBuilder;
    template <typename T>
    friend class PropertyBuilder;
    template <typename E>
    friend class ChoiceBuilder;
    friend class BlockBuilder;
    friend class ParameterBuilder;

    void report(ErrorCode code, std::string message);
    /// Checks a new name; reports and returns false if it cannot be registered.
    [[nodiscard]] bool can_register(std::string_view name, const void* key);
    TypeInfo* create_type(std::string_view name, const void* key);
    ChoiceInfo* create_choice_list(std::string_view name, const void* key);
    [[nodiscard]] const TypeInfo* find_type_by_key(const void* key) const noexcept;
    [[nodiscard]] const ChoiceInfo* find_choice_by_key(const void* key) const noexcept;

    struct KeyEntry {
        const void* key;
        const TypeInfo* type;
        const ChoiceInfo* choice;
    };

    std::vector<std::unique_ptr<TypeInfo>> types_;
    std::vector<std::unique_ptr<ChoiceInfo>> choices_;
    std::vector<KeyEntry> keys_;
    std::vector<RegistrationError> errors_;
};

// ---------------------------------------------------------------------------------------
// Reading and writing properties
// ---------------------------------------------------------------------------------------

/// Reads `property` from `object`. Returns nothing if the property belongs to another type.
template <typename T>
    requires(!std::is_same_v<std::remove_cvref_t<T>, ObjectRef>)
[[nodiscard]] std::optional<Value> get_property(const T& object, const PropertyInfo& property) {
    if (property.owner_key != detail::type_key<T>() || property.get == nullptr) {
        return std::nullopt;
    }
    return property.get(&object);
}

/// As above, for an object whose C++ type is not known here. Also returns nothing for a
/// null reference.
[[nodiscard]] std::optional<Value> get_property(ObjectRef object, const PropertyInfo& property);

/// Checks `value` against the property's flags, kind, range and choice entries, without
/// writing anything. Returns Ok, ReadOnly, WrongKind, OutOfRange or UnknownChoice.
[[nodiscard]] SetResult check_value(const PropertyInfo& property, const Value& value) noexcept;

/// Checks `value` with check_value(), then writes it into `object`. Leaves the object
/// unchanged unless the result is Ok. A null reference, or an object of another type than
/// the property's, gives WrongType.
[[nodiscard]] SetResult set_property(ObjectRef object, const PropertyInfo& property,
                                     const Value& value);

/// As above, for an object whose C++ type is known.
template <typename T>
    requires(!std::is_same_v<std::remove_cvref_t<T>, ObjectRef>)
[[nodiscard]] SetResult set_property(T& object, const PropertyInfo& property, const Value& value) {
    return set_property(ObjectRef::of(object), property, value);
}

/// Checks one argument against its parameter's kind, range and choice entries.
/// Returns Ok, WrongKind, OutOfRange or UnknownChoice.
[[nodiscard]] SetResult check_argument(const ParameterInfo& parameter, const Value& value) noexcept;

/// Checks the argument count and every argument of a call.
[[nodiscard]] CallResult check_arguments(const FunctionInfo& function,
                                         std::span<const Value> arguments) noexcept;

/// Calls `function` on `object` with `arguments`, after checking the object's type, the
/// argument count and every argument. Never throws for bad input; the result says what was
/// wrong. Conditions and getters return their result in CallResult::value. A null
/// reference gives WrongType.
[[nodiscard]] CallResult call(ObjectRef object, const FunctionInfo& function,
                              std::span<const Value> arguments);

/// As above, for an object whose C++ type is known.
template <typename T>
    requires(!std::is_same_v<std::remove_cvref_t<T>, ObjectRef>)
[[nodiscard]] CallResult call(T& object, const FunctionInfo& function,
                              std::span<const Value> arguments) {
    return call(ObjectRef::of(object), function, arguments);
}

// ---------------------------------------------------------------------------------------
// Template definitions
// ---------------------------------------------------------------------------------------

template <typename T>
TypeBuilder<T> Registry::add_type(std::string_view name) {
    static_assert(std::is_default_constructible_v<T>,
                  "Reflected types must be default-constructible: property defaults are read "
                  "from a default-constructed object.");
    return TypeBuilder<T>(*this, create_type(name, detail::type_key<T>()));
}

template <typename E>
ChoiceBuilder<E> Registry::add_choice_list(std::string_view name) {
    static_assert(std::is_enum_v<E>, "Choice lists are registered from C++ enums.");
    return ChoiceBuilder<E>(*this, create_choice_list(name, detail::type_key<E>()));
}

template <typename T>
TypeBuilder<T>& TypeBuilder<T>::display_name(std::string_view text) {
    if (info_ != nullptr) {
        info_->meta.display_name = text;
    }
    return *this;
}

template <typename T>
TypeBuilder<T>& TypeBuilder<T>::category(std::string_view text) {
    if (info_ != nullptr) {
        info_->meta.category = text;
    }
    return *this;
}

template <typename T>
TypeBuilder<T>& TypeBuilder<T>::tooltip(std::string_view text) {
    if (info_ != nullptr) {
        info_->meta.tooltip = text;
    }
    return *this;
}

template <typename T>
TypeBuilder<T>& TypeBuilder<T>::icon(std::string_view name) {
    if (info_ != nullptr) {
        info_->meta.icon = name;
    }
    return *this;
}

template <typename T>
template <auto Member>
PropertyBuilder<T> TypeBuilder<T>::property(std::string_view id) {
    using Traits = detail::MemberTraits<decltype(Member)>;
    using F = typename Traits::field;
    static_assert(std::is_same_v<typename Traits::owner, T>,
                  "The member must belong to the type being registered.");
    constexpr ValueKind kind = detail::kind_of_field<F>();

    constexpr std::size_t no_property = static_cast<std::size_t>(-1);
    if (info_ == nullptr) {
        return PropertyBuilder<T>(*this, no_property);
    }
    if (!is_valid_id(id)) {
        registry_->report(ErrorCode::InvalidId, "Type '" + info_->name + "': property id '" +
                                                    std::string(id) +
                                                    "' must be lowercase letters, digits and "
                                                    "underscores, starting with a letter.");
        return PropertyBuilder<T>(*this, no_property);
    }
    if (info_->find_property(id) != nullptr) {
        registry_->report(ErrorCode::DuplicateProperty, "Type '" + info_->name +
                                                            "' already has a property '" +
                                                            std::string(id) + "'.");
        return PropertyBuilder<T>(*this, no_property);
    }

    PropertyInfo property;
    property.id = id;
    property.kind = kind;
    property.meta.display_name = display_name_from_id(id);
    property.owner_key = detail::type_key<T>();
    property.get = &detail::get_member<Member>;
    property.set = &detail::set_member<Member>;
    const T probe{};
    property.default_value = property.get(&probe);
    if constexpr (kind == ValueKind::Choice) {
        property.choice = registry_->find_choice_list<F>();
        if (property.choice == nullptr) {
            registry_->report(ErrorCode::UnknownChoiceList,
                              "Type '" + info_->name + "', property '" + std::string(id) +
                                  "': register its enum with add_choice_list() before "
                                  "registering the type.");
        }
    }
    info_->properties.push_back(std::move(property));
    return PropertyBuilder<T>(*this, info_->properties.size() - 1);
}

template <typename T>
PropertyInfo* PropertyBuilder<T>::info() const noexcept {
    if (type_.info_ == nullptr || index_ >= type_.info_->properties.size()) {
        return nullptr;
    }
    return &type_.info_->properties[index_];
}

template <typename T>
PropertyBuilder<T>& PropertyBuilder<T>::display_name(std::string_view text) {
    if (PropertyInfo* p = info()) {
        p->meta.display_name = text;
    }
    return *this;
}

template <typename T>
PropertyBuilder<T>& PropertyBuilder<T>::category(std::string_view text) {
    if (PropertyInfo* p = info()) {
        p->meta.category = text;
    }
    return *this;
}

template <typename T>
PropertyBuilder<T>& PropertyBuilder<T>::tooltip(std::string_view text) {
    if (PropertyInfo* p = info()) {
        p->meta.tooltip = text;
    }
    return *this;
}

template <typename T>
PropertyBuilder<T>& PropertyBuilder<T>::icon(std::string_view name) {
    if (PropertyInfo* p = info()) {
        p->meta.icon = name;
    }
    return *this;
}

template <typename T>
PropertyBuilder<T>& PropertyBuilder<T>::unit(Unit value) {
    if (PropertyInfo* p = info()) {
        p->unit = value;
    }
    return *this;
}

template <typename T>
PropertyBuilder<T>& PropertyBuilder<T>::flags(PropertyFlags value) {
    if (PropertyInfo* p = info()) {
        p->flags = value;
    }
    return *this;
}

template <typename T>
PropertyBuilder<T>& PropertyBuilder<T>::step(double value) {
    if (PropertyInfo* p = info()) {
        p->limits.step = value;
    }
    return *this;
}

template <typename T>
PropertyBuilder<T>& PropertyBuilder<T>::range(double min, double max) {
    PropertyInfo* p = info();
    if (p == nullptr) {
        return *this;
    }
    const std::string where = "Type '" + type_.info_->name + "', property '" + p->id + "': ";
    if (p->kind != ValueKind::Int && p->kind != ValueKind::Float) {
        type_.registry_->report(ErrorCode::InvalidRange,
                                where + "a range only applies to numeric properties.");
        return *this;
    }
    if (min > max) {
        type_.registry_->report(ErrorCode::InvalidRange, where + "min is greater than max.");
        return *this;
    }
    p->limits.min = min;
    p->limits.max = max;
    if (check_value(*p, p->default_value) == SetResult::OutOfRange) {
        type_.registry_->report(ErrorCode::DefaultOutOfRange,
                                where + "the default value is outside the range.");
    }
    return *this;
}

template <typename T>
bool TypeBuilder<T>::can_add_member(std::string_view id, std::size_t parameter_count,
                                    std::size_t expected_count) {
    if (info_ == nullptr) {
        return false;
    }
    const std::string where = "Type '" + info_->name + "', member '" + std::string(id) + "': ";
    if (!is_valid_id(id)) {
        registry_->report(ErrorCode::InvalidId,
                          where + "the id must be lowercase letters, digits and underscores, "
                                  "starting with a letter.");
        return false;
    }
    if (info_->has_member(id)) {
        registry_->report(ErrorCode::DuplicateMember, where + "the id is already used.");
        return false;
    }
    if (parameter_count != expected_count) {
        registry_->report(ErrorCode::ParameterCountMismatch,
                          where + "expected " + std::to_string(expected_count) +
                              " parameter ids, got " + std::to_string(parameter_count) + ".");
        return false;
    }
    return true;
}

template <typename T>
template <typename P>
void TypeBuilder<T>::append_parameter(std::vector<ParameterInfo>& list, std::string_view id,
                                      const std::string& where) {
    constexpr ValueKind kind = detail::kind_of_field<P>();
    ParameterInfo parameter;
    parameter.id = id;
    parameter.kind = kind;
    parameter.meta.display_name = display_name_from_id(id);
    if (!is_valid_id(id)) {
        registry_->report(ErrorCode::InvalidId,
                          where + "parameter id '" + std::string(id) + "' is not a valid id.");
    }
    for (const ParameterInfo& existing : list) {
        if (existing.id == id) {
            registry_->report(ErrorCode::DuplicateMember,
                              where + "parameter id '" + std::string(id) + "' is used twice.");
        }
    }
    if constexpr (kind == ValueKind::Choice) {
        parameter.choice = registry_->template find_choice_list<P>();
        if (parameter.choice == nullptr) {
            registry_->report(ErrorCode::UnknownChoiceList,
                              where + "parameter '" + std::string(id) +
                                  "': register its enum with add_choice_list() first.");
        }
        const bool has_entries = parameter.choice != nullptr && !parameter.choice->entries.empty();
        parameter.default_value =
            Value{has_entries ? parameter.choice->entries.front().value : std::int64_t{0}};
    } else {
        parameter.default_value = detail::to_value(P{});
    }
    list.push_back(std::move(parameter));
}

template <typename T>
template <auto Fn>
BlockBuilder TypeBuilder<T>::add_function(FunctionRole role, std::string_view id,
                                          std::initializer_list<std::string_view> parameter_ids) {
    using Traits = detail::FunctionTraits<decltype(Fn)>;
    using Result = typename Traits::result;
    static_assert(std::is_same_v<typename Traits::owner, T>,
                  "The function must belong to the type being registered.");
    constexpr std::size_t no_member = static_cast<std::size_t>(-1);
    if (!can_add_member(id, parameter_ids.size(), Traits::arity)) {
        return BlockBuilder(registry_, info_, false, no_member);
    }

    FunctionInfo function;
    function.id = id;
    function.role = role;
    function.meta.display_name = display_name_from_id(id);
    function.owner_key = detail::type_key<T>();
    function.invoke = &detail::invoke_member<Fn>;
    const std::string where = "Type '" + info_->name + "', function '" + std::string(id) + "': ";
    [&]<std::size_t... I>(std::index_sequence<I...> /*indices*/) {
        [[maybe_unused]] const std::string_view* ids = parameter_ids.begin();
        (append_parameter<std::tuple_element_t<I, typename Traits::arguments>>(function.parameters,
                                                                               ids[I], where),
         ...);
    }(std::make_index_sequence<Traits::arity>{});
    if constexpr (!std::is_void_v<Result>) {
        constexpr ValueKind result_kind = detail::kind_of_field<Result>();
        function.result_kind = result_kind;
        if constexpr (result_kind == ValueKind::Choice) {
            function.result_choice = registry_->template find_choice_list<Result>();
            if (function.result_choice == nullptr) {
                registry_->report(ErrorCode::UnknownChoiceList,
                                  where + "register the result's enum with add_choice_list() "
                                          "first.");
            }
        }
    }
    info_->functions.push_back(std::move(function));
    return BlockBuilder(registry_, info_, false, info_->functions.size() - 1);
}

template <typename T>
template <auto Fn>
BlockBuilder TypeBuilder<T>::action(std::string_view id,
                                    std::initializer_list<std::string_view> parameter_ids) {
    static_assert(std::is_void_v<typename detail::FunctionTraits<decltype(Fn)>::result>,
                  "An action must return void. Use getter() for functions that return a "
                  "value.");
    return add_function<Fn>(FunctionRole::Action, id, parameter_ids);
}

template <typename T>
template <auto Fn>
BlockBuilder TypeBuilder<T>::condition(std::string_view id,
                                       std::initializer_list<std::string_view> parameter_ids) {
    using Traits = detail::FunctionTraits<decltype(Fn)>;
    static_assert(std::is_same_v<typename Traits::result, bool>, "A condition must return bool.");
    static_assert(Traits::is_const, "A condition must be a const member function: checking a "
                                    "condition must not change anything.");
    return add_function<Fn>(FunctionRole::Condition, id, parameter_ids);
}

template <typename T>
template <auto Fn>
BlockBuilder TypeBuilder<T>::getter(std::string_view id,
                                    std::initializer_list<std::string_view> parameter_ids) {
    using Traits = detail::FunctionTraits<decltype(Fn)>;
    static_assert(!std::is_void_v<typename Traits::result>, "A getter must return a value.");
    static_assert(Traits::is_const, "A getter must be a const member function: reading a value "
                                    "must not change anything.");
    return add_function<Fn>(FunctionRole::Getter, id, parameter_ids);
}

template <typename T>
template <typename... Payload>
BlockBuilder TypeBuilder<T>::event(std::string_view id,
                                   std::initializer_list<std::string_view> parameter_ids) {
    constexpr std::size_t no_member = static_cast<std::size_t>(-1);
    if (!can_add_member(id, parameter_ids.size(), sizeof...(Payload))) {
        return BlockBuilder(registry_, info_, true, no_member);
    }
    EventInfo event_info;
    event_info.id = id;
    event_info.meta.display_name = display_name_from_id(id);
    const std::string where = "Type '" + info_->name + "', event '" + std::string(id) + "': ";
    [&]<std::size_t... I>(std::index_sequence<I...> /*indices*/) {
        [[maybe_unused]] const std::string_view* ids = parameter_ids.begin();
        (append_parameter<std::remove_cvref_t<Payload>>(event_info.parameters, ids[I], where), ...);
    }(std::index_sequence_for<Payload...>{});
    info_->events.push_back(std::move(event_info));
    return BlockBuilder(registry_, info_, true, info_->events.size() - 1);
}

template <typename E>
ChoiceBuilder<E>& ChoiceBuilder<E>::display_name(std::string_view text) {
    if (info_ != nullptr) {
        info_->meta.display_name = text;
    }
    return *this;
}

template <typename E>
ChoiceBuilder<E>& ChoiceBuilder<E>::tooltip(std::string_view text) {
    if (info_ != nullptr) {
        info_->meta.tooltip = text;
    }
    return *this;
}

template <typename E>
ChoiceBuilder<E>& ChoiceBuilder<E>::entry(E value, std::string_view id,
                                          std::string_view display_name, std::string_view tooltip) {
    if (info_ == nullptr) {
        return *this;
    }
    const auto number = static_cast<std::int64_t>(value);
    if (!is_valid_id(id)) {
        registry_->report(ErrorCode::InvalidId, "Choice list '" + info_->name + "': entry id '" +
                                                    std::string(id) + "' is not a valid id.");
        return *this;
    }
    if (info_->find_entry(id) != nullptr || info_->find_entry(number) != nullptr) {
        registry_->report(ErrorCode::DuplicateChoice, "Choice list '" + info_->name + "': entry '" +
                                                          std::string(id) +
                                                          "' repeats an id or a value.");
        return *this;
    }
    ChoiceEntry entry;
    entry.id = id;
    entry.value = number;
    entry.meta.display_name =
        display_name.empty() ? display_name_from_id(id) : std::string(display_name);
    entry.meta.tooltip = tooltip;
    info_->entries.push_back(std::move(entry));
    return *this;
}

} // namespace engine::reflect
