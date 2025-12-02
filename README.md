# MC Denoise

Monte Carlo path tracing with denoising for CSCI 580.

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

When another target (e.g., a denoiser) lands in the tree, use `./run.sh --target <name>`
to build or clean it without touching the others.

### Running the path tracer

```bash
# From the repo root after building
./build/path_tracer_build/path_tracer/path_tracer \
    -c path_tracer/config/cornell.yaml \
    --scene cornell_box \
    -o ./output/cornell \
    --seed 1234

# or use the helper script to build (optional) and run (parallel build by default):
./trace.sh -- -c path_tracer/config/test.yaml -o ./output/cornell

# Skip the build step if you already built:
./trace.sh --no-build -- -c path_tracer/config/test.yaml -s cornell_box
```

Arguments:
- `-c, --config`: Image + camera + sampling settings (YAML). May include `scene` and `output`.
- `--scene`: Built-in scene to render (overrides YAML `scene` when supplied).
- `-o, --output`: Output directory (absolute or relative to CWD). The renderer writes `<dir>/<leaf-name>.png`, where `leaf-name` is taken from the directory name. Overrides YAML `output` when supplied.
- `--tonemap <preset>`: Tonemapping preset (`aces`, `agx`, `agx-golden`, `agx-punchy`). Overrides YAML `tonemap`.
- `--statmc` / `--no-statmc`: Enable or disable StatMC/RPF rendering (overrides YAML `statmc.enabled`).
- `--rpf-tile-size <N>`: RPF tile size (overrides YAML `statmc.rpf_tile_size`).
- `--rpf-target-samples <N>`: Target pooled samples per tile for RPF (-1 = auto; overrides YAML `statmc.rpf_target_samples`).
- `--rpf-max-radius <N>`: Max pooling radius in tiles (-1 = auto; overrides YAML `statmc.rpf_max_radius`).
- `--seed`: Optional sampler RNG seed (defaults to random).
- Additional CLI arguments follow the same precedence rules: CLI overrides YAML, YAML is the fallback.

YAML `output` values are treated as relative names (e.g., `output: cornell` saves to `output/cornell/cornell.png`) unless you supply an absolute path. CLI `-o` values are used as given: absolute paths go there; relative paths are resolved against the current working directory.

StatMC/RPF/adaptive settings in YAML (flat keys):

```yaml
tonemap: agx  # aces | agx | agx-golden | agx-punchy
statmc_enabled: false
rpf_tile_size: 8
rpf_target_samples: -1  # -1 = auto
rpf_max_radius: -1      # -1 = auto
color_window_radius: 1
color_normal_threshold: 0.95
color_depth_threshold: 0.01
color_compat_sigma: 1.5
color_shrinkage_k: 0.001
var_window_radius: 1
var_normal_threshold: 0.95
var_depth_threshold: 0.01
var_compat_sigma: 1.5
var_shrinkage_k: 0.001
var_iterations: 2
adaptive_base_samples: 0
adaptive_spp: 0
adaptive_sigma_max: 3.0
adaptive_passes: 1
```

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
./build/shared/tools/eval_tools/eval_tools compare-hdr a.hdr b.hdr
./build/shared/tools/eval_tools/eval_tools compare-png a.png b.png
./build/shared/tools/eval_tools/eval_tools hdr-metrics ref.hdr test.hdr [residual.hdr]
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
