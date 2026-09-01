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
#include <cmath>
#include <numeric>

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
uint32_t g_sampler_base_seed = 0;

struct DerivedFeatureCache {
    bool valid = false;
    int width = 0;
    int height = 0;
    std::vector<Vec3f> normals_unit;
    std::vector<float> depth_avg;
    std::vector<Vec3f> albedo_avg;
};

struct SensitivityImageCache {
    bool valid = false;
    int width = 0;
    int height = 0;
    std::vector<float> all;
    std::vector<float> pixel;
    std::vector<float> pixel_conf;
    std::vector<float> brdf;
    std::vector<float> brdf_conf;
    std::vector<float> lens;
    std::vector<float> lens_conf;
    std::vector<float> light;
    std::vector<float> light_conf;
    std::vector<float> light_uv;
    std::vector<float> light_uv_conf;
    std::vector<float> light_select;
    std::vector<float> light_select_conf;
    std::vector<float> environment;
    std::vector<float> environment_conf;
    std::vector<float> rr;
    std::vector<float> rr_conf;
    std::vector<float> gradient;
};

DerivedFeatureCache g_feature_cache{};
SensitivityImageCache g_sensitivity_cache{};

static void invalidate_feature_cache() {
    g_feature_cache.valid = false;
}

static void invalidate_sensitivity_cache() {
    g_sensitivity_cache.valid = false;
}

static void invalidate_postprocess_caches() {
    invalidate_feature_cache();
    invalidate_sensitivity_cache();
}

static const DerivedFeatureCache& ensure_feature_cache(int w, int h) {
    if (g_feature_cache.valid && g_feature_cache.width == w && g_feature_cache.height == h) return g_feature_cache;

    g_feature_cache.width = w;
    g_feature_cache.height = h;
    g_feature_cache.normals_unit.assign(w * h, Vec3f::Zero());
    g_feature_cache.depth_avg.assign(w * h, 0.0f);
    g_feature_cache.albedo_avg.assign(w * h, Vec3f::Zero());

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int i = 0; i < w * h; ++i) {
        if (buffers.hit_count[i] > 0) {
            Vec3f n = buffers.normal[i] / float(buffers.hit_count[i]);
            if (n.squaredNorm() > EPS_SMALL) n.normalize();
            g_feature_cache.normals_unit[i] = n;
            g_feature_cache.depth_avg[i] = buffers.depth[i] / float(buffers.hit_count[i]);
            g_feature_cache.albedo_avg[i] = buffers.albedo[i] / float(buffers.hit_count[i]);
        } else {
            g_feature_cache.normals_unit[i] = buffers.normal[i];
            g_feature_cache.depth_avg[i] = buffers.depth[i];
            g_feature_cache.albedo_avg[i] = buffers.albedo[i];
        }
    }

    g_feature_cache.valid = true;
    return g_feature_cache;
}

static const SensitivityImageCache& ensure_sensitivity_cache(int w, int h) {
    if (g_sensitivity_cache.valid && g_sensitivity_cache.width == w && g_sensitivity_cache.height == h) return g_sensitivity_cache;

    g_sensitivity_cache.width = w;
    g_sensitivity_cache.height = h;
    g_sensitivity_cache.all = buffers.sensitivity_tiles.expand_values(buffers.sensitivity_tiles.all, w, h);
    g_sensitivity_cache.pixel = buffers.sensitivity_tiles.expand_values(buffers.sensitivity_tiles.pixel, w, h);
    g_sensitivity_cache.pixel_conf = buffers.sensitivity_tiles.expand_values(buffers.sensitivity_tiles.pixel_conf, w, h);
    g_sensitivity_cache.brdf = buffers.sensitivity_tiles.expand_values(buffers.sensitivity_tiles.brdf, w, h);
    g_sensitivity_cache.brdf_conf = buffers.sensitivity_tiles.expand_values(buffers.sensitivity_tiles.brdf_conf, w, h);
    g_sensitivity_cache.lens = buffers.sensitivity_tiles.expand_values(buffers.sensitivity_tiles.lens, w, h);
    g_sensitivity_cache.lens_conf = buffers.sensitivity_tiles.expand_values(buffers.sensitivity_tiles.lens_conf, w, h);
    g_sensitivity_cache.light = buffers.sensitivity_tiles.expand_values(buffers.sensitivity_tiles.light, w, h);
    g_sensitivity_cache.light_conf = buffers.sensitivity_tiles.expand_values(buffers.sensitivity_tiles.light_conf, w, h);
    g_sensitivity_cache.light_uv = buffers.sensitivity_tiles.expand_values(buffers.sensitivity_tiles.light_uv, w, h);
    g_sensitivity_cache.light_uv_conf = buffers.sensitivity_tiles.expand_values(buffers.sensitivity_tiles.light_uv_conf, w, h);
    g_sensitivity_cache.light_select = buffers.sensitivity_tiles.expand_values(buffers.sensitivity_tiles.light_select, w, h);
    g_sensitivity_cache.light_select_conf = buffers.sensitivity_tiles.expand_values(buffers.sensitivity_tiles.light_select_conf, w, h);
    g_sensitivity_cache.environment = buffers.sensitivity_tiles.expand_values(buffers.sensitivity_tiles.environment, w, h);
    g_sensitivity_cache.environment_conf = buffers.sensitivity_tiles.expand_values(buffers.sensitivity_tiles.environment_conf, w, h);
    g_sensitivity_cache.rr = buffers.sensitivity_tiles.expand_values(buffers.sensitivity_tiles.rr, w, h);
    g_sensitivity_cache.rr_conf = buffers.sensitivity_tiles.expand_values(buffers.sensitivity_tiles.rr_conf, w, h);
    g_sensitivity_cache.gradient = buffers.sensitivity_gradient;

    if (g_sensitivity_cache.gradient.size() != static_cast<size_t>(w * h)) {
        g_sensitivity_cache.gradient.assign(w * h, 0.0f);
    }

    g_sensitivity_cache.valid = true;
    return g_sensitivity_cache;
}

static float sample_scalar_local_average(const std::vector<float>& values, int width, int height, int cx, int cy, int radius = 1) {
    double accum = 0.0;
    double weight_sum = 0.0;
    for (int dy = -radius; dy <= radius; ++dy) {
        const int y = std::clamp(cy + dy, 0, height - 1);
        for (int dx = -radius; dx <= radius; ++dx) {
            const int x = std::clamp(cx + dx, 0, width - 1);
            const double weight = 1.0 / (1.0 + double(dx * dx + dy * dy));
            accum += weight * static_cast<double>(values[y * width + x]);
            weight_sum += weight;
        }
    }
    return (weight_sum > 0.0) ? static_cast<float>(accum / weight_sum) : 0.0f;
}

static Vec3f sample_vec3_local_average(const std::vector<Vec3f>& values, int width, int height, int cx, int cy, int radius = 1) {
    Vec3d accum = Vec3d::Zero();
    double weight_sum = 0.0;
    for (int dy = -radius; dy <= radius; ++dy) {
        const int y = std::clamp(cy + dy, 0, height - 1);
        for (int dx = -radius; dx <= radius; ++dx) {
            const int x = std::clamp(cx + dx, 0, width - 1);
            const double weight = 1.0 / (1.0 + double(dx * dx + dy * dy));
            accum += values[y * width + x].cast<double>() * weight;
            weight_sum += weight;
        }
    }
    if (weight_sum <= 0.0) return Vec3f::Zero();
    return (accum / weight_sum).cast<float>();
}

// Two-sided t critical value.
static float lookup_t_critical(float df, float alpha) {
    df = std::max(df, 1.0f);
    alpha = std::clamp(alpha, EPS_SMALL, 0.999999f);

    auto interpolate_table = [df](const float (&table)[30]) {
        const int idx_lo = std::clamp(static_cast<int>(std::floor(df)), 1, 30);
        const int idx_hi = std::clamp(static_cast<int>(std::ceil(df)), 1, 30);
        if (idx_lo == idx_hi) return table[idx_lo - 1];
        return lerp(table[idx_lo - 1], table[idx_hi - 1],
                    (df - float(idx_lo)) / float(idx_hi - idx_lo));
    };

    if (df < 30.5f && std::abs(alpha - 0.05f) < EPS) {
        static constexpr float t05_table[30] = {
            12.706f, 4.303f, 3.182f, 2.776f, 2.571f, 2.447f, 2.365f, 2.306f, 2.262f, 2.228f,
            2.201f, 2.179f, 2.160f, 2.145f, 2.131f, 2.120f, 2.110f, 2.101f, 2.093f, 2.086f,
            2.080f, 2.074f, 2.069f, 2.064f, 2.060f, 2.056f, 2.052f, 2.048f, 2.045f, 2.042f};
        return interpolate_table(t05_table);
    }

    if (df < 30.5f && std::abs(alpha - 0.01f) < EPS) {
        static constexpr float t01_table[30] = {
            63.657f, 9.925f, 5.841f, 4.604f, 4.032f, 3.707f, 3.499f, 3.355f, 3.250f, 3.169f,
            3.106f, 3.055f, 3.012f, 2.977f, 2.947f, 2.921f, 2.898f, 2.878f, 2.861f, 2.845f,
            2.831f, 2.819f, 2.807f, 2.797f, 2.787f, 2.779f, 2.771f, 2.763f, 2.756f, 2.750f};
        return interpolate_table(t01_table);
    }

    auto normal_quantile = [](float a) {
        if (a <= 0.001f) return 3.29f;
        if (a <= 0.005f) return 2.81f;
        if (a <= 0.01f) return 2.58f;
        if (a <= 0.02f) return 2.33f;
        if (a <= 0.05f) return 1.96f;
        if (a <= 0.10f) return 1.64f;
        return 1.28f;
    };
    float z = normal_quantile(alpha);
    if (df < 30.0f) {
        const float adj = std::sqrt(df / std::max(1.0f, df - 1.0f));
        z *= adj;
    }
    return z;
}

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

};

std::optional<Image::ToneMapping> tonemap_from_string(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (name == "aces") return Image::ToneMapping::ACES;
    if (name == "agx" || name == "agx-default" || name == "agx_default") return Image::ToneMapping::AGXDefault;
    if (name == "agx-golden" || name == "agx_golden") return Image::ToneMapping::AGXGolden;
    if (name == "agx-punchy" || name == "agx_punchy") return Image::ToneMapping::AGXPunchy;
    return std::nullopt;
}

