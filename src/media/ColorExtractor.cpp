#include "ColorExtractor.hpp"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <array>
#include <string>
#include <cstdio>
namespace lumn::media
{

float ColorExtractor::sRGBtoLinear(float c) noexcept
{
    c = std::clamp(c, 0.0f, 1.0f);
    return (c <= 0.04045f) ? (c / 12.92f) : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float ColorExtractor::linearTosRGB(float c) noexcept
{
    c = std::clamp(c, 0.0f, 1.0f);
    return (c <= 0.0031308f) ? (12.92f * c) : (1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f);
}

ColorExtractor::OKLab ColorExtractor::RGBtoOKLab(float r, float g, float b) noexcept
{
    float lr = sRGBtoLinear(r);
    float lg = sRGBtoLinear(g);
    float lb = sRGBtoLinear(b);

    // Linear sRGB to cone LMS
    float l = 0.4122214708f * lr + 0.5363325363f * lg + 0.0514459929f * lb;
    float m = 0.2119034982f * lr + 0.6806995451f * lg + 0.1073969566f * lb;
    float s = 0.0883024619f * lr + 0.2817188376f * lg + 0.6299787005f * lb;

    // Non-linear cube root response
    float l_ = std::cbrt(l);
    float m_ = std::cbrt(m);
    float s_ = std::cbrt(s);

    // LMS to OKLab (L: Lightness, a: Green-Red, b: Blue-Yellow)
    return {
        0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_,
        1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_,
        0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_
    };
}

ColorExtractor::RGB ColorExtractor::OKLabtoRGB(float L, float a, float b) noexcept
{
    // OKLab to non-linear LMS
    float l_ = L + 0.3963377774f * a + 0.2158037573f * b;
    float m_ = L - 0.1055613458f * a - 0.0638541728f * b;
    float s_ = L - 0.0894841775f * a - 1.2914855480f * b;

    float l = l_ * l_ * l_;
    float m = m_ * m_ * m_;
    float s = s_ * s_ * s_;

    // LMS to linear sRGB
    float lr = +4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
    float lg = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
    float lb = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;

    return {
        linearTosRGB(lr),
        linearTosRGB(lg),
        linearTosRGB(lb)
    };
}

ColorExtractor::RGB ColorExtractor::GamutSafeOKLabtoRGB(float L, float a, float b) noexcept
{
    RGB rgb = OKLabtoRGB(L, a, b);
    if (rgb.r >= 0.0f && rgb.r <= 1.0f &&
        rgb.g >= 0.0f && rgb.g <= 1.0f &&
        rgb.b >= 0.0f && rgb.b <= 1.0f)
    {
        return rgb;
    }

    // 5-iteration bisection on chroma multiplier to preserve exact hue and lightness
    float low = 0.0f;
    float high = 1.0f;
    RGB best = OKLabtoRGB(L, 0.0f, 0.0f);
    best.r = std::clamp(best.r, 0.0f, 1.0f);
    best.g = std::clamp(best.g, 0.0f, 1.0f);
    best.b = std::clamp(best.b, 0.0f, 1.0f);

    for (int iter = 0; iter < 5; ++iter)
    {
        float mid = (low + high) * 0.5f;
        RGB test = OKLabtoRGB(L, a * mid, b * mid);
        if (test.r >= 0.0f && test.r <= 1.0f &&
            test.g >= 0.0f && test.g <= 1.0f &&
            test.b >= 0.0f && test.b <= 1.0f)
        {
            best = test;
            low = mid;
        }
        else
        {
            high = mid;
        }
    }
    return best;
}

float ColorExtractor::DeltaE(const OKLab& c1, const OKLab& c2) noexcept
{
    float dL = c1.L - c2.L;
    float da = c1.a - c2.a;
    float db = c1.b - c2.b;
    return std::sqrt(dL * dL + da * da + db * db);
}

ColorExtractor::HSB ColorExtractor::RGBtoHSB(float r, float g, float b) noexcept
{
    float maxV = std::max({ r, g, b });
    float minV = std::min({ r, g, b });
    float delta = maxV - minV;

    float h = 0.0f;
    float s = (maxV > 1e-5f) ? (delta / maxV) : 0.0f;
    float v = maxV;

    if (delta > 1e-5f)
    {
        if (maxV == r)
        {
            h = (g - b) / delta;
        }
        else if (maxV == g)
        {
            h = 2.0f + (b - r) / delta;
        }
        else
        {
            h = 4.0f + (r - g) / delta;
        }
        h /= 6.0f;
        if (h < 0.0f) h += 1.0f;
    }
    return { h, s, v };
}

ColorExtractor::RGB ColorExtractor::HSBtoRGB(float h, float s, float v) noexcept
{
    if (s <= 1e-5f) return { v, v, v };

    float sector = std::fmod(h * 6.0f, 6.0f);
    if (sector < 0.0f) sector += 6.0f;
    int i = static_cast<int>(sector);
    float f = sector - static_cast<float>(i);
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));

    switch (i)
    {
    case 0: return { v, t, p };
    case 1: return { q, v, p };
    case 2: return { p, v, t };
    case 3: return { p, q, v };
    case 4: return { t, p, v };
    default: return { v, p, q };
    }
}

