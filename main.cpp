#include <iostream>
#include <vector>
#include <chrono>
#include <string>
#include <random>
#include <stdexcept>
#include <algorithm>
#include <optional>

#include "CLI/CLI.hpp"
#include "spdlog/spdlog.h"
#include "stb_image_write.h"
#include "yaml-cpp/yaml.h"

#include "global/common.h"
#include "global/camera.h"
#include "global/ray.h"
#include "global/material.h"
#include "global/intersection.h"
#include "global/sampler.h"
#include "global/scene_loader.h"

struct RenderConfig {
    int image_width;
    int image_height;
    int samples_per_pixel;
    int max_depth;
    float gamma;
    float fov;
    Vec3f camera_position;
    Vec3f camera_lookat;
};

Vec3f trace_ray(const Ray& ray, const Scene& scene, int depth) {
    if (depth <= 0) {
        return Vec3f(0, 0, 0);
    }

    HitRecord hit;
    if (scene.intersect(ray, hit)) {
        Vec3f light_dir = Vec3f(0.5f, 1.0f, 0.3f).normalized();
        float ndotl = std::max(0.0f, hit.normal.dot(light_dir));

        Vec3f ambient = Vec3f(0.1f, 0.1f, 0.1f);
        Vec3f diffuse = hit.material->albedo * ndotl;

        return ambient + diffuse;
    }

    Vec3f unit_direction = ray.direction.normalized();
    float t = 0.5f * (unit_direction.y() + 1.0f);
    return (1.0f - t) * Vec3f(1.0f, 1.0f, 1.0f) + t * Vec3f(0.5f, 0.7f, 1.0f);
}

RenderConfig parse_render_config(const YAML::Node& config) {
    RenderConfig render_config{};
    render_config.image_width = config["image_width"].as<int>();
    render_config.image_height = config["image_height"].as<int>();
    render_config.samples_per_pixel = config["samples_per_pixel"].as<int>();
    render_config.max_depth = config["max_depth"].as<int>();
    render_config.gamma = config["gamma"].as<float>();
    render_config.fov = config["fov"].as<float>();

    auto cam_pos = config["camera_position"].as<std::vector<float>>();
    auto cam_lookat = config["camera_lookat"].as<std::vector<float>>();
    if (cam_pos.size() != 3 || cam_lookat.size() != 3) {
        throw std::runtime_error("Camera vectors must have three components");
    }

    render_config.camera_position = Vec3f(cam_pos[0], cam_pos[1], cam_pos[2]);
    render_config.camera_lookat = Vec3f(cam_lookat[0], cam_lookat[1], cam_lookat[2]);

    return render_config;
}

Camera build_camera(const RenderConfig& config) {
    Camera camera;
    float aspect_ratio = static_cast<float>(config.image_width) / config.image_height;
    camera.initialize(config.camera_position, config.camera_lookat, Vec3f(0, 1, 0), config.fov, aspect_ratio);
    return camera;
}

