#pragma once

#include "common.h"
#include "ray.h"
#include "intersection.h"

struct Material {
    Vec3f albedo;
    float roughness;

    Material() : albedo(0.8f, 0.8f, 0.8f), roughness(0.5f) {}
    Material(const Vec3f& albedo, float roughness = 0.5f)
        : albedo(albedo), roughness(roughness) {}

    Vec3f evaluate(const HitRecord& hit, const Vec3f& wo, const Vec3f& wi) const {
        return albedo / M_PI;
    }
};
