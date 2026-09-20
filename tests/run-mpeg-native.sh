#!/bin/sh
set -eu
flags="-O3"
suffix=""
if [ "${SANITIZE:-0}" = 1 ]; then
    flags="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"
    suffix="-sanitize"
fi
mkdir -p tests/out
# Only GamePresentation::prepare is timed. Discard its unused GU draw function
# so this CPU benchmark needs no graphics-device simulation or platform stubs.
g++ -std=c++11 $flags -Wall -Wextra -ffunction-sections -fdata-sections -Itests/host -Isrc \
    tests/mpeg_native.cpp src/core/*.cpp src/core/mpeg/*.cpp src/platform/game_presentation.cpp \
    -Wl,--gc-sections -lz -o "tests/out/mpeg-native$suffix"
if [ "${1:-}" = "--build-only" ]; then
    exit 0
fi
"tests/out/mpeg-native$suffix" "$@"