static float relative_depth_delta(float a, float b) {
    return std::abs(a - b) / std::max({std::abs(a), std::abs(b), EPS_SMALL});
}

static uint32_t hash_u32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static float hash_to_unit_float(uint32_t x) {
    return float(hash_u32(x) & 0x00ffffffu) / float(0x01000000u);
}

static uint32_t pixel_sample_seed(int pixel_index, int sample_offset) {
    return hash_u32(g_sampler_base_seed ^ hash_u32(static_cast<uint32_t>(pixel_index) + 0x9e3779b9u) ^
                    hash_u32(static_cast<uint32_t>(sample_offset) + 0x85ebca6bu));
}

static bool brdf_uses_random_sample(const Material& material) {
    return !brdf_is_delta(material);
}

struct SensitivityState {
    float all = 0.0f;
    float gradient = 0.0f;
    std::array<float, 6> components{};

    float reliable() const {
        return std::clamp(all, 0.0f, 1.0f);
    }

    float reliable_denoise() const {
        return std::clamp(all * (1.0f - gradient), 0.0f, 1.0f);
    }

    std::array<float, 6> components_denoise() const {
        std::array<float, 6> reliable = components;
        const float attenuation = 1.0f - gradient;
        for (float& value : reliable) value = std::clamp(value * attenuation, 0.0f, 1.0f);
        return reliable;
    }
};

static SensitivityState sample_sensitivity(const SensitivityImageCache& cache, int idx) {
    SensitivityState s;
    s.all = std::clamp(cache.all[idx], 0.0f, 1.0f);
    s.components = {
        std::clamp(cache.brdf[idx], 0.0f, 1.0f),
        std::clamp(cache.lens[idx], 0.0f, 1.0f),
        std::clamp(cache.light_uv[idx], 0.0f, 1.0f),
        std::clamp(cache.light_select[idx], 0.0f, 1.0f),
        std::clamp(cache.environment[idx], 0.0f, 1.0f),
        std::clamp(cache.rr[idx], 0.0f, 1.0f)};
    if (idx < static_cast<int>(cache.gradient.size())) {
        s.gradient = std::clamp(cache.gradient[idx], 0.0f, 1.0f);
    }
    return s;
}

static void accumulate_rpf_sample_to_grid(rpf::Grid& grid, int x, int y, const rpf::Sample& sample, float sample_lum_total) {
    if (!sample.pixel_valid && !sample.lens_valid && !sample.has_any_shading_data()) return;

    const float gx = float(x) / float(grid.step);
    const float gy = float(y) / float(grid.step);
    const int x0 = std::clamp(static_cast<int>(std::floor(gx)), 0, grid.nodes_x - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(gy)), 0, grid.nodes_y - 1);
    const int x1 = std::min(x0 + 1, grid.nodes_x - 1);
    const int y1 = std::min(y0 + 1, grid.nodes_y - 1);
    const float tx = std::clamp(gx - float(x0), 0.0f, 1.0f);
    const float ty = std::clamp(gy - float(y0), 0.0f, 1.0f);

    const struct NodeWeight { int nx; int ny; float w; } weights[4] = {
        {x0, y0, (1.0f - tx) * (1.0f - ty)},
        {x1, y0, tx * (1.0f - ty)},
        {x0, y1, (1.0f - tx) * ty},
        {x1, y1, tx * ty},
    };

    for (const auto& entry : weights) {
        if (entry.w <= EPS_SMALL) continue;
        auto& node = grid(entry.nx, entry.ny);
        if (sample.pixel_valid) {
            const float half_support = 0.5f * float(grid.support_size);
            const float support_left = float(entry.nx * grid.step) - half_support;
            const float support_top = float(entry.ny * grid.step) - half_support;
            const float screen_u = std::clamp(
                (float(x) + sample.pixel_u.x() - support_left) / float(grid.support_size), 0.0f, 1.0f);
            const float screen_v = std::clamp(
                (float(y) + sample.pixel_u.y() - support_top) / float(grid.support_size), 0.0f, 1.0f);
            node.pixel.add(screen_u, screen_v, sample_lum_total, entry.w);
            if (sample.brdf_valid) node.screen_brdf.add(screen_u, screen_v, sample_lum_total, entry.w);
            if (sample.lens_valid) node.screen_lens.add(screen_u, screen_v, sample_lum_total, entry.w);
            if (sample.light_valid) node.screen_light_uv.add(screen_u, screen_v, sample_lum_total, entry.w);
            if (sample.light_select_valid) node.screen_light_select.add(screen_u, screen_v, sample_lum_total, entry.w);
            if (sample.environment_valid) node.screen_environment.add(screen_u, screen_v, sample_lum_total, entry.w);
            if (sample.rr_valid) node.screen_rr.add(screen_u, screen_v, sample_lum_total, entry.w);
        }
        if (sample.lens_valid) node.lens.add(sample.lens_u.x(), sample.lens_u.y(), sample_lum_total, entry.w);
        if (sample.brdf_valid) node.brdf.add(sample.brdf_u.x(), sample.brdf_u.y(), sample_lum_total, entry.w);
        if (sample.light_valid) node.light_uv.add(sample.light_u.x(), sample.light_u.y(), sample_lum_total, entry.w);
        if (sample.environment_valid) {
            node.environment.add(sample.environment_u.x(), sample.environment_u.y(), sample_lum_total, entry.w);
        }
        if (sample.light_select_valid) node.light_select.add(sample.light_index, sample_lum_total, entry.w);
        if (sample.rr_valid) node.rr.add(sample.rr_survived ? 1 : 0, sample_lum_total, entry.w);
    }
}

static void merge_rpf_grids_into(rpf::Grid& dst, const std::vector<rpf::Grid>& sources) {
    const int node_count = dst.nodes_x * dst.nodes_y;
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int idx = 0; idx < node_count; ++idx) {
        for (const auto& source : sources) {
            dst.nodes[idx].merge(source.nodes[idx]);
        }
    }
}

/// @brief Estimates direct illumination with light and BRDF sampling.
/// @param hit Surface interaction to shade.
/// @param rpf_sample Optional random-parameter capture record.
/// @return Direct radiance leaving the surface.
Vec3f shade_mis(const HitInfo& hit, rpf::Sample* rpf_sample = nullptr) {
    const Material& material = hit.get_material();
    const Vec3f& p = hit.position;
    const Vec3f& wo = hit.wo;
    const Vec3f& normal = hit.normal;
    const bool delta_brdf = brdf_is_delta(material);

    Vec3f L_dir = Vec3f::Zero();

    const float light_select_u = Sampler::next1d();
    const auto [light_index, light_select_pdf] = scene.sample_light(light_select_u);
    if (rpf_sample) {
        rpf_sample->capture_light_select(
            light_index, light_index >= 0 && light_index < rpf::LIGHT_CATEGORY_BINS);
    }

    if (light_index >= 0 && (!delta_brdf || is_delta_light(scene.lights[light_index]))) {
        const Light& light = scene.lights[light_index];
        const bool is_delta = is_delta_light(light);
        Vec2f light_u = Sampler::next2d();
        if (rpf_sample) rpf_sample->capture_light_uv(light_u, !is_delta);

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
                        const float geometric_term = delta_brdf
                            ? 1.0f
                            : std::max(0.0f, normal.dot(light_sample.wi_world));
                        Vec3f contribution = emitted.cwiseProduct(f) * geometric_term / pdf_light;
                        const Vec3f weighted_contribution = w_light * contribution;
                        L_dir += weighted_contribution;
                    }
                }
            }
        }
    }

    const Vec2f environment_u = Sampler::next2d();
    if (scene.environment_color.maxCoeff() > 0.0f && !delta_brdf) {
        const Vec3f env_dir = Sampler::sample_sphere(environment_u);
        if (rpf_sample) rpf_sample->capture_environment(environment_u);
        constexpr float pdf_env = 1.0f / (4.0f * M_PI);
        const float brdf_pdf_env = brdf_pdf(material, wo, env_dir, normal);
        const float w_env = pdf_env / (pdf_env + brdf_pdf_env);
        const Ray env_ray(offset_ray_origin(p, normal), env_dir);
        const bool blocked = any_hit_combined(
            env_ray, std::numeric_limits<float>::infinity(), bvh, scene.triangles, scene.spheres);
        if (!blocked) {
            const Vec3f f = brdf_eval(material, wo, env_dir, normal);
            const float cos_term = std::max(0.0f, normal.dot(env_dir));
            L_dir += w_env * scene.environment_color.cwiseProduct(f) * (cos_term / pdf_env);
        }
    }

    Vec2f brdf_u = Sampler::next2d();
    if (rpf_sample && brdf_uses_random_sample(material)) rpf_sample->capture_brdf(brdf_u);
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
            const Light& emitter = scene.lights[emitter_idx];

            float w_brdf = 1.0f;
            if (!delta_brdf && !is_delta_light(emitter)) {
                float pdf_light_brdf = scene.light_pdf(emitter_idx, scene.light_select_pdf(emitter_idx), p, ray_dir, t_min);
                if (pdf_light_brdf > EPS_SMALL) {
                    w_brdf = pdf / (pdf + pdf_light_brdf);
                }
            }

            L_dir += w_brdf * eval_light(emitter, -ray_dir, t_min).cwiseProduct(
                brdf_sample_weight(material, wo, ray_dir, normal, pdf));
        } else if (!is_ray_hit) {
            constexpr float pdf_env = 1.0f / (4.0f * M_PI);
            const float w_brdf = delta_brdf ? 1.0f : pdf / (pdf + pdf_env);
            L_dir += w_brdf * scene.environment_color.cwiseProduct(
                brdf_sample_weight(material, wo, ray_dir, normal, pdf));
        }

        if (is_ray_hit && !hit_is_emitter) {
            const float p_rr = 0.8f;
            const float ksi = Sampler::next1d();
            if (rpf_sample) rpf_sample->capture_rr(ksi < p_rr);
            if (ksi < p_rr) {
                const Vec3f next_hit_pos = brdf_ray.at(t_min);
                Vec3f next_normal = (prim_type == PrimitiveType::Sphere) ? hit_normal : scene.triangles[idx].normal;

                HitInfo next_hit{prim_type, idx, next_hit_pos, next_normal, -ray_dir};

                L_indir = shade_mis(next_hit, rpf_sample).cwiseProduct(
                    brdf_sample_weight(material, wo, ray_dir, normal, pdf)) / p_rr;
            }
        }
    }

    return L_dir + L_indir;
}

