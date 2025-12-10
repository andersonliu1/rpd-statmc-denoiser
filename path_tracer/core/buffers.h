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
        int tile_size = 1;
        int tiles_x = 0;
        int tiles_y = 0;
        std::vector<float> all;
        std::vector<float> brdf;
        std::vector<float> lens;
        std::vector<float> light;
        std::vector<float> rr;

        void init(int width, int height, int tsize) {
            tile_size = tsize;
            tiles_x = (tile_size > 0) ? (width + tile_size - 1) / tile_size : 0;
            tiles_y = (tile_size > 0) ? (height + tile_size - 1) / tile_size : 0;
            const int size = tiles_x * tiles_y;
            all.assign(size, 0.0f);
            brdf.assign(size, 0.0f);
            lens.assign(size, 0.0f);
            light.assign(size, 0.0f);
            rr.assign(size, 0.0f);
        }

        float lookup(const std::vector<float>& tiles, int idx, int width) const {
            if (tile_size <= 0 || tiles.empty() || tiles_x == 0 || tiles_y == 0) return 0.0f;
            int y = idx / width;
            int x = idx - y * width;
            int tx = std::min(tiles_x - 1, x / tile_size);
            int ty = std::min(tiles_y - 1, y / tile_size);
            return at_2d(tiles, tx, ty, tiles_x);
        }

        std::vector<float> expand(int width, int height) const {
            std::vector<float> out(width * height, 0.0f);
            if (tile_size <= 0 || tiles_x == 0 || tiles_y == 0 || all.empty()) return out;
            for (int y = 0; y < height; ++y) {
                int ty = std::min(tiles_y - 1, y / tile_size);
                for (int x = 0; x < width; ++x) {
                    int tx = std::min(tiles_x - 1, x / tile_size);
                    at_2d(out, x, y, width) = at_2d(all, tx, ty, tiles_x);
                }
            }
            return out;
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
        hit_count.assign(w * h, 0);
        sample_count.assign(w * h, 0);
    }

    void init_sensitivity(int tile_size) {
        if (tile_size <= 0 || radiance.width <= 0 || radiance.height <= 0) return;
        const int tiles_x = (radiance.width + tile_size - 1) / tile_size;
        const int tiles_y = (radiance.height + tile_size - 1) / tile_size;
        const int size = tiles_x * tiles_y;
        if (tile_size == sensitivity_tiles.tile_size && size == static_cast<int>(sensitivity_tiles.all.size()) && !sensitivity_tiles.all.empty()) return;
        sensitivity_tiles.init(radiance.width, radiance.height, tile_size);
    }

    void finalize() {
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

inline Image normalize_image(const Image& src, const std::vector<int>& counts, bool renormalize_normals = false) {
    Image out = src;
    const int size = src.width * src.height;
    for (int i = 0; i < size; ++i) {
        int c = (i < static_cast<int>(counts.size())) ? counts[i] : 0;
        c = std::max(1, c);
        out[i] /= float(c);
        if (renormalize_normals && out[i].squaredNorm() > EPS_SMALL) out[i].normalize();
    }
    return out;
}

inline Image scalar_to_image(const std::vector<float>& buffer, int w, int h, const std::vector<int>* counts = nullptr) {
    Image img(w, h);
    const int size = w * h;
    for (int i = 0; i < size; ++i) {
        float v = (i < static_cast<int>(buffer.size())) ? buffer[i] : 0.0f;
        if (counts) {
            int c = std::max(1, (*counts)[i]);
            v /= float(c);
        }
        img[i] = Vec3f::Constant(v);
    }
    return img;
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
        ok &= save_scalar_buffer(fb.uncertainty, "_uncertainty.hdr");
        ok &= save_scalar_buffer(fb.alpha, "_alpha.hdr");
        ok &= save_scalar_buffer(fb.var_mean_denoised, "_vardenoised.hdr");
    }

    ok &= save_image_if_valid(fb.albedo, "_albedo.hdr");
    ok &= save_image_if_valid(fb.normal, "_normal.hdr");
    ok &= save_image_if_valid(fb.world_pos, "_worldpos.hdr");
    ok &= save_scalar_buffer(fb.depth, "_depth.hdr");
    // Debug-only: total/effective variance and ratio; remove when not needed
    ok &= save_scalar_buffer(fb.var_total_debug, "_var_total.hdr");
    ok &= save_scalar_buffer(fb.var_eff_debug, "_var_eff.hdr");
    ok &= save_scalar_buffer(fb.var_ratio_debug, "_var_ratio.hdr");

    return ok;
}
