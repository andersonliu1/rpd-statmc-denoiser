#pragma once

#include "global/scene.h"

namespace scenes {

inline Triangle make_triangle(const Vec3f& v0,
                              const Vec3f& v1,
                              const Vec3f& v2,
                              int material_id,
                              const Vec3f& emission = Vec3f::Zero()) {
    Triangle tri;
    tri.v0() = v0;
    tri.v1() = v1;
    tri.v2() = v2;
    tri.material_id = material_id;
    tri.emission = emission;

    Vec3f normal = (v1 - v0).cross(v2 - v0);
    if (normal.squaredNorm() > 0.0f) {
        tri.normal = normal.normalized();
    } else {
        tri.normal = Vec3f::UnitY();
    }

    return tri;
}

} // namespace scenes
