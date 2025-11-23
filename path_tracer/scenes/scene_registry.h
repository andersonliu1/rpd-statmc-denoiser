#pragma once

#include <stdexcept>
#include <string>

#include "scenes/bunny.h"
#include "scenes/cornell_box.h"
#include "scenes/debug_scene.h"

namespace scenes {

    inline Scene make_scene(const std::string& name) {
        if (name == "cornell_box") {
            return make_cornell_box();
        }
        if (name == "bunny") {
            return make_bunny();
        }
        if (name == "debug") {
            return make_debug_scene();
        }

        throw std::runtime_error("Unknown scene '" + name + "'. Available scenes: cornell_box, box, debug");
    }

} // namespace scenes
