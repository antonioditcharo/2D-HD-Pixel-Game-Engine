# The editor application (ADR-005: Qt 6 Widgets). Qt is used here only: the engine and
# exported games never link it.

# The Sparxel guide: docs/dev, built into the editor as a Qt resource, so it works offline.
file(GLOB sparxel_guide CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/docs/dev/*.md" "${PROJECT_SOURCE_DIR}/docs/dev/*.png")
set(sparxel_guide_entries "")
foreach(page IN LISTS sparxel_guide)
    get_filename_component(page_name "${page}" NAME)
    string(APPEND sparxel_guide_entries "    <file alias=\"${page_name}\">${page}</file>\n")
endforeach()
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/guide.qrc"
    CONTENT "<RCC>\n  <qresource prefix=\"/guide\">\n${sparxel_guide_entries}  </qresource>\n</RCC>\n")

add_library(editor_app STATIC
    "${CMAKE_CURRENT_BINARY_DIR}/guide.qrc"
    src/asset_browser.cpp
    src/asset_browser.h
    src/doctor_panel.cpp
    src/doctor_panel.h
    src/help_window.cpp
    src/help_window.h
    src/inspector.cpp
    src/inspector.h
    src/logic_panel.cpp
    src/logic_panel.h
    src/main_window.cpp
    src/main_window.h
    src/project_panel.cpp
    src/project_panel.h
    src/recolor_dialog.cpp
    src/recolor_dialog.h
    src/scene_view.cpp
    src/scene_view.h
    src/search.cpp
    src/sfx_generator.cpp
    src/sfx_generator.h
    src/search.h
    src/sprite_studio.cpp
    src/sprite_studio.h
    src/start_page.cpp
    src/story_window.cpp
    src/story_window.h
    src/table_window.cpp
    src/table_window.h
    src/start_page.h
    src/tile_palette.cpp
    src/tile_palette.h
    src/tutorials.cpp
    src/tutorials.h
)
add_library(editor::app ALIAS editor_app)
set_target_properties(editor_app PROPERTIES AUTOMOC ON AUTORCC ON)
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
