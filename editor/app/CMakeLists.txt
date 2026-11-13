# The editor application (ADR-005: Qt 6 Widgets). Qt is used here only: the engine and
# exported games never link it.

add_library(editor_app STATIC
    src/inspector.cpp
    src/inspector.h
    src/logic_panel.cpp
    src/logic_panel.h
    src/main_window.cpp
    src/main_window.h
    src/project_panel.cpp
    src/project_panel.h
    src/scene_view.cpp
    src/scene_view.h
    src/search.cpp
    src/sfx_generator.cpp
    src/sfx_generator.h
    src/search.h
    src/sprite_studio.cpp
    src/sprite_studio.h
    src/start_page.cpp
    src/start_page.h
    src/tile_palette.cpp
    src/tile_palette.h
)
add_library(editor::app ALIAS editor_app)
set_target_properties(editor_app PROPERTIES AUTOMOC ON)
target_include_directories(editor_app PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(editor_app PUBLIC editor::core editor::exporter Qt6::Widgets)
# Qt's lower-case keywords (emit, signals, slots) clash with the engine's names: use
# Q_EMIT and Q_SIGNALS.
target_compile_definitions(editor_app PUBLIC QT_NO_KEYWORDS)
engine_set_warnings(editor_app)

add_executable(sparxel_editor WIN32 src/main.cpp)
set_target_properties(sparxel_editor PROPERTIES OUTPUT_NAME sparxel-editor AUTOMOC ON)
target_link_libraries(sparxel_editor PRIVATE editor::app)
target_compile_definitions(sparxel_editor PRIVATE
    SPARXEL_DEFAULT_PLAYER="$<TARGET_FILE:sparxel_player>")
add_dependencies(sparxel_editor sparxel_player)
engine_set_warnings(sparxel_editor)
