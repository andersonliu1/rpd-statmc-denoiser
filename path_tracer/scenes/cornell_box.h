#pragma once

#include <array>

#include "scenes/scene_utils.h"

namespace scenes {

inline Scene make_cornell_box() {
    Scene scene;
    scene.environment_color = Vec3f::Zero();

    const int white_id = scene.add_material(Material{Lambertian{Vec3f(0.78f, 0.78f, 0.78f)}});
    const int red_id = scene.add_material(Material{Lambertian{Vec3f(0.63f, 0.065f, 0.05f)}});
    const int green_id = scene.add_material(Material{Lambertian{Vec3f(0.14f, 0.45f, 0.091f)}});
    const int light_mat_id = scene.add_material(Material{Lambertian{Vec3f(1.0f, 1.0f, 1.0f)}});
    const int box_white_id = scene.add_material(Material{Lambertian{Vec3f(0.73f, 0.73f, 0.73f)}});

    const int back_id = white_id;
    const int bottom_id = white_id;
    const int left_id = red_id;
    const int right_id = green_id;
    const int top_id = white_id;
    const int short_box_mat_id = box_white_id;
    const int tall_box_mat_id = box_white_id;

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

    // Short box
    Vec3f sb0 = cbox_point(130.0f, 0.0f, 65.0f);
    Vec3f sb1 = cbox_point(82.0f, 0.0f, 225.0f);
    Vec3f sb2 = cbox_point(240.0f, 0.0f, 272.0f);
    Vec3f sb3 = cbox_point(290.0f, 0.0f, 114.0f);
    Vec3f sb4 = cbox_point(130.0f, 165.0f, 65.0f);
    Vec3f sb5 = cbox_point(82.0f, 165.0f, 225.0f);
    Vec3f sb6 = cbox_point(240.0f, 165.0f, 272.0f);
    Vec3f sb7 = cbox_point(290.0f, 165.0f, 114.0f);

    const Vec3f short_box_center = (sb0 + sb2 + sb5 + sb7) * 0.25f;
    add_quad(sb4, sb5, sb6, sb7, short_box_mat_id, short_box_center, false);
    add_quad(sb0, sb4, sb7, sb3, short_box_mat_id, short_box_center, false);
    add_quad(sb3, sb7, sb6, sb2, short_box_mat_id, short_box_center, false);
    add_quad(sb2, sb6, sb5, sb1, short_box_mat_id, short_box_center, false);
    add_quad(sb1, sb5, sb4, sb0, short_box_mat_id, short_box_center, false);

    // Tall box
    Vec3f tb0 = cbox_point(423.0f, 0.0f, 247.0f);
    Vec3f tb1 = cbox_point(265.0f, 0.0f, 296.0f);
    Vec3f tb2 = cbox_point(314.0f, 0.0f, 456.0f);
    Vec3f tb3 = cbox_point(472.0f, 0.0f, 406.0f);
    Vec3f tb4 = cbox_point(423.0f, 330.0f, 247.0f);
    Vec3f tb5 = cbox_point(265.0f, 330.0f, 296.0f);
    Vec3f tb6 = cbox_point(314.0f, 330.0f, 456.0f);
    Vec3f tb7 = cbox_point(472.0f, 330.0f, 406.0f);

    const Vec3f tall_box_center = (tb0 + tb2 + tb5 + tb7) * 0.25f;
    add_quad(tb4, tb5, tb6, tb7, tall_box_mat_id, tall_box_center, false);
    add_quad(tb0, tb4, tb7, tb3, tall_box_mat_id, tall_box_center, false);
    add_quad(tb3, tb7, tb6, tb2, tall_box_mat_id, tall_box_center, false);
    add_quad(tb2, tb6, tb5, tb1, tall_box_mat_id, tall_box_center, false);
    add_quad(tb1, tb5, tb4, tb0, tall_box_mat_id, tall_box_center, false);

    return scene;
}

} // namespace scenes
