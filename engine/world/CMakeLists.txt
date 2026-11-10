# World: entities, hierarchy and components stored in flecs (ADR-006).

find_package(flecs CONFIG REQUIRED)

add_library(engine_world STATIC
    src/world.cpp
)
add_library(engine::world ALIAS engine_world)

target_include_directories(engine_world PUBLIC include)
target_link_libraries(engine_world
    PUBLIC engine::foundation
    PRIVATE $<IF:$<TARGET_EXISTS:flecs::flecs>,flecs::flecs,flecs::flecs_static>
)

engine_configure_runtime_target(engine_world)
