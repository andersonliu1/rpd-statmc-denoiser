#pragma once

#include "global/common.h"
#include "global/intersection.h"
#include "global/material.h"
#include "global/ray.h"
#include "objects/scene_object.h"
#include <cmath>

class Sphere : public SceneObject {
public:
    Sphere() : center(Vec3f::Zero()), radius(1.0f) {}
    Sphere(const Vec3f& c, float r) : center(c), radius(r) {}

    Vec3f center;
    float radius;
    mutable Material material;

    bool intersect(const Ray& ray, HitRecord& hit) const override {
        Vec3f oc = ray.origin - center;
        float a = ray.direction.dot(ray.direction);
        float half_b = oc.dot(ray.direction);
        float c = oc.dot(oc) - radius * radius;
        float discriminant = half_b * half_b - a * c;

        if (discriminant < 0.0f) {
            hit.hit = false;
            return false;
        }

        float sqrt_d = std::sqrt(discriminant);
        float root = (-half_b - sqrt_d) / a;
        if (root < EPS) {
            root = (-half_b + sqrt_d) / a;
            if (root < EPS) {
                hit.hit = false;
                return false;
            }
        }

        hit.t = root;
        hit.point = ray.at(root);
        hit.normal = (hit.point - center).normalized();
        hit.material = &material;
        hit.hit = true;
        return true;
    }
};
