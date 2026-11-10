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
