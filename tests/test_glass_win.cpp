#include <windows.h>
#include <dwmapi.h>
#include <iostream>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "user32.lib")

int main()
{
    std::cout << "Testing DWM Window Attributes on Windows 11...\n";

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TestGlassClass";
    RegisterClassExW(&wc);

    // Test 1: WS_POPUP with DWMWA_SYSTEMBACKDROP_TYPE
    DWORD exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    HWND hWnd = CreateWindowExW(
        exStyle,
        L"TestGlassClass",
        L"Test Glass",
        WS_POPUP,
        100, 100, 380, 140,
        nullptr, nullptr, wc.hInstance, nullptr
    );

    if (!hWnd)
    {
        std::cout << "Failed to create window: " << GetLastError() << "\n";
        return 1;
    }

    // Extend frame into client area
    MARGINS margins = { -1, -1, -1, -1 };
    HRESULT hrExtend = DwmExtendFrameIntoClientArea(hWnd, &margins);
    std::cout << "DwmExtendFrameIntoClientArea result: 0x" << std::hex << hrExtend << std::dec << "\n";

    // System backdrop type: DWMSBT_TRANSIENTWINDOW (3 = Acrylic)
    // DWMWA_SYSTEMBACKDROP_TYPE = 38
    int backdrop = 3; // DWMSBT_TRANSIENTWINDOW
    HRESULT hrBackdrop = DwmSetWindowAttribute(hWnd, 38, &backdrop, sizeof(backdrop));
    std::cout << "DwmSetWindowAttribute(38, TRANSIENT) result: 0x" << std::hex << hrBackdrop << std::dec << "\n";

    // Also test with WS_EX_NOREDIRECTIONBITMAP
    DestroyWindow(hWnd);

    exStyle |= WS_EX_NOREDIRECTIONBITMAP;
    HWND hWnd2 = CreateWindowExW(
        exStyle,
        L"TestGlassClass",
        L"Test Glass 2",
        WS_POPUP,
        100, 100, 380, 140,
        nullptr, nullptr, wc.hInstance, nullptr
    );
    hrExtend = DwmExtendFrameIntoClientArea(hWnd2, &margins);
    hrBackdrop = DwmSetWindowAttribute(hWnd2, 38, &backdrop, sizeof(backdrop));
    std::cout << "With WS_EX_NOREDIRECTIONBITMAP: DwmSetWindowAttribute result: 0x" << std::hex << hrBackdrop << std::dec << "\n";
    DestroyWindow(hWnd2);

    // Also test with WS_EX_LAYERED
    exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED;
    HWND hWnd3 = CreateWindowExW(
        exStyle,
        L"TestGlassClass",
        L"Test Glass 3",
        WS_POPUP,
        100, 100, 380, 140,
        nullptr, nullptr, wc.hInstance, nullptr
    );
    hrExtend = DwmExtendFrameIntoClientArea(hWnd3, &margins);
    hrBackdrop = DwmSetWindowAttribute(hWnd3, 38, &backdrop, sizeof(backdrop));
    std::cout << "With WS_EX_LAYERED: DwmSetWindowAttribute result: 0x" << std::hex << hrBackdrop << std::dec << "\n";
    DestroyWindow(hWnd3);

    return 0;
}
