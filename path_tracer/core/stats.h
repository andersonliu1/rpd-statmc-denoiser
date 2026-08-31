#pragma once
#include "common.h"
#include <array>
#include <cmath>

struct PixelStats {
    float mean = 0.0f, square_diff = 0.0f;
    int n = 0;

    Vec3f color_mean = Vec3f::Zero();
    Vec3f sqrt_color_mean = Vec3f::Zero();
    Vec3f sqrt_color_square_diff = Vec3f::Zero();

    float variance() const {
        return (n > 1) ? square_diff / float(n - 1) : 0.0f;
    }

    Vec3f sqrt_color_variance() const {
        if (n <= 1) return Vec3f::Zero();
        return sqrt_color_square_diff / float(n - 1);
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
    const Vec3f sqrt_color(
        std::sqrt(std::max(0.0f, luminance.x())),
        std::sqrt(std::max(0.0f, luminance.y())),
        std::sqrt(std::max(0.0f, luminance.z())));
    const Vec3f sqrt_delta = sqrt_color - s.sqrt_color_mean;
    s.sqrt_color_mean += sqrt_delta / float(s.n);
    s.sqrt_color_square_diff += sqrt_delta.cwiseProduct(sqrt_color - s.sqrt_color_mean);
}

namespace rpf {

    static constexpr float BASE_ALBEDO_THRESH = 0.12f;
    static constexpr int LIGHT_CATEGORY_BINS = 16;

    inline float relative_random_sensitivity(float random, float screen) {
        return random / std::max(EPS_SMALL, random + screen);
    }

    /// @brief Finds reliable dependency shared by the same random dimension.
    /// @param lhs Per-dimension reliability at the center pixel.
    /// @param rhs Per-dimension reliability at the neighbor pixel.
    /// @return Greatest pairwise minimum across dimensions.
    template <size_t DimensionCount>
    inline float shared_reliability(
        const std::array<float, DimensionCount>& lhs,
        const std::array<float, DimensionCount>& rhs) {
        float shared = 0.0f;
        for (size_t i = 0; i < lhs.size(); ++i) {
            shared = std::max(shared, std::min(lhs[i], rhs[i]));
        }
        return std::clamp(shared, 0.0f, 1.0f);
    }

    /// @brief Relaxes a StatMC compatibility score where a random driver is shared.
    /// @param score Original non-negative compatibility score.
    /// @param pair_reliability Reliable dependency shared by the pixel pair.
    /// @param scale Non-negative RPD strength; zero disables the adjustment.
    /// @return Relaxed compatibility score.
    inline double relax_compatibility(
        double score,
        float pair_reliability,
        float scale) {
        const float adjustment = std::clamp(scale * pair_reliability, 0.0f, 1.0f);
        return score / (1.0 + static_cast<double>(adjustment));
    }

    /// @brief Conservatively raises an underestimated variance toward its neighbors.
    /// @param value Current variance estimate.
    /// @param neighbor Local compatible-neighbor estimate.
    /// @param stabilizer Positive shrinkage stabilizer.
    /// @return Upward-only variance estimate.
    inline float conservative_variance_update(float value, float neighbor, float stabilizer) {
        const float blend = std::clamp(
            std::max(0.0f, neighbor - value) /
                std::max(EPS_SMALL, value + neighbor + stabilizer),
            0.0f, 1.0f);
        return lerp(value, neighbor, blend);
    }

    template <int BinCount>
    struct BinnedDependency {
        std::array<double, BinCount> weight{};
        std::array<double, BinCount> sum_y{};
        double w_sum = 0.0;
        double w2_sum = 0.0;
        double y_sum = 0.0;
        double y2_sum = 0.0;

        void add(int bin, float y, float w = 1.0f) {
            if (w <= EPS_SMALL) return;
            const int b = std::clamp(bin, 0, BinCount - 1);
            const double wd = static_cast<double>(w);
            const double yd = static_cast<double>(y);
            weight[b] += wd;
            sum_y[b] += wd * yd;
            w_sum += wd;
            w2_sum += wd * wd;
            y_sum += wd * yd;
            y2_sum += wd * yd * yd;
        }

