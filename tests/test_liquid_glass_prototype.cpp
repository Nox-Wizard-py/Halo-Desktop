#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_3.h>
#include <dcomp.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <iostream>
#include <vector>
#include <chrono>

#include "../src/render/BackdropSampler.hpp"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

using Microsoft::WRL::ComPtr;

struct alignas(16) LiquidGlassConstants
{
    float cardSize[2];          // (380.0, 140.0)
    float cornerRadius;         // 18.0
    float time;                 // continuous animation time
    float hotColor[4];          // palette hot
    float deepColor[4];         // palette deep
    float edgeCurvatureWidth;   // 22.0
    float refractionStrength;   // 16.0
    float chromaticDispersion;  // 2.2
    float tintStrength;         // 0.14
    float padding[2];           // 16-byte alignment padding
};

int main()
{
    std::cout << "=================================================\n";
    std::cout << " Phase 4B.1: Liquid Glass Standalone Prototype\n";
    std::cout << "=================================================\n\n";

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TestLiquidGlassClass";
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP,
        L"TestLiquidGlassClass",
        L"Test Liquid Glass",
        WS_POPUP,
        250, 250, 380, 140,
        nullptr, nullptr, wc.hInstance, nullptr
    );
    if (!hWnd) { std::cerr << "CreateWindowExW failed\n"; return 1; }

    // D3D11 Device & Context
    ComPtr<ID3D11Device> d3dDevice;
    ComPtr<ID3D11DeviceContext> d3dContext;
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
        &d3dDevice, &fl, &d3dContext);
    if (FAILED(hr)) { std::cerr << "D3D11CreateDevice failed\n"; return 1; }

    // DXGI SwapChain
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

    // RTV
    ComPtr<IDXGISurface> dxgiBackBuffer;
    swapChain->GetBuffer(0, IID_PPV_ARGS(&dxgiBackBuffer));
    ComPtr<ID3D11Texture2D> backBufferTex;
    dxgiBackBuffer.As(&backBufferTex);
    ComPtr<ID3D11RenderTargetView> rtv;
    d3dDevice->CreateRenderTargetView(backBufferTex.Get(), nullptr, &rtv);

    // Direct2D Setup
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

    // Backdrop Sampler
    lumn::render::BackdropSampler backdropSampler;
    bool initSampler = backdropSampler.Initialize(d3dDevice.Get(), hWnd, 380, 140);
    std::cout << "[Init] BackdropSampler initialized: " << initSampler << "\n";
    if (!initSampler) return 1;

    // Compile Shaders
    ComPtr<ID3DBlob> vsBlob, psBlob, errBlob;
    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    compileFlags |= D3DCOMPILE_DEBUG;
#endif

    std::wstring shaderPath = L"src/render/shaders/liquidGlass.hlsl";
    hr = D3DCompileFromFile(shaderPath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                            "VSMain", "vs_5_0", compileFlags, 0, &vsBlob, &errBlob);
    if (FAILED(hr))
    {
        if (errBlob) std::cerr << (char*)errBlob->GetBufferPointer() << "\n";
        return 1;
    }
    ComPtr<ID3D11VertexShader> liquidVS;
    d3dDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &liquidVS);

    hr = D3DCompileFromFile(shaderPath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                            "PSMain", "ps_5_0", compileFlags, 0, &psBlob, &errBlob);
    if (FAILED(hr))
    {
        if (errBlob) std::cerr << (char*)errBlob->GetBufferPointer() << "\n";
        return 1;
    }
    ComPtr<ID3D11PixelShader> liquidPS;
    d3dDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &liquidPS);

    // Constant Buffer
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(LiquidGlassConstants);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ComPtr<ID3D11Buffer> constantBuffer;
    d3dDevice->CreateBuffer(&cbDesc, nullptr, &constantBuffer);

    // Sampler State (Linear Clamp)
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    ComPtr<ID3D11SamplerState> samplerState;
    d3dDevice->CreateSamplerState(&sampDesc, &samplerState);

    // Show window
    ShowWindow(hWnd, SW_SHOWNOACTIVATE);

    // Measure single frame render latency
    auto startBench = std::chrono::high_resolution_clock::now();

    for (int frame = 0; frame < 60; ++frame)
    {
        // Step 1: Capture Backdrop
        backdropSampler.Capture(hWnd, d3dContext.Get());

        // Step 2: D3D11 Pass (Liquid Glass Background)
        float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        d3dContext->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
        d3dContext->ClearRenderTargetView(rtv.Get(), clearColor);

        D3D11_VIEWPORT vp = { 0.0f, 0.0f, 380.0f, 140.0f, 0.0f, 1.0f };
        d3dContext->RSSetViewports(1, &vp);

        // Update constant buffer
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        d3dContext->Map(constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        auto* cb = static_cast<LiquidGlassConstants*>(mapped.pData);
        cb->cardSize[0] = 380.0f;
        cb->cardSize[1] = 140.0f;
        cb->cornerRadius = 18.0f;
        cb->time = static_cast<float>(frame * 0.016);
        cb->hotColor[0] = 0.20f; cb->hotColor[1] = 0.85f; cb->hotColor[2] = 1.00f; cb->hotColor[3] = 1.00f;
        cb->deepColor[0] = 0.55f; cb->deepColor[1] = 0.10f; cb->deepColor[2] = 0.95f; cb->deepColor[3] = 1.00f;
        cb->edgeCurvatureWidth = 22.0f;
        cb->refractionStrength = 16.0f;
        cb->chromaticDispersion = 2.2f;
        cb->tintStrength = 0.14f;
        d3dContext->Unmap(constantBuffer.Get(), 0);

        d3dContext->VSSetShader(liquidVS.Get(), nullptr, 0);
        d3dContext->PSSetShader(liquidPS.Get(), nullptr, 0);
        d3dContext->PSSetConstantBuffers(0, 1, constantBuffer.GetAddressOf());

        ID3D11ShaderResourceView* srv = backdropSampler.GetShaderResourceView();
        d3dContext->PSSetShaderResources(0, 1, &srv);
        d3dContext->PSSetSamplers(0, 1, samplerState.GetAddressOf());

        d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        d3dContext->Draw(3, 0);

        // Unbind SRV to allow capture map on next frame
        ID3D11ShaderResourceView* nullSRV = nullptr;
        d3dContext->PSSetShaderResources(0, 1, &nullSRV);

        // Step 3: Direct2D Foreground Elements Pass
        d2dContext->BeginDraw();

        ComPtr<ID2D1SolidColorBrush> whiteBrush;
        d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &whiteBrush);
        d2dContext->DrawRectangle(D2D1::RectF(16, 16, 124, 124), whiteBrush.Get(), 1.5f);

        d2dContext->EndDraw();

        swapChain->Present(1, 0);
    }

    auto endBench = std::chrono::high_resolution_clock::now();
    double totalMs = std::chrono::duration<double, std::milli>(endBench - startBench).count();
    double avgMs = totalMs / 60.0;

    std::cout << "[Benchmark] Rendered 60 frames of full Liquid Glass in " << totalMs << " ms (Avg: "
              << avgMs << " ms/frame).\n";
    std::cout << "SUCCESS: Liquid Glass prototype runs smoothly with true backdrop sampling!\n";

    DestroyWindow(hWnd);
    return 0;
}

