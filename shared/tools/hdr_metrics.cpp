#include <cstdio>
#include <cmath>
#include <vector>
#include <string>

#include "shared/image.h"

struct ImageData {
    int w = 0, h = 0, c = 0;
    std::vector<float> pixels;
    bool load(const char* path) {
        float* data = stbi_loadf(path, &w, &h, &c, 3);
        if (!data) return false;
        c = 3;
        pixels.assign(data, data + w * h * 3);
        stbi_image_free(data);
        return true;
    }
};

static double compute_psnr(double mse) {
    if (mse <= 0.0) return INFINITY;
    return 20.0 * std::log10(1.0 / std::sqrt(mse));
}

int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) {
        std::fprintf(stderr, "Usage: %s reference.hdr test.hdr [residual_out.hdr]\n", argv[0]);
        return 1;
    }

    ImageData ref, test;
    if (!ref.load(argv[1]) || !test.load(argv[2])) {
        std::fprintf(stderr, "Failed to load input images\n");
        return 1;
    }
    if (ref.w != test.w || ref.h != test.h) {
        std::fprintf(stderr, "Dimension mismatch %dx%d vs %dx%d\n", ref.w, ref.h, test.w, test.h);
        return 1;
    }

    const int pixels = ref.w * ref.h;
    double sum_sq = 0.0;
    double sum_abs = 0.0;
    double peak = 0.0;
    double mean_ref[3] = {0.0, 0.0, 0.0};
    double mean_test[3] = {0.0, 0.0, 0.0};

    for (int i = 0; i < pixels; ++i) {
        for (int c = 0; c < 3; ++c) {
            double a = ref.pixels[3 * i + c];
            double b = test.pixels[3 * i + c];
            sum_sq += (a - b) * (a - b);
            sum_abs += std::abs(a - b);
            peak = std::max(peak, std::abs(a));
            mean_ref[c] += a;
            mean_test[c] += b;
        }
    }
    for (int c = 0; c < 3; ++c) {
        mean_ref[c] /= pixels;
        mean_test[c] /= pixels;
    }

    // Global SSIM per channel
    double ssim[3] = {0.0, 0.0, 0.0};
    if (peak <= 0.0) peak = 1.0;
    const double C1 = (0.01 * peak) * (0.01 * peak);
    const double C2 = (0.03 * peak) * (0.03 * peak);
    for (int c = 0; c < 3; ++c) {
        double var_ref = 0.0, var_test = 0.0, cov = 0.0;
        for (int i = 0; i < pixels; ++i) {
            double a = ref.pixels[3 * i + c];
            double b = test.pixels[3 * i + c];
            var_ref += (a - mean_ref[c]) * (a - mean_ref[c]);
            var_test += (b - mean_test[c]) * (b - mean_test[c]);
            cov += (a - mean_ref[c]) * (b - mean_test[c]);
        }
        var_ref /= pixels;
        var_test /= pixels;
        cov /= pixels;
        double num = (2.0 * mean_ref[c] * mean_test[c] + C1) * (2.0 * cov + C2);
        double den = (mean_ref[c] * mean_ref[c] + mean_test[c] * mean_test[c] + C1) * (var_ref + var_test + C2);
        ssim[c] = (den > 0.0) ? num / den : 0.0;
    }

    double mse = sum_sq / (pixels * 3);
    double rmse = std::sqrt(mse);
    double psnr = (rmse > 0.0) ? 20.0 * std::log10(peak / rmse) : INFINITY;

    std::printf("MAE: %.6g  RMSE: %.6g  PSNR (reference peak=%.6g): %.2f dB\n",
                sum_abs / (pixels * 3), rmse, peak, psnr);
    std::printf("SSIM (global diagnostic, per channel): R=%.4f G=%.4f B=%.4f  mean=%.4f\n", ssim[0], ssim[1], ssim[2], (ssim[0] + ssim[1] + ssim[2]) / 3.0);

    if (argc == 4) {
        std::vector<float> residual(3 * pixels);
        for (int i = 0; i < 3 * pixels; ++i) {
            residual[i] = std::abs(test.pixels[i] - ref.pixels[i]);
        }
        if (!stbi_write_hdr(argv[3], ref.w, ref.h, 3, residual.data())) {
            std::fprintf(stderr, "Failed to write residual to %s\n", argv[3]);
            return 1;
        }
        std::printf("Wrote residual heatmap to %s\n", argv[3]);
    }

    return 0;
}
