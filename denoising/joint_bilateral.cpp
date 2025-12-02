#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <cstdio>

#include "CLI/CLI.hpp"
#include "yaml-cpp/yaml.h"
#include "shared/image.h"

float* load_image(char const* imageName, int& width, int& height, int& comp) {
    return stbi_loadf(imageName, &width, &height, &comp, 0);
}

bool filter(const char* raw_file, const char* normal_file, const char* albedo_file, float sigma_c, float sigma_n, float sigma_p, float sigma_a, const std::filesystem::path& output_dir) {
    int w, h, raw_comp, normal_comp, albedo_comp;

    float* raw = load_image(raw_file, w, h, raw_comp);
    float* normal = load_image(normal_file, w, h, normal_comp);
    float* albedo = load_image(albedo_file, w, h, albedo_comp);

    if (!raw || !normal || !albedo || raw_comp < 3 || normal_comp < 3 || albedo_comp < 3) {
        stbi_image_free(raw);
        stbi_image_free(normal);
        stbi_image_free(albedo);
        return false;
    }

    float* gaussian = (float*)malloc(w * h * 3 * sizeof(float));
    float* bilateral = (float*)malloc(w * h * 3 * sizeof(float));
    float* joint = (float*)malloc(w * h * 3 * sizeof(float));

    auto normalize = [](float& x, float& y, float& z) {
        float len_sq = x * x + y * y + z * z;
        if (len_sq > EPS_SMALL) {
            float inv_len = 1.0f / std::sqrt(len_sq);
            x *= inv_len;
            y *= inv_len;
            z *= inv_len;
        }
    };

    const int radius = std::max(1, (int)std::ceil(3.0f * sigma_p));

    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            float weight_sum_gauss = 0.0f, value_r_gauss = 0.0f, value_g_gauss = 0.0f, value_b_gauss = 0.0f;
            float weight_sum_color = 0.0f, value_r_color = 0.0f, value_g_color = 0.0f, value_b_color = 0.0f;
            float weight_sum_joint = 0.0f, value_r_joint = 0.0f, value_g_joint = 0.0f, value_b_joint = 0.0f;
            float red = raw[3 * (i * w + j)];
            float green = raw[3 * (i * w + j) + 1];
            float blue = raw[3 * (i * w + j) + 2];

            float n_i_r = normal[3 * (i * w + j)] * 2.0f - 1.0f;
            float n_i_g = normal[3 * (i * w + j) + 1] * 2.0f - 1.0f;
            float n_i_b = normal[3 * (i * w + j) + 2] * 2.0f - 1.0f;
            normalize(n_i_r, n_i_g, n_i_b);

            float a_i_r = albedo[3 * (i * w + j)];
            float a_i_g = albedo[3 * (i * w + j) + 1];
            float a_i_b = albedo[3 * (i * w + j) + 2];

            for (int y = i - radius; y < i + radius + 1; y++) {
                for (int x = j - radius; x < j + radius + 1; x++) {
                    int c = (int)(std::min(std::max(0.0f, (float)y), (float)h - 1));
                    int r = (int)(std::min(std::max(0.0f, (float)x), (float)w - 1));

                    float alpha = 0.5f;
                    float beta = 2.0f - alpha;

                    float dist_square_pixel = (float)((i - c) * (i - c) + (j - r) * (j - r));

                    float red_diff = (red - raw[3 * (c * w + r)]);
                    float green_diff = (green - raw[3 * (c * w + r) + 1]);
                    float blue_diff = (blue - raw[3 * (c * w + r) + 2]);

                    float dist_square_color = red_diff * red_diff + green_diff * green_diff + blue_diff * blue_diff;
                    dist_square_color *= alpha;

                    float n_j_r = normal[3 * (c * w + r)] * 2.0f - 1.0f;
                    float n_j_g = normal[3 * (c * w + r) + 1] * 2.0f - 1.0f;
                    float n_j_b = normal[3 * (c * w + r) + 2] * 2.0f - 1.0f;
                    normalize(n_j_r, n_j_g, n_j_b);

                    float a_red_diff = (a_i_r - albedo[3 * (c * w + r)]);
                    float a_green_diff = (a_i_g - albedo[3 * (c * w + r) + 1]);
                    float a_blue_diff = (a_i_b - albedo[3 * (c * w + r) + 2]);

                    float dist_square_albedo = a_red_diff * a_red_diff + a_green_diff * a_green_diff + a_blue_diff * a_blue_diff;

                    float dot_product = n_i_r * n_j_r + n_i_g * n_j_g + n_i_b * n_j_b;
                    dot_product = std::clamp(dot_product, -1.0f, 1.0f);

                    float n_val = acos(dot_product);

                    float n_val_square = n_val * n_val;
                    n_val_square *= beta;

                    float weight_gauss = exp(-dist_square_pixel / (2.0f * sigma_p * sigma_p));
                    value_r_gauss += weight_gauss * raw[3 * (c * w + r)];
                    value_g_gauss += weight_gauss * raw[3 * (c * w + r) + 1];
                    value_b_gauss += weight_gauss * raw[3 * (c * w + r) + 2];

                    float weight_color = exp(-dist_square_pixel / (2.0f * sigma_p * sigma_p) - dist_square_color / (2.0f * sigma_c * sigma_c));
                    value_r_color += weight_color * raw[3 * (c * w + r)];
                    value_g_color += weight_color * raw[3 * (c * w + r) + 1];
                    value_b_color += weight_color * raw[3 * (c * w + r) + 2];

                    float weight_joint = exp(-dist_square_pixel / (2.0f * sigma_p * sigma_p) - dist_square_color / (2.0f * sigma_c * sigma_c) - n_val_square / (2.0f * sigma_n * sigma_n) - dist_square_albedo / (2.0f * sigma_a * sigma_a));
                    value_r_joint += weight_joint *  raw[3 * (c * w + r)];
                    value_g_joint += weight_joint * raw[3 * (c * w + r) + 1];
                    value_b_joint += weight_joint * raw[3 * (c * w + r) + 2];

                    weight_sum_gauss += weight_gauss;
                    weight_sum_color += weight_color;
                    weight_sum_joint += weight_joint;
                }
            }

            gaussian[3 * (i * w + j)] = value_r_gauss / weight_sum_gauss;
            gaussian[3 * (i * w + j) + 1] = value_g_gauss / weight_sum_gauss;
            gaussian[3 * (i * w + j) + 2] = value_b_gauss / weight_sum_gauss;

            bilateral[3 * (i * w + j)] = value_r_color / weight_sum_color;
            bilateral[3 * (i * w + j) + 1] = value_g_color / weight_sum_color;
            bilateral[3 * (i * w + j) + 2] = value_b_color / weight_sum_color;

            joint[3 * (i * w + j)] = value_r_joint / weight_sum_joint;
            joint[3 * (i * w + j) + 1] = value_g_joint / weight_sum_joint;
            joint[3 * (i * w + j) + 2] = value_b_joint / weight_sum_joint;
        }
    }

    const std::string gaussian_path = (output_dir / "gaussian.hdr").string();
    const std::string bilateral_path = (output_dir / "bilateral.hdr").string();
    const std::string joint_path = (output_dir / "joint.hdr").string();

    stbi_write_hdr(gaussian_path.c_str(), w, h, 3, gaussian);
    stbi_write_hdr(bilateral_path.c_str(), w, h, 3, bilateral);
    stbi_write_hdr(joint_path.c_str(), w, h, 3, joint);

    // Save PNG versions with tone mapping
    Image img_gaussian(w, h);
    Image img_bilateral(w, h);
    Image img_joint(w, h);

    for (int i = 0; i < w * h; i++) {
        img_gaussian.pixels[i] = Vec3f(gaussian[3 * i], gaussian[3 * i + 1], gaussian[3 * i + 2]);
        img_bilateral.pixels[i] = Vec3f(bilateral[3 * i], bilateral[3 * i + 1], bilateral[3 * i + 2]);
        img_joint.pixels[i] = Vec3f(joint[3 * i], joint[3 * i + 1], joint[3 * i + 2]);
    }

    const std::string gaussian_png = (output_dir / "gaussian.png").string();
    const std::string bilateral_png = (output_dir / "bilateral.png").string();
    const std::string joint_png = (output_dir / "joint.png").string();

    const Image::ToneMapping tonemap = Image::ToneMapping::AGXDefault;
    img_gaussian.save_with_tonemapping(gaussian_png, tonemap);
    img_bilateral.save_with_tonemapping(bilateral_png, tonemap);
    img_joint.save_with_tonemapping(joint_png, tonemap);

    stbi_image_free(raw);
    stbi_image_free(normal);
    stbi_image_free(albedo);
    free(gaussian);
    free(bilateral);
    free(joint);

    return true;
}

