#pragma once
#include "common.h"

struct PixelStats {
    float mean = 0.0f, square_diff = 0.0f;
    int n = 0;

    Vec3f color_mean = Vec3f::Zero();

    float variance() const {
        return (n > 1) ? square_diff / float(n - 1) : 0.0f;
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
