#pragma once

#include "global/intersection.h"
#include "global/ray.h"

class SceneObject {
public:
    virtual ~SceneObject() = default;
    virtual bool intersect(const Ray& ray, HitRecord& hit) const = 0;
};
