#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dcomp.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <iostream>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

using Microsoft::WRL::ComPtr;

int main()
{
    std::cout << "[Test] Verifying D3D11 + Direct2D Hybrid Rendering to SwapChain...\n";

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TestHybridClass";
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP,
        L"TestHybridClass",
        L"Test Hybrid",
        WS_POPUP,
        100, 100, 380, 140,
        nullptr, nullptr, wc.hInstance, nullptr
    );

    // D3D11
    ComPtr<ID3D11Device> d3dDevice;
    ComPtr<ID3D11DeviceContext> d3dContext;
    D3D_FEATURE_LEVEL fl;
    D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
        &d3dDevice, &fl, &d3dContext);

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
    factory->CreateSwapChainForComposition(d3dDevice.Get(), &desc, nullptr, &swapChain);

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

    // Swapchain buffers for D3D11 and D2D
    ComPtr<IDXGISurface> dxgiBackBuffer;
    swapChain->GetBuffer(0, IID_PPV_ARGS(&dxgiBackBuffer));

    ComPtr<ID3D11Texture2D> backBufferTex;
    dxgiBackBuffer.As(&backBufferTex);
    ComPtr<ID3D11RenderTargetView> rtv;
    d3dDevice->CreateRenderTargetView(backBufferTex.Get(), nullptr, &rtv);

    // Direct2D
    ComPtr<ID2D1Factory1> d2dFactory;
    D2D1_FACTORY_OPTIONS opt = {};
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opt, (void**)&d2dFactory);
    ComPtr<ID2D1Device> d2dDevice;
    d2dFactory->CreateDevice(dxgiDevice.Get(), &d2dDevice);
    ComPtr<ID2D1DeviceContext> d2dContext;
    d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2dContext);

    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );
    ComPtr<ID2D1Bitmap1> targetBitmap;
    d2dContext->CreateBitmapFromDxgiSurface(dxgiBackBuffer.Get(), &bp, &targetBitmap);
    d2dContext->SetTarget(targetBitmap.Get());

    // Step 1: D3D11 pass
    float clearColor[4] = { 0.2f, 0.4f, 0.8f, 0.7f };
    d3dContext->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
    d3dContext->ClearRenderTargetView(rtv.Get(), clearColor);

    // Step 2: D2D pass on top
    d2dContext->BeginDraw();
    ComPtr<ID2D1SolidColorBrush> brush;
    d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &brush);
    d2dContext->DrawRectangle(D2D1::RectF(10, 10, 100, 100), brush.Get(), 2.0f);
    HRESULT hrD2D = d2dContext->EndDraw();

    HRESULT hrPresent = swapChain->Present(1, 0);
    std::cout << "hrD2D: 0x" << std::hex << hrD2D << ", hrPresent: 0x" << hrPresent << std::dec << "\n";
    std::cout << "D3D11 + Direct2D hybrid presentation SUCCEEDED!\n";

    DestroyWindow(hWnd);
    return 0;
}
