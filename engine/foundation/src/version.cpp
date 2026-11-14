#include "engine/foundation/version.h"

#include "version_config.h"

namespace engine {

Version version() noexcept {
    return Version{ENGINE_VERSION_MAJOR, ENGINE_VERSION_MINOR, ENGINE_VERSION_PATCH};
}

std::string_view version_string() noexcept {
    return ENGINE_VERSION_STRING;
}

} // namespace engine
