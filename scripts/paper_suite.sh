#!/usr/bin/env bash
set -euo pipefail

OUTPUT_ROOT="output/paper_eval/final"
BUILD_DIR="build/path_tracer_build"
SEEDS="101,202,303,404,505,606,707,808"
REFERENCE_SPP=512
PAPER_ASSETS_DIR="paper/images/generated"
SMOKE=false
RESCORE=false
OUTPUT_ROOT_EXPLICIT=false

usage() {
    cat <<EOF
Usage: $0 [options]

Runs the complete Cornell, Bunny DOF, and Dragon paper evaluation, combines
the metrics, and installs ready-named images under paper/images/generated.

Options:
  --output-root DIR       Suite output (default: ${OUTPUT_ROOT})
  --build-dir DIR         Shared build directory (default: ${BUILD_DIR})
  --seeds CSV             Paired seeds (default: ${SEEDS})
  --reference-spp N       Spp for independent references (default: ${REFERENCE_SPP})
  --paper-assets-dir DIR  Installed paper images (default: ${PAPER_ASSETS_DIR})
  --smoke                  Validate only: one seed, 128px, 64-spp references
  --rescore                Reuse existing renders; recompute dual-reference metrics
  -h, --help               Show this help
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --output-root) OUTPUT_ROOT="$2"; OUTPUT_ROOT_EXPLICIT=true; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --seeds) SEEDS="$2"; shift 2 ;;
        --reference-spp) REFERENCE_SPP="$2"; shift 2 ;;
        --paper-assets-dir) PAPER_ASSETS_DIR="$2"; shift 2 ;;
        --smoke) SMOKE=true; shift ;;
        --rescore) RESCORE=true; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
    esac
done

if [[ "$SMOKE" == true ]]; then
    SEEDS="101"
    REFERENCE_SPP=64
    if [[ "$OUTPUT_ROOT_EXPLICIT" == false ]]; then
        OUTPUT_ROOT="output/paper_eval/smoke"
    fi
fi

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

if [[ "$RESCORE" == false ]]; then
    CORNELL_CONFIG="path_tracer/config/cornell.yaml"
    BUNNY_CONFIG="path_tracer/config/bunny_dof.yaml"
    DRAGON_CONFIG="path_tracer/config/model.yaml"
    mkdir -p "$OUTPUT_ROOT/paper_configs"
    cp "$CORNELL_CONFIG" "$OUTPUT_ROOT/paper_configs/cornell.yaml"
    cp "$BUNNY_CONFIG" "$OUTPUT_ROOT/paper_configs/bunny_dof.yaml"
    cp "$DRAGON_CONFIG" "$OUTPUT_ROOT/paper_configs/dragon.yaml"
    CORNELL_CONFIG="$OUTPUT_ROOT/paper_configs/cornell.yaml"
    BUNNY_CONFIG="$OUTPUT_ROOT/paper_configs/bunny_dof.yaml"
    DRAGON_CONFIG="$OUTPUT_ROOT/paper_configs/dragon.yaml"
    if [[ "$SMOKE" == true ]]; then
        sed -i -E 's/^image_width:.*/image_width: 128/; s/^image_height:.*/image_height: 128/' \
            "$CORNELL_CONFIG" \
            "$BUNNY_CONFIG" \
            "$DRAGON_CONFIG"
    fi

    run_scene() {
        local name="$1"
        local config="$2"
        local spp="$3"
        ./scripts/paper_eval.sh \
            --config "$config" \
            --output-root "$OUTPUT_ROOT/$name" \
            --build-dir "$BUILD_DIR" \
            --scene-label "$name" \
            --seeds "$SEEDS" \
            --uniform-spp "$spp" \
            --reference-spp "$REFERENCE_SPP"
    }

    run_scene cornell "$CORNELL_CONFIG" 16
    run_scene bunny_dof "$BUNNY_CONFIG" 16
    run_scene dragon "$DRAGON_CONFIG" 10
fi

for name in cornell bunny_dof dragon; do
    ./scripts/paper_rescore.sh \
        --output-root "$OUTPUT_ROOT/$name" \
        --build-dir "$BUILD_DIR"
done

mkdir -p "$OUTPUT_ROOT/paper_assets"
if [[ "$SMOKE" == false ]]; then
    mkdir -p "$PAPER_ASSETS_DIR"
fi
for name in cornell bunny_dof dragon; do
    cp "$OUTPUT_ROOT/$name/paper_assets/"* "$OUTPUT_ROOT/paper_assets/"
    if [[ "$SMOKE" == false ]]; then
        cp "$OUTPUT_ROOT/$name/paper_assets/"* "$PAPER_ASSETS_DIR/"
    fi
