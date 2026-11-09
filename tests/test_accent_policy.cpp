#include <windows.h>
#include <iostream>

#pragma comment(lib, "user32.lib")

struct ACCENT_POLICY
{
    int AccentState;
    int AccentFlags;
    int GradientColor;
    int AnimationId;
};

struct WINCOMPATTRDATA
{
    int nAttribute;
    void* pData;
    ULONG ulDataSize;
};

typedef BOOL(WINAPI* pfnSetWindowCompositionAttribute)(HWND, WINCOMPATTRDATA*);

int main()
{
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    auto SetWindowCompositionAttribute = (pfnSetWindowCompositionAttribute)GetProcAddress(hUser32, "SetWindowCompositionAttribute");
    if (!SetWindowCompositionAttribute)
    {
        std::cout << "SetWindowCompositionAttribute not found.\n";
        return 1;
    }

    std::cout << "Found SetWindowCompositionAttribute.\n";

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TestAccentClass";
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"TestAccentClass",
        L"Test Accent",
        WS_POPUP,
        150, 150, 380, 140,
        nullptr, nullptr, wc.hInstance, nullptr
    );

    // Accent state: ACCENT_ENABLE_BLURBEHIND = 3, ACCENT_ENABLE_ACRYLICBLURBEHIND = 4
    ACCENT_POLICY policy = { 4, 2, static_cast<int>(0x99181818), 0 };
    WINCOMPATTRDATA data = { 19, &policy, sizeof(policy) }; // WCA_ACCENT_POLICY = 19
    BOOL res = SetWindowCompositionAttribute(hWnd, &data);
    std::cout << "SetWindowCompositionAttribute result: " << res << "\n";

    DestroyWindow(hWnd);
    return 0;
}
