#pragma once

#include <array>

#include "scenes/scene_utils.h"

namespace scenes {

inline Scene make_cornell_box() {
    Scene scene;
    scene.environment_color = Vec3f::Zero();

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

    constexpr float room_width = 0.556f;
    constexpr float room_height = 0.5488f;
    constexpr float room_depth = 0.5592f;

    const Vec3f p000(0.0f, 0.0f, 0.0f);
    const Vec3f p100(room_width, 0.0f, 0.0f);
    const Vec3f p010(0.0f, room_height, 0.0f);
    const Vec3f p110(room_width, room_height, 0.0f);
    const Vec3f p001(0.0f, 0.0f, room_depth);
    const Vec3f p101(room_width, 0.0f, room_depth);
    const Vec3f p011(0.0f, room_height, room_depth);
    const Vec3f p111(room_width, room_height, room_depth);

    constexpr float light_width = 0.2f;
    constexpr float light_depth = 0.16f;
    const Vec3f light_emission(70.0f, 70.0f, 70.0f);
    const Vec3f light_origin(
        0.5f * (room_width - light_width),
        room_height - 1e-3f,
        0.5f * (room_depth - light_depth)
    );
    const Vec3f light_u(light_width, 0.0f, 0.0f);
    const Vec3f light_v(0.0f, 0.0f, light_depth);

    int light_idx = scene.add_light(make_rect_light(light_origin, light_u, light_v, light_emission));

    const Vec3f l0 = light_origin;
    const Vec3f l1 = light_origin + light_u;
    const Vec3f l2 = light_origin + light_u + light_v;
    const Vec3f l3 = light_origin + light_v;

    int light_tri_1 = scene.add_triangle(make_triangle(l0, l1, l2, light_id, light_emission));
    int light_tri_2 = scene.add_triangle(make_triangle(l0, l2, l3, light_id, light_emission));
    scene.link_triangle_to_light(light_tri_1, light_idx);
    scene.link_triangle_to_light(light_tri_2, light_idx);

    // Back wall (z = room_depth)
    scene.add_triangle(make_triangle(p001, p111, p101, back_id));
    scene.add_triangle(make_triangle(p001, p011, p111, back_id));

    // Floor (y = 0)
    scene.add_triangle(make_triangle(p000, p101, p100, bottom_id));
    scene.add_triangle(make_triangle(p000, p001, p101, bottom_id));

    // Left wall (x = 0)
    scene.add_triangle(make_triangle(p000, p011, p001, left_id));
    scene.add_triangle(make_triangle(p000, p010, p011, left_id));

    // Right wall (x = room_width)
    scene.add_triangle(make_triangle(p100, p101, p111, right_id));
    scene.add_triangle(make_triangle(p100, p111, p110, right_id));

    // Ceiling (y = room_height)
    scene.add_triangle(make_triangle(p010, p110, p111, top_id));
    scene.add_triangle(make_triangle(p010, p111, p011, top_id));

    return scene;
}

} // namespace scenes
