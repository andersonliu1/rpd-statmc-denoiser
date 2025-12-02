#pragma once
#include "common.h"
#include "triangle.h"
#include "ray.h"
#include <cstdint>
#include <limits>
#include <numeric>
#include <algorithm>
#include <tuple>
#include <utility>
#include <vector>
#include <cmath>

struct BoundingBox {
    Vec3f min_point, max_point;

    BoundingBox &include(const Vec3f& point) {
        min_point = min_point.cwiseMin(point);
        max_point = max_point.cwiseMax(point);
        
        return *this;
    }

    Vec3f get_center() const { return 0.5f * (min_point + max_point); }

    Vec3f get_corner(int index) const { 
        Vec3f res;
        for (size_t i = 0; i < 3; ++i){
            res[i] = (index & (1 << i)) ? max_point[i] : min_point[i];
        }

        return res;
    }

    bool overlaps_triangle(const Triangle& tri) const {
        Vec3f tri_min = tri.v0().cwiseMin(tri.v1().cwiseMin(tri.v2()));
        Vec3f tri_max = tri.v0().cwiseMax(tri.v1().cwiseMax(tri.v2()));

        return (
            tri_min.x() <= max_point.x() && tri_max.x() >= min_point.x() &&
            tri_min.y() <= max_point.y() && tri_max.y() >= min_point.y() &&
            tri_min.z() <= max_point.z() && tri_max.z() >= min_point.z()
        );
    }

    std::tuple<bool, float, float> ray_intersect(const Ray& ray) const {
        const Vec3f inv_dir = ray.direction.cwiseInverse();
        const Vec3f low = (min_point - ray.origin).cwiseProduct(inv_dir);
        const Vec3f high = (max_point - ray.origin).cwiseProduct(inv_dir);
        
        const Vec3f t_min = low.cwiseMin(high), t_max = low.cwiseMax(high);

        const float t_near = std::fmax(0.0f, std::fmax(t_min[0], std::fmax(t_min[1], t_min[2]))), t_far = std::fmin(t_max[0], std::fmin(t_max[1], t_max[2]));

        return { t_near <= t_far, t_near, t_far };
    }
};

struct BVHNode {
    BoundingBox bounds;
    uint32_t first_index;
    uint32_t index_count;
    int left, right;

    BVHNode(const BoundingBox& bounds, const uint32_t first_index, const uint32_t index_count = 0, const int left = -1, const int right = -1) : bounds(bounds), first_index(first_index), index_count(index_count), left(left), right(right) {}

    bool is_leaf() const { return index_count > 0; }
};

struct BVH {
    static constexpr uint32_t MAX_LEAF_SIZE = 4;
    static constexpr int MAX_DEPTH = 32;

    std::vector<BVHNode> nodes;
    std::vector<uint32_t> triangle_indices;
    std::vector<Vec3f> triangle_centroids;
    int root = -1;

    /// @brief Traverses the BVH starting from root
    /// @param triangles Array of triangles
    /// @param root Starting node
    /// @param ray Ray to trace
    /// @param tMax Max dist along the ray
    /// @param shadow_ray If shadow_ray then we only care about whether it hits or not
    /// @return 
    std::tuple<bool, float, uint32_t> traverse_bvh(const std::vector<Triangle>& triangles, int root, const Ray& ray, const float tMax, const bool shadow_ray) const {
        uint32_t closest_tri = UINT32_MAX;

        if (root < 0 || nodes.empty()) {
            return { false, tMax, closest_tri };
        }

        bool hit_anything = false;
        float effectiveMax = shadow_ray ? tMax - EPS_ANYHIT : tMax;
        if (effectiveMax <= EPS_SMALL) return { false, tMax, closest_tri };

        float closest_t = effectiveMax;

        std::vector<std::pair<int, float>> stack;
        stack.reserve(MAX_DEPTH);

        auto [root_hit, root_t_near, root_t_far] = nodes[root].bounds.ray_intersect(ray);
        if (!root_hit || root_t_near > effectiveMax || root_t_far < EPS_SMALL) {
            return { false, tMax, closest_tri };
        }
        stack.emplace_back(root, root_t_near);

        while (!stack.empty()) {
            auto [node_index, t_near] = stack.back();
            stack.pop_back();

            if (t_near > closest_t) continue;

            const BVHNode& node = nodes[node_index];

            if (node.is_leaf()) {
                for (uint32_t i = 0; i < node.index_count; ++i) {
                    uint32_t tri_idx = triangle_indices[node.first_index + i];
                    const Triangle& tri = triangles[tri_idx];
                    auto [hit, t] = Triangle::ray_triangle_intersect(tri, ray, 0.0f, closest_t);
                    if (hit && t < closest_t) {
                        closest_t = t;
                        closest_tri = tri_idx;
                        hit_anything = true;
                        if (shadow_ray) return { true, closest_t, closest_tri };
                    }
                }
            } else {
                std::pair<int, float> child_entries[2];
                int child_count = 0;

                auto consider_child = [&](int child_idx) {
                    if (child_idx < 0) return;
                    const BVHNode& child = nodes[child_idx];
                    auto [child_hit, child_near, child_far] = child.bounds.ray_intersect(ray);
                    if (!child_hit || child_near > closest_t || child_far < EPS_SMALL) return;
                    child_entries[child_count] = { child_idx, child_near };
                    ++child_count;
                };

                consider_child(node.left);
                consider_child(node.right);

                if (child_count == 1) {
                    stack.push_back(child_entries[0]);
                } else if (child_count == 2) {
                    if (child_entries[0].second < child_entries[1].second) {
                        stack.push_back(child_entries[1]);
                        stack.push_back(child_entries[0]);
                    } else {
                        stack.push_back(child_entries[0]);
                        stack.push_back(child_entries[1]);
                    }
                }
            }
        }

        return { hit_anything, closest_t, closest_tri };
    }

