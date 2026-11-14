# Warning flags shared by every target in the repository.
#
# Warnings are not errors by default, so a new compiler version cannot break a
# local build. CI turns them into errors with -DCMAKE_COMPILE_WARNING_AS_ERROR=ON.

function(engine_set_warnings target)
    if(MSVC)
        # Covers both MSVC and clang-cl.
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            # Aggregates are built from their leading fields on purpose (`{kind, slot}`);
            # the rest keep their defaults. MSVC does not warn about this either.
            target_compile_options(${target} PRIVATE -Wno-missing-field-initializers)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
            # Aggregates are built from their leading fields on purpose; see above.
            -Wno-missing-field-initializers
        )
    endif()
endfunction()