RimPalette ColorExtractor::ExtractFromRGBA48x48(const uint8_t* rgbaPixels)
{
    RimPalette result{};
    if (!rgbaPixels)
    {
        return result;
    }

    constexpr uint32_t W = DOWNSAMPLE_WIDTH;
    constexpr uint32_t H = DOWNSAMPLE_HEIGHT;
    constexpr uint32_t TOTAL_PIXELS = W * H;

    struct HueBin
    {
        float weightSum = 0.0f;
        float rgbSum[3] = { 0.0f, 0.0f, 0.0f };
        int pixelCount = 0;
    };

    HueBin bins[12] = {};
    float neutralRgbSum[3] = { 0.0f, 0.0f, 0.0f };
    int neutralCount = 0;
    int totalOpaqueCount = 0;

    for (uint32_t i = 0; i < TOTAL_PIXELS * 4; i += 4)
    {
        float a = rgbaPixels[i + 3] / 255.0f;
        if (a < 0.5f)
        {
            continue; // Alpha gate (>= 50% opaque)
        }
        totalOpaqueCount++;

        float r = rgbaPixels[i + 0] / 255.0f;
        float g = rgbaPixels[i + 1] / 255.0f;
        float b = rgbaPixels[i + 2] / 255.0f;

        HSB hsb = RGBtoHSB(r, g, b);

        // Vibrancy gate: Saturation >= 0.25 and Brightness >= 0.15
        if (hsb.s >= 0.25f && hsb.b >= 0.15f)
        {
            int binIndex = std::clamp(static_cast<int>(hsb.h * 12.0f), 0, 11);
            float weight = hsb.b * (hsb.s * hsb.s); // Brightness * Saturation^2

            bins[binIndex].weightSum += weight;
            bins[binIndex].rgbSum[0] += r * weight;
            bins[binIndex].rgbSum[1] += g * weight;
            bins[binIndex].rgbSum[2] += b * weight;
            bins[binIndex].pixelCount++;
        }
        else
        {
            neutralRgbSum[0] += r;
            neutralRgbSum[1] += g;
            neutralRgbSum[2] += b;
            neutralCount++;
        }
    }

    if (totalOpaqueCount == 0)
    {
        return result;
    }

    int coloredCount = totalOpaqueCount - neutralCount;

    // Grayscale / low-saturation branch: < 18% colorful pixels
    if (static_cast<float>(coloredCount) / static_cast<float>(totalOpaqueCount) < 0.18f || coloredCount == 0)
    {
        float avgR = (neutralCount > 0) ? (neutralRgbSum[0] / neutralCount) : 0.80f;
        float avgG = (neutralCount > 0) ? (neutralRgbSum[1] / neutralCount) : 0.80f;
        float avgB = (neutralCount > 0) ? (neutralRgbSum[2] / neutralCount) : 0.80f;

        HSB hsb = RGBtoHSB(avgR, avgG, avgB);
        hsb.s = std::min(hsb.s, 0.06f);

        if (hsb.b < 0.30f)
        {
            hsb.b = std::max(hsb.b, 0.12f);
            RGB hot = HSBtoRGB(hsb.h, hsb.s, hsb.b);
            RGB deep = HSBtoRGB(hsb.h, hsb.s, std::min(hsb.b + 0.10f, 0.30f));

            result.hot[0] = hot.r; result.hot[1] = hot.g; result.hot[2] = hot.b; result.hot[3] = 1.0f;
            result.deep[0] = deep.r; result.deep[1] = deep.g; result.deep[2] = deep.b; result.deep[3] = 1.0f;
            result.color2[0] = (hot.r + deep.r) * 0.5f; result.color2[1] = (hot.g + deep.g) * 0.5f; result.color2[2] = (hot.b + deep.b) * 0.5f; result.color2[3] = 1.0f;
            result.color3[0] = std::min(hot.r + 0.15f, 1.0f); result.color3[1] = std::min(hot.g + 0.15f, 1.0f); result.color3[2] = std::min(hot.b + 0.15f, 1.0f); result.color3[3] = 1.0f;
            result.balance = 0.50f;
            result.hasArtwork = true;
            return result;
        }
        else
        {
            hsb.b = std::min(hsb.b, 0.55f);
            RGB hot = HSBtoRGB(hsb.h, hsb.s, hsb.b);
            HSB deepHsb = RGBtoHSB(hot.r, hot.g, hot.b);
            deepHsb.b = (deepHsb.b >= 0.50f) ? std::max(deepHsb.b - 0.16f, 0.30f) : std::min(deepHsb.b + 0.16f, 0.95f);
            deepHsb.s = (deepHsb.s > 0.20f) ? std::max(deepHsb.s, 0.60f) : deepHsb.s;
            RGB deep = HSBtoRGB(deepHsb.h, deepHsb.s, deepHsb.b);

            result.hot[0] = hot.r; result.hot[1] = hot.g; result.hot[2] = hot.b; result.hot[3] = 1.0f;
            result.deep[0] = deep.r; result.deep[1] = deep.g; result.deep[2] = deep.b; result.deep[3] = 1.0f;
            result.color2[0] = (hot.r + deep.r) * 0.5f; result.color2[1] = (hot.g + deep.g) * 0.5f; result.color2[2] = (hot.b + deep.b) * 0.5f; result.color2[3] = 1.0f;
            result.color3[0] = std::min(hot.r + 0.15f, 1.0f); result.color3[1] = std::min(hot.g + 0.15f, 1.0f); result.color3[2] = std::min(hot.b + 0.15f, 1.0f); result.color3[3] = 1.0f;
            result.balance = 0.50f;
            result.hasArtwork = true;
            return result;
        }
    }

    struct QualifiedBin
    {
        int index = 0;
        RGB color{};
        int count = 0;
    };

    std::vector<QualifiedBin> qualified;
    for (int i = 0; i < 12; ++i)
    {
        if (bins[i].weightSum > 0.05f && bins[i].pixelCount >= 1)
        {
            float invW = 1.0f / bins[i].weightSum;
            QualifiedBin qb;
            qb.index = i;
            qb.color = { bins[i].rgbSum[0] * invW, bins[i].rgbSum[1] * invW, bins[i].rgbSum[2] * invW };
            qb.count = bins[i].pixelCount;
            qualified.push_back(qb);
        }
    }

    if (qualified.empty())
    {
        result.hasArtwork = true;
        return result;
    }

    // Sort qualified bins descending by pixel count
    std::sort(qualified.begin(), qualified.end(), [](const QualifiedBin& a, const QualifiedBin& b) {
        return a.count > b.count;
    });

    // 1. Dominant / HotColor (Primary Vibrant Accent)
    const auto& dominant = qualified[0];
    HSB hotHsb = RGBtoHSB(dominant.color.r, dominant.color.g, dominant.color.b);
    hotHsb.s = std::max(hotHsb.s, 0.60f); // Saturation >= 0.60
    hotHsb.b = std::max(hotHsb.b, 0.72f); // Brightness >= 0.72
    RGB hot = HSBtoRGB(hotHsb.h, hotHsb.s, hotHsb.b);

    result.hot[0] = hot.r;
    result.hot[1] = hot.g;
    result.hot[2] = hot.b;
    result.hot[3] = 1.0f;

    // 2. Multi-Color Palette Extraction: Find distinct Secondary (Color2) and Tertiary (Color3) candidate bins
    const QualifiedBin* secBin = nullptr;
    const QualifiedBin* tertBin = nullptr;

    for (size_t i = 1; i < qualified.size(); ++i)
    {
        const auto& cand = qualified[i];
        int diff = std::abs(cand.index - dominant.index);
        int circularDist = std::min(diff, 12 - diff);
        if (circularDist > 1 && !secBin) // At least 2 sectors (>= 60 deg apart)
        {
            secBin = &cand;
        }
        else if (secBin && !tertBin)
        {
            int diffSec = std::abs(cand.index - secBin->index);
            int distSec = std::min(diffSec, 12 - diffSec);
            if (circularDist >= 1 && distSec >= 1)
            {
                tertBin = &cand;
            }
        }
    }

    // Color 2 (Secondary Accent)
    RGB c2;
    if (secBin)
    {
        HSB hsb2 = RGBtoHSB(secBin->color.r, secBin->color.g, secBin->color.b);
        hsb2.s = std::max(hsb2.s, 0.60f);
        hsb2.b = std::max(hsb2.b, 0.70f);
        c2 = HSBtoRGB(hsb2.h, hsb2.s, hsb2.b);

        float totalPop = static_cast<float>(dominant.count + secBin->count);
        result.balance = std::clamp(static_cast<float>(dominant.count) / totalPop, 0.15f, 0.85f);
    }
    else
    {
        // Harmonic analogous shift (+30 deg)
        float h2 = std::fmod(hotHsb.h + (1.0f / 12.0f), 1.0f);
        c2 = HSBtoRGB(h2, std::max(hotHsb.s, 0.60f), std::max(hotHsb.b - 0.10f, 0.60f));
        result.balance = 0.50f;
    }
    result.color2[0] = c2.r; result.color2[1] = c2.g; result.color2[2] = c2.b; result.color2[3] = 1.0f;

    // Color 3 (Tertiary Accent / Luminous Highlight)
    RGB c3;
    if (tertBin)
    {
        HSB hsb3 = RGBtoHSB(tertBin->color.r, tertBin->color.g, tertBin->color.b);
        hsb3.s = std::max(hsb3.s, 0.55f);
        hsb3.b = std::max(hsb3.b, 0.75f);
        c3 = HSBtoRGB(hsb3.h, hsb3.s, hsb3.b);
    }
    else
    {
        // Harmonic complementary shift (-30 deg)
        float h3 = std::fmod(hotHsb.h - (1.0f / 12.0f) + 1.0f, 1.0f);
        c3 = HSBtoRGB(h3, std::max(hotHsb.s * 0.85f, 0.50f), std::min(hotHsb.b + 0.15f, 0.95f));
    }
    result.color3[0] = c3.r; result.color3[1] = c3.g; result.color3[2] = c3.b; result.color3[3] = 1.0f;

    // Color 4 / DeepColor (Grounded Foundation Tone)
    HSB deepHsb = RGBtoHSB(hot.r, hot.g, hot.b);
    if (secBin)
    {
        deepHsb = RGBtoHSB(c2.r, c2.g, c2.b);
    }
    deepHsb.b = (deepHsb.b >= 0.50f) ? std::max(deepHsb.b - 0.25f, 0.25f) : std::min(deepHsb.b + 0.16f, 0.95f);
    if (deepHsb.s > 0.20f)
    {
        deepHsb.s = std::max(deepHsb.s, 0.65f);
    }
    RGB deep = HSBtoRGB(deepHsb.h, deepHsb.s, deepHsb.b);
    result.deep[0] = deep.r;
    result.deep[1] = deep.g;
    result.deep[2] = deep.b;
    result.deep[3] = 1.0f;
    result.hasArtwork = true;

    return result;
}

