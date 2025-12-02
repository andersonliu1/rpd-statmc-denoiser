#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

#include "shared/image.h"

static Image::ToneMapping parse_preset(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "aces") return Image::ToneMapping::ACES;
    if (lower == "agx" || lower == "agx-default" || lower == "agx_default") return Image::ToneMapping::AGXDefault;
    if (lower == "agx-golden" || lower == "agx_golden") return Image::ToneMapping::AGXGolden;
    if (lower == "agx-punchy" || lower == "agx_punchy") return Image::ToneMapping::AGXPunchy;
    std::fprintf(stderr, "Unknown tonemap preset '%s', defaulting to ACES\n", name.c_str());
    return Image::ToneMapping::ACES;
}

int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) {
        std::fprintf(stderr, "Usage: %s <input.hdr> <output.png> [tonemap=aces|agx|agx-golden|agx-punchy]\n", argv[0]);
        return 1;
    }

    const char* input_path = argv[1];
    const char* output_path = argv[2];
    Image::ToneMapping preset = Image::ToneMapping::ACES;
    if (argc == 4) {
        preset = parse_preset(argv[3]);
    }

    int w = 0, h = 0, c = 0;
    float* data = stbi_loadf(input_path, &w, &h, &c, 3);
    if (!data || w <= 0 || h <= 0) {
        std::fprintf(stderr, "Failed to load HDR image '%s'\n", input_path);
        if (data) stbi_image_free(data);
        return 1;
    }

    Image img(w, h);
    const int pixels = w * h;
    for (int i = 0; i < pixels; ++i) {
        img[i] = Vec3f(data[3 * i + 0], data[3 * i + 1], data[3 * i + 2]);
    }
    stbi_image_free(data);

    if (!img.save_with_tonemapping(output_path, preset)) {
        std::fprintf(stderr, "Failed to write tonemapped PNG to '%s'\n", output_path);
        return 1;
    }

    std::printf("Wrote tonemapped PNG to %s using preset %d\n", output_path, static_cast<int>(preset));
    return 0;
}
