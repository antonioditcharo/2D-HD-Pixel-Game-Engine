# Logic runtime: the sandboxed Luau VM that runs no-code Rules and Sequences (ADR-007).

find_package(unofficial-luau CONFIG REQUIRED)

add_library(engine_logic STATIC
    src/common.cpp
    src/compiler.cpp
    src/vm.cpp
)
add_library(engine::logic ALIAS engine_logic)

target_include_directories(engine_logic PUBLIC include)
target_link_libraries(engine_logic
    PUBLIC engine::foundation
    PRIVATE unofficial::luau::Luau.VM unofficial::luau::Luau.Compiler
)

engine_configure_runtime_target(engine_logic)
