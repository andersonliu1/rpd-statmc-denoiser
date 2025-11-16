#pragma once

#include <array>

#include "scenes/scene_utils.h"

namespace scenes {

inline Scene make_cornell_box() {
    Scene scene;

    const std::array<Material, 6> material_defs = {
        Material{Lambertian{Vec3f(0.774f, 0.274f, 0.445f)}},
        Material{Lambertian{Vec3f(0.874f, 0.874f, 0.875f)}},
        Material{Lambertian{Vec3f(0.0f, 0.2117f, 0.3765f)}},
        Material{Lambertian{Vec3f(0.996f, 0.7373f, 0.0667f)}},
        Material{Lambertian{Vec3f(0.894f, 0.894f, 0.895f)}},
        Material{Lambertian{Vec3f(1.0f, 1.0f, 1.0f)}}
    };

    std::array<int, material_defs.size()> material_ids{};
    for (size_t i = 0; i < material_defs.size(); ++i) {
        material_ids[i] = scene.add_material(material_defs[i]);
    }

    const int back_id = material_ids[0];
    const int bottom_id = material_ids[1];
    const int left_id = material_ids[2];
    const int right_id = material_ids[3];
    const int top_id = material_ids[4];
    const int light_id = material_ids[5];

    constexpr float light_x = 0.195f;
    constexpr float light_y = -0.355f;
    constexpr float light_z = 0.545f;
    constexpr float light_len_x = 0.16f;
    constexpr float light_len_y = 0.16f;
    const Vec3f light_emission(70.0f, 70.0f, 70.0f);

    // Light
    scene.add_triangle(make_triangle(
        Vec3f(light_x, light_y + light_len_y, light_z),
        Vec3f(light_x + light_len_x, light_y, light_z),
        Vec3f(light_x, light_y, light_z),
        light_id,
        light_emission));
    scene.add_triangle(make_triangle(
        Vec3f(light_x, light_y + light_len_y, light_z),
        Vec3f(light_x + light_len_x, light_y + light_len_y, light_z),
        Vec3f(light_x + light_len_x, light_y, light_z),
        light_id,
        light_emission));

    // Back wall
    scene.add_triangle(make_triangle(
        Vec3f(0.0f, -0.5592f, 0.5488f),
        Vec3f(0.5560f, -0.5592f, 0.0f),
        Vec3f(0.0f, -0.5592f, 0.0f),
        back_id));
    scene.add_triangle(make_triangle(
        Vec3f(0.0f, -0.5592f, 0.5488f),
        Vec3f(0.5560f, -0.5592f, 0.5488f),
        Vec3f(0.5560f, -0.5592f, 0.0f),
        back_id));

    // Floor
    scene.add_triangle(make_triangle(
        Vec3f(0.0f, -0.5592f, 0.0f),
        Vec3f(0.5560f, -0.5592f, 0.0f),
        Vec3f(0.5560f, 0.0f, 0.0f),
        bottom_id));
    scene.add_triangle(make_triangle(
        Vec3f(0.0f, -0.5592f, 0.0f),
        Vec3f(0.5560f, 0.0f, 0.0f),
        Vec3f(0.0f, 0.0f, 0.0f),
        bottom_id));

    // Left wall
    scene.add_triangle(make_triangle(
        Vec3f(0.5560f, 0.0f, 0.5488f),
        Vec3f(0.5560f, 0.0f, 0.0f),
        Vec3f(0.5560f, -0.5592f, 0.0f),
        left_id));
    scene.add_triangle(make_triangle(
        Vec3f(0.5560f, 0.0f, 0.5488f),
        Vec3f(0.5560f, -0.5592f, 0.0f),
        Vec3f(0.5560f, -0.5592f, 0.5488f),
        left_id));

    // Right wall
    scene.add_triangle(make_triangle(
        Vec3f(0.0f, -0.5592f, 0.0f),
        Vec3f(0.0f, 0.0f, 0.0f),
        Vec3f(0.0f, 0.0f, 0.5488f),
        right_id));
    scene.add_triangle(make_triangle(
        Vec3f(0.0f, -0.5592f, 0.0f),
        Vec3f(0.0f, 0.0f, 0.5488f),
        Vec3f(0.0f, -0.5592f, 0.5488f),
        right_id));

    // Ceiling
    scene.add_triangle(make_triangle(
        Vec3f(0.0f, 0.0f, 0.5488f),
        Vec3f(0.5560f, -0.5592f, 0.5488f),
        Vec3f(0.0f, -0.5592f, 0.5488f),
        top_id));
    scene.add_triangle(make_triangle(
        Vec3f(0.0f, 0.0f, 0.5488f),
        Vec3f(0.5560f, 0.0f, 0.5488f),
        Vec3f(0.5560f, -0.5592f, 0.5488f),
        top_id));

    return scene;
}

} // namespace scenes
