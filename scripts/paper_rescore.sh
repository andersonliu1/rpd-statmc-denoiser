#!/usr/bin/env bash
set -euo pipefail

OUTPUT_ROOT=""
BUILD_DIR="build/path_tracer_build"

usage() {
    cat <<EOF
Usage: $0 --output-root DIR [--build-dir DIR]

Recomputes paper metrics from existing renders against both independent
references. No renderer is run and no image is replaced.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --output-root) OUTPUT_ROOT="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
    esac
done

[[ -n "$OUTPUT_ROOT" ]] || { echo "Missing --output-root" >&2; usage >&2; exit 1; }

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

manifest="$OUTPUT_ROOT/manifest.txt"
old_results="$OUTPUT_ROOT/results.csv"
[[ -f "$manifest" ]] || { echo "Manifest not found: $manifest" >&2; exit 1; }
[[ -f "$old_results" ]] || { echo "Results not found: $old_results" >&2; exit 1; }

manifest_value() {
    awk -F= -v key="$1" '$1 == key { sub(/^[^=]*=/, ""); print; exit }' "$manifest"
}

seeds="$(manifest_value seeds)"
reference_spp="$(manifest_value reference_spp)"
uniform_spp="$(manifest_value uniform_spp)"
reference_check="$(manifest_value reference_check)"
scene="$(manifest_value scene)"
[[ -n "$scene" ]] || scene="$(awk -F, 'NR == 2 { print $1; exit }' "$old_results")"
[[ -n "$scene" && -n "$seeds" ]] || { echo "Manifest lacks scene/seeds" >&2; exit 1; }
[[ "$reference_check" == true ]] || {
    echo "Dual-reference rescore requires a run without --no-reference-check" >&2
    exit 1
}
IFS=',' read -r -a seed_list <<< "$seeds"

