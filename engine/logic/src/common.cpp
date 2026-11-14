#include "common.h"

#include <algorithm>

namespace engine::logic::common {

bool is_valid_script_name(std::string_view name) noexcept {
    if (name.empty() || name.size() > 128) {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '-' || c == '.' || c == '/';
    });
}

} // namespace engine::logic::common
