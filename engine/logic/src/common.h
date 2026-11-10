#pragma once

// Helpers shared by the compiler and the VM: plain-language text, so that compile-time and
// run-time messages read the same way, and the rules for script names. Private to the
// logic module.

#include "engine/foundation/reflect.h"

#include <string>
#include <string_view>

namespace engine::logic::common {

/// Formats a number for messages: whole numbers without a decimal point, others in the
/// shortest form that reads back exactly. Not-a-number and infinities are spelled out.
[[nodiscard]] std::string format_number(double value);

/// Puts single quotes around `text`, as messages quote names and values.
[[nodiscard]] std::string quoted(std::string_view text);

/// What a slot of the given kind accepts: "yes or no (true or false)", "a whole number",
/// "one of: north, east".
[[nodiscard]] std::string expected_text(reflect::ValueKind kind, const reflect::ChoiceInfo* choice);

/// "must be between 1 and 999", "must be at least 0".
[[nodiscard]] std::string range_text(const reflect::NumericLimits& limits);

/// Script names are 1 to 128 letters, digits, '_', '-', '.' or '/', for example
/// "maps/village-01/intro". They appear in error messages as "name:line:", so they must not
/// contain ':' or spaces.
[[nodiscard]] bool is_valid_script_name(std::string_view name) noexcept;

} // namespace engine::logic::common
