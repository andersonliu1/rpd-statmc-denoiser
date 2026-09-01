#pragma once

#include "core/material.h"

namespace scenes {

inline const Microfacet Gold {
        .albedo = Vec3f(1.0f, 0.71f, 0.29f),
        .roughness = 0.0005f,
        .n1 = Vec3f::Ones(),
        .n2 = Vec3f(0.17f, 0.35f, 1.5f),
        .distribution = Microfacet::Distribution::GGX
    };

    inline const Microfacet Jade {
            .albedo = Vec3f(0.35f, 0.8f, 0.55f),
            .roughness = 0.05f,
            .n1 = Vec3f::Ones(),
            .n2 = Vec3f(1.55f, 1.55f, 1.55f),
            .distribution = Microfacet::Distribution::GGX
        };

    inline const Mirror MirrorWhite {
            .albedo = Vec3f(0.95f, 0.95f, 0.95f)
    };

    inline const Microfacet Copper {
            .albedo = Vec3f(0.95f, 0.64f, 0.54f),
            .roughness = 0.1f,
            .n1 = Vec3f::Ones(),
            .n2 = Vec3f(0.27f, 0.67f, 1.38f),
            .distribution = Microfacet::Distribution::GGX
    };

    inline const Microfacet Aluminum {
            .albedo = Vec3f(0.91f, 0.92f, 0.92f),
            .roughness = 0.05f,
            .n1 = Vec3f::Ones(),
            .n2 = Vec3f(1.44f, 0.96f, 0.62f),
            .distribution = Microfacet::Distribution::GGX
    };

    inline const Microfacet RoughGold {
            .albedo = Vec3f(1.0f, 0.71f, 0.29f),
            .roughness = 0.3f,
            .n1 = Vec3f::Ones(),
            .n2 = Vec3f(0.17f, 0.35f, 1.5f),
            .distribution = Microfacet::Distribution::GGX
    };

    inline const Microfacet Silver {
            .albedo = Vec3f(0.97f, 0.96f, 0.91f),
            .roughness = 0.02f,
            .n1 = Vec3f::Ones(),
            .n2 = Vec3f(0.16f, 0.14f, 0.13f),
            .distribution = Microfacet::Distribution::GGX
    };

}
