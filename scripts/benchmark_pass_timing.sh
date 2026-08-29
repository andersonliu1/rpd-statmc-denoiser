#!/usr/bin/env bash
set -euo pipefail

CONFIG=""
OUTPUT_BASE="output/timing_bench"
SPP_LIST=""
ADAPTIVE_PASSES_LIST="0,1,2"
ADAPTIVE_SPP=""
STATMC_MODE="on"
EXTRA_ARGS=""
TRACE_SCRIPT="./trace.sh"
SKIP_BUILD=true

usage() {
    cat <<EOF
Usage: $0 --config path_tracer/config/scene.yaml [options]

Options:
  --config PATH               YAML config (required)
  --output-base DIR           Base output directory (default: ${OUTPUT_BASE})
  --spp LIST                  Comma-separated SPP values (e.g. 2,4,8). If empty, use YAML.
  --adaptive-passes LIST      Comma-separated pass counts (default: ${ADAPTIVE_PASSES_LIST})
  --adaptive-spp N            Override adaptive spp (optional)
  --statmc-mode MODE          on/off (default: ${STATMC_MODE})
  --extra-args "..."          Extra renderer args appended after -- to trace.sh
  --trace-script PATH         Alternate trace wrapper (default: ${TRACE_SCRIPT})
  --build                     Build before each run instead of using --no-build

Output:
  Writes per-run logs under OUTPUT_BASE and prints CSV timing rows to stdout.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --config) CONFIG="$2"; shift 2;;
        --output-base) OUTPUT_BASE="$2"; shift 2;;
        --spp) SPP_LIST="$2"; shift 2;;
        --adaptive-passes) ADAPTIVE_PASSES_LIST="$2"; shift 2;;
        --adaptive-spp) ADAPTIVE_SPP="$2"; shift 2;;
        --statmc-mode) STATMC_MODE="$2"; shift 2;;
        --extra-args) EXTRA_ARGS="$2"; shift 2;;
        --trace-script) TRACE_SCRIPT="$2"; shift 2;;
        --build) SKIP_BUILD=false; shift;;
        -h|--help) usage; exit 0;;
        *) echo "Unknown option: $1" >&2; usage; exit 1;;
    esac
done

if [[ -z "${CONFIG}" ]]; then
    echo "Missing --config" >&2
    usage
    exit 1
fi

IFS=',' read -r -a PASS_ARR <<< "${ADAPTIVE_PASSES_LIST}"
if [[ -n "${SPP_LIST}" ]]; then
    IFS=',' read -r -a SPP_ARR <<< "${SPP_LIST}"
else
    SPP_ARR=("")
fi

mkdir -p "${OUTPUT_BASE}"

echo "scene,spp,statmc,adaptive_passes,adaptive_spp,wall_ms,render_ms,adaptive_render_ms,denoise_ms,var_denoise_ms,sensitivity_ms,adaptive_count_ms,postprocess_ms"

run_case() {
    local spp="$1"
    local adaptive_passes="$2"
    local label="spp${spp:-yaml}_${STATMC_MODE}_passes${adaptive_passes}"
    local out_dir="${OUTPUT_BASE}/${label}"
    local log_path="${OUTPUT_BASE}/${label}.log"
    mkdir -p "${out_dir}"

    local cmd=( "${TRACE_SCRIPT}" )
    if [[ "${SKIP_BUILD}" == true ]]; then
        cmd+=( "--no-build" )
    fi
    cmd+=( "--" "-c" "${CONFIG}" "-o" "${out_dir}" )

    if [[ -n "${spp}" ]]; then
        cmd+=( "--spp" "${spp}" )
    fi

    if [[ "${STATMC_MODE}" == "on" ]]; then
        cmd+=( "--statmc" "--adaptive-passes" "${adaptive_passes}" )
        if [[ -n "${ADAPTIVE_SPP}" ]]; then
            cmd+=( "--adaptive-spp" "${ADAPTIVE_SPP}" )
        fi
    else
        cmd+=( "--no-statmc" )
    fi

    if [[ -n "${EXTRA_ARGS}" ]]; then
        # shellcheck disable=SC2206
        local extra_parts=( ${EXTRA_ARGS} )
        cmd+=( "${extra_parts[@]}" )
    fi

    python3 - "${log_path}" "${CONFIG}" "${spp:-yaml}" "${STATMC_MODE}" "${adaptive_passes}" "${ADAPTIVE_SPP:-yaml}" "${cmd[@]}" <<'PY'
import csv
import pathlib
import re
import subprocess
import sys
import time

log_path = pathlib.Path(sys.argv[1])
scene = pathlib.Path(sys.argv[2]).stem
spp = sys.argv[3]
statmc = sys.argv[4]
adaptive_passes = sys.argv[5]
adaptive_spp = sys.argv[6]
cmd = sys.argv[7:]

start = time.perf_counter()
proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=True)
wall_ms = (time.perf_counter() - start) * 1000.0
log_path.write_text(proc.stdout)

patterns = {
    "render_ms": r"Rendering completed in (\d+) ms",
    "adaptive_render_ms": r"Adaptive rendering completed in (\d+) ms",
    "denoise_ms": r"StatMC denoise completed in (\d+) ms",
    "var_denoise_ms": r"Variance-of-mean denoise completed in (\d+) ms",
    "sensitivity_ms": r"Sensitivity recompute completed in (\d+) ms",
    "adaptive_count_ms": r"Adaptive sample count computation completed in (\d+) ms",
}

values = {}
for key, pattern in patterns.items():
    values[key] = sum(int(match) for match in re.findall(pattern, proc.stdout))

postprocess_ms = values["denoise_ms"] + values["var_denoise_ms"] + values["sensitivity_ms"] + values["adaptive_count_ms"]

row = [
    scene,
    spp,
    statmc,
    adaptive_passes,
    adaptive_spp,
    f"{wall_ms:.1f}",
    values["render_ms"],
    values["adaptive_render_ms"],
    values["denoise_ms"],
    values["var_denoise_ms"],
    values["sensitivity_ms"],
    values["adaptive_count_ms"],
    postprocess_ms,
]
writer = csv.writer(sys.stdout)
writer.writerow(row)
PY
}

for adaptive_passes in "${PASS_ARR[@]}"; do
    for spp in "${SPP_ARR[@]}"; do
        run_case "${spp}" "${adaptive_passes}"
    done
done