/// @brief Recursively traces a path and records per-pixel features.
/// @param ray Initial ray.
/// @param index Pixel index receiving feature statistics.
/// @param rpf_sample Optional random-parameter capture record.
/// @return Estimated path radiance.
Vec3f mis_path_trace(Ray ray, int index, rpf::Sample* rpf_sample = nullptr) {
    const auto [is_hit, t_min, prim_type, idx, hit_normal] =
        closest_hit_combined(ray, bvh, scene.triangles, scene.spheres);
    if (!is_hit) return scene.environment_color;

    const Vec3f hit_pos = ray.at(t_min);
    Vec3f normal, albedo;
    int material_id;
    bool hit_is_emitter;
    int light_idx;

    switch (prim_type) {
        case PrimitiveType::Sphere: {
            const Sphere& sphere = scene.spheres[idx];
            normal = hit_normal;
            material_id = sphere.material_id;
            hit_is_emitter = sphere.is_emitter();
            light_idx = hit_is_emitter ? scene.sphere_to_light[idx] : -1;
            break;
        }
        case PrimitiveType::Triangle: {
            const Triangle& tri = scene.triangles[idx];
            normal = tri.normal;
            material_id = tri.material_id;
            hit_is_emitter = tri.is_emitter();
            light_idx = hit_is_emitter ? scene.triangle_to_light[idx] : -1;
            break;
        }
    }

    albedo = material_albedo(scene.materials[material_id]);

    buffers.hit_count[index]++;
    buffers.normal[index] += normal;
    buffers.albedo[index] += albedo;
    buffers.world_pos[index] += hit_pos;
    buffers.depth[index] += t_min;

    if (hit_is_emitter) {
        return eval_light(scene.lights[light_idx], -ray.direction, t_min);
    }

    HitInfo hit{prim_type, idx, hit_pos, normal, -ray.direction.normalized()};

    return shade_mis(hit, rpf_sample);
}

