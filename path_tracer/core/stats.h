#pragma once
#include "common.h"
#include <cmath>

struct PixelStats {
    float mean = 0.0f, square_diff = 0.0f;
    int n = 0;

    Vec3f color_mean = Vec3f::Zero();

    float variance() const {
        return (n > 1) ? square_diff / float(n - 1) : 0.0f;
    }

    float variance_of_mean() const {
        return (n > 0) ? variance() / float(n) : 0.0f;
    }
};

static void accumulate_sample(PixelStats& s, const Vec3f& luminance) {
    const float sample_lum = calc_luminance(luminance);
    const float delta = sample_lum - s.mean;
    s.mean += delta / float(++s.n);
    const float delta2 = sample_lum - s.mean;
    s.square_diff += delta * delta2;

    const Vec3f color_delta = luminance - s.color_mean;
    s.color_mean += color_delta / float(s.n);
}

inline float compute_corr(const float sumX, const float sumY, const float sumXY, const float sumX2, const float sumY2, const int n) {
    if (n < 2) return 0.0f;
    float var_x = n * sumX2 - sumX * sumX;
    float var_y = n * sumY2 - sumY * sumY;
    float num = n * sumXY - sumX * sumY;
    float denom = sqrt(std::max(0.0f, var_x * var_y));
    if (denom <= EPS_SMALL) return 0.0f;
    return num / denom;
}

namespace rpf {

    struct Corr1D {
        float sum_x = 0.0f;
        float sum_x2 = 0.0f;
        float sum_xy = 0.0f;
        float sum_y = 0.0f;
        float sum_y2 = 0.0f;
        int n = 0;

        void add(float x, float y) {
            ++n;
            sum_x += x;
            sum_y += y;
            sum_xy += x * y;
            sum_x2 += x * x;
            sum_y2 += y * y;
        }

        float compute_sensitivity() const {
            float r = compute_corr(sum_x, sum_y, sum_xy, sum_x2, sum_y2, n);
            return r * r;
        }

        void reset() {
            sum_x = sum_x2 = sum_xy = sum_y = sum_y2 = 0.0f;
            n = 0;
        }

        void merge(const Corr1D& other) {
            sum_x += other.sum_x;
            sum_x2 += other.sum_x2;
            sum_xy += other.sum_xy;
            sum_y += other.sum_y;
            sum_y2 += other.sum_y2;
            n += other.n;
        }
    };

    struct Corr2D {
        float sum_x1 = 0.0f, sum_x12 = 0.0f, sum_x1y = 0.0f;
        float sum_x2 = 0.0f, sum_x22 = 0.0f, sum_x2y = 0.0f;
            float sum_x1x2 = 0.0f;
            float sum_y = 0.0f, sum_y2 = 0.0f;
            int n = 0;

            void add(float x1, float x2, float y) {
                ++n;
                sum_x1 += x1;
                sum_x2 += x2;
                sum_y += y;
                sum_x1y += x1 * y;
                sum_x2y += x2 * y;
                sum_x12 += x1 * x1;
                sum_x22 += x2 * x2;
                sum_x1x2 += x1 * x2;
                sum_y2 += y * y;
            }

            /// @brief PCA-based sensitivity
            /// @return 
            float compute_sensitivity() const {
                if (n < 2) return 0.0f;

                const float s11 = n * sum_x12 - sum_x1 * sum_x1;
                const float s22 = n * sum_x22 - sum_x2 * sum_x2;
                const float s12 = n * sum_x1x2 - sum_x1 * sum_x2;

                Mat2f scatter;
                scatter << s11, s12, s12, s22;
                if (scatter.norm() <= EPS_SMALL) return 0.0f;

                const float trace = s11 + s22;
                const float disc = (s11 - s22) * (s11 - s22) + 4.0f * s12 * s12;
                const float lambda_max = 0.5f * (trace + std::sqrt(std::max(0.0f, disc)));
                if (lambda_max <= EPS_SMALL) return 0.0f;

                Vec2f v;
                if (std::abs(s12) > EPS_SMALL) {
                    v = Vec2f(s12, lambda_max - s11);
                } else {
                    v = (s11 >= s22) ? Vec2f(1.0f, 0.0f) : Vec2f(0.0f, 1.0f);
                }
                const float norm = v.norm();
                if (norm <= EPS_SMALL) return 0.0f;
                const Vec2f axis = v / norm;

                const float proj_sum = axis.x() * sum_x1 + axis.y() * sum_x2;
                const float proj_sum2 = axis.x() * axis.x() * sum_x12 + 2.0f * axis.x() * axis.y() * sum_x1x2 + axis.y() * axis.y() * sum_x22;
                const float proj_sum_y = axis.x() * sum_x1y + axis.y() * sum_x2y;
                const float r = compute_corr(proj_sum, sum_y, proj_sum_y, proj_sum2, sum_y2, n);
                return r * r;
            }

        void reset() {
            sum_x1 = sum_x12 = sum_x1y = 0.0f;
            sum_x2 = sum_x22 = sum_x2y = 0.0f;
            sum_x1x2 = 0.0f;
            sum_y = sum_y2 = 0.0f;
            n = 0;
        }

        void merge(const Corr2D& other) {
            sum_x1 += other.sum_x1;
            sum_x12 += other.sum_x12;
            sum_x1y += other.sum_x1y;
            sum_x2 += other.sum_x2;
            sum_x22 += other.sum_x22;
            sum_x2y += other.sum_x2y;
            sum_x1x2 += other.sum_x1x2;
            sum_y += other.sum_y;
            sum_y2 += other.sum_y2;
            n += other.n;
        }
    };

    struct Tile {
        Corr2D brdf;
        Corr2D lens;
        Corr2D light;
        Corr1D rr;

        void reset() {
            brdf.reset();
            lens.reset();
            light.reset();
            rr.reset();
        }

        float computeTileSensitivity() const {
            float s_brdf = brdf.compute_sensitivity();
            float s_lens = lens.compute_sensitivity();
            float s_light = light.compute_sensitivity();
            float s_rr = rr.compute_sensitivity();
            float product = (1.0f - s_brdf) * (1.0f - s_lens) * (1.0f - s_light) * (1.0f - s_rr);
            return 1.0f - product;
        }
    };

    struct Grid {
        int tile_size = 1;
        int tiles_x = 0;
        int tiles_y = 0;
        std::vector<Tile> tiles;

        static constexpr int MIN_SAMPLES = 32;
        static constexpr int MAX_RADIUS = 4;

        void init(int w, int h, int size) {
            tile_size = size;
            tiles_x = (w + size - 1) / size;
            tiles_y = (h + size - 1) / size;
            tiles.assign(tiles_x * tiles_y, Tile{});
        }

        int id_from_pixel(int x, int y) const {
            return (y / tile_size) * tiles_x + (x / tile_size);
        }

        Tile& operator()(int tx, int ty) {
            return tiles[ty * tiles_x + tx];
        }

        const Tile& operator()(int tx, int ty) const {
            return tiles[ty * tiles_x + tx];
        }

        void reset() { for (auto& t : tiles) t.reset(); }
    };

struct Sample {
    Vec2f lens_u = Vec2f::Zero();
    Vec2f brdf_u = Vec2f::Zero();
    Vec2f light_u = Vec2f::Zero();
    float rr_u = 0.0f;
    bool valid = false;
};

} // namespace rpf
