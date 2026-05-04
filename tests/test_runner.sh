#!/usr/bin/env bash
SCRIPT_DIR=$(dirname "$0")
CC="$SCRIPT_DIR/../build/cinc"
PASS=0
FAIL=0
VERBOSE=0

GREEN="\e[32m"
RED="\e[31m"
ENDCOLOR="\e[0m"

while getopts "vc:" opt; do
    case $opt in
        v) VERBOSE=1 ;;
        c) CHAPTER="$OPTARG" ;;
        *) echo "Usage: $0 [-v] [-c <chapter>]"; exit 1 ;;
    esac
done

run_test() {
    local src="$1"
    local base=$(basename "$src" .c)
    local tag="${base%%.*}"

    if [ "$tag" = "fail" ]; then
        local err
        err=$("$CC" "$src" 2>&1)
        if [ $? -ne 0 ]; then
            echo -e "${GREEN}PASS${ENDCOLOR}: $base"
            [ $VERBOSE -eq 1 ] && echo "      $err"
            ((PASS++))
        else
            echo -e "${RED}FAIL${ENDCOLOR}: $base (expected compiler error, got success)"
            ((FAIL++))
        fi

    elif [[ "$tag" =~ ^[0-9]+$ ]]; then
        local tmp
        tmp=$(mktemp -d)
        local err
        err=$(cd "$tmp" && "$OLDPWD/$CC" "$OLDPWD/$src" -o "$tmp/out" 2>&1)
        if [ $? -ne 0 ]; then
            echo -e "${RED}FAIL${ENDCOLOR}: $base (compiler rejected valid input)"
            echo "      $err"
            ((FAIL++))
            rm -rf "$tmp"; return
        fi
        "$tmp/out"
        local got=$?
        rm -rf "$tmp"
        if [ "$got" -eq "$tag" ]; then
            echo -e "${GREEN}PASS${ENDCOLOR}: $base (exited $got)"
            ((PASS++))
        else
            echo -e "${RED}FAIL${ENDCOLOR}: $base (expected exit $tag, got $got)"
            ((FAIL++))
        fi
    else
        echo "SKIP: $base (no recognized tag: use .fail or .<number>)"
    fi
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
    run_test "$f"
done