/// @brief Renders the uniform-sampling baseline.
/// @param config Render configuration.
/// @param camera Initialized camera.
void render_image(const RenderConfig& config, const Camera& camera) {
    buffers.init(config.image_width, config.image_height);
    invalidate_postprocess_caches();

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
            Sampler::init_thread(pixel_sample_seed(idx, 0));

            for (int s = 0; s < config.samples_per_pixel; ++s) {
                const Vec2f pixel_u = Sampler::next2d();
                const float u = (x + pixel_u.x()) / (config.image_width);
                const float v = (y + pixel_u.y()) / (config.image_height);

                const Vec2f lens_u = Sampler::next2d();
                const Vec2f lens_sample = camera.lens_radius > 0.0f
                    ? Sampler::sample_disk(lens_u)
                    : Vec2f::Zero();
                Ray ray = camera.generate_ray(u, (1.0f - v), lens_sample);
                Vec3f path_trace = mis_path_trace(ray, idx);
                if (!path_trace.allFinite()) continue;

                buffers.radiance(x,y) += path_trace;
                buffers.sample_count[idx]++;
            }
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

void recompute_sensitivity(const RenderConfig& config);

/// @brief Renders the initial StatMC samples and RPD observations.
/// @param config Render configuration.
/// @param camera Initialized camera.
void render_image_statmc(const RenderConfig& config, const Camera& camera) {
    const bool capture_rpd = config.rpf_shrinkage_scale > 0.0f;
    buffers.init(config.image_width, config.image_height);
    pixel_stats.assign(config.image_width * config.image_height, PixelStats{});
    rpf_grid.init(config.image_width, config.image_height, config.rpf_tile_size);
    rpf_grid.reset();
    invalidate_postprocess_caches();

    spdlog::info("Rendering (StatMC / RPF)...");
#ifdef _OPENMP
    spdlog::info("OpenMP threads: {}", omp_get_max_threads());
#endif
    auto start = std::chrono::high_resolution_clock::now();

    std::atomic<int> rows_completed{0};
    const int num_rpf_grids =
#ifdef _OPENMP
        omp_get_max_threads();
#else
        1;
#endif
    // ponytail: full per-thread grids avoid locks; use row stripes if RSS dominates.
    std::vector<rpf::Grid> local_rpf_grids(capture_rpd ? num_rpf_grids : 0);
    for (auto& grid : local_rpf_grids) {
        grid.init(config.image_width, config.image_height, config.rpf_tile_size);
        grid.reset();
    }

#ifdef _OPENMP
#pragma omp parallel
    {
        const int rpf_grid_index = omp_get_thread_num();
#pragma omp for schedule(dynamic, 1)
#else
    const int rpf_grid_index = 0;
#endif
    for (int y = 0; y < config.image_height; ++y) {
        for (int x = 0; x < config.image_width; ++x) {
            int idx = y * config.image_width + x;
            Sampler::init_thread(pixel_sample_seed(idx, pixel_stats[idx].n));

            for (int s = 0; s < config.samples_per_pixel; ++s) {
                const Vec2f pixel_u = Sampler::next2d();
                const float u = (x + pixel_u.x()) / (config.image_width);
                const float v = (y + pixel_u.y()) / (config.image_height);

                const Vec2f lens_u = Sampler::next2d();
                const Vec2f lens_sample = camera.lens_radius > 0.0f
                    ? Sampler::sample_disk(lens_u)
                    : Vec2f::Zero();
                rpf::Sample rpf_sample{};
                if (capture_rpd) {
                    rpf_sample.capture_pixel(pixel_u);
                    if (camera.lens_radius > 0.0f) rpf_sample.capture_lens(lens_u);
                }

                Ray ray = camera.generate_ray(u, (1.0f - v), lens_sample);
                Vec3f path_trace = mis_path_trace(ray, idx, capture_rpd ? &rpf_sample : nullptr);
                if (!path_trace.allFinite()) continue;
                
                if (capture_rpd) {
                    accumulate_rpf_sample_to_grid(
                        local_rpf_grids[rpf_grid_index], x, y, rpf_sample, calc_luminance(path_trace));
                }

                accumulate_sample(pixel_stats[idx], path_trace);

                buffers.radiance(x,y) += path_trace;
                buffers.sample_count[idx]++;
            }
        }

        int finished = ++rows_completed;
        if (finished % 50 == 0 || finished == config.image_height) {
            spdlog::info("Rendering progress: {} / {}", finished, config.image_height);
        }
    }
#ifdef _OPENMP
    }
#endif

    if (capture_rpd) merge_rpf_grids_into(rpf_grid, local_rpf_grids);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    spdlog::info("Rendering completed in {} ms", duration.count());

    recompute_sensitivity(config);
}

/// @brief Reconstructs confidence-shrunk RPD fields from grid statistics.
/// @param config Render configuration controlling confidence shrinkage.
void recompute_sensitivity(const RenderConfig& config) {
    const auto start = std::chrono::high_resolution_clock::now();
    const int w = config.image_width;
    const int h = config.image_height;
    const int nodes_x = rpf_grid.nodes_x;
    const int nodes_y = rpf_grid.nodes_y;
    const int step = rpf_grid.step;
    const int node_count = nodes_x * nodes_y;
    invalidate_sensitivity_cache();
    buffers.init_sensitivity(rpf_grid.support_size);
    if (config.rpf_shrinkage_scale <= 0.0f) {
        buffers.sensitivity_confidence.assign(w * h, 0.0f);
        buffers.sensitivity_gradient.assign(w * h, 0.0f);
        ensure_sensitivity_cache(w, h);
        return;
    }

    const auto& features = ensure_feature_cache(w, h);
    const auto& normals_unit = features.normals_unit;
    const auto& depth_avg = features.depth_avg;
    const auto& albedo_avg = features.albedo_avg;

    std::vector<float> raw_pixel(node_count, 0.0f), raw_brdf(node_count, 0.0f), raw_lens(node_count, 0.0f), raw_light_uv(node_count, 0.0f), raw_light_select(node_count, 0.0f), raw_environment(node_count, 0.0f), raw_rr(node_count, 0.0f);
    std::vector<float> conf_pixel(node_count, 0.0f), conf_brdf(node_count, 0.0f), conf_lens(node_count, 0.0f), conf_light_uv(node_count, 0.0f), conf_light_select(node_count, 0.0f), conf_environment(node_count, 0.0f), conf_rr(node_count, 0.0f);
    std::array<std::vector<float>, 6> raw_screen;
    std::array<std::vector<float>, 6> conf_screen;
    for (auto& values : raw_screen) values.assign(node_count, 0.0f);
    for (auto& values : conf_screen) values.assign(node_count, 0.0f);
    std::vector<Vec3f> node_normals(node_count, Vec3f::Zero());
    std::vector<float> node_depth(node_count, 0.0f);
    std::vector<Vec3f> node_albedo(node_count, Vec3f::Zero());

#ifdef _OPENMP
#pragma omp parallel for collapse(2) schedule(static)
#endif
    for (int ny = 0; ny < nodes_y; ++ny) {
        for (int nx = 0; nx < nodes_x; ++nx) {
            const int idx = ny * nodes_x + nx;
            const auto sens = rpf_grid(nx, ny).computeSplitSensitivity();
            const auto conf = rpf_grid(nx, ny).computeConfidence(config.rpf_confidence_samples);
            raw_pixel[idx] = sens.pixel;
            raw_brdf[idx] = sens.brdf;
            raw_lens[idx] = sens.lens;
            raw_light_uv[idx] = sens.light_uv;
            raw_light_select[idx] = sens.light_select;
            raw_environment[idx] = sens.environment;
            raw_rr[idx] = sens.rr;
            raw_screen[0][idx] = sens.screen_brdf;
            raw_screen[1][idx] = sens.screen_lens;
            raw_screen[2][idx] = sens.screen_light_uv;
            raw_screen[3][idx] = sens.screen_light_select;
            raw_screen[4][idx] = sens.screen_environment;
            raw_screen[5][idx] = sens.screen_rr;
            conf_pixel[idx] = conf.pixel;
            conf_brdf[idx] = conf.brdf;
            conf_lens[idx] = conf.lens;
            conf_light_uv[idx] = conf.light_uv;
            conf_light_select[idx] = conf.light_select;
            conf_environment[idx] = conf.environment;
            conf_rr[idx] = conf.rr;
            conf_screen[0][idx] = conf.screen_brdf;
            conf_screen[1][idx] = conf.screen_lens;
            conf_screen[2][idx] = conf.screen_light_uv;
            conf_screen[3][idx] = conf.screen_light_select;
            conf_screen[4][idx] = conf.screen_environment;
            conf_screen[5][idx] = conf.screen_rr;

            const int px = std::clamp(nx * step, 0, w - 1);
            const int py = std::clamp(ny * step, 0, h - 1);
            node_normals[idx] = sample_vec3_local_average(normals_unit, w, h, px, py, 1);
            if (node_normals[idx].squaredNorm() > EPS_SMALL) node_normals[idx].normalize();
            node_depth[idx] = sample_scalar_local_average(depth_avg, w, h, px, py, 1);
            node_albedo[idx] = sample_vec3_local_average(albedo_avg, w, h, px, py, 1);
        }
    }

    auto prior_smooth = [&](const std::vector<float>& raw, const std::vector<float>& conf) {
        std::vector<float> prior(node_count, 0.0f);
#ifdef _OPENMP
#pragma omp parallel for collapse(2) schedule(static)
#endif
        for (int ny = 0; ny < nodes_y; ++ny) {
            for (int nx = 0; nx < nodes_x; ++nx) {
                const int idx = ny * nodes_x + nx;
                double weight_sum = 0.0;
                double accum = 0.0;
                for (int dy = -1; dy <= 1; ++dy) {
                    const int sy = ny + dy;
                    if (sy < 0 || sy >= nodes_y) continue;
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int sx = nx + dx;
                        if (sx < 0 || sx >= nodes_x) continue;
                        const int sidx = sy * nodes_x + sx;
                        const float spatial_w = 1.0f / (1.0f + float(dx * dx + dy * dy));
                        const float normal_w = std::clamp((node_normals[idx].dot(node_normals[sidx]) + 1.0f) * 0.5f, 0.0f, 1.0f);
                        const float depth_w = std::exp(-4.0f * relative_depth_delta(node_depth[idx], node_depth[sidx]));
                        const float albedo_w = std::exp(-2.5f * (node_albedo[idx] - node_albedo[sidx]).norm());
                        const double w_neighbor = static_cast<double>(spatial_w) * static_cast<double>(normal_w) *
                                                  static_cast<double>(depth_w) * static_cast<double>(albedo_w) *
                                                  static_cast<double>(std::max(0.05f, conf[sidx]));
                        accum += w_neighbor * static_cast<double>(raw[sidx]);
                        weight_sum += w_neighbor;
                    }
                }
                prior[idx] = (weight_sum > static_cast<double>(EPS_SMALL))
                                 ? static_cast<float>(accum / weight_sum)
                                 : raw[idx];
            }
        }
        return prior;
    };

    const std::vector<float> prior_pixel = prior_smooth(raw_pixel, conf_pixel);
    const std::vector<float> prior_brdf = prior_smooth(raw_brdf, conf_brdf);
    const std::vector<float> prior_lens = prior_smooth(raw_lens, conf_lens);
    const std::vector<float> prior_light_uv = prior_smooth(raw_light_uv, conf_light_uv);
    const std::vector<float> prior_light_select = prior_smooth(raw_light_select, conf_light_select);
    const std::vector<float> prior_environment = prior_smooth(raw_environment, conf_environment);
    const std::vector<float> prior_rr = prior_smooth(raw_rr, conf_rr);
    std::array<std::vector<float>, 6> prior_screen;
    for (int component = 0; component < 6; ++component) {
        prior_screen[component] = prior_smooth(raw_screen[component], conf_screen[component]);
    }

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int idx = 0; idx < node_count; ++idx) {
        const float final_pixel = lerp(prior_pixel[idx], raw_pixel[idx], conf_pixel[idx]);
        const float final_brdf = lerp(prior_brdf[idx], raw_brdf[idx], conf_brdf[idx]);
        const float final_lens = lerp(prior_lens[idx], raw_lens[idx], conf_lens[idx]);
        const float final_light_uv = lerp(prior_light_uv[idx], raw_light_uv[idx], conf_light_uv[idx]);
        const float final_light_select = lerp(prior_light_select[idx], raw_light_select[idx], conf_light_select[idx]);
        const float final_environment = lerp(prior_environment[idx], raw_environment[idx], conf_environment[idx]);
        const float final_rr = lerp(prior_rr[idx], raw_rr[idx], conf_rr[idx]);
        float screen_components[6];
        for (int component = 0; component < 6; ++component) {
            screen_components[component] = lerp(
                prior_screen[component][idx], raw_screen[component][idx], conf_screen[component][idx]);
        }
        buffers.sensitivity_tiles.pixel[idx] = std::clamp(final_pixel, 0.0f, 1.0f);
        buffers.sensitivity_tiles.brdf[idx] = std::clamp(final_brdf, 0.0f, 1.0f);
        buffers.sensitivity_tiles.lens[idx] = std::clamp(final_lens, 0.0f, 1.0f);
        buffers.sensitivity_tiles.light_uv[idx] = std::clamp(final_light_uv, 0.0f, 1.0f);
        buffers.sensitivity_tiles.light_select[idx] = std::clamp(final_light_select, 0.0f, 1.0f);
        buffers.sensitivity_tiles.environment[idx] = std::clamp(final_environment, 0.0f, 1.0f);
        buffers.sensitivity_tiles.rr[idx] = std::clamp(final_rr, 0.0f, 1.0f);
        buffers.sensitivity_tiles.pixel_conf[idx] = std::clamp(conf_pixel[idx], 0.0f, 1.0f);
        buffers.sensitivity_tiles.brdf_conf[idx] = std::clamp(conf_brdf[idx], 0.0f, 1.0f);
        buffers.sensitivity_tiles.lens_conf[idx] = std::clamp(conf_lens[idx], 0.0f, 1.0f);
        buffers.sensitivity_tiles.light_uv_conf[idx] = std::clamp(conf_light_uv[idx], 0.0f, 1.0f);
        buffers.sensitivity_tiles.light_select_conf[idx] = std::clamp(conf_light_select[idx], 0.0f, 1.0f);
        buffers.sensitivity_tiles.environment_conf[idx] = std::clamp(conf_environment[idx], 0.0f, 1.0f);
        buffers.sensitivity_tiles.rr_conf[idx] = std::clamp(conf_rr[idx], 0.0f, 1.0f);
        const float random_components[6] = {
            buffers.sensitivity_tiles.brdf[idx], buffers.sensitivity_tiles.lens[idx],
            buffers.sensitivity_tiles.light_uv[idx], buffers.sensitivity_tiles.light_select[idx],
            buffers.sensitivity_tiles.environment[idx],
            buffers.sensitivity_tiles.rr[idx]};
        const float random_confidences[6] = {
            buffers.sensitivity_tiles.brdf_conf[idx], buffers.sensitivity_tiles.lens_conf[idx],
            buffers.sensitivity_tiles.light_uv_conf[idx], buffers.sensitivity_tiles.light_select_conf[idx],
            buffers.sensitivity_tiles.environment_conf[idx],
            buffers.sensitivity_tiles.rr_conf[idx]};
        float relative_components[6];
        float reliable_components[6];
        for (int component = 0; component < 6; ++component) {
            relative_components[component] = rpf::relative_random_sensitivity(
                random_components[component], screen_components[component]);
            reliable_components[component] = relative_components[component] * std::min(
                random_confidences[component], conf_screen[component][idx]);
        }
        const int dominant = static_cast<int>(
            std::max_element(reliable_components, reliable_components + 6) - reliable_components);
        buffers.sensitivity_tiles.brdf[idx] = reliable_components[0];
        buffers.sensitivity_tiles.lens[idx] = reliable_components[1];
        buffers.sensitivity_tiles.light_uv[idx] = reliable_components[2];
        buffers.sensitivity_tiles.light_select[idx] = reliable_components[3];
        buffers.sensitivity_tiles.light[idx] = std::max(reliable_components[2], reliable_components[3]);
        buffers.sensitivity_tiles.environment[idx] = reliable_components[4];
        buffers.sensitivity_tiles.rr[idx] = reliable_components[5];
        buffers.sensitivity_tiles.light_conf[idx] = reliable_components[2] >= reliable_components[3]
            ? random_confidences[2]
            : random_confidences[3];
        buffers.sensitivity_tiles.all[idx] = reliable_components[dominant];
        buffers.sensitivity_tiles.all_conf[idx] = std::min(
            random_confidences[dominant], conf_screen[dominant][idx]);
    }

    const std::vector<float> expanded_sensitivity = buffers.sensitivity_tiles.expand_values(buffers.sensitivity_tiles.all, w, h);
    const std::vector<float> expanded_confidence = buffers.sensitivity_tiles.expand_values(buffers.sensitivity_tiles.all_conf, w, h);
    buffers.sensitivity_confidence = expanded_confidence;
    buffers.sensitivity_gradient.assign(w * h, 0.0f);
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int idx = y * w + x;
            const float left = expanded_sensitivity[y * w + std::max(0, x - 1)];
            const float right = expanded_sensitivity[y * w + std::min(w - 1, x + 1)];
            const float up = expanded_sensitivity[std::max(0, y - 1) * w + x];
            const float down = expanded_sensitivity[std::min(h - 1, y + 1) * w + x];
            const float grad = std::sqrt((right - left) * (right - left) + (down - up) * (down - up));
            buffers.sensitivity_gradient[idx] = std::clamp(0.5f * grad, 0.0f, 1.0f);
        }
    }

    ensure_sensitivity_cache(w, h);
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - start);
    spdlog::info("Sensitivity recompute completed in {} ms", duration.count());
}

