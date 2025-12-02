#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: ./eval.sh <command> [options] -- [args...]

Commands:
  compare-hdr   <a.hdr> <b.hdr>
  compare-png   <a.png> <b.png>
  hdr-metrics   <ref.hdr> <test.hdr> [residual.hdr]

Options:
  -B, --build-dir <dir>     Build directory (default: build)
  --no-build                Assume eval_tools is already built
  --parallel / --no-parallel  Toggle parallel build (default: on)
  -h, --help                Show this help

Examples:
  ./eval.sh compare-hdr -- output/foo_raw.hdr output/foo_denoised.hdr
  ./eval.sh hdr-metrics -- ref.hdr test.hdr residual.hdr
EOF
}

if [[ $# -lt 1 ]]; then
    usage
    exit 1
fi

COMMAND="$1"
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

if [[ "$SKIP_BUILD" == false ]]; then
    BUILD_CMD=("${REPO_ROOT}/run.sh" "-t" "eval_tools" "-B" "$BUILD_DIR")
    if [[ "$PARALLEL" == true ]]; then
        BUILD_CMD+=("--parallel")
    fi
    "${BUILD_CMD[@]}"
fi

BIN_PATH="${REPO_ROOT}/${BUILD_DIR}/shared/tools/eval_tools/eval_tools"
if [[ ! -x "$BIN_PATH" ]]; then
    echo "Executable not found at '${BIN_PATH}'. Build may have failed." >&2
    exit 1
fi

exec "$BIN_PATH" "$COMMAND" "$@"
