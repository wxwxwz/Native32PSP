#!/bin/sh
set -eu
mkdir -p tests/out
flags="-O2"
if [ "${SANITIZE:-0}" = 1 ]; then flags="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"; fi
g++ -std=c++11 $flags -DN32_TEST_PSP_MP3 -Wall -Wextra -Itests/host -Isrc \
    tests/mp3_backend.cpp src/core/*.cpp src/core/mpeg/*.cpp -lz -o tests/out/mp3-backend
tests/out/mp3-backend

g++ -std=c++11 $flags -Wall -Wextra -Itests/host -Isrc tests/mp3_software.cpp src/core/mp3_software.cpp -o tests/out/mp3-software
tests/out/mp3-software
