#!/bin/sh
set -eu

mode="${1:---all}"
if [ "$#" -gt 1 ]; then
    echo "usage: $0 [--host|--clock|--all]" >&2
    exit 2
fi
case "$mode" in
    --host|--clock|--all) ;;
    *) echo "usage: $0 [--host|--clock|--all]" >&2; exit 2 ;;
esac

flags="-O2"
suffix=""
if [ "${SANITIZE:-0}" = 1 ]; then
    flags="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"
    suffix="-sanitize"
fi
mkdir -p tests/out

build_run() {
    variant="$1"
    define="$2"
    g++ -std=c++11 $flags $define -Wall -Wextra -Itests/host -Isrc \
        tests/core_profile.cpp src/core/action_vm.cpp src/core/audio_engine.cpp \
        src/core/mp3_software.cpp src/core/native32_reader.cpp src/core/actions.cpp \
        src/core/header_decryptor.cpp src/core/des_constants.cpp src/core/image_decoder.cpp \
        -lz -o "tests/out/core-profile-$variant$suffix"
    "tests/out/core-profile-$variant$suffix"
}

if [ "$mode" != --clock ]; then build_run host ""; fi
if [ "$mode" != --host ]; then build_run clock "-DN32_TEST_CORE_PROFILE"; fi
