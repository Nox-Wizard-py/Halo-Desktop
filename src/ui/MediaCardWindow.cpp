#include "MediaCardWindow.hpp"
#include <dwmapi.h>
#include <iostream>

namespace lumn::ui
{

MediaCardWindow::MediaCardWindow() = default;

MediaCardWindow::~MediaCardWindow()
{
    Shutdown();
}

bool MediaCardWindow::Initialize(HINSTANCE hInstance, std::shared_ptr<media::MediaSessionWatcher> watcher)
{
    m_hInstance = hInstance;
    m_watcher = std::move(watcher);

    const wchar_t CLASS_NAME[] = L"LumnMediaCardClass";

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    RegisterClassExW(&wc);

    // Compute placement centered above taskbar work area
    RECT workArea = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);

    int cardW = static_cast<int>(MediaCardRenderer::CARD_WIDTH);
    int cardH = static_cast<int>(MediaCardRenderer::CARD_HEIGHT);

    int posX = workArea.left + ((workArea.right - workArea.left) - cardW) / 2;
    int posY = workArea.bottom - cardH - 48; // 48px above taskbar

    // Topmost, toolwindow (no taskbar item), non-activating, DirectComposition presentation
    DWORD exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP;
    DWORD style = WS_POPUP;

    m_hWnd = CreateWindowExW(
        exStyle,
        CLASS_NAME,
        L"Lumn Media Card",
        style,
        posX,
        posY,
        cardW,
        cardH,
        nullptr,
        nullptr,
        hInstance,
        this
    );

    if (!m_hWnd)
    {
        return false;
    }

    // Disable DWM window frame rounding and disable system window border
    // (The media card manages its own custom rounded glass geometry and anti-aliasing via DirectComposition)
    int cornerPref = 1; // DWMWCP_DONOTROUND (do not apply Windows 11 system frame or 8px corners)
    DwmSetWindowAttribute(m_hWnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &cornerPref, sizeof(cornerPref));

    COLORREF borderColor = 0xFFFFFFFE; // DWMWA_COLOR_NONE (suppress Windows 11 system border)
    DwmSetWindowAttribute(m_hWnd, 34 /* DWMWA_BORDER_COLOR */, &borderColor, sizeof(borderColor));

    if (!m_renderer.Initialize(m_hWnd))
    {
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
        return false;
    }

    // Register global toggle hotkeys:
    // Ctrl + Shift + M: Toggle Window Visibility
    // Ctrl + Shift + G: Toggle Liquid Glass vs Phase 4B Procedural Glass
    RegisterHotKey(m_hWnd, 101, MOD_CONTROL | MOD_SHIFT, 'M');
    RegisterHotKey(m_hWnd, 102, MOD_CONTROL | MOD_SHIFT, 'G');

    return true;
}

void MediaCardWindow::Shutdown()
{
    if (m_hWnd)
    {
        UnregisterHotKey(m_hWnd, 101);
        UnregisterHotKey(m_hWnd, 102);
        m_renderer.Shutdown();
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }
}

void MediaCardWindow::Show()
{
    if (m_hWnd)
    {
        ShowWindow(m_hWnd, SW_SHOWNOACTIVATE);
        m_visible = true;
    }
}

void MediaCardWindow::Hide()
{
    if (m_hWnd)
    {
        ShowWindow(m_hWnd, SW_HIDE);
        m_visible = false;
        m_hoveredElement = CardInteractiveElement::None;
        m_pressedElement = CardInteractiveElement::None;
        m_isScrubbing = false;
        m_hasOptimisticState = false;
    }
}

void MediaCardWindow::ToggleVisibility()
{
    if (m_visible)
    {
        Hide();
    }
    else
    {
        Show();
    }
}

void MediaCardWindow::Render(double currentTimeSec)
{
    if (!m_visible || !m_hWnd || !m_watcher)
    {
        return;
    }

    auto trackInfo = m_watcher->GetCurrentTrackInfo();

    // Reconcile optimistic playback state with authoritative GSMTC state
    if (m_hasOptimisticState)
    {
        auto now = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_optimisticTimestamp).count();

        // If GSMTC state matches what we predicted, or if timeout (2000ms) has elapsed, clear optimistic flag
        if (trackInfo.isPlaying == m_optimisticIsPlaying || elapsedMs > 2000)
        {
            m_hasOptimisticState = false;
        }
    }

    bool effectiveIsPlaying = m_hasOptimisticState ? m_optimisticIsPlaying : trackInfo.isPlaying;

    m_renderer.Render(
        trackInfo,
        m_hoveredElement,
        m_pressedElement,
        m_isScrubbing,
        m_scrubFraction,
        currentTimeSec,
        effectiveIsPlaying
    );
}

