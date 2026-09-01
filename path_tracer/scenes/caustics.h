#pragma once

#include <array>
#include <cmath>

#include "scenes/scene_utils.h"
#include "scenes/material_library.h"
#include "core/sphere.h"

namespace scenes {

inline Scene make_caustics() {
    Scene scene;
    scene.environment_color = Vec3f(0.01f, 0.01f, 0.01f);

    const int mirror_id = scene.add_material(Material{MirrorWhite});
    const int ground_id = scene.add_material(Material{Lambertian{Vec3f(0.7f, 0.7f, 0.7f)}});
    const int wall_id = scene.add_material(Material{Lambertian{Vec3f(0.6f, 0.6f, 0.65f)}});
    const int accent_diffuse_id = scene.add_material(Material{Lambertian{Vec3f(0.2f, 0.3f, 0.5f)}});
    const int light_mat_id = scene.add_material(Material{Lambertian{Vec3f(1.0f, 1.0f, 1.0f)}});

    auto ground = make_ground_plane(25.0f, 0.0f, ground_id);
    for (const auto& tri : ground) {
        scene.add_triangle(tri);
    }

    Vec3f wall_v0(-8.0f, 0.0f, 6.0f);
    Vec3f wall_v1(8.0f, 0.0f, 6.0f);
    Vec3f wall_v2(8.0f, 8.0f, 6.0f);
    Vec3f wall_v3(-8.0f, 8.0f, 6.0f);

    scene.add_triangle(make_triangle(wall_v0, wall_v1, wall_v2, wall_id));
    scene.add_triangle(make_triangle(wall_v0, wall_v2, wall_v3, wall_id));

    Vec3f left_wall_v0(-8.0f, 0.0f, -5.0f);
    Vec3f left_wall_v1(-8.0f, 0.0f, 6.0f);
    Vec3f left_wall_v2(-8.0f, 8.0f, 6.0f);
    Vec3f left_wall_v3(-8.0f, 8.0f, -5.0f);

    scene.add_triangle(make_triangle(left_wall_v0, left_wall_v1, left_wall_v2, wall_id));
    scene.add_triangle(make_triangle(left_wall_v0, left_wall_v2, left_wall_v3, wall_id));

    Vec3f right_wall_v0(8.0f, 0.0f, 6.0f);
    Vec3f right_wall_v1(8.0f, 0.0f, -5.0f);
    Vec3f right_wall_v2(8.0f, 8.0f, -5.0f);
    Vec3f right_wall_v3(8.0f, 8.0f, 6.0f);

    scene.add_triangle(make_triangle(right_wall_v0, right_wall_v1, right_wall_v2, wall_id));
    scene.add_triangle(make_triangle(right_wall_v0, right_wall_v2, right_wall_v3, wall_id));

    Vec3f ceiling_v0(-8.0f, 8.0f, -5.0f);
    Vec3f ceiling_v1(8.0f, 8.0f, -5.0f);
    Vec3f ceiling_v2(8.0f, 8.0f, 6.0f);
    Vec3f ceiling_v3(-8.0f, 8.0f, 6.0f);

    scene.add_triangle(make_triangle(ceiling_v0, ceiling_v1, ceiling_v2, wall_id));
    scene.add_triangle(make_triangle(ceiling_v0, ceiling_v2, ceiling_v3, wall_id));

    Sphere sphere1;
    sphere1.center = Vec3f(0.0f, 1.2f, 0.0f);
    sphere1.radius = 1.2f;
    sphere1.material_id = mirror_id;
    sphere1.emission = Vec3f::Zero();
    scene.add_sphere(sphere1);

    Sphere sphere2;
    sphere2.center = Vec3f(-2.5f, 0.7f, -1.5f);
    sphere2.radius = 0.7f;
    sphere2.material_id = mirror_id;
    sphere2.emission = Vec3f::Zero();
    scene.add_sphere(sphere2);

    Sphere sphere3;
    sphere3.center = Vec3f(2.5f, 0.6f, -1.0f);
    sphere3.radius = 0.6f;
    sphere3.material_id = mirror_id;
    sphere3.emission = Vec3f::Zero();
    scene.add_sphere(sphere3);

    Sphere diffuse_sphere;
    diffuse_sphere.center = Vec3f(-1.5f, 0.35f, 2.0f);
    diffuse_sphere.radius = 0.35f;
    diffuse_sphere.material_id = accent_diffuse_id;
    diffuse_sphere.emission = Vec3f::Zero();
    scene.add_sphere(diffuse_sphere);

    PointLight main_light;
    main_light.position = Vec3f(-3.0f, 6.0f, -2.0f);
    main_light.intensity = Vec3f(200.0f, 200.0f, 200.0f);
    scene.add_light(main_light);

    PointLight fill_light;
    fill_light.position = Vec3f(3.5f, 5.0f, -1.0f);
    fill_light.intensity = Vec3f(80.0f, 80.0f, 80.0f);
    scene.add_light(fill_light);

    Vec3f top_light_origin(-0.8f, 7.5f, -0.8f);
    Vec3f top_light_u(1.6f, 0.0f, 0.0f);
    Vec3f top_light_v(0.0f, 0.0f, 1.6f);
    Vec3f top_light_color(40.0f, 40.0f, 40.0f);

    int top_light_idx = scene.add_light(make_rect_light(top_light_origin, top_light_u, top_light_v, top_light_color));

    int top_tri1 = scene.add_triangle(make_triangle(
        top_light_origin,
        top_light_origin + top_light_u,
        top_light_origin + top_light_u + top_light_v,
        light_mat_id, top_light_color));
    int top_tri2 = scene.add_triangle(make_triangle(
        top_light_origin,
        top_light_origin + top_light_u + top_light_v,
        top_light_origin + top_light_v,
        light_mat_id, top_light_color));
    scene.link_triangle_to_light(top_tri1, top_light_idx);
    scene.link_triangle_to_light(top_tri2, top_light_idx);

    return scene;
}

}
