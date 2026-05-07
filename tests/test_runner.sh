#!/usr/bin/env bash

SCRIPT_DIR=$(dirname "$0")
CC="$SCRIPT_DIR/../build/cinc"

PASS=0
FAIL=0
VERBOSE=0
CHAPTER=""

GREEN="\e[32m"
RED="\e[31m"
YELLOW="\e[33m"
ENDCOLOR="\e[0m"

while getopts "vc:" opt; do
    case $opt in
        v) VERBOSE=1 ;;
        c) CHAPTER="$OPTARG" ;;
        *) echo "Usage: $0 [-v] [-c <chapter>]"; exit 1 ;;
    esac
done

tag_of() {
    local src="$1"
    local base
    base="$(basename "$src" .c)"
    echo "${base%%.*}"
}

pass() {
    echo -e "${GREEN}PASS${ENDCOLOR}: $1"
    ((PASS++))
}

fail() {
    echo -e "${RED}FAIL${ENDCOLOR}: $1"
    [ $# -gt 1 ] && echo "     $2"
    ((FAIL++))
}

compile_and_run() {
    local expected="$1"
    shift

    local name="$1"
    shift

    local tmp
    tmp="$(mktemp -d)"

    local err
    err=$("$CC" "$@" -o "$tmp/out 2>&1")
    local status=$?

    if [ "$status" -ne 0 ]; then
        fail "$name (compiler rejected valid input)" "$err"
        rm -rf "$tmp"
        return
    fi

    "$tmp/out"
    local got=$?

    rm -rf "$tmp"

    if [ "$got" -eq "$expected" ]; then
        pass "$name (exited $got)"
    else
        fail "$name (expected exit $expected, got $got)"
    fi
}

run_single_test() {
    local src="$1"
    local base tag

    base="$(basename "$src" .c)"
    tag="$(tag_of "$src")"

    if [ "$tag" = "fail" ]; then
        local err
        err=$("$CC" "$src" 2>&1)
        if [ $? -ne 0 ]; then
            pass "$base"
            [ "$VERBOSE" -eq 1 ] && echo "      $err"
        else
            fail "$base (expected compiler error, got success)"
        fi
    elif [[ "$tag" =~ ^[0-9]+$ ]]; then
        compile_and_run "$tag" "$base" "$src"
    else
        echo -e "${YELLOW}SKIP${ENDCOLOR}: $base (no recognized tag)"
    fi
}

run_library_test() {
    local client="$1"
    local dir base tag stem
    local inputs=()

    dir="$(dirname "$client")"
    base="$(basename "$client" .c)"
    tag="$(tag_of "$client")"

    if ! [[ "$tag" =~ ^[0-9]+$ ]]; then
        echo -e "${YELLOW}SKIP${ENDCOLOR}: $base (library client needs numeric tag)"
        return
    fi

    stem="${base#*.}"
    stem="${stem%_client}"

    for f in "$dir"/"$stem".c "$dir"/"$stem".*.c; do
        [ -e "$f" ] || continue
        [ "$f" = "$client" ] && continue
        inputs+=("$f")
    done

    inputs+=("$client")

    compile_and_run "$tag" "libraries/$base" "${inputs[@]}"
}

shopt -s globstar nullglob


if [ -n "$CHAPTER" ]; then
    SEARCH_DIR="$SCRIPT_DIR/chapter$CHAPTER"
    if [ ! -d "$SEARCH_DIR" ]; then
        echo "Error: chapter $CHAPTER not found ($SEARCH_DIR)"
        exit 1
    fi
else
    SEARCH_DIR="$SCRIPT_DIR"
fi

for f in "$SEARCH_DIR"/**/*.c; do
    case "$f" in
        */libraries/*) continue ;;
    esac
    run_single_test "$f"
done

for f in "$SEARCH_DIR"/**/libraries/*_client.c; do
    run_library_test "$f"
done

echo
echo "Passed: $PASS"
echo "Failed: $FAIL"

[ "$FAIL" -eq 0 ]
