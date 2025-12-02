#pragma once

#include <array>
#include <cmath>

#include "scenes/scene_utils.h"
#include "scenes/material_library.h"
#include "core/sphere.h"

namespace scenes {

// Material showcase scene
inline Scene make_material_showcase() {
    Scene scene;
    scene.environment_color = Vec3f(0.05f, 0.05f, 0.05f);

    // Define materials
    Lambertian red_diffuse{Vec3f(0.8f, 0.1f, 0.1f)};
    Lambertian green_diffuse{Vec3f(0.1f, 0.8f, 0.1f)};
    Lambertian blue_diffuse{Vec3f(0.1f, 0.1f, 0.8f)};

    Microfacet copper_smooth = Copper;
    copper_smooth.roughness = 0.05f;

    Microfacet gold_medium = Gold;
    gold_medium.roughness = 0.15f;

    Microfacet aluminum_rough = Aluminum;
    aluminum_rough.roughness = 0.4f;

    Mirror mirror_mat = MirrorWhite;
    Microfacet jade_glossy = Jade;
    Microfacet silver_mirror = Silver;

    // Register materials
    const int red_id = scene.add_material(Material{red_diffuse});
    const int green_id = scene.add_material(Material{green_diffuse});
    const int blue_id = scene.add_material(Material{blue_diffuse});
    const int copper_id = scene.add_material(Material{copper_smooth});
    const int gold_id = scene.add_material(Material{gold_medium});
    const int aluminum_id = scene.add_material(Material{aluminum_rough});
    const int mirror_id = scene.add_material(Material{mirror_mat});
    const int jade_id = scene.add_material(Material{jade_glossy});
    const int silver_id = scene.add_material(Material{silver_mirror});

    const int ground_id = scene.add_material(Material{Lambertian{Vec3f(0.4f, 0.4f, 0.4f)}});
    const int back_wall_id = scene.add_material(Material{Lambertian{Vec3f(0.6f, 0.6f, 0.65f)}});
    const int light_mat_id = scene.add_material(Material{Lambertian{Vec3f(1.0f, 1.0f, 1.0f)}});

    // Add ground plane
    auto ground = make_ground_plane(30.0f, 0.0f, ground_id);
    for (const auto& tri : ground) {
        scene.add_triangle(tri);
    }

    // Add back wall
    Vec3f wall_v0(-10.0f, 0.0f, 5.0f);
    Vec3f wall_v1(10.0f, 0.0f, 5.0f);
    Vec3f wall_v2(10.0f, 8.0f, 5.0f);
    Vec3f wall_v3(-10.0f, 8.0f, 5.0f);

    scene.add_triangle(make_triangle(wall_v0, wall_v1, wall_v2, back_wall_id));
    scene.add_triangle(make_triangle(wall_v0, wall_v2, wall_v3, back_wall_id));

    // Arrange 9 spheres in a 3x3 grid
    const float sphere_radius = 0.6f;
    const float sphere_y = sphere_radius;

    std::array<std::array<int, 3>, 3> material_grid = {{
        {red_id, green_id, blue_id},
        {copper_id, gold_id, aluminum_id},
        {mirror_id, jade_id, silver_id}
    }};

    const float x_spacing = 2.5f;
    const float z_spacing = 2.2f;
    const float start_x = -2.5f;
    const float start_z = -2.0f;

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            Sphere sphere;
            sphere.center = Vec3f(start_x + col * x_spacing, sphere_y, start_z + row * z_spacing);
            sphere.radius = sphere_radius;
            sphere.material_id = material_grid[row][col];
            sphere.emission = Vec3f::Zero();
            scene.add_sphere(sphere);
        }
    }

    // Studio lighting: key light + fill light + rim light

    // Key light
    Vec3f key_origin(-3.0f, 4.0f, -2.0f);
    Vec3f key_u(1.5f, 0.0f, 0.0f);
    Vec3f key_v(0.0f, 0.0f, 1.5f);
    Vec3f key_color(50.0f, 50.0f, 50.0f);

    int key_light_idx = scene.add_light(make_rect_light(key_origin, key_u, key_v, key_color));

    int key_tri1 = scene.add_triangle(make_triangle(
        key_origin,
        key_origin + key_u,
        key_origin + key_u + key_v,
        light_mat_id, key_color));
    int key_tri2 = scene.add_triangle(make_triangle(
        key_origin,
        key_origin + key_u + key_v,
        key_origin + key_v,
        light_mat_id, key_color));
    scene.link_triangle_to_light(key_tri1, key_light_idx);
    scene.link_triangle_to_light(key_tri2, key_light_idx);

    // Fill light
    Vec3f fill_origin(3.0f, 3.5f, -1.0f);
    Vec3f fill_u(0.8f, 0.0f, 0.0f);
    Vec3f fill_v(0.0f, 0.0f, 0.8f);
    Vec3f fill_color(20.0f, 20.0f, 20.0f);

    int fill_light_idx = scene.add_light(make_rect_light(fill_origin, fill_u, fill_v, fill_color));

    int fill_tri1 = scene.add_triangle(make_triangle(
        fill_origin,
        fill_origin + fill_u,
        fill_origin + fill_u + fill_v,
        light_mat_id, fill_color));
    int fill_tri2 = scene.add_triangle(make_triangle(
        fill_origin,
        fill_origin + fill_u + fill_v,
        fill_origin + fill_v,
        light_mat_id, fill_color));
    scene.link_triangle_to_light(fill_tri1, fill_light_idx);
    scene.link_triangle_to_light(fill_tri2, fill_light_idx);

    // Rim light (point light)
    PointLight rim_light;
    rim_light.position = Vec3f(0.0f, 3.0f, 4.0f);
    rim_light.intensity = Vec3f(15.0f, 15.0f, 15.0f);
    scene.add_light(rim_light);

    return scene;
}

} // namespace scenes
