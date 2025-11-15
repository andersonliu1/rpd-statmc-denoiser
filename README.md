# MC Denoise

Monte Carlo path tracing with denoising for CSCI 580.

## Setup

```bash
git submodule update --init --recursive
mkdir build && cd build
cmake ..
make
```

## Run

```bash
./mcdenoise -c config/test.yaml --asset-root assets --scene scenes/test_scene.yaml -o output.png
```

Arguments:
- `-c, --config`: Image + camera + sampling settings.
- `--asset-root`: Root folder for OBJ files referenced by scene meshes.
- `--scene`: Scene YAML under `scenes/` describing objects.
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
