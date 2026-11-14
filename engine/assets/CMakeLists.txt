# Assets: the asset database (GUIDs in .meta files, change detection) and importers.

find_package(SDL3 CONFIG REQUIRED)

# stb_image, for its zlib decoder: Aseprite compresses its pictures with zlib.
find_path(STB_IMAGE_INCLUDE_DIR stb_image.h REQUIRED)

add_library(engine_assets STATIC
    src/aseprite.cpp
    src/assets.cpp
    src/autotile.cpp
)
add_library(engine::assets ALIAS engine_assets)

target_include_directories(engine_assets PUBLIC include)
target_include_directories(engine_assets SYSTEM PRIVATE "${STB_IMAGE_INCLUDE_DIR}")
target_link_libraries(engine_assets
    PUBLIC engine::foundation
    PRIVATE engine::serialize SDL3::SDL3
)

engine_configure_runtime_target(engine_assets)
