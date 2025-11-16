#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>

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
        color = (color.cwiseProduct(A * color + Vec3f::Ones() * B))
              .cwiseQuotient(color.cwiseProduct(C * color + Vec3f::Ones() * D) + Vec3f::Ones() * E);
        return color.cwiseMax(0.0f).cwiseMin(1.0f);
    }

    /// @brief AGX Tone Mapping: https://iolite-engine.com/blog_posts/minimal_agx_implementation
    /// @param value 
    /// @return 
    Vec3f tone_map_Agx(const Vec3f& value) const {
        static const Mat3f inset = (Mat3f() <<
            0.842479062253094f, 0.0423282422610123f, 0.0423756549057051f,
            0.0784336f, 0.878468636469772f, 0.0784336f,
            0.0792237451477643f, 0.0791661274605434f, 0.879142973793104f
        ).finished();

        static const Mat3f outset = (Mat3f() <<
            1.19687900512017f, -0.0528968517574562f, -0.0529716355144438f,
            -0.0980208811401368f, 1.15190312990417f, -0.0980434501171241f,
            -0.0990297440797205f, -0.0989611768448433f, 1.15107367264116f
        ).finished();

        auto agx_contrast = [](const Vec3f& x) {
            Vec3f x2 = x.cwiseProduct(x);
            Vec3f x4 = x2.cwiseProduct(x2);
            Vec3f term6 = x4.cwiseProduct(x2);
            Vec3f term5 = x4.cwiseProduct(x);
            Vec3f term3 = x2.cwiseProduct(x);
            return 15.5f * term6 - 40.14f * term5 + 31.96f * x4
                 -6.868f * term3 + 0.4298f * x2 + 0.1191f * x
                 -Vec3f::Constant(0.00232f);
        };

        auto agx_look = [](const Vec3f& input) {
            Vec3f offset = Vec3f::Zero();
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

            Vec3f val = (input.cwiseProduct(slope) + offset)
                .array().pow(power.array()).matrix();
            const Vec3f lw(0.2126f, 0.7152f, 0.0722f);
            float luma = val.dot(lw);
            return Vec3f::Constant(luma) + saturation * (val - Vec3f::Constant(luma));
        };

        Vec3f c = inset * value;

        constexpr float minEv = -12.47393f;
        constexpr float maxEv = 4.026069f;
        constexpr float evRange = maxEv - minEv;

        for (int i = 0; i < 3; ++i) {
            float encoded = std::log2(std::max(c[i], EPS_SMALL));
            encoded = std::clamp(encoded, minEv, maxEv);
            c[i] = (encoded - minEv) / evRange;
        }

        c = agx_contrast(c);
        c = agx_look(c);
        c = outset * c;
        c = c.array().pow(2.2f).matrix();
        return c.cwiseMax(0.0f).cwiseMin(1.0f);
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
        for (int i = 0; i < width * height; ++i) {
            for (int j = 0; j < 3; j++) {
                Vec3f pixel = (ToneMappingPreset == ToneMapping::ACES) ? tone_map_Aces(pixels[i]) : tone_map_Agx(pixels[i]);
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
