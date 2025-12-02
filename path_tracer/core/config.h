#pragma once

#include <optional>
#include <string>
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
    bool use_statmc = false; // StatMC denoiser flag
    int rpf_tile_size = 8;
    int rpf_target_samples = -1; // -1 = auto
    int rpf_max_radius = -1;     // -1 = auto
    int color_window_radius = 1;
    float color_normal_threshold = 0.95f;
    float color_depth_threshold = 0.01f;
    float color_compat_sigma = 1.5f;
    float color_shrinkage_k = 1e-3f;
    int var_window_radius = 1;
    float var_normal_threshold = 0.95f;
    float var_depth_threshold = 0.01f;
    float var_compat_sigma = 1.5f;
    float var_shrinkage_k = 1e-3f;
    int var_iterations = 2;
    int adaptive_base_samples = 0;
    int adaptive_spp = 0;
    float adaptive_sigma_max = 3.0f;
    int adaptive_passes = 1;
    std::string tonemap = "agx"; // tonemapping preset
};

inline RenderConfig parse_render_config(const YAML::Node& config) {
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
    if (config["rpf_target_samples"]) render_config.rpf_target_samples = config["rpf_target_samples"].as<int>();
    if (config["rpf_max_radius"]) render_config.rpf_max_radius = config["rpf_max_radius"].as<int>();
    if (config["color_window_radius"]) render_config.color_window_radius = config["color_window_radius"].as<int>();
    if (config["color_normal_threshold"]) render_config.color_normal_threshold = config["color_normal_threshold"].as<float>();
    if (config["color_depth_threshold"]) render_config.color_depth_threshold = config["color_depth_threshold"].as<float>();
    if (config["color_compat_sigma"]) render_config.color_compat_sigma = config["color_compat_sigma"].as<float>();
    if (config["color_shrinkage_k"]) render_config.color_shrinkage_k = config["color_shrinkage_k"].as<float>();
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
    if (render_config.rpf_tile_size <= 0) {
        throw std::runtime_error("rpf_tile_size must be positive");
    }
    if (render_config.rpf_target_samples == 0 || render_config.rpf_target_samples < -1) {
        throw std::runtime_error("rpf_target_samples must be -1 or positive");
    }
    if (render_config.rpf_max_radius < -1) {
        throw std::runtime_error("rpf_max_radius must be -1 or non-negative");
    }
    if (render_config.color_window_radius <= 0) {
        throw std::runtime_error("color.window_radius must be positive");
    }
    if (render_config.color_normal_threshold <= 0.0f || render_config.color_normal_threshold > 1.0f) {
        throw std::runtime_error("color.normal_threshold must be in (0,1]");
    }
    if (render_config.color_depth_threshold < 0.0f) {
        throw std::runtime_error("color.depth_threshold must be non-negative");
    }
    if (render_config.color_compat_sigma <= 0.0f) {
        throw std::runtime_error("color.compat_sigma must be positive");
    }
    if (render_config.color_shrinkage_k <= 0.0f) {
        throw std::runtime_error("color.shrinkage_k must be positive");
    }
    if (render_config.var_window_radius <= 0) {
        throw std::runtime_error("var.window_radius must be positive");
    }
    if (render_config.var_normal_threshold <= 0.0f || render_config.var_normal_threshold > 1.0f) {
        throw std::runtime_error("var.normal_threshold must be in (0,1]");
    }
    if (render_config.var_depth_threshold < 0.0f) {
        throw std::runtime_error("var.depth_threshold must be non-negative");
    }
    if (render_config.var_compat_sigma <= 0.0f) {
        throw std::runtime_error("var.compat_sigma must be positive");
    }
    if (render_config.var_shrinkage_k <= 0.0f) {
        throw std::runtime_error("var.shrinkage_k must be positive");
    }
    if (render_config.adaptive_base_samples < 0) {
        throw std::runtime_error("adaptive_base_samples must be non-negative");
    }
    if (render_config.adaptive_spp < 0) {
        throw std::runtime_error("adaptive_spp must be non-negative");
    }
    if (render_config.adaptive_sigma_max <= 0.0f) {
        throw std::runtime_error("adaptive_sigma_max must be positive");
    }
    if (render_config.adaptive_passes <= 0) {
        throw std::runtime_error("adaptive_passes must be positive");
    }
    if (render_config.var_iterations <= 0) {
        throw std::runtime_error("var.iterations must be positive");
    }
    if (config["tonemap"]) {
        render_config.tonemap = config["tonemap"].as<std::string>();
    }

    return render_config;
}
