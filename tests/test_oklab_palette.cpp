#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <iomanip>

struct OKLab
{
    float L = 0.0f;
    float a = 0.0f;
    float b = 0.0f;
};

struct RGB
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

static inline float sRGBtoLinear(float c) noexcept
{
    return (c <= 0.04045f) ? (c / 12.92f) : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

static inline float linearTosRGB(float c) noexcept
{
    c = std::clamp(c, 0.0f, 1.0f);
    return (c <= 0.0031308f) ? (12.92f * c) : (1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f);
}

static OKLab RGBtoOKLab(float r, float g, float b) noexcept
{
    float lr = sRGBtoLinear(r);
    float lg = sRGBtoLinear(g);
    float lb = sRGBtoLinear(b);

    float l = 0.4122214708f * lr + 0.5363325363f * lg + 0.0514459929f * lb;
    float m = 0.2119034982f * lr + 0.6806995451f * lg + 0.1073969566f * lb;
    float s = 0.0883024619f * lr + 0.2817188376f * lg + 0.6299787005f * lb;

    float l_ = std::cbrt(l);
    float m_ = std::cbrt(m);
    float s_ = std::cbrt(s);

    return {
        0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_,
        1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_,
        0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_
    };
}

static RGB OKLabtoRGB(float L, float a, float b) noexcept
{
    float l_ = L + 0.3963377774f * a + 0.2158037573f * b;
    float m_ = L - 0.1055613458f * a - 0.0638541728f * b;
    float s_ = L - 0.0894841775f * a - 1.2914855480f * b;

    float l = l_ * l_ * l_;
    float m = m_ * m_ * m_;
    float s = s_ * s_ * s_;

    float lr = +4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
    float lg = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
    float lb = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;

    return {
        linearTosRGB(lr),
        linearTosRGB(lg),
        linearTosRGB(lb)
    };
}

static inline float DeltaE(const OKLab& c1, const OKLab& c2) noexcept
{
    float dL = c1.L - c2.L;
    float da = c1.a - c2.a;
    float db = c1.b - c2.b;
    return std::sqrt(dL * dL + da * da + db * db);
}

int main()
{
    std::cout << "Testing OKLab conversion precision..." << std::endl;
    RGB red{ 0.85f, 0.15f, 0.10f };
    OKLab oklabRed = RGBtoOKLab(red.r, red.g, red.b);
    RGB backRed = OKLabtoRGB(oklabRed.L, oklabRed.a, oklabRed.b);
    std::cout << "Red roundtrip: (" << backRed.r << ", " << backRed.g << ", " << backRed.b << ")\n";
    std::cout << "Red OKLab: L=" << oklabRed.L << " a=" << oklabRed.a << " b=" << oklabRed.b 
              << " Chroma=" << std::sqrt(oklabRed.a * oklabRed.a + oklabRed.b * oklabRed.b) << "\n";

    // Test derivation of rich deep midnight color from Red HotColor
    float L_deep = 0.22f;
    float C_hot = std::sqrt(oklabRed.a * oklabRed.a + oklabRed.b * oklabRed.b);
    float C_deep = std::min(C_hot * 0.85f, 0.12f);
    float a_deep = oklabRed.a * (C_deep / C_hot);
    float b_deep = oklabRed.b * (C_deep / C_hot);
    RGB deepRed = OKLabtoRGB(L_deep, a_deep, b_deep);
    std::cout << "Derived Deep Red: RGB(" << deepRed.r << ", " << deepRed.g << ", " << deepRed.b << ")\n";
    std::cout << "DeltaE(Hot, Deep): " << DeltaE(oklabRed, { L_deep, a_deep, b_deep }) << "\n";

    return 0;
}

