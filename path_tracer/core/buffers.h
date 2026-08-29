#pragma once

#include <optional>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>

#include "common.h"
#include "shared/image.h"

struct FrameBuffers {
    Image radiance;
    Image albedo;
    Image normal;
    Image world_pos;
    struct SensitivityTiles {
        int support_size = 1;
        int step = 1;
        int nodes_x = 0;
        int nodes_y = 0;
        std::vector<float> all;
        std::vector<float> all_conf;
        std::vector<float> pixel;
        std::vector<float> pixel_conf;
        std::vector<float> brdf;
        std::vector<float> brdf_conf;
        std::vector<float> lens;
        std::vector<float> lens_conf;
        std::vector<float> light;
        std::vector<float> light_conf;
        std::vector<float> environment;
        std::vector<float> environment_conf;
        std::vector<float> rr;
        std::vector<float> rr_conf;

        void init(int width, int height, int tsize) {
            support_size = tsize;
            step = std::max(1, tsize / 2);
            nodes_x = std::max(2, (width + step - 1) / step + 1);
            nodes_y = std::max(2, (height + step - 1) / step + 1);
            const int size = nodes_x * nodes_y;
            all.assign(size, 0.0f);
            all_conf.assign(size, 0.0f);
            pixel.assign(size, 0.0f);
            pixel_conf.assign(size, 0.0f);
            brdf.assign(size, 0.0f);
            brdf_conf.assign(size, 0.0f);
            lens.assign(size, 0.0f);
            lens_conf.assign(size, 0.0f);
            light.assign(size, 0.0f);
            light_conf.assign(size, 0.0f);
            environment.assign(size, 0.0f);
            environment_conf.assign(size, 0.0f);
            rr.assign(size, 0.0f);
            rr_conf.assign(size, 0.0f);
        }

        float lookup(const std::vector<float>& tiles, int idx, int width) const {
            if (step <= 0 || tiles.empty() || nodes_x == 0 || nodes_y == 0) return 0.0f;
            int y = idx / width;
            int x = idx - y * width;
            const float gx = float(x) / float(step);
            const float gy = float(y) / float(step);
            const int x0 = std::clamp(static_cast<int>(std::floor(gx)), 0, nodes_x - 1);
            const int y0 = std::clamp(static_cast<int>(std::floor(gy)), 0, nodes_y - 1);
            const int x1 = std::min(x0 + 1, nodes_x - 1);
            const int y1 = std::min(y0 + 1, nodes_y - 1);
            const float tx = std::clamp(gx - float(x0), 0.0f, 1.0f);
            const float ty = std::clamp(gy - float(y0), 0.0f, 1.0f);
            const float v00 = at_2d(tiles, x0, y0, nodes_x);
            const float v10 = at_2d(tiles, x1, y0, nodes_x);
            const float v01 = at_2d(tiles, x0, y1, nodes_x);
            const float v11 = at_2d(tiles, x1, y1, nodes_x);
            const float top = lerp(v00, v10, tx);
            const float bottom = lerp(v01, v11, tx);
            return lerp(top, bottom, ty);
        }

        std::vector<float> expand_values(const std::vector<float>& values, int width, int height) const {
            std::vector<float> out(width * height, 0.0f);
            if (step <= 0 || nodes_x == 0 || nodes_y == 0 || values.empty()) return out;
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    at_2d(out, x, y, width) = lookup(values, y * width + x, width);
                }
            }
            return out;
        }

        std::vector<float> expand(int width, int height) const {
            return expand_values(all, width, height);
        }
    } sensitivity_tiles;
    Image denoised;
    std::vector<float> uncertainty;
    std::vector<float> alpha;
    std::vector<float> var_mean_denoised;
    std::vector<float> var_total_debug;
    std::vector<float> var_eff_debug;
    std::vector<float> var_ratio_debug;
    std::vector<float> depth;
    std::vector<float> sensitivity_confidence;
    std::vector<float> sensitivity_gradient;
    std::vector<float> light_visibility;
    std::vector<float> adaptive_importance;
    std::vector<int> hit_count;
    std::vector<int> sample_count;

    void init(int w, int h) {
        radiance = Image(w, h);
        albedo = Image(w, h);
        normal = Image(w, h);
        world_pos = Image(w,h);
        denoised = Image(w, h);
        uncertainty.assign(w * h, 0.0f);
        alpha.assign(w * h, 0.0f);
        var_mean_denoised.assign(w * h, 0.0f);
        var_total_debug.assign(w * h, 0.0f);
        var_eff_debug.assign(w * h, 0.0f);
        var_ratio_debug.assign(w * h, 0.0f);
        depth.assign(w * h, 0.0f);
        sensitivity_confidence.assign(w * h, 0.0f);
        sensitivity_gradient.assign(w * h, 0.0f);
        light_visibility.assign(w * h, 0.0f);
        adaptive_importance.assign(w * h, 0.0f);
        hit_count.assign(w * h, 0);
        sample_count.assign(w * h, 0);
    }

    void init_sensitivity(int tile_size) {
        if (tile_size <= 0 || radiance.width <= 0 || radiance.height <= 0) return;
        const int step = std::max(1, tile_size / 2);
        const int nodes_x = std::max(2, (radiance.width + step - 1) / step + 1);
        const int nodes_y = std::max(2, (radiance.height + step - 1) / step + 1);
        const int size = nodes_x * nodes_y;
        if (tile_size == sensitivity_tiles.support_size && step == sensitivity_tiles.step && size == static_cast<int>(sensitivity_tiles.all.size()) && !sensitivity_tiles.all.empty()) return;
        sensitivity_tiles.init(radiance.width, radiance.height, tile_size);
    }

    void finalize() {
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
        for (int i = 0; i < radiance.width * radiance.height; ++i) {
            int samples = std::max(1, sample_count[i]);
            int hits = std::max(1, hit_count[i]);

            radiance[i] /= float(samples);
            albedo[i] /= float(hits);
            normal[i] /= float(hits);
            if (normal[i].squaredNorm() > EPS_SMALL) normal[i].normalize();
            world_pos[i] /= float(hits);
            depth[i] /= float(hits);
        }
    }
};