        float compute_sensitivity() const {
            constexpr double kEps = 1e-18;
            const double effective_n = sample_mass();
            if (effective_n <= 2.0) return 0.0f;
            const double mean = y_sum / w_sum;
            const double total = y2_sum - w_sum * mean * mean;
            if (total <= kEps) return 0.0f;

            int active = 0;
            double between = 0.0;
            for (int bin = 0; bin < BinCount; ++bin) {
                if (weight[bin] <= kEps) continue;
                ++active;
                const double delta = sum_y[bin] / weight[bin] - mean;
                between += weight[bin] * delta * delta;
            }
            if (active < 2 || effective_n <= static_cast<double>(active)) return 0.0f;

            const double eta2 = std::clamp(between / total, 0.0, 1.0);
            const double adjusted = std::max(
                0.0, 1.0 - (1.0 - eta2) * (effective_n - 1.0) / (effective_n - active));
            return static_cast<float>(std::sqrt(adjusted));
        }

        double sample_mass() const {
            return w2_sum > 0.0 ? w_sum * w_sum / w2_sum : 0.0;
        }

        void reset() {
            weight.fill(0.0);
            sum_y.fill(0.0);
            w_sum = w2_sum = y_sum = y2_sum = 0.0;
        }

        void merge(const BinnedDependency& other) {
            for (int bin = 0; bin < BinCount; ++bin) {
                weight[bin] += other.weight[bin];
                sum_y[bin] += other.sum_y[bin];
            }
            w_sum += other.w_sum;
            w2_sum += other.w2_sum;
            y_sum += other.y_sum;
            y2_sum += other.y2_sum;
        }
    };

    template <int AxisBins = 4>
    struct Dependency2D {
        BinnedDependency<AxisBins * AxisBins> nonlinear;

        void add(float u, float v, float y, float w = 1.0f) {
            const int x = std::clamp(static_cast<int>(u * AxisBins), 0, AxisBins - 1);
            const int z = std::clamp(static_cast<int>(v * AxisBins), 0, AxisBins - 1);
            nonlinear.add(z * AxisBins + x, y, w);
        }

        float compute_sensitivity() const { return nonlinear.compute_sensitivity(); }

        double sample_mass() const { return nonlinear.sample_mass(); }

        void reset() {
            nonlinear.reset();
        }

        void merge(const Dependency2D& other) {
            nonlinear.merge(other.nonlinear);
        }
    };

    struct Tile {
        Dependency2D<> pixel;
        Dependency2D<> brdf;
        Dependency2D<> screen_brdf;
        Dependency2D<> lens;
        Dependency2D<> screen_lens;
        Dependency2D<> light_uv;
        Dependency2D<> screen_light_uv;
        Dependency2D<> environment;
        Dependency2D<> screen_environment;
        // ponytail: 16 light categories; use sparse categories for larger scenes.
        BinnedDependency<LIGHT_CATEGORY_BINS> light_select;
        Dependency2D<> screen_light_select;
        BinnedDependency<2> rr;
        Dependency2D<> screen_rr;

        void reset() {
            pixel.reset();
            brdf.reset();
            screen_brdf.reset();
            lens.reset();
            screen_lens.reset();
            light_uv.reset();
            screen_light_uv.reset();
            environment.reset();
            screen_environment.reset();
            light_select.reset();
            screen_light_select.reset();
            rr.reset();
            screen_rr.reset();
        }

        void merge(const Tile& other) {
            pixel.merge(other.pixel);
            brdf.merge(other.brdf);
            screen_brdf.merge(other.screen_brdf);
            lens.merge(other.lens);
            screen_lens.merge(other.screen_lens);
            light_uv.merge(other.light_uv);
            screen_light_uv.merge(other.screen_light_uv);
            environment.merge(other.environment);
            screen_environment.merge(other.screen_environment);
            light_select.merge(other.light_select);
            screen_light_select.merge(other.screen_light_select);
            rr.merge(other.rr);
            screen_rr.merge(other.screen_rr);
        }

        struct SplitSensitivity {
            float pixel = 0.0f;
            float brdf = 0.0f;
            float screen_brdf = 0.0f;
            float lens = 0.0f;
            float screen_lens = 0.0f;
            float light_uv = 0.0f;
            float screen_light_uv = 0.0f;
            float light_select = 0.0f;
            float screen_light_select = 0.0f;
            float environment = 0.0f;
            float screen_environment = 0.0f;
            float rr = 0.0f;
            float screen_rr = 0.0f;
        };

        struct SplitConfidence {
            float pixel = 0.0f;
            float brdf = 0.0f;
            float screen_brdf = 0.0f;
            float lens = 0.0f;
            float screen_lens = 0.0f;
            float light_uv = 0.0f;
            float screen_light_uv = 0.0f;
            float light_select = 0.0f;
            float screen_light_select = 0.0f;
            float environment = 0.0f;
            float screen_environment = 0.0f;
            float rr = 0.0f;
            float screen_rr = 0.0f;
        };

