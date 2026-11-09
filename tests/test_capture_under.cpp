#include <windows.h>
#include <iostream>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        HBRUSH redBrush = CreateSolidBrush(RGB(255, 0, 0));
        FillRect(hdc, &ps.rcPaint, redBrush);
        DeleteObject(redBrush);
        EndPaint(hWnd, &ps);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int main()
{
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TestCaptureUnderClass";
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"TestCaptureUnderClass",
        L"Test Under",
        WS_POPUP,
        400, 400, 200, 200,
        nullptr, nullptr, wc.hInstance, nullptr
    );

    const DWORD WDA_EXCLUDE_FROM_CAPTURE = 0x00000011;
    SetWindowDisplayAffinity(hWnd, WDA_EXCLUDE_FROM_CAPTURE);

    ShowWindow(hWnd, SW_SHOWNOACTIVATE);
    UpdateWindow(hWnd);
    Sleep(50); // Let DWM composite the window

    // Now capture the screen at (450, 450)
    HDC screenDC = GetDC(nullptr);
    COLORREF captured = GetPixel(screenDC, 450, 450);
    ReleaseDC(nullptr, screenDC);

    int r = GetRValue(captured);
    int g = GetGValue(captured);
    int b = GetBValue(captured);

    std::cout << "Captured pixel at (450, 450): R=" << r << ", G=" << g << ", B=" << b << "\n";
    if (r == 255 && g == 0 && b == 0)
    {
        std::cout << "Result: Window WAS captured (BitBlt does NOT exclude WDA_EXCLUDEFROMCAPTURE).\n";
    }
    else
    {
        std::cout << "Result: Window WAS EXCLUDED! (Captured the actual desktop under the window!)\n";
    }

    DestroyWindow(hWnd);
    return 0;
}

