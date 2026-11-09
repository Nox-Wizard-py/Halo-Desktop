#include <windows.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dcomp.h>
#include <d2d1_1.h>
#include <iostream>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "d2d1.lib")

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
    wc.lpszClassName = L"TestAcrylicClass";
    RegisterClassExW(&wc);

    // Create popup window
    DWORD exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    HWND hWnd = CreateWindowExW(
        exStyle,
        L"TestAcrylicClass",
        L"Test Acrylic",
        WS_POPUP,
        200, 200, 400, 200,
        nullptr, nullptr, wc.hInstance, nullptr
    );

    MARGINS margins = { -1, -1, -1, -1 };
    HRESULT hr1 = DwmExtendFrameIntoClientArea(hWnd, &margins);
    int backdrop = 3; // DWMSBT_TRANSIENTWINDOW (Acrylic)
    HRESULT hr2 = DwmSetWindowAttribute(hWnd, 38, &backdrop, sizeof(backdrop));

    // Also test DWMWA_WINDOW_CORNER_PREFERENCE
    int corner = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hWnd, 33, &corner, sizeof(corner));

    std::cout << "hrExtend: 0x" << std::hex << hr1 << ", hrBackdrop: 0x" << hr2 << std::dec << "\n";

    ShowWindow(hWnd, SW_SHOWNOACTIVATE);
    UpdateWindow(hWnd);

    // Let it run for 1 second
    DWORD start = GetTickCount();
    MSG msg;
    while (GetTickCount() - start < 1000)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(16);
    }

    DestroyWindow(hWnd);
    std::cout << "Finished Acrylic test successfully.\n";
    return 0;
}

