#!/usr/bin/env bash

CC=${CC:-cc}
CFALGS="-std=c99 -Wall -Wextra -Wpedantic -g -O0"
SRC_DIR="src"
BUILD_DIR="build"
BIN="cinc"

mkdir -p "$BUILD_DIR"

$CC $CFLAGS \
    -I"$SRC_DIR" \
    "$SRC_DIR"/*.c \
    -o "$BUILD_DIR/$BIN"

./build/cinc
