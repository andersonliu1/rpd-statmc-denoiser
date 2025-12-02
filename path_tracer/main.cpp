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
#include <limits>
#include <cctype>

#ifdef _OPENMP
#include <omp.h>
#endif

#include "CLI/CLI.hpp"
#include "spdlog/spdlog.h"
#include "yaml-cpp/yaml.h"
#include "shared/global.h"
#include "shared/image.h"

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
std::vector<PixelStats> pixel_stats;
rpf::Grid rpf_grid{};

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

std::optional<Image::ToneMapping> tonemap_from_string(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (name == "aces") return Image::ToneMapping::ACES;
    if (name == "agx" || name == "agx-default" || name == "agx_default") return Image::ToneMapping::AGXDefault;
    if (name == "agx-golden" || name == "agx_golden") return Image::ToneMapping::AGXGolden;
    if (name == "agx-punchy" || name == "agx_punchy") return Image::ToneMapping::AGXPunchy;
    return std::nullopt;
}

Vec3f shade_mis(const HitInfo& hit, rpf::Sample* rpf_sample = nullptr) {
    const Material& material = hit.get_material();
    const Vec3f& p = hit.position;
    const Vec3f& wo = hit.wo;
    const Vec3f& normal = hit.normal;

    Vec3f L_dir = Vec3f::Zero();

    const auto [light_index, light_select_pdf] = scene.sample_light(Sampler::next1d());

    if (light_index >= 0) {
        Light light = scene.lights[light_index];
        bool is_delta = is_delta_light(light);
        Vec2f light_u = Sampler::next2d();
        if (rpf_sample && !rpf_sample->valid) {
            rpf_sample->light_u = light_u;
        }

        auto sample_light = [&](const auto& light_obj) {
            using T = std::decay_t<decltype(light_obj)>;
            if constexpr (std::is_same_v<T, RectLight> || std::is_same_v<T, AreaLight>) {
                return light_obj.sample(p, light_u);
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

    Vec2f brdf_u = Sampler::next2d();
    if (rpf_sample && !rpf_sample->valid) {
        rpf_sample->brdf_u = brdf_u;
    }
    const auto [ray_dir, pdf] = brdf_sample(material, wo, normal, brdf_u);

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
        if (rpf_sample && !rpf_sample->valid) {
            rpf_sample->rr_u = ksi;
        }

        if (ksi < p_rr) {
            if (is_ray_hit && !hit_is_emitter) {
                const Vec3f next_hit_pos = brdf_ray.at(t_min);
                Vec3f next_normal = (prim_type == PrimitiveType::Sphere) ? hit_normal : scene.triangles[idx].normal;

                HitInfo next_hit{prim_type, idx, next_hit_pos, next_normal, -ray_dir};

                Vec3f f = brdf_eval(material, wo, ray_dir, normal);
                L_indir = shade_mis(next_hit, rpf_sample).cwiseProduct(f) * (std::max(0.0f, normal.dot(ray_dir)) / (pdf * p_rr));
            }
        }
    }

    if (rpf_sample && !rpf_sample->valid) {
        rpf_sample->valid = true;
    }

    return L_dir + L_indir;
}

Vec3f mis_path_trace(Ray ray, int index, rpf::Sample* rpf_sample = nullptr) {
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

    HitInfo hit{prim_type, idx, hit_pos, normal, -ray.direction.normalized()};

    return shade_mis(hit, rpf_sample);
}

void statmc_denoise(const RenderConfig& config) {
    const int w = config.image_width;
    const int h = config.image_height;
    const int window_radius = config.statmc_window_radius;
    const float normal_threshold = config.statmc_normal_threshold;
    const float depth_threshold = config.statmc_depth_threshold;
    const float compat_sigma = config.statmc_compat_sigma;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int idx = y * w + x;
            if (buffers.hit_count[idx] == 0) continue;

            const Vec3f normal_i = buffers.normal[idx];
            const float depth_i = buffers.depth[idx];
            const Vec3f mean_color_i = pixel_stats[idx].color_mean;
            const float mean_lum_i = pixel_stats[idx].mean;
            const float var_i = std::max(EPS_SMALL, pixel_stats[idx].variance());
            const float sensitivity = buffers.sensitivity[idx];

            float weight_sum = 0.0f;
            Vec3f color_sum = Vec3f::Zero();
            float var_sum = 0.0f;
            int neighbor_count = 0;

            for (int dy = -window_radius; dy <= window_radius; ++dy) {
                int yy = y + dy;
                if (yy < 0 || yy >= h) continue;
                for (int dx = -window_radius; dx <= window_radius; ++dx) {
                    int xx = x + dx;
                    if (xx < 0 || xx >= w || (dx == 0 && dy == 0)) continue;

                    int neighbor_idx = yy * w + xx;
                    if (buffers.hit_count[neighbor_idx] == 0) continue;

                    const Vec3f normal_j = buffers.normal[neighbor_idx];
                    if (normal_i.dot(normal_j) < normal_threshold) continue;

                    const float depth_j = buffers.depth[neighbor_idx];
                    float depth_gate = std::abs(depth_i - depth_j) / std::max({depth_i, depth_j, EPS_SMALL});
                    if (depth_gate > depth_threshold) continue;

                    const float mean_lum_j = pixel_stats[neighbor_idx].mean;
                    const float var_j = std::max(EPS_SMALL, pixel_stats[neighbor_idx].variance());
                    if (std::abs(mean_lum_i - mean_lum_j) > compat_sigma * std::sqrt(var_i + var_j)) continue;

                    const float weight_j = 1.0f / var_j;
                    color_sum += pixel_stats[neighbor_idx].color_mean * weight_j;
                    var_sum += var_j;
                    weight_sum += weight_j;
                    neighbor_count++;
                }
            }

            Vec3f mean_color_neighbors = mean_color_i;
            float var_neighbors = var_i;
            if (neighbor_count > 0 && weight_sum > EPS_SMALL) {
                mean_color_neighbors = color_sum / weight_sum;
                var_neighbors = var_sum / float(neighbor_count);
            }

            const float shrinkage_k = config.statmc_shrinkage_k;
            float alpha_base = var_i / (var_i + shrinkage_k);
            float alpha = std::clamp(std::pow(alpha_base, 1.0f - sensitivity), 0.0f, 1.0f);

            Vec3f denoised = lerp(mean_color_i, mean_color_neighbors, 1.0f - alpha);
            float uncertainty = alpha * alpha * var_i + (1.0f - alpha) * (1.0f - alpha) * var_neighbors;

            buffers.denoised(x, y) = denoised;
            buffers.uncertainty[idx] = uncertainty;
        }
    }
>>>>>>> 8c71288 (adaptive modeling)
}