bool MediaCardWindow::ProcessMessages()
{
    MSG msg = {};
    while (PeekMessageW(&msg, m_hWnd, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}

LRESULT CALLBACK MediaCardWindow::WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    MediaCardWindow* self = nullptr;
    if (uMsg == WM_NCCREATE)
    {
        CREATESTRUCTW* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<MediaCardWindow*>(pCreate->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<MediaCardWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }

    if (self)
    {
        return self->HandleMessage(hWnd, uMsg, wParam, lParam);
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

LRESULT MediaCardWindow::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_NCHITTEST:
        return HTCLIENT;

    case WM_SETCURSOR:
        if (m_hoveredElement == CardInteractiveElement::ProgressBar ||
            m_hoveredElement == CardInteractiveElement::PreviousButton ||
            m_hoveredElement == CardInteractiveElement::PlayPauseButton ||
            m_hoveredElement == CardInteractiveElement::NextButton)
        {
            SetCursor(LoadCursor(nullptr, IDC_HAND));
            return TRUE;
        }
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        return TRUE;

    case WM_MOUSEMOVE:
    {
        if (!m_mouseTracking)
        {
            TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
            m_mouseTracking = true;
        }

        int x = LOWORD(lParam);
        int y = HIWORD(lParam);
        double fraction = 0.0;
        CardInteractiveElement elem = m_renderer.HitTest(x, y, fraction);

        if (m_isScrubbing)
        {
            m_scrubFraction = fraction;
        }
        m_hoveredElement = elem;
        return 0;
    }

    case WM_MOUSELEAVE:
        m_mouseTracking = false;
        m_hoveredElement = CardInteractiveElement::None;
        if (!m_isScrubbing)
        {
            m_pressedElement = CardInteractiveElement::None;
        }
        return 0;

    case WM_LBUTTONDOWN:
    {
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);
        double fraction = 0.0;
        CardInteractiveElement elem = m_renderer.HitTest(x, y, fraction);

        if (elem == CardInteractiveElement::ProgressBar)
        {
            m_isScrubbing = true;
            m_scrubFraction = fraction;
            m_pressedElement = elem;
            SetCapture(hWnd);
        }
        else if (elem == CardInteractiveElement::PreviousButton ||
                 elem == CardInteractiveElement::PlayPauseButton ||
                 elem == CardInteractiveElement::NextButton)
        {
            m_pressedElement = elem;
            SetCapture(hWnd);
        }
        else if (elem == CardInteractiveElement::CardBody)
        {
            // Allow smooth window dragging by clicking anywhere on the card body
            ReleaseCapture();
            SendMessageW(hWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        }
        return 0;
    }

    case WM_LBUTTONUP:
    {
        ReleaseCapture();
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);
        double fraction = 0.0;
        CardInteractiveElement elem = m_renderer.HitTest(x, y, fraction);

        if (m_isScrubbing)
        {
            m_isScrubbing = false;
            if (m_watcher)
            {
                m_watcher->SeekTo(m_scrubFraction);
            }
        }
        else if (m_pressedElement != CardInteractiveElement::None && elem == m_pressedElement)
        {
            if (m_watcher)
            {
                if (elem == CardInteractiveElement::PreviousButton)
                {
                    m_watcher->PreviousTrack();
                }
                else if (elem == CardInteractiveElement::PlayPauseButton)
                {
                    auto trackInfo = m_watcher->GetCurrentTrackInfo();
                    bool currentPlaying = m_hasOptimisticState ? m_optimisticIsPlaying : trackInfo.isPlaying;
                    bool desiredPlaying = !currentPlaying;

                    // Immediately update optimistic state so button icon reflects intent on current frame
                    m_hasOptimisticState = true;
                    m_optimisticIsPlaying = desiredPlaying;
                    m_optimisticTimestamp = std::chrono::steady_clock::now();

                    if (desiredPlaying)
                    {
                        m_watcher->PlayTrack();
                    }
                    else
                    {
                        m_watcher->PauseTrack();
                    }
                }
                else if (elem == CardInteractiveElement::NextButton)
                {
                    m_watcher->NextTrack();
                }
            }
        }

        m_pressedElement = CardInteractiveElement::None;
        return 0;
    }

    case WM_HOTKEY:
        if (wParam == 101) // Ctrl + Shift + M: Toggle Window Visibility
        {
            ToggleVisibility();
            return 0;
        }
        else if (wParam == 102) // Ctrl + Shift + G: Toggle Liquid Glass vs Phase 4B Glass
        {
            ToggleLiquidGlass();
            return 0;
        }
        break;

    case WM_DESTROY:
        m_hWnd = nullptr;
        return 0;
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

} // namespace lumn::ui

