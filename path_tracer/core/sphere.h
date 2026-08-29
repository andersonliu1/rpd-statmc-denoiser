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
};
