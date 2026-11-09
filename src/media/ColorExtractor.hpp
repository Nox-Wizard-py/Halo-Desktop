#pragma once

#include <cstdint>
#include <vector>

namespace lumn::media
{

struct RimPalette
{
    float hot[4]    = { 1.00f, 0.24f, 0.20f, 1.00f }; // LUMN fallback hot (Warm fiery red/orange)
    float deep[4]   = { 0.55f, 0.05f, 0.07f, 1.00f }; // LUMN fallback deep (Deep crimson red)
    float color2[4] = { 0.60f, 0.15f, 1.00f, 1.00f }; // Secondary Accent (Neon Purple)
    float color3[4] = { 0.95f, 0.25f, 0.65f, 1.00f }; // Tertiary Highlight (Luminous Magenta)
    float balance   = 0.50f;                           // Chromatic balance ratio [0.15, 0.85]
    bool hasArtwork = false;                           // True if extracted from live album art
};

// Original LUMN 1.1 RimColorModel Palette Extraction Engine
// Reverse-engineered from scratch/Lumn_arm64 (0x10008154c - 0x100081ffc)
//
// Method:
// - Downsamples source artwork to 48x48 RGBA (2304 pixels)
// - Quantizes vibrant pixels (Saturation >= 0.25, Brightness >= 0.15) into 12 hue sectors (30 deg each)
// - Applies non-linear saliency weighting: w = Brightness * Saturation^2
// - Extracts dominant hue as HotColor (boosted to Saturation >= 0.60, Brightness >= 0.72)
// - Searches candidate bins with circular distance >= 2 (>= 60 deg) for DeepColor companion
// - If single-hue or dominant > 60%, harmonically derives DeepColor with a +/-0.16 brightness shift
// - Calculates dynamic balance = clamp(N_dom / (N_dom + N_sec), 0.15, 0.85)
class ColorExtractor
{
public:
    static constexpr uint32_t DOWNSAMPLE_WIDTH = 48;
    static constexpr uint32_t DOWNSAMPLE_HEIGHT = 48;
    static constexpr uint32_t PIXEL_COUNT = DOWNSAMPLE_WIDTH * DOWNSAMPLE_HEIGHT; // 2304 pixels
    static constexpr uint32_t BUFFER_SIZE = PIXEL_COUNT * 4;                      // 9216 bytes

    // Extracts a harmonic dual-color rim palette from a 48x48 RGBA pixel buffer
    static RimPalette ExtractFromRGBA48x48(const uint8_t* rgbaPixels);

    // HSB color representation (LUMN native)
    struct HSB
    {
        float h = 0.0f; // Hue [0.0, 1.0]
        float s = 0.0f; // Saturation [0.0, 1.0]
        float b = 0.0f; // Brightness / Value [0.0, 1.0]
    };

    struct RGB
    {
        float r = 0.0f; // [0.0, 1.0]
        float g = 0.0f; // [0.0, 1.0]
        float b = 0.0f; // [0.0, 1.0]
    };

    // OKLab color space representation (retained for backward compatibility and tests)
    struct OKLab
    {
        float L = 0.0f; // Perceived lightness [0.0, 1.0]
        float a = 0.0f; // Green-Red opponent channel
        float b = 0.0f; // Blue-Yellow opponent channel
    };

    // Color conversion utilities
    static HSB RGBtoHSB(float r, float g, float b) noexcept;
    static RGB HSBtoRGB(float h, float s, float b) noexcept;

    static OKLab RGBtoOKLab(float r, float g, float b) noexcept;
    static RGB OKLabtoRGB(float L, float a, float b) noexcept;
    static float DeltaE(const OKLab& c1, const OKLab& c2) noexcept;

    static float sRGBtoLinear(float c) noexcept;
    static float linearTosRGB(float c) noexcept;
};

} // namespace lumn::media
