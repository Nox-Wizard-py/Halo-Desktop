#include "MediaCardRenderer.hpp"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <d3dcompiler.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace lumn::ui
{

static std::wstring FormatTime(int64_t time100ns)
{
    if (time100ns <= 0) return L"0:00";
    int64_t totalSeconds = time100ns / 10000000;
    int64_t minutes = totalSeconds / 60;
    int64_t seconds = totalSeconds % 60;
    std::wstringstream ss;
    ss << minutes << L":" << std::setfill(L'0') << std::setw(2) << seconds;
    return ss.str();
}

MediaCardRenderer::MediaCardRenderer() = default;

MediaCardRenderer::~MediaCardRenderer()
{
    Shutdown();
}

bool MediaCardRenderer::Initialize(HWND hWnd)
{
    m_hWnd = hWnd;
    if (!CreateDeviceAndSwapChain(hWnd))
    {
        return false;
    }
    if (!CreateDirectComposition(hWnd))
    {
        return false;
    }
    if (!CreateDirect2D())
    {
        return false;
    }
    if (!CreateTextFormats())
    {
        return false;
    }

    // Phase 4B.1: Initialize Liquid Glass Backdrop Sampler and Shaders
    // If backdrop sampling or shaders fail, m_useLiquidGlass falls back to Phase 4B procedural glass
    UINT dpi = GetDpiForWindow(hWnd);
    m_dpiScale = (dpi > 0) ? (static_cast<float>(dpi) / 96.0f) : 1.0f;
    uint32_t margin = static_cast<uint32_t>(std::round(24.0f * CARD_SCALE * m_dpiScale));
    m_backdropSampler.Initialize(m_d3dDevice.Get(), hWnd, CARD_WIDTH, CARD_HEIGHT, margin);
    CreateLiquidGlassResources();

    return true;
}

void MediaCardRenderer::Shutdown()
{
    m_liquidVS.Reset();
    m_liquidPS.Reset();
    m_liquidConstantBuffer.Reset();
    m_liquidSampler.Reset();
    m_renderTargetView.Reset();
    m_backdropSampler.Shutdown();

    m_artworkBitmap.Reset();
    m_cardBgBrush.Reset();
    m_cardBorderBrush.Reset();
    m_textWhiteBrush.Reset();
    m_textSecondaryBrush.Reset();
    m_textMutedBrush.Reset();
    m_progressBgBrush.Reset();
    m_progressFillBrush.Reset();
    m_btnHoverBrush.Reset();
    m_btnPressedBrush.Reset();
    m_btnIconBrush.Reset();
    m_btnDisabledBrush.Reset();

    m_glassBaseBrush.Reset();
    m_glassSheenBrush.Reset();
    m_glassBevelBrush.Reset();
    m_causticBrush.Reset();
    m_artworkGlowBrush.Reset();
    m_progressGradientBrush.Reset();
    m_cardShadowBrush.Reset();
    m_outerBorderTintBrush.Reset();

    m_titleFormat.Reset();
    m_artistFormat.Reset();
    m_timeFormat.Reset();
    m_dwriteFactory.Reset();

    m_d2dTargetBitmap.Reset();
    m_d2dContext.Reset();
    m_d2dDevice.Reset();
    m_d2dFactory.Reset();

    m_dcompVisual.Reset();
    m_dcompTarget.Reset();
    m_dcompDevice.Reset();

    m_swapChain.Reset();
    m_d3dContext.Reset();
    m_d3dDevice.Reset();
}

bool MediaCardRenderer::CreateDeviceAndSwapChain([[maybe_unused]] HWND hWnd)
{
    UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        creationFlags,
        featureLevels,
        static_cast<UINT>(std::size(featureLevels)),
        D3D11_SDK_VERSION,
        &m_d3dDevice,
        &featureLevel,
        &m_d3dContext
    );

#if defined(_DEBUG)
    if (FAILED(hr))
    {
        creationFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            creationFlags,
            featureLevels,
            static_cast<UINT>(std::size(featureLevels)),
            D3D11_SDK_VERSION,
            &m_d3dDevice,
            &featureLevel,
            &m_d3dContext
        );
    }
#endif

    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    hr = m_d3dDevice.As(&dxgiDevice);
    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<IDXGIAdapter> dxgiAdapter;
    hr = dxgiDevice->GetAdapter(&dxgiAdapter);
    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<IDXGIFactory2> dxgiFactory;
    hr = dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory));
    if (FAILED(hr)) return false;

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = CARD_WIDTH;
    desc.Height = CARD_HEIGHT;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.Stereo = FALSE;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    desc.Flags = 0;

    hr = dxgiFactory->CreateSwapChainForComposition(
        m_d3dDevice.Get(),
        &desc,
        nullptr,
        &m_swapChain
    );
    return SUCCEEDED(hr);
}

bool MediaCardRenderer::CreateDirectComposition(HWND hWnd)
{
    HRESULT hr = DCompositionCreateDevice(
        nullptr,
        IID_PPV_ARGS(&m_dcompDevice)
    );
    if (FAILED(hr)) return false;

    hr = m_dcompDevice->CreateTargetForHwnd(hWnd, TRUE, &m_dcompTarget);
    if (FAILED(hr)) return false;

    hr = m_dcompDevice->CreateVisual(&m_dcompVisual);
    if (FAILED(hr)) return false;

    hr = m_dcompVisual->SetContent(m_swapChain.Get());
    if (FAILED(hr)) return false;

    hr = m_dcompTarget->SetRoot(m_dcompVisual.Get());
    if (FAILED(hr)) return false;

    hr = m_dcompDevice->Commit();
    return SUCCEEDED(hr);
}

