# Serialization: reflected objects and logic assets to and from JSON (ADR-011).

find_package(nlohmann_json CONFIG REQUIRED)

add_library(engine_serialize STATIC
    src/json.cpp
)
add_library(engine::serialize ALIAS engine_serialize)

target_include_directories(engine_serialize PUBLIC include)
target_link_libraries(engine_serialize
    PUBLIC engine::foundation engine::logic nlohmann_json::nlohmann_json
)

engine_configure_runtime_target(engine_serialize)
