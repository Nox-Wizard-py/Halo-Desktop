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
    static constexpr uint32_t DOWNSAMPLE_WIDTH  = 48;
    static constexpr uint32_t DOWNSAMPLE_HEIGHT = 48;
    static constexpr uint32_t PIXEL_COUNT       = DOWNSAMPLE_WIDTH * DOWNSAMPLE_HEIGHT; // 2304 pixels
    static constexpr uint32_t BUFFER_SIZE       = PIXEL_COUNT * 4;                     // 9216 bytes

    // Extracts a harmonic dual-color rim palette from a 48x48 RGBA pixel buffer.
    // Uses the original LUMN 1.1 hue-binning approach (saturation/brightness weighted).
    static RimPalette ExtractFromRGBA48x48(const uint8_t* rgbaPixels);

    // -------------------------------------------------------------------------
    // Volume-Weighted Median Cut Quantization (VWMC)
    // -------------------------------------------------------------------------
    // Extracts exactly 4 mathematically dominant colors from a 48x48 RGBA pixel
    // buffer without any saturation, brightness, or hue bias.
    //
    // Algorithm:
    //   1. Gather all opaque pixels (alpha >= 128) into a flat pool.
    //   2. Place the entire pool into one RGB bounding box.
    //   3. Repeatedly select the box with the most pixels, find its longest
    //      color axis (max R/G/B spread), sort along that axis, and split at
    //      the median — yielding two new boxes.
    //   4. Continue until 4 boxes exist.
    //   5. Return the per-channel arithmetic mean (centroid) of each box as the
    //      four output colors, ordered from most-to-least populated.
    //
    // The 4 centroids are mapped onto the existing RimPalette slots:
    //   hot    <- most populated box
    //   deep   <- second most populated box
    //   color2 <- third  most populated box
    //   color3 <- fourth most populated box
    //   balance is set proportionally from box 1 / (box 1 + box 2) pixel counts
    //
    // No pixel is ever discarded for being too dark, too grey, or too muted.
    static RimPalette ExtractMedianCut(const uint8_t* rgbaPixels);

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

private:
    // -------------------------------------------------------------------------
    // Internal VWMC types — not exposed to callers
    // -------------------------------------------------------------------------

    // A single pixel stored as packed uint8 to minimise memory and avoid float
    // conversion until the final centroid calculation.
    struct Pixel8
    {
        uint8_t r, g, b;
    };

    // A box in RGB color space, represented by a [begin, end) half-open range
    // into the shared pixel pool.  All sorting is done in-place on the pool, so
    // boxes need only store their slice boundaries.
    struct Box
    {
        uint32_t begin;  // inclusive index into the pixel pool
        uint32_t end;    // exclusive index into the pixel pool
        uint8_t  rMin, rMax;
        uint8_t  gMin, gMax;
        uint8_t  bMin, bMax;

        // Number of pixels in this box
        [[nodiscard]] uint32_t count() const noexcept { return end - begin; }

        // Length of the longest color axis in [0, 255] units — implemented in .cpp
        // to avoid std::max initializer_list in headers included by Windows TUs.
        [[nodiscard]] uint8_t longestSpan() const noexcept;
    };

    // Recompute the R/G/B extents of a box from its current pixel slice.
    static void RecomputeBounds(Box& box, std::vector<Pixel8>& pool) noexcept;

    // Split a box at the median of its longest axis, returning the upper half.
    // The pixel pool slice [box.begin, box.end) is reordered in-place.
    static Box SplitBox(Box& box, std::vector<Pixel8>& pool);
};

} // namespace lumn::media
