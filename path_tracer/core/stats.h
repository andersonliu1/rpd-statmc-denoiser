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
    float light_visibility_sum = 0.0f;
    int light_visibility_n = 0;

    float variance() const {
        return (n > 1) ? square_diff / float(n - 1) : 0.0f;
    }

    Vec3f sqrt_color_variance() const {
        if (n <= 1) return Vec3f::Zero();
        return sqrt_color_square_diff / float(n - 1);
    }

    void accumulate_light_visibility(bool visible) {
        light_visibility_sum += visible ? 1.0f : 0.0f;
        ++light_visibility_n;
    }

    float light_visibility_mean() const {
        return (light_visibility_sum + 1.0f) / float(light_visibility_n + 2);
    }

    float light_visibility_variance() const {
        const float p = light_visibility_mean();
        return p * (1.0f - p) / float(light_visibility_n + 3);
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

    template <int BinCount>
    struct BinaryMutualInformation {
        std::array<double, BinCount * 2> joint{};
        double w_sum = 0.0;
        double w2_sum = 0.0;

        void add(int bin, bool value, float w = 1.0f) {
            if (w <= EPS_SMALL) return;
            const int clamped_bin = std::clamp(bin, 0, BinCount - 1);
            joint[2 * clamped_bin + (value ? 1 : 0)] += static_cast<double>(w);
            w_sum += static_cast<double>(w);
            w2_sum += static_cast<double>(w) * static_cast<double>(w);
        }

        float compute_sensitivity() const {
            constexpr double kEps = 1e-18;
            const double effective_n = w_sum * w_sum / std::max(kEps, w2_sum);
            if (effective_n <= 1.0) return 0.0f;

            std::array<double, BinCount> x_mass{};
            double y_mass[2] = {0.0, 0.0};
            int active_x = 0;
            for (int bin = 0; bin < BinCount; ++bin) {
                x_mass[bin] = joint[2 * bin] + joint[2 * bin + 1];
                if (x_mass[bin] > kEps) ++active_x;
                y_mass[0] += joint[2 * bin];
                y_mass[1] += joint[2 * bin + 1];
            }
            if (active_x < 2 || y_mass[0] <= kEps || y_mass[1] <= kEps) return 0.0f;

            double mutual_information = 0.0;
            for (int bin = 0; bin < BinCount; ++bin) {
                for (int value = 0; value < 2; ++value) {
                    const double mass = joint[2 * bin + value];
                    if (mass <= kEps) continue;
                    mutual_information += (mass / w_sum) *
                        std::log((mass * w_sum) / (x_mass[bin] * y_mass[value]));
                }
            }

            const double bias = double(active_x - 1) / (2.0 * effective_n);
            const double corrected_mi = std::max(0.0, mutual_information - bias);
            const double p0 = y_mass[0] / w_sum;
            const double p1 = y_mass[1] / w_sum;
            const double entropy_y = -p0 * std::log(p0) - p1 * std::log(p1);
            return static_cast<float>(std::sqrt(std::clamp(corrected_mi / std::max(kEps, entropy_y), 0.0, 1.0)));
        }

        void reset() {
            joint.fill(0.0);
            w_sum = w2_sum = 0.0;
        }

        void merge(const BinaryMutualInformation& other) {
            for (int i = 0; i < BinCount * 2; ++i) joint[i] += other.joint[i];
            w_sum += other.w_sum;
            w2_sum += other.w2_sum;
        }
    };

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
        Dependency2D<> lens;
        Dependency2D<> light_uv;
        Dependency2D<> environment;
        // ponytail: 16 light categories; use sparse categories for larger scenes.
        BinnedDependency<LIGHT_CATEGORY_BINS> light_select;
        BinaryMutualInformation<16> light_uv_visibility;
        BinaryMutualInformation<LIGHT_CATEGORY_BINS> light_select_visibility;
        BinaryMutualInformation<16> environment_visibility;
        BinnedDependency<2> rr;

        void reset() {
            pixel.reset();
            brdf.reset();
            lens.reset();
            light_uv.reset();
            environment.reset();
            light_select.reset();
            light_uv_visibility.reset();
            light_select_visibility.reset();
            environment_visibility.reset();
            rr.reset();
        }

        void merge(const Tile& other) {
            pixel.merge(other.pixel);
            brdf.merge(other.brdf);
            lens.merge(other.lens);
            light_uv.merge(other.light_uv);
            environment.merge(other.environment);
            light_select.merge(other.light_select);
            light_uv_visibility.merge(other.light_uv_visibility);
            light_select_visibility.merge(other.light_select_visibility);
            environment_visibility.merge(other.environment_visibility);
            rr.merge(other.rr);
        }

        struct SplitSensitivity {
            float pixel = 0.0f;
            float brdf = 0.0f;
            float lens = 0.0f;
            float light = 0.0f;
            float environment = 0.0f;
            float rr = 0.0f;
        };

        struct SplitConfidence {
            float pixel = 0.0f;
            float brdf = 0.0f;
            float lens = 0.0f;
            float light = 0.0f;
            float environment = 0.0f;
            float rr = 0.0f;
        };

        SplitSensitivity computeSplitSensitivity() const {
            SplitSensitivity s;
            s.pixel = pixel.compute_sensitivity();
            s.brdf = brdf.compute_sensitivity();
            s.lens = lens.compute_sensitivity();
            const float light_uv_s = light_uv.compute_sensitivity();
            const float light_select_s = light_select.compute_sensitivity();
            const float visibility_s = std::max(
                light_uv_visibility.compute_sensitivity(),
                light_select_visibility.compute_sensitivity());
            s.light = std::max({light_uv_s, light_select_s, visibility_s});
            s.environment = std::max(
                environment.compute_sensitivity(),
                environment_visibility.compute_sensitivity());
            s.rr = rr.compute_sensitivity();
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
            c.lens = confidence(lens.sample_mass());
            const float light_uv_c = confidence(light_uv.sample_mass());
            const float light_select_c = confidence(light_select.sample_mass());
            c.light = std::max(light_uv_c, light_select_c);
            c.environment = confidence(environment.sample_mass());
            c.rr = confidence(rr.sample_mass());
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
    bool light_select_visible = false;
    bool light_uv_visible = false;
    bool environment_visible = false;
    bool pixel_valid = false;
    bool lens_valid = false;
    bool light_select_valid = false;
    bool brdf_valid = false;
    bool light_valid = false;
    bool light_select_visibility_valid = false;
    bool light_uv_visibility_valid = false;
    bool environment_valid = false;
    bool environment_visibility_valid = false;
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

    bool capture_light_select_visibility(bool visible) {
        if (light_select_visibility_valid) return false;
        light_select_visible = visible;
        light_select_visibility_valid = true;
        return true;
    }

    bool capture_light_uv_visibility(bool visible) {
        if (light_uv_visibility_valid) return false;
        light_uv_visible = visible;
        light_uv_visibility_valid = true;
        return true;
    }

    bool capture_environment(const Vec2f& u) {
        if (environment_valid) return false;
        environment_u = u;
        environment_valid = true;
        return true;
    }

    bool capture_environment_visibility(bool visible) {
        if (environment_visibility_valid) return false;
        environment_visible = visible;
        environment_visibility_valid = true;
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
