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

} // namespace scenes
