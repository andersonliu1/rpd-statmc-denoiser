#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
#include <array>

#include "CLI/CLI.hpp"
#include "yaml-cpp/yaml.h"
#include "shared/image.h"

float *load_image(char const *imageName, int &width, int &height, int &comp)
{
    return stbi_loadf(imageName, &width, &height, &comp, 0);
}

static constexpr float h_kernel[5] = {1 / 16.0f, 1 / 4.0f, 3 / 8.0f, 1 / 4.0f, 1 / 16.0f};

static constexpr int kernel_offsets[25][2] = {
    {-2, -2}, {-1, -2}, {0, -2}, {1, -2}, {2, -2}, {-2, -1}, {-1, -1}, {0, -1}, {1, -1}, {2, -1}, {-2, 0}, {-1, 0}, {0, 0}, {1, 0}, {2, 0}, {-2, 1}, {-1, 1}, {0, 1}, {1, 1}, {2, 1}, {-2, 2}, {-1, 2}, {0, 2}, {1, 2}, {2, 2}};

static constexpr auto kernel_weights = []() constexpr
{
    std::array<float, 25> w{};
    for (int i = 0; i < 25; i++)
    {
        int ky = kernel_offsets[i][1] + 2;
        int kx = kernel_offsets[i][0] + 2;
        w[i] = h_kernel[ky] * h_kernel[kx];
    }
    return w;
}();

inline void normalize(float &x, float &y, float &z)
{
    float len_sq = x * x + y * y + z * z;
    if (len_sq > EPS_SMALL)
    {
        float inv_len = 1.0f / std::sqrt(len_sq);
        x *= inv_len;
        y *= inv_len;
        z *= inv_len;
    }
}

inline float luminance(float r, float g, float b)
{
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

void atrous_filter_pass(
    const float *input,
    float *output,
    const float *normal,
    const float *albedo,
    int w, int h,
    int step_width,
    float sigma_c,
    float sigma_n,
    float sigma_a)
{
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            const int idx = y * w + x;

            float c_r = input[3 * idx];
            float c_g = input[3 * idx + 1];
            float c_b = input[3 * idx + 2];
            float c_lum = luminance(c_r, c_g, c_b);

            float n_r = normal[3 * idx] * 2.0f - 1.0f;
            float n_g = normal[3 * idx + 1] * 2.0f - 1.0f;
            float n_b = normal[3 * idx + 2] * 2.0f - 1.0f;
            normalize(n_r, n_g, n_b);

            float a_r = albedo[3 * idx];
            float a_g = albedo[3 * idx + 1];
            float a_b = albedo[3 * idx + 2];

            float sum_r = 0.0f, sum_g = 0.0f, sum_b = 0.0f;
            float sum_weight = 0.0f;

            for (int k = 0; k < 25; k++)
            {
                int ox = kernel_offsets[k][0] * step_width;
                int oy = kernel_offsets[k][1] * step_width;

                int nx = std::clamp(x + ox, 0, w - 1);
                int ny = std::clamp(y + oy, 0, h - 1);
                int n_idx = ny * w + nx;

                float p_r = input[3 * n_idx];
                float p_g = input[3 * n_idx + 1];
                float p_b = input[3 * n_idx + 2];
                float p_lum = luminance(p_r, p_g, p_b);

                float pn_r = normal[3 * n_idx] * 2.0f - 1.0f;
                float pn_g = normal[3 * n_idx + 1] * 2.0f - 1.0f;
                float pn_b = normal[3 * n_idx + 2] * 2.0f - 1.0f;
                normalize(pn_r, pn_g, pn_b);

                float pa_r = albedo[3 * n_idx];
                float pa_g = albedo[3 * n_idx + 1];
                float pa_b = albedo[3 * n_idx + 2];

                float w_spatial = kernel_weights[k];

                float lum_diff = c_lum - p_lum;
                float w_color = exp(-lum_diff * lum_diff / (2.0f * sigma_c * sigma_c));

                float dot_n = n_r * pn_r + n_g * pn_g + n_b * pn_b;
                dot_n = std::clamp(dot_n, -1.0f, 1.0f);
                float angle = acos(dot_n);
                float w_normal = exp(-angle * angle / (2.0f * sigma_n * sigma_n));

                float da_r = a_r - pa_r;
                float da_g = a_g - pa_g;
                float da_b = a_b - pa_b;
                float dist_sq_albedo = da_r * da_r + da_g * da_g + da_b * da_b;
                float w_albedo = exp(-dist_sq_albedo / (2.0f * sigma_a * sigma_a));

                float weight = w_spatial * w_color * w_normal * w_albedo;

                sum_r += weight * p_r;
                sum_g += weight * p_g;
                sum_b += weight * p_b;
                sum_weight += weight;
            }

            if (sum_weight > EPS_SMALL)
            {
                output[3 * idx] = sum_r / sum_weight;
                output[3 * idx + 1] = sum_g / sum_weight;
                output[3 * idx + 2] = sum_b / sum_weight;
            }
            else
            {
                output[3 * idx] = c_r;
                output[3 * idx + 1] = c_g;
                output[3 * idx + 2] = c_b;
            }
        }
    }
}

