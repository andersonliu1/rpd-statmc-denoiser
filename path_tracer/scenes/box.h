#pragma once

#include <array>

#include "scenes/scene_utils.h"

namespace scenes {

inline Scene make_box() {
    Scene scene;
    scene.environment_color = Vec3f::Zero();

    const std::array<Material, 6> material_defs = {
        Material{Lambertian{Vec3f(0.874000013f, 0.874000013f, 0.875000000f)}}, // Back
        Material{Lambertian{Vec3f(0.874000013f, 0.874000013f, 0.875000000f)}}, // Bottom
        Material{Lambertian{Vec3f(0.0f, 0.2117f, 0.3765f)}},                   // Left
        Material{Lambertian{Vec3f(0.996f, 0.7373f, 0.0667f)}},                  // Right
        Material{Lambertian{Vec3f(0.874000013f, 0.874000013f, 0.875000000f)}}, // Top
        Material{Lambertian{Vec3f(1.0f, 1.0f, 1.0f)}}                           // Light
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
    const int light_mat_id = material_ids[5];

    const float light_x = 0.195f;
    const float light_y_sn = -0.355f;
    const float light_z_sn = 0.545f;
    const float light_len_x = 0.16f;
    const float light_len_y = 0.16f;
    const Vec3f light_color(50.0f, 50.0f, 50.0f);

    auto to_scene = [](const Vec3f& v) {
        return Vec3f(v.x(), v.z(), -v.y());
    };

    const Vec3f light_origin = to_scene(Vec3f(light_x, light_y_sn, light_z_sn));
    const Vec3f light_u(light_len_x, 0.0f, 0.0f);
    const Vec3f light_v(0.0f, 0.0f, light_len_y);

    int light_idx = scene.add_light(make_rect_light(light_origin, light_u, light_v, light_color));

    auto add_light_tri = [&](const Vec3f& v0, const Vec3f& v1, const Vec3f& v2) {
        int tri = scene.add_triangle(make_triangle(v0, v1, v2, light_mat_id, light_color));
        scene.link_triangle_to_light(tri, light_idx);
    };

    add_light_tri(to_scene(Vec3f(light_x, light_y_sn + light_len_y, light_z_sn)),
                  to_scene(Vec3f(light_x + light_len_x, light_y_sn, light_z_sn)),
                  to_scene(Vec3f(light_x, light_y_sn, light_z_sn)));
    add_light_tri(to_scene(Vec3f(light_x, light_y_sn + light_len_y, light_z_sn)),
                  to_scene(Vec3f(light_x + light_len_x, light_y_sn + light_len_y, light_z_sn)),
                  to_scene(Vec3f(light_x + light_len_x, light_y_sn, light_z_sn)));

    // Back wall
    scene.add_triangle(make_triangle(to_scene(Vec3f(0.000000133f, -0.559199989f, 0.548799932f)),
                                     to_scene(Vec3f(0.555999935f, -0.559199989f, 0.000000040f)),
                                     to_scene(Vec3f(0.000000133f, -0.559199989f, 0.000000040f)), back_id));
    scene.add_triangle(make_triangle(to_scene(Vec3f(0.000000133f, -0.559199989f, 0.548799932f)),
                                     to_scene(Vec3f(0.555999935f, -0.559199989f, 0.548799932f)),
                                     to_scene(Vec3f(0.555999935f, -0.559199989f, 0.000000040f)), back_id));

    // Bottom
    scene.add_triangle(make_triangle(to_scene(Vec3f(0.000000133f, -0.559199989f, 0.000000040f)),
                                     to_scene(Vec3f(0.555999935f, -0.559199989f, 0.000000040f)),
                                     to_scene(Vec3f(0.555999935f, -0.000000119f, 0.000000040f)), bottom_id));
    scene.add_triangle(make_triangle(to_scene(Vec3f(0.000000133f, -0.559199989f, 0.000000040f)),
                                     to_scene(Vec3f(0.555999935f, -0.000000119f, 0.000000040f)),
                                     to_scene(Vec3f(0.000000133f, -0.000000119f, 0.000000040f)), bottom_id));

    // Left wall
    scene.add_triangle(make_triangle(to_scene(Vec3f(0.555999935f, -0.000000119f, 0.548799932f)),
                                     to_scene(Vec3f(0.555999935f, -0.000000119f, 0.000000040f)),
                                     to_scene(Vec3f(0.555999935f, -0.559199989f, 0.000000040f)), left_id));
    scene.add_triangle(make_triangle(to_scene(Vec3f(0.555999935f, -0.000000119f, 0.548799932f)),
                                     to_scene(Vec3f(0.555999935f, -0.559199989f, 0.000000040f)),
                                     to_scene(Vec3f(0.555999935f, -0.559199989f, 0.548799932f)), left_id));

    // Right wall
    scene.add_triangle(make_triangle(to_scene(Vec3f(0.000000133f, -0.559199989f, 0.000000040f)),
                                     to_scene(Vec3f(0.000000133f, -0.000000119f, 0.000000040f)),
                                     to_scene(Vec3f(0.000000133f, -0.000000119f, 0.548799932f)), right_id));
    scene.add_triangle(make_triangle(to_scene(Vec3f(0.000000133f, -0.559199989f, 0.000000040f)),
                                     to_scene(Vec3f(0.000000133f, -0.000000119f, 0.548799932f)),
                                     to_scene(Vec3f(0.000000133f, -0.559199989f, 0.548799932f)), right_id));

    // Top
    scene.add_triangle(make_triangle(to_scene(Vec3f(0.000000133f, -0.000000119f, 0.548799932f)),
                                     to_scene(Vec3f(0.555999935f, -0.559199989f, 0.548799932f)),
                                     to_scene(Vec3f(0.000000133f, -0.559199989f, 0.548799932f)), top_id));
    scene.add_triangle(make_triangle(to_scene(Vec3f(0.000000133f, -0.000000119f, 0.548799932f)),
                                     to_scene(Vec3f(0.555999935f, -0.000000119f, 0.548799932f)),
                                     to_scene(Vec3f(0.555999935f, -0.559199989f, 0.548799932f)), top_id));

    return scene;
}

} // namespace scenes
