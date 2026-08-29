#!/usr/bin/env bash
set -euo pipefail

CONFIG=""
OUTPUT_ROOT="output/paper_eval"
BUILD_DIR="build/path_tracer_build"
SEEDS="101,202,303,404,505,606,707,808"
UNIFORM_SPP=16
REFERENCE_SPP=512
REFERENCE_SEED=99991
REFERENCE_SEED2=99992
REFERENCE_CHECK=true
REPRESENTATIVE_SEED=""
SCENE_LABEL=""

usage() {
    cat <<EOF
Usage: $0 --config path_tracer/config/scene.yaml [options]

Runs uniform, StatMC/RPD-off, and StatMC/RPD-on with paired seeds. It writes
extended metrics, paired confidence intervals, a reference-noise check, and
ready-named paper images.

Options:
  --config PATH                YAML scene configuration (required)
  --output-root DIR            Output root (default: ${OUTPUT_ROOT})
  --build-dir DIR              Shared build directory (default: ${BUILD_DIR})
  --seeds CSV                  Paired low-spp seeds (default: ${SEEDS})
  --uniform-spp N              Uniform baseline spp (default: ${UNIFORM_SPP})
  --reference-spp N            Spp for both references (default: ${REFERENCE_SPP})
  --reference-seed N           Primary reference seed (default: ${REFERENCE_SEED})
  --reference-seed2 N          Independent reference seed (default: ${REFERENCE_SEED2})
  --representative-seed N      Seed used for paper images (default: first seed)
  --scene-label NAME           Stable scene name for reports/assets
  --no-reference-check         Skip the second independent reference
  -h, --help                   Show this help

The YAML budget samples_per_pixel + adaptive_spp * adaptive_passes must equal
--uniform-spp for a fair comparison.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --config) CONFIG="$2"; shift 2 ;;
        --output-root) OUTPUT_ROOT="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --seeds) SEEDS="$2"; shift 2 ;;
        --uniform-spp) UNIFORM_SPP="$2"; shift 2 ;;
        --reference-spp) REFERENCE_SPP="$2"; shift 2 ;;
        --reference-seed) REFERENCE_SEED="$2"; shift 2 ;;
        --reference-seed2) REFERENCE_SEED2="$2"; shift 2 ;;
        --representative-seed) REPRESENTATIVE_SEED="$2"; shift 2 ;;
        --scene-label) SCENE_LABEL="$2"; shift 2 ;;
        --no-reference-check) REFERENCE_CHECK=false; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
    esac
done

[[ -n "$CONFIG" ]] || { echo "Missing --config" >&2; usage >&2; exit 1; }
[[ -f "$CONFIG" ]] || { echo "Config not found: $CONFIG" >&2; exit 1; }

yaml_int() {
    awk -v key="$1:" '$1 == key { gsub(/\r/, "", $2); print $2; exit }' "$CONFIG"
}
base_spp="$(yaml_int samples_per_pixel)"
adaptive_spp="$(yaml_int adaptive_spp)"
adaptive_passes="$(yaml_int adaptive_passes)"
adaptive_spp="${adaptive_spp:-0}"
adaptive_passes="${adaptive_passes:-1}"
[[ -n "$base_spp" ]] || { echo "Config lacks samples_per_pixel" >&2; exit 1; }
for value in "$base_spp" "$adaptive_spp" "$adaptive_passes"; do
    [[ "$value" =~ ^[0-9]+$ ]] || { echo "Config sample budgets must be integers" >&2; exit 1; }
done
expected_spp=$((base_spp + adaptive_spp * adaptive_passes))
[[ "$expected_spp" -eq "$UNIFORM_SPP" ]] || {
    echo "Method sample budgets differ: StatMC=$expected_spp spp; uniform=$UNIFORM_SPP spp" >&2
    exit 1
}

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

