# Classic 2D scenes: 2D transforms and sprites, drawn from the world through the renderer.

add_library(engine_scene2d STATIC
    src/scene2d.cpp
)
add_library(engine::scene2d ALIAS engine_scene2d)

target_include_directories(engine_scene2d PUBLIC include)
target_link_libraries(engine_scene2d
    PUBLIC engine::foundation engine::world engine::render engine::assets
    PRIVATE engine::serialize
)

engine_configure_runtime_target(engine_scene2d)
