# Game: the runtime that connects the engine's parts and runs a project.

find_package(SDL3 CONFIG REQUIRED)

add_library(engine_game STATIC
    src/battle.cpp
    src/battle_screen.cpp
    src/behaviours.cpp
    src/camera.cpp
    src/clock.cpp
    src/conversation.cpp
    src/debug_menu.cpp
    src/dialogue.cpp
    src/hud.cpp
    src/inventory.cpp
    src/localization.cpp
    src/logic_world.cpp
    src/menus.cpp
    src/shop.cpp
    src/particles.cpp
    src/quests.cpp
    src/runtime.cpp
    src/save.cpp
    src/sound.cpp
    src/tables.cpp
    src/ui_theme.cpp
    src/walker.cpp
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
        engine::audio
        engine::physics
        engine::ui
    PRIVATE
        SDL3::SDL3
)

engine_configure_runtime_target(engine_game)
