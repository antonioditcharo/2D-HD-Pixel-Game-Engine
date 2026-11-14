# Plays the sample project with sparxel-player, headless, and checks what it prints.
# A copy is played, so nothing is written into the repository (.meta files, saves).
#
# -DPLAYER=<sparxel-player> -DSAMPLE=<samples/hello> -DWORK=<scratch folder>
# [-DSTYLE=diorama] [-DFRAMES=120] [-DEXPECT=<another line the output must have>]

file(REMOVE_RECURSE "${WORK}")
file(COPY "${SAMPLE}/" DESTINATION "${WORK}")
if(STYLE)
    file(READ "${WORK}/project.sparxel.json" project)
    string(REPLACE "\"classic_2d\"" "\"${STYLE}\"" project "${project}")
    file(WRITE "${WORK}/project.sparxel.json" "${project}")
endif()

if(NOT FRAMES)
    set(FRAMES 120)
endif()
execute_process(
    COMMAND "${PLAYER}" "${WORK}" --headless --frames ${FRAMES} --no-audio
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
)
message(STATUS "Output:\n${output}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "sparxel-player failed (${result}):\n${errors}")
endif()
if(NOT errors STREQUAL "")
    message(FATAL_ERROR "sparxel-player reported problems:\n${errors}")
endif()
foreach(expected "said: Welcome to the village" "The sun is out" ${EXPECT})
    string(FIND "${output}" "${expected}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "The output lacks \"${expected}\".")
    endif()
endforeach()