// =============================================================================
// Volume-Weighted Median Cut Quantization (VWMC)
// =============================================================================
// Design goals
//   - Strictly volume-driven: the box chosen for each split is always the one
//     with the highest raw pixel count — no saturation/brightness weighting.
//   - Absolutely neutral: dark, grey, sepia, and muted pixels are treated
//     identically to vivid ones.  Nothing is discarded before the split phase.
//   - Zero allocation in the hot path: a single std::vector<Pixel8> is
//     pre-allocated once per call and reused for all in-place partition steps.
//   - Boxes are stored in a small fixed-size std::array<Box, 4>; no heap
//     growth after the initial pixel gather.
//
// Complexity:
//   For N = 48 × 48 = 2 304 pixels and 3 split rounds (1 → 2 → 4 boxes):
//   Each split is O(N log N) for the nth_element + sort, so total is
//   O(3 N log N) ≈ 33 000 comparisons — negligible on a track-change event.
// =============================================================================

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

uint8_t ColorExtractor::Box::longestSpan() const noexcept
{
    // Manual three-way max to avoid the initializer_list overload of std::max,
    // which conflicts with the Win32 max() macro in Windows-aware translation units.
    uint8_t dr = rMax - rMin;
    uint8_t dg = gMax - gMin;
    uint8_t db = bMax - bMin;
    uint8_t m  = dr > dg ? dr : dg;
    return m > db ? m : db;
}