/// @brief Reconstructs radiance using statistical neighbor compatibility.
/// @param config Render configuration controlling feature gates.
void statmc_denoise(const RenderConfig& config) {
    const auto start = std::chrono::high_resolution_clock::now();
    const int w = config.image_width;
    const int h = config.image_height;
    const int window_radius = config.color_window_radius;
    const float normal_threshold = config.color_normal_threshold;
    const float depth_threshold = config.color_depth_threshold;
    const float rpd_scale = config.rpf_shrinkage_scale;

    const auto& features = ensure_feature_cache(w, h);
    const auto& normals_unit = features.normals_unit;
    const auto& depth_avg = features.depth_avg;
    const auto& albedo_avg = features.albedo_avg;
    const auto& sensitivity = ensure_sensitivity_cache(w, h);
    // ponytail: 3x3 variance shrinkage; use multi-transform denoising if tails dominate.
    std::vector<Vec3f> sqrt_variance_mean(w * h, Vec3f::Zero());
    std::vector<Vec3f> sqrt_variance_stable(w * h, Vec3f::Zero());
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int i = 0; i < w * h; ++i) {
        sqrt_variance_mean[i] = (pixel_stats[i].sqrt_color_variance() /
            float(std::max(1, pixel_stats[i].n))).cwiseMax(EPS_SMALL);
    }
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int idx = y * w + x;
            sqrt_variance_stable[idx] = sqrt_variance_mean[idx].cwiseMax(
                sample_vec3_local_average(sqrt_variance_mean, w, h, x, y, 1));
        }
    }
    std::vector<float> neighbor_count_debug(w * h, 0.0f);
    std::vector<float> dbg_weight_sum(w * h, 0.0f);
    std::vector<float> dbg_neff(w * h, 0.0f);

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int idx = y * w + x;
            if (buffers.hit_count[idx] == 0) {
                buffers.denoised(x, y) = pixel_stats[idx].color_mean;
                buffers.uncertainty[idx] = pixel_stats[idx].variance() / float(std::max(1, pixel_stats[idx].n));
                buffers.alpha[idx] = 1.0f;
                continue;
            }

            const Vec3f normal_i = normals_unit[idx];
            const float depth_i = depth_avg[idx];
            const Vec3f mean_color_i = pixel_stats[idx].color_mean;
            const float var2_max = config.color_sigma_max * config.color_sigma_max;
            float var_i = pixel_stats[idx].variance();
            if (!std::isfinite(var_i)) var_i = EPS_SMALL;
            var_i = std::clamp(var_i, EPS_SMALL, var2_max);
            const int n_i = std::max(1, pixel_stats[idx].n);
            const SensitivityState sens_i = sample_sensitivity(sensitivity, idx);
            const auto rpd_components_i = sens_i.components_denoise();
            const float var_mean_i = std::max(EPS_SMALL, var_i / float(n_i));
            const Vec3f sqrt_mean_i = pixel_stats[idx].sqrt_color_mean;
            const Vec3f sqrt_var_mean_i = sqrt_variance_stable[idx];
            buffers.var_total_debug[idx] = var_i;
            buffers.var_eff_debug[idx] = var_mean_i;
            buffers.var_ratio_debug[idx] = (var_i > EPS_SMALL) ? (var_mean_i * float(n_i)) / var_i : 0.0f;

            double weight_sum = 0.0;
            double weight_square_sum = 0.0;
            double weighted_variance_sum = 0.0;
            Vec3d color_sum = Vec3d::Zero();
            double accepted_neighbors = 0.0;
            constexpr double center_weight = 1.0;

            for (int dy = -window_radius; dy <= window_radius; ++dy) {
                int yy = y + dy;
                if (yy < 0 || yy >= h) continue;
                for (int dx = -window_radius; dx <= window_radius; ++dx) {
                    int xx = x + dx;
                    if (xx < 0 || xx >= w) continue;

                    int neighbor_idx = yy * w + xx;
                    if (buffers.hit_count[neighbor_idx] == 0) continue;

                    const SensitivityState sens_j = sample_sensitivity(sensitivity, neighbor_idx);

                    const Vec3f normal_j = normals_unit[neighbor_idx];
                    if (normal_i.dot(normal_j) < normal_threshold) continue;

                    const float depth_delta = relative_depth_delta(depth_i, depth_avg[neighbor_idx]);
                    if (depth_delta > depth_threshold) continue;

                    const Vec3f albedo_i = albedo_avg[idx];
                    const Vec3f albedo_j = albedo_avg[neighbor_idx];
                    const float albedo_diff = (albedo_i - albedo_j).norm();
                    if (albedo_diff > rpf::BASE_ALBEDO_THRESH) continue;

                    float var_j = pixel_stats[neighbor_idx].variance();
                    if (!std::isfinite(var_j)) var_j = EPS_SMALL;
                    var_j = std::clamp(var_j, EPS_SMALL, var2_max);
                    const int n_j = std::max(1, pixel_stats[neighbor_idx].n);
                    const float var_mean_j = std::max(EPS_SMALL, var_j / float(n_j));
                    const Vec3f sqrt_mean_j = pixel_stats[neighbor_idx].sqrt_color_mean;
                    const Vec3f sqrt_var_mean_j = sqrt_variance_stable[neighbor_idx];

                    double max_normalized_t2 = 0.0;
                    for (int channel = 0; channel < 3; ++channel) {
                        const double v_i = static_cast<double>(sqrt_var_mean_i[channel]);
                        const double v_j = static_cast<double>(sqrt_var_mean_j[channel]);
                        const double variance_sum = v_i + v_j;
                        const double mean_diff = static_cast<double>(sqrt_mean_i[channel] - sqrt_mean_j[channel]);
                        const double t2 = mean_diff * mean_diff / std::max(static_cast<double>(EPS_SMALL), variance_sum);
                        const double denom_df =
                            (v_i * v_i) / std::max(static_cast<double>(n_i - 1), 1.0) +
                            (v_j * v_j) / std::max(static_cast<double>(n_j - 1), 1.0);
                        const double df = (denom_df > static_cast<double>(EPS_SMALL))
                                              ? (variance_sum * variance_sum) / denom_df
                                              : static_cast<double>(n_i + n_j - 2);
                        const double t_crit = static_cast<double>(lookup_t_critical(static_cast<float>(df), config.color_compat_alpha));
                        max_normalized_t2 = std::max(max_normalized_t2, t2 / (t_crit * t_crit));
                    }
                    const double compatibility_q = rpf::relax_compatibility(
                        max_normalized_t2,
                        rpf::shared_reliability(rpd_components_i, sens_j.components_denoise()),
                        rpd_scale);
                    if (compatibility_q > 25.0) continue;

                    const double dist2 = double(dx * dx + dy * dy);
                    const double spatial_w = 1.0 / (1.0 + dist2);
                    const double weight_j = spatial_w * std::exp(-0.5 * compatibility_q);
                    color_sum += pixel_stats[neighbor_idx].color_mean.cast<double>() * weight_j;
                    weight_sum += weight_j;
                    weight_square_sum += weight_j * weight_j;
                    weighted_variance_sum += weight_j * weight_j * static_cast<double>(var_mean_j);
                    if ((dx != 0 || dy != 0) && weight_j > 0.05 * spatial_w) {
                        neighbor_count_debug[idx] += 1.0f;
                        accepted_neighbors += 1.0;
                    }
                }
            }

            Vec3f mean_color_neighbors = mean_color_i;
            double var_neighbors = static_cast<double>(var_mean_i);
            if (weight_sum > static_cast<double>(EPS_SMALL)) {
                mean_color_neighbors = (color_sum / weight_sum).cast<float>();
                var_neighbors = weighted_variance_sum / (weight_sum * weight_sum);
            }
            const double effective_neighbors = weight_sum * weight_sum /
                std::max(static_cast<double>(EPS_SMALL), weight_square_sum);
            dbg_weight_sum[idx] = static_cast<float>(weight_sum);
            dbg_neff[idx] = static_cast<float>(effective_neighbors);

            const double blend = (accepted_neighbors >= 2.0 && weight_sum >= 1.1 * center_weight)
                ? std::clamp(1.0 - 1.0 / std::sqrt(effective_neighbors), 0.0, 0.8)
                : 0.0;

            const float alpha = static_cast<float>(1.0 - blend);
            Vec3f denoised = lerp(mean_color_i, mean_color_neighbors, static_cast<float>(blend));
            const float uncertainty = static_cast<float>(
                static_cast<double>(alpha) * static_cast<double>(alpha) * static_cast<double>(var_mean_i) +
                blend * blend * var_neighbors +
                2.0 * static_cast<double>(alpha) * blend *
                    (center_weight / std::max(weight_sum, static_cast<double>(EPS_SMALL))) *
                    static_cast<double>(var_mean_i));

            buffers.alpha[idx] = alpha;

            buffers.denoised(x, y) = denoised;
            buffers.uncertainty[idx] = uncertainty;
        }
    }
    if (config.debug_statmc_outputs) {
        std::vector<float> reliability_base(w * h, 0.0f);
        std::vector<float> reliability_denoise(w * h, 0.0f);
        for (int i = 0; i < w * h; ++i) {
            const SensitivityState sens = sample_sensitivity(sensitivity, i);
            reliability_base[i] = sens.reliable();
            reliability_denoise[i] = sens.reliable_denoise();
        }
        scalar_to_image(neighbor_count_debug, w, h).save("debug_neighbors.hdr");
        scalar_to_image(buffers.alpha, w, h).save("debug_alpha.hdr");
        scalar_to_image(buffers.var_total_debug, w, h).save("debug_var_total.hdr");
        scalar_to_image(buffers.var_eff_debug, w, h).save("debug_var_eff.hdr");
        scalar_to_image(buffers.var_ratio_debug, w, h).save("debug_var_ratio.hdr");
        scalar_to_image(dbg_weight_sum, w, h).save("debug_weight_sum.hdr");
        scalar_to_image(dbg_neff, w, h).save("debug_neff.hdr");
        scalar_to_image(sensitivity.all, w, h).save("debug_sensitivity_all.hdr");
        scalar_to_image(sensitivity.pixel, w, h).save("debug_sensitivity_pixel.hdr");
        scalar_to_image(sensitivity.brdf, w, h).save("debug_sensitivity_brdf.hdr");
        scalar_to_image(sensitivity.lens, w, h).save("debug_sensitivity_lens.hdr");
        scalar_to_image(sensitivity.light, w, h).save("debug_sensitivity_light.hdr");
        scalar_to_image(sensitivity.light_uv, w, h).save("debug_sensitivity_light_uv.hdr");
        scalar_to_image(sensitivity.light_select, w, h).save("debug_sensitivity_light_select.hdr");
        scalar_to_image(sensitivity.environment, w, h).save("debug_sensitivity_environment.hdr");
        scalar_to_image(sensitivity.rr, w, h).save("debug_sensitivity_rr.hdr");
        scalar_to_image(buffers.sensitivity_confidence, w, h).save("debug_sensitivity_confidence.hdr");
        scalar_to_image(buffers.sensitivity_gradient, w, h).save("debug_sensitivity_gradient.hdr");
        scalar_to_image(reliability_base, w, h).save("debug_reliability_base.hdr");
        scalar_to_image(reliability_denoise, w, h).save("debug_reliability_denoise.hdr");
    }

    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - start);
    spdlog::info("StatMC denoise completed in {} ms", duration.count());
}

