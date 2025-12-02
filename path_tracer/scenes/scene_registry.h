#pragma once

#include <stdexcept>
#include <string>

#include "scenes/bunny.h"
#include "scenes/cornell_box.h"
#include "scenes/cornell_spheres.h"
#include "scenes/veach_mis.h"
#include "scenes/material_showcase.h"
#include "scenes/caustics.h"
#include "scenes/debug_scene.h"
#include "scenes/dragon.h"
#include "scenes/bunny_dof.h"

namespace scenes {

    inline Scene make_scene(const std::string& name) {
        // Classic scenes
        if (name == "cornell_box") {
            return make_cornell_box();
        }
        if (name == "debug") {
            return make_debug_scene();
        }

        // Sphere scenes
        if (name == "cornell_spheres") {
            return make_cornell_spheres();
        }
        if (name == "veach_mis") {
            return make_veach_mis();
        }
        if (name == "material_showcase") {
            return make_material_showcase();
        }
        if (name == "caustics") {
            return make_caustics();
        }

        // Mesh scenes (OBJ models)
        if (name == "bunny") {
            return make_bunny();
        }
        if (name == "bunny_dof") {
            return make_bunny_dof();
        }
        if (name == "dragon") {
            return make_dragon();
        }

        throw std::runtime_error("Unknown scene '" + name + "'. Available scenes:\n"
            "  Primitives: cornell_box, cornell_spheres, veach_mis, material_showcase, caustics\n"
            "  Meshes: bunny, bunny_dof, dragon\n"
            "  Debug: debug");
    }

} // namespace scenes
