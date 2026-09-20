#!/bin/sh
set -eu
flags="-O3"
suffix=""
if [ "${SANITIZE:-0}" = 1 ]; then
    flags="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"
    suffix="-sanitize"
fi
mkdir -p tests/out
g++ -std=c++11 $flags -Wall -Wextra -Itests/host -Isrc \
    tests/string_extract.cpp src/core/action_vm.cpp src/core/native32_reader.cpp \
    src/core/actions.cpp src/core/header_decryptor.cpp src/core/des_constants.cpp \
    src/core/image_decoder.cpp -lz -o "tests/out/string-extract$suffix"
if [ "${1:-}" != --build-only ]; then "tests/out/string-extract$suffix"; fi
