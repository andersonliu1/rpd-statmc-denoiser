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
    std::vector<float> sensitivity;
    Image denoised;
    std::vector<float> uncertainty;
    std::vector<float> var_mean_denoised;
    std::vector<float> depth;
    std::vector<int> hit_count;
    std::vector<int> sample_count;

    void init(int w, int h) {
        radiance = Image(w, h);
        albedo = Image(w, h);
        normal = Image(w, h);
        world_pos = Image(w,h);
        sensitivity.assign(w * h, 0.0f);
        denoised = Image(w, h);
        uncertainty.assign(w * h, 0.0f);
        var_mean_denoised.assign(w * h, 0.0f);
        depth.assign(w * h, 0.0f);
        hit_count.assign(w * h, 0);
        sample_count.assign(w * h, 0);
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
        ok &= save_scalar_buffer(fb.sensitivity, "_sensitivity.hdr");
        ok &= save_scalar_buffer(fb.uncertainty, "_uncertainty.hdr");
        ok &= save_scalar_buffer(fb.var_mean_denoised, "_vardenoised.hdr");
    }

    ok &= save_image_if_valid(fb.albedo, "_albedo.hdr");
    ok &= save_image_if_valid(fb.normal, "_normal.hdr");
    ok &= save_image_if_valid(fb.world_pos, "_worldpos.hdr");
    ok &= save_scalar_buffer(fb.depth, "_depth.hdr");

    return ok;
}
