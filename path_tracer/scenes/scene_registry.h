#pragma once

#include <stdexcept>
#include <string>

#include "scenes/cornell_box.h"
#include "scenes/debug_scene.h"

namespace scenes {

    const Microfacet Gold {
        .albedo = Vec3f(1.0f, 0.71f, 0.29f),
        .roughness = 0.0005f,
        .n1 = Vec3f::Ones(),
        .n2 = Vec3f(0.17f, 0.35f, 1.5f),
        .distribution = Microfacet::Distribution::GGX
    };

    inline Scene make_scene(const std::string& name) {
        if (name == "cornell_box") {
            return make_cornell_box();
        }
        if (name == "debug") {
            return make_debug_scene();
        }

        throw std::runtime_error("Unknown scene '" + name + "'. Available scenes: cornell_box, debug");
    }

} // namespace scenes
