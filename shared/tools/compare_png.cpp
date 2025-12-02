#include <cstdio>
#include <cmath>
#include "shared/image.h"

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "Usage: %s a.png b.png\n", argv[0]);
        return 1;
    }
    int w1, h1, c1;
    unsigned char* d1 = stbi_load(argv[1], &w1, &h1, &c1, 3);
    int w2, h2, c2;
    unsigned char* d2 = stbi_load(argv[2], &w2, &h2, &c2, 3);
    if (!d1 || !d2) {
        std::fprintf(stderr, "Failed to load images\n");
        return 1;
    }
    if (w1 != w2 || h1 != h2) {
        std::fprintf(stderr, "Dimension mismatch %dx%d vs %dx%d\n", w1, h1, w2, h2);
        stbi_image_free(d1);
        stbi_image_free(d2);
        return 1;
    }

    double sum_abs[3] = {0, 0, 0};
    double max_abs[3] = {0, 0, 0};
    double sum_sq = 0;
    const int pixels = w1 * h1;
    for (int i = 0; i < pixels; ++i) {
        for (int c = 0; c < 3; ++c) {
            double a = d1[3 * i + c] / 255.0;
            double b = d2[3 * i + c] / 255.0;
            double diff = std::abs(a - b);
            sum_abs[c] += diff;
            if (diff > max_abs[c]) max_abs[c] = diff;
            sum_sq += diff * diff;
        }
    }
    stbi_image_free(d1);
    stbi_image_free(d2);

    double mean_abs[3];
    for (int c = 0; c < 3; ++c) mean_abs[c] = sum_abs[c] / pixels;
    double rmse = std::sqrt(sum_sq / (pixels * 3));
    double psnr = (rmse > 0) ? 20 * std::log10(1.0 / rmse) : INFINITY;

    std::printf("Mean abs diff per channel: R=%.6f G=%.6f B=%.6f\n", mean_abs[0], mean_abs[1], mean_abs[2]);
    std::printf("Max abs diff per channel:  R=%.6f G=%.6f B=%.6f\n", max_abs[0], max_abs[1], max_abs[2]);
    std::printf("RMSE: %.6f  PSNR: %.2f dB\n", rmse, psnr);
    return 0;
}
