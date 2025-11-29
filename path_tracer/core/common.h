#pragma once
#include "shared/global.h"

#define EPS 1e-4f
#define EPS_ANYHIT 5e-4f

#define M_1_2PI 0.159154943091895335769 // 1/2 pi

/// @brief Orthonormal Basis: https://graphics.pixar.com/library/OrthonormalB/paper.pdf
/// @param v 
/// @return 
inline std::tuple<Vec3f, Vec3f> coordinate_system(const Vec3f& v){
    float sign = copysignf(1.0f, v.z());
    const float a = -1.0f / (sign + v.z());
    const float b = v.x() * v.y() * a;

    return {
        Vec3f(1.0f + sign * v.x() * v.x() * a, sign * b, -sign * v.x()),
        Vec3f(b, sign + v.y() * v.y() * a, -v.y())
    };
}

inline Vec3f local_to_world(const Vec3f& v, const Vec3f& n){
    auto [x, y] = coordinate_system(n);
    return v.x() * x + v.y() * y + v.z() * n;
}

inline Vec3f world_to_local(const Vec3f& v, const Vec3f& n){
    auto [x, y] = coordinate_system(n);
    return Vec3f(v.dot(x), v.dot(y), v.dot(n));
}

inline Vec3f offset_ray_origin(const Vec3f& ray_pos, const Vec3f& normal) {
    return ray_pos + EPS * normal;
}

// https://en.wikipedia.org/wiki/Relative_luminance
inline float calc_luminance(const Vec3f& c) {
    return 0.2126f * c.x() + 0.7152f * c.y() + 0.0722f * c.z();
}

template<class T>
inline T lerp(T a, T b, float t){
    return a + (b - a) * t;
}
