#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <stdexcept>
#include <vector>

#include "common.h"
#include "yaml-cpp/yaml.h"

struct RenderConfig {
    int image_width;
    int image_height;
    int samples_per_pixel;
    float fov;
    float focus_distance;
    float aperture;
    Vec3f camera_position;
    Vec3f camera_direction;
    Vec3f camera_up;
    std::optional<std::string> scene_name;
    std::optional<std::string> output_dir;
    bool use_statmc = false;
    int rpf_tile_size = 8;
    int color_window_radius = 7;
    float color_normal_threshold = 0.5f;
    float color_depth_threshold = 0.25f;
    float color_compat_alpha = 0.05f;
    float color_sigma_max = 6.0f;
    int var_window_radius = 1;
    float var_normal_threshold = 0.5f;
    float var_depth_threshold = 0.25f;
    float var_compat_sigma = 30.0f;
    float var_shrinkage_k = 1e-3f;
    int var_iterations = 2;
    int adaptive_base_samples = 0;
    int adaptive_spp = 0;
    float adaptive_sigma_max = 3.0f;
    int adaptive_passes = 1;
    int adaptive_importance_smoothing_radius = 1;
    std::string tonemap = "agx";
    float rpf_shrinkage_scale = 1.0f;
    float rpf_confidence_samples = 128.0f;
    bool debug_statmc_outputs = false;
};