/// @brief Stabilizes the per-sample variance field used by adaptive sampling.
/// @param config Render configuration controlling variance smoothing.
void variance_denoise(const RenderConfig& config) {
    const auto start = std::chrono::high_resolution_clock::now();
    const int w = config.image_width;
    const int h = config.image_height;
    const int radius = config.var_window_radius;
    const float normal_threshold = config.var_normal_threshold;
    const float depth_threshold = config.var_depth_threshold;
    const float compat_sigma = config.var_compat_sigma;
    const float shrinkage_k = config.var_shrinkage_k;
    const int iterations = config.var_iterations;
    const float max_variance = config.adaptive_sigma_max * config.adaptive_sigma_max;

    std::vector<float> curr(w * h, 0.0f);
    std::vector<float> next(w * h, 0.0f);
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int i = 0; i < w * h; ++i) {
        float v = pixel_stats[i].variance();
        if (!std::isfinite(v)) v = EPS_SMALL;
        curr[i] = std::clamp(v, EPS_SMALL, max_variance);
    }

    const auto& features = ensure_feature_cache(w, h);
    const auto& normals_unit = features.normals_unit;
    const auto& depth_avg = features.depth_avg;

    auto smooth_once = [&](const std::vector<float>& src, std::vector<float>& dst) {
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int idx = y * w + x;
                if (buffers.hit_count[idx] == 0) {
                    dst[idx] = src[idx];
                    continue;
                }

                const Vec3f normal_i = normals_unit[idx];
                const float depth_i = depth_avg[idx];
                const float var_i = std::max(EPS_SMALL, src[idx]);

                float weight_sum = 0.0f;
                float weighted_sum = 0.0f;

                for (int dy = -radius; dy <= radius; ++dy) {
                    int yy = y + dy;
                    if (yy < 0 || yy >= h) continue;
                    for (int dx = -radius; dx <= radius; ++dx) {
                        int xx = x + dx;
                        if (xx < 0 || xx >= w || (dx == 0 && dy == 0)) continue;

                        int neighbor_idx = yy * w + xx;
                        if (buffers.hit_count[neighbor_idx] == 0) continue;

                        const Vec3f normal_j = normals_unit[neighbor_idx];
                        if (normal_i.dot(normal_j) < normal_threshold) continue;

                        const float depth_j = depth_avg[neighbor_idx];
                        const float depth_gate = relative_depth_delta(depth_i, depth_j);
                        if (depth_gate > depth_threshold) continue;

                        const float var_j = std::max(EPS_SMALL, src[neighbor_idx]);
                        const float relative_difference = std::abs(var_i - var_j) /
                            std::max(EPS_SMALL, std::sqrt(var_i * var_j));
                        if (relative_difference > compat_sigma) continue;

                        const float dist2 = float(dx * dx + dy * dy);
                        const float w_j = 1.0f / (1.0f + dist2);
                        weighted_sum += w_j * var_j;
                        weight_sum += w_j;
                    }
                }

                float neighbor_mean = var_i;
                if (weight_sum > EPS_SMALL) neighbor_mean = weighted_sum / weight_sum;

                dst[idx] = rpf::conservative_variance_update(var_i, neighbor_mean, shrinkage_k);
            }
        }
    };

    for (int it = 0; it < iterations; ++it) {
        smooth_once(curr, next);
        std::swap(curr, next);
    }

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int i = 0; i < w * h; ++i) {
        float raw = pixel_stats[i].variance();
        if (!std::isfinite(raw)) raw = EPS_SMALL;
        raw = std::clamp(raw, EPS_SMALL, max_variance);
        curr[i] = std::max(curr[i], raw);
    }

    buffers.variance_denoised = std::move(curr);
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - start);
    spdlog::info("Sample-variance denoise completed in {} ms", duration.count());
}

/// @brief Allocates one adaptive pass without exceeding its sample budget.
/// @param config Render configuration controlling the allocation.
/// @return Extra sample count for each pixel.
std::vector<int> compute_adaptive_sample_counts(const RenderConfig& config) {
    const auto start = std::chrono::high_resolution_clock::now();
    const int w = config.image_width;
    const int h = config.image_height;
    const int extra_sample_total = config.adaptive_spp * w * h;
    const int floor_per_pixel = std::clamp(config.adaptive_base_samples, 0, config.adaptive_spp);
    const int reserved_samples = floor_per_pixel * w * h;

    std::vector<int> sample_counts(w * h, floor_per_pixel);
    auto save_extra_counts_debug = [&]() {
        if (!config.debug_statmc_outputs) return;
        std::vector<float> extra_counts_debug(w * h, 0.0f);
        for (int i = 0; i < w * h; ++i) {
            extra_counts_debug[i] = static_cast<float>(sample_counts[i]);
        }
        scalar_to_image(extra_counts_debug, w, h).save("debug_adaptive_extra_counts.hdr");
    };
    if (extra_sample_total <= 0) {
        save_extra_counts_debug();
        return sample_counts;
    }

    const float SIGMA_MAX = config.adaptive_sigma_max;
    const auto& features = ensure_feature_cache(w, h);
    const auto& normals_unit = features.normals_unit;
    const auto& depth_avg = features.depth_avg;

    std::vector<float> importance(w * h, 0.0f);
    double sum_importance = 0.0;

    std::vector<float> variance_source = buffers.variance_denoised;
    if (variance_source.size() != static_cast<size_t>(w * h)) variance_source.assign(w * h, 0.0f);
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int i = 0; i < w * h; ++i) {
        float raw = pixel_stats[i].variance();
        if (!std::isfinite(raw)) raw = EPS_SMALL;
        raw = std::max(EPS_SMALL, raw);
        float denoised_sample_variance = variance_source[i];
        if (!std::isfinite(denoised_sample_variance) || denoised_sample_variance <= 0.0f) {
            denoised_sample_variance = raw;
        }
        variance_source[i] = std::clamp(std::max(raw, denoised_sample_variance), EPS_SMALL, SIGMA_MAX * SIGMA_MAX);
    }

    if (config.debug_statmc_outputs) {
        scalar_to_image(variance_source, w, h).save("debug_variance_source_adaptive.hdr");
    }

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int i = 0; i < w * h; ++i) {
        importance[i] = std::sqrt(variance_source[i]);
    }
    sum_importance = std::accumulate(importance.begin(), importance.end(), 0.0);

    const int radius = config.adaptive_importance_smoothing_radius;
    if (radius > 0) {
        std::vector<float> smoothed(importance.size(), 0.0f);
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const int idx = y * w + x;
                if (buffers.hit_count[idx] == 0) {
                    smoothed[idx] = importance[idx];
                    continue;
                }
                double accum = 0.0;
                double weight_sum = 0.0;
                for (int dy = -radius; dy <= radius; ++dy) {
                    const int yy = y + dy;
                    if (yy < 0 || yy >= h) continue;
                    for (int dx = -radius; dx <= radius; ++dx) {
                        const int xx = x + dx;
                        if (xx < 0 || xx >= w) continue;
                        const int nidx = yy * w + xx;
                        if (buffers.hit_count[nidx] == 0) continue;
                        const float spatial_w = 1.0f / (1.0f + float(dx * dx + dy * dy));
                        const float normal_w = std::clamp((normals_unit[idx].dot(normals_unit[nidx]) + 1.0f) * 0.5f, 0.0f, 1.0f);
                        const float depth_w = std::exp(-4.0f * relative_depth_delta(depth_avg[idx], depth_avg[nidx]));
                        const double w_neighbor = static_cast<double>(spatial_w) *
                                                  static_cast<double>(normal_w) *
                                                  static_cast<double>(depth_w);
                        accum += w_neighbor * static_cast<double>(importance[nidx]);
                        weight_sum += w_neighbor;
                    }
                }
                smoothed[idx] = (weight_sum > static_cast<double>(EPS_SMALL))
                                    ? static_cast<float>(accum / weight_sum)
                                    : importance[idx];
            }
        }
        importance.swap(smoothed);
        sum_importance = std::accumulate(importance.begin(), importance.end(), 0.0);
    }

    buffers.adaptive_importance = importance;

    int remaining_samples = std::max(0, extra_sample_total - reserved_samples);
    if (sum_importance <= 0.0 || remaining_samples <= 0) {
        save_extra_counts_debug();
        return sample_counts;
    }

    std::vector<float> desired_add(w * h, 0.0f);
    std::vector<int> active_indices(w * h);
    std::iota(active_indices.begin(), active_indices.end(), 0);
    double remaining_importance = sum_importance;
    const int max_extra_per_pixel = std::max(4, 8 * std::max(1, config.adaptive_spp));

    while (!active_indices.empty() && remaining_samples > 0 && remaining_importance > static_cast<double>(EPS_SMALL)) {
        bool clipped = false;
        std::vector<int> next_active;
        next_active.reserve(active_indices.size());
        for (int idx : active_indices) {
            const double share = static_cast<double>(importance[idx]) / remaining_importance;
            const double desired = share * static_cast<double>(remaining_samples);
            if (desired > static_cast<double>(max_extra_per_pixel)) {
                desired_add[idx] = static_cast<float>(max_extra_per_pixel);
                remaining_samples -= max_extra_per_pixel;
                remaining_importance -= static_cast<double>(importance[idx]);
                clipped = true;
            } else {
                next_active.push_back(idx);
            }
        }
        if (!clipped) {
            for (int idx : next_active) {
                desired_add[idx] = static_cast<float>(
                    static_cast<double>(importance[idx]) / remaining_importance *
                    static_cast<double>(remaining_samples));
            }
            break;
        }
        active_indices.swap(next_active);
    }

    int assigned = 0;
    std::vector<float> residual_weights(w * h, 0.0f);
    double residual_sum = 0.0;
    for (int i = 0; i < w * h; ++i) {
        const int add = static_cast<int>(std::floor(desired_add[i]));
        sample_counts[i] += add;
        assigned += add;
        residual_weights[i] = std::max(0.0f, desired_add[i] - float(add));
        residual_sum += static_cast<double>(residual_weights[i]);
    }

    int remaining = std::max(0, extra_sample_total - reserved_samples - assigned);
    if (remaining > 0 && residual_sum > static_cast<double>(EPS_SMALL)) {
        const double offset = static_cast<double>(hash_to_unit_float(g_sampler_base_seed ^ 0x4d595df4u));
        double cumulative = 0.0;
        int cursor = 0;
        for (int k = 0; k < remaining; ++k) {
            const double target = ((static_cast<double>(k) + offset) / static_cast<double>(remaining)) * residual_sum;
            while (cursor < w * h && cumulative + static_cast<double>(residual_weights[cursor]) < target) {
                cumulative += static_cast<double>(residual_weights[cursor]);
                ++cursor;
            }
            if (cursor < w * h) sample_counts[cursor]++;
        }
    }

    save_extra_counts_debug();

    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - start);
    spdlog::info("Adaptive sample count computation completed in {} ms", duration.count());
    return sample_counts;
}

