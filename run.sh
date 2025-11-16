#!/usr/bin/env bash
set -euo pipefail

BUILD_ROOT="build"
BUILD_DIR=""
TARGET="path_tracer"
CLEAN=false
CONFIG_ARGS=()
BUILD_ARGS=()

usage() {
    cat <<EOF
Usage: $(basename "$0") [clean] [options] [-- <cmake configure args>]

Options:
  -t, --target <name>     Build only the specified CMake target (default: path_tracer)
  -B, --build-dir <dir>   Override the build directory (defaults to build/<target>)
  --                      Everything after '--' is passed to the CMake configure step

Any additional arguments (before '--') are forwarded to 'cmake --build' so you can
pass options like '--parallel'.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        clean)
            CLEAN=true
            shift
            ;;
        -t|--target)
            [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; usage; exit 1; }
            TARGET="$2"
            shift 2
            ;;
        -B|--build-dir)
            [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; usage; exit 1; }
            BUILD_DIR="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            CONFIG_ARGS=("$@")
            break
            ;;
        *)
            BUILD_ARGS+=("$1")
            shift
            ;;
    esac
done

if [[ -z "$TARGET" ]]; then
    echo "Target cannot be empty" >&2
    exit 1
fi

if [[ -z "$BUILD_DIR" ]]; then
    BUILD_DIR="${BUILD_ROOT}/${TARGET}_build"
fi

if [[ "$CLEAN" == true ]]; then
    if [[ ! -d "$BUILD_DIR" ]]; then
        echo "Build directory '$BUILD_DIR' does not exist, nothing to clean." >&2
        exit 0
    fi
    cmake --build "$BUILD_DIR" --target clean
    exit 0
fi

if [[ ${#CONFIG_ARGS[@]} -gt 0 ]]; then
    cmake -S . -B "$BUILD_DIR" "${CONFIG_ARGS[@]}"
else
    cmake -S . -B "$BUILD_DIR"
fi

BUILD_CMD=(cmake --build "$BUILD_DIR" --target "$TARGET")
if [[ ${#BUILD_ARGS[@]} -gt 0 ]]; then
    BUILD_CMD+=("${BUILD_ARGS[@]}")
fi

"${BUILD_CMD[@]}"
