#pragma once

#include "objects/scene_object.h"
#include <limits>
#include <memory>
#include <utility>
#include <vector>

class Scene {
public:
    void add_object(std::unique_ptr<SceneObject> object) {
        objects_.push_back(std::move(object));
    }

    bool intersect(const Ray& ray, HitRecord& hit) const {
        bool hit_anything = false;
        float closest = std::numeric_limits<float>::infinity();

        for (const auto& object : objects_) {
            HitRecord local_hit;
            if (object->intersect(ray, local_hit) && local_hit.t < closest) {
                closest = local_hit.t;
                hit = local_hit;
                hit_anything = true;
            }
        }

        hit.hit = hit_anything;
        return hit_anything;
    }

    size_t object_count() const {
        return objects_.size();
    }

private:
    std::vector<std::unique_ptr<SceneObject>> objects_;
};
