#include <windows.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dcomp.h>
#include <d2d1_1.h>
#include <wrl/client.h>
#include <iostream>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "d2d1.lib")

using Microsoft::WRL::ComPtr;

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_DESTROY) PostQuitMessage(0);
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int main()
{
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TestDCompGlass";
    RegisterClassExW(&wc);

    // Let's test WS_EX_NOREDIRECTIONBITMAP vs WS_EX_LAYERED
    DWORD exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP;
    HWND hWnd = CreateWindowExW(
        exStyle,
        L"TestDCompGlass",
        L"Test DComp Glass",
        WS_POPUP,
        300, 300, 380, 140,
        nullptr, nullptr, wc.hInstance, nullptr
    );

    MARGINS margins = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(hWnd, &margins);
    int backdrop = 3; // Acrylic
    DwmSetWindowAttribute(hWnd, 38, &backdrop, sizeof(backdrop));

    // Create D3D11 & SwapChain
    ComPtr<ID3D11Device> d3dDevice;
    ComPtr<ID3D11DeviceContext> d3dContext;
    D3D_FEATURE_LEVEL fl;
    D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &d3dDevice, &fl, &d3dContext);

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

    // D2D
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

    // Render clear frame
    d2dContext->BeginDraw();
    d2dContext->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    d2dContext->EndDraw();
    swapChain->Present(1, 0);

    ShowWindow(hWnd, SW_SHOWNOACTIVATE);

    // Capture screen pixel at (350, 350)
    HDC screenDC = GetDC(nullptr);
    COLORREF c1 = GetPixel(screenDC, 350, 350);
    ReleaseDC(nullptr, screenDC);
    std::cout << "Pixel color at (350, 350): R=" << (int)GetRValue(c1) << " G=" << (int)GetGValue(c1) << " B=" << (int)GetBValue(c1) << "\n";

    DestroyWindow(hWnd);
    return 0;
}