std::vector<Vec3f> render_image(const RenderConfig& config, const Scene& scene, const Camera& camera) {
    std::vector<Vec3f> image(config.image_width * config.image_height, Vec3f(0, 0, 0));

    spdlog::info("Rendering...");
    auto start = std::chrono::high_resolution_clock::now();

    for (int j = 0; j < config.image_height; ++j) {
        if (j % 50 == 0) {
            spdlog::info("Progress: {}/{}", j, config.image_height);
        }
        for (int i = 0; i < config.image_width; ++i) {
            Vec3f color(0, 0, 0);

            for (int s = 0; s < config.samples_per_pixel; ++s) {
                float u = (i + Sampler::next1d()) / (config.image_width - 1);
                float v = (j + Sampler::next1d()) / (config.image_height - 1);

                Ray ray = camera.generate_ray(u, v);
                color += trace_ray(ray, scene, config.max_depth);
            }

            color /= config.samples_per_pixel;

            color.x() = std::pow(color.x(), 1.0f / config.gamma);
            color.y() = std::pow(color.y(), 1.0f / config.gamma);
            color.z() = std::pow(color.z(), 1.0f / config.gamma);

            image[j * config.image_width + i] = color;
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    spdlog::info("Rendering completed in {} ms", duration.count());

    return image;
}

std::vector<unsigned char> tonemap_image(const RenderConfig& config, const std::vector<Vec3f>& image) {
    std::vector<unsigned char> pixels(config.image_width * config.image_height * 3);
    for (int j = 0; j < config.image_height; ++j) {
        for (int i = 0; i < config.image_width; ++i) {
            Vec3f color = image[j * config.image_width + i];
            int idx = (j * config.image_width + i) * 3;
            pixels[idx + 0] = static_cast<unsigned char>(255.99f * std::clamp(color.x(), 0.0f, 1.0f));
            pixels[idx + 1] = static_cast<unsigned char>(255.99f * std::clamp(color.y(), 0.0f, 1.0f));
            pixels[idx + 2] = static_cast<unsigned char>(255.99f * std::clamp(color.z(), 0.0f, 1.0f));
        }
    }
    return pixels;
}

YAML::Node load_yaml(const std::string& path) {
    try {
        return YAML::LoadFile(path);
    } catch (const YAML::BadFile& e) {
        throw std::runtime_error("Failed to load YAML file " + path + ": " + e.what());
    }
}

int main(int argc, char** argv) {
    CLI::App app{"Monte Carlo path tracer with denoising"};

    std::string config_file;
    std::string asset_root;
    std::string scene_file;
    std::string output_file;
    std::optional<uint32_t> sampler_seed;

    app.add_option("-c,--config", config_file, "Path to config YAML file")->required();
    app.add_option("--asset-root", asset_root, "Root directory for assets")->required();
    app.add_option("--scene", scene_file, "Path to scene YAML file")->required();
    app.add_option("-o,--output", output_file, "Output PNG file path")->required();
    app.add_option("--seed", sampler_seed, "Optional RNG seed for the sampler");

    CLI11_PARSE(app, argc, argv);

    YAML::Node config;
    try {
        config = load_yaml(config_file);
    } catch (const std::exception& e) {
        spdlog::error("{}", e.what());
        return 1;
    }

    RenderConfig render_config;
    try {
        render_config = parse_render_config(config);
    } catch (const std::exception& e) {
        spdlog::error("Failed to parse render config: {}", e.what());
        return 1;
    }

    spdlog::info("Image: {}x{}", render_config.image_width, render_config.image_height);
    spdlog::info("Samples per pixel: {}", render_config.samples_per_pixel);
    spdlog::info("Max depth: {}", render_config.max_depth);
    spdlog::info("Camera position: [{}, {}, {}]", render_config.camera_position.x(), render_config.camera_position.y(), render_config.camera_position.z());
    spdlog::info("Asset root: {}", asset_root);
    spdlog::info("Scene file: {}", scene_file);

    Scene scene;
    try {
        scene = scene_loader::load_scene(scene_file, asset_root);
    } catch (const std::exception& e) {
        spdlog::error("Failed to load scene {}: {}", scene_file, e.what());
        return 1;
    }

    spdlog::info("Loaded scene with {} objects", scene.object_count());

    Camera camera = build_camera(render_config);

    uint32_t seed = sampler_seed.value_or(std::random_device{}());
    if (sampler_seed) {
        spdlog::info("Using user-provided sampler seed {}", seed);
    } else {
        spdlog::info("Using random sampler seed {}", seed);
    }
    Sampler::init(seed);

    auto image = render_image(render_config, scene, camera);
    auto pixels = tonemap_image(render_config, image);

    if (stbi_write_png(output_file.c_str(), render_config.image_width, render_config.image_height, 3, pixels.data(), render_config.image_width * 3)) {
        spdlog::info("Saved to {}", output_file);
        return 0;
    }

    spdlog::error("Failed to save image");
    return 1;
}
