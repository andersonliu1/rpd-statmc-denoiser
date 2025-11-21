#pragma once

#include <algorithm>
#include <limits>
#include <vector>

#include "light.h"
#include "material.h"
#include "ray.h"
#include "triangle.h"

struct Scene {
    int add_material(const Material& material) {
        materials.push_back(material);
        return static_cast<int>(materials.size() - 1);
    }

    int add_triangle(const Triangle& triangle) {
        triangles.push_back(triangle);
        triangle_to_light.push_back(-1);
        return static_cast<int>(triangles.size() - 1);
    }

    int add_light(const Light& light) {
        lights.push_back(light);
        light_powers.push_back(std::visit([](const auto& l) { return l.power(); }, light));
        light_cdf.push_back((light_cdf.empty()) ? light_powers.back() : light_cdf.back());
        return static_cast<int>(lights.size() - 1);
    }

    void link_triangle_to_light(int triangle_index, int light_index) {
        triangle_to_light[triangle_index] = light_index;
    }

    float light_select_pdf(int light_idx) const {
        return light_powers[light_idx] / light_cdf.back();
    }

    std::tuple<int, float> sample_light(float u) const {
        if (lights.empty()) return {-1, 0.0f};
        float total = light_cdf.back();
        size_t idx = std::distance(light_cdf.begin(), std::lower_bound(light_cdf.begin(), light_cdf.end(), u * total));
        if (idx >= lights.size()) idx = lights.size() - 1;
        float select_pdf = light_select_pdf(static_cast<int>(idx));
        return {static_cast<int>(idx), select_pdf};
    }

    float light_pdf(int light_idx, float select_pdf, const Vec3f& p, const Vec3f& dir, float distance) const {
        return select_pdf * calc_light_pdf(lights[light_idx], p, dir, distance);
    }

    Vec3f environment_color = Vec3f::Zero();

    size_t triangle_count() const { return triangles.size(); }
    size_t material_count() const { return materials.size(); }

    std::vector<Triangle> triangles;
    std::vector<Material> materials;
    std::vector<Light> lights;
    std::vector<int> triangle_to_light;
    std::vector<float> light_cdf;
    std::vector<float> light_powers;
};
