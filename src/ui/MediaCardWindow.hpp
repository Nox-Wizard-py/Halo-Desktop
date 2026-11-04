#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <shellapi.h>
#include <memory>
#include <chrono>

#include "MediaCardRenderer.hpp"
#include "../media/MediaSessionWatcher.hpp"

namespace lumn::ui
{

class MediaCardWindow
{
public:
    MediaCardWindow();
    ~MediaCardWindow();

    MediaCardWindow(const MediaCardWindow&) = delete;
    MediaCardWindow& operator=(const MediaCardWindow&) = delete;

    bool Initialize(HINSTANCE hInstance, std::shared_ptr<media::MediaSessionWatcher> watcher);
    void Shutdown();

    void Show();
    void Hide();
    void ToggleVisibility();
    bool IsVisible() const noexcept { return m_visible; }

    // Phase 4B.1: Liquid Glass Mode Toggle
    void SetLiquidGlassEnabled(bool enabled) noexcept { m_renderer.SetLiquidGlassEnabled(enabled); }
    bool IsLiquidGlassEnabled() const noexcept { return m_renderer.IsLiquidGlassEnabled(); }
    void ToggleLiquidGlass() noexcept { m_renderer.ToggleLiquidGlass(); }

    // Renders the media card frame if visible
    void Render(double currentTimeSec);

    // Process Win32 message queue for this window
    bool ProcessMessages();

    HWND GetHandle() const noexcept { return m_hWnd; }

    static constexpr UINT WM_TRAYICON = WM_APP + 1;

    enum TrayCommandId : UINT
    {
        ID_TRAY_TOGGLE_HALO = 2001,
        ID_TRAY_PLAY_PAUSE  = 2002,
        ID_TRAY_PREV        = 2003,
        ID_TRAY_NEXT        = 2004,
        ID_TRAY_SETTINGS    = 2005,
        ID_TRAY_STARTUP     = 2006,
        ID_TRAY_ABOUT       = 2007,
        ID_TRAY_QUIT        = 2008
    };

    bool IsStartupEnabled() const;
    bool SetStartupEnabled(bool enable);

private:
    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    bool InitializeTray();
    void RemoveTray();
    void ShowContextMenu();

    HWND m_hWnd = nullptr;
    HINSTANCE m_hInstance = nullptr;
    bool m_visible = false;
    bool m_mouseTracking = false;

    // Windows Notification Area System Tray State
    NOTIFYICONDATAW m_nid = {};
    bool m_trayAdded = false;
    bool m_isMenuOpen = false;
    HICON m_trayIcon = nullptr;
    UINT m_wmTaskbarCreated = 0;

    std::shared_ptr<media::MediaSessionWatcher> m_watcher;
    MediaCardRenderer m_renderer;

    CardInteractiveElement m_hoveredElement = CardInteractiveElement::None;
    CardInteractiveElement m_pressedElement = CardInteractiveElement::None;
    bool m_isScrubbing = false;
    double m_scrubFraction = 0.0;

    // Optimistic playback state feedback and reconciliation (Phase 4A.1)
    bool m_hasOptimisticState = false;
    bool m_optimisticIsPlaying = false;
    std::chrono::steady_clock::time_point m_optimisticTimestamp{};

    // Optimistic seek state feedback and reconciliation
    bool m_hasOptimisticSeek = false;
    double m_optimisticSeekFraction = 0.0;
    std::chrono::steady_clock::time_point m_optimisticSeekTimestamp{};
};

} // namespace lumn::ui

