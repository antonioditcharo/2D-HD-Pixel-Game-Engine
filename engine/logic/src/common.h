#pragma once

// Helpers shared by the compiler and the VM. Private to the logic module.

#include "engine/foundation/text.h"

#include <string_view>

namespace engine::logic::common {

using engine::text::expected_text;
using engine::text::format_number;
using engine::text::quote;
using engine::text::range_text;

/// Script names are 1 to 128 letters, digits, '_', '-', '.' or '/', for example
/// "maps/village-01/intro". They appear in error messages as "name:line:", so they must not
/// contain ':' or spaces.
[[nodiscard]] bool is_valid_script_name(std::string_view name) noexcept;

} // namespace engine::logic::common