IFS=',' read -r -a seed_list <<< "$SEEDS"
[[ ${#seed_list[@]} -gt 0 ]] || { echo "At least one seed is required" >&2; exit 1; }
[[ -n "$REPRESENTATIVE_SEED" ]] || REPRESENTATIVE_SEED="${seed_list[0]}"
[[ ",$SEEDS," == *",$REPRESENTATIVE_SEED,"* ]] || {
    echo "Representative seed $REPRESENTATIVE_SEED is not in --seeds" >&2
    exit 1
}

mkdir -p "$OUTPUT_ROOT"
cp "$CONFIG" "$OUTPUT_ROOT/config.yaml"
scene="${SCENE_LABEL:-$(basename "${CONFIG%.*}")}"
working_tree_dirty=false
working_tree_diff_sha256=clean
if ! git diff --quiet HEAD --; then
    working_tree_dirty=true
    working_tree_diff_sha256="$(git diff --binary HEAD -- | sha256sum | awk '{ print $1 }')"
fi
{
    echo "revision=$(git rev-parse HEAD)"
    echo "working_tree_dirty=$working_tree_dirty"
    echo "working_tree_diff_sha256=$working_tree_diff_sha256"
    echo "config=$CONFIG"
    echo "scene=$scene"
    echo "seeds=$SEEDS"
    echo "uniform_spp=$UNIFORM_SPP"
    echo "statmc_spp=$expected_spp"
    echo "reference_spp=$REFERENCE_SPP"
    echo "reference_seed=$REFERENCE_SEED"
    echo "reference_seed2=$REFERENCE_SEED2"
    echo "reference_check=$REFERENCE_CHECK"
    echo "representative_seed=$REPRESENTATIVE_SEED"
} > "$OUTPUT_ROOT/manifest.txt"

./run.sh -t path_tracer -B "$BUILD_DIR" --parallel
./run.sh -t eval_tools -B "$BUILD_DIR" --parallel
./run.sh -t tonemap_hdr -B "$BUILD_DIR" --parallel

BUILD_DIR_ABS="$BUILD_DIR"
[[ "$BUILD_DIR_ABS" == /* ]] || BUILD_DIR_ABS="$REPO_ROOT/$BUILD_DIR_ABS"
TRACER="$BUILD_DIR_ABS/path_tracer/path_tracer"
EVAL="$BUILD_DIR_ABS/shared/tools/eval_tools"
TONEMAP="$BUILD_DIR_ABS/shared/tools/tonemap_hdr"
for executable in "$TRACER" "$EVAL" "$TONEMAP"; do
    [[ -x "$executable" ]] || { echo "Executable not found: $executable" >&2; exit 1; }
done
"$EVAL" self-test >/dev/null

declare -A render_seconds
render() {
    local label="$1"
    shift
    local started finished
    started="$(date +%s%N)"
    "$TRACER" -c "$CONFIG" -o "$OUTPUT_ROOT/$label" "$@"
    finished="$(date +%s%N)"
    render_seconds["$label"]="$(awk -v start="$started" -v finish="$finished" 'BEGIN { printf "%.6f", (finish-start)/1000000000 }')"
}

render reference --seed "$REFERENCE_SEED" --spp "$REFERENCE_SPP" --no-statmc
if [[ "$REFERENCE_CHECK" == true ]]; then
    render reference_check --seed "$REFERENCE_SEED2" --spp "$REFERENCE_SPP" --no-statmc
fi

assets="$OUTPUT_ROOT/paper_assets"
mkdir -p "$assets"
cp "$OUTPUT_ROOT/reference/reference.png" "$assets/${scene}_reference.png"
if [[ "$REFERENCE_CHECK" == true ]]; then
    cp "$OUTPUT_ROOT/reference_check/reference_check.png" "$assets/${scene}_reference_check.png"
fi

csv="$OUTPUT_ROOT/results.csv"
printf 'scene,seed,method,hdr_mae,hdr_rmse,hdr_psnr,hdr_nrmse,log_ssim11,log_gradient_nrmse,png_rmse,png_psnr,wall_seconds\n' > "$csv"

for seed in "${seed_list[@]}"; do
    render "uniform_seed${seed}" --seed "$seed" --spp "$UNIFORM_SPP" --no-statmc
    render "statmc_no_rpd_seed${seed}" --seed "$seed" --statmc --rpf-shrinkage-scale 0
    render "statmc_rpd_seed${seed}" --seed "$seed" --statmc --rpf-shrinkage-scale 1

    for method in uniform statmc_no_rpd statmc_rpd; do
        label="${method}_seed${seed}"
        residual_args=()
        if [[ "$seed" == "$REPRESENTATIVE_SEED" ]]; then
            residual_args=("$assets/${scene}_${method}_residual.hdr")
            cp "$OUTPUT_ROOT/$label/$label.png" "$assets/${scene}_${method}.png"
        fi
        hdr_metrics="$("$EVAL" hdr-metrics \
            "$OUTPUT_ROOT/reference/reference.hdr" "$OUTPUT_ROOT/$label/$label.hdr" "${residual_args[@]}")"
        png_metrics="$("$EVAL" compare-png \
            "$OUTPUT_ROOT/reference/reference.png" "$OUTPUT_ROOT/$label/$label.png")"
        hdr_line="$(awk '/^  MAE:/ { print; exit }' <<< "$hdr_metrics")"
        extended_line="$(awk '/^  Extended:/ { print; exit }' <<< "$hdr_metrics")"
        png_line="$(awk '/^  RMSE:/ { print; exit }' <<< "$png_metrics")"
        hdr_mae="$(awk '{ print $2 }' <<< "$hdr_line")"
        hdr_rmse="$(awk '{ print $4 }' <<< "$hdr_line")"
        hdr_psnr="$(awk '{ print $8 }' <<< "$hdr_line")"
        hdr_nrmse="$(awk '{ print $3 }' <<< "$extended_line")"
        log_ssim11="$(awk '{ print $5 }' <<< "$extended_line")"
        log_gradient_nrmse="$(awk '{ print $7 }' <<< "$extended_line")"
        png_rmse="$(awk '{ print $2 }' <<< "$png_line")"
        png_psnr="$(awk '{ print $4 }' <<< "$png_line")"
        printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
            "$scene" "$seed" "$method" "$hdr_mae" "$hdr_rmse" "$hdr_psnr" \
            "$hdr_nrmse" "$log_ssim11" "$log_gradient_nrmse" "$png_rmse" "$png_psnr" \
            "${render_seconds[$label]}" >> "$csv"

        if [[ "$seed" == "$REPRESENTATIVE_SEED" ]]; then
            "$TONEMAP" "$assets/${scene}_${method}_residual.hdr" \
                "$assets/${scene}_${method}_residual.png" agx >/dev/null
        fi
    done
done

rpd_label="statmc_rpd_seed${REPRESENTATIVE_SEED}"
for diagnostic in sensitivity sensitivity_pixel sensitivity_brdf sensitivity_lens sensitivity_light sensitivity_environment sensitivity_rr \
                  sensitivity_confidence sensitivity_gradient light_visibility alpha sample_fraction; do
    diagnostic_hdr="$OUTPUT_ROOT/$rpd_label/${rpd_label}_${diagnostic}.hdr"
    if [[ -f "$diagnostic_hdr" ]]; then
        "$TONEMAP" "$diagnostic_hdr" "$assets/${scene}_rpd_${diagnostic}.png" linear >/dev/null
    fi
done
for diagnostic in uncertainty vardenoised adaptive_importance var_total var_eff; do
    diagnostic_hdr="$OUTPUT_ROOT/$rpd_label/${rpd_label}_${diagnostic}.hdr"
    if [[ -f "$diagnostic_hdr" ]]; then
        "$TONEMAP" "$diagnostic_hdr" "$assets/${scene}_rpd_${diagnostic}.png" agx >/dev/null
    fi
done

summary="$OUTPUT_ROOT/summary.csv"
awk -F, '
BEGIN { print "scene,method,n,hdr_rmse_mean,hdr_rmse_ci95,hdr_nrmse_mean,hdr_nrmse_ci95,log_ssim11_mean,log_ssim11_ci95,log_gradient_nrmse_mean,log_gradient_nrmse_ci95,png_rmse_mean,png_rmse_ci95,wall_seconds_mean,wall_seconds_ci95" }
NR > 1 {
    scene_name=$1; m=$3; n[m]++
    a[m]+=$5; a2[m]+=$5*$5
    b[m]+=$7; b2[m]+=$7*$7
    c[m]+=$8; c2[m]+=$8*$8
    d[m]+=$9; d2[m]+=$9*$9
    e[m]+=$10; e2[m]+=$10*$10
    f[m]+=$12; f2[m]+=$12*$12
}
function mean(sum, count) { return sum/count }
function tcrit(count, df,z,z2,z3,z5,z7) {
    if (count <= 1) return 0
    df=count-1; z=1.95996398454005; z2=z*z; z3=z2*z; z5=z3*z2; z7=z5*z2
    return z+(z3+z)/(4*df)+(5*z5+16*z3+3*z)/(96*df*df)+(3*z7+19*z5+17*z3-15*z)/(384*df*df*df)
}
function ci(sum, sum2, count, avg, variance) {
    if (count <= 1) return 0
    variance=(sum2-count*avg*avg)/(count-1)
    return tcrit(count)*sqrt((variance > 0 ? variance : 0)/count)
}
END {
    order[1]="uniform"; order[2]="statmc_no_rpd"; order[3]="statmc_rpd"
    for (i=1; i<=3; ++i) {
        m=order[i]
        am=mean(a[m],n[m]); bm=mean(b[m],n[m]); cm=mean(c[m],n[m])
        dm=mean(d[m],n[m]); em=mean(e[m],n[m]); fm=mean(f[m],n[m])
        printf "%s,%s,%d,%.8g,%.8g,%.8g,%.8g,%.8g,%.8g,%.8g,%.8g,%.8g,%.8g,%.8g,%.8g\n", \
            scene_name,m,n[m],am,ci(a[m],a2[m],n[m],am),bm,ci(b[m],b2[m],n[m],bm), \
            cm,ci(c[m],c2[m],n[m],cm),dm,ci(d[m],d2[m],n[m],dm), \
            em,ci(e[m],e2[m],n[m],em),fm,ci(f[m],f2[m],n[m],fm)
    }
}' "$csv" > "$summary"

paired="$OUTPUT_ROOT/paired_deltas.csv"
awk -F, '
BEGIN { print "scene,seed,comparison,hdr_rmse_reduction,hdr_nrmse_reduction,log_ssim11_gain,log_gradient_nrmse_reduction,png_rmse_reduction,wall_time_ratio" }
NR > 1 {
    scene[$2]=$1; seen[$2]=1; key=$2 SUBSEP $3
    hdr[key]=$5; nrmse[key]=$7; ssim[key]=$8; grad[key]=$9; png[key]=$10; wall[key]=$12
}
END {
    base[1]="uniform"; candidate[1]="statmc_no_rpd"; label[1]="statmc_no_rpd-vs-uniform"
    base[2]="uniform"; candidate[2]="statmc_rpd"; label[2]="statmc_rpd-vs-uniform"
    base[3]="statmc_no_rpd"; candidate[3]="statmc_rpd"; label[3]="rpd-vs-no_rpd"
    for (seed_value in seen) for (i=1; i<=3; ++i) {
        b=seed_value SUBSEP base[i]; c=seed_value SUBSEP candidate[i]
        printf "%s,%s,%s,%.8g,%.8g,%.8g,%.8g,%.8g,%.8g\n", scene[seed_value],seed_value,label[i], \
            hdr[b]-hdr[c],nrmse[b]-nrmse[c],ssim[c]-ssim[b],grad[b]-grad[c],png[b]-png[c],wall[c]/wall[b]
    }
}' "$csv" > "$paired"

paired_summary="$OUTPUT_ROOT/paired_summary.csv"
awk -F, '
BEGIN { print "scene,comparison,n,hdr_rmse_reduction_mean,hdr_rmse_reduction_ci95,hdr_nrmse_reduction_mean,hdr_nrmse_reduction_ci95,log_ssim11_gain_mean,log_ssim11_gain_ci95,log_gradient_nrmse_reduction_mean,log_gradient_nrmse_reduction_ci95,png_rmse_reduction_mean,png_rmse_reduction_ci95,wall_time_ratio_mean,wall_time_ratio_ci95" }
NR > 1 {
    scene_name=$1; m=$3; n[m]++
    a[m]+=$4; a2[m]+=$4*$4; b[m]+=$5; b2[m]+=$5*$5; c[m]+=$6; c2[m]+=$6*$6
    d[m]+=$7; d2[m]+=$7*$7; e[m]+=$8; e2[m]+=$8*$8; f[m]+=$9; f2[m]+=$9*$9
}
function mean(sum, count) { return sum/count }
function tcrit(count, df,z,z2,z3,z5,z7) {
    if (count <= 1) return 0
    df=count-1; z=1.95996398454005; z2=z*z; z3=z2*z; z5=z3*z2; z7=z5*z2
    return z+(z3+z)/(4*df)+(5*z5+16*z3+3*z)/(96*df*df)+(3*z7+19*z5+17*z3-15*z)/(384*df*df*df)
}
function ci(sum, sum2, count, avg, variance) {
    if (count <= 1) return 0
    variance=(sum2-count*avg*avg)/(count-1)
    return tcrit(count)*sqrt((variance > 0 ? variance : 0)/count)
}
END {
    order[1]="statmc_no_rpd-vs-uniform"; order[2]="statmc_rpd-vs-uniform"; order[3]="rpd-vs-no_rpd"
    for (i=1; i<=3; ++i) {
        m=order[i]
        am=mean(a[m],n[m]); bm=mean(b[m],n[m]); cm=mean(c[m],n[m])
        dm=mean(d[m],n[m]); em=mean(e[m],n[m]); fm=mean(f[m],n[m])
        printf "%s,%s,%d,%.8g,%.8g,%.8g,%.8g,%.8g,%.8g,%.8g,%.8g,%.8g,%.8g,%.8g,%.8g\n", \
            scene_name,m,n[m],am,ci(a[m],a2[m],n[m],am),bm,ci(b[m],b2[m],n[m],bm), \
            cm,ci(c[m],c2[m],n[m],cm),dm,ci(d[m],d2[m],n[m],dm), \
            em,ci(e[m],e2[m],n[m],em),fm,ci(f[m],f2[m],n[m],fm)
    }
}' "$paired" > "$paired_summary"

if [[ "$REFERENCE_CHECK" == true ]]; then
    reference_metrics="$("$EVAL" hdr-metrics "$OUTPUT_ROOT/reference/reference.hdr" \
        "$OUTPUT_ROOT/reference_check/reference_check.hdr")"
    reference_png="$("$EVAL" compare-png "$OUTPUT_ROOT/reference/reference.png" \
        "$OUTPUT_ROOT/reference_check/reference_check.png")"
    hdr_line="$(awk '/^  MAE:/ { print; exit }' <<< "$reference_metrics")"
    extended_line="$(awk '/^  Extended:/ { print; exit }' <<< "$reference_metrics")"
    png_line="$(awk '/^  RMSE:/ { print; exit }' <<< "$reference_png")"
    {
        printf 'scene,reference_spp,hdr_rmse,hdr_nrmse,log_ssim11,log_gradient_nrmse,png_rmse\n'
        printf '%s,%s,%s,%s,%s,%s,%s\n' "$scene" "$REFERENCE_SPP" \
            "$(awk '{ print $4 }' <<< "$hdr_line")" "$(awk '{ print $3 }' <<< "$extended_line")" \
            "$(awk '{ print $5 }' <<< "$extended_line")" "$(awk '{ print $7 }' <<< "$extended_line")" \
            "$(awk '{ print $2 }' <<< "$png_line")"
    } > "$OUTPUT_ROOT/reference_noise.csv"
fi

awk -F, -v scene="$scene" '
NR > 1 {
    name=$2
    if (name=="uniform") name="Uniform"
    else if (name=="statmc_no_rpd") name="StatMC (no RPD)"
    else if (name=="statmc_rpd") name="StatMC + RPD"
    display_scene=scene; gsub(/_/, "\\_", display_scene)
    printf "%s & %s & %.4f $\\pm$ %.4f & %.4f $\\pm$ %.4f & %.4f $\\pm$ %.4f & %.4f $\\pm$ %.4f \\\\\n", \
        display_scene,name,$6,$7,$8,$9,$10,$11,$12,$13
}' "$summary" > "$OUTPUT_ROOT/table_rows.tex"

{
    echo "Representative seed: $REPRESENTATIVE_SEED"
    echo "Reference: ${scene}_reference.png"
    echo "Methods: ${scene}_uniform.png, ${scene}_statmc_no_rpd.png, ${scene}_statmc_rpd.png"
    echo "Residuals: ${scene}_{uniform,statmc_no_rpd,statmc_rpd}_residual.png"
    echo "Diagnostics: ${scene}_rpd_{sensitivity,sensitivity_light,light_visibility,...}.png"
} > "$assets/${scene}_README.txt"

echo "Wrote $csv"
echo "Wrote $summary"
echo "Wrote $paired_summary"
echo "Wrote $assets"