bool filter(
    const char *raw_file,
    const char *normal_file,
    const char *albedo_file,
    float sigma_c,
    float sigma_n,
    float sigma_a,
    int num_iterations,
    const std::filesystem::path &output_dir)
{
    int w, h, raw_comp, normal_comp, albedo_comp;

    float *raw = load_image(raw_file, w, h, raw_comp);
    float *normal = load_image(normal_file, w, h, normal_comp);
    float *albedo = load_image(albedo_file, w, h, albedo_comp);

    if (!raw || !normal || !albedo || raw_comp < 3 || normal_comp < 3 || albedo_comp < 3)
    {
        stbi_image_free(raw);
        stbi_image_free(normal);
        stbi_image_free(albedo);
        return false;
    }

    const size_t buffer_size = w * h * 3 * sizeof(float);

    float *buffer_a = (float *)malloc(buffer_size);
    float *buffer_b = (float *)malloc(buffer_size);

    memcpy(buffer_a, raw, buffer_size);

    float *input = buffer_a;
    float *output = buffer_b;

    for (int iter = 0; iter < num_iterations; iter++)
    {
        int step_width = 1 << iter;

        atrous_filter_pass(
            input, output,
            normal, albedo,
            w, h,
            step_width,
            sigma_c, sigma_n, sigma_a);

        std::swap(input, output);
    }

    float *result = input;

    const std::string atrous_path = (output_dir / "atrous_wavelet.hdr").string();
    stbi_write_hdr(atrous_path.c_str(), w, h, 3, result);

    Image img(w, h);
    for (int i = 0; i < w * h; i++) {
        img.pixels[i] = Vec3f(result[3 * i], result[3 * i + 1], result[3 * i + 2]);
    }
    const std::string png_path = (output_dir / "atrous_wavelet.png").string();
    img.save_with_tonemapping(png_path);

    stbi_image_free(raw);
    stbi_image_free(normal);
    stbi_image_free(albedo);
    free(buffer_a);
    free(buffer_b);

    return true;
}

struct ATrousConfig
{
    std::string raw_path;
    std::string normal_path;
    std::string albedo_path;
    std::string output_dir = "output/denoise";
    float sigma_c = 0.5f;
    float sigma_n = 0.5f;
    float sigma_a = 0.1f;
    int num_iterations = 5;
};

ATrousConfig parse_config(const YAML::Node &node)
{
    ATrousConfig cfg;

    auto require_string = [&](const char *key) -> std::string
    {
        if (!node[key])
        {
            throw std::runtime_error(std::string("Missing required key '") + key + "' in config");
        }
        return node[key].as<std::string>();
    };

    cfg.raw_path = require_string("raw");
    cfg.normal_path = require_string("normal");
    cfg.albedo_path = require_string("albedo");

    if (node["output"])
        cfg.output_dir = node["output"].as<std::string>();
    if (node["iterations"])
        cfg.num_iterations = node["iterations"].as<int>();

    auto read_sigma = [&](const char *key, float &target)
    {
        const std::string keyed = std::string("sigma_") + key;
        if (node["sigma"] && node["sigma"][key])
        {
            target = node["sigma"][key].as<float>();
        }
        else if (node[keyed])
        {
            target = node[keyed].as<float>();
        }
    };

    read_sigma("c", cfg.sigma_c);
    read_sigma("n", cfg.sigma_n);
    read_sigma("a", cfg.sigma_a);

    return cfg;
}

