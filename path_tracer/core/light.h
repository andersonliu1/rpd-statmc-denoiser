#pragma once

#include <algorithm>
#include <variant>
#include <limits>
#include <cmath>

#include "common.h"
#include "triangle.h"

struct LightSample {
    Vec3f wi_world = Vec3f::Zero();
    float light_dist = 0.0f;
};

struct RectLight {
    Vec3f position, width, length, normal, emission;
    float area;

    RectLight() = default;
    RectLight(const Vec3f& position, const Vec3f& width, const Vec3f& length, const Vec3f& emission) : position(position), width(width), length(length), emission(emission) {
        Vec3f cross_uv = width.cross(length);
        area = cross_uv.norm();
        normal = cross_uv / area;
    }

    Vec3f eval(const Vec3f& light_dir) const {
        return (light_dir.dot(normal) > 0.0f) ? emission : Vec3f::Zero();
    }

    LightSample sample(const Vec3f& p, const Vec2f u) const {
        LightSample sample{};

        Vec3f sample_pos = position + u.x() * width + u.y() * length;
        Vec3f point_to_sample = sample_pos - p;
        float sqr_dist = point_to_sample.squaredNorm();
        if (sqr_dist <= EPS_SMALL) return sample;

        point_to_sample.normalize();

        float cos_light = normal.dot(-point_to_sample);
        if (cos_light <= EPS_SMALL) return sample;

        sample.wi_world = point_to_sample;
        sample.light_dist = sqrt(sqr_dist);
        return sample;
    }

    float power() const {
        return area * std::max(0.0f, calc_luminance(emission));
    }

    float pdf(const Vec3f& p, const Vec3f& dir, float dist) const {
        float cos_light = normal.dot(-dir);
        if (cos_light <= EPS_SMALL) return 0.0f;
        return (dist * dist) / (cos_light * area);
    }
};

struct AreaLight {
    Triangle triangle;
    float area = 0.0f;

    AreaLight() = default;
    explicit AreaLight(const Triangle& tri) : triangle(tri), area(tri.area()) {}

    Vec3f eval(const Vec3f& light_dir) const {
        return (light_dir.dot(triangle.normal) > 0.0f) ? triangle.emission : Vec3f::Zero();
    }

    Vec3f sample_point(const Vec2f u) const {
        float su = std::sqrt(std::max(0.0f, u.x()));
        float b0 = 1.0f - su;
        float b1 = u.y() * su;
        float b2 = 1.0f - b0 - b1;
        return b0 * triangle.v0() + b1 * triangle.v1() + b2 * triangle.v2();
    }

    LightSample sample(const Vec3f& p, const Vec2f u) const {
        LightSample sample{};

        Vec3f sample_pos = sample_point(u);
        Vec3f point_to_sample = sample_pos - p;
        float sqr_dist = point_to_sample.squaredNorm();
        if (sqr_dist <= EPS_SMALL) return sample;

        point_to_sample.normalize();

        float cos_light = triangle.normal.dot(-point_to_sample);
        if (cos_light <= EPS_SMALL) return sample;

        sample.wi_world = point_to_sample;
        sample.light_dist = sqrt(sqr_dist);
        return sample;
    }

    float power() const {
        return area * std::max(0.0f, calc_luminance(triangle.emission));
    }

    float pdf(const Vec3f& p, const Vec3f& dir, float dist) const {
        float cos_light = triangle.normal.dot(-dir);
        if (cos_light <= EPS_SMALL) return 0.0f;
        return (dist * dist) / (cos_light * area);
    }
};

struct PointLight {
    Vec3f position, intensity;

    LightSample sample(const Vec3f& p) const {
        LightSample sample{};

        Vec3f point_to_light = position - p;
        float sqr_dist = point_to_light.squaredNorm();
        if (sqr_dist <= EPS_SMALL) return sample;

        point_to_light.normalize();
        
        sample.wi_world = point_to_light;
        sample.light_dist = sqrt(sqr_dist);
        return sample;
    }

    float power() const {
        return std::max(0.0f, calc_luminance(intensity));
    }

};

struct DirectionalLight {
    Vec3f direction, intensity;

    LightSample sample(const Vec3f& x) const {
        return LightSample{.wi_world = direction, .light_dist = std::numeric_limits<float>::infinity()};
    }

    float power() const {
        return std::max(0.0f, calc_luminance(intensity));
    }
};

using Light = std::variant<RectLight, AreaLight, PointLight, DirectionalLight>;

inline Vec3f eval_area_light(const Light& light, const Vec3f& light_dir) {
    return std::visit([&](const auto& light) -> Vec3f {
            using T = std::decay_t<decltype(light)>;
            if constexpr (std::is_same_v<T, RectLight> || std::is_same_v<T, AreaLight>) return light.eval(light_dir);
            return Vec3f::Zero();
        }, light);
}

inline float calc_light_pdf(const Light& light, const Vec3f& p, const Vec3f& dir, float distance) {
    return std::visit([&](const auto& light_obj) -> float {
        using T = std::decay_t<decltype(light_obj)>;
        if constexpr (std::is_same_v<T, RectLight> || std::is_same_v<T, AreaLight>) {
            return light_obj.pdf(p, dir, distance);
        }

        return 0.0f;
    }, light);
}

inline bool is_delta_light(const Light& light) {
    return std::holds_alternative<PointLight>(light) || std::holds_alternative<DirectionalLight>(light);
}
