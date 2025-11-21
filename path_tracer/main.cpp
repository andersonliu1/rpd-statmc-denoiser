#include <vector>
#include <chrono>
#include <string>
#include <random>
#include <stdexcept>
#include <algorithm>
#include <optional>
#include <type_traits>
#include <filesystem>

#include "CLI/CLI.hpp"
#include "spdlog/spdlog.h"
#include "yaml-cpp/yaml.h"

#include "core/common.h"
#include "core/camera.h"
#include "core/ray.h"
#include "core/material.h"
#include "core/sampler.h"
#include "core/scene_loader.h"
#include "core/image.h"
#include "core/ray_tracer.h"

struct RenderConfig {
    int image_width;
    int image_height;
    int samples_per_pixel;
    float fov;
    float focus_distance;
    float aperture;
    Vec3f camera_position;
    Vec3f camera_direction;
    std::optional<std::string> scene_name;
    std::optional<std::string> output_path;
};

Scene scene{};
BVH bvh{};

RenderConfig parse_render_config(const YAML::Node& config) {
    RenderConfig render_config{};
    render_config.image_width = config["image_width"].as<int>();
    render_config.image_height = config["image_height"].as<int>();
    render_config.samples_per_pixel = config["samples_per_pixel"].as<int>();
    render_config.fov = config["fov"].as<float>();

    auto cam_pos = config["camera_position"].as<std::vector<float>>();
    auto cam_direction = config["camera_direction"].as<std::vector<float>>();
    if (cam_pos.size() != 3 || cam_direction.size() != 3) {
        throw std::runtime_error("Camera vectors must have three components");
    }

    render_config.camera_position = Vec3f(cam_pos[0], cam_pos[1], cam_pos[2]);
    render_config.camera_direction = Vec3f(cam_direction[0], cam_direction[1], cam_direction[2]);
    render_config.focus_distance = config["focus_distance"] ? config["focus_distance"].as<float>() : render_config.camera_direction.norm();
    render_config.aperture = config["aperture"] ? config["aperture"].as<float>() : 0.0f;
    if (config["scene"]) render_config.scene_name = config["scene"].as<std::string>();
    if (config["output"]) render_config.output_path = config["output"].as<std::string>();

    return render_config;
}

Vec3f shade_mis(const int tri_idx, const Vec3f& p, const Vec3f wo) {
    const Triangle& tri = scene.triangles[tri_idx];
    const Material& material = scene.materials[tri.material_id];

    Vec3f L_dir = Vec3f::Zero();

    const auto [light_index, light_select_pdf] = scene.sample_light(Sampler::next1d());

    if (light_index >= 0) {
        Light light = scene.lights[light_index];
        bool is_delta = is_delta_light(light);

        auto sample_light = [&](const auto& light_obj) {
            using T = std::decay_t<decltype(light_obj)>;
            if constexpr (std::is_same_v<T, RectLight> || std::is_same_v<T, AreaLight>) {
                return light_obj.sample(p, Sampler::next2d());
            } else {
                return light_obj.sample(p);
            }
        };

        LightSample light_sample = std::visit(sample_light, light);

        if (light_sample.light_dist > EPS_SMALL) {
            float pdf_light = scene.light_pdf(light_index, light_select_pdf, p, light_sample.wi_world, light_sample.light_dist);

            if (pdf_light > EPS_SMALL) {
                float p_omega_w = std::max(EPS_SMALL, brdf_pdf(material, wo, light_sample.wi_world, tri.normal));
                float w_light = is_delta ? 1.0f : pdf_light / (pdf_light + p_omega_w);

                Ray shadow_ray(offset_ray_origin(p, tri.normal), light_sample.wi_world);
                bool hit = any_hit(shadow_ray, light_sample.light_dist, bvh, scene.triangles);

                if (!hit) {
                    Vec3f f = brdf_eval(material, wo, light_sample.wi_world, tri.normal);
                    Vec3f contribution = eval_area_light(light, -light_sample.wi_world).cwiseProduct(f) * std::max(0.0f, tri.normal.dot(light_sample.wi_world));
                    if (!is_delta) contribution /= pdf_light;
                    L_dir += w_light * contribution;
                }
            }
        }
    }

    const auto [ray_dir, pdf] = brdf_sample(material, wo, tri.normal, Sampler::next2d());

    Vec3f L_indir = Vec3f::Zero();

    if (pdf > EPS_SMALL && ray_dir.squaredNorm() > 0.0f) {
        Ray brdf_ray(offset_ray_origin(p, tri.normal), ray_dir);
        const auto [is_ray_hit, t_min, nearest_tri] = closest_hit(brdf_ray, bvh, scene.triangles);

        if (is_ray_hit && scene.triangles[nearest_tri].is_emitter()) {
            int emitter_idx = scene.triangle_to_light[nearest_tri];
            Light emitter = scene.lights[emitter_idx];

            float w_brdf = 1.0f;
            if (!is_delta_light(emitter)) {
                float pdf_light_brdf = scene.light_pdf(emitter_idx, scene.light_select_pdf(emitter_idx), p, ray_dir, t_min);
                if (pdf_light_brdf > EPS_SMALL) {
                    w_brdf = pdf / (pdf + pdf_light_brdf);
                }
            }

            Vec3f f = brdf_eval(material, wo, ray_dir, tri.normal);
            L_dir += w_brdf * eval_area_light(emitter, -ray_dir).cwiseProduct(f) * (std::max(0.0f, tri.normal.dot(ray_dir)) / pdf);
        }

        const float p_rr = 0.8f;
        float ksi = Sampler::next1d();

        if (ksi < p_rr) {
            if (is_ray_hit && !scene.triangles[nearest_tri].is_emitter()) {
                const Vec3f hit_pos = brdf_ray.at(t_min);
                Vec3f f = brdf_eval(material, wo, ray_dir, tri.normal);
                L_indir = shade_mis(nearest_tri, hit_pos, -ray_dir).cwiseProduct(f) * (std::max(0.0f, tri.normal.dot(ray_dir)) / (pdf * p_rr));
            }
        }
    }

    return L_dir + L_indir;
}

