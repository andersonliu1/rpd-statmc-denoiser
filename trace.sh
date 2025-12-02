#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_ROOT="build"
BUILD_DIR=""
TARGET="path_tracer"
SKIP_BUILD=false
PARALLEL=true
CONFIG_ARGS=()
RUN_ARGS=()

usage() {
    cat <<EOF
Usage: $(basename "$0") [options] -- <path tracer arguments>

Options:
  -t, --target <name>       Executable target to run (default: path_tracer)
  -B, --build-dir <dir>     Build directory (default: build/<target>_build)
  --cmake-arg <arg>         Extra argument forwarded to the CMake configure step
                            (repeat this option for multiple arguments)
  --no-build                Assume the target is already built and skip invoking run.sh
  -j, --no-parallel         Disable parallel build (parallel on by default)
  -h, --help                Show this message

All arguments after '--' are passed directly to the path tracer executable.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
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
        --cmake-arg)
            [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; usage; exit 1; }
            CONFIG_ARGS+=("$2")
            shift 2
            ;;
        --cmake-arg=*)
            CONFIG_ARGS+=("${1#*=}")
            shift
            ;;
        --no-build)
            SKIP_BUILD=true
            shift
            ;;
        -j|--no-parallel)
            PARALLEL=false
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            RUN_ARGS=("$@")
            break
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage
            exit 1
            ;;
    esac
done

if [[ ${#RUN_ARGS[@]} -eq 0 ]]; then
    echo "No arguments supplied for the path tracer. Pass '-- -c <config> -o <output>' etc." >&2
    usage
    exit 1
fi

if [[ -z "$BUILD_DIR" ]]; then
    BUILD_DIR="${BUILD_ROOT}/${TARGET}_build"
fi

BUILD_DIR_ABS="$BUILD_DIR"
if [[ "$BUILD_DIR" != /* ]]; then
    BUILD_DIR_ABS="${SCRIPT_DIR}/${BUILD_DIR}"
fi

if [[ "$SKIP_BUILD" == false ]]; then
    BUILD_CMD=("${SCRIPT_DIR}/run.sh" "-t" "$TARGET" "-B" "$BUILD_DIR")
    if [[ ${#CONFIG_ARGS[@]} -gt 0 ]]; then
        BUILD_CMD+=("--")
        BUILD_CMD+=("${CONFIG_ARGS[@]}")
    fi
    if [[ "$PARALLEL" == true ]]; then
        BUILD_CMD+=("--parallel")
    fi
    "${BUILD_CMD[@]}"
fi

BINARY_PATH="${BUILD_DIR_ABS}/${TARGET}/${TARGET}"
if [[ ! -f "$BINARY_PATH" ]]; then
    echo "Executable not found at '${BINARY_PATH}'. Run './run.sh' manually to build the target." >&2
    exit 1
fi

if [[ ! -x "$BINARY_PATH" ]]; then
    chmod +x "$BINARY_PATH"
fi

exec "$BINARY_PATH" "${RUN_ARGS[@]}"
