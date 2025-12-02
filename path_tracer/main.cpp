#include <vector>
#include <chrono>
#include <string>
#include <random>
#include <stdexcept>
#include <algorithm>
#include <optional>
#include <type_traits>
#include <filesystem>
#include <atomic>

#ifdef _OPENMP
#include <omp.h>
#endif

#include "CLI/CLI.hpp"
#include "spdlog/spdlog.h"
#include "yaml-cpp/yaml.h"
#include "shared/global.h"

#include "core/buffers.h"
#include "core/config.h"
#include "core/common.h"
#include "core/camera.h"
#include "core/ray.h"
#include "core/material.h"
#include "core/sampler.h"
#include "core/scene_loader.h"
#include "core/stats.h"
#include "core/ray_tracer.h"
#include "core/primitive_type.h"


Scene scene{};
BVH bvh{};
FrameBuffers buffers{};
std::vector<PixelStats> pixelStats;

struct HitInfo {
    PrimitiveType type;
    uint32_t index;
    Vec3f position;
    Vec3f normal;
    Vec3f wo;

    int get_material_id() const {
        switch (type) {
            case PrimitiveType::Sphere:
                return scene.spheres[index].material_id;
            case PrimitiveType::Triangle:
                return scene.triangles[index].material_id;
            default:
                return -1;
        }
    }

    const Material& get_material() const {
        return scene.materials[get_material_id()];
    }

    Vec3f get_emission() const {
        switch (type) {
            case PrimitiveType::Sphere:
                return scene.spheres[index].emission;
            case PrimitiveType::Triangle:
                return scene.triangles[index].emission;
            default:
                return Vec3f::Zero();
        }
    }

    bool is_emitter() const {
        switch (type) {
            case PrimitiveType::Sphere:
                return scene.spheres[index].is_emitter();
            case PrimitiveType::Triangle:
                return scene.triangles[index].is_emitter();
            default:
                return false;
        }
    }

    int get_light_index() const {
        switch (type) {
            case PrimitiveType::Sphere:
                return scene.sphere_to_light[index];
            case PrimitiveType::Triangle:
                return scene.triangle_to_light[index];
            default:
                return -1;
        }
    }
};

