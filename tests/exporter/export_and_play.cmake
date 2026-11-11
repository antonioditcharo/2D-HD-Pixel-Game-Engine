# Exports a copy of the sample with sparxel-export, then plays the exported game the way a
# player would: its executable, with no project folder given, started from another folder.
#
# -DEXPORTER=<sparxel-export> -DSAMPLE=<samples/hello> -DWORK=<scratch folder>

file(REMOVE_RECURSE "${WORK}")
file(COPY "${SAMPLE}/" DESTINATION "${WORK}/project")
file(MAKE_DIRECTORY "${WORK}/elsewhere")

execute_process(
    COMMAND "${EXPORTER}" "${WORK}/project" "${WORK}/exported"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
)
message(STATUS "Export:\n${output}${errors}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "sparxel-export failed (${result}).")
endif()

if(CMAKE_HOST_WIN32)
    set(game "${WORK}/exported/Hello Sparxel.exe")
else()
    set(game "${WORK}/exported/Hello Sparxel")
endif()
if(NOT EXISTS "${game}")
    message(FATAL_ERROR "The exported game ${game} is missing.")
endif()
# The project is moved away, so the game can only be playing its own copy.
file(RENAME "${WORK}/project" "${WORK}/project-moved")

execute_process(
    COMMAND "${game}" --headless --frames 60 --no-audio
    WORKING_DIRECTORY "${WORK}/elsewhere"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
)
message(STATUS "Game:\n${output}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "The exported game failed (${result}):\n${errors}")
endif()
if(NOT errors STREQUAL "")
    message(FATAL_ERROR "The exported game reported problems:\n${errors}")
endif()
foreach(expected "said: Welcome to the village" "The sun is out")
    string(FIND "${output}" "${expected}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "The output lacks \"${expected}\".")
    endif()
endforeach()
