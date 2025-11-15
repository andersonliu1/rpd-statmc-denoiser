#pragma once

#include "common.h"
#include "ray.h"
#include <cmath>

class Camera {
public:
    Vec3f position;
    Vec3f forward_vector;
    Vec3f right_vector;
    Vec3f up_vector;
    float vertical_fov;
    float aspect_ratio;

    Camera() = default;
    void initialize(const Vec3f& pos, const Vec3f& target_point,
                   const Vec3f& up_hint, float vfov, float aspect) {
        position = pos;
        vertical_fov = vfov;
        aspect_ratio = aspect;

        forward_vector = (target_point - position).normalized();
        right_vector = forward_vector.cross(up_hint).normalized();
        up_vector = right_vector.cross(forward_vector);
    }

    Ray generate_ray(float u, float v) const {
        float vfov_rad = vertical_fov * M_PI / 180.0f;
        float half_height = std::tan(vfov_rad / 2.0f);
        float half_width = half_height * aspect_ratio;

        Vec3f direction = forward_vector
                        + (2.0f * u - 1.0f) * half_width * right_vector
                        + (1.0f - 2.0f * v) * half_height * up_vector;

        return Ray(position, direction.normalized());
    }
};
