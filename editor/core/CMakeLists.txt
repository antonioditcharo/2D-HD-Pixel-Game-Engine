# The editor's model: an open project and scene, with undo, picking and a preview. No
# user interface, so it builds and is tested without Qt.

add_library(editor_core STATIC
    src/asset_tools.cpp
    src/doctor.cpp
    src/document.cpp
    src/logic_edit.cpp
    src/logic_text.cpp
    src/looks.cpp
    src/new_project.cpp
    src/preview.cpp
    src/sfx.cpp
    src/snapshots.cpp
    src/sprite.cpp
    src/story.cpp
    src/table_edit.cpp
)
add_library(editor::core ALIAS editor_core)
target_include_directories(editor_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(editor_core PUBLIC engine::game)
engine_set_warnings(editor_core)
