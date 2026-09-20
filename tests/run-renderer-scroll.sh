#!/bin/sh
# Run from psp/native32psp. Optional renderer.cpp and label compare only this
# optimization while sharing the same reader/decoder sources and class layout.
set -eu
renderer=${1:-src/core/renderer.cpp}
label=${2:-current}
flags="-O3"
features=""
if [ "$renderer" = src/core/renderer.cpp ]; then features="-DN32_EARLY_CULL"; fi
if [ "${SANITIZE:-0}" = 1 ]; then
    flags="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"
    label="$label-sanitize"
fi
mkdir -p tests/out
g++ -std=c++11 $flags $features -Wall -Wextra -Itests/host -Isrc \
    tests/renderer_scroll.cpp "$renderer" src/core/native32_reader.cpp \
    src/core/sprite_system.cpp src/core/actions.cpp src/core/header_decryptor.cpp \
    src/core/des_constants.cpp src/core/image_decoder.cpp -lz \
    -o "tests/out/renderer-scroll-$label"
"tests/out/renderer-scroll-$label"
