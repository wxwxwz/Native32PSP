#!/bin/sh
# Run from psp/native32psp; synthetic data and outputs stay in tests/out.
set -eu
mkdir -p tests/out
flags="-O3"
suffix=""
if [ "${SANITIZE:-0}" = 1 ]; then
    flags="-O1 -g -DN32_SANITIZE -fsanitize=address,undefined -fno-omit-frame-pointer"
    suffix="-sanitize"
fi
g++ -std=c++11 $flags -Wall -Wextra -Wno-mismatched-new-delete \
    -Itests/host -Isrc tests/game_loop.cpp src/core/*.cpp src/core/mpeg/*.cpp \
    -lz -o "tests/out/game-loop$suffix"
"tests/out/game-loop$suffix"
