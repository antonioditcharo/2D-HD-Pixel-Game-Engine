# Helpers that apply the project-wide build rules from ADR-002
# (docs/plan/07-tech-stack-adrs.md).

# Rules for code that ships inside exported games: no RTTI.
function(engine_configure_runtime_target target)
    engine_set_warnings(${target})
    if(MSVC)
        target_compile_options(${target} PRIVATE /GR-)
    else()
        target_compile_options(${target} PRIVATE -fno-rtti)
    endif()
endfunction()

# Rules for test executables.
function(engine_configure_test_target target)
    engine_set_warnings(${target})
endfunction()
