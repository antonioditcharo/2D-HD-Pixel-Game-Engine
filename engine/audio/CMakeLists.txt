# Audio: sound effects and music on miniaudio (ADR-009). miniaudio is one header with no
# CMake package; its implementation is compiled once, in src/audio.cpp.

find_path(MINIAUDIO_INCLUDE_DIR miniaudio.h REQUIRED)
find_package(Threads REQUIRED)

add_library(engine_audio STATIC
    src/audio.cpp
)
add_library(engine::audio ALIAS engine_audio)

target_include_directories(engine_audio PUBLIC include)
# A system include: warnings inside miniaudio are not ours to fix.
target_include_directories(engine_audio SYSTEM PRIVATE "${MINIAUDIO_INCLUDE_DIR}")
target_link_libraries(engine_audio PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
if(UNIX AND NOT APPLE)
    target_link_libraries(engine_audio PRIVATE m)
endif()

engine_configure_runtime_target(engine_audio)