inline RenderConfig parse_render_config(const YAML::Node& config) {
    if (!config.IsMap()) throw std::runtime_error("Render config must be a map");

    static constexpr auto known_keys = std::to_array<std::string_view>({
        "image_width", "image_height", "samples_per_pixel", "fov", "focus_distance", "aperture",
        "camera_position", "camera_direction", "world_up", "scene", "output", "statmc_enabled",
        "rpf_tile_size", "color_window_radius", "color_normal_threshold", "color_depth_threshold",
        "color_compat_alpha", "color_sigma_max", "var_window_radius", "var_normal_threshold",
        "var_depth_threshold", "var_compat_sigma", "var_shrinkage_k", "var_iterations",
        "adaptive_base_samples", "adaptive_spp", "adaptive_sigma_max", "adaptive_passes",
        "adaptive_importance_smoothing_radius", "tonemap", "rpf_shrinkage_scale",
        "rpf_confidence_samples", "debug_statmc_outputs"
    });
    for (const auto& entry : config) {
        const std::string key = entry.first.as<std::string>();
        if (std::find(known_keys.begin(), known_keys.end(), key) == known_keys.end()) {
            throw std::runtime_error("Unknown render config key: " + key);
        }
    }

    RenderConfig render_config{};
    render_config.image_width = config["image_width"].as<int>();
    render_config.image_height = config["image_height"].as<int>();
    render_config.samples_per_pixel = config["samples_per_pixel"].as<int>();
    render_config.fov = config["fov"].as<float>();

    auto cam_pos = config["camera_position"].as<std::vector<float>>();
    auto cam_direction = config["camera_direction"].as<std::vector<float>>();
    std::vector<float> cam_up_vec = config["world_up"] ? config["world_up"].as<std::vector<float>>() : std::vector<float>{0.0f, 1.0f, 0.0f};
    if (cam_pos.size() != 3 || cam_direction.size() != 3) {
        throw std::runtime_error("Camera vectors must have three components");
    }
    if (cam_up_vec.size() != 3) {
        throw std::runtime_error("world_up must have three components");
    }

    render_config.camera_position = Vec3f(cam_pos[0], cam_pos[1], cam_pos[2]);
    render_config.camera_direction = Vec3f(cam_direction[0], cam_direction[1], cam_direction[2]);
    render_config.camera_up = Vec3f(cam_up_vec[0], cam_up_vec[1], cam_up_vec[2]);
    render_config.focus_distance = config["focus_distance"] ? config["focus_distance"].as<float>() : render_config.camera_direction.norm();
    render_config.aperture = config["aperture"] ? config["aperture"].as<float>() : 0.0f;
    if (config["scene"]) render_config.scene_name = config["scene"].as<std::string>();
    if (config["output"]) render_config.output_dir = config["output"].as<std::string>();

    if (config["statmc_enabled"]) render_config.use_statmc = config["statmc_enabled"].as<bool>();
    if (config["rpf_tile_size"]) render_config.rpf_tile_size = config["rpf_tile_size"].as<int>();
    if (config["color_window_radius"]) render_config.color_window_radius = config["color_window_radius"].as<int>();
    if (config["color_normal_threshold"]) render_config.color_normal_threshold = config["color_normal_threshold"].as<float>();
    if (config["color_depth_threshold"]) render_config.color_depth_threshold = config["color_depth_threshold"].as<float>();
    if (config["color_compat_alpha"]) render_config.color_compat_alpha = config["color_compat_alpha"].as<float>();
    if (config["color_sigma_max"]) render_config.color_sigma_max = config["color_sigma_max"].as<float>();
    if (config["var_window_radius"]) render_config.var_window_radius = config["var_window_radius"].as<int>();
    if (config["var_normal_threshold"]) render_config.var_normal_threshold = config["var_normal_threshold"].as<float>();
    if (config["var_depth_threshold"]) render_config.var_depth_threshold = config["var_depth_threshold"].as<float>();
    if (config["var_compat_sigma"]) render_config.var_compat_sigma = config["var_compat_sigma"].as<float>();
    if (config["var_shrinkage_k"]) render_config.var_shrinkage_k = config["var_shrinkage_k"].as<float>();
    if (config["var_iterations"]) render_config.var_iterations = config["var_iterations"].as<int>();
    if (config["adaptive_base_samples"]) render_config.adaptive_base_samples = config["adaptive_base_samples"].as<int>();
    if (config["adaptive_spp"]) render_config.adaptive_spp = config["adaptive_spp"].as<int>();
    if (config["adaptive_sigma_max"]) render_config.adaptive_sigma_max = config["adaptive_sigma_max"].as<float>();
    if (config["adaptive_passes"]) render_config.adaptive_passes = config["adaptive_passes"].as<int>();
    if (config["adaptive_importance_smoothing_radius"]) render_config.adaptive_importance_smoothing_radius = config["adaptive_importance_smoothing_radius"].as<int>();
    if (config["rpf_shrinkage_scale"]) render_config.rpf_shrinkage_scale = config["rpf_shrinkage_scale"].as<float>();
    if (config["rpf_confidence_samples"]) render_config.rpf_confidence_samples = config["rpf_confidence_samples"].as<float>();
    if (config["debug_statmc_outputs"]) render_config.debug_statmc_outputs = config["debug_statmc_outputs"].as<bool>();
    if (render_config.image_width <= 0 || render_config.image_height <= 0) {
        throw std::runtime_error("image_width and image_height must be positive");
    }
    if (render_config.samples_per_pixel <= 0) {
        throw std::runtime_error("samples_per_pixel must be positive");
    }
    if (!std::isfinite(render_config.fov) || render_config.fov <= 0.0f || render_config.fov >= 180.0f) {
        throw std::runtime_error("fov must be finite and in (0,180)");
    }
    if (!render_config.camera_position.allFinite() || !render_config.camera_direction.allFinite() ||
        !render_config.camera_up.allFinite() || render_config.camera_direction.squaredNorm() <= EPS_SMALL ||
        render_config.camera_up.squaredNorm() <= EPS_SMALL) {
        throw std::runtime_error("Camera vectors must be finite and direction/up must be nonzero");
    }
    if (!std::isfinite(render_config.focus_distance) || render_config.focus_distance <= 0.0f) {
        throw std::runtime_error("focus_distance must be finite and positive");
    }
    if (!std::isfinite(render_config.aperture) || render_config.aperture < 0.0f) {
        throw std::runtime_error("aperture must be finite and non-negative");
    }
    if (render_config.rpf_tile_size <= 0) {
        throw std::runtime_error("rpf_tile_size must be positive");
    }
    if (render_config.color_window_radius <= 0) {
        throw std::runtime_error("color_window_radius must be positive");
    }
    if (!std::isfinite(render_config.color_normal_threshold) || render_config.color_normal_threshold <= 0.0f || render_config.color_normal_threshold > 1.0f) {
        throw std::runtime_error("color_normal_threshold must be finite and in (0,1]");
    }
    if (!std::isfinite(render_config.color_depth_threshold) || render_config.color_depth_threshold < 0.0f) {
        throw std::runtime_error("color_depth_threshold must be finite and non-negative");
    }
    if (!std::isfinite(render_config.color_compat_alpha) || render_config.color_compat_alpha <= 0.0f || render_config.color_compat_alpha >= 1.0f) {
        throw std::runtime_error("color_compat_alpha must be finite and in (0,1)");
    }
    if (!std::isfinite(render_config.color_sigma_max) || render_config.color_sigma_max <= 0.0f) {
        throw std::runtime_error("color_sigma_max must be finite and positive");
    }
    if (render_config.var_window_radius <= 0) {
        throw std::runtime_error("var_window_radius must be positive");
    }
    if (!std::isfinite(render_config.var_normal_threshold) || render_config.var_normal_threshold <= 0.0f || render_config.var_normal_threshold > 1.0f) {
        throw std::runtime_error("var_normal_threshold must be finite and in (0,1]");
    }
    if (!std::isfinite(render_config.var_depth_threshold) || render_config.var_depth_threshold < 0.0f) {
        throw std::runtime_error("var_depth_threshold must be finite and non-negative");
    }
    if (!std::isfinite(render_config.var_compat_sigma) || render_config.var_compat_sigma <= 0.0f) {
        throw std::runtime_error("var_compat_sigma must be finite and positive");
    }
    if (!std::isfinite(render_config.var_shrinkage_k) || render_config.var_shrinkage_k <= 0.0f) {
        throw std::runtime_error("var_shrinkage_k must be finite and positive");
    }
    if (render_config.adaptive_base_samples < 0) {
        throw std::runtime_error("adaptive_base_samples must be non-negative");
    }
    if (render_config.adaptive_spp < 0) {
        throw std::runtime_error("adaptive_spp must be non-negative");
    }
    if (render_config.adaptive_base_samples > render_config.adaptive_spp) {
        throw std::runtime_error("adaptive_base_samples cannot exceed adaptive_spp");
    }
    if (!std::isfinite(render_config.adaptive_sigma_max) || render_config.adaptive_sigma_max <= 0.0f) {
        throw std::runtime_error("adaptive_sigma_max must be finite and positive");
    }
    if (render_config.adaptive_passes < 0) {
        throw std::runtime_error("adaptive_passes must be non-negative");
    }
    if (render_config.adaptive_importance_smoothing_radius < 0) {
        throw std::runtime_error("adaptive_importance_smoothing_radius must be non-negative");
    }
    if (render_config.var_iterations <= 0) {
        throw std::runtime_error("var_iterations must be positive");
    }
    if (!std::isfinite(render_config.rpf_shrinkage_scale) || render_config.rpf_shrinkage_scale < 0.0f) {
        throw std::runtime_error("rpf_shrinkage_scale must be finite and non-negative");
    }
    if (!std::isfinite(render_config.rpf_confidence_samples) || render_config.rpf_confidence_samples <= 0.0f) {
        throw std::runtime_error("rpf_confidence_samples must be finite and positive");
    }
    if (config["tonemap"]) {
        render_config.tonemap = config["tonemap"].as<std::string>();
    }

    return render_config;
}
