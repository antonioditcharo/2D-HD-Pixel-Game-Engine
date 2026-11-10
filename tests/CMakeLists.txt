find_package(doctest CONFIG REQUIRED)

add_executable(engine_foundation_tests
    test_main.cpp
    foundation/version_test.cpp
)
target_link_libraries(engine_foundation_tests PRIVATE
    engine::foundation
    doctest::doctest
)
target_compile_definitions(engine_foundation_tests PRIVATE
    ENGINE_EXPECTED_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    ENGINE_EXPECTED_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    ENGINE_EXPECTED_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    ENGINE_EXPECTED_VERSION_STRING="${PROJECT_VERSION}"
)
engine_configure_test_target(engine_foundation_tests)

add_test(NAME foundation COMMAND engine_foundation_tests)
