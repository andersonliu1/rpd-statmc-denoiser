#pragma once

#include <limits>
#include <vector>

#include "intersection.h"
#include "material.h"
#include "ray.h"
#include "triangle.h"

struct Scene {
    int add_material(const Material& material) {
        materials.push_back(material);
        return static_cast<int>(materials.size() - 1);
    }

    void add_triangle(const Triangle& triangle) {
        triangles.push_back(triangle);
    }

    bool intersect(const Ray& ray, HitRecord& hit) const {
        bool hit_anything = false;
        float closest = hit.t;

        for (const auto& tri : triangles) {
            auto [did_hit, t] = Triangle::ray_triangle_intersect(tri, ray, EPS, closest);
            if (did_hit && t < closest) {
                closest = t;
                hit.t = t;
                hit.point = ray.at(t);
                hit.normal = tri.normal;
                hit.material = (tri.material_id >= 0 && tri.material_id < static_cast<int>(materials.size()))
                    ? const_cast<Material*>(&materials[tri.material_id])
                    : nullptr;
                hit.hit = true;
                hit_anything = true;
            }
        }

        hit.hit = hit_anything;

        return hit_anything;
    }

    size_t triangle_count() const { return triangles.size(); }
    size_t material_count() const { return materials.size(); }

    std::vector<Triangle> triangles;
    std::vector<Material> materials;
};
