# Editor-side code. Nothing here ships in exported games.

add_subdirectory(core)
add_subdirectory(exporter)

# The editor application needs Qt 6 (ADR-005). Without it, everything else still builds;
# SPARXEL_REQUIRE_EDITOR (CI) makes a missing Qt an error instead.
option(SPARXEL_REQUIRE_EDITOR "Fail if Qt 6 is not found, instead of skipping the editor" OFF)
find_package(Qt6 6.4 QUIET COMPONENTS Widgets)
if(Qt6_FOUND)
    message(STATUS "Qt ${Qt6_VERSION} found: building the editor")
    add_subdirectory(app)
elseif(SPARXEL_REQUIRE_EDITOR)
    message(FATAL_ERROR "Qt 6 (Widgets) was not found, and SPARXEL_REQUIRE_EDITOR is on.")
else()
    message(STATUS "Qt 6 not found: the editor application is not built")
endif()
