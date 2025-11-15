#pragma once

#include "scenes/scene_utils.h"

namespace scenes {

inline Scene make_debug_scene() {
    Scene scene;
    const int floor_id = scene.add_material(Material(Vec3f(0.8f, 0.8f, 0.8f)));
    const int left_id = scene.add_material(Material(Vec3f(0.2f, 0.6f, 0.9f)));
    const int right_id = scene.add_material(Material(Vec3f(0.9f, 0.3f, 0.3f)));

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

} // namespace scenes
