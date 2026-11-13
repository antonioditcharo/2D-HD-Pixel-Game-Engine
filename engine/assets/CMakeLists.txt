# Assets: the asset database (GUIDs in .meta files, change detection) and importers.

find_package(SDL3 CONFIG REQUIRED)

add_library(engine_assets STATIC
    src/assets.cpp
    src/autotile.cpp
)
add_library(engine::assets ALIAS engine_assets)

target_include_directories(engine_assets PUBLIC include)
target_link_libraries(engine_assets
    PUBLIC engine::foundation
    PRIVATE engine::serialize SDL3::SDL3
)

engine_configure_runtime_target(engine_assets)
