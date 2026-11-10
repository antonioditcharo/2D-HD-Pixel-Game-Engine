#pragma once

// Plain-language text for messages about values: the wording shared by the logic
// compiler, the logic VM and the serializer, so that every message about a value reads
// the same way wherever it comes from.

#include "engine/foundation/reflect.h"

#include <string>
#include <string_view>

namespace engine::text {

/// Formats a number for messages: whole numbers without a decimal point, others in the
/// shortest form that reads back exactly. Not-a-number and infinities are spelled out.
[[nodiscard]] std::string format_number(double value);

/// Puts single quotes around `text`, as messages quote names and values.
[[nodiscard]] std::string quote(std::string_view text);

/// What a slot of the given kind accepts: "yes or no (true or false)", "a whole number",
/// "one of: north, east".
[[nodiscard]] std::string expected_text(reflect::ValueKind kind, const reflect::ChoiceInfo* choice);

/// "must be between 1 and 999", "must be at least 0".
[[nodiscard]] std::string range_text(const reflect::NumericLimits& limits);

} // namespace engine::text