Vec3f shade_mis(const HitInfo& hit) {
    const Material& material = hit.get_material();
    const Vec3f& p = hit.position;
    const Vec3f& wo = hit.wo;
    const Vec3f& normal = hit.normal;

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

        if (light_sample.light_dist > EPS_SMALL || is_delta) {
            float pdf_light = scene.light_pdf(light_index, light_select_pdf, p, light_sample.wi_world, light_sample.light_dist);

            if (pdf_light > EPS_SMALL) {
                float p_omega_w = std::max(EPS_SMALL, brdf_pdf(material, wo, light_sample.wi_world, normal));
                float w_light = is_delta ? 1.0f : pdf_light / (pdf_light + p_omega_w);

                Ray shadow_ray(offset_ray_origin(p, normal), light_sample.wi_world);
                bool hit_shadow = any_hit_combined(shadow_ray, light_sample.light_dist, bvh, scene.triangles, scene.spheres);

                if (!hit_shadow) {
                    Vec3f f = brdf_eval(material, wo, light_sample.wi_world, normal);
                    Vec3f emitted = eval_light(light, -light_sample.wi_world, light_sample.light_dist);
                    if (emitted.maxCoeff() > 0.0f) {
                        Vec3f contribution = emitted.cwiseProduct(f) * std::max(0.0f, normal.dot(light_sample.wi_world)) / pdf_light;
                        L_dir += w_light * contribution;
                    }
                }
            }
        }
    }


    Vec3f env_dir = Sampler::sample_sphere();
    constexpr float pdf_env = 1.0f / (4.0f * M_PI);

    float brdf_pdf_env = brdf_pdf(material, wo, env_dir, normal);
    float w_env = pdf_env / (pdf_env + brdf_pdf_env);

    Ray env_ray(offset_ray_origin(p, normal), env_dir);
    bool blocked = any_hit_combined(env_ray, std::numeric_limits<float>::infinity(), bvh, scene.triangles, scene.spheres);
    if (!blocked) {
        Vec3f f = brdf_eval(material, wo, env_dir, normal);
        float cos_term = std::max(0.0f, normal.dot(env_dir));
        L_dir += w_env * scene.environment_color.cwiseProduct(f) * (cos_term / pdf_env);
    }


    const auto [ray_dir, pdf] = brdf_sample(material, wo, normal, Sampler::next2d());

    Vec3f L_indir = Vec3f::Zero();

    if (pdf > EPS_SMALL && ray_dir.squaredNorm() > 0.0f) {
        Ray brdf_ray(offset_ray_origin(p, normal), ray_dir);
        const auto [is_ray_hit, t_min, prim_type, idx, hit_normal] = closest_hit_combined(brdf_ray, bvh, scene.triangles, scene.spheres);

        bool hit_is_emitter = false;
        int emitter_idx = -1;

        if (is_ray_hit) {
            switch (prim_type) {
                case PrimitiveType::Sphere:
                    hit_is_emitter = scene.spheres[idx].is_emitter();
                    if (hit_is_emitter) emitter_idx = scene.sphere_to_light[idx];
                    break;
                case PrimitiveType::Triangle:
                    hit_is_emitter = scene.triangles[idx].is_emitter();
                    if (hit_is_emitter) emitter_idx = scene.triangle_to_light[idx];
                    break;
            }
        }

        if (is_ray_hit && hit_is_emitter) {
            Light emitter = scene.lights[emitter_idx];

            float w_brdf = 1.0f;
            if (!is_delta_light(emitter)) {
                float pdf_light_brdf = scene.light_pdf(emitter_idx, scene.light_select_pdf(emitter_idx), p, ray_dir, t_min);
                if (pdf_light_brdf > EPS_SMALL) {
                    w_brdf = pdf / (pdf + pdf_light_brdf);
                }
            }

            Vec3f f = brdf_eval(material, wo, ray_dir, normal);
            L_dir += w_brdf * eval_light(emitter, -ray_dir, t_min).cwiseProduct(f) * (std::max(0.0f, normal.dot(ray_dir)) / pdf);
        } else if (!is_ray_hit) {
            constexpr float pdf_env = 1.0f / (4.0f * M_PI);
            float w_brdf = pdf / (pdf + pdf_env);
            Vec3f f = brdf_eval(material, wo, ray_dir, normal);
            L_dir += w_brdf * scene.environment_color.cwiseProduct(f) * (std::max(0.0f, normal.dot(ray_dir)) / pdf);
        }

        const float p_rr = 0.8f;
        float ksi = Sampler::next1d();

        if (ksi < p_rr) {
            if (is_ray_hit && !hit_is_emitter) {
                const Vec3f next_hit_pos = brdf_ray.at(t_min);
                Vec3f next_normal = (prim_type == PrimitiveType::Sphere) ? hit_normal : scene.triangles[idx].normal;

                HitInfo next_hit;
                next_hit.type = prim_type;
                next_hit.index = idx;
                next_hit.position = next_hit_pos;
                next_hit.normal = next_normal;
                next_hit.wo = -ray_dir;

                Vec3f f = brdf_eval(material, wo, ray_dir, normal);
                L_indir = shade_mis(next_hit).cwiseProduct(f) * (std::max(0.0f, normal.dot(ray_dir)) / (pdf * p_rr));
            }
        }
    }

    return L_dir + L_indir;
}

