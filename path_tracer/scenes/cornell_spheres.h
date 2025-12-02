#pragma once

#include <array>

#include "scenes/scene_utils.h"
#include "scenes/material_library.h"
#include "core/sphere.h"

namespace scenes {

inline Scene make_cornell_spheres() {
    Scene scene;
    scene.environment_color = Vec3f::Zero();

    // Materials
    const int white_id = scene.add_material(Material{Lambertian{Vec3f(0.78f, 0.78f, 0.78f)}});
    const int red_id = scene.add_material(Material{Lambertian{Vec3f(0.63f, 0.065f, 0.05f)}});
    const int green_id = scene.add_material(Material{Lambertian{Vec3f(0.14f, 0.45f, 0.091f)}});
    const int light_mat_id = scene.add_material(Material{Lambertian{Vec3f(1.0f, 1.0f, 1.0f)}});

    // Sphere materials
    const int mirror_white_id = scene.add_material(Material{MirrorWhite});
    const int gold_id = scene.add_material(Material{Gold});
    const int silver_id = scene.add_material(Material{Silver});

    const int back_id = white_id;
    const int bottom_id = white_id;
    const int left_id = red_id;
    const int right_id = green_id;
    const int top_id = white_id;

    const float scale = 0.001f;
    auto cbox_point = [&](float x, float y, float z) {
        return Vec3f(x * scale, y * scale, z * scale);
    };

    const Vec3f room_center = cbox_point(278.0f, 274.4f, 279.6f);

    auto add_triangle_oriented = [&](const Vec3f& v0, const Vec3f& v1, const Vec3f& v2,
                                     int mat_id, const Vec3f& emission,
                                     const Vec3f& reference_point, bool face_toward_reference) {
        Triangle tri = make_triangle(v0, v1, v2, mat_id, emission);
        Vec3f tri_center = (v0 + v1 + v2) / 3.0f;
        float orient = tri.normal.dot(reference_point - tri_center);
        bool should_flip = face_toward_reference ? (orient < 0.0f) : (orient > 0.0f);
        if (should_flip) tri.normal = -tri.normal;
        return scene.add_triangle(tri);
    };

    auto add_quad = [&](const Vec3f& v0, const Vec3f& v1, const Vec3f& v2, const Vec3f& v3,
                        int mat_id, const Vec3f& reference_point, bool face_toward_reference) {
        add_triangle_oriented(v0, v1, v2, mat_id, Vec3f::Zero(), reference_point, face_toward_reference);
        add_triangle_oriented(v0, v2, v3, mat_id, Vec3f::Zero(), reference_point, face_toward_reference);
    };

    // Light setup
    const Vec3f light_color(70.0f, 70.0f, 70.0f);
    Vec3f light_origin = cbox_point(213.0f, 548.0f, 227.0f);
    Vec3f light_u = cbox_point(343.0f, 548.0f, 227.0f) - light_origin;
    Vec3f light_v = cbox_point(213.0f, 548.0f, 332.0f) - light_origin;
    int light_idx = scene.add_light(make_rect_light(light_origin, light_u, light_v, light_color));

    auto add_light_tri = [&](const Vec3f& v0, const Vec3f& v1, const Vec3f& v2) {
        int tri = add_triangle_oriented(v0, v1, v2, light_mat_id, light_color, room_center, true);
        scene.link_triangle_to_light(tri, light_idx);
    };

    Vec3f l0 = cbox_point(213.0f, 548.0f, 227.0f);
    Vec3f l1 = cbox_point(343.0f, 548.0f, 227.0f);
    Vec3f l2 = cbox_point(343.0f, 548.0f, 332.0f);
    Vec3f l3 = cbox_point(213.0f, 548.0f, 332.0f);
    add_light_tri(l0, l2, l1);
    add_light_tri(l0, l3, l2);

    // Floor and ceiling
    add_quad(cbox_point(552.8f, 0.0f, 0.0f), cbox_point(0.0f, 0.0f, 0.0f), cbox_point(0.0f, 0.0f, 559.2f), cbox_point(549.6f, 0.0f, 559.2f), bottom_id, room_center, true);
    add_quad(cbox_point(556.0f, 548.8f, 0.0f), cbox_point(556.0f, 548.8f, 559.2f), cbox_point(0.0f, 548.8f, 559.2f), cbox_point(0.0f, 548.8f, 0.0f), top_id, room_center, true);

    // Walls
    add_quad(cbox_point(549.6f, 0.0f, 559.2f), cbox_point(0.0f, 0.0f, 559.2f), cbox_point(0.0f, 548.8f, 559.2f), cbox_point(556.0f, 548.8f, 559.2f), back_id, room_center, true);
    add_quad(cbox_point(0.0f, 0.0f, 559.2f), cbox_point(0.0f, 0.0f, 0.0f), cbox_point(0.0f, 548.8f, 0.0f), cbox_point(0.0f, 548.8f, 559.2f), right_id, room_center, true);
    add_quad(cbox_point(552.8f, 0.0f, 0.0f), cbox_point(549.6f, 0.0f, 559.2f), cbox_point(556.0f, 548.8f, 559.2f), cbox_point(556.0f, 548.8f, 0.0f), left_id, room_center, true);

    // Left sphere (small, mirror white) - on the green side
    Sphere sphere1;
    sphere1.center = cbox_point(150.0f, 90.0f, 150.0f);
    sphere1.radius = 0.09f;
    sphere1.material_id = mirror_white_id;
    sphere1.emission = Vec3f::Zero();
    scene.add_sphere(sphere1);

    // Center sphere (large, gold) - slightly to the right and back
    Sphere sphere2;
    sphere2.center = cbox_point(350.0f, 120.0f, 350.0f);
    sphere2.radius = 0.12f;
    sphere2.material_id = gold_id;
    sphere2.emission = Vec3f::Zero();
    scene.add_sphere(sphere2);

    // Right sphere (medium, silver) - on the red side
    Sphere sphere3;
    sphere3.center = cbox_point(450.0f, 80.0f, 200.0f);
    sphere3.radius = 0.08f;
    sphere3.material_id = silver_id;
    sphere3.emission = Vec3f::Zero();
    scene.add_sphere(sphere3);

    return scene;
}

} // namespace scenes
