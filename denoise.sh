#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: ./denoise.sh <mode> --config <file> [additional args...]

Modes:
  joint    Run joint bilateral filter (denoising/joint_bilateral)
  trous    Placeholder for future à trous implementation

Examples:
  ./denoise.sh joint -c denoising/config/joint_bilateral.yaml
  ./denoise.sh joint --config myconfig.yaml --sigma-c 0.2 --output out/denoise
EOF
}

if [[ $# -lt 1 ]]; then
    usage
    exit 1
fi

mode="$1"
shift

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

case "$mode" in
    joint)
        exe="${repo_root}/build/denoising/joint_bilateral"
        if [[ ! -x "$exe" ]]; then
            echo "joint_bilateral not built; building with CMake..." >&2
            cmake -S "$repo_root" -B "$repo_root/build"
            cmake --build "$repo_root/build" --target joint_bilateral
        fi
        exec "$exe" "$@"
        ;;
    trous)
        echo "à trous denoiser not yet implemented. Please build and register its executable when available." >&2
        exit 1
        ;;
    *)
        echo "Unknown mode: '$mode'" >&2
        usage
        exit 1
        ;;
esac
