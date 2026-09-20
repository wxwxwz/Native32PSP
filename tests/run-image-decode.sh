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
    tests/image_decode.cpp src/core/image_decoder.cpp src/core/native32_reader.cpp \
    src/core/actions.cpp src/core/header_decryptor.cpp src/core/des_constants.cpp \
    -lz -o "tests/out/image-decode$suffix"
"tests/out/image-decode$suffix"
