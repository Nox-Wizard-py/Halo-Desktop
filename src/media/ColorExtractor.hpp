#pragma once

#include <cstdint>
#include <vector>

namespace lumn::media
{

struct RimPalette
{
    float hot[4]  = { 0.20f, 0.85f, 1.00f, 1.00f }; // Luminous accent color (Electric Cyan default)
    float deep[4] = { 0.55f, 0.10f, 0.95f, 1.00f }; // Rich dark foundation color (Neon Purple default)
    float balance = 0.50f;                           // Chromatic balance ratio [0.30, 0.70]
    bool hasArtwork = false;                         // True if extracted from live album art
};

// Phase 3.1: Perceptual Color Extraction Engine (OKLab Color Space)
// Extracts balanced, high-fidelity rim lighting palettes from 48x48 RGBA artwork buffers.
//
// Roles:
// - DeepColor: Rich, dark foundation color (perceptual lightness L in [0.18, 0.35], good chroma)
// - HotColor:  Luminous, vivid accent color (perceptual lightness L in [0.55, 0.82], high chroma)
// - Balance:   Dynamic ratio determined by relative visual saliency [0.30, 0.70]
class ColorExtractor
{
public:
    static constexpr uint32_t DOWNSAMPLE_WIDTH = 48;
    static constexpr uint32_t DOWNSAMPLE_HEIGHT = 48;
    static constexpr uint32_t PIXEL_COUNT = DOWNSAMPLE_WIDTH * DOWNSAMPLE_HEIGHT; // 2304 pixels
    static constexpr uint32_t BUFFER_SIZE = PIXEL_COUNT * 4;                      // 9216 bytes

    // Extracts a harmonic dual-color rim palette from a 48x48 RGBA pixel buffer
    static RimPalette ExtractFromRGBA48x48(const uint8_t* rgbaPixels);

    // OKLab color space representation
    struct OKLab
    {
        float L = 0.0f; // Perceived lightness [0.0, 1.0]
        float a = 0.0f; // Green (negative) to Red (positive) opponent channel
        float b = 0.0f; // Blue (negative) to Yellow (positive) opponent channel
    };

    struct RGB
    {
        float r = 0.0f; // [0.0, 1.0]
        float g = 0.0f; // [0.0, 1.0]
        float b = 0.0f; // [0.0, 1.0]
    };

    // Fast, precise conversions between sRGB and OKLab
    static OKLab RGBtoOKLab(float r, float g, float b) noexcept;
    static RGB OKLabtoRGB(float L, float a, float b) noexcept;

    // Perceptual color difference in OKLab (Delta E)
    static float DeltaE(const OKLab& c1, const OKLab& c2) noexcept;

    // Fast sRGB <-> linear conversions
    static float sRGBtoLinear(float c) noexcept;
    static float linearTosRGB(float c) noexcept;
};

} // namespace lumn::media
