#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <iostream>
#include <vector>

#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")

using namespace winrt;
using namespace Windows::Media::Control;
using namespace Windows::Storage::Streams;

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
                    std::cout << "Opening thumbnail stream..." << std::endl;
                    auto stream = props.Thumbnail().OpenReadAsync().get();
                    uint32_t size = static_cast<uint32_t>(stream.Size());
                    std::cout << "Thumbnail stream size: " << size << " bytes" << std::endl;

                    Buffer buffer(size);
                    stream.ReadAsync(buffer, size, InputStreamOptions::None).get();

                    // Create IStream from bytes
                    IStream* pMemStream = SHCreateMemStream(buffer.data(), size);
                    if (pMemStream)
                    {
                        IWICImagingFactory* pFactory = nullptr;
                        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
                        if (SUCCEEDED(hr))
                        {
                            IWICBitmapDecoder* pDecoder = nullptr;
                            hr = pFactory->CreateDecoderFromStream(pMemStream, nullptr, WICDecodeMetadataCacheOnDemand, &pDecoder);
                            if (SUCCEEDED(hr))
                            {
                                IWICBitmapFrameDecode* pFrame = nullptr;
                                hr = pDecoder->GetFrame(0, &pFrame);
                                if (SUCCEEDED(hr))
                                {
                                    UINT width = 0, height = 0;
                                    pFrame->GetSize(&width, &height);
                                    std::cout << "Original image dimensions: " << width << "x" << height << std::endl;

                                    // Scale to 48x48
                                    IWICBitmapScaler* pScaler = nullptr;
                                    hr = pFactory->CreateBitmapScaler(&pScaler);
                                    if (SUCCEEDED(hr))
                                    {
                                        pScaler->Initialize(pFrame, 48, 48, WICBitmapInterpolationModeFant);

                                        // Convert to RGBA
                                        IWICFormatConverter* pConverter = nullptr;
                                        hr = pFactory->CreateFormatConverter(&pConverter);
                                        if (SUCCEEDED(hr))
                                        {
                                            pConverter->Initialize(pScaler, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
                                            std::vector<BYTE> pixelBuffer(48 * 48 * 4);
                                            hr = pConverter->CopyPixels(nullptr, 48 * 4, static_cast<UINT>(pixelBuffer.size()), pixelBuffer.data());
                                            if (SUCCEEDED(hr))
                                            {
                                                std::cout << "Successfully decoded and downsampled 48x48 RGBA pixel buffer (" << pixelBuffer.size() << " bytes)!" << std::endl;
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

