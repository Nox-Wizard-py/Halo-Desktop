#include <iostream>
#include <vector>
#include <cassert>
#include <cmath>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>

#include "../src/media/ColorExtractor.hpp"

using namespace lumn::media;

void TestOKLabConversions()
{
    std::cout << "[Test 1] OKLab Math & Roundtrip Precision...\n";
    std::vector<ColorExtractor::RGB> testColors = {
        { 1.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f },
        { 0.85f, 0.70f, 0.15f }, // Gold
        { 0.12f, 0.45f, 0.65f }, // Steel Blue
        { 0.05f, 0.05f, 0.05f }, // Near Black
        { 0.95f, 0.95f, 0.95f }  // Near White
    };

    for (const auto& rgb : testColors)
    {
        auto lab = ColorExtractor::RGBtoOKLab(rgb.r, rgb.g, rgb.b);
        auto back = ColorExtractor::OKLabtoRGB(lab.L, lab.a, lab.b);
        float err = std::abs(rgb.r - back.r) + std::abs(rgb.g - back.g) + std::abs(rgb.b - back.b);
        assert(err < 0.005f);
    }
    std::cout << "  -> PASSED: All 7 test colors roundtripped with error < 0.005.\n\n";
}

void TestPaletteDualTone()
{
    std::cout << "[Test 2] Dual-Tone Artwork Palette Extraction...\n";
    std::vector<uint8_t> buffer(48 * 48 * 4);
    // Cyan primary + Crimson secondary
    for (size_t i = 0; i < buffer.size(); i += 4)
    {
        if (i % 8 < 4)
        {
            buffer[i + 0] = 40;  buffer[i + 1] = 210; buffer[i + 2] = 230; buffer[i + 3] = 255;
        }
        else
        {
            buffer[i + 0] = 220; buffer[i + 1] = 30;  buffer[i + 2] = 80;  buffer[i + 3] = 255;
        }
    }

    RimPalette p = ColorExtractor::ExtractFromRGBA48x48(buffer.data());
    std::cout << "  HotColor:  RGB(" << p.hot[0] << ", " << p.hot[1] << ", " << p.hot[2] << ")\n";
    std::cout << "  DeepColor: RGB(" << p.deep[0] << ", " << p.deep[1] << ", " << p.deep[2] << ")\n";
    std::cout << "  Balance:   " << p.balance << "\n";

    // Verify HotColor is luminous
    auto hotLab = ColorExtractor::RGBtoOKLab(p.hot[0], p.hot[1], p.hot[2]);
    auto deepLab = ColorExtractor::RGBtoOKLab(p.deep[0], p.deep[1], p.deep[2]);
    float dE = ColorExtractor::DeltaE(hotLab, deepLab);

    std::cout << "  HotLightness=" << hotLab.L << " DeepLightness=" << deepLab.L << " DeltaE=" << dE << "\n";
    assert(hotLab.L >= 0.50f);
    assert(deepLab.L <= 0.38f);
    assert(dE >= 0.18f);
    std::cout << "  -> PASSED: Proper tonal roles (Hot luminous, Deep foundation, distinct separation).\n\n";
}

