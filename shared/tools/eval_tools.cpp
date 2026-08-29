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
            peak = std::max(peak, std::abs(a));
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
    std::printf("  RMSE: %.6g  PSNR (reference peak=%.6g): %.2f dB\n", rmse, peak, psnr);
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

struct ExtendedMetrics {
    double nrmse = 0.0;
    double log_ssim11 = 1.0;
    double log_gradient_nrmse = 0.0;
};

static double box_sum(const std::vector<double>& integral, int stride,
                      int x0, int y0, int x1, int y1) {
    return integral[y1 * stride + x1] - integral[y0 * stride + x1] -
           integral[y1 * stride + x0] + integral[y0 * stride + x0];
}

static ExtendedMetrics compute_extended_metrics(const float* ref, const float* tst, int w, int h) {
    const int pixels = w * h;
    std::vector<double> ref_log_lum(pixels);
    std::vector<double> tst_log_lum(pixels);
    double ref_energy = 0.0;
    double error_energy = 0.0;
    double log_peak = 0.0;
    for (int i = 0; i < pixels; ++i) {
        double ref_lum = 0.0;
        double tst_lum = 0.0;
        for (int c = 0; c < 3; ++c) {
            const double a = ref[3 * i + c];
            const double b = tst[3 * i + c];
            ref_energy += a * a;
            error_energy += (a - b) * (a - b);
            constexpr double weights[3] = {0.2126, 0.7152, 0.0722};
            ref_lum += weights[c] * a;
            tst_lum += weights[c] * b;
        }
        ref_log_lum[i] = std::log1p(std::max(0.0, ref_lum));
        tst_log_lum[i] = std::log1p(std::max(0.0, tst_lum));
        log_peak = std::max(log_peak, ref_log_lum[i]);
    }

    ExtendedMetrics metrics;
    metrics.nrmse = std::sqrt(error_energy / std::max(1e-18, ref_energy));

    const int stride = w + 1;
    const int integral_size = stride * (h + 1);
    std::vector<double> sum_ref(integral_size, 0.0);
    std::vector<double> sum_tst(integral_size, 0.0);
    std::vector<double> sum_ref2(integral_size, 0.0);
    std::vector<double> sum_tst2(integral_size, 0.0);
    std::vector<double> sum_cross(integral_size, 0.0);
    for (int y = 0; y < h; ++y) {
        double row_ref = 0.0, row_tst = 0.0, row_ref2 = 0.0, row_tst2 = 0.0, row_cross = 0.0;
        for (int x = 0; x < w; ++x) {
            const double a = ref_log_lum[y * w + x];
            const double b = tst_log_lum[y * w + x];
            row_ref += a;
            row_tst += b;
            row_ref2 += a * a;
            row_tst2 += b * b;
            row_cross += a * b;
            const int out = (y + 1) * stride + x + 1;
            sum_ref[out] = sum_ref[out - stride] + row_ref;
            sum_tst[out] = sum_tst[out - stride] + row_tst;
            sum_ref2[out] = sum_ref2[out - stride] + row_ref2;
            sum_tst2[out] = sum_tst2[out - stride] + row_tst2;
            sum_cross[out] = sum_cross[out - stride] + row_cross;
        }
    }

    log_peak = std::max(1e-6, log_peak);
    const double c1 = std::pow(0.01 * log_peak, 2.0);
    const double c2 = std::pow(0.03 * log_peak, 2.0);
    double ssim_sum = 0.0;
    for (int y = 0; y < h; ++y) {
        const int y0 = std::max(0, y - 5);
        const int y1 = std::min(h, y + 6);
        for (int x = 0; x < w; ++x) {
            const int x0 = std::max(0, x - 5);
            const int x1 = std::min(w, x + 6);
            const double n = double((x1 - x0) * (y1 - y0));
            const double mean_ref = box_sum(sum_ref, stride, x0, y0, x1, y1) / n;
            const double mean_tst = box_sum(sum_tst, stride, x0, y0, x1, y1) / n;
            const double var_ref = std::max(0.0, box_sum(sum_ref2, stride, x0, y0, x1, y1) / n - mean_ref * mean_ref);
            const double var_tst = std::max(0.0, box_sum(sum_tst2, stride, x0, y0, x1, y1) / n - mean_tst * mean_tst);
            const double covariance = box_sum(sum_cross, stride, x0, y0, x1, y1) / n - mean_ref * mean_tst;
            const double numerator = (2.0 * mean_ref * mean_tst + c1) * (2.0 * covariance + c2);
            const double denominator = (mean_ref * mean_ref + mean_tst * mean_tst + c1) * (var_ref + var_tst + c2);
            ssim_sum += (denominator > 0.0) ? numerator / denominator : 1.0;
        }
    }
    metrics.log_ssim11 = ssim_sum / double(pixels);

    double gradient_error = 0.0;
    double gradient_reference = 0.0;
    for (int y = 0; y < h - 1; ++y) {
        for (int x = 0; x < w - 1; ++x) {
            const int i = y * w + x;
            const double ref_dx = ref_log_lum[i + 1] - ref_log_lum[i];
            const double ref_dy = ref_log_lum[i + w] - ref_log_lum[i];
            const double tst_dx = tst_log_lum[i + 1] - tst_log_lum[i];
            const double tst_dy = tst_log_lum[i + w] - tst_log_lum[i];
            gradient_error += (ref_dx - tst_dx) * (ref_dx - tst_dx) +
                              (ref_dy - tst_dy) * (ref_dy - tst_dy);
            gradient_reference += ref_dx * ref_dx + ref_dy * ref_dy;
        }
    }
    metrics.log_gradient_nrmse = std::sqrt(gradient_error / std::max(1e-18, gradient_reference));
    return metrics;
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
    double sum_abs = 0.0;
    double peak = 0.0;
    double mean_ref[3] = {0.0, 0.0, 0.0};
    double mean_tst[3] = {0.0, 0.0, 0.0};

    for (int i = 0; i < pixels; ++i) {
        for (int c = 0; c < 3; ++c) {
            double a = ref[3 * i + c];
            double b = tst[3 * i + c];
            sum_sq += (a - b) * (a - b);
            sum_abs += std::abs(a - b);
            peak = std::max(peak, std::abs(a));
            mean_ref[c] += a;
            mean_tst[c] += b;
        }
    }
    for (int c = 0; c < 3; ++c) {
        mean_ref[c] /= pixels;
        mean_tst[c] /= pixels;
    }

    double ssim[3] = {0.0, 0.0, 0.0};
    if (peak <= 0.0) peak = 1.0;
    const double C1 = (0.01 * peak) * (0.01 * peak);
    const double C2 = (0.03 * peak) * (0.03 * peak);
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
    double rmse = std::sqrt(mse);
    double psnr = (rmse > 0.0) ? 20.0 * std::log10(peak / rmse) : INFINITY;
    const ExtendedMetrics extended = compute_extended_metrics(ref, tst, wr, hr);

    std::printf("HDR metrics:\n");
    std::printf("  MAE: %.6g  RMSE: %.6g  PSNR (reference peak=%.6g): %.2f dB\n",
                sum_abs / (pixels * 3), rmse, peak, psnr);
    std::printf("  SSIM (global diagnostic, per channel): R=%.4f G=%.4f B=%.4f  mean=%.4f\n", ssim[0], ssim[1], ssim[2], (ssim[0] + ssim[1] + ssim[2]) / 3.0);
    std::printf("  Extended: NRMSE: %.6g  LogSSIM11: %.6g  LogGradNRMSE: %.6g\n",
                extended.nrmse, extended.log_ssim11, extended.log_gradient_nrmse);

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
        "  %s compare-hdr <reference.hdr> <test.hdr>\n"
        "  %s compare-png <a.png> <b.png>\n"
        "  %s hdr-metrics <ref.hdr> <test.hdr> [residual.hdr]\n"
        "  %s self-test\n",
        prog, prog, prog, prog);
}

static int self_test() {
    constexpr int size = 16;
    std::vector<float> reference(size * size * 3);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float value = float(x + y + 1) / float(2 * size);
            for (int c = 0; c < 3; ++c) reference[3 * (y * size + x) + c] = value;
        }
    }
    std::vector<float> changed = reference;
    changed[3 * (8 * size + 8)] += 0.25f;
    const ExtendedMetrics identical = compute_extended_metrics(reference.data(), reference.data(), size, size);
    const ExtendedMetrics different = compute_extended_metrics(reference.data(), changed.data(), size, size);
    if (identical.nrmse > 1e-12 || std::abs(identical.log_ssim11 - 1.0) > 1e-12 ||
        identical.log_gradient_nrmse > 1e-12 || different.nrmse <= 0.0 ||
        different.log_ssim11 >= 1.0 || different.log_gradient_nrmse <= 0.0) {
        std::fprintf(stderr, "eval_tools self-test failed\n");
        return 1;
    }
    std::printf("eval_tools self-test passed\n");
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }
    const std::string cmd = argv[1];
    if (cmd == "self-test" && argc == 2) {
        return self_test();
    }
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
