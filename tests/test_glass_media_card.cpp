#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dcomp.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <iostream>
#include <vector>
#include <chrono>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

using Microsoft::WRL::ComPtr;

int main()
{
    std::cout << "[Test] Initializing Glass Material Test...\n";

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TestGlassCardClass";
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP,
        L"TestGlassCardClass",
        L"Test Glass Card",
        WS_POPUP,
        100, 100, 380, 140,
        nullptr, nullptr, wc.hInstance, nullptr
    );

    if (!hWnd)
    {
        std::cerr << "[Test] Failed to create window\n";
        return 1;
    }

    // D3D11
    ComPtr<ID3D11Device> d3dDevice;
    ComPtr<ID3D11DeviceContext> d3dContext;
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
        &d3dDevice, &fl, &d3dContext);
    if (FAILED(hr)) { std::cerr << "D3D11CreateDevice failed\n"; return 1; }

    ComPtr<IDXGIDevice> dxgiDevice;
    d3dDevice.As(&dxgiDevice);
    ComPtr<IDXGIAdapter> adapter;
    dxgiDevice->GetAdapter(&adapter);
    ComPtr<IDXGIFactory2> factory;
    adapter->GetParent(IID_PPV_ARGS(&factory));

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = 380;
    desc.Height = 140;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    ComPtr<IDXGISwapChain1> swapChain;
    hr = factory->CreateSwapChainForComposition(d3dDevice.Get(), &desc, nullptr, &swapChain);
    if (FAILED(hr)) { std::cerr << "CreateSwapChainForComposition failed\n"; return 1; }

    // DirectComposition
    ComPtr<IDCompositionDevice> dcompDevice;
    DCompositionCreateDevice(nullptr, IID_PPV_ARGS(&dcompDevice));
    ComPtr<IDCompositionTarget> dcompTarget;
    dcompDevice->CreateTargetForHwnd(hWnd, TRUE, &dcompTarget);
    ComPtr<IDCompositionVisual> visual;
    dcompDevice->CreateVisual(&visual);
    visual->SetContent(swapChain.Get());
    dcompTarget->SetRoot(visual.Get());
    dcompDevice->Commit();

    // Direct2D
    ComPtr<ID2D1Factory1> d2dFactory;
    D2D1_FACTORY_OPTIONS opt = {};
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opt, (void**)&d2dFactory);
    ComPtr<ID2D1Device> d2dDevice;
    d2dFactory->CreateDevice(dxgiDevice.Get(), &d2dDevice);
    ComPtr<ID2D1DeviceContext> d2dContext;
    d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2dContext);

    ComPtr<IDXGISurface> backBuffer;
    swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );
    ComPtr<ID2D1Bitmap1> targetBitmap;
    d2dContext->CreateBitmapFromDxgiSurface(backBuffer.Get(), &bp, &targetBitmap);
    d2dContext->SetTarget(targetBitmap.Get());

    // Create Gradient Brushes for Glass
    // 1. Smoked Glass Base Brush (Vertical)
    D2D1_GRADIENT_STOP baseStops[2];
    baseStops[0].position = 0.0f;
    baseStops[0].color = D2D1::ColorF(0.08f, 0.09f, 0.13f, 0.65f);
    baseStops[1].position = 1.0f;
    baseStops[1].color = D2D1::ColorF(0.04f, 0.05f, 0.08f, 0.80f);
    ComPtr<ID2D1GradientStopCollection> baseStopColl;
    d2dContext->CreateGradientStopCollection(baseStops, 2, &baseStopColl);

    ComPtr<ID2D1LinearGradientBrush> baseGlassBrush;
    d2dContext->CreateLinearGradientBrush(
        D2D1::LinearGradientBrushProperties(D2D1::Point2F(0.0f, 0.0f), D2D1::Point2F(0.0f, 140.0f)),
        baseStopColl.Get(),
        &baseGlassBrush
    );

    // 2. Specular Sheen Brush (Top-to-bottom overhead light)
    D2D1_GRADIENT_STOP sheenStops[3];
    sheenStops[0].position = 0.0f;
    sheenStops[0].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.16f);
    sheenStops[1].position = 0.35f;
    sheenStops[1].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.03f);
    sheenStops[2].position = 1.0f;
    sheenStops[2].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.00f);
    ComPtr<ID2D1GradientStopCollection> sheenStopColl;
    d2dContext->CreateGradientStopCollection(sheenStops, 3, &sheenStopColl);

    ComPtr<ID2D1LinearGradientBrush> sheenBrush;
    d2dContext->CreateLinearGradientBrush(
        D2D1::LinearGradientBrushProperties(D2D1::Point2F(0.0f, 0.0f), D2D1::Point2F(0.0f, 140.0f)),
        sheenStopColl.Get(),
        &sheenBrush
    );

    // 3. Inner Bevel Highlight Stroke (Bright top, dark bottom)
    D2D1_GRADIENT_STOP bevelStops[4];
    bevelStops[0].position = 0.0f;
    bevelStops[0].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.42f);
    bevelStops[1].position = 0.25f;
    bevelStops[1].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.15f);
    bevelStops[2].position = 0.70f;
    bevelStops[2].color = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.08f);
    bevelStops[3].position = 1.0f;
    bevelStops[3].color = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.48f);
    ComPtr<ID2D1GradientStopCollection> bevelStopColl;
    d2dContext->CreateGradientStopCollection(bevelStops, 4, &bevelStopColl);

    ComPtr<ID2D1LinearGradientBrush> bevelBrush;
    d2dContext->CreateLinearGradientBrush(
        D2D1::LinearGradientBrushProperties(D2D1::Point2F(0.0f, 0.0f), D2D1::Point2F(0.0f, 140.0f)),
        bevelStopColl.Get(),
        &bevelBrush
    );

    // Test Palette (Amber / Violet)
    float hot[4] = { 1.0f, 0.45f, 0.15f, 1.0f };
    float deep[4] = { 0.50f, 0.10f, 0.85f, 1.0f };

    // 4. Ambient Artwork Radial Tint
    D2D1_GRADIENT_STOP tintStops[3];
    tintStops[0].position = 0.0f;
    tintStops[0].color = D2D1::ColorF(hot[0], hot[1], hot[2], 0.22f);
    tintStops[1].position = 0.45f;
    tintStops[1].color = D2D1::ColorF(deep[0], deep[1], deep[2], 0.12f);
    tintStops[2].position = 1.0f;
    tintStops[2].color = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f);
    ComPtr<ID2D1GradientStopCollection> tintStopColl;
    d2dContext->CreateGradientStopCollection(tintStops, 3, &tintStopColl);

    ComPtr<ID2D1RadialGradientBrush> tintBrush;
    d2dContext->CreateRadialGradientBrush(
        D2D1::RadialGradientBrushProperties(D2D1::Point2F(70.0f, 70.0f), D2D1::Point2F(0.0f, 0.0f), 240.0f, 140.0f),
        tintStopColl.Get(),
        &tintBrush
    );

    // Render frame
    d2dContext->BeginDraw();
    d2dContext->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    D2D1_ROUNDED_RECT cardRect = D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, 379.5f, 139.5f), 18.0f, 18.0f);

    // Pass 1: Translucent Smoked Glass Substrate
    d2dContext->FillRoundedRectangle(cardRect, baseGlassBrush.Get());

    // Pass 2: Artwork Ambient Radial Color Wash
    d2dContext->FillRoundedRectangle(cardRect, tintBrush.Get());

    // Pass 3: Top Overhead Specular Sheen
    d2dContext->FillRoundedRectangle(cardRect, sheenBrush.Get());

    // Pass 4: Inner Caustic Refraction Band
    D2D1_ROUNDED_RECT causticRect = D2D1::RoundedRect(D2D1::RectF(1.5f, 1.5f, 378.5f, 138.5f), 17.0f, 17.0f);
    ComPtr<ID2D1SolidColorBrush> causticBrush;
    d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.08f), &causticBrush);
    d2dContext->DrawRoundedRectangle(causticRect, causticBrush.Get(), 1.5f);

    // Pass 5: 3D Inner Bevel Highlight & Occlusion Stroke
    d2dContext->DrawRoundedRectangle(cardRect, bevelBrush.Get(), 1.0f);

    // Pass 6: Outer Boundary Accent Tint
    ComPtr<ID2D1SolidColorBrush> outerBorderBrush;
    d2dContext->CreateSolidColorBrush(D2D1::ColorF(hot[0], hot[1], hot[2], 0.22f), &outerBorderBrush);
    d2dContext->DrawRoundedRectangle(cardRect, outerBorderBrush.Get(), 1.0f);

    hr = d2dContext->EndDraw();
    if (FAILED(hr)) { std::cerr << "EndDraw failed: 0x" << std::hex << hr << "\n"; return 1; }

    hr = swapChain->Present(1, 0);
    if (FAILED(hr)) { std::cerr << "Present failed: 0x" << std::hex << hr << "\n"; return 1; }

    std::cout << "[Test] Glass material rendered successfully! All passes succeeded.\n";
    DestroyWindow(hWnd);
    return 0;
}