void render_image(const RenderConfig& config, const Scene& scene, const Camera& camera) {
    buffers.init(config.image_width, config.image_height);
    pixel_stats.assign(config.image_width * config.image_height, PixelStats{});

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
            int idx = y * config.image_width + x;

            for (int s = 0; s < config.samples_per_pixel; ++s) {
                const float u = (x + Sampler::next1d()) / (config.image_width);
                const float v = (y + Sampler::next1d()) / (config.image_height);

                Vec2f lens_sample = Sampler::sample_disk();
                Ray ray = camera.generate_ray(u, (1.0f - v), lens_sample);
                Vec3f path_trace = mis_path_trace(ray, idx);

                accumulate_sample(pixel_stats[idx], path_trace);

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

void render_image_statmc(const RenderConfig& config, const Scene& scene, const Camera& camera) {
    buffers.init(config.image_width, config.image_height);
    pixel_stats.assign(config.image_width * config.image_height, PixelStats{});
    rpf_grid.init(config.image_width, config.image_height, config.rpf_tile_size);
    rpf_grid.reset();

    spdlog::info("Rendering (StatMC/RPF)...");
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
            int idx = y * config.image_width + x;

            for (int s = 0; s < config.samples_per_pixel; ++s) {
                const float u = (x + Sampler::next1d()) / (config.image_width);
                const float v = (y + Sampler::next1d()) / (config.image_height);

                Vec2f lens_sample = Sampler::sample_disk();
                rpf::Sample rpf_sample{};
                rpf_sample.lens_u = lens_sample;

                Ray ray = camera.generate_ray(u, (1.0f - v), lens_sample);
                Vec3f path_trace = mis_path_trace(ray, idx, &rpf_sample);
                
                if (rpf_sample.valid) {
                    const float sample_lum = calc_luminance(path_trace);
                    if (std::isfinite(sample_lum)) {
                        const int tile_idx = rpf_grid.id_from_pixel(x, y);
#ifdef _OPENMP
#pragma omp critical
#endif
                        {
                            auto& tile = rpf_grid.tiles[tile_idx];
                            tile.brdf.add(rpf_sample.brdf_u.x(), rpf_sample.brdf_u.y(), sample_lum);
                            tile.lens.add(rpf_sample.lens_u.x(), rpf_sample.lens_u.y(), sample_lum);
                            tile.light.add(rpf_sample.light_u.x(), rpf_sample.light_u.y(), sample_lum);
                            tile.rr.add(rpf_sample.rr_u, sample_lum);
                        }
                    }
                }

                accumulate_sample(pixel_stats[idx], path_trace);

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

    for (int ty = 0; ty < rpf_grid.tiles_y; ++ty) {
        for (int tx = 0; tx < rpf_grid.tiles_x; ++tx) {
            const int x0 = tx * rpf_grid.tile_size;
            const int y0 = ty * rpf_grid.tile_size;
            const int x1 = std::min(config.image_width, x0 + rpf_grid.tile_size);
            const int y1 = std::min(config.image_height, y0 + rpf_grid.tile_size);
            const int tile_pixels = (x1 - x0) * (y1 - y0);
            const float expected_hits = float(tile_pixels) * config.samples_per_pixel;
            int hit_sum = 0;
            for (int yy = y0; yy < y1; ++yy) {
                for (int xx = x0; xx < x1; ++xx) {
                    hit_sum += buffers.hit_count[yy * config.image_width + xx];
                }
            }
            if (hit_sum == 0) continue;

            const rpf::Tile& base_tile = rpf_grid(tx, ty);
            if (base_tile.brdf.n == 0) continue;

            rpf::Tile pooled = base_tile;
            const int target_samples = (config.rpf_target_samples > 0) ? config.rpf_target_samples : std::max(rpf::Grid::MIN_SAMPLES, int(0.5f * expected_hits));
            int max_radius = 0;
            if (pooled.brdf.n < target_samples && expected_hits > 0.0f) {
                const int radius_cap = (config.rpf_max_radius >= 0) ? config.rpf_max_radius : rpf::Grid::MAX_RADIUS;
                if (config.rpf_max_radius >= 0) {
                    max_radius = radius_cap;
                } else {
                    float ratio = float(target_samples) / std::max(1.0f, float(pooled.brdf.n));
                    max_radius = std::min(radius_cap, std::max(1, int(std::ceil(std::sqrt(ratio) - 1.0f))));
                }
                const int grid_bound = std::max(0, std::min(rpf_grid.tiles_x, rpf_grid.tiles_y) - 1);
                max_radius = std::min(max_radius, grid_bound);
            }
            int combined_n = pooled.brdf.n;
            for (int radius = 1; radius <= max_radius && combined_n < target_samples; ++radius) {
                int min_y = std::max(0, ty - radius);
                int max_y = std::min(rpf_grid.tiles_y - 1, ty + radius);
                int min_x = std::max(0, tx - radius);
                int max_x = std::min(rpf_grid.tiles_x - 1, tx + radius);
                for (int ny = min_y; ny <= max_y; ++ny) {
                    for (int nx = min_x; nx <= max_x; ++nx) {
                        int dx = nx - tx;
                        int dy = ny - ty;
                        if (std::max(std::abs(dx), std::abs(dy)) != radius) continue;
                        const auto& neighbor = rpf_grid(nx, ny);
                        pooled.brdf.merge(neighbor.brdf);
                        pooled.lens.merge(neighbor.lens);
                        pooled.light.merge(neighbor.light);
                        pooled.rr.merge(neighbor.rr);
                    }
                }
                combined_n = pooled.brdf.n;
            }
            const float S = pooled.computeTileSensitivity();
            const float coverage = std::clamp(hit_sum / expected_hits, 0.0f, 1.0f);
            const float S_scaled = S * coverage;
            for (int yy = y0; yy < y1; ++yy) {
                for (int xx = x0; xx < x1; ++xx) {
                    buffers.sensitivity[yy * config.image_width + xx] = S_scaled;
                }
            }
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    spdlog::info("Rendering completed in {} ms", duration.count());
}

int main(int argc, char** argv) {
    CLI::App app{"Monte Carlo path tracer"};

    std::string config_file;
    std::string scene_name;
    std::string output_dir_cli;
    bool statmc_enable_cli = false;
    bool statmc_disable_cli = false;
    int rpf_tile_size_cli = -1;
    int rpf_target_samples_cli = std::numeric_limits<int>::min();
    int rpf_max_radius_cli = std::numeric_limits<int>::min();
    int statmc_window_radius_cli = std::numeric_limits<int>::min();
    float statmc_normal_thresh_cli = std::numeric_limits<float>::quiet_NaN();
    float statmc_depth_thresh_cli = std::numeric_limits<float>::quiet_NaN();
    float statmc_compat_sigma_cli = std::numeric_limits<float>::quiet_NaN();
    float statmc_shrinkage_k_cli = std::numeric_limits<float>::quiet_NaN();
    std::string tonemap_cli;
    std::optional<uint32_t> sampler_seed;

    // Rendering / scene
    app.add_option("-c,--config", config_file, "Path to config YAML file")->required();
    app.add_option("-s,--scene", scene_name, "Name of the built-in scene to render (overrides config)");
    app.add_option("-o,--output", output_dir_cli, "Output directory (absolute or relative). Image saved as <dir>/<name>.png");
    // StatMC / RPF
    app.add_flag("--statmc", statmc_enable_cli, "Enable StatMC/RPF rendering");
    app.add_flag("--no-statmc", statmc_disable_cli, "Disable StatMC/RPF rendering");
    app.add_option("--rpf-tile-size", rpf_tile_size_cli, "Tile size for RPF (StatMC)")->check(CLI::PositiveNumber);
    app.add_option("--rpf-target-samples", rpf_target_samples_cli, "Target pooled samples per tile for RPF (-1 = auto)"); // manual validation to allow -1
    app.add_option("--rpf-max-radius", rpf_max_radius_cli, "Max pooling radius (tiles) for RPF (-1 = auto)");
    app.add_option("--statmc-window-radius", statmc_window_radius_cli, "StatMC denoiser window radius (pixels)")->check(CLI::PositiveNumber);
    app.add_option("--statmc-normal-thresh", statmc_normal_thresh_cli, "StatMC normal dot threshold (0,1]");
    app.add_option("--statmc-depth-thresh", statmc_depth_thresh_cli, "StatMC relative depth threshold (>=0)");
    app.add_option("--statmc-compat-sigma", statmc_compat_sigma_cli, "StatMC compatibility sigma (>0)");
    app.add_option("--statmc-shrinkage-k", statmc_shrinkage_k_cli, "StatMC shrinkage stabilizer (>0)");
    app.add_option("--tonemap", tonemap_cli, "Tonemapping preset: aces | agx | agx-golden | agx-punchy");
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

    if (statmc_enable_cli && statmc_disable_cli) {
        spdlog::error("Cannot specify both --statmc and --no-statmc");
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

    bool use_statmc = render_config.use_statmc;
    if (statmc_enable_cli) use_statmc = true;
    if (statmc_disable_cli) use_statmc = false;
    const int rpf_tile_size = (rpf_tile_size_cli > 0) ? rpf_tile_size_cli : render_config.rpf_tile_size;
    if (rpf_tile_size <= 0) {
        spdlog::error("RPF tile size must be positive");
        return 1;
    }
    int rpf_target_samples = render_config.rpf_target_samples;
    if (rpf_target_samples_cli != std::numeric_limits<int>::min()) {
        rpf_target_samples = rpf_target_samples_cli;
    }
    if (rpf_target_samples == 0 || rpf_target_samples < -1) {
        spdlog::error("rpf_target_samples must be -1 or positive");
        return 1;
    }
    int rpf_max_radius = render_config.rpf_max_radius;
    if (rpf_max_radius_cli != std::numeric_limits<int>::min()) {
        rpf_max_radius = rpf_max_radius_cli;
    }
    if (rpf_max_radius < -1) {
        spdlog::error("rpf_max_radius must be -1 or non-negative");
        return 1;
    }
    int statmc_window_radius = render_config.statmc_window_radius;
    if (statmc_window_radius_cli != std::numeric_limits<int>::min()) {
        statmc_window_radius = statmc_window_radius_cli;
    }
    if (statmc_window_radius <= 0) {
        spdlog::error("statmc_window_radius must be positive");
        return 1;
    }
    float statmc_normal_thresh = std::isnan(statmc_normal_thresh_cli) ? render_config.statmc_normal_threshold : statmc_normal_thresh_cli;
    if (statmc_normal_thresh <= 0.0f || statmc_normal_thresh > 1.0f) {
        spdlog::error("statmc_normal_thresh must be in (0,1]");
        return 1;
    }
    float statmc_depth_thresh = std::isnan(statmc_depth_thresh_cli) ? render_config.statmc_depth_threshold : statmc_depth_thresh_cli;
    if (statmc_depth_thresh < 0.0f) {
        spdlog::error("statmc_depth_thresh must be non-negative");
        return 1;
    }
    float statmc_compat_sigma = std::isnan(statmc_compat_sigma_cli) ? render_config.statmc_compat_sigma : statmc_compat_sigma_cli;
    if (statmc_compat_sigma <= 0.0f) {
        spdlog::error("statmc_compat_sigma must be positive");
        return 1;
    }
    float statmc_shrinkage_k = std::isnan(statmc_shrinkage_k_cli) ? render_config.statmc_shrinkage_k : statmc_shrinkage_k_cli;
    if (statmc_shrinkage_k <= 0.0f) {
        spdlog::error("statmc_shrinkage_k must be positive");
        return 1;
    }
    std::string tonemap_name = render_config.tonemap;
    if (!tonemap_cli.empty()) {
        if (render_config.tonemap != tonemap_cli) {
            spdlog::info("CLI tonemap '{}' overrides config tonemap '{}'", tonemap_cli, render_config.tonemap);
        }
        tonemap_name = tonemap_cli;
    }
    auto tonemap_parsed = tonemap_from_string(tonemap_name);
    if (!tonemap_parsed) {
        spdlog::error("Unknown tonemap preset '{}'. Allowed: aces, agx, agx-golden, agx-punchy", tonemap_name);
        return 1;
    }
    render_config.tonemap = tonemap_name;
    Image::ToneMapping tonemap_preset = *tonemap_parsed;

    render_config.use_statmc = use_statmc;
    render_config.rpf_tile_size = rpf_tile_size;
    render_config.rpf_target_samples = rpf_target_samples;
    render_config.rpf_max_radius = rpf_max_radius;
    render_config.statmc_window_radius = statmc_window_radius;
    render_config.statmc_normal_threshold = statmc_normal_thresh;
    render_config.statmc_depth_threshold = statmc_depth_thresh;
    render_config.statmc_compat_sigma = statmc_compat_sigma;
    render_config.statmc_shrinkage_k = statmc_shrinkage_k;

    spdlog::info("Image: {}x{}", render_config.image_width, render_config.image_height);
    spdlog::info("Samples per pixel: {}", render_config.samples_per_pixel);
    spdlog::info("Camera position: [{}, {}, {}]", render_config.camera_position.x(), render_config.camera_position.y(), render_config.camera_position.z());
    spdlog::info("Scene: {}", scene_to_render);
    spdlog::info("Output directory: {}", output_dir_path.string());
    spdlog::info("StatMC enabled: {}", render_config.use_statmc);
    spdlog::info("RPF tile size: {}", render_config.rpf_tile_size);
    spdlog::info("StatMC window radius: {}", render_config.statmc_window_radius);
    spdlog::info("Tonemap: {}", render_config.tonemap);

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

    if (render_config.use_statmc) {
        render_image_statmc(render_config, scene, camera);
        statmc_denoise(render_config);
    } else {
        render_image(render_config, scene, camera);
    }

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

    if (output_buffers(buffers, output_path.string(), tonemap_preset)) {
        spdlog::info("Successfully saved output to: {}", output_dir_path.string());
        return 0;
    }

    spdlog::error("Failed to save image");
    return 1;
}
