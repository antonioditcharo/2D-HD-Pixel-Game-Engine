#pragma once

// Helpers shared by the serializer's source files. Private to the serialize module.

#include "engine/serialize/json.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace engine::serialize::detail {

/// Appends a key to a JSON Pointer, escaping '~' and '/' as RFC 6901 requires.
[[nodiscard]] std::string child(std::string_view path, std::string_view key);
/// Appends a list index to a JSON Pointer.
[[nodiscard]] std::string child(std::string_view path, std::size_t index);

/// The value of a whole number above 0, whether JSON holds it as signed or unsigned.
[[nodiscard]] std::optional<std::uint64_t> positive_integer(const Json& json);

/// How a JSON value reads in a message: 'text', 3, "a list", "nothing".
[[nodiscard]] std::string describe(const Json& json);

} // namespace engine::serialize::detail
