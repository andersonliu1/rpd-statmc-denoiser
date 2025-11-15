#pragma once

#include "common.h"

struct Ray {
    Vec3f origin;
    Vec3f direction;

    Ray() = default;
    Ray(const Vec3f& o, const Vec3f& d) : origin(o), direction(d) {}

    Vec3f at(float t) const {
        return origin + t * direction;
    }
};
