#include "ColorExtractor.hpp"
#include <cmath>
#include <algorithm>

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

} // namespace lumn::media
