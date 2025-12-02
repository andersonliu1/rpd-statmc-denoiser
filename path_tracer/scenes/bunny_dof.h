#pragma once

#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <vector>

#include "scenes/scene_utils.h"
#include "core/obj_loader.h"
#include "scenes/material_library.h"

namespace scenes {

inline Scene make_bunny_dof() {
    Scene scene;
    scene.environment_color = Vec3f::Zero();

    // Cornell-like box materials
    const std::array<Material, 6> material_defs = {
        Material{Lambertian{Vec3f(0.874f, 0.874f, 0.875f)}}, // Back
        Material{Lambertian{Vec3f(0.874f, 0.874f, 0.875f)}}, // Bottom
        Material{Lambertian{Vec3f(0.0f, 0.2117f, 0.3765f)}}, // Left
        Material{Lambertian{Vec3f(0.996f, 0.7373f, 0.0667f)}}, // Right
        Material{Lambertian{Vec3f(0.874f, 0.874f, 0.875f)}}, // Top
        Material{Lambertian{Vec3f(1.0f, 1.0f, 1.0f)}} // Light
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

    const int ground_mat = bottom_id;
    const int sphere_red = scene.add_material(Material{Lambertian{Vec3f(0.75f, 0.2f, 0.2f)}});
    const int sphere_green = scene.add_material(Material{Lambertian{Vec3f(0.2f, 0.75f, 0.2f)}});
    const int sphere_blue = scene.add_material(Material{Lambertian{Vec3f(0.2f, 0.35f, 0.9f)}});
    const int sphere_gold = scene.add_material(Material{Gold});
    const int sphere_copper = scene.add_material(Material{Copper});
    const int bunny_mat_diffuse = scene.add_material(Material{Lambertian{Vec3f(0.8f, 0.7f, 0.6f)}});
    const int bunny_mat_gold = scene.add_material(Material{Gold});
    const int bunny_mat_copper = scene.add_material(Material{Copper});

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

    // Box walls
    scene.add_triangle(make_triangle(to_scene(Vec3f(0.000000133f, -0.559199989f, 0.548799932f)),
                                     to_scene(Vec3f(0.555999935f, -0.559199989f, 0.000000040f)),
                                     to_scene(Vec3f(0.000000133f, -0.559199989f, 0.000000040f)), back_id));
    scene.add_triangle(make_triangle(to_scene(Vec3f(0.000000133f, -0.559199989f, 0.548799932f)),
                                     to_scene(Vec3f(0.555999935f, -0.559199989f, 0.548799932f)),
                                     to_scene(Vec3f(0.555999935f, -0.559199989f, 0.000000040f)), back_id));

    scene.add_triangle(make_triangle(to_scene(Vec3f(0.000000133f, -0.559199989f, 0.000000040f)),
                                     to_scene(Vec3f(0.555999935f, -0.559199989f, 0.000000040f)),
                                     to_scene(Vec3f(0.555999935f, -0.000000119f, 0.000000040f)), ground_mat));
    scene.add_triangle(make_triangle(to_scene(Vec3f(0.000000133f, -0.559199989f, 0.000000040f)),
                                     to_scene(Vec3f(0.555999935f, -0.000000119f, 0.000000040f)),
                                     to_scene(Vec3f(0.000000133f, -0.000000119f, 0.000000040f)), ground_mat));

    scene.add_triangle(make_triangle(to_scene(Vec3f(0.555999935f, -0.000000119f, 0.548799932f)),
                                     to_scene(Vec3f(0.555999935f, -0.000000119f, 0.000000040f)),
                                     to_scene(Vec3f(0.555999935f, -0.559199989f, 0.000000040f)), left_id));
    scene.add_triangle(make_triangle(to_scene(Vec3f(0.555999935f, -0.000000119f, 0.548799932f)),
                                     to_scene(Vec3f(0.555999935f, -0.559199989f, 0.000000040f)),
                                     to_scene(Vec3f(0.555999935f, -0.559199989f, 0.548799932f)), left_id));

    scene.add_triangle(make_triangle(to_scene(Vec3f(0.000000133f, -0.559199989f, 0.000000040f)),
                                     to_scene(Vec3f(0.000000133f, -0.000000119f, 0.000000040f)),
                                     to_scene(Vec3f(0.000000133f, -0.000000119f, 0.548799932f)), right_id));
    scene.add_triangle(make_triangle(to_scene(Vec3f(0.000000133f, -0.559199989f, 0.000000040f)),
                                     to_scene(Vec3f(0.000000133f, -0.000000119f, 0.548799932f)),
                                     to_scene(Vec3f(0.000000133f, -0.559199989f, 0.548799932f)), right_id));

    scene.add_triangle(make_triangle(to_scene(Vec3f(0.000000133f, -0.000000119f, 0.548799932f)),
                                     to_scene(Vec3f(0.555999935f, -0.559199989f, 0.548799932f)),
                                     to_scene(Vec3f(0.000000133f, -0.559199989f, 0.548799932f)), top_id));
    scene.add_triangle(make_triangle(to_scene(Vec3f(0.000000133f, -0.000000119f, 0.548799932f)),
                                     to_scene(Vec3f(0.555999935f, -0.000000119f, 0.548799932f)),
                                     to_scene(Vec3f(0.555999935f, -0.559199989f, 0.548799932f)), top_id));

    // Load bunny mesh once
    const std::filesystem::path bunny_path = std::filesystem::path("resources/models/bunny.obj");
    std::vector<Triangle> bunny_mesh = load_obj(bunny_path.string(), bunny_mat_diffuse);

    // Normalize bunny mesh bounds for instancing
    Vec3f min_bounds = Vec3f::Constant(std::numeric_limits<float>::max());
    Vec3f max_bounds = Vec3f::Constant(std::numeric_limits<float>::lowest());
    for (const Triangle& tri : bunny_mesh) {
        for (int i = 0; i < 3; ++i) {
            min_bounds = min_bounds.cwiseMin(tri.v[i]);
            max_bounds = max_bounds.cwiseMax(tri.v[i]);
        }
    }
    const float height = std::max(EPS_SMALL, max_bounds.y() - min_bounds.y());
    const float center_x = 0.5f * (min_bounds.x() + max_bounds.x());
    const float center_z = 0.5f * (min_bounds.z() + max_bounds.z());
    const float min_y = min_bounds.y();

    auto rotate_y = [](const Vec3f& v, float angle) {
        float c = std::cos(angle);
        float s = std::sin(angle);
        return Vec3f(c * v.x() + s * v.z(), v.y(), -s * v.x() + c * v.z());
    };

    auto add_bunny_instance = [&](const Vec3f& translate, float target_height, float yaw, int material_override) {
        float scale = 1.2f * target_height / height; // 20% larger
        const float face_angle = static_cast<float>(M_PI);
        for (Triangle tri : bunny_mesh) {
            for (int i = 0; i < 3; ++i) {
                Vec3f v = tri.v[i];
                v.x() = (v.x() - center_x);
                v.y() = (v.y() - min_y);
                v.z() = (v.z() - center_z);
                v *= scale;
                v = rotate_y(v, face_angle + yaw);
                v += translate;
                tri.v[i] = v;
            }
            tri.material_id = material_override;
            tri.normal = (tri.v1() - tri.v0()).cross(tri.v2() - tri.v0()).normalized();
            scene.add_triangle(tri);
        }
    };

    // Multiple bunnies at varying depths and scales
    add_bunny_instance(Vec3f(0.18f, 1e-3f, 0.20f), 0.14f, 0.1f, bunny_mat_diffuse);  // near
    add_bunny_instance(Vec3f(0.34f, 1e-3f, 0.34f), 0.12f, -0.2f, bunny_mat_gold);    // mid-near
    add_bunny_instance(Vec3f(0.22f, 1e-3f, 0.46f), 0.15f, 0.25f, bunny_mat_copper);  // mid (focus)
    add_bunny_instance(Vec3f(0.42f, 1e-3f, 0.52f), 0.13f, -0.35f, bunny_mat_diffuse); // mid-far
    add_bunny_instance(Vec3f(0.30f, 1e-3f, 0.62f), 0.11f, 0.4f, bunny_mat_gold);     // far

    return scene;
}

} // namespace scenes