int main(int argc, char **argv)
{
    CLI::App app{"A-trous wavelet denoising"};

    std::string config_file;
    std::string raw_cli, normal_cli, albedo_cli, output_dir_cli;
    std::optional<float> sigma_c_cli, sigma_n_cli, sigma_a_cli;
    std::optional<int> iterations_cli;

    app.add_option("-c,--config", config_file, "Path to config YAML file")->required();
    app.add_option("--raw", raw_cli, "Raw color HDR file path (overrides config)");
    app.add_option("--normal", normal_cli, "Normal HDR file path (overrides config)");
    app.add_option("--albedo", albedo_cli, "Albedo HDR file path (overrides config)");
    app.add_option("--sigma-c", sigma_c_cli, "Color sigma (overrides config)");
    app.add_option("--sigma-n", sigma_n_cli, "Normal sigma (overrides config)");
    app.add_option("--sigma-a", sigma_a_cli, "Albedo sigma (overrides config)");
    app.add_option("-i,--iterations", iterations_cli, "Number of filter iterations (overrides config)");
    app.add_option("-o,--output", output_dir_cli, "Output directory for denoised files (overrides config)");

    CLI11_PARSE(app, argc, argv);

    YAML::Node config;
    try
    {
        config = YAML::LoadFile(config_file);
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "Failed to load config '%s': %s\n", config_file.c_str(), e.what());
        return 1;
    }

    ATrousConfig atrous_cfg;
    try
    {
        atrous_cfg = parse_config(config);
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "Failed to parse config: %s\n", e.what());
        return 1;
    }

    if (!raw_cli.empty())
        atrous_cfg.raw_path = raw_cli;
    if (!normal_cli.empty())
        atrous_cfg.normal_path = normal_cli;
    if (!albedo_cli.empty())
        atrous_cfg.albedo_path = albedo_cli;
    if (sigma_c_cli)
        atrous_cfg.sigma_c = *sigma_c_cli;
    if (sigma_n_cli)
        atrous_cfg.sigma_n = *sigma_n_cli;
    if (sigma_a_cli)
        atrous_cfg.sigma_a = *sigma_a_cli;
    if (iterations_cli)
        atrous_cfg.num_iterations = *iterations_cli;
    if (!output_dir_cli.empty())
        atrous_cfg.output_dir = output_dir_cli;

    std::filesystem::path output_dir_path = std::filesystem::path(atrous_cfg.output_dir).lexically_normal();
    if (output_dir_path.empty())
    {
        fprintf(stderr, "Resolved output directory is empty\n");
        return 1;
    }

    std::error_code ec;
    if (!std::filesystem::exists(output_dir_path, ec))
    {
        if (!std::filesystem::create_directories(output_dir_path, ec) && ec)
        {
            fprintf(stderr, "Failed to create output directory '%s': %s\n", output_dir_path.string().c_str(), ec.message().c_str());
            return 1;
        }
    }
    else if (!std::filesystem::is_directory(output_dir_path, ec))
    {
        fprintf(stderr, "Output path '%s' exists but is not a directory\n", output_dir_path.string().c_str());
        return 1;
    }

    fprintf(stdout, "Raw: %s\n", atrous_cfg.raw_path.c_str());
    fprintf(stdout, "Normal: %s\n", atrous_cfg.normal_path.c_str());
    fprintf(stdout, "Albedo: %s\n", atrous_cfg.albedo_path.c_str());
    fprintf(stdout, "Sigmas -> c: %.4f, n: %.4f, a: %.4f\n", atrous_cfg.sigma_c, atrous_cfg.sigma_n, atrous_cfg.sigma_a);
    fprintf(stdout, "Iterations: %d\n", atrous_cfg.num_iterations);
    fprintf(stdout, "Output directory: %s\n", output_dir_path.string().c_str());

    auto sigma_positive = [](float v)
    { return v > 0.0f; };
    if (!sigma_positive(atrous_cfg.sigma_c) || !sigma_positive(atrous_cfg.sigma_n) || !sigma_positive(atrous_cfg.sigma_a))
    {
        fprintf(stderr, "All sigma values must be positive\n");
        return 1;
    }

    if (atrous_cfg.num_iterations < 1)
    {
        fprintf(stderr, "Number of iterations must be at least 1\n");
        return 1;
    }

    if (filter(
            atrous_cfg.raw_path.c_str(),
            atrous_cfg.normal_path.c_str(),
            atrous_cfg.albedo_path.c_str(),
            atrous_cfg.sigma_c,
            atrous_cfg.sigma_n,
            atrous_cfg.sigma_a,
            atrous_cfg.num_iterations,
            output_dir_path))
    {
        fprintf(stdout, "Finished denoising. Outputs saved to '%s'\n", output_dir_path.string().c_str());
        return 0;
    }

    fprintf(stderr, "Denoising failed\n");
    return 1;
}
