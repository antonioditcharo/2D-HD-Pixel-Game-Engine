#include "engine/foundation/version.h"

#include <doctest/doctest.h>

#include <string>

TEST_CASE("version() matches the version CMake built the project as") {
    const engine::Version expected{ENGINE_EXPECTED_VERSION_MAJOR, ENGINE_EXPECTED_VERSION_MINOR,
                                   ENGINE_EXPECTED_VERSION_PATCH};
    CHECK(engine::version() == expected);
}

TEST_CASE("version_string() is major.minor.patch") {
    const engine::Version v = engine::version();
    const std::string built =
        std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
    CHECK(engine::version_string() == built);
    CHECK(engine::version_string() == ENGINE_EXPECTED_VERSION_STRING);
}