void ColorExtractor::RecomputeBounds(Box& box, std::vector<Pixel8>& pool) noexcept
{
    // Walk the pixel slice and tighten the axis-aligned bounding box.
    uint8_t rMin = 255, rMax = 0;
    uint8_t gMin = 255, gMax = 0;
    uint8_t bMin = 255, bMax = 0;

    for (uint32_t i = box.begin; i < box.end; ++i)
    {
        const Pixel8& p = pool[i];
        if (p.r < rMin) rMin = p.r;  if (p.r > rMax) rMax = p.r;
        if (p.g < gMin) gMin = p.g;  if (p.g > gMax) gMax = p.g;
        if (p.b < bMin) bMin = p.b;  if (p.b > bMax) bMax = p.b;
    }

    box.rMin = rMin;  box.rMax = rMax;
    box.gMin = gMin;  box.gMax = gMax;
    box.bMin = bMin;  box.bMax = bMax;
}

ColorExtractor::Box ColorExtractor::SplitBox(Box& box, std::vector<Pixel8>& pool)
{
    // -------------------------------------------------------------------------
    // 1. Identify the longest color axis.
    // -------------------------------------------------------------------------
    const uint8_t dr = box.rMax - box.rMin;
    const uint8_t dg = box.gMax - box.gMin;
    const uint8_t db = box.bMax - box.bMin;

    // -------------------------------------------------------------------------
    // 2. Sort the box's pixel slice along the dominant axis.
    //    std::sort on 2304 uint8 values takes ~15 µs on a modern CPU — fine for
    //    a track-change event.  We use a plain lambda comparator to let the
    //    compiler inline the per-axis branch at compile time via if-constexpr
    //    simulation at runtime.
    // -------------------------------------------------------------------------
    auto* begin = pool.data() + box.begin;
    auto* end   = pool.data() + box.end;

    if (dr >= dg && dr >= db)
    {
        std::sort(begin, end, [](const Pixel8& a, const Pixel8& b) noexcept {
            return a.r < b.r;
        });
    }
    else if (dg >= dr && dg >= db)
    {
        std::sort(begin, end, [](const Pixel8& a, const Pixel8& b) noexcept {
            return a.g < b.g;
        });
    }
    else
    {
        std::sort(begin, end, [](const Pixel8& a, const Pixel8& b) noexcept {
            return a.b < b.b;
        });
    }

    // -------------------------------------------------------------------------
    // 3. Split at the median pixel (integer midpoint of the slice).
    // -------------------------------------------------------------------------
    const uint32_t mid = box.begin + box.count() / 2;

    // Upper half → new box; lower half stays in `box` (mutated in place).
    Box upper;
    upper.begin = mid;
    upper.end   = box.end;
    box.end     = mid;

    // -------------------------------------------------------------------------
    // 4. Recompute tight bounds for both halves.
    // -------------------------------------------------------------------------
    RecomputeBounds(box, pool);
    RecomputeBounds(upper, pool);

    return upper;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

RimPalette ColorExtractor::ExtractMedianCut(const uint8_t* rgbaPixels)
{
    RimPalette result{};
    if (!rgbaPixels)
    {
        return result;
    }

    // -------------------------------------------------------------------------
    // Phase 1 — Gather pixels
    // -------------------------------------------------------------------------
    // Pre-allocate for the maximum possible count (all pixels fully opaque) to
    // avoid any reallocation inside the loop.
    constexpr uint32_t MAX_PIXELS = PIXEL_COUNT; // 2 304
    std::vector<Pixel8> pool;
    pool.reserve(MAX_PIXELS);

    for (uint32_t i = 0; i < MAX_PIXELS * 4; i += 4)
    {
        // Discard fully-transparent pixels only; keep everything >= 50% opaque.
        if (rgbaPixels[i + 3] < 128u)
        {
            continue;
        }
        pool.push_back({ rgbaPixels[i], rgbaPixels[i + 1], rgbaPixels[i + 2] });
    }

    if (pool.empty())
    {
        return result;
    }

    // -------------------------------------------------------------------------
    // Phase 2 — Initialise the single root box spanning the entire pool.
    // -------------------------------------------------------------------------
    Box root;
    root.begin = 0;
    root.end   = static_cast<uint32_t>(pool.size());
    RecomputeBounds(root, pool);

    // boxes is a fixed-capacity container: we start with 1 and grow to 4.
    // Using std::array avoids heap allocation; activeCount tracks logical size.
    std::array<Box, 4> boxes;
    boxes[0]         = root;
    uint32_t activeCount = 1;

    // -------------------------------------------------------------------------
    // Phase 3 — Volume-strict split loop (3 iterations: 1→2→3→4 boxes)
    // -------------------------------------------------------------------------
    while (activeCount < 4)
    {
        // --- Select the box with the most pixels (volume-only criterion). ---
        uint32_t targetIdx = 0;
        uint32_t maxCount  = 0;
        for (uint32_t i = 0; i < activeCount; ++i)
        {
            // A box with only 1 pixel cannot be split; skip it.
            if (boxes[i].count() > maxCount && boxes[i].count() > 1)
            {
                maxCount  = boxes[i].count();
                targetIdx = i;
            }
        }

        // If every box has ≤ 1 pixel we cannot split further; break early.
        if (maxCount <= 1)
        {
            break;
        }

        // --- Split the chosen box. ------------------------------------------
        // SplitBox mutates boxes[targetIdx] (lower half) and returns the upper
        // half, which we append to the array.
        boxes[activeCount] = SplitBox(boxes[targetIdx], pool);
        ++activeCount;
    }

    // -------------------------------------------------------------------------
    // Phase 4 — Compute per-box centroids (arithmetic mean in [0, 255] space).
    // -------------------------------------------------------------------------
    // Sort boxes descending by pixel count so slot 0 == most dominant colour.
    std::sort(boxes.begin(), boxes.begin() + activeCount, [](const Box& a, const Box& b) noexcept {
        return a.count() > b.count();
    });

    // For each box, sum R, G, B over all its pixels and divide by count.
    // Using 64-bit accumulators to prevent overflow for large boxes.
    struct Centroid { float r, g, b; };
    std::array<Centroid, 4> centroids{};

    for (uint32_t bi = 0; bi < activeCount; ++bi)
    {
        const Box& bx = boxes[bi];
        uint64_t rSum = 0, gSum = 0, bSum = 0;

        for (uint32_t pi = bx.begin; pi < bx.end; ++pi)
        {
            rSum += pool[pi].r;
            gSum += pool[pi].g;
            bSum += pool[pi].b;
        }

        const float inv  = 1.0f / static_cast<float>(bx.count());
        centroids[bi].r  = static_cast<float>(rSum) * inv / 255.0f;
        centroids[bi].g  = static_cast<float>(gSum) * inv / 255.0f;
        centroids[bi].b  = static_cast<float>(bSum) * inv / 255.0f;
    }

    // -------------------------------------------------------------------------
    // Phase 5 — Map centroids onto RimPalette slots.
    // -------------------------------------------------------------------------
    // Slot assignment (most → least populated):
    //   hot    ← centroid[0]  (largest box, the single most-present colour mass)
    //   deep   ← centroid[1]  (second largest; ground foundation tone)
    //   color2 ← centroid[2]  (third)
    //   color3 ← centroid[3]  (fourth; smallest dominant region)
    //
    // balance is derived from the relative population of the top two boxes,
    // matching the semantic used by the existing shader pipeline.
    for (uint32_t bi = 0; bi < activeCount; ++bi)
    {
        float* slot = nullptr;
        switch (bi)
        {
        case 0: slot = result.hot;    break;
        case 1: slot = result.deep;   break;
        case 2: slot = result.color2; break;
        case 3: slot = result.color3; break;
        default: break;
        }
        if (slot)
        {
            slot[0] = centroids[bi].r;
            slot[1] = centroids[bi].g;
            slot[2] = centroids[bi].b;
            slot[3] = 1.0f;
        }
    }

    // If we ended up with fewer than 4 boxes (degenerate tiny image), leave the
    // unfilled slots at their RimPalette defaults — they are never written above.

    // balance: ratio of box-0 population to (box-0 + box-1), clamped to the
    // same [0.15, 0.85] range used by the legacy extractor.
    if (activeCount >= 2)
    {
        const float n0 = static_cast<float>(boxes[0].count());
        const float n1 = static_cast<float>(boxes[1].count());
        result.balance = std::clamp(n0 / (n0 + n1), 0.15f, 0.85f);
    }

    result.hasArtwork = true;
    return result;
}

// =============================================================================
// Perceptual Hue-Binning & Visibility-Weighted Palette Extractor (2026 Engine)
// =============================================================================

RimPalette ColorExtractor::ExtractPalette(const uint8_t* rgbaPixels, PaletteDiagnostics* outDiag)
{
    RimPalette result{};
    if (!rgbaPixels)
    {
        return result;
    }

    constexpr uint32_t W = DOWNSAMPLE_WIDTH;
    constexpr uint32_t H = DOWNSAMPLE_HEIGHT;
    constexpr uint32_t TOTAL_PIXELS = W * H;

    struct Cluster
    {
        std::string name;
        int binIdx = -1;
        uint32_t count = 0;
        double rSum = 0.0, gSum = 0.0, bSum = 0.0;
        double lSum = 0.0, aSum = 0.0, bColSum = 0.0;

        void Add(float r, float g, float b, float L, float oklabA, float oklabB) noexcept
        {
            count++;
            rSum += r; gSum += g; bSum += b;
            lSum += L; aSum += oklabA; bColSum += oklabB;
        }

        void MergeFrom(const Cluster& other)
        {
            count += other.count;
            rSum += other.rSum; gSum += other.gSum; bSum += other.bSum;
            lSum += other.lSum; aSum += other.aSum; bColSum += other.bColSum;
        }

        [[nodiscard]] RGB CentroidRGB() const noexcept
        {
            if (count == 0) return { 0.0f, 0.0f, 0.0f };
            float inv = 1.0f / static_cast<float>(count);
            return {
                std::clamp(static_cast<float>(rSum * inv), 0.0f, 1.0f),
                std::clamp(static_cast<float>(gSum * inv), 0.0f, 1.0f),
                std::clamp(static_cast<float>(bSum * inv), 0.0f, 1.0f)
            };
        }

        [[nodiscard]] OKLab CentroidOKLab() const noexcept
        {
            if (count == 0) return { 0.0f, 0.0f, 0.0f };
            float inv = 1.0f / static_cast<float>(count);
            return {
                static_cast<float>(lSum * inv),
                static_cast<float>(aSum * inv),
                static_cast<float>(bColSum * inv)
            };
        }

        [[nodiscard]] float Chroma() const noexcept
        {
            OKLab ok = CentroidOKLab();
            return std::sqrt(ok.a * ok.a + ok.b * ok.b);
        }
    };

    static const char* const kBinNames[13] = {
        "Red", "Orange", "Yellow", "Y-Green", "Green", "Spring-G",
        "Cyan", "Azure", "Blue", "Violet", "Magenta", "Rose", "Neutral"
    };

    std::vector<Cluster> clusters(13);
    for (int i = 0; i < 13; ++i)
    {
        clusters[i].name = kBinNames[i];
        clusters[i].binIdx = i;
    }

    uint32_t totalOpaque = 0;
    for (uint32_t i = 0; i < TOTAL_PIXELS * 4; i += 4)
    {
        if (rgbaPixels[i + 3] < 128u) continue;
        totalOpaque++;

        float r = rgbaPixels[i + 0] / 255.0f;
        float g = rgbaPixels[i + 1] / 255.0f;
        float b = rgbaPixels[i + 2] / 255.0f;

        OKLab ok = RGBtoOKLab(r, g, b);
        float chroma = std::sqrt(ok.a * ok.a + ok.b * ok.b);

        // A pixel is classified as neutral if chroma < 0.035 in OKLab
        if (chroma < 0.035f)
        {
            clusters[12].Add(r, g, b, ok.L, ok.a, ok.b);
        }
        else
        {
            HSB hsb = RGBtoHSB(r, g, b);
            float hDeg = hsb.h * 360.0f;
            int binIdx = static_cast<int>((hDeg + 15.0f) / 30.0f) % 12;
            if (binIdx < 0) binIdx += 12;
            clusters[binIdx].Add(r, g, b, ok.L, ok.a, ok.b);
        }
    }

    if (totalOpaque == 0)
    {
        return result;
    }

    // Filter out active clusters
    std::vector<Cluster> active;
    for (auto& c : clusters)
    {
        if (c.count > 0) active.push_back(std::move(c));
    }

    // Merge perceptually similar adjacent bins (Requirement B)
    // Avoid artificial bin boundaries between neighboring chromatic bins (e.g. Blue & Violet in Sadiyan)
    bool mergedAny = true;
    while (mergedAny)
    {
        mergedAny = false;
        int bestI = -1, bestJ = -1;
        float minDist = 999.0f;

        for (size_t i = 0; i < active.size(); ++i)
        {
            for (size_t j = i + 1; j < active.size(); ++j)
            {
                if (active[i].binIdx < 12 && active[j].binIdx < 12)
                {
                    int diff = std::abs(active[i].binIdx - active[j].binIdx);
                    int circDiff = std::min(diff, 12 - diff);
                    if (circDiff == 1)
                    {
                        float d = DeltaE(active[i].CentroidOKLab(), active[j].CentroidOKLab());
                        if (d < 0.08f && d < minDist)
                        {
                            minDist = d;
                            bestI = static_cast<int>(i);
                            bestJ = static_cast<int>(j);
                        }
                    }
                }
            }
        }

        if (bestI >= 0 && bestJ >= 0)
        {
            active[bestI].name += "+" + active[bestJ].name;
            active[bestI].MergeFrom(active[bestJ]);
            active.erase(active.begin() + bestJ);
            mergedAny = true;
        }
    }

    // Candidate scoring and anti-noise gating (Requirements A & D)
    struct ScoredCandidate
    {
        Cluster cluster;
        float areaRatio = 0.0f;
        float meanChroma = 0.0f;
        float visibilityScore = 0.0f;
        std::string hexColor;
    };

    std::vector<ScoredCandidate> candidates;
    for (const auto& c : active)
    {
        float areaRatio = static_cast<float>(c.count) / static_cast<float>(totalOpaque);
        float chroma = c.Chroma();

        // Anti-noise gate: keep genuine accents occupying >= 1.2% area (28 pixels)
        if (areaRatio >= 0.012f)
        {
            float visScore = std::pow(areaRatio, 0.35f) * (0.05f + 3.0f * chroma);
            RGB rgb = c.CentroidRGB();
            char hexBuf[16];
            snprintf(hexBuf, sizeof(hexBuf), "#%02X%02X%02X",
                     static_cast<int>(rgb.r * 255.0f + 0.5f),
                     static_cast<int>(rgb.g * 255.0f + 0.5f),
                     static_cast<int>(rgb.b * 255.0f + 0.5f));

            ScoredCandidate sc;
            sc.cluster = c;
            sc.areaRatio = areaRatio;
            sc.meanChroma = chroma;
            sc.visibilityScore = visScore;
            sc.hexColor = hexBuf;
            candidates.push_back(sc);
        }
        else if (outDiag)
        {
            // Log anti-noise rejection
            RGB rgb = c.CentroidRGB();
            char hexBuf[16];
            snprintf(hexBuf, sizeof(hexBuf), "#%02X%02X%02X",
                     static_cast<int>(rgb.r * 255.0f + 0.5f),
                     static_cast<int>(rgb.g * 255.0f + 0.5f),
                     static_cast<int>(rgb.b * 255.0f + 0.5f));
            PaletteDiagnostics::CandidateInfo info;
            info.name = c.name;
            info.pixelCount = c.count;
            info.areaRatio = areaRatio;
            info.meanChroma = chroma;
            info.visibilityScore = 0.0f;
            info.hexColor = hexBuf;
            info.selected = false;
            info.rejectionReason = "Anti-noise gate: area < 1.2% (" + std::to_string(c.count) + " px)";
            outDiag->candidates.push_back(info);
        }
    }

    // Sort descending by visibility score
    std::sort(candidates.begin(), candidates.end(), [](const ScoredCandidate& a, const ScoredCandidate& b) {
        return a.visibilityScore > b.visibilityScore;
    });

    // Diverse selection using OKLab perceptual distance (Requirement C)
    constexpr float DIST_THRESH = 0.125f;
    std::vector<ScoredCandidate> selected;

    for (const auto& cand : candidates)
    {
        OKLab ok = cand.cluster.CentroidOKLab();
        bool isDistinct = true;
        std::string rejectionReason;

        for (const auto& sel : selected)
        {
            float d = DeltaE(ok, sel.cluster.CentroidOKLab());
            if (d < DIST_THRESH)
            {
                isDistinct = false;
                char buf[128];
                snprintf(buf, sizeof(buf), "Too close to %s (DeltaE=%.3f < %.3f)",
                         sel.cluster.name.c_str(), d, DIST_THRESH);
                rejectionReason = buf;
                break;
            }
        }

        if (outDiag)
        {
            PaletteDiagnostics::CandidateInfo info;
            info.name = cand.cluster.name;
            info.pixelCount = cand.cluster.count;
            info.areaRatio = cand.areaRatio;
            info.meanChroma = cand.meanChroma;
            info.visibilityScore = cand.visibilityScore;
            info.hexColor = cand.hexColor;
            OKLab cOk = cand.cluster.CentroidOKLab();
            info.oklab[0] = cOk.L; info.oklab[1] = cOk.a; info.oklab[2] = cOk.b;
            RGB cRgb = cand.cluster.CentroidRGB();
            info.rgb[0] = cRgb.r; info.rgb[1] = cRgb.g; info.rgb[2] = cRgb.b;
            info.selected = isDistinct && (selected.size() < 4);
            info.rejectionReason = rejectionReason;
            outDiag->candidates.push_back(info);
        }

        if (isDistinct)
        {
            selected.push_back(cand);
            if (selected.size() == 4)
            {
                if (!outDiag) break;
            }
        }
    }

    // Map selected candidates to 4 final colors
    // Gamut-safe luminance adjustments (Requirement E)
    RGB finalColors[4] = {};
    uint32_t numSelected = static_cast<uint32_t>(std::min<size_t>(selected.size(), 4));

    for (uint32_t i = 0; i < numSelected; ++i)
    {
        OKLab ok = selected[i].cluster.CentroidOKLab();
        // Preserve original identity while ensuring minimum visibility for dark tones
        if (ok.L < 0.15f) ok.L = 0.15f;
        finalColors[i] = GamutSafeOKLabtoRGB(ok.L, ok.a, ok.b);
        if (outDiag)
        {
            PaletteDiagnostics::CandidateInfo selInfo;
            selInfo.name = selected[i].cluster.name;
            selInfo.pixelCount = selected[i].cluster.count;
            selInfo.areaRatio = selected[i].areaRatio;
            selInfo.meanChroma = selected[i].meanChroma;
            selInfo.visibilityScore = selected[i].visibilityScore;
            selInfo.hexColor = selected[i].hexColor;
            selInfo.selected = true;
            selInfo.rgb[0] = finalColors[i].r;
            selInfo.rgb[1] = finalColors[i].g;
            selInfo.rgb[2] = finalColors[i].b;
            outDiag->selected.push_back(selInfo);
        }
    }

    // Harmonic derivation if fewer than 4 candidates found
    if (numSelected == 1)
    {
        OKLab ok = selected[0].cluster.CentroidOKLab();
        float baseL = std::clamp(ok.L, 0.25f, 0.75f);
        finalColors[1] = GamutSafeOKLabtoRGB(std::max(baseL - 0.18f, 0.14f), ok.a, ok.b);
        finalColors[2] = GamutSafeOKLabtoRGB(std::min(baseL + 0.18f, 0.88f), ok.a, ok.b);
        finalColors[3] = GamutSafeOKLabtoRGB(baseL * 0.70f, ok.a * 0.8f, ok.b * 0.8f);
    }
    else if (numSelected == 2)
    {
        OKLab ok0 = selected[0].cluster.CentroidOKLab();
        OKLab ok1 = selected[1].cluster.CentroidOKLab();
        finalColors[2] = GamutSafeOKLabtoRGB((ok0.L + ok1.L) * 0.5f, (ok0.a + ok1.a) * 0.5f, (ok0.b + ok1.b) * 0.5f);
        finalColors[3] = GamutSafeOKLabtoRGB(std::max(ok0.L - 0.20f, 0.14f), ok0.a, ok0.b);
    }
    else if (numSelected == 3)
    {
        OKLab ok0 = selected[0].cluster.CentroidOKLab();
        finalColors[3] = GamutSafeOKLabtoRGB(std::max(ok0.L - 0.20f, 0.14f), ok0.a, ok0.b);
    }

    // Semantic RimPalette mapping (Requirement F):
    //   hot    = Color 1: Primary dominant color (highest visibility score, e.g. Violet)
    //   color2 = Color 2: Secondary contrasting accent (e.g. Red)
    //   color3 = Color 3: Tertiary distinct accent / luminous highlight (e.g. Cyan)
    //   deep   = Color 4: Supporting / grounded foundation tone (e.g. Blue/Azure)
    result.hot[0] = finalColors[0].r;
    result.hot[1] = finalColors[0].g;
    result.hot[2] = finalColors[0].b;
    result.hot[3] = 1.0f;

    result.color2[0] = finalColors[1].r;
    result.color2[1] = finalColors[1].g;
    result.color2[2] = finalColors[1].b;
    result.color2[3] = 1.0f;

    // For slots 3 & 4: the lighter / higher-chroma tone becomes color3 (accent highlight),
    // and the deeper tone becomes deep (grounded foundation)
    if (numSelected == 4)
    {
        OKLab ok2 = selected[2].cluster.CentroidOKLab();
        OKLab ok3 = selected[3].cluster.CentroidOKLab();
        float score2 = ok2.L + ok2.a * 0.2f + std::sqrt(ok2.a * ok2.a + ok2.b * ok2.b);
        float score3 = ok3.L + ok3.a * 0.2f + std::sqrt(ok3.a * ok3.a + ok3.b * ok3.b);

        int accentIdx = (score3 >= score2) ? 3 : 2;
        int supportIdx = (score3 >= score2) ? 2 : 3;

        result.color3[0] = finalColors[accentIdx].r;
        result.color3[1] = finalColors[accentIdx].g;
        result.color3[2] = finalColors[accentIdx].b;
        result.color3[3] = 1.0f;

        result.deep[0] = finalColors[supportIdx].r;
        result.deep[1] = finalColors[supportIdx].g;
        result.deep[2] = finalColors[supportIdx].b;
        result.deep[3] = 1.0f;
    }
    else
    {
        result.color3[0] = finalColors[2].r;
        result.color3[1] = finalColors[2].g;
        result.color3[2] = finalColors[2].b;
        result.color3[3] = 1.0f;

        result.deep[0] = finalColors[3].r;
        result.deep[1] = finalColors[3].g;
        result.deep[2] = finalColors[3].b;
        result.deep[3] = 1.0f;
    }

    if (numSelected >= 2)
    {
        float n0 = static_cast<float>(selected[0].cluster.count);
        float n1 = static_cast<float>(selected[1].cluster.count);
        result.balance = std::clamp(n0 / (n0 + n1), 0.25f, 0.75f);
    }
    else
    {
        result.balance = 0.50f;
    }

    result.hasArtwork = true;
    return result;
}

} // namespace lumn::media