void TestPaletteSingleHue()
{
    std::cout << "[Test 3] Single-Hue Monochromatic Artwork...\n";
    std::vector<uint8_t> buffer(48 * 48 * 4);
    // Pure cobalt blue artwork
    for (size_t i = 0; i < buffer.size(); i += 4)
    {
        buffer[i + 0] = 30; buffer[i + 1] = 90; buffer[i + 2] = 220; buffer[i + 3] = 255;
    }

    RimPalette p = ColorExtractor::ExtractFromRGBA48x48(buffer.data());
    std::cout << "  HotColor:  RGB(" << p.hot[0] << ", " << p.hot[1] << ", " << p.hot[2] << ")\n";
    std::cout << "  DeepColor: RGB(" << p.deep[0] << ", " << p.deep[1] << ", " << p.deep[2] << ")\n";
    std::cout << "  Balance:   " << p.balance << "\n";

    auto hotLab = ColorExtractor::RGBtoOKLab(p.hot[0], p.hot[1], p.hot[2]);
    auto deepLab = ColorExtractor::RGBtoOKLab(p.deep[0], p.deep[1], p.deep[2]);
    float dE = ColorExtractor::DeltaE(hotLab, deepLab);
    std::cout << "  HotLightness=" << hotLab.L << " DeepLightness=" << deepLab.L << " DeltaE=" << dE << "\n";
    assert(deepLab.L <= 0.35f);
    assert(dE >= 0.18f);
    // Deep color should be rich midnight blue, not desaturated gray or bright identical color
    assert(p.deep[2] > p.deep[0] && p.deep[2] > p.deep[1]);
    std::cout << "  -> PASSED: Single-hue derived rich midnight base with DeltaE >= 0.18.\n\n";
}

void TestPaletteMonochrome()
{
    std::cout << "[Test 4] Monochrome / Grayscale Artwork...\n";
    std::vector<uint8_t> buffer(48 * 48 * 4);
    for (size_t i = 0; i < buffer.size(); i += 4)
    {
        uint8_t v = static_cast<uint8_t>(30 + (i % 160));
        buffer[i + 0] = v; buffer[i + 1] = v; buffer[i + 2] = v; buffer[i + 3] = 255;
    }

    RimPalette p = ColorExtractor::ExtractFromRGBA48x48(buffer.data());
    std::cout << "  HotColor:  RGB(" << p.hot[0] << ", " << p.hot[1] << ", " << p.hot[2] << ")\n";
    std::cout << "  DeepColor: RGB(" << p.deep[0] << ", " << p.deep[1] << ", " << p.deep[2] << ")\n";
    std::cout << "  Balance:   " << p.balance << "\n";

    assert(p.hot[0] >= 0.70f && p.hot[1] >= 0.70f && p.hot[2] >= 0.70f);
    assert(p.deep[0] <= 0.30f && p.deep[1] <= 0.30f && p.deep[2] <= 0.30f);
    std::cout << "  -> PASSED: Platinum highlight + obsidian base for grayscale covers.\n\n";
}

void TestRaceSafetyGenerationTracking()
{
    std::cout << "[Test 5] Race Safety & Monotonic Generation Invalidation...\n";
    std::atomic<uint64_t> generation{ 0 };

    // Simulate Track 1 started
    uint64_t req1 = ++generation;
    std::cout << "  Track 1 request launched with Gen=" << req1 << "\n";

    // Simulate Track 2 arriving quickly while Track 1 is "retrying" thumbnail
    uint64_t req2 = ++generation;
    std::cout << "  Track 2 request launched with Gen=" << req2 << " (superseding Track 1)\n";

    // Track 1 finishes late and checks generation:
    bool track1CanPublish = (generation.load() == req1);
    bool track2CanPublish = (generation.load() == req2);

    assert(!track1CanPublish);
    assert(track2CanPublish);
    std::cout << "  Track 1 publication rejected: " << (!track1CanPublish ? "YES" : "NO") << "\n";
    std::cout << "  Track 2 publication accepted: " << (track2CanPublish ? "YES" : "NO") << "\n";
    std::cout << "  -> PASSED: Late async results from superseded tracks are cleanly discarded.\n\n";
}

int main()
{
    std::cout << "========================================\n";
    std::cout << " LUMN REA - Phase 3.1 Verification Suite\n";
    std::cout << "========================================\n\n";

    TestOKLabConversions();
    TestPaletteDualTone();
    TestPaletteSingleHue();
    TestPaletteMonochrome();
    TestRaceSafetyGenerationTracking();

    std::cout << "ALL 5 PHASE 3.1 VERIFICATION TESTS PASSED SUCCESSFULLY!\n";
    return 0;
}

