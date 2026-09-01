#pragma once
#include <cmath>

#include "common.h"
#include "ray.h"
#include "sampler.h"

struct Camera {
    Vec3f position = Vec3f::Zero();
    Vec3f forward_vector = Vec3f::UnitZ();
    Vec3f right_vector = Vec3f::UnitX();
    Vec3f up_vector = Vec3f::UnitY();

    float vertical_fov = 45.0f;
    float aspect_ratio = 1.0f;
    float focus_distance = 1.0f;
    float lens_radius = 0.0f;

    Vec3f lower_left_corner = Vec3f::Zero();
    Vec3f horizontal = Vec3f::Zero();
    Vec3f vertical = Vec3f::Zero();

    /// @brief Initializes the camera basis and thin-lens geometry.
    /// @param eye_pos Camera origin in world space.
    /// @param forward Viewing direction in world space.
    /// @param fov Vertical field of view in degrees.
    /// @param camera_aspect Image width divided by height.
    /// @param focus_dist Distance from the eye to the focal plane.
    /// @param aperture Lens diameter; zero selects a pinhole camera.
    /// @param world_up Preferred upward direction.
    void init(const Vec3f& eye_pos, const Vec3f& forward, const float fov, const float camera_aspect, const float focus_dist = 1.0f, const float aperture = 0.0f, const Vec3f& world_up = Vec3f(0.0f, 1.0f, 0.0f)) {
        position = eye_pos;
        vertical_fov = fov;
        aspect_ratio = camera_aspect;
        focus_distance = focus_dist;
        lens_radius = 0.5f * aperture;

        forward_vector = forward.normalized();
        Vec3f up_hint = world_up.normalized();
        Vec3f right = forward_vector.cross(up_hint);

        if (right.squaredNorm() < EPS_SMALL) {
            Vec3f alt_up = (std::fabs(forward_vector.z()) < 0.999f) ? Vec3f::UnitZ() : Vec3f::UnitY();
            if (alt_up.isApprox(up_hint, EPS_SMALL)) {
                alt_up = Vec3f::UnitX();
            }
            up_hint = alt_up;
            right = forward_vector.cross(up_hint);
        }

        right_vector = right.normalized();
        up_vector = right_vector.cross(forward_vector).normalized();

        update_film_geometry();
    }

    void set_focus_distance(float focus_dist) {
        focus_distance = focus_dist;
        update_film_geometry();
    }

    void set_aperture(float aperture) {
        lens_radius = 0.5f * aperture;
    }

    Ray generate_ray(const float u, const float v, const Vec2f sample) const {
        Vec3f origin = position;

        if (lens_radius > 0.0f) {
            Vec2f lens_offset = lens_radius * sample;
            origin += lens_offset.x() * right_vector + lens_offset.y() * up_vector;
        }

        Vec3f pixel = lower_left_corner + u * horizontal + v * vertical;
        Vec3f direction = (pixel - origin).normalized();
        return Ray(origin, direction);
    }

private:
    void update_film_geometry() {
        float theta = vertical_fov * M_PI / 180.0f;
        float half_height = std::tan(0.5f * theta);
        float half_width = aspect_ratio * half_height;

        horizontal = 2.0f * focus_distance * half_width * right_vector;
        vertical = 2.0f * focus_distance * half_height * up_vector;
        lower_left_corner = position + forward_vector * focus_distance - 0.5f * horizontal - 0.5f * vertical;
    }
};
