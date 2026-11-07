#include "Window.hpp"
#include <iostream>
#include <fstream>
#include <string>

namespace lumn::platform
{

static void LogWindowMsg(const std::string& msg)
{
    std::ofstream f("lumn_debug.log", std::ios::app);
    f << msg << std::endl;
}

static constexpr const wchar_t* WINDOW_CLASS_NAME = L"LumnOverlayWindowClass";

Window::Window() = default;

Window::~Window()
{
    Shutdown();
}

bool Window::Initialize(HINSTANCE hInstance, ResizeCallback onResize)
{
    m_hInstance = hInstance;
    m_onResize = std::move(onResize);

    // 1. Enable Per-Monitor V2 DPI awareness for pixel-accurate primary monitor coordinates
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // 2. Register Window Class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = m_hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // DirectComposition handles presentation
    wc.lpszClassName = WINDOW_CLASS_NAME;

    if (!RegisterClassExW(&wc))
    {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS)
        {
            LogWindowMsg("[Lumn] Failed to register window class. Error: " + std::to_string(err));
            return false;
        }
    }

    // 3. Determine Primary Monitor Physical Bounds
    int posX = 0;
    int posY = 0;
    QueryPrimaryMonitorBounds(posX, posY, m_width, m_height);
    LogWindowMsg("[Lumn] Primary monitor bounds: " + std::to_string(m_width) + "x" + std::to_string(m_height) +
                 " at (" + std::to_string(posX) + "," + std::to_string(posY) + ")");

    // 4. Create Transparent Click-Through Topmost Window
    DWORD dwExStyle = WS_EX_TOPMOST |
                      WS_EX_TRANSPARENT |
                      WS_EX_LAYERED |
                      WS_EX_TOOLWINDOW |
                      WS_EX_NOACTIVATE |
                      WS_EX_NOREDIRECTIONBITMAP;

    DWORD dwStyle = WS_POPUP;

    m_hwnd = CreateWindowExW(
        dwExStyle,
        WINDOW_CLASS_NAME,
        L"Lumn Rim Overlay",
        dwStyle,
        posX,
        posY,
        static_cast<int>(m_width),
        static_cast<int>(m_height),
        nullptr,
        nullptr,
        m_hInstance,
        this
    );

    if (!m_hwnd)
    {
        DWORD err = GetLastError();
        LogWindowMsg("[Lumn] Failed to create overlay window. Error: " + std::to_string(err));
        return false;
    }

    // 5. Register Global Hotkeys for Clean Exit
    // Since WS_EX_TRANSPARENT and WS_EX_NOACTIVATE prevent keyboard focus,
    // we register ESC and Ctrl+Shift+Q as system hotkeys to close the overlay cleanly.
    if (!RegisterHotKey(m_hwnd, HOTKEY_ID_ESC, MOD_NOREPEAT, VK_ESCAPE))
    {
        std::cerr << "[Lumn] Warning: Could not register ESC hotkey. Trying fallback hotkey.\n";
    }

    RegisterHotKey(m_hwnd, HOTKEY_ID_CTRL_SHIFT_Q, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'Q');

    // 6. Display the Window (without activating or taking focus)
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(m_hwnd);

    m_running = true;
    std::cout << "[Lumn] Native overlay window created (" << m_width << "x" << m_height 
              << " at " << posX << "," << posY << ")\n";
    std::cout << "[Lumn] Press ESC or Ctrl+Shift+Q at any time to exit.\n";

    return true;
}

void Window::Show()
{
    if (m_hwnd)
    {
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    }
}

void Window::Hide()
{
    if (m_hwnd)
    {
        ShowWindow(m_hwnd, SW_HIDE);
    }
}

bool Window::IsVisible() const noexcept
{
    return m_hwnd && IsWindowVisible(m_hwnd);
}

void Window::Shutdown()
{
    if (m_hwnd)
    {
        UnregisterHotKey(m_hwnd, HOTKEY_ID_ESC);
        UnregisterHotKey(m_hwnd, HOTKEY_ID_CTRL_SHIFT_Q);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }

    if (m_hInstance)
    {
        UnregisterClassW(WINDOW_CLASS_NAME, m_hInstance);
        m_hInstance = nullptr;
    }

    m_running = false;
}