bool MediaCardRenderer::CreateDirect2D()
{
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = m_d3dDevice.As(&dxgiDevice);
    if (FAILED(hr)) return false;

    D2D1_FACTORY_OPTIONS options = {};
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options, (void**)&m_d2dFactory);
    if (FAILED(hr)) return false;

    hr = m_d2dFactory->CreateDevice(dxgiDevice.Get(), &m_d2dDevice);
    if (FAILED(hr)) return false;

    hr = m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_d2dContext);
    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<IDXGISurface> dxgiBackBuffer;
    hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&dxgiBackBuffer));
    if (FAILED(hr)) return false;

    D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );

    hr = m_d2dContext->CreateBitmapFromDxgiSurface(dxgiBackBuffer.Get(), &bitmapProperties, &m_d2dTargetBitmap);
    if (FAILED(hr)) return false;

    m_d2dContext->SetTarget(m_d2dTargetBitmap.Get());

    // Initialize base UI brushes
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.08f, 0.09f, 0.12f, 0.70f), &m_cardBgBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.10f), &m_cardBorderBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.98f, 0.98f, 1.00f, 1.0f), &m_textWhiteBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.78f, 0.81f, 0.88f, 0.90f), &m_textSecondaryBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.62f, 0.65f, 0.74f, 0.85f), &m_textMutedBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.12f), &m_progressBgBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.85f, 1.00f, 0.95f), &m_progressFillBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.14f), &m_btnHoverBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.25f), &m_btnPressedBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.92f), &m_btnIconBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.25f), &m_btnDisabledBrush);

    // 1. Smoked Glass Base Brush (Vertical: translucent dark substrate)
    D2D1_GRADIENT_STOP baseStops[2];
    baseStops[0].position = 0.0f;
    baseStops[0].color = D2D1::ColorF(0.08f, 0.09f, 0.13f, 0.68f);
    baseStops[1].position = 1.0f;
    baseStops[1].color = D2D1::ColorF(0.04f, 0.05f, 0.08f, 0.82f);
    Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> baseStopColl;
    hr = m_d2dContext->CreateGradientStopCollection(baseStops, 2, &baseStopColl);
    if (FAILED(hr)) return false;

    hr = m_d2dContext->CreateLinearGradientBrush(
        D2D1::LinearGradientBrushProperties(D2D1::Point2F(0.0f, 0.0f), D2D1::Point2F(0.0f, static_cast<float>(BASE_CARD_HEIGHT))),
        baseStopColl.Get(),
        &m_glassBaseBrush
    );
    if (FAILED(hr)) return false;

    // 2. Specular Sheen Brush (Top-to-bottom overhead light reflection)
    D2D1_GRADIENT_STOP sheenStops[3];
    sheenStops[0].position = 0.0f;
    sheenStops[0].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.15f);
    sheenStops[1].position = 0.35f;
    sheenStops[1].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.03f);
    sheenStops[2].position = 1.0f;
    sheenStops[2].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.00f);
    Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> sheenStopColl;
    hr = m_d2dContext->CreateGradientStopCollection(sheenStops, 3, &sheenStopColl);
    if (FAILED(hr)) return false;

    hr = m_d2dContext->CreateLinearGradientBrush(
        D2D1::LinearGradientBrushProperties(D2D1::Point2F(0.0f, 0.0f), D2D1::Point2F(0.0f, static_cast<float>(BASE_CARD_HEIGHT))),
        sheenStopColl.Get(),
        &m_glassSheenBrush
    );
    if (FAILED(hr)) return false;

    // 3. 3D Inner Bevel Highlight & Shadow (Bright top rim, dark bottom rim)
    D2D1_GRADIENT_STOP bevelStops[4];
    bevelStops[0].position = 0.0f;
    bevelStops[0].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.40f);
    bevelStops[1].position = 0.25f;
    bevelStops[1].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.14f);
    bevelStops[2].position = 0.70f;
    bevelStops[2].color = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.08f);
    bevelStops[3].position = 1.0f;
    bevelStops[3].color = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.45f);
    Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> bevelStopColl;
    hr = m_d2dContext->CreateGradientStopCollection(bevelStops, 4, &bevelStopColl);
    if (FAILED(hr)) return false;

    hr = m_d2dContext->CreateLinearGradientBrush(
        D2D1::LinearGradientBrushProperties(D2D1::Point2F(0.0f, 0.0f), D2D1::Point2F(0.0f, static_cast<float>(BASE_CARD_HEIGHT))),
        bevelStopColl.Get(),
        &m_glassBevelBrush
    );
    if (FAILED(hr)) return false;

    // 4. Soft Shadow & Caustic Brushes
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.22f), &m_cardShadowBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.07f), &m_causticBrush);

    return true;
}

bool MediaCardRenderer::CreateTextFormats()
{
    HRESULT hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        (IUnknown**)&m_dwriteFactory
    );
    if (FAILED(hr)) return false;

    // Title: 13.5pt Segoe UI Variable / Segoe UI Semi-Bold
    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        14.0f,
        L"en-US",
        &m_titleFormat
    );
    if (FAILED(hr)) return false;
    m_titleFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    // Artist: 11.0pt Segoe UI Regular
    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        11.5f,
        L"en-US",
        &m_artistFormat
    );
    if (FAILED(hr)) return false;
    m_artistFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    // Time: 9.5pt Segoe UI Regular
    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        9.5f,
        L"en-US",
        &m_timeFormat
    );
    if (FAILED(hr)) return false;
    m_timeFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    return true;
}

