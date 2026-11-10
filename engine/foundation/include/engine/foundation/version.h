#pragma once

#include <string_view>

namespace engine {

/// The engine version, following semantic versioning.
struct Version {
    int major;
    int minor;
    int patch;

    friend constexpr bool operator==(const Version&, const Version&) = default;
};

/// Returns the version this library was built as.
[[nodiscard]] Version version() noexcept;

/// Returns the version as text, for example "0.0.1".
[[nodiscard]] std::string_view version_string() noexcept;

} // namespace engine