struct JointBilateralConfig {
    std::string raw_path;
    std::string normal_path;
    std::string albedo_path;
    std::string output_dir = "output/denoise";
    float sigma_c = 1.0f;
    float sigma_n = 1.0f;
    float sigma_p = 1.0f;
    float sigma_a = 1.0f;
};

JointBilateralConfig parse_denoise_config(const YAML::Node& node) {
    JointBilateralConfig cfg;

    auto require_string = [&](const char* key) -> std::string {
        if (!node[key]) {
            throw std::runtime_error(std::string("Missing required key '") + key + "' in config");
        }
        return node[key].as<std::string>();
    };

    cfg.raw_path = require_string("raw");
    cfg.normal_path = require_string("normal");
    cfg.albedo_path = require_string("albedo");

    if (node["output"]) cfg.output_dir = node["output"].as<std::string>();

    auto read_sigma = [&](const char* key, float& target) {
        const std::string keyed = std::string("sigma_") + key;
        if (node["sigma"] && node["sigma"][key]) {
            target = node["sigma"][key].as<float>();
        } else if (node[keyed]) {
            target = node[keyed].as<float>();
        }
    };

    read_sigma("c", cfg.sigma_c);
    read_sigma("n", cfg.sigma_n);
    read_sigma("p", cfg.sigma_p);
    read_sigma("a", cfg.sigma_a);

    return cfg;
}

