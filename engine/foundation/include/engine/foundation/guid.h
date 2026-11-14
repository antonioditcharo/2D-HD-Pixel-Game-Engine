#pragma once

// Globally unique ids for everything a project references: entities, assets, blocks.
// Paths and names are for people; references are GUIDs, so renaming or moving something
// never breaks a reference (plan/03 §4).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace engine {

/// A 128-bit random id. The zero value means "none".
struct Guid {
    std::uint64_t high = 0;
    std::uint64_t low = 0;

    [[nodiscard]] constexpr bool is_valid() const noexcept { return high != 0 || low != 0; }
    friend constexpr bool operator==(Guid, Guid) = default;
    friend constexpr auto operator<=>(Guid, Guid) = default;

    /// A new random GUID (version 4 layout, RFC 9562). Never the zero value.
    [[nodiscard]] static Guid generate();

    /// Reads 32 hexadecimal digits, upper or lower case, without separators. Returns
    /// nothing for any other text, and for the zero value.
    [[nodiscard]] static std::optional<Guid> parse(std::string_view text);

    /// 32 lowercase hexadecimal digits, as stored in project files.
    [[nodiscard]] std::string to_string() const;
};

/// For std::unordered_map keys.
struct GuidHash {
    [[nodiscard]] std::size_t operator()(Guid guid) const noexcept {
        return static_cast<std::size_t>(guid.high ^ (guid.low * 0x9E3779B97F4A7C15ULL));
    }
};

} // namespace engine
