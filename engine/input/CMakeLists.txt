# Input: named actions bound to keys and gamepad buttons, recording and replay.

find_package(SDL3 CONFIG REQUIRED)

add_library(engine_input STATIC
    src/input.cpp
    src/sdl_events.cpp
)
add_library(engine::input ALIAS engine_input)

target_include_directories(engine_input PUBLIC include)
target_link_libraries(engine_input
    PUBLIC engine::foundation
    PRIVATE SDL3::SDL3
)

engine_configure_runtime_target(engine_input)
