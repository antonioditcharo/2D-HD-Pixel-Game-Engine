# Interface: text in pixel fonts (ADR-010), message boxes.

add_library(engine_ui STATIC
    src/text.cpp
)
add_library(engine::ui ALIAS engine_ui)

target_include_directories(engine_ui PUBLIC include)
target_link_libraries(engine_ui PUBLIC engine::render)

engine_configure_runtime_target(engine_ui)