/// @brief Renders one adaptive pass using the current importance field.
/// @param config Render configuration.
/// @param camera Initialized camera.
void render_image_adaptive(const RenderConfig& config, const Camera& camera) {
    if (config.adaptive_spp <= 0)  return;

    const bool capture_rpd = config.rpf_shrinkage_scale > 0.0f;
    const int floor_per_pixel = std::clamp(config.adaptive_base_samples, 0, config.adaptive_spp);
    spdlog::info("Adaptive render: extra spp = {}, minimum per-pixel = {} (budget-preserving), passes = {}",
                 config.adaptive_spp, floor_per_pixel, config.adaptive_passes);
#ifdef _OPENMP
    spdlog::info("OpenMP threads: {}", omp_get_max_threads());
#endif

    const int w = config.image_width, h = config.image_height;

    std::vector<int> extra_counts = compute_adaptive_sample_counts(config);

    auto start = std::chrono::high_resolution_clock::now();
    std::atomic<int> rows_completed{0};
    const int num_rpf_grids =
#ifdef _OPENMP
        omp_get_max_threads();
#else
        1;
#endif
    std::vector<rpf::Grid> local_rpf_grids(capture_rpd ? num_rpf_grids : 0);
    for (auto& grid : local_rpf_grids) {
        grid.init(w, h, config.rpf_tile_size);
        grid.reset();
    }

#ifdef _OPENMP
#pragma omp parallel
    {
        const int rpf_grid_index = omp_get_thread_num();
#pragma omp for schedule(dynamic, 1)
#else
    const int rpf_grid_index = 0;
#endif
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int idx = y * w + x;
            int n_extra = extra_counts[idx];
            Sampler::init_thread(pixel_sample_seed(idx, pixel_stats[idx].n));
            for (int s = 0; s < n_extra; ++s) {
                const Vec2f pixel_u = Sampler::next2d();
                const float u = (x + pixel_u.x()) / float(w);
                const float v = (y + pixel_u.y()) / float(h);

                const Vec2f lens_u = Sampler::next2d();
                const Vec2f lens_sample = camera.lens_radius > 0.0f
                    ? Sampler::sample_disk(lens_u)
                    : Vec2f::Zero();
                rpf::Sample rpf_sample{};
                if (capture_rpd) {
                    rpf_sample.capture_pixel(pixel_u);
                    if (camera.lens_radius > 0.0f) rpf_sample.capture_lens(lens_u);
                }

                Ray ray = camera.generate_ray(u, (1.0f - v), lens_sample);
                Vec3f path_trace = mis_path_trace(ray, idx, capture_rpd ? &rpf_sample : nullptr);
                if (!path_trace.allFinite()) continue;

                if (capture_rpd) {
                    accumulate_rpf_sample_to_grid(
                        local_rpf_grids[rpf_grid_index], x, y, rpf_sample, calc_luminance(path_trace));
                }
                accumulate_sample(pixel_stats[idx], path_trace);
                buffers.radiance(x, y) += path_trace;
                buffers.sample_count[idx]++;
            }
        }

        int finished = ++rows_completed;
        if (finished % 50 == 0 || finished == h) {
            spdlog::info("Adaptive progress: {} / {}", finished, h);
        }
    }
#ifdef _OPENMP
    }
#endif

    if (capture_rpd) merge_rpf_grids_into(rpf_grid, local_rpf_grids);

    invalidate_postprocess_caches();

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    spdlog::info("Adaptive rendering completed in {} ms", duration.count());
}

/// @brief Executes uniform rendering or the full StatMC/RPD pipeline.
/// @param config Render configuration.
/// @param camera Initialized camera.
void render(const RenderConfig& config, const Camera& camera) {
    if (config.use_statmc) {
        render_image_statmc(config, camera);

        for (int i = 0; i < config.adaptive_passes && config.adaptive_spp > 0; ++i) {
            statmc_denoise(config);
            variance_denoise(config);
            render_image_adaptive(config, camera);
            recompute_sensitivity(config);
        }
        
        recompute_sensitivity(config);
        statmc_denoise(config);
        variance_denoise(config);
        buffers.finalize();
        buffers.raw_radiance = buffers.radiance;
        buffers.radiance = buffers.denoised;
    } else {
        render_image(config, camera);
        buffers.finalize();
    }
}

