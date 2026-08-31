#!/usr/bin/env bash
set -euo pipefail

OUTPUT_ROOT="output/paper_eval/final"
BUILD_DIR="build/path_tracer_build"
SEEDS="101,202,303,404,505,606,707,808"
REFERENCE_SPP=512
PAPER_ASSETS_DIR="paper/images/generated"
RPD_SCALE=1
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
  --rpd-scale T           RPD compatibility-relaxation scale (>0, default: ${RPD_SCALE})
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
        --rpd-scale) RPD_SCALE="$2"; shift 2 ;;
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
scoring_diff_sha256=clean
if ! git diff --quiet HEAD --; then
    scoring_diff_sha256="$(git diff --binary HEAD -- | sha256sum | awk '{ print $1}')"
fi

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
            --reference-spp "$REFERENCE_SPP" \
            --rpd-scale "$RPD_SCALE"
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

render_revision=""
render_diff_sha256=""
render_seeds=""
render_representative_seed=""
render_reference_spp=""
render_rpd_scale=""
for name in cornell bunny_dof dragon; do
    manifest="$OUTPUT_ROOT/$name/manifest.txt"
    scene_revision="$(awk -F= '$1 == "revision" { print $2; exit }' "$manifest")"
    scene_diff_sha256="$(awk -F= '$1 == "working_tree_diff_sha256" { print $2; exit }' "$manifest")"
    scene_seeds="$(awk -F= '$1 == "seeds" { print $2; exit }' "$manifest")"
    scene_representative_seed="$(awk -F= '$1 == "representative_seed" { print $2; exit }' "$manifest")"
    scene_reference_spp="$(awk -F= '$1 == "reference_spp" { print $2; exit }' "$manifest")"
    scene_rpd_scale="$(awk -F= '$1 == "rpd_scale" { print $2; exit }' "$manifest")"
    scene_rpd_scale="${scene_rpd_scale:-1}"
    [[ -n "$scene_revision" && -n "$scene_diff_sha256" && -n "$scene_seeds" && -n "$scene_representative_seed" && -n "$scene_reference_spp" && -n "$scene_rpd_scale" ]] || {
        echo "Incomplete provenance: $manifest" >&2
        exit 1
    }
    if [[ -z "$render_revision" ]]; then
        render_revision="$scene_revision"
        render_diff_sha256="$scene_diff_sha256"
        render_seeds="$scene_seeds"
        render_representative_seed="$scene_representative_seed"
        render_reference_spp="$scene_reference_spp"
        render_rpd_scale="$scene_rpd_scale"
    elif [[ "$scene_revision" != "$render_revision" || "$scene_diff_sha256" != "$render_diff_sha256" || "$scene_seeds" != "$render_seeds" || "$scene_representative_seed" != "$render_representative_seed" || "$scene_reference_spp" != "$render_reference_spp" || "$scene_rpd_scale" != "$render_rpd_scale" ]]; then
        echo "Mixed render revisions in $OUTPUT_ROOT" >&2
        exit 1
    fi
done

mkdir -p "$OUTPUT_ROOT/paper_assets"
if [[ "$SMOKE" == false ]]; then
    mkdir -p "$PAPER_ASSETS_DIR"
fi
for name in cornell bunny_dof dragon; do
    rm -f -- "$OUTPUT_ROOT/$name/paper_assets/${name}_rpd_light_visibility.png"
    rm -f -- "$OUTPUT_ROOT/paper_assets/${name}_rpd_light_visibility.png"
    if [[ "$SMOKE" == false ]]; then
        rm -f -- "$PAPER_ASSETS_DIR/${name}_rpd_light_visibility.png"
    fi
    for method in uniform adaptive_raw statmc_no_rpd statmc_rpd; do
        rm -f -- "$OUTPUT_ROOT/$name/paper_assets/${name}_${method}_residual.hdr"
        rm -f -- "$OUTPUT_ROOT/paper_assets/${name}_${method}_residual.hdr"
        if [[ "$SMOKE" == false ]]; then
            rm -f -- "$PAPER_ASSETS_DIR/${name}_${method}_residual.hdr"
        fi
    done
    cp "$OUTPUT_ROOT/$name/paper_assets/"*.png "$OUTPUT_ROOT/paper_assets/"
    cp "$OUTPUT_ROOT/$name/paper_assets/"*.txt "$OUTPUT_ROOT/paper_assets/"
    if [[ "$SMOKE" == false ]]; then
        cp "$OUTPUT_ROOT/$name/paper_assets/"*.png "$PAPER_ASSETS_DIR/"
        cp "$OUTPUT_ROOT/$name/paper_assets/"*.txt "$PAPER_ASSETS_DIR/"
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
    printf '\\caption{Dual-reference paired evaluation. Squared-error metrics combine each reference pair before taking roots; Log-SSIM and gradient NRMSE are averaged directly. Values are mean $\\pm$ 95\\%% CI across seeds.}\n'
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
    echo "Render revision: $render_revision"
    echo "Render working-tree diff: $render_diff_sha256"
    echo "Scoring revision: $(git rev-parse HEAD)"
    echo "Scoring working-tree diff: $scoring_diff_sha256"
    echo "Seeds: $render_seeds"
    echo "Representative seed: $render_representative_seed"
    echo "Reference spp: $render_reference_spp"
    echo "RPD scale: $render_rpd_scale"
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
