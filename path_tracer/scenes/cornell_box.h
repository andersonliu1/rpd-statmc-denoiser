#pragma once

#include "scenes/scene_utils.h"

namespace scenes {

inline Scene make_cornell_box() {
    Scene scene;

    const int back_id = scene.add_material(Material(Vec3f(0.774f, 0.274f, 0.445f)));
    const int bottom_id = scene.add_material(Material(Vec3f(0.874f, 0.874f, 0.875f)));
    const int left_id = scene.add_material(Material(Vec3f(0.0f, 0.2117f, 0.3765f)));
    const int right_id = scene.add_material(Material(Vec3f(0.996f, 0.7373f, 0.0667f)));
    const int top_id = scene.add_material(Material(Vec3f(0.894f, 0.894f, 0.895f)));
    const int light_id = scene.add_material(Material(Vec3f(1.0f, 1.0f, 1.0f)));

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
