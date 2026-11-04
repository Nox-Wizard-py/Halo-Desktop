#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <functional>
#include <cstdint>

namespace lumn::platform
{

class Window
{
public:
    using ResizeCallback = std::function<void(uint32_t width, uint32_t height)>;

    Window();
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool Initialize(HINSTANCE hInstance, ResizeCallback onResize);
    void Shutdown();

    bool ProcessMessages();

    HWND GetHandle() const noexcept { return m_hwnd; }
    uint32_t GetWidth() const noexcept { return m_width; }
    uint32_t GetHeight() const noexcept { return m_height; }
    bool IsRunning() const noexcept { return m_running; }

private:
    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    void QueryPrimaryMonitorBounds(int& x, int& y, uint32_t& w, uint32_t& h);
    void UpdateDimensions(uint32_t newWidth, uint32_t newHeight);

    HWND m_hwnd = nullptr;
    HINSTANCE m_hInstance = nullptr;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_running = false;
    ResizeCallback m_onResize;

    static constexpr int HOTKEY_ID_ESC = 1001;
    static constexpr int HOTKEY_ID_CTRL_SHIFT_Q = 1002;
};

} // namespace lumn::platform

