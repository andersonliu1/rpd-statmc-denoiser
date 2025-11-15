#pragma once

#include <stdexcept>
#include <string>

#include "scene.h"

#include "scenes/registry.h"

namespace scenes {

inline Scene load_scene(const std::string& scene_name) {
    return make_scene(scene_name);
}

} // namespace scenes
