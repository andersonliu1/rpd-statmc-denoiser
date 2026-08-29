#!/usr/bin/env bash
set -euo pipefail

BUILD_ROOT="build"
BUILD_DIR=""
TARGET="path_tracer"
CLEAN=false
TEST=false
CONFIG_ARGS=()
BUILD_ARGS=()

safe_remove_dir() {
    local dir="$1"
    if [[ -z "$dir" || "$dir" == "/" || "$dir" == "." ]]; then
        echo "Refusing to remove unsafe directory '$dir'." >&2
        exit 1
    fi
    rm -rf "$dir"
}

perform_clean() {
    if [[ ! -d "$BUILD_DIR" ]]; then
        echo "Build directory '$BUILD_DIR' does not exist, nothing to clean."
        exit 0
    fi

    if [[ -f "$BUILD_DIR/CMakeCache.txt" ]]; then
        if ! cmake --build "$BUILD_DIR" --target clean; then
            echo "Warning: cmake clean failed, removing build directory anyway." >&2
        fi
    fi

    echo "Removing build directory '$BUILD_DIR'."
    safe_remove_dir "$BUILD_DIR"
    exit 0
}

usage() {
    cat <<EOF
Usage: $(basename "$0") [clean|test] [options] [-- <cmake configure args>]

Commands:
  clean                 Remove the configured build directory.
  test                  Build and run the CTest suite.

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
        test)
            TEST=true
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
    if [[ "$TEST" == true ]]; then
        BUILD_DIR="${BUILD_ROOT}/path_tracer_build"
    else
        BUILD_DIR="${BUILD_ROOT}/${TARGET}_build"
    fi
fi

if [[ "$CLEAN" == true ]]; then
    perform_clean
fi

if [[ ${#CONFIG_ARGS[@]} -gt 0 ]]; then
    cmake -S . -B "$BUILD_DIR" "${CONFIG_ARGS[@]}"
else
    cmake -S . -B "$BUILD_DIR"
fi

if [[ "$TEST" == true ]]; then
    BUILD_CMD=(cmake --build "$BUILD_DIR" --target stats_test config_test)
else
    BUILD_CMD=(cmake --build "$BUILD_DIR" --target "$TARGET")
fi
if [[ ${#BUILD_ARGS[@]} -gt 0 ]]; then
    BUILD_CMD+=("${BUILD_ARGS[@]}")
fi

"${BUILD_CMD[@]}"

if [[ "$TEST" == true ]]; then
    ctest --test-dir "$BUILD_DIR" --output-on-failure
fi