Vec3f mis_path_trace(Ray ray, int index) {
    const auto [is_hit, t_min, prim_type, idx, hit_normal] =
        closest_hit_combined(ray, bvh, scene.triangles, scene.spheres);

    if (!is_hit) return scene.environment_color;

    const Vec3f hit_pos = ray.at(t_min);
    Vec3f normal, emission, albedo;
    int material_id;

    switch (prim_type) {
        case PrimitiveType::Sphere: {
            const Sphere& sphere = scene.spheres[idx];
            normal = hit_normal;  // Already computed by ray_sphere_intersect
            emission = sphere.emission;
            material_id = sphere.material_id;
            break;
        }
        case PrimitiveType::Triangle: {
            const Triangle& tri = scene.triangles[idx];
            normal = tri.normal;
            emission = tri.emission;
            material_id = tri.material_id;
            break;
        }
    }

    albedo = material_albedo(scene.materials[material_id]);

    buffers.hit_count[index]++;
    buffers.normal[index] += normal;
    buffers.albedo[index] += albedo;
    buffers.world_pos[index] += hit_pos;
    buffers.depth[index] += t_min;

    // Check if hit an emitter
    bool hit_is_emitter = false;
    int light_idx = -1;

    switch (prim_type) {
        case PrimitiveType::Sphere:
            hit_is_emitter = scene.spheres[idx].is_emitter();
            if (hit_is_emitter) light_idx = scene.sphere_to_light[idx];
            break;
        case PrimitiveType::Triangle:
            hit_is_emitter = scene.triangles[idx].is_emitter();
            if (hit_is_emitter) light_idx = scene.triangle_to_light[idx];
            break;
    }

    if (hit_is_emitter) {
        return eval_light(scene.lights[light_idx], -ray.direction, t_min);
    }

    HitInfo hit;
    hit.type = prim_type;
    hit.index = idx;
    hit.position = hit_pos;
    hit.normal = normal;
    hit.wo = -ray.direction.normalized();

    return shade_mis(hit);
}

void render_image(const RenderConfig& config, const Scene& scene, const Camera& camera) {
    buffers.init(config.image_width, config.image_height);
    pixelStats.assign(config.image_width * config.image_height, PixelStats{});

    spdlog::info("Rendering...");
#ifdef _OPENMP
    spdlog::info("OpenMP threads: {}", omp_get_max_threads());
#endif
    auto start = std::chrono::high_resolution_clock::now();

    std::atomic<int> rows_completed{0};

#ifdef _OPENMP
#pragma omp parallel
    {
#pragma omp for schedule(dynamic, 1)
#endif
    for (int y = 0; y < config.image_height; ++y) {
        for (int x = 0; x < config.image_width; ++x) {
            buffers.radiance(x, y) = Vec3f::Zero();
            int idx = y * config.image_width + x;

            for (int s = 0; s < config.samples_per_pixel; ++s) {
                const float u = (x + Sampler::next1d()) / (config.image_width);
                const float v = (y + Sampler::next1d()) / (config.image_height);

                Ray ray = camera.generate_ray(u, (1.0f - v));
                Vec3f path_trace = mis_path_trace(ray, idx);
                
                accumulate_sample(pixelStats[idx], path_trace);

                buffers.radiance(x,y) += path_trace;
            }

            buffers.radiance(x,y) /= config.samples_per_pixel;
            buffers.average(idx);
        }

        int finished = ++rows_completed;
        if (finished % 50 == 0 || finished == config.image_height) {
            spdlog::info("Rendering progress: {} / {}", finished, config.image_height);
        }
    }
#ifdef _OPENMP
    }
#endif

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    spdlog::info("Rendering completed in {} ms", duration.count());
}

