# Physics: rigid bodies on Box2D v3 (ADR-008), behind a facade.

find_package(box2d CONFIG REQUIRED)

add_library(engine_physics STATIC
    src/physics.cpp
)
add_library(engine::physics ALIAS engine_physics)

target_include_directories(engine_physics PUBLIC include)
target_link_libraries(engine_physics PRIVATE box2d::box2d)

engine_configure_runtime_target(engine_physics)
