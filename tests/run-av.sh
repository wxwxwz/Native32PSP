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
    tests/av_timing.cpp src/platform/psp_app.cpp src/platform/game_presentation.cpp src/platform/pause_menu.cpp src/platform/screenshots.cpp src/platform/system_info.cpp src/platform/psp_settings.cpp \
    src/platform/logo_rgba.cpp src/platform/menu_font.cpp src/core/*.cpp src/core/mpeg/*.cpp \
    -lz -o "tests/out/av$suffix"
"tests/out/av$suffix"
g++ -std=c++11 $flags -Wall -Wextra -Itests/host -Isrc \
    tests/mpeg_bits.cpp src/core/mpeg/buffer.cpp -o "tests/out/mpeg-bits$suffix"
"tests/out/mpeg-bits$suffix"

g++ -std=c++11 $flags -Wall -Wextra -Itests/host -Isrc \
    tests/mpeg_start_codes.cpp src/core/mpeg/buffer.cpp -o "tests/out/mpeg-start-codes$suffix"
"tests/out/mpeg-start-codes$suffix"

g++ -std=c++11 $flags -Wall -Wextra -Itests/host -Isrc \
    tests/mpeg_motion.cpp src/core/mpeg/buffer.cpp -o "tests/out/mpeg-motion$suffix"
"tests/out/mpeg-motion$suffix"
g++ -std=c++11 $flags -Wall -Wextra -Itests/host -Isrc \
    tests/mpeg_skip.cpp src/core/mpeg/*.cpp -o "tests/out/mpeg-skip$suffix"
"tests/out/mpeg-skip$suffix" tests/fixtures/mpeg-test.mpg
g++ -std=c++11 $flags -Wall -Wextra -Itests/host -Isrc \
    tests/game_presentation.cpp src/platform/game_presentation.cpp -o "tests/out/game-presentation$suffix"
"tests/out/game-presentation$suffix"

g++ -std=c++11 $flags -Wall -Wextra -Itests/host -Isrc \
    tests/mpeg_io.cpp src/core/mpeg/buffer.cpp src/core/mpeg/demux.cpp -o "tests/out/mpeg-io$suffix"
"tests/out/mpeg-io$suffix" tests/fixtures/mpeg-test.mpg

sh tests/run-mpeg-vlc.sh
sh tests/run-mpeg-coeff.sh
sh tests/run-game-loop.sh
sh tests/run-image-decode.sh
sh tests/run-image-cache-budget.sh
sh tests/run-renderer-scroll.sh
sh tests/run-core-profile.sh
sh tests/run-vm-numeric.sh
sh tests/run-vm-semantics.sh
sh tests/run-mpeg-rgb.sh
sh tests/run-mpeg-native.sh
sh tests/run-string-extract.sh
