# sparxel-player: plays a project folder. What exported games run.

add_executable(sparxel_player main.cpp)
set_target_properties(sparxel_player PROPERTIES OUTPUT_NAME sparxel-player)
target_link_libraries(sparxel_player PRIVATE engine::game)
engine_configure_runtime_target(sparxel_player)