bool MediaCardRenderer::CreateLiquidGlassResources()
{
    if (!m_d3dDevice || !m_swapChain)
    {
        return false;
    }

    // 1. Create Render Target View for the SwapChain backbuffer
    Microsoft::WRL::ComPtr<IDXGISurface> dxgiBackBuffer;
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&dxgiBackBuffer));
    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBufferTex;
    hr = dxgiBackBuffer.As(&backBufferTex);
    if (FAILED(hr)) return false;

    hr = m_d3dDevice->CreateRenderTargetView(backBufferTex.Get(), nullptr, &m_renderTargetView);
    if (FAILED(hr)) return false;

    // 2. Compile and create Liquid Glass Shaders
    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    compileFlags |= D3DCOMPILE_DEBUG;
#endif

    const std::wstring shaderPaths[] = {
        L"liquidGlass.hlsl",
        L"src/render/shaders/liquidGlass.hlsl",
        L"../src/render/shaders/liquidGlass.hlsl"
    };

    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob, psBlob, errorBlob;
    bool compiled = false;

    for (const auto& path : shaderPaths)
    {
        errorBlob.Reset();
        hr = D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                "VSMain", "vs_5_0", compileFlags, 0, &vsBlob, &errorBlob);
        if (SUCCEEDED(hr))
        {
            hr = D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                    "PSMain", "ps_5_0", compileFlags, 0, &psBlob, &errorBlob);
            if (SUCCEEDED(hr))
            {
                compiled = true;
                break;
            }
        }
    }

    if (!compiled || !vsBlob || !psBlob)
    {
        std::cerr << "[MediaCardRenderer] Warning: Could not compile liquidGlass.hlsl. Falling back to Phase 4B procedural glass.\n";
        m_useLiquidGlass = false;
        return false;
    }

    hr = m_d3dDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_liquidVS);
    if (FAILED(hr)) return false;

    hr = m_d3dDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_liquidPS);
    if (FAILED(hr)) return false;

    // 3. Create Constant Buffer
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(LiquidGlassConstants);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = m_d3dDevice->CreateBuffer(&cbDesc, nullptr, &m_liquidConstantBuffer);
    if (FAILED(hr)) return false;

    // 4. Create Sampler State (Linear Clamp for clean optical backdrop distortion)
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

    hr = m_d3dDevice->CreateSamplerState(&sampDesc, &m_liquidSampler);
    return SUCCEEDED(hr);
}

void MediaCardRenderer::RenderLiquidGlassPass([[maybe_unused]] const media::MediaTrackInfo& trackInfo, [[maybe_unused]] double currentTimeSec)
{
    if (!m_d3dContext || !m_liquidPS || !m_renderTargetView || !m_liquidConstantBuffer || !m_backdropSampler.IsValid())
    {
        return;
    }

    // 1. Capture localized desktop backdrop directly behind the card (including margin)
    m_backdropSampler.Capture(m_hWnd, m_d3dContext.Get());

    // 2. Clear render target view to transparent
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_d3dContext->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), nullptr);
    m_d3dContext->ClearRenderTargetView(m_renderTargetView.Get(), clearColor);

    D3D11_VIEWPORT vp = { 0.0f, 0.0f, static_cast<float>(CARD_WIDTH), static_cast<float>(CARD_HEIGHT), 0.0f, 1.0f };
    m_d3dContext->RSSetViewports(1, &vp);

    // 3. Update constant buffer with Kyant0 liquid glass parameters (adapted for DPI)
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(m_d3dContext->Map(m_liquidConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        auto* cb = static_cast<LiquidGlassConstants*>(mapped.pData);
        float marginVal = static_cast<float>(m_backdropSampler.GetMargin());

        cb->cardSize[0] = static_cast<float>(CARD_WIDTH);
        cb->cardSize[1] = static_cast<float>(CARD_HEIGHT);
        cb->margin[0] = marginVal;
        cb->margin[1] = marginVal;

        cb->cornerRadii[0] = 18.0f * CARD_SCALE;
        cb->cornerRadii[1] = 18.0f * CARD_SCALE;
        cb->cornerRadii[2] = 18.0f * CARD_SCALE;
        cb->cornerRadii[3] = 18.0f * CARD_SCALE;

        // Approved physical optics parameters (scaled proportionally for 2.3x card)
        cb->refractionHeight = 22.0f * CARD_SCALE;
        cb->refractionAmount = -22.0f * CARD_SCALE;
        cb->depthEffect = 0.50f;
        cb->chromaticAberration = 0.40f;
        cb->highlightAngle = 0.785398f; // 45 deg in radians
        cb->highlightFalloff = 1.50f;
        cb->vibrancy = 2.50f;
        cb->specularStrength = 0.50f;

        cb->highlightColor[0] = 1.0f;
        cb->highlightColor[1] = 1.0f;
        cb->highlightColor[2] = 1.0f;
        cb->highlightColor[3] = 0.50f;

        cb->tintColor[0] = 0.06f;
        cb->tintColor[1] = 0.06f;
        cb->tintColor[2] = 0.10f;
        cb->tintColor[3] = 0.14f;

        for (int i = 0; i < 4; ++i)
        {
            cb->hotColor[i] = m_smoothedHot[i];
            cb->deepColor[i] = m_smoothedDeep[i];
        }

        // Frosted glass parameters: soft diffusion radius and milky translucent scatter
        cb->frostAmount = 4.5f * CARD_SCALE;
        cb->frostOpacity = 0.14f;
        cb->padding[0] = 0.0f;
        cb->padding[1] = 0.0f;

        m_d3dContext->Unmap(m_liquidConstantBuffer.Get(), 0);
    }

    // 4. Draw Full-Card Triangle Quad
    m_d3dContext->VSSetShader(m_liquidVS.Get(), nullptr, 0);
    m_d3dContext->PSSetShader(m_liquidPS.Get(), nullptr, 0);
    m_d3dContext->PSSetConstantBuffers(0, 1, m_liquidConstantBuffer.GetAddressOf());

    ID3D11ShaderResourceView* srv = m_backdropSampler.GetShaderResourceView();
    m_d3dContext->PSSetShaderResources(0, 1, &srv);
    m_d3dContext->PSSetSamplers(0, 1, m_liquidSampler.GetAddressOf());

    m_d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_d3dContext->Draw(3, 0);

    // Unbind SRV and RTV to allow clean handoff to Direct2D
    ID3D11ShaderResourceView* nullSRV = nullptr;
    m_d3dContext->PSSetShaderResources(0, 1, &nullSRV);
    ID3D11RenderTargetView* nullRTV = nullptr;
    m_d3dContext->OMSetRenderTargets(1, &nullRTV, nullptr);
}

