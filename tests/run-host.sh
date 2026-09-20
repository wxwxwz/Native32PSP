#!/bin/sh
# Run from psp/native32psp. Optional arg: complete baseline source directory.
set -eu
variant=${1:-.}
mkdir -p tests/out
name=$(basename "$variant")
if [ "$name" = . ]; then name=current; fi
flags="-O3"
features=""
if [ "$variant" = . ]; then features="-DN32_REUSE_OUTPUT"; fi
if [ "${SANITIZE:-0}" = 1 ]; then
    flags="-O1 -g -DN32_SANITIZE -fsanitize=address,undefined -fno-omit-frame-pointer"
    name="$name-sanitize"
fi
g++ -std=c++11 $flags $features -Wall -Wextra -Wno-mismatched-new-delete \
    -Itests/host -I"$variant/src" -Isrc tests/performance.cpp \
    "$variant/src/core/renderer.cpp" "$variant/src/core/audio_engine.cpp" \
    "$variant/src/core/mpeg/video.cpp" "$variant/src/core/mpeg/buffer.cpp" \
    "$variant/src/core/native32_reader.cpp" "$variant/src/core/sprite_system.cpp" "$variant/src/core/actions.cpp" \
    "$variant/src/core/header_decryptor.cpp" "$variant/src/core/des_constants.cpp" \
    "$variant/src/core/image_decoder.cpp" "$variant/src/core/mp3_software.cpp" \
    -lz -o "tests/out/$name"
"tests/out/$name"
