#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>

#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")

using namespace winrt;
using namespace Windows::Media::Control;
using namespace Windows::Storage::Streams;

struct ColorRGB
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

struct ColorHSB
{
    float h = 0.0f; // [0, 1]
    float s = 0.0f; // [0, 1]
    float b = 0.0f; // [0, 1]
};

ColorHSB RGBtoHSB(float r, float g, float b)
{
    float maxV = std::max({ r, g, b });
    float minV = std::min({ r, g, b });
    float delta = maxV - minV;

    ColorHSB hsb;
    hsb.b = maxV;
    hsb.s = (maxV > 0.0001f) ? (delta / maxV) : 0.0f;

    if (delta < 0.0001f)
    {
        hsb.h = 0.0f;
    }
    else if (maxV == r)
    {
        hsb.h = std::fmod((g - b) / delta, 6.0f) / 6.0f;
        if (hsb.h < 0.0f) hsb.h += 1.0f;
    }
    else if (maxV == g)
    {
        hsb.h = ((b - r) / delta + 2.0f) / 6.0f;
    }
    else
    {
        hsb.h = ((r - g) / delta + 4.0f) / 6.0f;
    }
    return hsb;
}

ColorRGB HSBtoRGB(float h, float s, float b)
{
    float c = b * s;
    float hPrime = std::fmod(h * 6.0f, 6.0f);
    if (hPrime < 0.0f) hPrime += 6.0f;
    float x = c * (1.0f - std::abs(std::fmod(hPrime, 2.0f) - 1.0f));
    float m = b - c;

    float r1 = 0, g1 = 0, b1 = 0;
    if (hPrime >= 0.0f && hPrime < 1.0f) { r1 = c; g1 = x; b1 = 0; }
    else if (hPrime >= 1.0f && hPrime < 2.0f) { r1 = x; g1 = c; b1 = 0; }
    else if (hPrime >= 2.0f && hPrime < 3.0f) { r1 = 0; g1 = c; b1 = x; }
    else if (hPrime >= 3.0f && hPrime < 4.0f) { r1 = 0; g1 = x; b1 = c; }
    else if (hPrime >= 4.0f && hPrime < 5.0f) { r1 = x; g1 = 0; b1 = c; }
    else { r1 = c; g1 = 0; b1 = x; }

    return { r1 + m, g1 + m, b1 + m, 1.0f };
}

struct HueBin
{
    int binIndex = 0;
    float weightSum = 0.0f;
    float rgbSum[3] = { 0.0f, 0.0f, 0.0f };
    uint32_t pixelCount = 0;
};