void MediaCardRenderer::UpdateArtworkBitmap(const media::MediaTrackInfo& trackInfo)
{
    if (trackInfo.generation == m_artworkGeneration)
    {
        return;
    }

    m_artworkGeneration = trackInfo.generation;
    m_artworkBitmap.Reset();

    if (!trackInfo.hasArtwork || trackInfo.cardArtworkRGBA.empty() ||
        trackInfo.cardArtworkWidth == 0 || trackInfo.cardArtworkHeight == 0)
    {
        return;
    }

    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );

    D2D1_SIZE_U size = D2D1::SizeU(trackInfo.cardArtworkWidth, trackInfo.cardArtworkHeight);
    UINT32 pitch = trackInfo.cardArtworkWidth * 4;

    m_d2dContext->CreateBitmap(
        size,
        trackInfo.cardArtworkRGBA.data(),
        pitch,
        &props,
        &m_artworkBitmap
    );
}

void MediaCardRenderer::UpdateDynamicPaletteBrushes(const media::MediaTrackInfo& trackInfo)
{
    if (!m_paletteInitialized)
    {
        for (int i = 0; i < 4; ++i)
        {
            m_smoothedHot[i] = trackInfo.palette.hot[i];
            m_smoothedDeep[i] = trackInfo.palette.deep[i];
        }
        m_paletteInitialized = true;
    }
    else
    {
        // Smooth exponential moving average across frames (~60Hz tick)
        constexpr float alpha = 0.06f;
        for (int i = 0; i < 4; ++i)
        {
            m_smoothedHot[i] += (trackInfo.palette.hot[i] - m_smoothedHot[i]) * alpha;
            m_smoothedDeep[i] += (trackInfo.palette.deep[i] - m_smoothedDeep[i]) * alpha;
        }
    }

    // Dynamic Artwork Ambient Radial Glow Brush
    D2D1_GRADIENT_STOP glowStops[3];
    glowStops[0].position = 0.0f;
    glowStops[0].color = D2D1::ColorF(m_smoothedHot[0], m_smoothedHot[1], m_smoothedHot[2], 0.22f);
    glowStops[1].position = 0.45f;
    glowStops[1].color = D2D1::ColorF(m_smoothedDeep[0], m_smoothedDeep[1], m_smoothedDeep[2], 0.12f);
    glowStops[2].position = 1.0f;
    glowStops[2].color = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f);

    Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> glowStopColl;
    if (SUCCEEDED(m_d2dContext->CreateGradientStopCollection(glowStops, 3, &glowStopColl)))
    {
        m_d2dContext->CreateRadialGradientBrush(
            D2D1::RadialGradientBrushProperties(D2D1::Point2F(70.0f, 70.0f), D2D1::Point2F(0.0f, 0.0f), 240.0f, 140.0f),
            glowStopColl.Get(),
            &m_artworkGlowBrush
        );
    }

    // Dynamic Progress Fill Gradient Brush (Deep to Hot)
    D2D1_GRADIENT_STOP progStops[2];
    progStops[0].position = 0.0f;
    progStops[0].color = D2D1::ColorF(m_smoothedDeep[0], m_smoothedDeep[1], m_smoothedDeep[2], 0.95f);
    progStops[1].position = 1.0f;
    progStops[1].color = D2D1::ColorF(m_smoothedHot[0], m_smoothedHot[1], m_smoothedHot[2], 0.98f);

    Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> progStopColl;
    if (SUCCEEDED(m_d2dContext->CreateGradientStopCollection(progStops, 2, &progStopColl)))
    {
        D2D1_RECT_F pRect = GetProgressBarRect();
        m_d2dContext->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(pRect.left, 0.0f), D2D1::Point2F(pRect.right, 0.0f)),
            progStopColl.Get(),
            &m_progressGradientBrush
        );
    }

    // Dynamic Outer Border Accent Tint Brush
    m_d2dContext->CreateSolidColorBrush(
        D2D1::ColorF(m_smoothedHot[0], m_smoothedHot[1], m_smoothedHot[2], 0.24f),
        &m_outerBorderTintBrush
    );
}

