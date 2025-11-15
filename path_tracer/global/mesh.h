#pragma once

#include "common.h"
#include "triangle.h"
#include "material.h"
#include "intersection.h"
#include "ray.h"
#include <vector>
#include <limits>

class Mesh {
public:
    std::vector<Triangle> triangles;
    mutable Material material;

    Mesh() = default;

    void add_triangle(const Triangle& tri) {
        triangles.push_back(tri);
    }

    bool intersect(const Ray& ray, HitRecord& hit) const {
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