int main(int argc, char** argv) {
    CLI::App app{"Joint bilateral/gaussian/bilateral denoising"};

    std::string config_file;
    std::string raw_cli, normal_cli, albedo_cli, output_dir_cli;
    std::optional<float> sigma_c_cli, sigma_n_cli, sigma_p_cli, sigma_a_cli;

    app.add_option("-c,--config", config_file, "Path to config YAML file")->required();
    app.add_option("--raw", raw_cli, "Raw color HDR file path (overrides config)");
    app.add_option("--normal", normal_cli, "Normal HDR file path (overrides config)");
    app.add_option("--albedo", albedo_cli, "Albedo HDR file path (overrides config)");
    app.add_option("--sigma-c", sigma_c_cli, "Color sigma (overrides config)");
    app.add_option("--sigma-n", sigma_n_cli, "Normal sigma (overrides config)");
    app.add_option("--sigma-p", sigma_p_cli, "Pixel/position sigma (overrides config)");
    app.add_option("--sigma-a", sigma_a_cli, "Albedo sigma (overrides config)");
    app.add_option("-o,--output", output_dir_cli, "Output directory for denoised files (overrides config)");

    CLI11_PARSE(app, argc, argv);

    YAML::Node config;
    try {
        config = YAML::LoadFile(config_file);
    } catch (const std::exception& e) {
        fprintf(stderr, "Failed to load config '%s': %s\n", config_file.c_str(), e.what());
        return 1;
    }

    JointBilateralConfig denoise_cfg;
    try {
        denoise_cfg = parse_denoise_config(config);
    } catch (const std::exception& e) {
        fprintf(stderr, "Failed to parse config: %s\n", e.what());
        return 1;
    }

    if (!raw_cli.empty()) denoise_cfg.raw_path = raw_cli;
    if (!normal_cli.empty()) denoise_cfg.normal_path = normal_cli;
    if (!albedo_cli.empty()) denoise_cfg.albedo_path = albedo_cli;
    if (sigma_c_cli) denoise_cfg.sigma_c = *sigma_c_cli;
    if (sigma_n_cli) denoise_cfg.sigma_n = *sigma_n_cli;
    if (sigma_p_cli) denoise_cfg.sigma_p = *sigma_p_cli;
    if (sigma_a_cli) denoise_cfg.sigma_a = *sigma_a_cli;
    if (!output_dir_cli.empty()) denoise_cfg.output_dir = output_dir_cli;

    std::filesystem::path output_dir_path = std::filesystem::path(denoise_cfg.output_dir).lexically_normal();
    if (output_dir_path.empty()) {
        fprintf(stderr, "Resolved output directory is empty\n");
        return 1;
    }

    std::error_code ec;
    if (!std::filesystem::exists(output_dir_path, ec)) {
        if (!std::filesystem::create_directories(output_dir_path, ec) && ec) {
            fprintf(stderr, "Failed to create output directory '%s': %s\n", output_dir_path.string().c_str(), ec.message().c_str());
            return 1;
        }
    } else if (!std::filesystem::is_directory(output_dir_path, ec)) {
        fprintf(stderr, "Output path '%s' exists but is not a directory\n", output_dir_path.string().c_str());
        return 1;
    }

    fprintf(stdout, "Raw: %s\n", denoise_cfg.raw_path.c_str());
    fprintf(stdout, "Normal: %s\n", denoise_cfg.normal_path.c_str());
    fprintf(stdout, "Albedo: %s\n", denoise_cfg.albedo_path.c_str());
    fprintf(stdout, "Sigmas -> c: %.4f, n: %.4f, p: %.4f, a: %.4f\n", denoise_cfg.sigma_c, denoise_cfg.sigma_n, denoise_cfg.sigma_p, denoise_cfg.sigma_a);
    fprintf(stdout, "Output directory: %s\n", output_dir_path.string().c_str());

    auto sigma_positive = [](float v) { return v > 0.0f; };
    if (!sigma_positive(denoise_cfg.sigma_c) || !sigma_positive(denoise_cfg.sigma_n) || !sigma_positive(denoise_cfg.sigma_p) || !sigma_positive(denoise_cfg.sigma_a)) {
        fprintf(stderr, "All sigma values must be positive\n");
        return 1;
    }

    if (filter(denoise_cfg.raw_path.c_str(),
               denoise_cfg.normal_path.c_str(),
               denoise_cfg.albedo_path.c_str(),
               denoise_cfg.sigma_c,
               denoise_cfg.sigma_n,
               denoise_cfg.sigma_p,
               denoise_cfg.sigma_a,
               output_dir_path)) {
        fprintf(stdout, "Finished denoising. Outputs saved to '%s'\n", output_dir_path.string().c_str());
        return 0;
    }

    fprintf(stderr, "Denoising failed\n");
    return 1;
}
