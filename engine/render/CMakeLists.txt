# Rendering: the GPU device, textures, render targets and sprites on the SDL3 GPU API
# (ADR-004), with HLSL shaders compiled offline (ADR-015).

find_package(SDL3 CONFIG REQUIRED)
include(Shaders)

add_library(engine_render STATIC
    src/render.cpp
)
add_library(engine::render ALIAS engine_render)

target_include_directories(engine_render PUBLIC include)
target_link_libraries(engine_render PRIVATE SDL3::SDL3)
engine_add_shaders(engine_render
    shaders/sprite.vert.hlsl
    shaders/sprite.frag.hlsl
    shaders/world.vert.hlsl
    shaders/world.frag.hlsl
    shaders/shadow.frag.hlsl
    shaders/sun_depth.frag.hlsl
    shaders/post.vert.hlsl
    shaders/post_blur.frag.hlsl
    shaders/post_final.frag.hlsl
)

engine_configure_runtime_target(engine_render)
