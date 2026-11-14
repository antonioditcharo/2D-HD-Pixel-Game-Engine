# Exporting: packages a project and the player into a folder that plays the game.

add_library(editor_exporter STATIC src/exporter.cpp)
add_library(editor::exporter ALIAS editor_exporter)
target_include_directories(editor_exporter PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(editor_exporter PUBLIC engine::game)
engine_set_warnings(editor_exporter)

# sparxel-export: the command-line front end. The player it ships defaults to the one
# this build makes.
add_executable(sparxel_export src/main.cpp)
set_target_properties(sparxel_export PROPERTIES OUTPUT_NAME sparxel-export)
target_link_libraries(sparxel_export PRIVATE editor::exporter)
target_compile_definitions(sparxel_export PRIVATE
    SPARXEL_DEFAULT_PLAYER="$<TARGET_FILE:sparxel_player>")
add_dependencies(sparxel_export sparxel_player)
engine_set_warnings(sparxel_export)
