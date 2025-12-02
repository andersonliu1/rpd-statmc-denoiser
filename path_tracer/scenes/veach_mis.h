#pragma once

#include <array>
#include <cmath>

#include "scenes/scene_utils.h"
#include "scenes/material_library.h"
#include "core/sphere.h"

namespace scenes {

// Classic Veach Multiple Importance Sampling test scene
inline Scene make_veach_mis() {
    Scene scene;
    scene.environment_color = Vec3f(0.02f, 0.02f, 0.02f);

    // Create materials with varying roughness
    Microfacet glossy_smooth = {
        .albedo = Vec3f(0.9f, 0.9f, 0.9f),
        .roughness = 0.001f,
        .n1 = Vec3f::Ones(),
        .n2 = Vec3f(1.5f, 1.5f, 1.5f),
        .distribution = Microfacet::Distribution::GGX
    };

    Microfacet glossy_medium1 = {
        .albedo = Vec3f(0.9f, 0.9f, 0.9f),
        .roughness = 0.1f,
        .n1 = Vec3f::Ones(),
        .n2 = Vec3f(1.5f, 1.5f, 1.5f),
        .distribution = Microfacet::Distribution::GGX
    };

    Microfacet glossy_medium2 = {
        .albedo = Vec3f(0.9f, 0.9f, 0.9f),
        .roughness = 0.3f,
        .n1 = Vec3f::Ones(),
        .n2 = Vec3f(1.5f, 1.5f, 1.5f),
        .distribution = Microfacet::Distribution::GGX
    };

    Microfacet glossy_rough = {
        .albedo = Vec3f(0.9f, 0.9f, 0.9f),
        .roughness = 0.7f,
        .n1 = Vec3f::Ones(),
        .n2 = Vec3f(1.5f, 1.5f, 1.5f),
        .distribution = Microfacet::Distribution::GGX
    };

    const int mat_smooth_id = scene.add_material(Material{glossy_smooth});
    const int mat_medium1_id = scene.add_material(Material{glossy_medium1});
    const int mat_medium2_id = scene.add_material(Material{glossy_medium2});
    const int mat_rough_id = scene.add_material(Material{glossy_rough});
    const int ground_id = scene.add_material(Material{Lambertian{Vec3f(0.3f, 0.3f, 0.3f)}});
    const int light_mat_id = scene.add_material(Material{Lambertian{Vec3f(1.0f, 1.0f, 1.0f)}});

    // Add ground plane
    auto ground = make_ground_plane(20.0f, 0.0f, ground_id);
    for (const auto& tri : ground) {
        scene.add_triangle(tri);
    }

    // Add 4 spheres with increasing roughness
    const float sphere_radius = 0.5f;
    const float sphere_y = sphere_radius;
    const float spacing = 2.0f;
    const float start_x = -3.0f;
    const float sphere_z = 0.0f;

    std::array<int, 4> sphere_materials = {mat_smooth_id, mat_medium1_id, mat_medium2_id, mat_rough_id};

    for (int i = 0; i < 4; ++i) {
        Sphere sphere;
        sphere.center = Vec3f(start_x + i * spacing, sphere_y, sphere_z);
        sphere.radius = sphere_radius;
        sphere.material_id = sphere_materials[i];
        sphere.emission = Vec3f::Zero();
        scene.add_sphere(sphere);
    }

    // Add 4 rectangular area lights with varying sizes
    const float lights_y = 3.0f;
    const float lights_z = -2.0f;
    std::array<float, 4> light_sizes = {1.2f, 0.6f, 0.3f, 0.15f};
    const float base_power = 40.0f;

    for (int i = 0; i < 4; ++i) {
        float x = start_x + i * spacing;
        float size = light_sizes[i];
        float area = size * size;
        float intensity = base_power / area;
        Vec3f light_color(intensity, intensity, intensity);

        Vec3f light_origin(x - size * 0.5f, lights_y, lights_z - size * 0.5f);
        Vec3f light_u(size, 0.0f, 0.0f);
        Vec3f light_v(0.0f, 0.0f, size);

        int light_idx = scene.add_light(make_rect_light(light_origin, light_u, light_v, light_color));

        Vec3f v0 = light_origin;
        Vec3f v1 = light_origin + light_u;
        Vec3f v2 = light_origin + light_u + light_v;
        Vec3f v3 = light_origin + light_v;

        int tri1 = scene.add_triangle(make_triangle(v0, v1, v2, light_mat_id, light_color));
        int tri2 = scene.add_triangle(make_triangle(v0, v2, v3, light_mat_id, light_color));

        scene.link_triangle_to_light(tri1, light_idx);
        scene.link_triangle_to_light(tri2, light_idx);
    }

    return scene;
}

} // namespace scenes
