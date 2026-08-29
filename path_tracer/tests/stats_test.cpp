#include <cmath>
#include <stdexcept>

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
    for (int i = 0; i < 8; ++i) pixels.accumulate_light_visibility(true);
    for (int i = 0; i < 2; ++i) pixels.accumulate_light_visibility(false);
    require(std::abs(pixels.light_visibility_mean() - 0.75f) < 1e-5f);
    require(pixels.light_visibility_variance() > 0.0f);
    require(std::abs(rpf::relative_random_sensitivity(0.5f, 0.5f) - 0.5f) < 1e-5f);
    require(rpf::relative_random_sensitivity(0.0f, 0.5f) == 0.0f);

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

    rpf::BinaryMutualInformation<4> dependent_visibility;
    rpf::BinaryMutualInformation<4> independent_visibility;
    for (int bin = 0; bin < 4; ++bin) {
        for (int i = 0; i < 100; ++i) {
            dependent_visibility.add(bin, bin >= 2);
            independent_visibility.add(bin, i % 2 == 0);
        }
    }
    require(dependent_visibility.compute_sensitivity() > 0.95f);
    require(independent_visibility.compute_sensitivity() < 0.05f);
}