int main()
{
    init_apartment();
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    try
    {
        auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        if (manager)
        {
            auto session = manager.GetCurrentSession();
            if (session)
            {
                auto props = session.TryGetMediaPropertiesAsync().get();
                if (props && props.Thumbnail())
                {
                    auto stream = props.Thumbnail().OpenReadAsync().get();
                    uint32_t size = static_cast<uint32_t>(stream.Size());

                    Buffer buffer(size);
                    stream.ReadAsync(buffer, size, InputStreamOptions::None).get();

                    IStream* pMemStream = SHCreateMemStream(buffer.data(), size);
                    if (pMemStream)
                    {
                        IWICImagingFactory* pFactory = nullptr;
                        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
                        if (pFactory)
                        {
                            IWICBitmapDecoder* pDecoder = nullptr;
                            pFactory->CreateDecoderFromStream(pMemStream, nullptr, WICDecodeMetadataCacheOnDemand, &pDecoder);
                            if (pDecoder)
                            {
                                IWICBitmapFrameDecode* pFrame = nullptr;
                                pDecoder->GetFrame(0, &pFrame);
                                if (pFrame)
                                {
                                    IWICBitmapScaler* pScaler = nullptr;
                                    pFactory->CreateBitmapScaler(&pScaler);
                                    if (pScaler)
                                    {
                                        pScaler->Initialize(pFrame, 48, 48, WICBitmapInterpolationModeFant);

                                        IWICFormatConverter* pConverter = nullptr;
                                        pFactory->CreateFormatConverter(&pConverter);
                                        if (pConverter)
                                        {
                                            pConverter->Initialize(pScaler, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
                                            std::vector<BYTE> pixels(48 * 48 * 4);
                                            pConverter->CopyPixels(nullptr, 48 * 4, static_cast<UINT>(pixels.size()), pixels.data());

                                            // Execute Lumn 1.1 Color Extraction Algorithm
                                            HueBin bins[12];
                                            for (int i = 0; i < 12; ++i) bins[i].binIndex = i;

                                            uint32_t totalOpaque = 0;
                                            uint32_t coloredPixels = 0;

                                            for (size_t i = 0; i < pixels.size(); i += 4)
                                            {
                                                BYTE rB = pixels[i + 0];
                                                BYTE gB = pixels[i + 1];
                                                BYTE bB = pixels[i + 2];
                                                BYTE aB = pixels[i + 3];

                                                if (aB < 128) continue;
                                                totalOpaque++;

                                                float r = rB / 255.0f;
                                                float g = gB / 255.0f;
                                                float b = bB / 255.0f;

                                                ColorHSB hsb = RGBtoHSB(r, g, b);
                                                if (hsb.s >= 0.25f && hsb.b >= 0.15f)
                                                {
                                                    coloredPixels++;
                                                    float weight = hsb.b * (hsb.s * hsb.s);
                                                    int binIdx = std::clamp(static_cast<int>(std::floor(hsb.h * 12.0f)), 0, 11);
                                                    bins[binIdx].weightSum += weight;
                                                    bins[binIdx].rgbSum[0] += r * weight;
                                                    bins[binIdx].rgbSum[1] += g * weight;
                                                    bins[binIdx].rgbSum[2] += b * weight;
                                                    bins[binIdx].pixelCount++;
                                                }
                                            }

                                            std::cout << "Total Opaque: " << totalOpaque << ", Colored: " << coloredPixels << std::endl;

                                            std::vector<HueBin> qualified;
                                            for (int i = 0; i < 12; ++i)
                                            {
                                                if (bins[i].weightSum > 0.05f && bins[i].pixelCount >= 1)
                                                {
                                                    qualified.push_back(bins[i]);
                                                }
                                            }

                                            std::sort(qualified.begin(), qualified.end(), [](const HueBin& a, const HueBin& b) {
                                                return a.pixelCount > b.pixelCount;
                                            });

                                            std::cout << "Qualified Bins Count: " << qualified.size() << std::endl;
                                            if (!qualified.empty())
                                            {
                                                auto& dom = qualified[0];
                                                float invW = 1.0f / dom.weightSum;
                                                ColorRGB domRGB = { dom.rgbSum[0] * invW, dom.rgbSum[1] * invW, dom.rgbSum[2] * invW, 1.0f };
                                                ColorHSB domHSB = RGBtoHSB(domRGB.r, domRGB.g, domRGB.b);

                                                ColorHSB hotHSB = { domHSB.h, std::max(domHSB.s, 0.60f), std::max(domHSB.b, 0.72f) };
                                                ColorRGB hotRGB = HSBtoRGB(hotHSB.h, hotHSB.s, hotHSB.b);

                                                std::cout << "Dominant Hue Bin: " << dom.binIndex << " (Count: " << dom.pixelCount << ")" << std::endl;
                                                std::cout << "HotColor RGB: (" << hotRGB.r << ", " << hotRGB.g << ", " << hotRGB.b << ")" << std::endl;

                                                // Companion search
                                                bool foundCompanion = false;
                                                ColorRGB deepRGB{};
                                                float balance = 0.50f;

                                                for (size_t i = 1; i < qualified.size(); ++i)
                                                {
                                                    int diff = std::abs(qualified[i].binIndex - dom.binIndex);
                                                    int hueDist = std::min(diff, 12 - diff);
                                                    if (hueDist >= 2)
                                                    {
                                                        auto& sec = qualified[i];
                                                        float invSecW = 1.0f / sec.weightSum;
                                                        ColorRGB secRGB = { sec.rgbSum[0] * invSecW, sec.rgbSum[1] * invSecW, sec.rgbSum[2] * invSecW, 1.0f };
                                                        ColorHSB secHSB = RGBtoHSB(secRGB.r, secRGB.g, secRGB.b);
                                                        ColorHSB deepHSB = { secHSB.h, std::max(secHSB.s, 0.60f), std::max(secHSB.b, 0.70f) };
                                                        deepRGB = HSBtoRGB(deepHSB.h, deepHSB.s, deepHSB.b);

                                                        balance = std::clamp(static_cast<float>(dom.pixelCount) / static_cast<float>(dom.pixelCount + sec.pixelCount), 0.15f, 0.85f);
                                                        foundCompanion = true;
                                                        std::cout << "Found Companion Hue Bin: " << sec.binIndex << " (HueDist: " << hueDist << ")" << std::endl;
                                                        break;
                                                    }
                                                }

                                                if (!foundCompanion)
                                                {
                                                    float deepB = (hotHSB.b >= 0.50f) ? std::max(hotHSB.b - 0.16f, 0.30f) : std::min(hotHSB.b + 0.16f, 0.95f);
                                                    ColorHSB deepHSB = { hotHSB.h, std::max(hotHSB.s, 0.60f), deepB };
                                                    deepRGB = HSBtoRGB(deepHSB.h, deepHSB.s, deepHSB.b);
                                                    balance = 0.50f;
                                                    std::cout << "Using Single-Hue Derived Companion." << std::endl;
                                                }

                                                std::cout << "DeepColor RGB: (" << deepRGB.r << ", " << deepRGB.g << ", " << deepRGB.b << ")" << std::endl;
                                                std::cout << "Balance: " << balance << std::endl;
                                            }

                                            pConverter->Release();
                                        }
                                        pScaler->Release();
                                    }
                                    pFrame->Release();
                                }
                                pDecoder->Release();
                            }
                            pFactory->Release();
                        }
                        pMemStream->Release();
                    }
                }
            }
        }
    }
    catch (const hresult_error& ex)
    {
        std::wcout << L"WinRT Error: " << ex.message().c_str() << std::endl;
    }
    CoUninitialize();
    uninit_apartment();
    return 0;
}
