# sparxel-player: plays a project folder. What exported games run.

add_executable(sparxel_player main.cpp)
set_target_properties(sparxel_player PROPERTIES OUTPUT_NAME sparxel-player)
find_package(SDL3 CONFIG REQUIRED)
target_link_libraries(sparxel_player PRIVATE engine::game SDL3::SDL3)
# The licence files every copy of the player must carry; exporting ships them with it.
add_custom_command(TARGET sparxel_player POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${PROJECT_SOURCE_DIR}/LICENSE" "$<TARGET_FILE_DIR:sparxel_player>/LICENSE.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
        "$<TARGET_FILE_DIR:sparxel_player>/THIRD_PARTY_NOTICES.md"
    VERBATIM)
engine_configure_runtime_target(sparxel_player)
