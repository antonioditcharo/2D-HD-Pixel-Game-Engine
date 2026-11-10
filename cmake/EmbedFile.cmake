# Writes a C++ header that holds the bytes of a file as an array.
# Usage: cmake -DINPUT=<file> -DOUTPUT=<header> -DNAME=<identifier> -P EmbedFile.cmake

file(READ "${INPUT}" content HEX)
string(LENGTH "${content}" length)
math(EXPR size "${length} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${content}")
string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){16})" "\\1\n    " bytes "${bytes}")
file(WRITE "${OUTPUT}"
"// Generated from ${INPUT} by cmake/EmbedFile.cmake. Do not edit.
#pragma once

#include <array>
#include <cstdint>

inline constexpr std::array<std::uint8_t, ${size}> ${NAME}{
    ${bytes}
};
")
