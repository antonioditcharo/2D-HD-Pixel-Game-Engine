cmake_minimum_required(VERSION 3.25)

project(engine
    VERSION 0.0.1
    DESCRIPTION "No-code engine for pixel-art diorama games"
    LANGUAGES CXX
)

option(ENGINE_BUILD_TESTS "Build the unit tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(EngineTargets)

add_subdirectory(engine)
add_subdirectory(player)
add_subdirectory(editor)

if(ENGINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
