#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="build"
CLEAN=false
CONFIG_ARGS=()
BUILD_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        clean)
            CLEAN=true
            shift
            ;;
        --)
            shift
            CONFIG_ARGS=("${@}")
            break
            ;;
        *)
            BUILD_ARGS+=("$1")
            shift
            ;;
    esac
done

if [[ ${#CONFIG_ARGS[@]} -gt 0 ]]; then
    cmake -S . -B "$BUILD_DIR" "${CONFIG_ARGS[@]}"
else
    cmake -S . -B "$BUILD_DIR"
fi

if [[ "$CLEAN" == true ]]; then
    cmake --build "$BUILD_DIR" --target clean
else
    if [[ ${#BUILD_ARGS[@]} -gt 0 ]]; then
        cmake --build "$BUILD_DIR" "${BUILD_ARGS[@]}"
    else
        cmake --build "$BUILD_DIR"
    fi
fi
