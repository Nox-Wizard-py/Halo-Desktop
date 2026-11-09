#include <windows.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <iostream>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

int main()
{
    ID2D1Factory1* d2dFactory = nullptr;
    D2D1_FACTORY_OPTIONS options = {};
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options, (void**)&d2dFactory);
    if (FAILED(hr))
    {
        std::cout << "D2D1CreateFactory failed hr=0x" << std::hex << hr << std::dec << std::endl;
        return 1;
    }
    std::cout << "Direct2D 1.1 Factory created successfully!" << std::endl;

    IDWriteFactory* dwriteFactory = nullptr;
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&dwriteFactory);
    if (FAILED(hr))
    {
        std::cout << "DWriteCreateFactory failed hr=0x" << std::hex << hr << std::dec << std::endl;
        return 1;
    }
    std::cout << "DirectWrite Factory created successfully!" << std::endl;

    dwriteFactory->Release();
    d2dFactory->Release();
    return 0;
}

