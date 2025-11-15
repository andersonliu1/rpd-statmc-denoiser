#include <iostream>
#include <vector>
#include <chrono>
#include <string>
#include <random>

#include "CLI/CLI.hpp"
#include "spdlog/spdlog.h"
#include "stb_image_write.h"
#include "yaml-cpp/yaml.h"

#include "global/common.h"
#include "global/camera.h"
#include "global/ray.h"
#include "global/material.h"
#include "global/mesh.h"
#include "global/obj_loader.h"
#include "global/intersection.h"

std::random_device rd;
std::mt19937 gen(rd());
std::uniform_real_distribution<float> dis(0.0f, 1.0f);

Vec3f trace_ray(const Ray& ray, const Mesh& mesh, int depth) {
    if (depth <= 0) {
        return Vec3f(0, 0, 0);
    }

    HitRecord hit;
    if (mesh.intersect(ray, hit)) {
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

int main(int argc, char** argv) {
    CLI::App app{"Monte Carlo path tracer with denoising"};

    std::string config_file;
    std::string mesh_file;
    std::string output_file;

    app.add_option("-c,--config", config_file, "Path to config YAML file")->required();
    app.add_option("-m,--mesh", mesh_file, "Path to OBJ mesh file")->required();
    app.add_option("-o,--output", output_file, "Output PNG file path")->required();

    CLI11_PARSE(app, argc, argv);

    YAML::Node config;
    try {
        config = YAML::LoadFile(config_file);
    } catch (const YAML::BadFile& e) {
        spdlog::error("Failed to load config file {}: {}", config_file, e.what());
        return 1;
    }

    int image_width = config["image_width"].as<int>();
    int image_height = config["image_height"].as<int>();
    int samples_per_pixel = config["samples_per_pixel"].as<int>();
    int max_depth = config["max_depth"].as<int>();
    float gamma = config["gamma"].as<float>();

    float fov = config["fov"].as<float>();
    auto cam_pos = config["camera_position"];
    auto cam_lookat = config["camera_lookat"];

    Vec3f camera_pos(cam_pos[0].as<float>(), cam_pos[1].as<float>(), cam_pos[2].as<float>());
    Vec3f lookat(cam_lookat[0].as<float>(), cam_lookat[1].as<float>(), cam_lookat[2].as<float>());

    spdlog::info("Image: {}x{}", image_width, image_height);
    spdlog::info("Samples per pixel: {}", samples_per_pixel);
    spdlog::info("Max depth: {}", max_depth);
    spdlog::info("Camera position: [{}, {}, {}]", camera_pos.x(), camera_pos.y(), camera_pos.z());
    spdlog::info("Loading mesh: {}", mesh_file);

    Mesh mesh;
    auto triangles = load_obj(mesh_file);
    for (const auto& tri : triangles) {
        mesh.add_triangle(tri);
    }
    mesh.material = Material(Vec3f(0.7f, 0.7f, 0.7f), 0.5f);

    spdlog::info("Loaded {} triangles", triangles.size());

    Camera camera;
    float aspect_ratio = static_cast<float>(image_width) / image_height;
    camera.initialize(camera_pos, lookat, Vec3f(0, 1, 0), fov, aspect_ratio);

    std::vector<Vec3f> image(image_width * image_height, Vec3f(0, 0, 0));

    spdlog::info("Rendering...");
    auto start = std::chrono::high_resolution_clock::now();

    for (int j = 0; j < image_height; ++j) {
        if (j % 50 == 0) {
            spdlog::info("Progress: {}/{}", j, image_height);
        }
        for (int i = 0; i < image_width; ++i) {
            Vec3f color(0, 0, 0);

            for (int s = 0; s < samples_per_pixel; ++s) {
                float u = (i + dis(gen)) / (image_width - 1);
                float v = (j + dis(gen)) / (image_height - 1);

                Ray ray = camera.generate_ray(u, v);
                color += trace_ray(ray, mesh, max_depth);
            }

            color /= samples_per_pixel;

            color.x() = std::pow(color.x(), 1.0f / gamma);
            color.y() = std::pow(color.y(), 1.0f / gamma);
            color.z() = std::pow(color.z(), 1.0f / gamma);

            image[j * image_width + i] = color;
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    spdlog::info("Rendering completed in {} ms", duration.count());

    std::vector<unsigned char> pixels(image_width * image_height * 3);
    for (int j = 0; j < image_height; ++j) {
        for (int i = 0; i < image_width; ++i) {
            Vec3f color = image[j * image_width + i];
            int idx = (j * image_width + i) * 3;
            pixels[idx + 0] = static_cast<unsigned char>(255.99f * std::clamp(color.x(), 0.0f, 1.0f));
            pixels[idx + 1] = static_cast<unsigned char>(255.99f * std::clamp(color.y(), 0.0f, 1.0f));
            pixels[idx + 2] = static_cast<unsigned char>(255.99f * std::clamp(color.z(), 0.0f, 1.0f));
        }
    }

    if (stbi_write_png(output_file.c_str(), image_width, image_height, 3, pixels.data(), image_width * 3)) {
        spdlog::info("Saved to {}", output_file);
    } else {
        spdlog::error("Failed to save image");
        return 1;
    }

    return 0;
}