Vec3f mis_path_trace(Ray ray) {
    const auto [is_hit, t_min, nearest_tri] = closest_hit(ray, bvh, scene.triangles);
    if (!is_hit) return scene.environment_color;

    const Vec3f hit_pos = ray.at(t_min);

    if (scene.triangles[nearest_tri].is_emitter()) {
        return eval_area_light(scene.lights[scene.triangle_to_light[nearest_tri]], -ray.direction);
    }

    return shade_mis(nearest_tri, hit_pos, -ray.direction.normalized());
}

Image render_image(const RenderConfig& config, const Scene& scene, const Camera& camera) {
    Image image(config.image_width, config.image_height);

    spdlog::info("Rendering...");
    auto start = std::chrono::high_resolution_clock::now();

    for (int y = 0; y < config.image_height; ++y) {
        if (y % 50 == 0) {
            spdlog::info("Rendering row {} / {}", y, config.image_height);
        }
        for (int x = 0; x < config.image_width; ++x) {
            image(x, y) = Vec3f::Zero();

            for (int s = 0; s < config.samples_per_pixel; ++s) {
                const float u = (x + Sampler::next1d()) / (config.image_width);
                const float v = (y + Sampler::next1d()) / (config.image_height);

                Ray ray = camera.generate_ray(u, (1.0f - v));
                Vec3f path_trace = mis_path_trace(ray);
                image(x,y) += clamp(path_trace, Vec3f(0.0f, 0.0f, 0.0f), Vec3f(50.0f, 50.0f, 50.0f));
            }

            image(x,y) /= (float)config.samples_per_pixel;
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    spdlog::info("Rendering completed in {} ms", duration.count());

    return image;
}

int main(int argc, char** argv) {
    CLI::App app{"Monte Carlo path tracer with denoising"};

    std::string config_file;
    std::string scene_name;
    std::string output_file;
    std::optional<uint32_t> sampler_seed;

    app.add_option("-c,--config", config_file, "Path to config YAML file")->required();
    app.add_option("-s,--scene", scene_name, "Name of the built-in scene to render (overrides config)");
    app.add_option("-o,--output", output_file, "Output PNG file path");
    app.add_option("--seed", sampler_seed, "Optional RNG seed for the sampler");

    CLI11_PARSE(app, argc, argv);

    YAML::Node config;
    try {
        config = YAML::LoadFile(config_file);
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

    std::string scene_to_render = !scene_name.empty() ? scene_name : render_config.scene_name.value_or("");
    if (scene_to_render.empty()) {
        spdlog::error("No scene specified. Provide --scene or set 'scene' in the config.");
        return 1;
    }

    std::string output_path = !output_file.empty() ? output_file : render_config.output_path.value_or("");
    if (output_path.empty()) {
        spdlog::error("No output specified. Provide --output or set 'output' in the config.");
        return 1;
    }
    if (!output_file.empty() && render_config.output_path && output_file != *render_config.output_path) {
        spdlog::info("CLI output '{}' overrides config output '{}'", output_file, *render_config.output_path);
    }

    spdlog::info("Image: {}x{}", render_config.image_width, render_config.image_height);
    spdlog::info("Samples per pixel: {}", render_config.samples_per_pixel);
    spdlog::info("Camera position: [{}, {}, {}]", render_config.camera_position.x(), render_config.camera_position.y(), render_config.camera_position.z());
    spdlog::info("Scene: {}", scene_to_render);
    spdlog::info("Output path: {}", output_path);

    try {
        scene = scenes::load_scene(scene_to_render);
    } catch (const std::exception& e) {
        spdlog::error("Failed to load scene '{}': {}", scene_to_render, e.what());
        return 1;
    }

    bvh.build_bvh(scene.triangles);

    spdlog::info("Loaded scene with {} triangles, {} materials, {} lights", scene.triangle_count(), scene.material_count(), scene.lights.size());

    Camera camera;
    camera.init(render_config.camera_position, render_config.camera_direction, render_config.fov, float(render_config.image_width) / render_config.image_height, render_config.focus_distance, render_config.aperture);

    uint32_t seed = sampler_seed.value_or(std::random_device{}());
    if (sampler_seed) {
        spdlog::info("Using user-provided sampler seed {}", seed);
    } else {
        spdlog::info("Using random sampler seed {}", seed);
    }
    Sampler::init(seed);

    Image image = render_image(render_config, scene, camera);

    const std::filesystem::path output_fs_path(output_path);
    const std::filesystem::path parent_dir = output_fs_path.parent_path();
    if (!parent_dir.empty()) {
        std::error_code ec;
        if (!std::filesystem::create_directories(parent_dir, ec) && ec) {
            spdlog::warn("Failed to create directories for '{}': {}", output_path, ec.message());
        }
    }

    if (image.save_with_tonemapping(output_path)) {
        spdlog::info("Saved to {}", output_path);
        return 0;
    }

    spdlog::error("Failed to save image");
    return 1;
}
