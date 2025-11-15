#pragma once

#include "global/common.h"
#include "global/triangle.h"
#include "global/material.h"
#include "global/intersection.h"
#include "global/ray.h"
#include "objects/scene_object.h"
#include <vector>
#include <limits>

class Mesh : public SceneObject {
public:
    std::vector<Triangle> triangles;
    mutable Material material;

    Mesh() = default;

    void add_triangle(const Triangle& tri) {
        triangles.push_back(tri);
    }

    bool intersect(const Ray& ray, HitRecord& hit) const override {
        bool hit_anything = false;
        float closest = std::numeric_limits<float>::infinity();

        for (const auto& tri : triangles) {
            auto [did_hit, t] = Triangle::ray_triangle_intersect(tri, ray.origin, ray.direction, EPS, closest);

            if (did_hit && t < closest) {
                closest = t;
                hit_anything = true;
                hit.t = t;
                hit.point = ray.at(t);
                hit.normal = tri.normal.normalized();
                hit.material = &material;
            }
        }

        hit.hit = hit_anything;
        return hit_anything;
    }
};
