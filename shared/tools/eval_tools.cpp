#include <cstdio>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "shared/image.h"

static int compare_hdr(const char* a_path, const char* b_path) {
    int w1, h1, c1, w2, h2, c2;
    float* d1 = stbi_loadf(a_path, &w1, &h1, &c1, 3);
    float* d2 = stbi_loadf(b_path, &w2, &h2, &c2, 3);
    if (!d1 || !d2) {
        std::fprintf(stderr, "Failed to load images\n");
        if (d1) stbi_image_free(d1);
        if (d2) stbi_image_free(d2);
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
    double peak = 0.0;
    const int pixels = w1 * h1;
    for (int i = 0; i < pixels; ++i) {
        for (int c = 0; c < 3; ++c) {
            double a = d1[3 * i + c];
            double b = d2[3 * i + c];
            double diff = std::abs(a - b);
            sum_abs[c] += diff;
            if (diff > max_abs[c]) max_abs[c] = diff;
            sum_sq += diff * diff;
            peak = std::max(peak, std::max(a, b));
        }
    }
    stbi_image_free(d1);
    stbi_image_free(d2);

    double mean_abs[3];
    for (int c = 0; c < 3; ++c) mean_abs[c] = sum_abs[c] / pixels;
    double rmse = std::sqrt(sum_sq / (pixels * 3));
    if (peak <= 0.0) peak = 1.0;
    double psnr = (rmse > 0) ? 20 * std::log10(peak / rmse) : INFINITY;

    std::printf("HDR compare:\n");
    std::printf("  Mean abs diff per channel: R=%.6g G=%.6g B=%.6g\n", mean_abs[0], mean_abs[1], mean_abs[2]);
    std::printf("  Max  abs diff per channel: R=%.6g G=%.6g B=%.6g\n", max_abs[0], max_abs[1], max_abs[2]);
    std::printf("  RMSE: %.6g  PSNR (peak=%.6g): %.2f dB\n", rmse, peak, psnr);
    return 0;
}

static int compare_png(const char* a_path, const char* b_path) {
    int w1, h1, c1, w2, h2, c2;
    unsigned char* d1 = stbi_load(a_path, &w1, &h1, &c1, 3);
    unsigned char* d2 = stbi_load(b_path, &w2, &h2, &c2, 3);
    if (!d1 || !d2) {
        std::fprintf(stderr, "Failed to load images\n");
        if (d1) stbi_image_free(d1);
        if (d2) stbi_image_free(d2);
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

    std::printf("PNG compare:\n");
    std::printf("  Mean abs diff per channel: R=%.6f G=%.6f B=%.6f\n", mean_abs[0], mean_abs[1], mean_abs[2]);
    std::printf("  Max  abs diff per channel: R=%.6f G=%.6f B=%.6f\n", max_abs[0], max_abs[1], max_abs[2]);
    std::printf("  RMSE: %.6f  PSNR: %.2f dB\n", rmse, psnr);
    return 0;
}

static double compute_psnr(double mse) {
    if (mse <= 0.0) return INFINITY;
    return 20.0 * std::log10(1.0 / std::sqrt(mse));
}

static int hdr_metrics(const char* ref_path, const char* test_path, const char* residual_out) {
    int wr, hr, cr, wt, ht, ct;
    float* ref = stbi_loadf(ref_path, &wr, &hr, &cr, 3);
    float* tst = stbi_loadf(test_path, &wt, &ht, &ct, 3);
    if (!ref || !tst) {
        std::fprintf(stderr, "Failed to load input images\n");
        if (ref) stbi_image_free(ref);
        if (tst) stbi_image_free(tst);
        return 1;
    }
    if (wr != wt || hr != ht) {
        std::fprintf(stderr, "Dimension mismatch %dx%d vs %dx%d\n", wr, hr, wt, ht);
        stbi_image_free(ref);
        stbi_image_free(tst);
        return 1;
    }

    const int pixels = wr * hr;
    double sum_sq = 0.0;
    double mean_ref[3] = {0.0, 0.0, 0.0};
    double mean_tst[3] = {0.0, 0.0, 0.0};

    for (int i = 0; i < pixels; ++i) {
        for (int c = 0; c < 3; ++c) {
            double a = ref[3 * i + c];
            double b = tst[3 * i + c];
            sum_sq += (a - b) * (a - b);
            mean_ref[c] += a;
            mean_tst[c] += b;
        }
    }
    for (int c = 0; c < 3; ++c) {
        mean_ref[c] /= pixels;
        mean_tst[c] /= pixels;
    }

    double ssim[3] = {0.0, 0.0, 0.0};
    const double C1 = 0.01 * 0.01;
    const double C2 = 0.03 * 0.03;
    for (int c = 0; c < 3; ++c) {
        double var_ref = 0.0, var_tst = 0.0, cov = 0.0;
        for (int i = 0; i < pixels; ++i) {
            double a = ref[3 * i + c];
            double b = tst[3 * i + c];
            var_ref += (a - mean_ref[c]) * (a - mean_ref[c]);
            var_tst += (b - mean_tst[c]) * (b - mean_tst[c]);
            cov += (a - mean_ref[c]) * (b - mean_tst[c]);
        }
        var_ref /= pixels;
        var_tst /= pixels;
        cov /= pixels;
        double num = (2.0 * mean_ref[c] * mean_tst[c] + C1) * (2.0 * cov + C2);
        double den = (mean_ref[c] * mean_ref[c] + mean_tst[c] * mean_tst[c] + C1) * (var_ref + var_tst + C2);
        ssim[c] = (den > 0.0) ? num / den : 0.0;
    }

    double mse = sum_sq / (pixels * 3);
    double psnr = compute_psnr(mse);

    std::printf("HDR metrics:\n");
    std::printf("  PSNR: %.2f dB (vs max=1.0)\n", psnr);
    std::printf("  SSIM (global, per channel): R=%.4f G=%.4f B=%.4f  mean=%.4f\n", ssim[0], ssim[1], ssim[2], (ssim[0] + ssim[1] + ssim[2]) / 3.0);

    if (residual_out) {
        std::vector<float> residual(3 * pixels);
        for (int i = 0; i < 3 * pixels; ++i) {
            residual[i] = std::abs(tst[i] - ref[i]);
        }
        if (!stbi_write_hdr(residual_out, wr, hr, 3, residual.data())) {
            std::fprintf(stderr, "Failed to write residual to %s\n", residual_out);
            stbi_image_free(ref);
            stbi_image_free(tst);
            return 1;
        }
        std::printf("  Wrote residual heatmap to %s\n", residual_out);
    }

    stbi_image_free(ref);
    stbi_image_free(tst);
    return 0;
}

static void usage(const char* prog) {
    std::fprintf(stderr,
        "Usage:\n"
        "  %s compare-hdr <a.hdr> <b.hdr>\n"
        "  %s compare-png <a.png> <b.png>\n"
        "  %s hdr-metrics <ref.hdr> <test.hdr> [residual.hdr]\n",
        prog, prog, prog);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }
    const std::string cmd = argv[1];
    if (cmd == "compare-hdr" && argc == 4) {
        return compare_hdr(argv[2], argv[3]);
    }
    if (cmd == "compare-png" && argc == 4) {
        return compare_png(argv[2], argv[3]);
    }
    if (cmd == "hdr-metrics" && (argc == 4 || argc == 5)) {
        const char* residual = (argc == 5) ? argv[4] : nullptr;
        return hdr_metrics(argv[2], argv[3], residual);
    }

    usage(argv[0]);
    return 1;
}
