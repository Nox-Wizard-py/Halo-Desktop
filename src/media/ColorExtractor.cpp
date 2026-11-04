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

struct PerceptualCluster
{
    int hueSector = 0;
    int lightnessTier = 0; // 0=Dark [0.08, 0.40), 1=Mid [0.40, 0.70), 2=Light [0.70, 0.94]
    float weightSum = 0.0f;
    float L_sum = 0.0f;
    float a_sum = 0.0f;
    float b_sum = 0.0f;
    float rgb_sum[3] = { 0.0f, 0.0f, 0.0f };
    uint32_t count = 0;
};

RimPalette ColorExtractor::ExtractFromRGBA48x48(const uint8_t* rgbaPixels)
{
    RimPalette result{};
    if (!rgbaPixels)
    {
        return result;
    }

    constexpr uint32_t W = DOWNSAMPLE_WIDTH;
    constexpr uint32_t H = DOWNSAMPLE_HEIGHT;

    // 16 hue sectors * 3 lightness tiers = 48 color bins + 1 neutral/monochrome bin
    PerceptualCluster bins[49];
    for (int i = 0; i < 49; ++i)
    {
        bins[i].hueSector = i % 16;
        bins[i].lightnessTier = (i < 48) ? (i / 16) : 3;
    }

    uint32_t totalOpaque = 0;
    uint32_t colorfulPixels = 0;
    float totalChroma = 0.0f;
    float neutralLSum = 0.0f;
    uint32_t neutralCount = 0;

    for (uint32_t y = 0; y < H; ++y)
    {
        for (uint32_t x = 0; x < W; ++x)
        {
            size_t idx = (y * W + x) * 4;
            uint8_t aByte = rgbaPixels[idx + 3];
            if (aByte < 128)
            {
                continue; // Alpha gate
            }
            totalOpaque++;

            float r = rgbaPixels[idx + 0] / 255.0f;
            float g = rgbaPixels[idx + 1] / 255.0f;
            float b = rgbaPixels[idx + 2] / 255.0f;

            OKLab lab = RGBtoOKLab(r, g, b);
            float chroma = std::sqrt(lab.a * lab.a + lab.b * lab.b);

            // Border dampening: down-weight outer 2 pixels by 0.35x to avoid letterboxing/frame bias
            float borderWeight = 1.0f;
            if (x < 2 || x >= W - 2 || y < 2 || y >= H - 2)
            {
                borderWeight = 0.35f;
            }

            // Extreme highlight (> 0.94) or near-black (< 0.06) down-weighting
            if (lab.L > 0.94f || lab.L < 0.06f)
            {
                borderWeight *= 0.15f;
            }

            totalChroma += chroma;

            // Neutral / Desaturated bin (Chroma < 0.035)
            if (chroma < 0.035f)
            {
                neutralCount++;
                neutralLSum += lab.L;
                bins[48].weightSum += 0.5f * borderWeight;
                bins[48].L_sum += lab.L * borderWeight;
                bins[48].a_sum += lab.a * borderWeight;
                bins[48].b_sum += lab.b * borderWeight;
                bins[48].rgb_sum[0] += r * borderWeight;
                bins[48].rgb_sum[1] += g * borderWeight;
                bins[48].rgb_sum[2] += b * borderWeight;
                bins[48].count++;
                continue;
            }

            colorfulPixels++;

            // Perceptual saliency weight:
            // Peaks at midtones sin(pi * L) and scales non-linearly with chroma C^1.2
            float midWeight = std::sin(3.14159265f * std::clamp(lab.L, 0.0f, 1.0f));
            float weight = std::pow(chroma, 1.2f) * midWeight * borderWeight;

            // Quantize hue angle in [0, 2pi) into 16 sectors (22.5 deg each)
            float hue = std::atan2(lab.b, lab.a);
            if (hue < 0.0f) hue += 6.2831853f;
            int hueSector = std::clamp(static_cast<int>(hue / (6.2831853f / 16.0f)), 0, 15);

            // Lightness tier: 0=Dark [0.08, 0.40), 1=Mid [0.40, 0.70), 2=Light [0.70, 0.94]
            int tier = 1;
            if (lab.L < 0.40f) tier = 0;
            else if (lab.L >= 0.70f) tier = 2;

            int binIdx = tier * 16 + hueSector;
            bins[binIdx].weightSum += weight;
            bins[binIdx].L_sum += lab.L * weight;
            bins[binIdx].a_sum += lab.a * weight;
            bins[binIdx].b_sum += lab.b * weight;
            bins[binIdx].rgb_sum[0] += r * weight;
            bins[binIdx].rgb_sum[1] += g * weight;
            bins[binIdx].rgb_sum[2] += b * weight;
            bins[binIdx].count++;
        }
    }

    if (totalOpaque == 0)
    {
        return result;
    }

    float avgChroma = totalChroma / static_cast<float>(totalOpaque);

    // 1. Monochrome / Low-saturation path (avgChroma < 0.032 or < 40 colorful pixels)
    if (avgChroma < 0.032f || colorfulPixels < 40)
    {
        float meanL = (neutralCount > 0) ? (neutralLSum / static_cast<float>(neutralCount)) : 0.5f;

        // Elegant platinum highlight with cool slate tone
        result.hot[0] = std::clamp(meanL + 0.35f, 0.75f, 0.95f);
        result.hot[1] = std::clamp(meanL + 0.37f, 0.78f, 0.97f);
        result.hot[2] = std::clamp(meanL + 0.42f, 0.82f, 1.00f);
        result.hot[3] = 1.0f;

        // Deep obsidian foundation
        result.deep[0] = std::clamp(meanL - 0.30f, 0.08f, 0.22f);
        result.deep[1] = std::clamp(meanL - 0.28f, 0.10f, 0.25f);
        result.deep[2] = std::clamp(meanL - 0.24f, 0.14f, 0.30f);
        result.deep[3] = 1.0f;

        result.balance = 0.50f;
        result.hasArtwork = true;
        return result;
    }

    // Collect qualifying chromatic candidate clusters
    struct Candidate
    {
        OKLab lab{};
        RGB rgb{};
        float weight = 0.0f;
        uint32_t count = 0;
        int tier = 0;
    };

    std::vector<Candidate> candidates;
    for (int i = 0; i < 48; ++i)
    {
        if (bins[i].weightSum > 0.005f && bins[i].count >= 6)
        {
            float invW = 1.0f / bins[i].weightSum;
            Candidate c;
            c.lab = { bins[i].L_sum * invW, bins[i].a_sum * invW, bins[i].b_sum * invW };
            c.rgb = { bins[i].rgb_sum[0] * invW, bins[i].rgb_sum[1] * invW, bins[i].rgb_sum[2] * invW };
            c.weight = bins[i].weightSum;
            c.count = bins[i].count;
            c.tier = bins[i].lightnessTier;
            candidates.push_back(c);
        }
    }

    if (candidates.empty())
    {
        result.hasArtwork = true;
        return result;
    }

    // Sort descending by accumulated saliency weight
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.weight > b.weight;
    });

    // 2. Select HotColor: Luminous, vivid accent (optimal L ~ 0.65, high chroma)
    int bestHotIdx = 0;
    float bestHotScore = -1e9f;
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        const auto& c = candidates[i];
        float chroma = std::sqrt(c.lab.a * c.lab.a + c.lab.b * c.lab.b);
        // Reward high chroma, penalize deviation from ideal highlight lightness (0.65)
        float score = c.weight * (chroma + 0.05f) * (1.0f - 1.6f * std::abs(c.lab.L - 0.65f));
        if (score > bestHotScore)
        {
            bestHotScore = score;
            bestHotIdx = static_cast<int>(i);
        }
    }

    Candidate hotCand = candidates[bestHotIdx];
    OKLab hotLab = hotCand.lab;
    // Guarantee hot accent is adequately luminous for the plasma rim
    if (hotLab.L < 0.55f)
    {
        hotLab.L = 0.58f;
    }
    RGB hotRGB = OKLabtoRGB(hotLab.L, hotLab.a, hotLab.b);

    result.hot[0] = hotRGB.r;
    result.hot[1] = hotRGB.g;
    result.hot[2] = hotRGB.b;
    result.hot[3] = 1.0f;

    // 3. Select DeepColor: Rich dark foundation, L in [0.18, 0.35], DeltaE >= 0.18 from HotColor
    int bestDeepIdx = -1;
    float bestDeepScore = -1e9f;
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        if (static_cast<int>(i) == bestHotIdx) continue;
        const auto& c = candidates[i];
        float dE = DeltaE(hotLab, c.lab);
        if (dE < 0.18f) continue; // Prevent choosing two colors that are too similar

        float chroma = std::sqrt(c.lab.a * c.lab.a + c.lab.b * c.lab.b);
        // Reward darker candidates with distinct separation and good chroma
        float score = c.weight * (1.0f + dE * 1.5f) * (chroma + 0.04f) * (1.0f - 2.0f * std::abs(c.lab.L - 0.25f));
        if (score > bestDeepScore)
        {
            bestDeepScore = score;
            bestDeepIdx = static_cast<int>(i);
        }
    }

    RGB deepRGB;
    float deepWeight = 0.0f;
    if (bestDeepIdx >= 0)
    {
        // Distinct companion cluster found
        OKLab deepLab = candidates[bestDeepIdx].lab;
        deepWeight = candidates[bestDeepIdx].weight;
        // Restrict foundation lightness strictly to [0.18, 0.35]
        deepLab.L = std::clamp(deepLab.L, 0.18f, 0.35f);
        deepRGB = OKLabtoRGB(deepLab.L, deepLab.a, deepLab.b);
    }
    else
    {
        // Monochromatic / Single-Hue artwork:
        // Harmonically derive rich midnight shade sharing HotColor's hue angle
        float hotChroma = std::sqrt(hotLab.a * hotLab.a + hotLab.b * hotLab.b);
        float deepL = 0.22f; // Midnight foundation
        float deepChroma = (hotChroma > 0.001f) ? std::min(hotChroma * 0.85f, 0.13f) : 0.0f;
        float deepA = (hotChroma > 0.001f) ? (hotLab.a * (deepChroma / hotChroma)) : 0.0f;
        float deepB = (hotChroma > 0.001f) ? (hotLab.b * (deepChroma / hotChroma)) : 0.0f;
        deepRGB = OKLabtoRGB(deepL, deepA, deepB);
        deepWeight = hotCand.weight * 0.5f;
    }

    result.deep[0] = deepRGB.r;
    result.deep[1] = deepRGB.g;
    result.deep[2] = deepRGB.b;
    result.deep[3] = 1.0f;

    // 4. Dynamic Balance ratio in [0.35, 0.65]
    result.balance = std::clamp(
        0.40f + 0.20f * (hotCand.weight / (hotCand.weight + deepWeight + 1e-4f)),
        0.35f,
        0.65f
    );
    result.hasArtwork = true;

    return result;
}

} // namespace lumn::media
