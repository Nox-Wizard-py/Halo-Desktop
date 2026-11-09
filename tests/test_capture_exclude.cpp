#include <windows.h>
#include <iostream>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

int main()
{
    std::cout << "[Test] Investigating WDA_EXCLUDEFROMCAPTURE...\n";

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TestCaptureExcludeClass";
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"TestCaptureExcludeClass",
        L"Test Capture Exclude",
        WS_POPUP,
        300, 300, 200, 200,
        nullptr, nullptr, wc.hInstance, nullptr
    );

    if (!hWnd)
    {
        std::cerr << "Failed to create test window\n";
        return 1;
    }

    // SetWindowDisplayAffinity with WDA_EXCLUDEFROMCAPTURE (0x11)
    // Documented in Windows 10 2004+
    const DWORD WDA_EXCLUDE_FROM_CAPTURE = 0x00000011;
    BOOL res = SetWindowDisplayAffinity(hWnd, WDA_EXCLUDE_FROM_CAPTURE);
    DWORD err = GetLastError();
    std::cout << "SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) result: " << res
              << ", GetLastError: " << err << "\n";

    DestroyWindow(hWnd);
    return 0;
}

