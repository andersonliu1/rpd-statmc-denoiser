#pragma once
#include "common.h"
#include "triangle.h"
#include <cstdint>
#include <numeric>
#include <algorithm>
#include <tuple>

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

    std::tuple<bool, float, float> ray_intersect(const Vec3f& ray_origin, const Vec3f& ray_direction) const {
        const Vec3f inv_dir = ray_direction.cwiseInverse();
        const Vec3f low = (min_point - ray_origin) * inv_dir;
        const Vec3f high = (max_point - ray_origin) * inv_dir;
        
        const Vec3f t_min = low.cwiseMin(high), t_max = low.cwiseMax(high);

        const float t_near = std::fmax(0.0f, std::fmax(t_min[0], std::fmax(t_min[1], t_min[2]))), t_far = std::fmin(t_max[0], std::fmin(t_max[1], t_max[2]));

        return { t_near <= t_far, t_near, t_far };
    }
};

// struct OctreeNode {
//     BoundingBox bounds;
//     std::vector<uint32_t> triangle_indices;
//     std::array<std::unique_ptr<OctreeNode>, 8> children;
//     bool is_leaf;


//     OctreeNode(const BoundingBox& bounds, const std::vector<uint32_t>& triangle_indices, const bool is_leaf = false) : bounds(bounds), triangle_indices(triangle_indices), is_leaf(is_leaf) {
//         for (size_t i = 0; i < 8; ++i) children[i] = nullptr;
//     }
// };

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
