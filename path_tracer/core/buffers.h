#pragma once

#include <optional>
#include <string>
#include <vector>
#include <filesystem>

#include "common.h"
#include "shared/image.h"

struct FrameBuffers {
    Image radiance;
    Image albedo;
    Image normal;
    Image world_pos;
    std::vector<float> depth;
    std::vector<int> hit_count;

    void init(int w, int h) {
        radiance = Image(w, h);
        albedo = Image(w, h);
        normal = Image(w, h);
        world_pos = Image(w,h);
        depth.assign(w * h, 0.0f);
        hit_count.assign(w * h, 0);
    }

    void average(int index) {
      if (!hit_count[index]) return;
      albedo[index] /= hit_count[index];
      normal[index] /= hit_count[index];
      normal[index].normalize();
      world_pos[index] /= hit_count[index];
      depth[index] /= hit_count[index];
    }
};

inline bool output_buffers(const FrameBuffers& fb, const std::string& directory) {
    std::filesystem::path dir(directory);
    auto out_path = [&](const std::string& name) {
        return dir.string() + name;
    };

    bool ok = true;
    ok &= fb.radiance.save_with_tonemapping(out_path(".png"));
    ok &= fb.radiance.save(out_path("_raw.hdr"));
    ok &= fb.albedo.save(out_path("_albedo.hdr"));
    ok &= fb.normal.save(out_path("_normal.hdr"));
    ok &= fb.world_pos.save(out_path("_worldpos.hdr"));

    if (!fb.depth.empty()) {
        Image depth_img(fb.radiance.width, fb.radiance.height);
        for (int i = 0; i < fb.radiance.width * fb.radiance.height; ++i) {
            float v = fb.depth[i];
            depth_img[i] = Vec3f(v, v, v);
        }
        ok &= depth_img.save(out_path("_depth.hdr"));
    }

    return ok;
}
