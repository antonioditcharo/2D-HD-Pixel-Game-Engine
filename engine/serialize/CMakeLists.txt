# Serialization: reflected objects, logic assets and scenes to and from JSON (ADR-011).

find_package(nlohmann_json CONFIG REQUIRED)

add_library(engine_serialize STATIC
    src/json.cpp
    src/scene.cpp
)
add_library(engine::serialize ALIAS engine_serialize)

target_include_directories(engine_serialize PUBLIC include)
target_link_libraries(engine_serialize
    PUBLIC engine::foundation engine::logic engine::world nlohmann_json::nlohmann_json
)

engine_configure_runtime_target(engine_serialize)
