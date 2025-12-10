#!/usr/bin/env bash
set -euo pipefail

# Batch runner driven by an editable run list.
# You edit scripts/batch_runs.example (or a copy) to define per-run args.
# Then call this script with a config and output base:
#   scripts/batch_trace_from_list.sh --config path_tracer/config/bunny_dof.yaml --output-base output/batch_dof --list scripts/batch_runs.example

CONFIG=""
OUTPUT_BASE="output/batch"
LIST_FILE="scripts/batch_runs.example"
EXTRA_ARGS=""
EVAL_REF=""
REF_DEFAULT=""

usage() {
    cat <<EOF
Usage: $0 --config path_tracer/config/foo.yaml [--output-base DIR] [--list FILE] [--extra-args "..."] [--eval-ref DIR]
       $0 -c path_tracer/config/foo.yaml [-o DIR] [-l FILE] [-e DIR] [--extra-args "..."]

List file format (per line):
  label|args
Examples:
  statmc_spp8|--statmc --spp 8 --adaptive-passes 2 --adaptive-spp 8
  nostatmc_spp8|--no-statmc --spp 8

Blank lines and lines starting with # are ignored.

If --eval-ref is provided, after each render we run:
  ./eval.sh compare-hdr -- <ref_dir> <output_dir>
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --config|-c) CONFIG="$2"; shift 2;;
        --output-base|-o) OUTPUT_BASE="$2"; shift 2;;
        --list|-l) LIST_FILE="$2"; shift 2;;
        --extra-args) EXTRA_ARGS="$2"; shift 2;;
        --eval-ref|-e) EVAL_REF="$2"; shift 2;;
        -h|--help) usage; exit 0;;
        *) echo "Unknown option: $1"; usage; exit 1;;
    esac
done

if [[ -z "${CONFIG}" ]]; then
    echo "Missing --config"; usage; exit 1;
fi

if [[ ! -f "${LIST_FILE}" ]]; then
    echo "List file not found: ${LIST_FILE}"; exit 1;
fi

mkdir -p "${OUTPUT_BASE}"

while IFS= read -r line; do
    [[ -z "${line}" ]] && continue
    [[ "${line}" =~ ^# ]] && continue
    label="${line%%|*}"
    args="${line#*|}"
    label_trimmed="$(echo "$label" | xargs)"
    args_trimmed="$(echo "$args" | xargs)"
    if [[ -z "${label_trimmed}" ]]; then
        echo "Skipping line with empty label: ${line}"
        continue
    fi
    out_dir="${OUTPUT_BASE}_${label_trimmed}"
    base_name="$(basename "${out_dir}")"
    hdr_path="${out_dir%/}/${base_name}.hdr"

    if [[ "${label_trimmed}" == "reference" ]]; then
        REF_DEFAULT="${out_dir}"
        if [[ -f "${hdr_path}" ]]; then
            echo "Skipping reference (exists): ${hdr_path}"
        else
            cmd=( "./trace.sh" "--" "-c" "${CONFIG}" "-o" "${out_dir}" )
            if [[ -n "${args_trimmed}" ]]; then
                # shellcheck disable=SC2206
                run_parts=( ${args_trimmed} )
                cmd+=( "${run_parts[@]}" )
            fi
            if [[ -n "${EXTRA_ARGS}" ]]; then
                # shellcheck disable=SC2206
                extra_parts=( ${EXTRA_ARGS} )
                cmd+=( "${extra_parts[@]}" )
            fi
            echo "==> ${cmd[*]}"
            "${cmd[@]}"
        fi
    else
        cmd=( "./trace.sh" "--" "-c" "${CONFIG}" "-o" "${out_dir}" )
        if [[ -n "${args_trimmed}" ]]; then
            # shellcheck disable=SC2206
            run_parts=( ${args_trimmed} )
            cmd+=( "${run_parts[@]}" )
        fi
        if [[ -n "${EXTRA_ARGS}" ]]; then
            # shellcheck disable=SC2206
            extra_parts=( ${EXTRA_ARGS} )
            cmd+=( "${extra_parts[@]}" )
        fi
        echo "==> ${cmd[*]}"
        "${cmd[@]}"
    fi

    # Evaluation: prefer explicit -e; otherwise use the 'reference' run as ref if available.
    ref_root="${EVAL_REF}"
    ref_hdr=""
    if [[ -n "${ref_root}" ]]; then
        ref_base="$(basename "${ref_root}")"
        ref_hdr="${ref_root%/}/${ref_base}.hdr"
    elif [[ -n "${REF_DEFAULT}" ]]; then
        ref_base="$(basename "${REF_DEFAULT}")"
        ref_hdr="${REF_DEFAULT%/}/${ref_base}.hdr"
    fi

    if [[ -n "${ref_hdr}" ]]; then
        out_hdr="${out_dir%/}/${base_name}.hdr"
        if [[ -f "${ref_hdr}" ]]; then
            eval_cmd=( "./eval.sh" "compare-hdr" "--" "${ref_hdr}" "${out_hdr}" )
            echo "==> ${eval_cmd[*]}"
            "${eval_cmd[@]}"
        else
            echo "Warning: reference HDR not found, skipping eval: ${ref_hdr}"
        fi
    fi
done < "${LIST_FILE}"
