#!/bin/sh
set -eu
mkdir -p tests/out
flags="-O2"
suffix=""
if [ "${SANITIZE:-0}" = 1 ]; then
    flags="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"
    suffix="-sanitize"
fi
g++ -std=c++11 $flags -Wall -Wextra -pthread -Itests/host -Isrc \
    tests/av_timing.cpp src/platform/psp_app.cpp src/platform/pause_menu.cpp src/platform/screenshots.cpp src/platform/system_info.cpp src/platform/psp_settings.cpp \
    src/platform/logo_rgba.cpp src/platform/menu_font.cpp src/core/*.cpp src/core/mpeg/*.cpp \
    -lz -o "tests/out/av$suffix"
"tests/out/av$suffix"
g++ -std=c++11 $flags -Wall -Wextra -Itests/host -Isrc \
    tests/mpeg_bits.cpp src/core/mpeg/buffer.cpp -o "tests/out/mpeg-bits$suffix"
"tests/out/mpeg-bits$suffix"
g++ -std=c++11 $flags -Wall -Wextra -Itests/host -Isrc \
    tests/mpeg_skip.cpp src/core/mpeg/*.cpp -o "tests/out/mpeg-skip$suffix"
"tests/out/mpeg-skip$suffix" tests/fixtures/mpeg-test.mpg
