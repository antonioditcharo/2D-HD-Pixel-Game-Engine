find_package(doctest CONFIG REQUIRED)

add_executable(engine_foundation_tests
    test_main.cpp
    foundation/version_test.cpp
    foundation/reflect_test.cpp
    foundation/reflect_functions_test.cpp
)
target_link_libraries(engine_foundation_tests PRIVATE
    engine::foundation
    doctest::doctest
)
target_compile_definitions(engine_foundation_tests PRIVATE
    # Make doctest include the real <ostream> instead of forward-declaring it.
    # Without this, checks that print a std::string_view fail to compile with
    # the MSVC standard library (MSVC 14.51 and clang-cl), because doctest then
    # instantiates operator<< on an incomplete std::basic_ostream.
    DOCTEST_CONFIG_USE_STD_HEADERS
    ENGINE_EXPECTED_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    ENGINE_EXPECTED_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    ENGINE_EXPECTED_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    ENGINE_EXPECTED_VERSION_STRING="${PROJECT_VERSION}"
)
engine_configure_test_target(engine_foundation_tests)

add_test(NAME foundation COMMAND engine_foundation_tests)

add_executable(engine_logic_tests
    test_main.cpp
    logic/compiler_test.cpp
    logic/stress_test.cpp
    logic/vm_test.cpp
)
target_link_libraries(engine_logic_tests PRIVATE
    engine::logic
    doctest::doctest
)
target_compile_definitions(engine_logic_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
engine_configure_test_target(engine_logic_tests)

add_test(NAME logic COMMAND engine_logic_tests)

add_executable(engine_serialize_tests
    test_main.cpp
    serialize/json_test.cpp
    serialize/scene_test.cpp
)
target_link_libraries(engine_serialize_tests PRIVATE
    engine::serialize
    doctest::doctest
)
target_compile_definitions(engine_serialize_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
engine_configure_test_target(engine_serialize_tests)

add_test(NAME serialize COMMAND engine_serialize_tests)

add_executable(engine_world_tests
    test_main.cpp
    world/world_test.cpp
)
target_link_libraries(engine_world_tests PRIVATE
    engine::world
    doctest::doctest
)
target_compile_definitions(engine_world_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
engine_configure_test_target(engine_world_tests)

add_test(NAME world COMMAND engine_world_tests)

add_executable(engine_render_tests
    test_main.cpp
    render/render_test.cpp
)
target_link_libraries(engine_render_tests PRIVATE
    engine::render
    doctest::doctest
)
target_compile_definitions(engine_render_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
target_include_directories(engine_render_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
engine_configure_test_target(engine_render_tests)

add_test(NAME render COMMAND engine_render_tests)

add_executable(engine_assets_tests
    test_main.cpp
    assets/assets_test.cpp
)
target_link_libraries(engine_assets_tests PRIVATE
    engine::assets
    doctest::doctest
)
target_compile_definitions(engine_assets_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
engine_configure_test_target(engine_assets_tests)

add_test(NAME assets COMMAND engine_assets_tests)

add_executable(engine_scene2d_tests
    test_main.cpp
    scene2d/scene2d_test.cpp
)
target_link_libraries(engine_scene2d_tests PRIVATE
    engine::scene2d
    engine::serialize
    doctest::doctest
)
target_compile_definitions(engine_scene2d_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
target_include_directories(engine_scene2d_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
engine_configure_test_target(engine_scene2d_tests)

add_test(NAME scene2d COMMAND engine_scene2d_tests)

add_executable(engine_audio_tests
    test_main.cpp
    audio/audio_test.cpp
)
target_link_libraries(engine_audio_tests PRIVATE
    engine::audio
    doctest::doctest
)
target_compile_definitions(engine_audio_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
target_include_directories(engine_audio_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
engine_configure_test_target(engine_audio_tests)

add_test(NAME audio COMMAND engine_audio_tests)

add_executable(engine_game_tests
    test_main.cpp
    game/behaviours_test.cpp
    game/clock_test.cpp
    game/logic_world_test.cpp
    game/runtime_test.cpp
)
target_link_libraries(engine_game_tests PRIVATE
    engine::game
    doctest::doctest
)
target_compile_definitions(engine_game_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
target_include_directories(engine_game_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
engine_configure_test_target(engine_game_tests)

add_test(NAME game COMMAND engine_game_tests)

find_package(SDL3 CONFIG REQUIRED)
add_executable(engine_input_tests
    test_main.cpp
    input/input_test.cpp
)
target_link_libraries(engine_input_tests PRIVATE
    engine::input
    SDL3::SDL3
    doctest::doctest
)
target_compile_definitions(engine_input_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
engine_configure_test_target(engine_input_tests)

add_test(NAME input COMMAND engine_input_tests)
