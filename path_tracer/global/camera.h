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

    /// Initializes the camera basis and thin-lens parameters.
    /// @param eye_position Camera origin in world space.
    /// @param target Point in world space the camera looks toward.
    /// @param world_up_hint Hint for the upward direction (should not be parallel to the view direction).
    /// @param vfovVertical field of view in degrees.
    /// @param camera_aspect_ratio Image aspect ratio (width / height).
    /// @param focus_dist Distance from the eye to the focal plane.
    /// @param aperture Diameter of the lens opening (0 for pinhole).
    void init(const Vec3f& eye_pos,
              const Vec3f& target,
              const Vec3f& world_up_hint,
              float vfov,
              float camera_aspect_ratio,
              float focus_dist = 1.0f,
              float aperture = 0.0f) {
        position = eye_pos;
        vertical_fov = vfov;
        aspect_ratio = camera_aspect_ratio;
        focus_distance = focus_dist;
        lens_radius = 0.5f * aperture;

        forward_vector = (target - position).normalized();
        right_vector = forward_vector.cross(world_up_hint).normalized();
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

    Ray generate_ray(float u, float v) const {
        Vec3f origin = position;

        if (lens_radius > 0.0f) {
            Vec2f sample = Sampler::sample_disk();
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
