#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: ./eval.sh <command> [options] -- [args...]

Commands:
  compare-hdr   <a.hdr> <b.hdr>
  compare-png   <a.png> <b.png>
  hdr-metrics   <ref.hdr> <test.hdr> [residual.hdr]
  tonemap       <input.hdr> <output.png> [tonemap]

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

# CMake places binaries under build/<dir>/shared/tools by default.
BIN_CANDIDATES=(
    "${REPO_ROOT}/${BUILD_DIR}/shared/tools/eval_tools"
    "${REPO_ROOT}/${BUILD_DIR}/shared/tools/eval_tools/eval_tools"
    "${REPO_ROOT}/${BUILD_DIR}/shared/tools/tonemap_hdr"
)

BIN_PATH=""
TONEMAP_BIN=""
for cand in "${BIN_CANDIDATES[@]}"; do
    if [[ -x "$cand" ]]; then
        if [[ "$cand" == *tonemap_hdr ]]; then
            TONEMAP_BIN="$cand"
        else
            BIN_PATH="$cand"
        fi
    fi
done

if [[ "$COMMAND" == "tonemap" ]]; then
    if [[ -z "$TONEMAP_BIN" ]]; then
        echo "tonemap_hdr executable not found under '${BUILD_DIR}/shared/tools'." >&2
        exit 1
    fi
    exec "$TONEMAP_BIN" "$@"
else
    if [[ -z "$BIN_PATH" ]]; then
        echo "Executable not found in expected locations under '${BUILD_DIR}' for eval_tools. Build may have failed." >&2
        exit 1
    fi
    exec "$BIN_PATH" "$COMMAND" "$@"
fi
