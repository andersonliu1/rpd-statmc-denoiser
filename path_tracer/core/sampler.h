#pragma once
#include "common.h"
#include <cmath>
#include <random>

struct Sampler {
    inline thread_local static std::mt19937 rng;
    inline thread_local static std::uniform_real_distribution<float> dist;
    inline thread_local static bool initialized = false;

    static void init(uint32_t seed) {
        rng = std::mt19937(seed);
        dist = std::uniform_real_distribution<float>(0.0f, 1.0f);
        initialized = true;
    }

    static void init_thread(uint32_t seed) {
        rng = std::mt19937(seed);
        dist = std::uniform_real_distribution<float>(0.0f, 1.0f);
        initialized = true;
    }

    static void ensure_thread_seed(uint32_t base_seed, uint32_t thread_id) {
        if (!initialized) {
            init_thread(base_seed ^ (0x9e3779b9u + thread_id * 0x85ebca6bu));
        }
    }

    static float next1d() {
        return dist(rng);
    }

    static Vec2f next2d() {
        return Vec2f(next1d(), next1d());
    }

    static Vec2f sample_disk() {
        Vec2f u = next2d();
        Vec2f offset = 2.0f * u - Vec2f::Ones();
        if(offset.x() == 0 && offset.y() == 0) return Vec2f::Zero();
        float theta, r;

        if(abs(offset.x()) > abs(offset.y())){
            r = offset.x();
            theta = M_PI_4 * (offset.y() / offset.x());
        }else{
            r = offset.y();
            theta = M_PI_2 - M_PI_4 * (offset.x() / offset.y());
        }
        return r * Vec2f(cos(theta), sin(theta));
    }

    static Vec3f sample_sphere() {
        Vec2f u = next2d();

        float z = 1.0f - 2.0f * u.x();
        float r = sqrt(std::max(0.0f, 1.0f - z * z));
        float phi = 2.0f * M_PI * u.y();

        return Vec3f(r * cos(phi), r * sin(phi), z);
    }
};
