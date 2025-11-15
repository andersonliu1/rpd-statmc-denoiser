#pragma once

#include "common.h"

struct Material;

struct HitRecord {
    float t;
    Vec3f point;
    Vec3f normal;
    Material* material;
    bool hit;

    HitRecord() : t(INFINITY), material(nullptr), hit(false) {}
};