    BVH() = default;
    BVH(const std::vector<Triangle>& triangles){
        build_bvh(triangles);
    }

    void build_bvh(const std::vector<Triangle>& triangles) {
        triangle_indices.resize(triangles.size());
        triangle_centroids.resize(triangles.size());
        std::iota(triangle_indices.begin(), triangle_indices.end(), 0);

        nodes.clear();
        nodes.reserve(triangles.size() * 2);

        for (size_t i = 0; i < triangles.size(); ++i) {
            const Triangle& tri = triangles[i];
            triangle_centroids[i] = (tri.v0() + tri.v1() + tri.v2()) / 3.0f;
        }

        if (triangles.empty()) {
            root = -1;
            return;
        }

        root = build(triangles, 0, (uint32_t)triangles.size(), 0);
    }

    int build(const std::vector<Triangle>& triangles, uint32_t first, uint32_t count, int depth) {
        BoundingBox bounds;
        
        float inf = std::numeric_limits<float>::infinity();
        bounds.min_point = Vec3f(inf, inf, inf);
        bounds.max_point = Vec3f(-inf, -inf, -inf);

        for (uint32_t i = 0; i < count; ++i) {
            uint32_t tri_idx = triangle_indices[first + i];
            const Triangle& tri = triangles[tri_idx];
            bounds.include(tri.v0()).include(tri.v1()).include(tri.v2());
        }

        if (count <= MAX_LEAF_SIZE || depth >= MAX_DEPTH) {
            int node_index = (int)nodes.size();
            nodes.emplace_back(bounds, first, count);
            return node_index;
        }

        Vec3f centroid_min(inf, inf, inf);
        Vec3f centroid_max(-inf, -inf, -inf);

        for (uint32_t i = 0; i < count; ++i) {
            uint32_t tri_idx = triangle_indices[first + i];
            const Vec3f& c = triangle_centroids[tri_idx];
            centroid_min = centroid_min.cwiseMin(c);
            centroid_max = centroid_max.cwiseMax(c);
        }

        Vec3f centroid_extent = centroid_max - centroid_min;
        int axis = 0;
        if (centroid_extent.y() > centroid_extent.x()) axis = 1;
        if (centroid_extent.z() > centroid_extent[axis]) axis = 2;

        if (centroid_extent[axis] <= EPS) {
            int node_index = (int)nodes.size();
            nodes.emplace_back(bounds, first, count);
            return node_index;
        }

        uint32_t mid = first + count / 2;
        auto begin = triangle_indices.begin() + first;
        auto mid_iter = triangle_indices.begin() + mid;
        auto end = triangle_indices.begin() + first + count;

        std::nth_element(begin, mid_iter, end, [&](uint32_t lhs, uint32_t rhs) {
            return triangle_centroids[lhs][axis] < triangle_centroids[rhs][axis];
        });

        if (mid == first || mid == first + count) {
            int node_index = (int)nodes.size();
            nodes.emplace_back(bounds, first, count);
            return node_index;
        }

        int left = build(triangles, first, mid - first, depth + 1);
        int right = build(triangles, mid, count - (mid - first), depth + 1);
        int node_index = (int)nodes.size();
        nodes.emplace_back(bounds, first, 0, left, right);
        return node_index;
    }
};

