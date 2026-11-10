#pragma once

// The GPU for tests that render. Without one they are skipped with a message, unless the
// environment variable SPARXEL_REQUIRE_GPU=1 is set (as Linux CI does), which turns a
// missing GPU into a failure.

#include "engine/render/render.h"

#include <doctest/doctest.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>

namespace test_support {

/// True if the environment variable `name` is set to "1".
inline bool environment_flag(const char* name) {
#ifdef _WIN32
    // MSVC deprecates std::getenv in favour of _dupenv_s.
    char* value = nullptr;
    std::size_t length = 0;
    const bool set =
        _dupenv_s(&value, &length, name) == 0 && value != nullptr && std::string_view(value) == "1";
    std::free(value); // NOLINT(cppcoreguidelines-no-malloc): _dupenv_s allocates with malloc
    return set;
#else
    const char* value = std::getenv(name); // NOLINT(concurrency-mt-unsafe): read once, at start
    return value != nullptr && std::string_view(value) == "1";
#endif
}

/// The shared headless device, or null when there is no GPU.
inline engine::render::Device* gpu() {
    static std::string error;
    static const std::unique_ptr<engine::render::Device> device =
        engine::render::Device::create(engine::render::DeviceOptions{true, false}, error);
    if (device == nullptr) {
        if (environment_flag("SPARXEL_REQUIRE_GPU")) {
            FAIL("A GPU is required (SPARXEL_REQUIRE_GPU=1) but none was found: " << error);
        }
        MESSAGE("Skipped: no GPU (" << error << ")");
    }
    return device.get();
}

} // namespace test_support