inline Image scalar_to_image(const std::vector<float>& buffer, int w, int h) {
    Image img(w, h);
    const int size = w * h;
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int i = 0; i < size; ++i) {
        float v = (i < static_cast<int>(buffer.size())) ? buffer[i] : 0.0f;
        img[i] = Vec3f::Constant(v);
    }
    return img;
}

inline std::vector<float> normalized_sample_counts(const std::vector<int>& counts) {
    std::vector<float> out(counts.size(), 0.0f);
    if (counts.empty()) return out;
    const int max_count = std::max(1, *std::max_element(counts.begin(), counts.end()));
    const float inv_max = 1.0f / float(max_count);
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (size_t i = 0; i < counts.size(); ++i) {
        out[i] = float(std::max(0, counts[i])) * inv_max;
    }
    return out;
}

inline bool output_buffers(const FrameBuffers& fb, const std::string& directory, Image::ToneMapping tonemap, const bool statmc) {
    std::filesystem::path dir(directory);
    auto out_path = [&](const std::string& name) {
        return dir.string() + name;
    };

    auto save_scalar_buffer = [&](const std::vector<float>& buffer, const std::string& suffix) {
        if (buffer.empty()) return true;
        Image img = scalar_to_image(buffer, fb.radiance.width, fb.radiance.height);
        return img.save(out_path(suffix));
    };

    auto save_image_if_valid = [&](const Image& img, const std::string& suffix, bool tonemap_png = false) {
        if (img.width == 0 || img.height == 0) return true;
        return tonemap_png ? img.save_with_tonemapping(out_path(suffix), tonemap): img.save(out_path(suffix));
    };

    bool ok = true;
    ok &= save_image_if_valid(fb.radiance, ".png", true);
    ok &= save_image_if_valid(fb.radiance, ".hdr");

    if (statmc) {
        ok &= save_scalar_buffer(fb.sensitivity_tiles.expand(fb.radiance.width, fb.radiance.height), "_sensitivity.hdr");
        ok &= save_scalar_buffer(fb.sensitivity_tiles.expand_values(fb.sensitivity_tiles.pixel, fb.radiance.width, fb.radiance.height), "_sensitivity_pixel.hdr");
        ok &= save_scalar_buffer(fb.sensitivity_tiles.expand_values(fb.sensitivity_tiles.brdf, fb.radiance.width, fb.radiance.height), "_sensitivity_brdf.hdr");
        ok &= save_scalar_buffer(fb.sensitivity_tiles.expand_values(fb.sensitivity_tiles.lens, fb.radiance.width, fb.radiance.height), "_sensitivity_lens.hdr");
        ok &= save_scalar_buffer(fb.sensitivity_tiles.expand_values(fb.sensitivity_tiles.light, fb.radiance.width, fb.radiance.height), "_sensitivity_light.hdr");
        ok &= save_scalar_buffer(fb.sensitivity_tiles.expand_values(fb.sensitivity_tiles.environment, fb.radiance.width, fb.radiance.height), "_sensitivity_environment.hdr");
        ok &= save_scalar_buffer(fb.sensitivity_tiles.expand_values(fb.sensitivity_tiles.rr, fb.radiance.width, fb.radiance.height), "_sensitivity_rr.hdr");
        ok &= save_scalar_buffer(fb.sensitivity_confidence, "_sensitivity_confidence.hdr");
        ok &= save_scalar_buffer(fb.sensitivity_gradient, "_sensitivity_gradient.hdr");
        ok &= save_scalar_buffer(fb.light_visibility, "_light_visibility.hdr");
        ok &= save_scalar_buffer(fb.uncertainty, "_uncertainty.hdr");
        ok &= save_scalar_buffer(fb.alpha, "_alpha.hdr");
        ok &= save_scalar_buffer(fb.var_mean_denoised, "_vardenoised.hdr");
        ok &= save_scalar_buffer(fb.adaptive_importance, "_adaptive_importance.hdr");
        ok &= save_scalar_buffer(normalized_sample_counts(fb.sample_count), "_sample_fraction.hdr");
    }

    ok &= save_image_if_valid(fb.albedo, "_albedo.hdr");
    ok &= save_image_if_valid(fb.normal, "_normal.hdr");
    ok &= save_image_if_valid(fb.world_pos, "_worldpos.hdr");
    ok &= save_scalar_buffer(fb.depth, "_depth.hdr");
    if (statmc) {
        ok &= save_scalar_buffer(fb.var_total_debug, "_var_total.hdr");
        ok &= save_scalar_buffer(fb.var_eff_debug, "_var_eff.hdr");
        ok &= save_scalar_buffer(fb.var_ratio_debug, "_var_ratio.hdr");
    }

    return ok;
}
