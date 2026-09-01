#include <cstdio>
#include <cctype>
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
        std::fprintf(stderr, "Usage: %s <input.hdr> <output.png> [tonemap=linear|aces|agx|agx-golden|agx-punchy]\n", argv[0]);
        return 1;
    }

    const char* input_path = argv[1];
    const char* output_path = argv[2];
    Image::ToneMapping preset = Image::ToneMapping::ACES;
    bool linear = false;
    if (argc == 4) {
        std::string preset_name = argv[3];
        std::transform(preset_name.begin(), preset_name.end(), preset_name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        linear = preset_name == "linear";
        if (!linear) preset = parse_preset(preset_name);
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

    const bool saved = linear ? img.save_png(output_path) : img.save_with_tonemapping(output_path, preset);
    if (!saved) {
        std::fprintf(stderr, "Failed to write tonemapped PNG to '%s'\n", output_path);
        return 1;
    }

    std::printf("Wrote PNG to %s using %s mapping\n", output_path, linear ? "linear" : "tone");
    return 0;
}
