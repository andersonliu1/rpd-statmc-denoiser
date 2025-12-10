#!/usr/bin/env bash
set -euo pipefail

# Simple batch runner for multiple SPP / StatMC / adaptive combos.
# Example:
#   scripts/batch_trace.sh \
#     --config path_tracer/config/bunny_dof.yaml \
#     --output-base output/batch_bunny \
#     --spp 4,8,16 \
#     --statmc-modes on,off \
#     --adaptive-passes 2 \
#     --adaptive-spp 8

CONFIG=""
OUTPUT_BASE="output/batch"
SPP_LIST=""
STATMC_MODES="on,off"       # comma-separated: on/off
ADAPTIVE_SPP=""
ADAPTIVE_PASSES=""
EXTRA_ARGS=""

usage() {
    cat <<EOF
Usage: $0 --config path_tracer/config/scene.yaml [options]

Options:
  --config PATH             YAML config (required)
  --output-base DIR         Base output directory (default: ${OUTPUT_BASE})
  --spp LIST                Comma-separated SPP values (e.g. 4,8,16). If empty, use YAML.
  --statmc-modes LIST       Comma-separated modes: on/off (default: ${STATMC_MODES})
  --adaptive-passes N       Override adaptive passes (optional)
  --adaptive-spp N          Override adaptive spp (optional)
  --extra-args \"...\"        Extra args passed after -- to trace.sh (quoted)
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --config) CONFIG="$2"; shift 2;;
        --output-base) OUTPUT_BASE="$2"; shift 2;;
        --spp) SPP_LIST="$2"; shift 2;;
        --statmc-modes) STATMC_MODES="$2"; shift 2;;
        --adaptive-passes) ADAPTIVE_PASSES="$2"; shift 2;;
        --adaptive-spp) ADAPTIVE_SPP="$2"; shift 2;;
        --extra-args) EXTRA_ARGS="$2"; shift 2;;
        -h|--help) usage; exit 0;;
        *) echo "Unknown option: $1"; usage; exit 1;;
    esac
done

if [[ -z "${CONFIG}" ]]; then
    echo "Missing --config"; usage; exit 1;
fi

IFS=',' read -r -a SPP_ARR <<< "${SPP_LIST}"
IFS=',' read -r -a STATMC_ARR <<< "${STATMC_MODES}"

mkdir -p "${OUTPUT_BASE}"

run_cmd() {
    local spp="$1"
    local statmc_mode="$2"
    local out_dir="$3"

    cmd=( "./trace.sh" "--" "-c" "${CONFIG}" "-o" "${out_dir}" )
    if [[ -n "${spp}" ]]; then
        cmd+=( "--spp" "${spp}" )
    fi

    if [[ "${statmc_mode}" == "on" ]]; then
        cmd+=( "--statmc" )
        [[ -n "${ADAPTIVE_PASSES}" ]] && cmd+=( "--adaptive-passes" "${ADAPTIVE_PASSES}" )
        [[ -n "${ADAPTIVE_SPP}" ]] && cmd+=( "--adaptive-spp" "${ADAPTIVE_SPP}" )
    else
        cmd+=( "--no-statmc" )
    fi

    if [[ -n "${EXTRA_ARGS}" ]]; then
        # shellcheck disable=SC2206
        extra_parts=( ${EXTRA_ARGS} )
        cmd+=( "${extra_parts[@]}" )
    fi

    echo "==> ${cmd[*]}"
    "${cmd[@]}"
}

for statmc_mode in "${STATMC_ARR[@]}"; do
    if [[ -n "${SPP_LIST}" ]]; then
        for spp in "${SPP_ARR[@]}"; do
            out="${OUTPUT_BASE}/spp${spp}_${statmc_mode}"
            run_cmd "${spp}" "${statmc_mode}" "${out}"
        done
    else
        out="${OUTPUT_BASE}/${statmc_mode}"
        run_cmd "" "${statmc_mode}" "${out}"
    fi
done
