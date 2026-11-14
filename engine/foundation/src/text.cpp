#include "engine/foundation/text.h"

#include <array>
#include <charconv>
#include <cmath>
#include <system_error>

namespace engine::text {

[[nodiscard]] std::string format_number(double value) {
    if (std::isnan(value)) {
        return "not a number";
    }
    if (std::isinf(value)) {
        return value > 0 ? "infinity" : "minus infinity";
    }
    std::array<char, 64> buffer{};
    std::to_chars_result result{};
    if (std::trunc(value) == value && std::fabs(value) < 1e15) {
        result = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                               static_cast<long long>(value));
    } else {
        result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    }
    if (result.ec != std::errc{}) {
        return "?";
    }
    return {buffer.data(), result.ptr};
}

[[nodiscard]] std::string quote(std::string_view text) {
    return "'" + std::string(text) + "'";
}

[[nodiscard]] std::string expected_text(reflect::ValueKind kind,
                                        const reflect::ChoiceInfo* choice) {
    switch (kind) {
    case reflect::ValueKind::Bool:
        return "yes or no (true or false)";
    case reflect::ValueKind::Int:
        return "a whole number";
    case reflect::ValueKind::Float:
        return "a number";
    case reflect::ValueKind::Text:
        return "text";
    case reflect::ValueKind::Choice: {
        std::string list = "one of: ";
        if (choice != nullptr) {
            for (std::size_t i = 0; i < choice->entries.size(); ++i) {
                list += (i == 0 ? "" : ", ") + choice->entries[i].id;
            }
        }
        return list;
    }
    }
    return "a value";
}

[[nodiscard]] std::string range_text(const reflect::NumericLimits& limits) {
    if (limits.min && limits.max) {
        return "must be between " + format_number(*limits.min) + " and " +
               format_number(*limits.max);
    }
    if (limits.min) {
        return "must be at least " + format_number(*limits.min);
    }
    if (limits.max) {
        return "must be at most " + format_number(*limits.max);
    }
    return "is out of range";
}

} // namespace engine::text
