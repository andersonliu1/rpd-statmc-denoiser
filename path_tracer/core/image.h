#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>
#include <cmath>

#include "common.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

struct Image {
    int width = 0;
    int height = 0;
    std::vector<Vec3f> pixels;

    enum class ToneMapping : uint8_t { AGXDefault, AGXGolden, AGXPunchy, ACES };

    static constexpr ToneMapping ToneMappingPreset = ToneMapping::AGXDefault; // Define Tonemapping Here

    Image() = default;
    Image(int w, int h) : width(w), height(h), pixels(w * h, Vec3f::Zero()) {}

    /// @brief Compute log-average luminance for exposure scaling.
    float log_average_luminance(float delta = 1e-4f) const {
        if (pixels.empty()) return 0.0f;
        double log_sum = 0.0;
        for (const Vec3f& px : pixels) {
            float lum = calc_luminance(px);
            log_sum += std::log(std::max(delta, lum));
        }
        double avg = log_sum / static_cast<double>(pixels.size());
        return static_cast<float>(std::exp(avg));
    }

    /// @brief ACES-like Tone Mapping: https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
    /// @param value 
    /// @return 
    Vec3f tone_map_Aces(const Vec3f& value) const {
        const float A = 2.51f;
        const float B = 0.03f;
        const float C = 2.43f;
        const float D = 0.59f;
        const float E = 0.14f;

        Vec3f color = 0.6f * value;
        color = (color.cwiseProduct(A * color + Vec3f::Ones() * B)).cwiseQuotient(color.cwiseProduct(C * color + Vec3f::Ones() * D) + Vec3f::Ones() * E);

        return clamp(color, 0.0f, 1.0f);
    }

    /// @brief AGX Tone Mapping: https://iolite-engine.com/blog_posts/minimal_agx_implementation
    /// @param value 
    /// @return 
    Vec3f tone_map_Agx(const Vec3f& value) const {
        auto mul_rows = [](const Vec3f& r0, const Vec3f& r1, const Vec3f& r2, const Vec3f& v) {
            return Vec3f(r0.dot(v), r1.dot(v), r2.dot(v));
        };

        const Vec3f inset_r0(0.842479062253094f, 0.0423282422610123f, 0.0423756549057051f);
        const Vec3f inset_r1(0.0784336f, 0.878468636469772f, 0.0791661274605434f);
        const Vec3f inset_r2(0.0792237451477643f, 0.0791661274605434f, 0.879142973793104f);
        Vec3f col = mul_rows(inset_r0, inset_r1, inset_r2, value).cwiseMax(Vec3f::Constant(1e-6f));

        constexpr float minEv = -12.47393f;
        constexpr float maxEv = 4.026069f;
        constexpr float evRange = maxEv - minEv;

        for (int i = 0; i < 3; ++i) {
            float encoded = std::log2(std::max(col[i], EPS_SMALL));
            encoded = std::clamp(encoded, minEv, maxEv);
            col[i] = (encoded - minEv) / evRange;
        }

        Vec3f x2 = col.cwiseProduct(col);
        Vec3f x4 = x2.cwiseProduct(x2);
        Vec3f x6 = x4.cwiseProduct(x2);
        Vec3f x2x = x2.cwiseProduct(col);
        Vec3f x4x = x4.cwiseProduct(col);
        Vec3f x6x = x6.cwiseProduct(col);
        col = -17.86f * x6x + 78.01f * x6
            - 126.7f * x4x + 92.06f * x4
            - 28.72f * x2x + 4.361f * x2
            - 0.1718f * col + Vec3f::Constant(0.002857f);

        Vec3f slope = Vec3f::Ones();
        Vec3f power = Vec3f::Ones();
        float saturation = 1.0f;
        if constexpr (ToneMappingPreset == ToneMapping::AGXGolden) {
            slope = Vec3f(1.0f, 0.9f, 0.5f);
            power = Vec3f::Constant(0.8f);
            saturation = 0.8f;
        } else if constexpr (ToneMappingPreset == ToneMapping::AGXPunchy) {
            slope = Vec3f::Ones();
            power = Vec3f::Constant(1.35f);
            saturation = 1.4f;
        }
        Vec3f val = (col.cwiseProduct(slope)).array().pow(power.array()).matrix();
        const Vec3f lw(0.2126f, 0.7152f, 0.0722f);
        float luma = val.dot(lw);
        val = Vec3f::Constant(luma) + saturation * (val - Vec3f::Constant(luma));

        const Vec3f outset_r0(1.19687900512017f, -0.0528968517574562f, -0.0529716355144438f);
        const Vec3f outset_r1(-0.0980208811401368f, 1.15190312990417f, -0.0989611768448433f);
        const Vec3f outset_r2(-0.0990297440797205f, -0.0980434501171241f, 1.15107367264116f);
        Vec3f result = mul_rows(outset_r0, outset_r1, outset_r2, val);
        return clamp(result, 0.0f, 1.0f);
    }

    bool save(const std::string &filename) const {
        std::vector<uint8_t> data(3 * width * height);
        for (int i = 0; i < width * height; ++i) {
            for (int j = 0; j < 3; ++j) {
                data[3 * i + j] = static_cast<uint8_t>(255.0f * std::max(0.0f, std::min(1.0f, pixels[i][j])));
            }
        }
        return stbi_write_png(filename.c_str(), width, height, 3, data.data(), sizeof(uint8_t) * 3 * width) != 0;
    }

    bool save_with_tonemapping(const std::string &filename) const {
        std::vector<uint8_t> data(3 * width * height);
        constexpr float target_luminance = 0.18f;
        float log_avg = log_average_luminance();
        float exposure = (log_avg > EPS_SMALL) ? target_luminance / log_avg : 1.0f;

        for (int i = 0; i < width * height; ++i) {
            Vec3f scaled = pixels[i] * exposure;
            Vec3f pixel = (ToneMappingPreset == ToneMapping::ACES) ? tone_map_Aces(scaled) : tone_map_Agx(scaled);
            for (int j = 0; j < 3; j++) {
                data[3 * i + j] = static_cast<unsigned char>(255.0f * std::max(0.0f, std::min(1.0f, pixel[j])));
            }
        }
        return stbi_write_png(filename.c_str(), width, height, 3, data.data(), sizeof(uint8_t) * 3 * width) != 0;
    }

    Vec3f &operator()(int x, int y) { return pixels[y * width + x]; }

    const Vec3f &operator()(int x, int y) const {
        return pixels[y * width + x];
    }
};
