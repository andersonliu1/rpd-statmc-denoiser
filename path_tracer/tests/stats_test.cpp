#include <cmath>
#include <stdexcept>

#include "core/material.h"
#include "core/stats.h"

static void require(bool condition) {
    if (!condition) throw std::runtime_error("stats test failed");
}

int main() {
    PixelStats pixels;
    accumulate_sample(pixels, Vec3f(1.0f, 4.0f, 9.0f));
    accumulate_sample(pixels, Vec3f(3.0f, 16.0f, 25.0f));
    require(pixels.n == 2);
    require((pixels.color_mean - Vec3f(2.0f, 10.0f, 17.0f)).norm() < 1e-5f);
    require(pixels.sqrt_color_variance().minCoeff() > 0.0f);
    require(std::abs(rpf::relative_random_sensitivity(0.5f, 0.5f) - 0.5f) < 1e-5f);
    require(rpf::relative_random_sensitivity(0.0f, 0.5f) == 0.0f);
    const std::array<float, 6> lhs_rpd{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    const std::array<float, 6> shared_rpd{0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    const std::array<float, 6> disjoint_rpd{0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    require(rpf::shared_reliability(lhs_rpd, shared_rpd) == 0.5f);
    require(rpf::shared_reliability(lhs_rpd, disjoint_rpd) == 0.0f);
    require(rpf::relax_compatibility(4.0, 1.0f, 0.0f) == 4.0);
    require(rpf::relax_compatibility(4.0, 1.0f, 1.0f) == 2.0);
    require(rpf::relax_compatibility(4.0, 0.0f, 1.0f) == 4.0);
    require(rpf::conservative_variance_update(1.0f, 2.0f, 1e-3f) > 1.0f);
    require(rpf::conservative_variance_update(2.0f, 1.0f, 1e-3f) == 2.0f);

    const Vec3f mirror_albedo(0.8f, 0.7f, 0.6f);
    const Material mirror = Mirror{mirror_albedo};
    const Vec3f normal(0.0f, 0.0f, 1.0f);
    const Vec3f wo(0.6f, 0.0f, 0.8f);
    const auto [wi, mirror_pdf] = brdf_sample(mirror, wo, normal, Vec2f::Zero());
    require(brdf_is_delta(mirror));
    require((brdf_sample_weight(mirror, wo, wi, normal, mirror_pdf) - mirror_albedo).norm() < 1e-5f);

    rpf::Dependency2D<> nonlinear;
    rpf::Dependency2D<> independent;
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 40; ++x) {
            const float u = (float(x) + 0.5f) / 40.0f;
            const float v = (float(y) + 0.5f) / 40.0f;
            nonlinear.add(u, v, ((x / 10) + (y / 10)) % 2 ? 1.0f : 0.0f);
            independent.add(u, v, (x + 3 * y) % 2 ? 1.0f : 0.0f);
        }
    }
    require(nonlinear.compute_sensitivity() > 0.95f);
    require(independent.compute_sensitivity() < 0.05f);

    rpf::Tile light_tile;
    for (int i = 0; i < 10; ++i) {
        const float u = i < 5 ? 0.1f : 0.9f;
        light_tile.light_uv.add(u, u, i < 5 ? 0.0f : 1.0f);
        light_tile.screen_light_uv.add(u, u, i < 5 ? 0.0f : 1.0f);
    }
    for (int i = 0; i < 100; ++i) light_tile.light_select.add(0, float(i % 2));
    require(light_tile.computeSplitSensitivity().light_uv > 0.95f);
    require(light_tile.computeSplitSensitivity().screen_light_uv > 0.95f);
    require(light_tile.computeSplitSensitivity().light_select == 0.0f);
    require(std::abs(light_tile.computeConfidence(10.0f).light_uv - 0.5f) < 1e-5f);
    require(light_tile.computeConfidence(10.0f).light_select > 0.9f);

    rpf::BinnedDependency<2> dependent_rr;
    rpf::BinnedDependency<2> independent_rr;
    for (int bin = 0; bin < 2; ++bin) {
        for (int i = 0; i < 100; ++i) {
            dependent_rr.add(bin, static_cast<float>(bin));
            independent_rr.add(bin, static_cast<float>(i % 2));
        }
    }
    require(dependent_rr.compute_sensitivity() > 0.95f);
    require(independent_rr.compute_sensitivity() < 0.05f);

}
