#pragma once
#include <array>
#include <cmath>
#include <string>

// Colors are six-digit sRGB hex values in configuration and linear reference
// white values in the MAIN shader. The default 040404 is near-black (sRGB decoded to linear).
struct SubtitleGeneratedGrayStyle
{
    std::array<float, 3> color{0.001214108f, 0.001214108f, 0.001214108f};
    // Linear-light glyph tint: white is an exact identity, gray dims SDR/HDR text.
    std::array<float, 3> textColor{1.0f, 1.0f, 1.0f};
    float opacity = 0.85f;
    // Source-pixel radius; zero disables whole-overlay backdrop blur. Smooth separable Gaussian; support radius in source pixels.
    float blurPixels = 3.0f;
    // Highlight-shoulder knee in linear reference-white units. The shader
    // compresses highlights smoothly so texture does not collapse to a flat cap.
    float maxLuminance = 0.16f;
    std::array<float, 3> borderColor{0.0f, 0.0f, 0.0f};
    float borderOpacity = 0.65f;
    float borderWidth = 0.0f;
};

inline bool ParseSubtitleRgbHex(const std::string& text, std::array<float, 3>& linear)
{
    if (text.size() != 6) return false;
    auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::array<float, 3> parsed{};
    for (int i = 0; i < 3; ++i) {
        const int hi = digit(text[size_t(2 * i)]), lo = digit(text[size_t(2 * i + 1)]);
        if (hi < 0 || lo < 0) return false;
        const float encoded = float(16 * hi + lo) / 255.0f;
        parsed[size_t(i)] = encoded <= 0.04045f ? encoded / 12.92f :
            std::pow((encoded + 0.055f) / 1.055f, 2.4f);
    }
    linear = parsed;
    return true;
}
