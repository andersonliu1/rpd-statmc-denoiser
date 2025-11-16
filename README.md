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
./run.sh

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
./build/path_tracer_build/path_tracer \
    -c path_tracer/config/test.yaml \
    --scene cornell_box \
    -o output.png
```

Arguments:
- `-c, --config`: Image + camera + sampling settings.
- `--scene`: Built-in scene to render (e.g., `cornell_box`, `debug`).
- `-o, --output`: Output PNG path.
- `--seed`: Optional sampler RNG seed (defaults to random).


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