bool Window::ProcessMessages()
{
    MSG msg = {};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT)
        {
            m_running = false;
            return false;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return m_running;
}

void Window::QueryPrimaryMonitorBounds(int& x, int& y, uint32_t& w, uint32_t& h)
{
    HMONITOR hMonitor = MonitorFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);

    if (GetMonitorInfoW(hMonitor, &mi))
    {
        x = mi.rcMonitor.left;
        y = mi.rcMonitor.top;
        w = static_cast<uint32_t>(mi.rcMonitor.right - mi.rcMonitor.left);
        h = static_cast<uint32_t>(mi.rcMonitor.bottom - mi.rcMonitor.top);
    }
    else
    {
        // Fallback to SM_CXSCREEN / SM_CYSCREEN
        x = 0;
        y = 0;
        w = static_cast<uint32_t>(GetSystemMetrics(SM_CXSCREEN));
        h = static_cast<uint32_t>(GetSystemMetrics(SM_CYSCREEN));
    }
}

void Window::UpdateDimensions(uint32_t newWidth, uint32_t newHeight)
{
    if (m_width != newWidth || m_height != newHeight)
    {
        m_width = newWidth;
        m_height = newHeight;
        if (m_onResize)
        {
            m_onResize(m_width, m_height);
        }
    }
}

LRESULT CALLBACK Window::WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    Window* self = nullptr;

    if (uMsg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<Window*>(cs->lpCreateParams);
        if (self)
        {
            self->m_hwnd = hWnd;
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
    }
    else
    {
        self = reinterpret_cast<Window*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }

    if (self)
    {
        return self->HandleMessage(hWnd, uMsg, wParam, lParam);
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

LRESULT Window::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_HOTKEY:
    {
        if (wParam == HOTKEY_ID_ESC)
        {
            if (m_onEscape && m_onEscape())
            {
                return 0; // Escape was handled/consumed without quitting application
            }
            std::cout << "[Lumn] Exit hotkey received. Shutting down overlay...\n";
            m_running = false;
            PostQuitMessage(0);
            return 0;
        }
        else if (wParam == HOTKEY_ID_CTRL_SHIFT_Q)
        {
            std::cout << "[Lumn] Exit hotkey received. Shutting down overlay...\n";
            m_running = false;
            PostQuitMessage(0);
            return 0;
        }
        break;
    }

    case WM_DISPLAYCHANGE:
    {
        std::cout << "[Lumn] Display resolution change detected (WM_DISPLAYCHANGE).\n";
        int newX = 0;
        int newY = 0;
        uint32_t newW = 0;
        uint32_t newH = 0;
        QueryPrimaryMonitorBounds(newX, newY, newW, newH);

        SetWindowPos(
            hWnd,
            HWND_TOPMOST,
            newX,
            newY,
            static_cast<int>(newW),
            static_cast<int>(newH),
            SWP_NOACTIVATE | SWP_FRAMECHANGED
        );

        UpdateDimensions(newW, newH);
        return 0;
    }

    case WM_DPICHANGED:
    {
        auto* const prcNewWindow = reinterpret_cast<RECT*>(lParam);
        int newW = prcNewWindow->right - prcNewWindow->left;
        int newH = prcNewWindow->bottom - prcNewWindow->top;

        SetWindowPos(
            hWnd,
            HWND_TOPMOST,
            prcNewWindow->left,
            prcNewWindow->top,
            newW,
            newH,
            SWP_NOACTIVATE | SWP_FRAMECHANGED
        );

        UpdateDimensions(static_cast<uint32_t>(newW), static_cast<uint32_t>(newH));
        return 0;
    }

    case WM_ERASEBKGND:
        return 1; // Prevent background erasing; DirectComposition manages presentation

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_CLOSE:
        m_running = false;
        PostQuitMessage(0);
        return 0;

    case WM_DESTROY:
        m_running = false;
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

} // namespace lumn::platform