int main(int argc, char** argv) {
    CLI::App app{"Monte Carlo path tracer"};

    std::string config_file;
    std::string scene_name;
    std::string output_dir_cli;
    bool statmc_enable_cli = false;
    bool statmc_disable_cli = false;
    int rpf_tile_size_cli = -1;
    int color_window_radius_cli = std::numeric_limits<int>::min();
    float color_normal_thresh_cli = std::numeric_limits<float>::quiet_NaN();
    float color_depth_thresh_cli = std::numeric_limits<float>::quiet_NaN();
    float color_compat_alpha_cli = std::numeric_limits<float>::quiet_NaN();
    float color_sigma_max_cli = std::numeric_limits<float>::quiet_NaN();
    int var_window_radius_cli = std::numeric_limits<int>::min();
    float var_normal_thresh_cli = std::numeric_limits<float>::quiet_NaN();
    float var_depth_thresh_cli = std::numeric_limits<float>::quiet_NaN();
    float var_compat_sigma_cli = std::numeric_limits<float>::quiet_NaN();
    float var_shrinkage_k_cli = std::numeric_limits<float>::quiet_NaN();
    int var_iterations_cli = std::numeric_limits<int>::min();
    int adaptive_base_samples_cli = std::numeric_limits<int>::min();
    int adaptive_spp_cli = std::numeric_limits<int>::min();
    float adaptive_sigma_max_cli = std::numeric_limits<float>::quiet_NaN();
    int adaptive_passes_cli = std::numeric_limits<int>::min();
    int adaptive_importance_smoothing_radius_cli = std::numeric_limits<int>::min();
    float rpf_shrinkage_scale_cli = std::numeric_limits<float>::quiet_NaN();
    float rpf_confidence_samples_cli = std::numeric_limits<float>::quiet_NaN();
    std::string tonemap_cli;
    int samples_per_pixel_cli = std::numeric_limits<int>::min();
    std::optional<uint32_t> sampler_seed;

    app.add_option("-c,--config", config_file, "Path to config YAML file")->required();
    app.add_option("-s,--scene", scene_name, "Name of the built-in scene to render (overrides config)");
    app.add_option("-o,--output", output_dir_cli, "Output directory (absolute or relative). Image saved as <dir>/<name>.png");
    app.add_option("--spp", samples_per_pixel_cli, "Override samples per pixel (>0)")->check(CLI::PositiveNumber);
    app.add_flag("--statmc", statmc_enable_cli, "Enable StatMC/RPF rendering");
    app.add_flag("--no-statmc", statmc_disable_cli, "Disable StatMC/RPF rendering");
    app.add_option("--rpf-tile-size", rpf_tile_size_cli, "Sensitivity support size for the overlapping RPF grid")->check(CLI::PositiveNumber);
    app.add_option("--color-window-radius", color_window_radius_cli, "Color window radius (pixels)")->check(CLI::PositiveNumber);
    app.add_option("--color-normal-thresh", color_normal_thresh_cli, "Color normal dot threshold (0,1]");
    app.add_option("--color-depth-thresh", color_depth_thresh_cli, "Color relative depth threshold (>=0)");
    app.add_option("--color-compat-alpha", color_compat_alpha_cli, "Color compatibility significance alpha (0,1)");
    app.add_option("--color-sigma-max", color_sigma_max_cli, "Max stddev clamp for color variance (>0)");
    app.add_option("--var-window-radius", var_window_radius_cli, "Sample-variance window radius (pixels)")->check(CLI::PositiveNumber);
    app.add_option("--var-normal-thresh", var_normal_thresh_cli, "Sample-variance normal dot threshold (0,1]");
    app.add_option("--var-depth-thresh", var_depth_thresh_cli, "Sample-variance relative depth threshold (>=0)");
    app.add_option("--var-compat-sigma", var_compat_sigma_cli, "Sample-variance relative compatibility threshold (>0)");
    app.add_option("--var-shrinkage-k", var_shrinkage_k_cli, "Sample-variance shrinkage stabilizer (>0)");
    app.add_option("--var-iterations", var_iterations_cli, "Sample-variance smoothing iterations")->check(CLI::PositiveNumber);
    app.add_option("--adaptive-base-samples", adaptive_base_samples_cli, "Minimum extra samples per pixel per adaptive pass, taken from the adaptive_spp budget");
    app.add_option("--adaptive-spp", adaptive_spp_cli, "Extra samples per adaptive pass (spp)");
    app.add_option("--adaptive-sigma-max", adaptive_sigma_max_cli, "Max sigma clamp for adaptive importance (>0)");
    app.add_option("--adaptive-passes", adaptive_passes_cli, "Number of adaptive refinement passes (>=0)");
    app.add_option("--adaptive-importance-radius", adaptive_importance_smoothing_radius_cli, "Edge-aware smoothing radius for adaptive importance (>=0)");
    app.add_option("--rpf-shrinkage-scale", rpf_shrinkage_scale_cli, "Scale RPD-guided StatMC compatibility relaxation; zero disables all RPD influence on reconstruction");
    app.add_option("--rpf-confidence-samples", rpf_confidence_samples_cli, "Confidence sample mass for RP sensitivity shrinkage (>0)");
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

    int samples_per_pixel = render_config.samples_per_pixel;
    if (samples_per_pixel_cli != std::numeric_limits<int>::min()) {
        samples_per_pixel = samples_per_pixel_cli;
        if (samples_per_pixel <= 0) {
            spdlog::error("Samples per pixel must be positive");
            return 1;
        }
        spdlog::info("CLI samples-per-pixel {} overrides config {}", samples_per_pixel_cli, render_config.samples_per_pixel);
    }

    bool use_statmc = render_config.use_statmc;
    if (statmc_enable_cli) use_statmc = true;
    if (statmc_disable_cli) use_statmc = false;
    const int rpf_tile_size = (rpf_tile_size_cli > 0) ? rpf_tile_size_cli : render_config.rpf_tile_size;
    if (rpf_tile_size <= 0) {
        spdlog::error("RPF tile size must be positive");
        return 1;
    }
    int color_window_radius = render_config.color_window_radius;
    if (color_window_radius_cli != std::numeric_limits<int>::min()) {
        color_window_radius = color_window_radius_cli;
    }
    if (color_window_radius <= 0) {
        spdlog::error("color_window_radius must be positive");
        return 1;
    }
    float statmc_normal_thresh = std::isnan(color_normal_thresh_cli) ? render_config.color_normal_threshold : color_normal_thresh_cli;
    if (statmc_normal_thresh <= 0.0f || statmc_normal_thresh > 1.0f) {
        spdlog::error("color_normal_thresh must be in (0,1]");
        return 1;
    }
    float statmc_depth_thresh = std::isnan(color_depth_thresh_cli) ? render_config.color_depth_threshold : color_depth_thresh_cli;
    if (statmc_depth_thresh < 0.0f) {
        spdlog::error("color_depth_thresh must be non-negative");
        return 1;
    }
    float color_compat_alpha = std::isnan(color_compat_alpha_cli) ? render_config.color_compat_alpha : color_compat_alpha_cli;
    if (color_compat_alpha <= 0.0f || color_compat_alpha >= 1.0f) {
        spdlog::error("color_compat_alpha must be in (0,1)");
        return 1;
    }
    float color_sigma_max = std::isnan(color_sigma_max_cli) ? render_config.color_sigma_max : color_sigma_max_cli;
    if (color_sigma_max <= 0.0f) {
        spdlog::error("color_sigma_max must be positive");
        return 1;
    }
    int var_window_radius = render_config.var_window_radius;
    if (var_window_radius_cli != std::numeric_limits<int>::min()) {
        var_window_radius = var_window_radius_cli;
    }
    if (var_window_radius <= 0) {
        spdlog::error("var_window_radius must be positive");
        return 1;
    }
    float var_normal_thresh = std::isnan(var_normal_thresh_cli) ? render_config.var_normal_threshold : var_normal_thresh_cli;
    if (var_normal_thresh <= 0.0f || var_normal_thresh > 1.0f) {
        spdlog::error("var_normal_thresh must be in (0,1]");
        return 1;
    }
    float var_depth_thresh = std::isnan(var_depth_thresh_cli) ? render_config.var_depth_threshold : var_depth_thresh_cli;
    if (var_depth_thresh < 0.0f) {
        spdlog::error("var_depth_thresh must be non-negative");
        return 1;
    }
    float var_compat_sigma = std::isnan(var_compat_sigma_cli) ? render_config.var_compat_sigma : var_compat_sigma_cli;
    if (var_compat_sigma <= 0.0f) {
        spdlog::error("var_compat_sigma must be positive");
        return 1;
    }
    float var_shrinkage_k = std::isnan(var_shrinkage_k_cli) ? render_config.var_shrinkage_k : var_shrinkage_k_cli;
    if (var_shrinkage_k <= 0.0f) {
        spdlog::error("var_shrinkage_k must be positive");
        return 1;
    }
    int var_iterations = render_config.var_iterations;
    if (var_iterations_cli != std::numeric_limits<int>::min()) {
        var_iterations = var_iterations_cli;
    }
    if (var_iterations <= 0) {
        spdlog::error("var_iterations must be positive");
        return 1;
    }
    int adaptive_base_samples = render_config.adaptive_base_samples;
    if (adaptive_base_samples_cli != std::numeric_limits<int>::min()) {
        adaptive_base_samples = adaptive_base_samples_cli;
    }
    int adaptive_spp = render_config.adaptive_spp;
    if (adaptive_spp_cli != std::numeric_limits<int>::min()) {
        adaptive_spp = adaptive_spp_cli;
    }
    float adaptive_sigma_max = std::isnan(adaptive_sigma_max_cli) ? render_config.adaptive_sigma_max : adaptive_sigma_max_cli;
    int adaptive_passes = render_config.adaptive_passes;
    if (adaptive_passes_cli != std::numeric_limits<int>::min()) {
        adaptive_passes = adaptive_passes_cli;
    }
    int adaptive_importance_smoothing_radius = render_config.adaptive_importance_smoothing_radius;
    if (adaptive_importance_smoothing_radius_cli != std::numeric_limits<int>::min()) {
        adaptive_importance_smoothing_radius = adaptive_importance_smoothing_radius_cli;
    }
    if (adaptive_base_samples < 0 || adaptive_spp < 0) {
        spdlog::error("adaptive_base_samples and adaptive_spp must be non-negative");
        return 1;
    }
    if (adaptive_base_samples > adaptive_spp) {
        spdlog::error("adaptive_base_samples cannot exceed adaptive_spp");
        return 1;
    }
    if (adaptive_sigma_max <= 0.0f) {
        spdlog::error("adaptive_sigma_max must be positive");
        return 1;
    }
    if (adaptive_passes < 0) {
        spdlog::error("adaptive_passes must be non-negative");
        return 1;
    }
    if (adaptive_importance_smoothing_radius < 0) {
        spdlog::error("adaptive_importance_smoothing_radius must be non-negative");
        return 1;
    }
    float rpf_shrinkage_scale = std::isnan(rpf_shrinkage_scale_cli) ? render_config.rpf_shrinkage_scale : rpf_shrinkage_scale_cli;
    if (!std::isfinite(rpf_shrinkage_scale) || rpf_shrinkage_scale < 0.0f) {
        spdlog::error("rpf_shrinkage_scale must be finite and non-negative");
        return 1;
    }
    float rpf_confidence_samples = std::isnan(rpf_confidence_samples_cli) ? render_config.rpf_confidence_samples : rpf_confidence_samples_cli;
    if (!std::isfinite(rpf_confidence_samples) || rpf_confidence_samples <= 0.0f) {
        spdlog::error("rpf_confidence_samples must be finite and positive");
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
    render_config.color_window_radius = color_window_radius;
    render_config.color_normal_threshold = statmc_normal_thresh;
    render_config.color_depth_threshold = statmc_depth_thresh;
    render_config.color_compat_alpha = color_compat_alpha;
    render_config.color_sigma_max = color_sigma_max;
    render_config.var_window_radius = var_window_radius;
    render_config.var_normal_threshold = var_normal_thresh;
    render_config.var_depth_threshold = var_depth_thresh;
    render_config.var_compat_sigma = var_compat_sigma;
    render_config.var_shrinkage_k = var_shrinkage_k;
    render_config.var_iterations = var_iterations;
    render_config.adaptive_base_samples = adaptive_base_samples;
    render_config.adaptive_spp = adaptive_spp;
    render_config.adaptive_sigma_max = adaptive_sigma_max;
    render_config.adaptive_passes = adaptive_passes;
    render_config.adaptive_importance_smoothing_radius = adaptive_importance_smoothing_radius;
    render_config.samples_per_pixel = samples_per_pixel;
    render_config.rpf_shrinkage_scale = rpf_shrinkage_scale;
    render_config.rpf_confidence_samples = rpf_confidence_samples;

    spdlog::info("Image: {}x{}", render_config.image_width, render_config.image_height);
    spdlog::info("Samples per pixel: {}", render_config.samples_per_pixel);
    spdlog::info("Camera position: [{}, {}, {}]", render_config.camera_position.x(), render_config.camera_position.y(), render_config.camera_position.z());
    spdlog::info("Scene: {}", scene_to_render);
    spdlog::info("Output directory: {}", output_dir_path.string());
    spdlog::info("StatMC enabled: {}", render_config.use_statmc);
    spdlog::info("Color window radius: {}", render_config.color_window_radius);
    spdlog::info("Sample-variance window radius: {}", render_config.var_window_radius);
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
    g_sampler_base_seed = seed;

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
        spdlog::info("Clearing output directory '{}'", output_dir_path.string());
        std::error_code clear_ec;
        std::filesystem::remove_all(output_dir_path, clear_ec);
        if (clear_ec) {
            spdlog::error("Failed to clear output directory '{}': {}", output_dir_path.string(), clear_ec.message());
            return 1;
        }
    }

    std::error_code create_ec;
    if (!std::filesystem::exists(output_dir_path) && !std::filesystem::create_directories(output_dir_path, create_ec) && create_ec) {
        spdlog::error("Failed to create output directory '{}': {}", output_dir_path.string(), create_ec.message());
        return 1;
    }

    const std::filesystem::path output_path = output_dir_path / output_dir_name;

    render(render_config, camera);

    if (output_buffers(buffers, output_path.string(), tonemap_preset, render_config.use_statmc)) {
        spdlog::info("Successfully saved output to: {}", output_dir_path.string());
        return 0;
    }

    spdlog::error("Failed to save image");
    return 1;
}
