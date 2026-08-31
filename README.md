# Random Parameter Decomposition for Statistics-Based Monte Carlo Denoising
![Imgur](https://i.imgur.com/dMKWZ8M.png)
## Setup

```bash
git submodule update --init --recursive
./run.sh
```

## Run

### Building

```bash
# Build the renderer (default target) into build/path_tracer_build
./run.sh -- --preset Release

# Pass options to CMake's configure step after '--'
./run.sh -- --preset <preset-name>

# Build with more parallel jobs or other cmake --build flags
./run.sh --parallel

# Clean only the renderer's build directory
./run.sh clean --target path_tracer
```

Run the repository test suite:

```bash
./run.sh test --parallel
```

When another target (e.g., a denoiser) lands in the tree, use `./run.sh --target <name>`
to build or clean it without touching the others.

### Running the path tracer

```bash
# From the repo root after building
./build/path_tracer_build/path_tracer/path_tracer \
    -c path_tracer/config/cornell.yaml \
    -o ./output/cornell \
    --seed 1234

# or use the helper script to build (optional) and run (parallel build by default):
./trace.sh -- -c path_tracer/config/cornell.yaml -o ./output/cornell

# Skip the build step if you already built:
./trace.sh --no-build -- -c path_tracer/config/cornell.yaml -o ./output/cornell
```

Arguments:
- `-c, --config`: Image + camera + sampling settings (YAML). May include `scene` and `output`.
- `--scene`: Built-in scene to render (overrides YAML `scene` when supplied).
- `-o, --output`: Output directory (absolute or relative to CWD). The renderer clears the directory first, then writes `<dir>/<leaf-name>.png`, `<dir>/<leaf-name>.hdr`, and the auxiliary outputs described below. Overrides YAML `output` when supplied.
- `--tonemap <preset>`: Tonemapping preset (`aces`, `agx`, `agx-golden`, `agx-punchy`). Overrides YAML `tonemap`.
- `--statmc` / `--no-statmc`: Enable or disable StatMC/RPF rendering (overrides YAML `statmc_enabled`).
- `--rpf-tile-size <N>`: Sensitivity support size for the overlapping RPF grid.
- `--rpf-shrinkage-scale <T>`: Scale RPD-guided StatMC compatibility relaxation; zero is a true RPD-off ablation.
- `--rpf-confidence-samples <T>`: Confidence sample mass for sensitivity shrinkage (>0).
- `--color-window-radius <N>`: Color window radius (pixels).
- `--color-normal-thresh <T>`: Color normal dot threshold (0,1].
- `--color-depth-thresh <T>`: Color relative depth threshold (>=0).
- `--color-compat-alpha <T>`: Color compatibility significance alpha (0,1).
- `--color-sigma-max <T>`: Max stddev clamp for color variance (>0).
- `--var-window-radius <N>`: Sample-variance window radius (pixels).
- `--var-normal-thresh <T>`: Sample-variance normal dot threshold (0,1].
- `--var-depth-thresh <T>`: Sample-variance relative depth threshold (>=0).
- `--var-compat-sigma <T>`: Sample-variance relative compatibility threshold (>0).
- `--var-shrinkage-k <T>`: Sample-variance shrinkage stabilizer (>0).
- `--var-iterations <N>`: Sample-variance smoothing iterations.
- `--adaptive-base-samples <N>`: Minimum extra samples per pixel per adaptive pass, taken from the `adaptive_spp` budget.
- `--adaptive-spp <N>`: Extra samples (spp) allocated by each adaptive pass.
- `--adaptive-sigma-max <T>`: Max sigma clamp for adaptive importance (>0).
- `--adaptive-passes <N>`: Number of adaptive refinement passes.
- `--adaptive-importance-radius <N>`: Edge-aware smoothing radius for adaptive importance (>=0).
- `--seed`: Optional sampler RNG seed (defaults to random).
- Additional CLI arguments follow the same precedence rules: CLI overrides YAML, YAML is the fallback.

YAML `output` values are treated as relative names by default. For example, `output: cornell` resolves to `output/cornell/` and produces `output/cornell/cornell.png`. CLI `-o` values are used as given: absolute paths go there; relative paths are resolved against the current working directory. The output directory must include a leaf name because that leaf name is used as the base filename.

YAML keys are flat. Unknown keys fail fast, and omitted optional keys use the
defaults in `path_tracer/core/config.h`. A minimal StatMC configuration only
needs to override values that differ from those defaults:

```yaml
image_width: 1024
image_height: 1024
samples_per_pixel: 4
scene: cornell_box
output: cornell
fov: 38.6
camera_position: [0.278, 0.2744, -0.8]
camera_direction: [0.0, 0.0, 1.0]
statmc_enabled: true
adaptive_spp: 4
adaptive_passes: 3
```

Current StatMC/RPF implementation notes:
- Random-parameter dependency diagnostics are accumulated on an overlapping low-resolution grid and reconstructed per pixel with bilinear interpolation.
- Recorded random inputs stay in their canonical `[0,1]` sampling domain and use the first valid occurrence. They include active lens samples, non-delta BRDF samples, categorical light selection, light UV, nonzero-environment direction, and binary Russian-roulette survival. Node-local full raster sample position is recorded separately as the screen-space comparator.
- Radiance dependency uses a bias-adjusted 4x4 binned explained-variance score. Weighted estimates and confidence use effective sample mass.
- Every recorded dimension is compared against full-path sample luminance. Random sensitivity is normalized against a screen-position dependency estimated from the same conditional sample subset, matching RPF's distinction between stochastic variation and image detail without mixing path populations. These values are nonlinear dependency diagnostics, not additive variance contributions or direct estimates of converged pixel error.
- Color denoising uses per-channel square-root-domain Welch statistics, soft compatibility weights, and a conservative variance floor. RPD uniformly relaxes the Welch compatibility score only when both pixels share reliable dependency on the same random dimension; it does not add parameter-specific gates or reduce variance.
- Variance stabilization only raises locally underestimated per-sample variance values. This keeps neighboring estimates in the same units after adaptive passes with unequal sample counts. Adaptive sampling then uses `max(raw, stabilized)` variance, a budget-preserving per-pixel minimum allocation, and `sqrt(variance)`-scaled importance with edge-aware smoothing.
- Spatial reconstruction is a lower-MSE, finite-sample biased estimator, not an unbiased Monte Carlo estimator. The unfiltered adaptive result is preserved as `*_raw.{png,hdr}` for bias and convergence checks, but pilot reuse means that result is not guaranteed to be finite-sample unbiased either; the equal-spp uniform baseline remains the unbiased control.
- Pixel/sample-derived RNG seeds make renders independent of OpenMP row scheduling.
- Render-time RPF accumulation is thread-local and merged by grid node in parallel after each sampling phase.
- The renderer logs per-stage timings for `recompute_sensitivity`, `statmc_denoise`, `variance_denoise`, `compute_adaptive_sample_counts`, and `render_image_adaptive`.

StatMC outputs:
- Always written for every render:
  `<leaf>.png`, `<leaf>.hdr`, `<leaf>_albedo.hdr`, `<leaf>_normal.hdr`, `<leaf>_worldpos.hdr`, `<leaf>_depth.hdr`.
- Written when `statmc_enabled: true` or `--statmc` is active:
  `*_raw.png`, `*_raw.hdr`, `*_sensitivity.hdr`, per-dimension `*_sensitivity_{pixel,brdf,lens,light,light_uv,light_select,environment,rr}.hdr` (screen dependency plus reliable random dependencies; `light` is the aggregate view), `*_sensitivity_confidence.hdr`, `*_sensitivity_gradient.hdr`, `*_uncertainty.hdr`, `*_alpha.hdr`, `*_vardenoised.hdr`, `*_adaptive_importance.hdr`, and `*_sample_fraction.hdr`.
  This also includes `*_var_total.hdr`, `*_var_eff.hdr`, and `*_var_ratio.hdr`.
- If `debug_statmc_outputs: true`, extra debug HDRs are written into the current working directory:
  `debug_neighbors.hdr`, `debug_alpha.hdr`, `debug_var_total.hdr`, `debug_var_eff.hdr`, `debug_var_ratio.hdr`, `debug_weight_sum.hdr`, `debug_neff.hdr`, `debug_sensitivity_all.hdr`, `debug_sensitivity_pixel.hdr`, `debug_sensitivity_brdf.hdr`, `debug_sensitivity_lens.hdr`, `debug_sensitivity_light.hdr`, `debug_sensitivity_light_uv.hdr`, `debug_sensitivity_light_select.hdr`, `debug_sensitivity_environment.hdr`, `debug_sensitivity_rr.hdr`, `debug_sensitivity_confidence.hdr`, `debug_sensitivity_gradient.hdr`, `debug_reliability_base.hdr`, `debug_reliability_denoise.hdr`, `debug_variance_source_adaptive.hdr`, `debug_adaptive_extra_counts.hdr`.

Timing sweep helper:

```bash
scripts/benchmark_pass_timing.sh \
  --config path_tracer/config/cornell.yaml \
  --output-base output/timing_cornell \
  --spp 2,4,8 \
  --adaptive-passes 0,1,2 \
  --adaptive-spp 2 \
  --extra-args "--seed 1234"
```

This helper prints CSV rows with wall time and summed stage timings for render, post-pass denoising/sensitivity work, and adaptive rendering.

### Reproducible paper evaluation

Use `paper_eval.sh` to render one scene's paired seed matrix for uniform sampling,
raw variance-guided adaptive sampling, corrected StatMC with RPD disabled, and
corrected StatMC with RPD enabled. It writes
linear HDR RMSE/NRMSE, local log-luminance SSIM, log-gradient NRMSE, PNG metrics,
wall times, paired 95% confidence intervals, an independent-reference noise check,
residuals, ready-named paper images, the source config, and the Git revision.
The suite scores the raw adaptive output against both references and verifies that
RPD-on and RPD-off runs have byte-identical raw HDR paths, so the ablation changes
reconstruction rather than sample allocation.
The adaptive-raw row shares the StatMC no-RPD render wall time because it is an
intermediate output; that row does not measure standalone reconstruction overhead.

```bash
scripts/paper_eval.sh \
  --config path_tracer/config/cornell.yaml \
  --output-root output/paper_eval/cornell \
  --uniform-spp 16 \
  --reference-spp 512
```

Ensure the configuration's total adaptive budget equals `--uniform-spp`; for the
default Cornell configuration, $4 + 3\times4 = 16$ spp. Use a fresh output root:
the renderer replaces each method/seed directory.

`paper_suite.sh` calls that single-scene worker for the predefined Cornell, Bunny
DOF, and Dragon benchmark, then aggregates their metrics:

```bash
./scripts/paper_suite.sh
```

The suite defaults to RPD scale `1`. After choosing a scale on separate tuning
renders, preserve the tuning output and evaluate that fixed choice with fresh
seeds, for example:

```bash
./scripts/paper_suite.sh \
  --rpd-scale 0.25 \
  --seeds 1001,2002,3003,4004,5005,6006,7007,8008 \
  --output-root output/paper_eval/rpd_scale_025_heldout
```

The selected scale is recorded in every scene manifest and suite README.

Combined CSVs are written under `output/paper_eval/final/`. Validate the workflow
quickly with `./scripts/paper_suite.sh --smoke`; smoke outputs stay under
`output/paper_eval/smoke/`.

Recompute metrics from an existing full suite without rendering again:

```bash
./scripts/paper_suite.sh --rescore
```

Rescoring compares every candidate to both independent references, combines the
reference pair within each seed, and only then computes paired seed confidence
intervals. HDR/PNG errors are combined in squared-error space. The main outputs
are `all_rescore_summary.csv`, `all_rescore_paired_summary.csv`,
`all_rescore_reference_noise.csv`, and `all_rescore_invalid.csv`.

### Denoisers

Use `denoise.sh` to build/run the denoising executables with consistent flags:

```bash
# Joint bilateral
./denoise.sh joint -- --config denoising/config/joint_bilateral.yaml --raw output/foo/foo_raw.hdr --normal ... --albedo ... -o output/foo/bilateral

# À trous wavelet
./denoise.sh trous -- --config denoising/config/atrous_wavelet.yaml --raw ... --normal ... --albedo ... --iterations 5 -o output/foo/atrous

# Skip the build step if already built
./denoise.sh joint --no-build -- --config ...
```

Options: `-B/--build-dir` to choose a build dir (default `build`), `--parallel/--no-parallel` to toggle parallel build.

### Evaluation tools

Build target `eval_tools` (or use `run.sh --target eval_tools`). Binary lives at `build/shared/tools/eval_tools`. Subcommands:

```bash
./build/shared/tools/eval_tools/eval_tools compare-hdr reference.hdr test.hdr
./build/shared/tools/eval_tools/eval_tools compare-png a.png b.png
./build/shared/tools/eval_tools/eval_tools hdr-metrics ref.hdr test.hdr [residual.hdr]
./build/shared/tools/eval_tools/eval_tools self-test
```

Helper script (builds `eval_tools` if needed and runs it):

```bash
# HDR compare
./eval.sh compare-hdr -- output/foo_raw.hdr output/foo_denoised.hdr

# HDR metrics with residual output
./eval.sh hdr-metrics -- ref.hdr test.hdr residual.hdr
```

Standalone binaries `compare_hdr`, `compare_png`, and `hdr_metrics` are also built in the same folder.


## Dependencies

- [spdlog](https://github.com/gabime/spdlog) - Logging
- [stb](https://github.com/nothings/stb) - Image I/O
- [yaml-cpp](https://github.com/jbeder/yaml-cpp) - Config parsing
- [Eigen](https://eigen.tuxfamily.org/) - Linear algebra
- [tinyobjloader](https://github.com/tinyobjloader/tinyobjloader) - OBJ file loading
- [CLI11](https://github.com/CLIUtils/CLI11) - Command line parsing

## References

- [A Statistical Approach to Monte Carlo Denoising](https://users.cg.tuwien.ac.at/~hiroyuki/StatMC/) - Sakai et al., SIGGRAPH Asia 2024
- [Statistical Error Reduction for Monte Carlo Rendering](https://users.cg.tuwien.ac.at/~hiroyuki/StatER/) - Sakai et al., SIGGRAPH Asia 2025
- [P-RPF: Pixel-Based Random Parameter Filtering for Monte Carlo Rendering](https://ieeexplore.ieee.org/document/6814987/) - Park et al., CAD/Graphics 2013
- [Random-Parameter Filtering for Monte Carlo Rendering](https://dl.acm.org/doi/10.1145/2167076.2167083) - Sen and Darabi, EGSR 2012
