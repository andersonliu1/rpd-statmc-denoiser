#pragma once

#include <array>

#include "scenes/scene_utils.h"

namespace scenes {

inline Scene make_debug_scene() {
    Scene scene;
    scene.environment_color = Vec3f::Zero();

    const std::array<Material, 3> material_defs = {
        Material{Lambertian{Vec3f(0.8f, 0.8f, 0.8f)}},
        Material{Lambertian{Vec3f(0.2f, 0.6f, 0.9f)}},
        Material{Lambertian{Vec3f(0.9f, 0.3f, 0.3f)}}
    };

    std::array<int, material_defs.size()> material_ids{};
    for (size_t i = 0; i < material_defs.size(); ++i) {
        material_ids[i] = scene.add_material(material_defs[i]);
    }

    const int floor_id = material_ids[0];
    const int left_id = material_ids[1];
    const int right_id = material_ids[2];

    scene.add_triangle(make_triangle(
        Vec3f(-1.0f, -1.0f, -1.0f),
        Vec3f(1.0f, -1.0f, -1.0f),
        Vec3f(1.0f, -1.0f, -3.0f),
        floor_id));
    scene.add_triangle(make_triangle(
        Vec3f(-1.0f, -1.0f, -1.0f),
        Vec3f(1.0f, -1.0f, -3.0f),
        Vec3f(-1.0f, -1.0f, -3.0f),
        floor_id));

    scene.add_triangle(make_triangle(
        Vec3f(-0.2f, -1.0f, -1.5f),
        Vec3f(-0.2f, 0.2f, -1.5f),
        Vec3f(-0.8f, 0.2f, -2.5f),
        left_id));
    scene.add_triangle(make_triangle(
        Vec3f(-0.2f, -1.0f, -1.5f),
        Vec3f(-0.8f, 0.2f, -2.5f),
        Vec3f(-0.8f, -1.0f, -2.5f),
        left_id));

    scene.add_triangle(make_triangle(
        Vec3f(0.5f, -1.0f, -2.0f),
        Vec3f(0.9f, 0.5f, -2.2f),
        Vec3f(0.1f, 0.4f, -2.5f),
        right_id));

    return scene;
}

}
