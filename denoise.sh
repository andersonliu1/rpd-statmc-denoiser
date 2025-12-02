#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: ./denoise.sh <mode> [options] -- [denoiser args]

Modes:
  joint    Run joint bilateral filter (denoising/joint_bilateral)
  trous    Run à trous wavelet filter (denoising/atrous_wavelet)

Options:
  -B, --build-dir <dir>   Build directory (default: build)
  --no-build              Assume executables are already built
  --parallel / --no-parallel  Toggle parallel build (default: on)
  -h, --help              Show this help

Examples:
  ./denoise.sh joint -- --config denoising/config/joint_bilateral.yaml --raw output/foo/foo_raw.hdr ...
  ./denoise.sh trous -- --config denoising/config/atrous_wavelet.yaml --raw ... --iterations 5
EOF
}

if [[ $# -lt 1 ]]; then
    usage
    exit 1
fi

MODE="$1"
shift

BUILD_DIR="build"
SKIP_BUILD=false
PARALLEL=true

while [[ $# -gt 0 ]]; do
    case "$1" in
        -B|--build-dir)
            [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; usage; exit 1; }
            BUILD_DIR="$2"
            shift 2
            ;;
        --no-build)
            SKIP_BUILD=true
            shift
            ;;
        --parallel)
            PARALLEL=true
            shift
            ;;
        --no-parallel)
            PARALLEL=false
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            break
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage
            exit 1
            ;;
    esac
done

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

case "$MODE" in
    joint)
        TARGET="joint_bilateral"
        ;;
    trous)
        TARGET="atrous_wavelet"
        ;;
    *)
        echo "Unknown mode: '$MODE'" >&2
        usage
        exit 1
        ;;
esac

if [[ "$SKIP_BUILD" == false ]]; then
    BUILD_CMD=("${REPO_ROOT}/run.sh" "-t" "$TARGET" "-B" "$BUILD_DIR")
    if [[ "$PARALLEL" == true ]]; then
        BUILD_CMD+=("--parallel")
    fi
    "${BUILD_CMD[@]}"
fi

BIN_PATH="${REPO_ROOT}/${BUILD_DIR}/${TARGET}/${TARGET}"
if [[ ! -x "$BIN_PATH" ]]; then
    echo "Executable not found at '${BIN_PATH}'. Build may have failed." >&2
    exit 1
fi

exec "$BIN_PATH" "$@"
