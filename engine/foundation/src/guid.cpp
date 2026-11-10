#include "engine/foundation/guid.h"

#include <array>
#include <mutex>
#include <random>

namespace engine {

namespace {

/// A generator seeded once per process from the operating system's random source.
/// Guarded by a mutex, so GUIDs can be generated from any thread.
std::uint64_t random_word() {
    static std::mutex mutex;
    static std::mt19937_64 generator = [] {
        std::random_device device;
        std::seed_seq seed{device(), device(), device(), device(),
                           device(), device(), device(), device()};
        return std::mt19937_64(seed);
    }();
    const std::scoped_lock lock(mutex);
    return generator();
}

[[nodiscard]] int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

} // namespace

Guid Guid::generate() {
    Guid guid{random_word(), random_word()};
    // Version 4 and variant bits (RFC 9562 section 5.4). They also make zero impossible.
    guid.high = (guid.high & ~0xF000ULL) | 0x4000ULL;
    guid.low = (guid.low & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
    return guid;
}

std::optional<Guid> Guid::parse(std::string_view text) {
    if (text.size() != 32) {
        return std::nullopt;
    }
    Guid guid;
    for (std::size_t i = 0; i < 32; ++i) {
        const int digit = hex_value(text[i]);
        if (digit < 0) {
            return std::nullopt;
        }
        std::uint64_t& word = i < 16 ? guid.high : guid.low;
        word = (word << 4U) | static_cast<std::uint64_t>(digit);
    }
    if (!guid.is_valid()) {
        return std::nullopt;
    }
    return guid;
}

std::string Guid::to_string() const {
    constexpr std::array<char, 16> digits{'0', '1', '2', '3', '4', '5', '6', '7',
                                          '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::string out(32, '0');
    for (std::size_t i = 0; i < 16; ++i) {
        const unsigned shift = static_cast<unsigned>(60 - 4 * i);
        out[i] = digits[(high >> shift) & 0xFU];
        out[16 + i] = digits[(low >> shift) & 0xFU];
    }
    return out;
}

} // namespace engine
