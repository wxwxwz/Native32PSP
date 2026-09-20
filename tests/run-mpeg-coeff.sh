#!/bin/sh
set -eu
mkdir -p tests/out
flags="-O3"
suffix=""
if [ "${SANITIZE:-0}" = 1 ]; then
    flags="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"
    suffix="-sanitize"
fi
g++ -std=c++11 $flags -Wall -Wextra -Itests/host -Isrc \
    tests/mpeg_coeff.cpp src/core/mpeg/buffer.cpp -o "tests/out/mpeg-coeff$suffix"
"tests/out/mpeg-coeff$suffix"
