#!/bin/sh
set -eu

# Build once, then invoke tests/out/image-decode-bench --before/--after in
# separate alternating processes. No sanitizer or unrelated test runs here.
variant="${1:---after}"
if [ "$#" -gt 1 ]; then
    echo "usage: $0 [--before|--after|--build-only]" >&2
    exit 2
fi
case "$variant" in
    --before|--after|--build-only) ;;
    *) echo "usage: $0 [--before|--after|--build-only]" >&2; exit 2 ;;
esac

mkdir -p tests/out
g++ -std=c++11 -O3 -Wall -Wextra -Itests/host -Isrc \
    tests/image_decode_bench.cpp src/core/image_decoder.cpp src/core/native32_reader.cpp \
    src/core/actions.cpp src/core/header_decryptor.cpp src/core/des_constants.cpp \
    -lz -o tests/out/image-decode-bench

if [ "$variant" != --build-only ]; then
    tests/out/image-decode-bench "$variant"
fi
