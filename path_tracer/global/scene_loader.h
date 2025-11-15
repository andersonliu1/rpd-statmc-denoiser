#pragma once

#include "scene.h"
#include <string>

namespace scene_loader {

Scene load_scene(const std::string& scene_file, const std::string& asset_root);

} // namespace scene_loader
