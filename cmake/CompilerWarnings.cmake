# Warning flags shared by every target in the repository.
#
# Warnings are not errors by default, so a new compiler version cannot break a
# local build. CI turns them into errors with -DCMAKE_COMPILE_WARNING_AS_ERROR=ON.

function(engine_set_warnings target)
    if(MSVC)
        # Covers both MSVC and clang-cl.
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
        )
    endif()
endfunction()
