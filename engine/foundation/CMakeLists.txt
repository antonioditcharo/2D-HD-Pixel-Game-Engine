# Foundation: the lowest layer of the runtime. Everything else depends on it.

configure_file(
    src/version_config.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/version_config.h"
    @ONLY
)

add_library(engine_foundation STATIC
    src/guid.cpp
    src/reflect.cpp
    src/text.cpp
    src/version.cpp
)
add_library(engine::foundation ALIAS engine_foundation)

target_include_directories(engine_foundation
    PUBLIC include
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

engine_configure_runtime_target(engine_foundation)
