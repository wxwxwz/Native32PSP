#!/bin/sh
# Run from psp/native32psp. Optional reader source and label allow a negative
# control using the old fixed cache budget with the same reader class layout.
set -eu
reader=${1:-src/core/native32_reader.cpp}
label=${2:-current}
flags="-O3"
if [ "${SANITIZE:-0}" = 1 ]; then
    flags="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"
    label="$label-sanitize"
fi
mkdir -p tests/out
g++ -std=c++11 $flags -Wall -Wextra -Itests/host -Isrc \
    tests/image_cache_budget.cpp "$reader" src/core/renderer.cpp \
    src/core/sprite_system.cpp src/core/image_decoder.cpp src/core/actions.cpp \
    src/core/header_decryptor.cpp src/core/des_constants.cpp -lz \
    -o "tests/out/image-cache-budget-$label"
"tests/out/image-cache-budget-$label"
