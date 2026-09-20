#!/bin/sh
set -eu

mode="${1:---verify}"
case "$mode" in
    --verify|--before|--after|--build-only) ;;
    *) echo "usage: $0 [--verify|--before [ticks]|--after [ticks]|--build-only]" >&2; exit 2 ;;
esac
if [ "$#" -gt 2 ] || { [ "$#" -eq 2 ] && [ "$mode" != --before ] && [ "$mode" != --after ]; }; then
    echo "usage: $0 [--verify|--before [ticks]|--after [ticks]|--build-only]" >&2
    exit 2
fi

flags="-O3"
suffix=""
if [ "${SANITIZE:-0}" = 1 ]; then
    flags="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"
    suffix="-sanitize"
fi
mkdir -p tests/out
g++ -std=c++11 $flags -Wall -Wextra -Itests/host -Isrc \
    tests/vm_numeric.cpp src/core/action_vm.cpp src/core/native32_reader.cpp \
    src/core/actions.cpp src/core/header_decryptor.cpp src/core/des_constants.cpp \
    src/core/image_decoder.cpp -lz -o "tests/out/vm-numeric$suffix"

if [ "$mode" != --build-only ]; then
    if [ "$#" -eq 2 ]; then
        "tests/out/vm-numeric$suffix" "$mode" "$2"
    else
        "tests/out/vm-numeric$suffix" "$mode"
    fi
fi
