#include <filesystem>
#include <stdexcept>

#include "core/config.h"

static YAML::Node minimal_config() {
    return YAML::Load(R"(
image_width: 16
image_height: 8
samples_per_pixel: 2
fov: 45
camera_position: [0, 0, 0]
camera_direction: [0, 0, 1]
scene: cornell_box
output: test
)");
}

static void require(bool condition) {
    if (!condition) throw std::runtime_error("config test failed");
}

int main(int argc, char** argv) {
    require(argc == 2);
    const RenderConfig config = parse_render_config(minimal_config());
    require(config.camera_up.isApprox(Vec3f::UnitY()));
    require(config.tonemap == "agx");
    require(config.rpf_tile_size == 8);
    require(config.adaptive_importance_smoothing_radius == 1);

    auto unknown = minimal_config();
    unknown["unused_setting"] = 1;
    bool rejected_unknown = false;
    try {
        (void)parse_render_config(unknown);
    } catch (const std::runtime_error&) {
        rejected_unknown = true;
    }
    require(rejected_unknown);

    auto invalid = minimal_config();
    invalid["color_sigma_max"] = 0;
    bool rejected_invalid = false;
    try {
        (void)parse_render_config(invalid);
    } catch (const std::runtime_error&) {
        rejected_invalid = true;
    }
    require(rejected_invalid);

    for (const auto& entry : std::filesystem::directory_iterator(argv[1])) {
        if (entry.path().extension() == ".yaml") {
            (void)parse_render_config(YAML::LoadFile(entry.path().string()));
        }
    }
}
