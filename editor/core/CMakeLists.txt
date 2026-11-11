# The editor's model: an open project and scene, with undo, picking and a preview. No
# user interface, so it builds and is tested without Qt.

add_library(editor_core STATIC
    src/document.cpp
    src/logic_text.cpp
    src/preview.cpp
)
add_library(editor::core ALIAS editor_core)
target_include_directories(editor_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(editor_core PUBLIC engine::game)
engine_set_warnings(editor_core)