./run.sh -t eval_tools -B "$BUILD_DIR" --parallel
BUILD_DIR_ABS="$BUILD_DIR"
[[ "$BUILD_DIR_ABS" == /* ]] || BUILD_DIR_ABS="$REPO_ROOT/$BUILD_DIR_ABS"
EVAL="$BUILD_DIR_ABS/shared/tools/eval_tools"
[[ -x "$EVAL" ]] || { echo "Executable not found: $EVAL" >&2; exit 1; }
"$EVAL" self-test >/dev/null

invalid="$OUTPUT_ROOT/rescore_invalid.csv"
printf 'severity,item,detail\n' > "$invalid"
fail=false
require_file() {
    if [[ ! -f "$1" ]]; then
        printf 'error,missing_artifact,%s\n' "$1" >> "$invalid"
        fail=true
    fi
}

references=(reference reference_check)
require_file "$OUTPUT_ROOT/config.yaml"
for ref in "${references[@]}"; do
    require_file "$OUTPUT_ROOT/$ref/$ref.hdr"
    require_file "$OUTPUT_ROOT/$ref/$ref.png"
done
for seed in "${seed_list[@]}"; do
    for method in uniform statmc_no_rpd statmc_rpd; do
        label="${method}_seed${seed}"
        require_file "$OUTPUT_ROOT/$label/$label.hdr"
        require_file "$OUTPUT_ROOT/$label/$label.png"
    done
    label="statmc_no_rpd_seed${seed}"
    require_file "$OUTPUT_ROOT/$label/${label}_raw.hdr"
    require_file "$OUTPUT_ROOT/$label/${label}_raw.png"
    label="statmc_rpd_seed${seed}"
    require_file "$OUTPUT_ROOT/$label/${label}_raw.hdr"
done
[[ "$fail" == false ]] || { echo "Missing artifacts; see $invalid" >&2; exit 1; }
for seed in "${seed_list[@]}"; do
    no_rpd="statmc_no_rpd_seed${seed}"
    rpd="statmc_rpd_seed${seed}"
    if ! cmp -s "$OUTPUT_ROOT/$no_rpd/${no_rpd}_raw.hdr" "$OUTPUT_ROOT/$rpd/${rpd}_raw.hdr"; then
        printf 'error,raw_ablation_mismatch,seed %s\n' "$seed" >> "$invalid"
        echo "RPD changed the raw path for seed $seed; see $invalid" >&2
        exit 1
    fi
done

base_spp="$(awk '$1 == "samples_per_pixel:" { gsub(/\r/, "", $2); print $2; exit }' "$OUTPUT_ROOT/config.yaml")"
adaptive_spp="$(awk '$1 == "adaptive_spp:" { gsub(/\r/, "", $2); print $2; exit }' "$OUTPUT_ROOT/config.yaml")"
adaptive_passes="$(awk '$1 == "adaptive_passes:" { gsub(/\r/, "", $2); print $2; exit }' "$OUTPUT_ROOT/config.yaml")"
adaptive_spp="${adaptive_spp:-0}"
adaptive_passes="${adaptive_passes:-1}"
if [[ -z "$base_spp" ]]; then
    printf 'error,budget_metadata,missing samples_per_pixel\n' >> "$invalid"
    echo "Config lacks budget metadata; see $invalid" >&2
    exit 1
fi
for value in "$base_spp" "$adaptive_spp" "$adaptive_passes"; do
    if [[ ! "$value" =~ ^[0-9]+$ ]]; then
        printf 'error,budget_metadata,non-integer sample budget\n' >> "$invalid"
        echo "Config sample budgets must be integers; see $invalid" >&2
        exit 1
    fi
done
expected_spp=$((base_spp + adaptive_spp * adaptive_passes))
if [[ -z "$uniform_spp" || "$expected_spp" -ne "$uniform_spp" ]]; then
    printf 'error,budget_mismatch,StatMC=%s spp; uniform=%s spp\n' "$expected_spp" "${uniform_spp:-missing}" >> "$invalid"
    echo "Method sample budgets differ; see $invalid" >&2
    exit 1
fi

ref_hash1="$(sha256sum "$OUTPUT_ROOT/reference/reference.hdr" | awk '{ print $1 }')"
ref_hash2="$(sha256sum "$OUTPUT_ROOT/reference_check/reference_check.hdr" | awk '{ print $1 }')"
if [[ "$ref_hash1" == "$ref_hash2" ]]; then
    printf 'error,duplicate_references,reference HDR hashes are identical\n' >> "$invalid"
    echo "Independent references are identical; see $invalid" >&2
    exit 1
fi
if [[ ${#seed_list[@]} -lt 3 ]]; then
    printf 'warning,insufficient_seeds,N=%s; confidence intervals unavailable\n' "${#seed_list[@]}" >> "$invalid"
fi

extract_metrics() {
    local reference="$1"
    local candidate="$2"
    local hdr_metrics png_metrics hdr_line extended_line png_line
    hdr_metrics="$("$EVAL" hdr-metrics "$reference.hdr" "$candidate.hdr")"
    png_metrics="$("$EVAL" compare-png "$reference.png" "$candidate.png")"
    hdr_line="$(awk '/^  MAE:/ { print; exit }' <<< "$hdr_metrics")"
    extended_line="$(awk '/^  Extended:/ { print; exit }' <<< "$hdr_metrics")"
    png_line="$(awk '/^  RMSE:/ { print; exit }' <<< "$png_metrics")"
    printf '%s,%s,%s,%s,%s,%s,%s,%s' \
        "$(awk '{ print $2 }' <<< "$hdr_line")" \
        "$(awk '{ print $4 }' <<< "$hdr_line")" \
        "$(awk '{ print $8 }' <<< "$hdr_line")" \
        "$(awk '{ print $3 }' <<< "$extended_line")" \
        "$(awk '{ print $5 }' <<< "$extended_line")" \
        "$(awk '{ print $7 }' <<< "$extended_line")" \
        "$(awk '{ print $2 }' <<< "$png_line")" \
        "$(awk '{ print $4 }' <<< "$png_line")"
}

reference_metrics="$(extract_metrics \
    "$OUTPUT_ROOT/reference/reference" \
    "$OUTPUT_ROOT/reference_check/reference_check")"
IFS=',' read -r ref_mae ref_rmse ref_psnr ref_nrmse ref_ssim ref_grad ref_png_rmse ref_png_psnr <<< "$reference_metrics"
reference_noise="$OUTPUT_ROOT/rescore_reference_noise.csv"
printf 'scene,reference_spp,hdr_mse,hdr_rmse,hdr_nrmse2,hdr_nrmse,log_ssim11,log_gradient_nrmse,png_mse,png_rmse,reference_hash1,reference_hash2\n' > "$reference_noise"
awk -v scene="$scene" -v spp="$reference_spp" -v rmse="$ref_rmse" -v nrmse="$ref_nrmse" \
    -v ssim="$ref_ssim" -v grad="$ref_grad" -v png="$ref_png_rmse" \
    -v hash1="$ref_hash1" -v hash2="$ref_hash2" \
    'BEGIN { printf "%s,%s,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%s,%s\n", scene,spp,rmse*rmse,rmse,nrmse*nrmse,nrmse,ssim,grad,png*png,png,hash1,hash2 }' \
    >> "$reference_noise"
ref_mse="$(awk -F, 'NR == 2 { print $3 }' "$reference_noise")"

rows="$OUTPUT_ROOT/rescore_rows.csv"
printf 'scene,seed,method,reference_id,hdr_mae,hdr_rmse,hdr_psnr,hdr_nrmse,log_ssim11,log_gradient_nrmse,png_rmse,png_psnr,wall_seconds,hdr_mse,hdr_nrmse2,png_mse\n' > "$rows"
for seed in "${seed_list[@]}"; do
    for method in uniform adaptive_raw statmc_no_rpd statmc_rpd; do
        if [[ "$method" == adaptive_raw ]]; then
            label="statmc_no_rpd_seed${seed}"
            candidate="$OUTPUT_ROOT/$label/${label}_raw"
        else
            label="${method}_seed${seed}"
            candidate="$OUTPUT_ROOT/$label/$label"
        fi
        wall_method="$method"
        [[ "$method" == adaptive_raw ]] && wall_method=statmc_no_rpd
        wall="$(awk -F, -v seed="$seed" -v method="$wall_method" 'NR > 1 && $2 == seed && $3 == method { print $12; exit }' "$old_results")"
        [[ -n "$wall" ]] || { echo "Missing wall time for $label in $old_results" >&2; exit 1; }
        for ref in "${references[@]}"; do
            metrics="$(extract_metrics "$OUTPUT_ROOT/$ref/$ref" "$candidate")"
            IFS=',' read -r hdr_mae hdr_rmse hdr_psnr hdr_nrmse log_ssim log_grad png_rmse png_psnr <<< "$metrics"
            awk -v scene="$scene" -v seed="$seed" -v method="$method" -v ref="$ref" \
                -v mae="$hdr_mae" -v rmse="$hdr_rmse" -v psnr="$hdr_psnr" -v nrmse="$hdr_nrmse" \
                -v ssim="$log_ssim" -v grad="$log_grad" -v png="$png_rmse" -v pngpsnr="$png_psnr" -v wall="$wall" \
                'BEGIN { printf "%s,%s,%s,%s,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%s,%.12g,%.12g,%.12g\n", scene,seed,method,ref,mae,rmse,psnr,nrmse,ssim,grad,png,pngpsnr,wall,rmse*rmse,nrmse*nrmse,png*png }' \
                >> "$rows"
        done
    done
done

seed_means="$OUTPUT_ROOT/rescore_seed_means.csv"
awk -F, -v ref_mse="$ref_mse" '
BEGIN { print "scene,seed,method,reference_count,hdr_mse,hdr_rmse,hdr_nrmse2,hdr_nrmse,log_ssim11,log_gradient_nrmse,png_mse,png_rmse,wall_seconds,hdr_latent_mse" }
NR > 1 {
    key=$1 SUBSEP $2 SUBSEP $3; scene[key]=$1; seed[key]=$2; method[key]=$3; n[key]++
    hdr_mse[key]+=$14; nrmse2[key]+=$15; ssim[key]+=$9; grad[key]+=$10; png_mse[key]+=$16; wall[key]=$13
}
END {
    for (key in n) {
        hm=hdr_mse[key]/n[key]; nm=nrmse2[key]/n[key]; pm=png_mse[key]/n[key]
        printf "%s,%s,%s,%d,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%s,%.12g\n", \
            scene[key],seed[key],method[key],n[key],hm,sqrt(hm),nm,sqrt(nm),ssim[key]/n[key],grad[key]/n[key],pm,sqrt(pm),wall[key],hm-ref_mse/2
    }
}' "$rows" > "$seed_means"

summary="$OUTPUT_ROOT/rescore_summary.csv"
awk -F, '
BEGIN { print "scene,method,n,hdr_mse_mean,hdr_mse_ci95,hdr_rmse,hdr_nrmse2_mean,hdr_nrmse2_ci95,hdr_nrmse,log_ssim11_mean,log_ssim11_ci95,log_gradient_nrmse_mean,log_gradient_nrmse_ci95,png_mse_mean,png_mse_ci95,png_rmse,wall_seconds_mean,wall_seconds_ci95,hdr_latent_mse_mean" }
NR > 1 {
    scene_name=$1; m=$3; n[m]++
    a[m]+=$5; a2[m]+=$5*$5; b[m]+=$7; b2[m]+=$7*$7; c[m]+=$9; c2[m]+=$9*$9
    d[m]+=$10; d2[m]+=$10*$10; e[m]+=$11; e2[m]+=$11*$11; f[m]+=$13; f2[m]+=$13*$13; g[m]+=$14
}
function tcrit(count,df,z,z2,z3,z5,z7) {
    df=count-1; z=1.95996398454005; z2=z*z; z3=z2*z; z5=z3*z2; z7=z5*z2
    return z+(z3+z)/(4*df)+(5*z5+16*z3+3*z)/(96*df*df)+(3*z7+19*z5+17*z3-15*z)/(384*df*df*df)
}
function ci(sum,sum2,count,avg,variance) {
    if (count < 3) return "NA"
    avg=sum/count; variance=(sum2-count*avg*avg)/(count-1)
    return tcrit(count)*sqrt((variance > 0 ? variance : 0)/count)
}
END {
    order[1]="uniform"; order[2]="adaptive_raw"; order[3]="statmc_no_rpd"; order[4]="statmc_rpd"
    for (i=1; i<=4; ++i) {
        m=order[i]; am=a[m]/n[m]; bm=b[m]/n[m]; cm=c[m]/n[m]; dm=d[m]/n[m]; em=e[m]/n[m]; fm=f[m]/n[m]
        printf "%s,%s,%d,%.12g,%s,%.12g,%.12g,%s,%.12g,%.12g,%s,%.12g,%s,%.12g,%s,%.12g,%.12g,%s,%.12g\n", \
            scene_name,m,n[m],am,ci(a[m],a2[m],n[m]),sqrt(am),bm,ci(b[m],b2[m],n[m]),sqrt(bm),cm,ci(c[m],c2[m],n[m]),dm,ci(d[m],d2[m],n[m]),em,ci(e[m],e2[m],n[m]),sqrt(em),fm,ci(f[m],f2[m],n[m]),g[m]/n[m]
    }
}' "$seed_means" > "$summary"

paired="$OUTPUT_ROOT/rescore_paired.csv"
awk -F, '
BEGIN { print "scene,seed,comparison,hdr_mse_reduction,hdr_nrmse2_reduction,log_ssim11_gain,log_gradient_nrmse_reduction,png_mse_reduction,wall_time_ratio" }
NR > 1 {
    scene[$2]=$1; seen[$2]=1; key=$2 SUBSEP $3
    hdr[key]=$5; nrmse[key]=$7; ssim[key]=$9; grad[key]=$10; png[key]=$11; wall[key]=$13
}
END {
    base[1]="uniform"; candidate[1]="adaptive_raw"; label[1]="adaptive_raw-vs-uniform"
    base[2]="adaptive_raw"; candidate[2]="statmc_no_rpd"; label[2]="statmc_no_rpd-vs-adaptive_raw"
    base[3]="uniform"; candidate[3]="statmc_no_rpd"; label[3]="statmc_no_rpd-vs-uniform"
    base[4]="uniform"; candidate[4]="statmc_rpd"; label[4]="statmc_rpd-vs-uniform"
    base[5]="statmc_no_rpd"; candidate[5]="statmc_rpd"; label[5]="rpd-vs-no_rpd"
    for (seed_value in seen) for (i=1; i<=5; ++i) {
        b=seed_value SUBSEP base[i]; c=seed_value SUBSEP candidate[i]
        ratio=(base[i]=="adaptive_raw" || candidate[i]=="adaptive_raw") ? "NA" : sprintf("%.12g",wall[c]/wall[b])
        printf "%s,%s,%s,%.12g,%.12g,%.12g,%.12g,%.12g,%s\n", scene[seed_value],seed_value,label[i], \
            hdr[b]-hdr[c],nrmse[b]-nrmse[c],ssim[c]-ssim[b],grad[b]-grad[c],png[b]-png[c],ratio
    }
}' "$seed_means" > "$paired"

paired_summary="$OUTPUT_ROOT/rescore_paired_summary.csv"
awk -F, '
BEGIN { print "scene,comparison,n,hdr_mse_reduction_mean,hdr_mse_reduction_ci95,hdr_nrmse2_reduction_mean,hdr_nrmse2_reduction_ci95,log_ssim11_gain_mean,log_ssim11_gain_ci95,log_gradient_nrmse_reduction_mean,log_gradient_nrmse_reduction_ci95,png_mse_reduction_mean,png_mse_reduction_ci95,wall_time_ratio_mean,wall_time_ratio_ci95" }
NR > 1 {
    scene_name=$1; m=$3; n[m]++
    a[m]+=$4; a2[m]+=$4*$4; b[m]+=$5; b2[m]+=$5*$5; c[m]+=$6; c2[m]+=$6*$6
    d[m]+=$7; d2[m]+=$7*$7; e[m]+=$8; e2[m]+=$8*$8
    if ($9 != "NA") { f[m]+=$9; f2[m]+=$9*$9; fn[m]++ }
}
function tcrit(count,df,z,z2,z3,z5,z7) {
    df=count-1; z=1.95996398454005; z2=z*z; z3=z2*z; z5=z3*z2; z7=z5*z2
    return z+(z3+z)/(4*df)+(5*z5+16*z3+3*z)/(96*df*df)+(3*z7+19*z5+17*z3-15*z)/(384*df*df*df)
}
function ci(sum,sum2,count,avg,variance) {
    if (count < 3) return "NA"
    avg=sum/count; variance=(sum2-count*avg*avg)/(count-1)
    return tcrit(count)*sqrt((variance > 0 ? variance : 0)/count)
}
END {
    order[1]="adaptive_raw-vs-uniform"; order[2]="statmc_no_rpd-vs-adaptive_raw"
    order[3]="statmc_no_rpd-vs-uniform"; order[4]="statmc_rpd-vs-uniform"; order[5]="rpd-vs-no_rpd"
    for (i=1; i<=5; ++i) {
        m=order[i]
        runtime_mean=fn[m] ? sprintf("%.12g",f[m]/fn[m]) : "NA"
        runtime_ci=fn[m] ? ci(f[m],f2[m],fn[m]) : "NA"
        printf "%s,%s,%d,%.12g,%s,%.12g,%s,%.12g,%s,%.12g,%s,%.12g,%s,%s,%s\n", \
            scene_name,m,n[m],a[m]/n[m],ci(a[m],a2[m],n[m]),b[m]/n[m],ci(b[m],b2[m],n[m]), \
            c[m]/n[m],ci(c[m],c2[m],n[m]),d[m]/n[m],ci(d[m],d2[m],n[m]), \
            e[m]/n[m],ci(e[m],e2[m],n[m]),runtime_mean,runtime_ci
    }
}' "$paired" > "$paired_summary"

awk -F, 'NR > 1 && $14 <= 0 { printf "warning,negative_latent_mse,%s seed %s %s: %.12g\n", $1,$2,$3,$14 }' \
    "$seed_means" >> "$invalid"
awk -F, -v scene="$scene" '
function metric(value,ci) {
    return (ci=="NA") ? sprintf("%.4f",value) : sprintf("%.4f $\\pm$ %.4f",value,ci)
}
NR > 1 {
    name=$2
    if (name=="uniform") name="Uniform"
    else if (name=="adaptive_raw") name="Adaptive raw MC"
    else if (name=="statmc_no_rpd") name="StatMC (no RPD)"
    else if (name=="statmc_rpd") name="StatMC + RPD"
    display_scene=scene; gsub(/_/, "\\_", display_scene)
    nrmse_ci=($8=="NA") ? "NA" : $8/(2*$9)
    png_ci=($15=="NA") ? "NA" : $15/(2*$16)
    printf "%s & %s & %s & %s & %s & %s \\\\\n", display_scene,name, \
        metric($9,nrmse_ci),metric($10,$11),metric($12,$13),metric($16,png_ci)
}' "$summary" > "$OUTPUT_ROOT/rescore_table_rows.tex"

echo "Rescored existing renders: $OUTPUT_ROOT"
echo "Seed-level inputs: $seed_means"
echo "Paired RPD evidence: $paired_summary"
