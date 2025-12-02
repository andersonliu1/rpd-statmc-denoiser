#pragma once
#include "common.h"
#include "ray.h"
#include <tuple>
#include <cmath>

struct Sphere
{
    Vec3f center;
    float radius;
    Vec3f emission;
    int material_id;

    inline bool is_emitter() const
    {
        return !emission.isZero(EPS_SMALL);
    }

    inline float area() const
    {
        return 4.0f * M_PI * radius * radius;
    }
    static std::tuple<bool, float, Vec3f> ray_sphere_intersect(
        const Sphere &sphere,
        const Ray &ray,
        float t_min,
        float t_max)
    {

        Vec3f oc = ray.origin - sphere.center;
        float a = ray.direction.squaredNorm();
        float half_b = oc.dot(ray.direction);
        float c = oc.squaredNorm() - sphere.radius * sphere.radius;

        float discriminant = half_b * half_b - a * c;

        if (discriminant < 0.0f)
        {
            return {false, 0.0f, Vec3f::Zero()};
        }

        float sqrt_d = std::sqrt(discriminant);

        float t = (-half_b - sqrt_d) / a;
        if (t < t_min || t > t_max)
        {
            t = (-half_b + sqrt_d) / a;
            if (t < t_min || t > t_max)
            {
                return {false, 0.0f, Vec3f::Zero()};
            }
        }

        Vec3f hit_point = ray.at(t);
        Vec3f normal = (hit_point - sphere.center).normalized();

        return {true, t, normal};
    }

    /// @brief Sample a random point on the sphere surface
    /// @param u1 uniform random [0,1)
    /// @param u2 uniform random [0,1)
    /// @return (position, normal, pdf)
    std::tuple<Vec3f, Vec3f, float> sample_surface(float u1, float u2) const
    {
        float z = 1.0f - 2.0f * u1;
        float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        float phi = 2.0f * M_PI * u2;

        Vec3f local_dir(r * std::cos(phi), r * std::sin(phi), z);
        Vec3f position = center + radius * local_dir;
        Vec3f normal = local_dir;

        float pdf = 1.0f / area();

        return {position, normal, pdf};
    }
};
