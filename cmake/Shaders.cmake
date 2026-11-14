# Offline shader compilation (ADR-015): HLSL is compiled by SDL_shadercross to SPIR-V (for
# the Vulkan backend) and DXIL (for Direct3D 12), and each result is embedded in the
# binary as a byte array in a generated header.

find_program(ENGINE_SHADERCROSS shadercross
    HINTS
        "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/sdl3-shadercross"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/sdl3-shadercross"
    NO_DEFAULT_PATH
    REQUIRED
)

# engine_add_shaders(<target> <shader.stage.hlsl>...)
# Each file is named <name>.<vert|frag>.hlsl. The headers are generated in
# <binary dir>/shaders/ as <name>_<stage>_<spirv|dxil>.h, holding the arrays
# <name>_<stage>_<spirv|dxil>; the directory is added to the target's include path.
function(engine_add_shaders target)
    set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    file(MAKE_DIRECTORY "${out_dir}")
    set(headers)
    foreach(source IN LISTS ARGN)
        get_filename_component(file_name "${source}" NAME)
        string(REGEX MATCH "^([a-z0-9_]+)\\.(vert|frag)\\.hlsl$" matched "${file_name}")
        if(NOT matched)
            message(FATAL_ERROR "Shader files are named <name>.<vert|frag>.hlsl: ${file_name}")
        endif()
        set(name "${CMAKE_MATCH_1}")
        set(stage "${CMAKE_MATCH_2}")
        if(stage STREQUAL "vert")
            set(stage_name vertex)
        else()
            set(stage_name fragment)
        endif()
        set(input "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
        foreach(format spirv dxil)
            if(format STREQUAL "spirv")
                set(dest SPIRV)
            else()
                set(dest DXIL)
            endif()
            set(blob "${out_dir}/${name}_${stage}.${format}")
            set(header "${out_dir}/${name}_${stage}_${format}.h")
            add_custom_command(
                OUTPUT "${header}"
                COMMAND "${ENGINE_SHADERCROSS}" "${input}" -s HLSL -d ${dest} -t ${stage_name} -o "${blob}"
                COMMAND "${CMAKE_COMMAND}" -DINPUT=${blob} -DOUTPUT=${header}
                        -DNAME=${name}_${stage}_${format} -P "${PROJECT_SOURCE_DIR}/cmake/EmbedFile.cmake"
                DEPENDS "${input}" "${PROJECT_SOURCE_DIR}/cmake/EmbedFile.cmake"
                COMMENT "Compiling shader ${file_name} to ${dest}"
                VERBATIM
            )
            list(APPEND headers "${header}")
        endforeach()
    endforeach()
    add_custom_target(${target}_shaders DEPENDS ${headers})
    add_dependencies(${target} ${target}_shaders)
    target_include_directories(${target} PRIVATE "${out_dir}")
endfunction()
