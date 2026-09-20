#!/bin/sh
set -eu
flags="-O3"
suffix=""
if [ "${SANITIZE:-0}" = 1 ]; then
    flags="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -DNATIVE32_MPEG_RGB_SANITIZE"
    suffix="-sanitize"
fi
mkdir -p tests/out
g++ -std=c++11 $flags -Wall -Wextra -Itests/host -Isrc \
    tests/mpeg_rgb.cpp src/core/mpeg/video.cpp src/core/mpeg/buffer.cpp \
    -o "tests/out/mpeg-rgb$suffix"
if [ "${1:-}" = "--build-only" ]; then
    exit 0
fi
"tests/out/mpeg-rgb$suffix" "$@"
