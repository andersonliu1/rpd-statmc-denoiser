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
./build/path_tracer_build/path_tracer \
    -c path_tracer/config/test.yaml \
    --scene cornell_box \
    -o ./output/cornell \
    --seed 1234

# or use the helper script to build (optional) and run:
./trace.sh -- -c path_tracer/config/test.yaml -o ./output/cornell

# Skip the build step if you already built:
./trace.sh --no-build -- -c path_tracer/config/test.yaml -s cornell_box
```

Arguments:
- `-c, --config`: Image + camera + sampling settings (YAML). May include `scene` and `output`.
- `--scene`: Built-in scene to render (overrides YAML `scene` when supplied).
- `-o, --output`: Output directory (absolute or relative to CWD). The renderer writes `<dir>/<leaf-name>.png`, where `leaf-name` is taken from the directory name. Overrides YAML `output` when supplied.
- `--seed`: Optional sampler RNG seed (defaults to random).
- Additional CLI arguments follow the same precedence rules: CLI overrides YAML, YAML is the fallback.

YAML `output` values are treated as relative names (e.g., `output: cornell` saves to `output/cornell/cornell.png`) unless you supply an absolute path. CLI `-o` values are used as given: absolute paths go there; relative paths are resolved against the current working directory.


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