        SplitSensitivity computeSplitSensitivity() const {
            SplitSensitivity s;
            s.pixel = pixel.compute_sensitivity();
            s.brdf = brdf.compute_sensitivity();
            s.screen_brdf = screen_brdf.compute_sensitivity();
            s.lens = lens.compute_sensitivity();
            s.screen_lens = screen_lens.compute_sensitivity();
            s.light_uv = light_uv.compute_sensitivity();
            s.screen_light_uv = screen_light_uv.compute_sensitivity();
            s.light_select = light_select.compute_sensitivity();
            s.screen_light_select = screen_light_select.compute_sensitivity();
            s.environment = environment.compute_sensitivity();
            s.screen_environment = screen_environment.compute_sensitivity();
            s.rr = rr.compute_sensitivity();
            s.screen_rr = screen_rr.compute_sensitivity();
            return s;
        }

        SplitConfidence computeConfidence(float n0) const {
            SplitConfidence c;
            const double n0d = static_cast<double>(n0);
            const auto confidence = [n0d](double mass) {
                return static_cast<float>(mass / (mass + n0d));
            };
            c.pixel = confidence(pixel.sample_mass());
            c.brdf = confidence(brdf.sample_mass());
            c.screen_brdf = confidence(screen_brdf.sample_mass());
            c.lens = confidence(lens.sample_mass());
            c.screen_lens = confidence(screen_lens.sample_mass());
            c.light_uv = confidence(light_uv.sample_mass());
            c.screen_light_uv = confidence(screen_light_uv.sample_mass());
            c.light_select = confidence(light_select.sample_mass());
            c.screen_light_select = confidence(screen_light_select.sample_mass());
            c.environment = confidence(environment.sample_mass());
            c.screen_environment = confidence(screen_environment.sample_mass());
            c.rr = confidence(rr.sample_mass());
            c.screen_rr = confidence(screen_rr.sample_mass());
            return c;
        }
    };

    struct Grid {
        int support_size = 1;
        int step = 1;
        int nodes_x = 0;
        int nodes_y = 0;
        std::vector<Tile> nodes;

        void init(int w, int h, int size) {
            support_size = size;
            step = std::max(1, size / 2);
            nodes_x = std::max(2, (w + step - 1) / step + 1);
            nodes_y = std::max(2, (h + step - 1) / step + 1);
            nodes.assign(nodes_x * nodes_y, Tile{});
        }

        Tile& operator()(int tx, int ty) {
            return nodes[ty * nodes_x + tx];
        }

        const Tile& operator()(int tx, int ty) const {
            return nodes[ty * nodes_x + tx];
        }

        void reset() { for (auto& t : nodes) t.reset(); }
    };

struct Sample {
    Vec2f pixel_u = Vec2f::Zero();
    Vec2f lens_u = Vec2f::Zero();
    int light_index = -1;
    Vec2f brdf_u = Vec2f::Zero();
    Vec2f light_u = Vec2f::Zero();
    Vec2f environment_u = Vec2f::Zero();
    bool rr_survived = false;
    bool pixel_valid = false;
    bool lens_valid = false;
    bool light_select_valid = false;
    bool brdf_valid = false;
    bool light_valid = false;
    bool environment_valid = false;
    bool rr_valid = false;

    bool capture_pixel(const Vec2f& u) {
        if (pixel_valid) return false;
        pixel_u = u;
        pixel_valid = true;
        return true;
    }

    bool capture_lens(const Vec2f& u) {
        if (lens_valid) return false;
        lens_u = u;
        lens_valid = true;
        return true;
    }

    bool capture_light_select(int index, bool is_valid) {
        if (light_select_valid || !is_valid) return false;
        light_index = index;
        light_select_valid = true;
        return true;
    }

    bool capture_light_uv(const Vec2f& u, bool is_valid) {
        if (light_valid || !is_valid) return false;
        light_u = u;
        light_valid = true;
        return true;
    }

    bool capture_environment(const Vec2f& u) {
        if (environment_valid) return false;
        environment_u = u;
        environment_valid = true;
        return true;
    }

    bool capture_brdf(const Vec2f& u) {
        if (brdf_valid) return false;
        brdf_u = u;
        brdf_valid = true;
        return true;
    }

    bool capture_rr(bool survived) {
        if (rr_valid) return false;
        rr_survived = survived;
        rr_valid = true;
        return true;
    }

    bool has_any_shading_data() const {
        return light_select_valid || brdf_valid || light_valid || environment_valid || rr_valid;
    }
};

}