/// @brief Finds the closest triangle hit for the given ray and BVH
/// @param ray Ray to trace. Make sure the origin is offset if needed
/// @param bvh BVH built over `triangles`
/// @param triangles Array of triangles
/// @return
static std::tuple<bool, float, uint32_t> closest_hit(const Ray& ray, const BVH& bvh, const std::vector<Triangle>& triangles) {
    return bvh.traverse_bvh(triangles, bvh.root, ray, std::numeric_limits<float>::infinity(), false);
}

/// @brief Tests whether the ray hits any triangle in the BVH
/// @param ray Ray to trace. Make sure the origin is offset if needed
/// @param bvh BVH built over `triangles`
/// @param triangles Array of triangles
/// @return True if any intersection occurs before tMax, false otherwise
static bool any_hit(const Ray& ray, const float tMax, const BVH& bvh, const std::vector<Triangle>& triangles) {
    auto [hit, t, tri] = bvh.traverse_bvh(triangles, bvh.root, ray, tMax, true);
    return hit;
}

#include "sphere.h"
#include "primitive_type.h"

/// @brief Finds the closest sphere hit for the given ray
/// @param ray Ray to trace
/// @param spheres Array of spheres
/// @return (hit, t, sphere_index, normal)
static std::tuple<bool, float, uint32_t, Vec3f> closest_sphere_hit(
    const Ray& ray,
    const std::vector<Sphere>& spheres
) {
    bool hit_anything = false;
    float closest_t = std::numeric_limits<float>::infinity();
    uint32_t closest_sphere = UINT32_MAX;
    Vec3f closest_normal = Vec3f::Zero();

    for (size_t i = 0; i < spheres.size(); ++i) {
        auto [hit, t, normal] = Sphere::ray_sphere_intersect(spheres[i], ray, EPS_SMALL, closest_t);
        if (hit && t < closest_t) {
            hit_anything = true;
            closest_t = t;
            closest_sphere = static_cast<uint32_t>(i);
            closest_normal = normal;
        }
    }

    return {hit_anything, closest_t, closest_sphere, closest_normal};
}

/// @brief Tests whether the ray hits any sphere
/// @param ray Ray to trace
/// @param tMax Maximum distance to check
/// @param spheres Array of spheres
/// @return True if any intersection occurs before tMax
static bool any_sphere_hit(
    const Ray& ray,
    const float tMax,
    const std::vector<Sphere>& spheres
) {
    float effective_max = tMax - EPS_ANYHIT;
    if (effective_max <= EPS_SMALL) return false;

    for (const auto& sphere : spheres) {
        auto [hit, t, normal] = Sphere::ray_sphere_intersect(sphere, ray, EPS_SMALL, effective_max);
        if (hit) return true;
    }

    return false;
}

/// @brief Combined closest hit test for both triangles and spheres
/// @return (hit, t, primitive_type, primitive_index, normal)
///         if primitive_type==Triangle, use triangle normal from triangles[index]
///         if primitive_type==Sphere, normal is provided directly
static std::tuple<bool, float, PrimitiveType, uint32_t, Vec3f> closest_hit_combined(
    const Ray& ray,
    const BVH& bvh,
    const std::vector<Triangle>& triangles,
    const std::vector<Sphere>& spheres
) {
    // Test triangles
    auto [tri_hit, tri_t, tri_idx] = closest_hit(ray, bvh, triangles);

    // Test spheres
    auto [sphere_hit, sphere_t, sphere_idx, sphere_normal] = closest_sphere_hit(ray, spheres);

    // Determine which is closer
    if (tri_hit && sphere_hit) {
        if (tri_t < sphere_t) {
            return {true, tri_t, PrimitiveType::Triangle, tri_idx, Vec3f::Zero()};
        } else {
            return {true, sphere_t, PrimitiveType::Sphere, sphere_idx, sphere_normal};
        }
    } else if (tri_hit) {
        return {true, tri_t, PrimitiveType::Triangle, tri_idx, Vec3f::Zero()};
    } else if (sphere_hit) {
        return {true, sphere_t, PrimitiveType::Sphere, sphere_idx, sphere_normal};
    } else {
        return {false, 0.0f, PrimitiveType::Triangle, UINT32_MAX, Vec3f::Zero()};
    }
}

/// @brief Combined any hit test for both triangles and spheres
static bool any_hit_combined(
    const Ray& ray,
    const float tMax,
    const BVH& bvh,
    const std::vector<Triangle>& triangles,
    const std::vector<Sphere>& spheres
) {
    if (any_hit(ray, tMax, bvh, triangles)) return true;
    if (any_sphere_hit(ray, tMax, spheres)) return true;
    return false;
}