void MediaCardRenderer::DrawPlaceholderArtwork(const D2D1_RECT_F& rect)
{
    // Background placeholder rounded rect
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush;
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.14f, 0.16f, 0.22f, 1.0f), &bgBrush);
    D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(rect, 12.0f, 12.0f);
    m_d2dContext->FillRoundedRectangle(rrect, bgBrush.Get());

    // Draw stylized musical disc / note glyph
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> glyphBrush;
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.25f), &glyphBrush);

    float cx = (rect.left + rect.right) * 0.5f;
    float cy = (rect.top + rect.bottom) * 0.5f;

    // Disc ring
    m_d2dContext->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 24.0f, 24.0f), glyphBrush.Get(), 2.5f);
    // Center spindle hole
    m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 6.0f, 6.0f), glyphBrush.Get());
}

void MediaCardRenderer::DrawVectorControls(const media::MediaTrackInfo& trackInfo,
                                          CardInteractiveElement hoveredElement,
                                          CardInteractiveElement pressedElement,
                                          bool effectiveIsPlaying)
{
    // 1. Previous Button
    D2D1_RECT_F prevRect = GetPreviousButtonRect();
    float prevCx = (prevRect.left + prevRect.right) * 0.5f;
    float prevCy = (prevRect.top + prevRect.bottom) * 0.5f;

    bool prevHover = (hoveredElement == CardInteractiveElement::PreviousButton);
    bool prevPressed = (pressedElement == CardInteractiveElement::PreviousButton);

    if (prevPressed)
    {
        m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(prevCx, prevCy), 15.0f, 15.0f), m_btnPressedBrush.Get());
    }
    else if (prevHover)
    {
        m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(prevCx, prevCy), 15.0f, 15.0f), m_btnHoverBrush.Get());
    }

    auto prevBrush = trackInfo.canPrevious ? m_btnIconBrush.Get() : m_btnDisabledBrush.Get();
    // Draw |<<
    // Vertical bar
    m_d2dContext->DrawLine(D2D1::Point2F(prevCx - 7.0f, prevCy - 6.0f), D2D1::Point2F(prevCx - 7.0f, prevCy + 6.0f), prevBrush, 2.0f);
    // Double left-pointing triangle
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> prevGeo;
    m_d2dFactory->CreatePathGeometry(&prevGeo);
    if (prevGeo)
    {
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        prevGeo->Open(&sink);
        sink->BeginFigure(D2D1::Point2F(prevCx + 7.0f, prevCy - 6.0f), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1::Point2F(prevCx, prevCy));
        sink->AddLine(D2D1::Point2F(prevCx + 7.0f, prevCy + 6.0f));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);

        sink->BeginFigure(D2D1::Point2F(prevCx, prevCy - 6.0f), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1::Point2F(prevCx - 6.0f, prevCy));
        sink->AddLine(D2D1::Point2F(prevCx, prevCy + 6.0f));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();

        m_d2dContext->FillGeometry(prevGeo.Get(), prevBrush);
    }

    // 2. Play / Pause Button
    D2D1_RECT_F playRect = GetPlayPauseButtonRect();
    float playCx = (playRect.left + playRect.right) * 0.5f;
    float playCy = (playRect.top + playRect.bottom) * 0.5f;

    bool playHover = (hoveredElement == CardInteractiveElement::PlayPauseButton);
    bool playPressed = (pressedElement == CardInteractiveElement::PlayPauseButton);

    // Play/Pause circular background button
    if (playPressed)
    {
        m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(playCx, playCy), 19.0f, 19.0f), m_btnPressedBrush.Get());
    }
    else if (playHover)
    {
        m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(playCx, playCy), 19.0f, 19.0f), m_btnHoverBrush.Get());
    }
    else
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> subtleBg;
        m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.08f), &subtleBg);
        m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(playCx, playCy), 18.0f, 18.0f), subtleBg.Get());
    }

    // Frosted glass rim bevel around Play/Pause button
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> btnRimBrush;
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, playHover ? 0.35f : 0.18f), &btnRimBrush);
    m_d2dContext->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(playCx, playCy), 18.0f, 18.0f), btnRimBrush.Get(), 1.0f);

    bool canPlayPause = effectiveIsPlaying ? trackInfo.canPause : trackInfo.canPlay;
    if (!trackInfo.canPlay && !trackInfo.canPause)
    {
        canPlayPause = true;
    }
    auto playBrush = canPlayPause ? m_btnIconBrush.Get() : m_btnDisabledBrush.Get();

    if (effectiveIsPlaying)
    {
        // Pause icon: Two vertical rounded bars
        D2D1_RECT_F bar1 = D2D1::RectF(playCx - 5.0f, playCy - 7.0f, playCx - 2.0f, playCy + 7.0f);
        D2D1_RECT_F bar2 = D2D1::RectF(playCx + 2.0f, playCy - 7.0f, playCx + 5.0f, playCy + 7.0f);
        m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(bar1, 1.5f, 1.5f), playBrush);
        m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(bar2, 1.5f, 1.5f), playBrush);
    }
    else
    {
        // Play icon: Right-pointing triangle
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> playGeo;
        m_d2dFactory->CreatePathGeometry(&playGeo);
        if (playGeo)
        {
            Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
            playGeo->Open(&sink);
            sink->BeginFigure(D2D1::Point2F(playCx - 4.5f, playCy - 7.5f), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(D2D1::Point2F(playCx + 6.5f, playCy));
            sink->AddLine(D2D1::Point2F(playCx - 4.5f, playCy + 7.5f));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            m_d2dContext->FillGeometry(playGeo.Get(), playBrush);
        }
    }

    // 3. Next Button
    D2D1_RECT_F nextRect = GetNextButtonRect();
    float nextCx = (nextRect.left + nextRect.right) * 0.5f;
    float nextCy = (nextRect.top + nextRect.bottom) * 0.5f;

    bool nextHover = (hoveredElement == CardInteractiveElement::NextButton);
    bool nextPressed = (pressedElement == CardInteractiveElement::NextButton);

    if (nextPressed)
    {
        m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(nextCx, nextCy), 15.0f, 15.0f), m_btnPressedBrush.Get());
    }
    else if (nextHover)
    {
        m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(nextCx, nextCy), 15.0f, 15.0f), m_btnHoverBrush.Get());
    }

    auto nextBrush = trackInfo.canNext ? m_btnIconBrush.Get() : m_btnDisabledBrush.Get();
    // Draw >>|
    // Vertical bar
    m_d2dContext->DrawLine(D2D1::Point2F(nextCx + 7.0f, nextCy - 6.0f), D2D1::Point2F(nextCx + 7.0f, nextCy + 6.0f), nextBrush, 2.0f);
    // Double right-pointing triangle
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> nextGeo;
    m_d2dFactory->CreatePathGeometry(&nextGeo);
    if (nextGeo)
    {
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        nextGeo->Open(&sink);
        sink->BeginFigure(D2D1::Point2F(nextCx - 7.0f, nextCy - 6.0f), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1::Point2F(nextCx, nextCy));
        sink->AddLine(D2D1::Point2F(nextCx - 7.0f, nextCy + 6.0f));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);

        sink->BeginFigure(D2D1::Point2F(nextCx, nextCy - 6.0f), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1::Point2F(nextCx + 6.0f, nextCy));
        sink->AddLine(D2D1::Point2F(nextCx, nextCy + 6.0f));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();

        m_d2dContext->FillGeometry(nextGeo.Get(), nextBrush);
    }
}

