# Game: the runtime that connects the engine's parts and runs a project.

find_package(SDL3 CONFIG REQUIRED)

add_library(engine_game STATIC
    src/behaviours.cpp
    src/clock.cpp
    src/logic_world.cpp
    src/runtime.cpp
    src/save.cpp
)
add_library(engine::game ALIAS engine_game)

target_include_directories(engine_game PUBLIC include)
target_link_libraries(engine_game
    PUBLIC
        engine::foundation
        engine::logic
        engine::world
        engine::serialize
        engine::assets
        engine::render
        engine::scene2d
        engine::input
    PRIVATE
        SDL3::SDL3
)

engine_configure_runtime_target(engine_game)
