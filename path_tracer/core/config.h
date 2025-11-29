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

    return render_config;
}
