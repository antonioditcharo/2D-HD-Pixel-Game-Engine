# Puts the editor and everything it needs into one folder that runs on a Windows PC with
# nothing else installed: the editor with Qt's libraries (windeployqt), the player in its
# own folder (so exported games get only the player's libraries), the licence notices,
# and the sample project.
#
#   cmake -DBUILD=<build folder> -DSOURCE=<repository> -DOUT=<package folder>
#         -DQT=<Qt folder, such as C:/Qt/6.8.3/msvc2022_64> -P tools/package_windows.cmake
#
# See docs/dev/editor.md §5.

foreach(needed BUILD SOURCE OUT QT)
    if(NOT DEFINED ${needed})
        message(FATAL_ERROR "Pass -D${needed}=...")
    endif()
endforeach()

set(editor "${BUILD}/editor/app/sparxel-editor.exe")
set(player "${BUILD}/player/sparxel-player.exe")
foreach(program "${editor}" "${player}")
    if(NOT EXISTS "${program}")
        message(FATAL_ERROR "${program} is missing: build the project first.")
    endif()
endforeach()

file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}/player")

# The editor, and the libraries the build put beside it (SDL and the like).
get_filename_component(editor_folder "${editor}" DIRECTORY)
file(GLOB editor_libraries "${editor_folder}/*.dll")
file(COPY "${editor}" ${editor_libraries} DESTINATION "${OUT}")

# The player, its libraries and the notices every copy of it carries.
get_filename_component(player_folder "${player}" DIRECTORY)
file(GLOB player_libraries "${player_folder}/*.dll")
file(COPY "${player}" ${player_libraries} "${player_folder}/LICENSE.txt"
          "${player_folder}/THIRD_PARTY_NOTICES.md"
     DESTINATION "${OUT}/player")

# Qt's libraries and plugins for the editor.
execute_process(
    COMMAND "${QT}/bin/windeployqt.exe" --release --no-translations --no-opengl-sw
            --no-system-d3d-compiler "${OUT}/sparxel-editor.exe"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
)
message(STATUS "windeployqt:\n${output}${errors}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "windeployqt failed (${result}).")
endif()
foreach(needed "Qt6Widgets.dll" "Qt6Gui.dll" "Qt6Core.dll" "platforms/qwindows.dll")
    if(NOT EXISTS "${OUT}/${needed}")
        message(FATAL_ERROR "The package has no ${needed}.")
    endif()
endforeach()

# Microsoft's C++ runtime, beside the editor and beside the player, so the package runs
# on a PC that never had it installed (Microsoft's "app-local" deployment). Exported games
# copy the player's folder, so they get it too. VCToolsRedistDir is set by vcvars64.bat.
if(DEFINED ENV{VCToolsRedistDir})
    file(TO_CMAKE_PATH "$ENV{VCToolsRedistDir}" redist)
    file(GLOB runtime "${redist}/x64/Microsoft.VC*.CRT/*.dll")
endif()
if(NOT runtime)
    message(FATAL_ERROR "The C++ runtime was not found: run this from a Visual Studio "
                        "developer prompt (VCToolsRedistDir is not set).")
endif()
file(COPY ${runtime} DESTINATION "${OUT}")
file(COPY ${runtime} DESTINATION "${OUT}/player")
foreach(needed "vcruntime140.dll" "msvcp140.dll" "player/vcruntime140.dll")
    if(NOT EXISTS "${OUT}/${needed}")
        message(FATAL_ERROR "The package has no ${needed}.")
    endif()
endforeach()

# Notices: the engine's, the libraries', and Qt's (LGPL: its licence texts, and the
# libraries left as separate files that can be replaced).
file(COPY "${OUT}/player/LICENSE.txt" "${OUT}/player/THIRD_PARTY_NOTICES.md" DESTINATION "${OUT}")
if(IS_DIRECTORY "${QT}/../../Licenses")
    file(COPY "${QT}/../../Licenses/" DESTINATION "${OUT}/qt-licences")
endif()
file(WRITE "${OUT}/QT-NOTICE.txt"
"The editor (sparxel-editor.exe) uses the Qt framework, version 6, by The Qt Company
and its contributors, under the GNU Lesser General Public License version 3 (LGPL-3.0).

Qt is linked dynamically: its libraries are the Qt6*.dll files and the plugin folders
beside the editor, unmodified, and you may replace them with your own builds of Qt.
Qt's source code and licence texts: https://www.qt.io/download-open-source and
https://doc.qt.io/qt-6/licensing.html ; the LGPL: https://www.gnu.org/licenses/lgpl-3.0.html

Qt is not part of the player or of the games you export: see THIRD_PARTY_NOTICES.md.
")

# The sample, to open first.
file(COPY "${SOURCE}/samples/hello" DESTINATION "${OUT}/samples")

file(WRITE "${OUT}/README.txt"
"Sparxel: an HD-2D pixel game engine with a no-code editor.

To start:        double-click sparxel-editor.exe
To see a game:   File > Open project, and choose the folder samples\\hello
                 then press F5 (Play)
To make your own: File > New project

Nothing needs installing: Microsoft's C++ runtime is included beside the programs.

The folder 'player' holds the program that plays games; exported games are copies of it
with your game's files. Keep it beside the editor.

Licences: LICENSE.txt (Sparxel, MIT), THIRD_PARTY_NOTICES.md, QT-NOTICE.txt.
")
message(STATUS "Packaged in ${OUT}")
