#include "global/scene_loader.h"

#include "objects/mesh.h"
#include "objects/sphere.h"
#include "obj_loader.h"
#include "yaml-cpp/yaml.h"

#include <Eigen/Geometry>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0f;

Vec3f read_vec3(const YAML::Node& node, const Vec3f& fallback) {
    if (!node || !node.IsSequence() || node.size() != 3) {
        return fallback;
    }
    return Vec3f(node[0].as<float>(), node[1].as<float>(), node[2].as<float>());
}

struct TransformInfo {
    Eigen::Affine3f matrix = Eigen::Affine3f::Identity();
    Vec3f scale = Vec3f::Ones();
};

TransformInfo build_transform(const YAML::Node& node) {
    TransformInfo info;

    constexpr float epsilon = 1e-6f;
    info.scale = read_vec3(node ? node["scale"] : YAML::Node(), Vec3f::Ones());
    if (std::abs(info.scale.x()) < epsilon) info.scale.x() = epsilon;
    if (std::abs(info.scale.y()) < epsilon) info.scale.y() = epsilon;
    if (std::abs(info.scale.z()) < epsilon) info.scale.z() = epsilon;
    info.matrix.scale(info.scale);

    Vec3f rotation_deg = read_vec3(node ? node["rotation"] : YAML::Node(), Vec3f::Zero());
    info.matrix.rotate(Eigen::AngleAxisf(rotation_deg.x() * kDegToRad, Vec3f::UnitX()));
    info.matrix.rotate(Eigen::AngleAxisf(rotation_deg.y() * kDegToRad, Vec3f::UnitY()));
    info.matrix.rotate(Eigen::AngleAxisf(rotation_deg.z() * kDegToRad, Vec3f::UnitZ()));

    Vec3f translation = read_vec3(node ? node["translation"] : YAML::Node(), Vec3f::Zero());
    info.matrix.translate(translation);

    return info;
}

Material build_material(const YAML::Node& node) {
    Vec3f albedo = read_vec3(node ? node["albedo"] : YAML::Node(), Vec3f(0.7f, 0.7f, 0.7f));
    float roughness = node && node["roughness"] ? node["roughness"].as<float>() : 0.5f;
    return Material(albedo, roughness);
}

void apply_transform(std::vector<Triangle>& triangles, const TransformInfo& transform) {
    for (auto& tri : triangles) {
        tri.v0() = transform.matrix * tri.v0();
        tri.v1() = transform.matrix * tri.v1();
        tri.v2() = transform.matrix * tri.v2();

        Vec3f normal = (tri.v1() - tri.v0()).cross(tri.v2() - tri.v0());
        if (normal.squaredNorm() > 0.0f) {
            tri.normal = normal.normalized();
        }
    }
}

} // namespace

namespace scene_loader {

Scene load_scene(const std::string& scene_file, const std::string& asset_root) {
    YAML::Node scene_root;
    try {
        scene_root = YAML::LoadFile(scene_file);
    } catch (const YAML::BadFile& e) {
        throw std::runtime_error(std::string("Failed to load scene file: ") + e.what());
    }

    auto objects = scene_root["objects"];
    if (!objects || !objects.IsSequence()) {
        throw std::runtime_error("Scene file must contain an 'objects' sequence");
    }

    Scene scene;
    for (const auto& object : objects) {
        if (!object["type"]) {
            throw std::runtime_error("Scene object is missing 'type'");
        }

        const auto type = object["type"].as<std::string>();
        if (type == "mesh") {
            if (!object["path"]) {
                throw std::runtime_error("Mesh object requires a 'path'");
            }

            std::filesystem::path mesh_path = std::filesystem::path(asset_root) / object["path"].as<std::string>();
            if (!std::filesystem::exists(mesh_path)) {
                throw std::runtime_error("Mesh file not found: " + mesh_path.string());
            }

            auto triangles = load_obj(mesh_path.string());
            auto transform = build_transform(object["transform"]);
            apply_transform(triangles, transform);

            auto mesh = std::make_unique<Mesh>();
            for (const auto& tri : triangles) {
                mesh->add_triangle(tri);
            }

            mesh->material = build_material(object["material"]);
            scene.add_object(std::move(mesh));
            continue;
        }

        if (type == "sphere") {
            auto sphere = std::make_unique<Sphere>();
            float radius = object["radius"] ? object["radius"].as<float>() : 1.0f;

            auto transform = build_transform(object["transform"]);
            Vec3f center = transform.matrix * Vec3f::Zero();

            const Vec3f scale = transform.scale.cwiseAbs();
            const float max_scale = std::max(scale.x(), std::max(scale.y(), scale.z()));
            const float min_scale = std::min(scale.x(), std::min(scale.y(), scale.z()));
            if (std::abs(max_scale - min_scale) > 1e-3f) {
                throw std::runtime_error("Sphere scaling must be uniform");
            }

            sphere->center = center;
            sphere->radius = radius * max_scale;
            sphere->material = build_material(object["material"]);
            scene.add_object(std::move(sphere));
            continue;
        }

        throw std::runtime_error("Unsupported object type: " + type);
    }

    if (scene.object_count() == 0) {
        throw std::runtime_error("Scene contains no supported objects");
    }

    return scene;
}

} // namespace scene_loader