int main(int argc, char** argv) {
    CLI::App app{"Monte Carlo path tracer"};

    std::string config_file;
    std::string scene_name;
    std::string output_dir_cli;
    std::optional<uint32_t> sampler_seed;

    app.add_option("-c,--config", config_file, "Path to config YAML file")->required();
    app.add_option("-s,--scene", scene_name, "Name of the built-in scene to render (overrides config)");
    app.add_option("-o,--output", output_dir_cli, "Output directory (absolute or relative). Image saved as <dir>/<name>.png");
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

    const std::filesystem::path default_output_root("output");
    std::filesystem::path output_dir_path;

    if (!output_dir_cli.empty()) {
        output_dir_path = std::filesystem::path(output_dir_cli);
        if (render_config.output_dir && output_dir_cli != *render_config.output_dir) {
            spdlog::info("CLI output '{}' overrides config output '{}'", output_dir_cli, *render_config.output_dir);
        }
    } else if (render_config.output_dir) {
        output_dir_path = std::filesystem::path(*render_config.output_dir);
        if (!output_dir_path.is_absolute()) {
            output_dir_path = default_output_root / output_dir_path;
        }
    } else {
        spdlog::error("No output directory specified. Provide --output or set 'output' in the config.");
        return 1;
    }

    spdlog::info("Image: {}x{}", render_config.image_width, render_config.image_height);
    spdlog::info("Samples per pixel: {}", render_config.samples_per_pixel);
    spdlog::info("Scene: {}", scene_to_render);
    spdlog::info("Output directory: {}", output_dir_path.string());

    try {
        scene = scenes::load_scene(scene_to_render);
    } catch (const std::exception& e) {
        spdlog::error("Failed to load scene '{}': {}", scene_to_render, e.what());
        return 1;
    }

    bvh.build_bvh(scene.triangles);

    spdlog::info("Loaded scene with {} triangles, {} spheres, {} materials, {} lights",
        scene.triangle_count(), scene.sphere_count(), scene.material_count(), scene.lights.size());

    Camera camera;
    camera.init(render_config.camera_position,
                render_config.camera_direction,
                render_config.fov,
                float(render_config.image_width) / render_config.image_height,
                render_config.focus_distance,
                render_config.aperture,
                render_config.camera_up);

    uint32_t seed = sampler_seed.value_or(std::random_device{}());
    if (sampler_seed) {
        spdlog::info("Using user-provided sampler seed {}", seed);
    } else {
        spdlog::info("Using random sampler seed {}", seed);
    }
    Sampler::init(seed);

    render_image(render_config, scene, camera);

    output_dir_path = output_dir_path.lexically_normal();

    if (output_dir_path.empty()) {
        spdlog::error("Resolved output directory is empty");
        return 1;
    }

    const std::string output_dir_name = output_dir_path.filename().string();
    if (output_dir_name.empty() || output_dir_name == "." || output_dir_name == "/") {
        spdlog::error("Output directory '{}' must include a leaf name to derive the PNG filename", output_dir_path.string());
        return 1;
    }

    std::error_code exists_ec;
    if (std::filesystem::exists(output_dir_path, exists_ec)) {
        if (!std::filesystem::is_directory(output_dir_path, exists_ec)) {
            spdlog::error("Output path '{}' exists but is not a directory", output_dir_path.string());
            return 1;
        }

        std::error_code dir_iter_ec;
        for (const auto& entry : std::filesystem::directory_iterator(output_dir_path, dir_iter_ec)) {
            if (dir_iter_ec) {
                spdlog::warn("Failed to enumerate existing contents of '{}': {}", output_dir_path.string(), dir_iter_ec.message());
                break;
            }
            std::error_code remove_ec;
            std::filesystem::remove_all(entry.path(), remove_ec);
            if (remove_ec) {
                spdlog::warn("Failed to remove '{}': {}", entry.path().string(), remove_ec.message());
            }
        }
    } else {
        if (exists_ec) {
            spdlog::error("Failed to check output directory '{}': {}", output_dir_path.string(), exists_ec.message());
            return 1;
        }
        std::error_code create_ec;
        if (!std::filesystem::create_directories(output_dir_path, create_ec) && create_ec) {
            spdlog::error("Failed to create output directory '{}': {}", output_dir_path.string(), create_ec.message());
            return 1;
        }
    }

    const std::filesystem::path output_path = output_dir_path / output_dir_name;

    if (output_buffers(buffers, output_path.string())) {
        spdlog::info("Successfully saved output to: {}", output_dir_path.string());
        return 0;
    }

    spdlog::error("Failed to save image");
    return 1;
}