void MediaCardRenderer::DrawMinimizeButton(CardInteractiveElement hoveredElement, CardInteractiveElement pressedElement)
{
    D2D1_RECT_F minRect = GetMinimizeButtonRect();
    float cx = (minRect.left + minRect.right) * 0.5f;
    float cy = (minRect.top + minRect.bottom) * 0.5f;

    bool isHovered = (hoveredElement == CardInteractiveElement::MinimizeButton);
    bool isPressed = (pressedElement == CardInteractiveElement::MinimizeButton);

    if (isPressed)
    {
        m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 9.0f, 9.0f), m_btnPressedBrush.Get());
    }
    else if (isHovered)
    {
        m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 9.0f, 9.0f), m_btnHoverBrush.Get());
    }

    if (isHovered)
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> rimBrush;
        m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.25f), &rimBrush);
        m_d2dContext->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 9.0f, 9.0f), rimBrush.Get(), 1.0f);
    }

    // Downward tray chevron glyph
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> iconBrush;
    float alpha = isHovered ? 0.95f : 0.45f;
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, alpha), &iconBrush);

    m_d2dContext->DrawLine(D2D1::Point2F(cx - 3.5f, cy - 1.5f), D2D1::Point2F(cx, cy + 2.0f), iconBrush.Get(), 1.5f);
    m_d2dContext->DrawLine(D2D1::Point2F(cx, cy + 2.0f), D2D1::Point2F(cx + 3.5f, cy - 1.5f), iconBrush.Get(), 1.5f);
}

CardInteractiveElement MediaCardRenderer::HitTest(int x, int y, double& outProgressFraction) const
{
    float fx = static_cast<float>(x) / CARD_SCALE;
    float fy = static_cast<float>(y) / CARD_SCALE;

    D2D1_RECT_F prog = GetProgressBarRect();
    if (fx >= prog.left && fx <= prog.right && fy >= 64.0f && fy <= 88.0f)
    {
        outProgressFraction = std::clamp(static_cast<double>(fx - prog.left) / (prog.right - prog.left), 0.0, 1.0);
        return CardInteractiveElement::ProgressBar;
    }

    D2D1_RECT_F prev = GetPreviousButtonRect();
    if (fx >= prev.left && fx <= prev.right && fy >= prev.top && fy <= prev.bottom)
    {
        return CardInteractiveElement::PreviousButton;
    }

    D2D1_RECT_F play = GetPlayPauseButtonRect();
    if (fx >= play.left && fx <= play.right && fy >= play.top && fy <= play.bottom)
    {
        return CardInteractiveElement::PlayPauseButton;
    }

    D2D1_RECT_F next = GetNextButtonRect();
    if (fx >= next.left && fx <= next.right && fy >= next.top && fy <= next.bottom)
    {
        return CardInteractiveElement::NextButton;
    }

    D2D1_RECT_F minBtn = GetMinimizeButtonRect();
    if (fx >= minBtn.left && fx <= minBtn.right && fy >= minBtn.top && fy <= minBtn.bottom)
    {
        return CardInteractiveElement::MinimizeButton;
    }

    if (fx >= 0.0f && fx <= BASE_CARD_WIDTH && fy >= 0.0f && fy <= BASE_CARD_HEIGHT)
    {
        return CardInteractiveElement::CardBody;
    }

    return CardInteractiveElement::None;
}

