#include <windows.h>
#include <iostream>
#include <chrono>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

int main()
{
    std::cout << "[Test] Measuring 380x140 Localized Backdrop Capture Latency...\n";

    int w = 380;
    int h = 140;

    HDC screenDC = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hBmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    HGDIOBJ oldBmp = SelectObject(memDC, hBmp);

    // Warm up
    BitBlt(memDC, 0, 0, w, h, screenDC, 100, 100, SRCCOPY);

    // Benchmark 100 iterations
    auto start = std::chrono::high_resolution_clock::now();
    const int ITERS = 100;
    for (int i = 0; i < ITERS; ++i)
    {
        BitBlt(memDC, 0, 0, w, h, screenDC, 100, 100, SRCCOPY);
    }
    auto end = std::chrono::high_resolution_clock::now();

    double totalMs = std::chrono::duration<double, std::milli>(end - start).count();
    double avgMs = totalMs / ITERS;

    std::cout << "Captured " << ITERS << " localized frames in " << totalMs << " ms (Average: " << avgMs << " ms per capture)\n";

    SelectObject(memDC, oldBmp);
    DeleteObject(hBmp);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);

    return 0;
}

