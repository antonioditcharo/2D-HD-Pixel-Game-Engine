# Game: the runtime that connects the engine's parts. Today: logic reaching the world.

add_library(engine_game STATIC
    src/logic_world.cpp
)
add_library(engine::game ALIAS engine_game)

target_include_directories(engine_game PUBLIC include)
target_link_libraries(engine_game
    PUBLIC engine::foundation engine::logic engine::world
)

engine_configure_runtime_target(engine_game)