void MediaCardRenderer::Render(const media::MediaTrackInfo& trackInfo,
                               CardInteractiveElement hoveredElement,
                               CardInteractiveElement pressedElement,
                               bool isScrubbing,
                               double scrubFraction,
                               [[maybe_unused]] double currentTimeSec,
                               bool effectiveIsPlaying)
{
    if (!m_d2dContext || !m_swapChain)
    {
        return;
    }

    UpdateArtworkBitmap(trackInfo);
    UpdateDynamicPaletteBrushes(trackInfo);

    if (m_useLiquidGlass && m_liquidPS && m_backdropSampler.IsValid())
    {
        // Phase 4B.1: D3D11 Liquid Glass Background Pass with Real Backdrop Refraction
        RenderLiquidGlassPass(trackInfo, currentTimeSec);

        // Phase 4B.1: Direct2D Foreground Elements Pass (Scaled 2.3x)
        m_d2dContext->BeginDraw();
        m_d2dContext->SetTransform(D2D1::Matrix3x2F::Scale(CARD_SCALE, CARD_SCALE));

        // Subtle rounded edge highlight matching the exact card contour
        D2D1_ROUNDED_RECT cardRect = D2D1::RoundedRect(
            D2D1::RectF(0.5f, 0.5f, BASE_CARD_WIDTH - 0.5f, BASE_CARD_HEIGHT - 0.5f),
            18.0f, 18.0f
        );
        if (m_cardBorderBrush)
        {
            m_d2dContext->DrawRoundedRectangle(cardRect, m_cardBorderBrush.Get(), 1.0f);
        }
    }
    else
    {
        // Fallback: Phase 4B Direct2D Procedural Glass Pipeline
        m_d2dContext->BeginDraw();
        m_d2dContext->SetTransform(D2D1::Matrix3x2F::Scale(CARD_SCALE, CARD_SCALE));
        m_d2dContext->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f)); // Fully transparent canvas

        // 1. Soft Penumbra / Drop Shadow below card
        D2D1_ROUNDED_RECT shadowRect = D2D1::RoundedRect(
            D2D1::RectF(0.5f, 2.0f, BASE_CARD_WIDTH - 0.5f, BASE_CARD_HEIGHT + 1.0f),
            18.0f, 18.0f
        );
        if (m_cardShadowBrush)
        {
            m_d2dContext->FillRoundedRectangle(shadowRect, m_cardShadowBrush.Get());
        }

        // 2. Card Background & Outline
        D2D1_ROUNDED_RECT cardRect = D2D1::RoundedRect(
            D2D1::RectF(0.5f, 0.5f, BASE_CARD_WIDTH - 0.5f, BASE_CARD_HEIGHT - 0.5f),
            18.0f, 18.0f
        );

        // Pass A: Translucent Smoked Glass Substrate
        if (m_glassBaseBrush)
        {
            m_d2dContext->FillRoundedRectangle(cardRect, m_glassBaseBrush.Get());
        }

        // Pass B: Dynamic Artwork Ambient Radial Color Wash
        if (m_artworkGlowBrush)
        {
            m_d2dContext->FillRoundedRectangle(cardRect, m_artworkGlowBrush.Get());
        }

        // Pass C: Top Overhead Specular Sheen (Polished Glass Surface)
        if (m_glassSheenBrush)
        {
            m_d2dContext->FillRoundedRectangle(cardRect, m_glassSheenBrush.Get());
        }

        // Pass D: Inner Caustic Rim Refraction Band (Simulated Fresnel Edge Light)
        if (m_causticBrush)
        {
            D2D1_ROUNDED_RECT causticRect = D2D1::RoundedRect(
                D2D1::RectF(1.5f, 1.5f, BASE_CARD_WIDTH - 1.5f, BASE_CARD_HEIGHT - 1.5f),
                17.0f, 17.0f
            );
            m_d2dContext->DrawRoundedRectangle(causticRect, m_causticBrush.Get(), 1.5f);
        }

        // Pass E: 3D Inner Bevel Highlight & Ambient Occlusion (Linear Gradient Stroke)
        if (m_glassBevelBrush)
        {
            m_d2dContext->DrawRoundedRectangle(cardRect, m_glassBevelBrush.Get(), 1.0f);
        }

        // Pass F: Outer Boundary Micro-Border & Accent Tint
        if (m_cardBorderBrush)
        {
            m_d2dContext->DrawRoundedRectangle(cardRect, m_cardBorderBrush.Get(), 1.0f);
        }
        if (m_outerBorderTintBrush)
        {
            m_d2dContext->DrawRoundedRectangle(cardRect, m_outerBorderTintBrush.Get(), 1.0f);
        }
    }

    // 3. Album Artwork Thumbnail (Left Side, 108x108 at (16, 16))
    D2D1_RECT_F artRect = D2D1::RectF(16.0f, 16.0f, 124.0f, 124.0f);

    // Subtle dark under-shadow for artwork recess
    D2D1_ROUNDED_RECT artShadow = D2D1::RoundedRect(
        D2D1::RectF(16.0f, 17.5f, 124.0f, 125.5f),
        12.0f, 12.0f
    );
    if (m_cardShadowBrush)
    {
        m_d2dContext->FillRoundedRectangle(artShadow, m_cardShadowBrush.Get());
    }

    if (m_artworkBitmap)
    {
        // Clipped rounded rectangle for artwork
        D2D1_ROUNDED_RECT artRRect = D2D1::RoundedRect(artRect, 12.0f, 12.0f);
        m_d2dContext->PushAxisAlignedClip(artRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        m_d2dContext->DrawBitmap(
            m_artworkBitmap.Get(),
            artRect,
            1.0f,
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR
        );
        m_d2dContext->PopAxisAlignedClip();

        // Subtle thin inner border for artwork definition
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> artBorder;
        m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.18f), &artBorder);
        m_d2dContext->DrawRoundedRectangle(artRRect, artBorder.Get(), 1.0f);
    }
    else
    {
        DrawPlaceholderArtwork(artRect);
    }

    // 3. Track Title Text (Right Side)
    std::wstring titleText = trackInfo.title.empty() ? L"No Media Active" : trackInfo.title;
    D2D1_RECT_F titleRect = D2D1::RectF(136.0f, 16.0f, 344.0f, 38.0f);
    m_d2dContext->DrawText(
        titleText.c_str(),
        static_cast<UINT32>(titleText.length()),
        m_titleFormat.Get(),
        titleRect,
        m_textWhiteBrush.Get()
    );

    // 4. Artist & Album Text
    std::wstring artistText;
    if (trackInfo.artist.empty())
    {
        artistText = trackInfo.title.empty() ? L"Start playing in Spotify, Apple Music, or Browser" : L"Unknown Artist";
    }
    else
    {
        artistText = trackInfo.artist;
        if (!trackInfo.album.empty() && trackInfo.album != trackInfo.title)
        {
            artistText += L" • " + trackInfo.album;
        }
    }
    D2D1_RECT_F artistRect = D2D1::RectF(136.0f, 38.0f, 344.0f, 58.0f);
    m_d2dContext->DrawText(
        artistText.c_str(),
        static_cast<UINT32>(artistText.length()),
        m_artistFormat.Get(),
        artistRect,
        m_textSecondaryBrush.Get()
    );

    // 5. Playback Progress Calculation & Bar (Phase 4A.3 Single Authoritative Position)
    auto renderNow = std::chrono::steady_clock::now();
    int64_t currentPos100ns = trackInfo.GetCurrentPosition100ns(renderNow, effectiveIsPlaying);

    double progress = 0.0;
    if (trackInfo.duration100ns > 0)
    {
        progress = std::clamp(static_cast<double>(currentPos100ns) / trackInfo.duration100ns, 0.0, 1.0);
    }
    if (isScrubbing)
    {
        progress = scrubFraction;
    }

    D2D1_RECT_F progHitRect = GetProgressBarRect();
    bool progHover = (hoveredElement == CardInteractiveElement::ProgressBar) || isScrubbing;
    float barHeight = progHover ? 5.0f : 3.5f;
    float barY = 74.0f;

    // Track Background Line
    D2D1_ROUNDED_RECT progBgRect = D2D1::RoundedRect(
        D2D1::RectF(progHitRect.left, barY, progHitRect.right, barY + barHeight),
        2.0f, 2.0f
    );
    m_d2dContext->FillRoundedRectangle(progBgRect, m_progressBgBrush.Get());

    // Progress Fill Line (Dual-color gradient using dynamic artwork palette)
    float fillRight = progHitRect.left + static_cast<float>(progress * (progHitRect.right - progHitRect.left));
    if (fillRight > progHitRect.left + 1.0f)
    {
        D2D1_ROUNDED_RECT fillRect = D2D1::RoundedRect(
            D2D1::RectF(progHitRect.left, barY, fillRight, barY + barHeight),
            2.0f, 2.0f
        );
        if (m_progressGradientBrush)
        {
            m_d2dContext->FillRoundedRectangle(fillRect, m_progressGradientBrush.Get());
        }
        else
        {
            D2D1_COLOR_F fillCol = D2D1::ColorF(
                m_smoothedHot[0],
                m_smoothedHot[1],
                m_smoothedHot[2],
                0.95f
            );
            m_progressFillBrush->SetColor(fillCol);
            m_d2dContext->FillRoundedRectangle(fillRect, m_progressFillBrush.Get());
        }
    }

    // Scrub Thumb Indicator (Luminous Core + Halo Glow)
    if (progHover && trackInfo.canSeek)
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> thumbGlow;
        m_d2dContext->CreateSolidColorBrush(
            D2D1::ColorF(m_smoothedHot[0], m_smoothedHot[1], m_smoothedHot[2], 0.45f),
            &thumbGlow
        );
        m_d2dContext->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(fillRight, barY + barHeight * 0.5f), 7.5f, 7.5f),
            thumbGlow.Get()
        );
        m_d2dContext->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(fillRight, barY + barHeight * 0.5f), 5.0f, 5.0f),
            m_textWhiteBrush.Get()
        );
    }

    // Time Labels
    int64_t displayPos100ns = isScrubbing ? static_cast<int64_t>(scrubFraction * trackInfo.duration100ns) : currentPos100ns;
    std::wstring posStr = FormatTime(displayPos100ns);
    std::wstring durStr = (trackInfo.duration100ns > 0) ? FormatTime(trackInfo.duration100ns) : L"--:--";

    D2D1_RECT_F posLabelRect = D2D1::RectF(136.0f, 82.0f, 200.0f, 96.0f);
    m_d2dContext->DrawText(
        posStr.c_str(),
        static_cast<UINT32>(posStr.length()),
        m_timeFormat.Get(),
        posLabelRect,
        m_textMutedBrush.Get()
    );

    // Duration label right aligned
    m_timeFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    D2D1_RECT_F durLabelRect = D2D1::RectF(280.0f, 82.0f, 364.0f, 96.0f);
    m_d2dContext->DrawText(
        durStr.c_str(),
        static_cast<UINT32>(durStr.length()),
        m_timeFormat.Get(),
        durLabelRect,
        m_textMutedBrush.Get()
    );
    m_timeFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    // 6. Playback Control Buttons
    DrawVectorControls(trackInfo, hoveredElement, pressedElement, effectiveIsPlaying);

    // 7. Minimize to Tray Button
    DrawMinimizeButton(hoveredElement, pressedElement);

    m_d2dContext->EndDraw();
    m_swapChain->Present(1, 0);
}

} // namespace lumn::ui