done

for report in results summary paired_deltas paired_summary reference_noise; do
    awk 'FNR == 1 && NR != 1 { next } { print }' \
        "$OUTPUT_ROOT/cornell/${report}.csv" \
        "$OUTPUT_ROOT/bunny_dof/${report}.csv" \
        "$OUTPUT_ROOT/dragon/${report}.csv" > "$OUTPUT_ROOT/all_${report}.csv"
done
for report in rescore_rows rescore_seed_means rescore_summary rescore_paired rescore_paired_summary rescore_reference_noise rescore_invalid; do
    awk 'FNR == 1 && NR != 1 { next } { print }' \
        "$OUTPUT_ROOT/cornell/${report}.csv" \
        "$OUTPUT_ROOT/bunny_dof/${report}.csv" \
        "$OUTPUT_ROOT/dragon/${report}.csv" > "$OUTPUT_ROOT/all_${report}.csv"
done

{
    printf '\\begin{table*}[t]\n'
    printf '\\centering\n'
    printf '\\caption{Dual-reference paired evaluation. Error metrics combine each reference pair in squared-error space before aggregation; values are mean $\\pm$ 95\\%% CI across seeds.}\n'
    printf '\\label{tab:generated-metrics}\n'
    printf '\\begin{tabular}{llrrrr}\n'
    printf '\\toprule\n'
    printf 'Scene & Method & HDR NRMSE & Log-SSIM & Gradient NRMSE & PNG RMSE \\\\\n'
    printf '\\midrule\n'
    cat "$OUTPUT_ROOT/cornell/rescore_table_rows.tex"
    cat "$OUTPUT_ROOT/bunny_dof/rescore_table_rows.tex"
    cat "$OUTPUT_ROOT/dragon/rescore_table_rows.tex"
    printf '\\bottomrule\n'
    printf '\\end{tabular}\n'
    printf '\\end{table*}\n'
    printf '\n\\begin{table*}[t]\n'
    printf '\\centering\n'
    printf '\\caption{Paired contribution of RPD relative to StatMC without RPD. Positive quality deltas favor RPD; runtime ratios above one indicate added cost. Values are mean $\\pm$ 95\\%% CI across seeds.}\n'
    printf '\\label{tab:generated-rpd-ablation}\n'
    printf '\\begin{tabular}{lrrrrr}\n'
    printf '\\toprule\n'
    printf 'Scene & HDR NRMSE$^2$ reduction & Log-SSIM gain & Gradient reduction & PNG MSE reduction & Runtime ratio \\\\\n'
    printf '\\midrule\n'
    awk -F, '
    function metric(value,ci,format) {
        if (ci=="NA") return sprintf(format,value)
        return sprintf(format " $\\pm$ " format,value,ci)
    }
    $2 == "rpd-vs-no_rpd" {
        gsub(/_/, "\\_", $1)
        printf "%s & %s & %s & %s & %s & %s \\\\\n", $1, \
            metric($6,$7,"%.6g"),metric($8,$9,"%.4f"),metric($10,$11,"%.4f"), \
            metric($12,$13,"%.6g"),metric($14,$15,"%.3f")
    }' "$OUTPUT_ROOT/all_rescore_paired_summary.csv"
    printf '\\bottomrule\n'
    printf '\\end{tabular}\n'
    printf '\\end{table*}\n'
} > "$OUTPUT_ROOT/generated_metrics_table.tex"

if [[ "$SMOKE" == false ]]; then
    cp "$OUTPUT_ROOT/generated_metrics_table.tex" paper/generated_metrics_table.tex
fi
{
    echo "Generated by scripts/paper_suite.sh"
    echo "Revision: $(git rev-parse HEAD)"
    echo "Representative seed: ${SEEDS%%,*}"
    echo "Images are ready under: $OUTPUT_ROOT/paper_assets"
    echo "Combined metrics: $OUTPUT_ROOT/all_results.csv"
    echo "Dual-reference metrics: $OUTPUT_ROOT/all_rescore_summary.csv"
    echo "Paired RPD evidence: $OUTPUT_ROOT/all_rescore_paired_summary.csv"
    echo "Reference noise: $OUTPUT_ROOT/all_rescore_reference_noise.csv"
    echo "LaTeX table: $OUTPUT_ROOT/generated_metrics_table.tex"
} > "$OUTPUT_ROOT/README.txt"

echo "Paper suite complete: $OUTPUT_ROOT"
if [[ "$SMOKE" == false ]]; then
    echo "Paper images installed: $PAPER_ASSETS_DIR"
    echo "LaTeX table installed: paper/generated_metrics_table.tex"
else
    echo "Smoke outputs only; paper files were not replaced"
fi
