#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dcomp.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <iostream>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

int main()
{
    std::cout << "Testing Direct2D + DirectComposition window setup..." << std::endl;
    // Step 1: Create D3D11 device
    ID3D11Device* d3dDevice = nullptr;
    ID3D11DeviceContext* d3dContext = nullptr;
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
        &d3dDevice, &fl, &d3dContext);
    if (FAILED(hr))
    {
        std::cout << "D3D11CreateDevice failed: 0x" << std::hex << hr << std::dec << std::endl;
        return 1;
    }

    // Step 2: Get DXGI device
    IDXGIDevice* dxgiDevice = nullptr;
    hr = d3dDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
    if (FAILED(hr)) { d3dDevice->Release(); return 1; }

    // Step 3: Create D2D device & device context
    ID2D1Factory1* d2dFactory = nullptr;
    D2D1_FACTORY_OPTIONS options = {};
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options, (void**)&d2dFactory);
    if (FAILED(hr)) { dxgiDevice->Release(); d3dDevice->Release(); return 1; }

    ID2D1Device* d2dDevice = nullptr;
    hr = d2dFactory->CreateDevice(dxgiDevice, &d2dDevice);
    if (FAILED(hr)) { d2dFactory->Release(); dxgiDevice->Release(); d3dDevice->Release(); return 1; }

    ID2D1DeviceContext* d2dContext = nullptr;
    hr = d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2dContext);
    if (FAILED(hr)) { d2dDevice->Release(); d2dFactory->Release(); dxgiDevice->Release(); d3dDevice->Release(); return 1; }

    std::cout << "Successfully created D2D1 Device Context backed by D3D11!" << std::endl;

    d2dContext->Release();
    d2dDevice->Release();
    d2dFactory->Release();
    dxgiDevice->Release();
    d3dContext->Release();
    d3dDevice->Release();
    return 0;
}

