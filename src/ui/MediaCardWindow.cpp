#include "MediaCardWindow.hpp"
#include "resource.h"
#include <dwmapi.h>
#include <iostream>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "advapi32.lib")

namespace
{

void EnableDarkModeForMenu(HWND hWnd)
{
    HMODULE hUxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (hUxtheme)
    {
        using fnSetPreferredAppMode = int (WINAPI *)(int);
        auto pSetPreferredAppMode = reinterpret_cast<fnSetPreferredAppMode>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135)));
        if (pSetPreferredAppMode)
        {
            pSetPreferredAppMode(2); // 2 = ForceDark
        }
        FreeLibrary(hUxtheme);
    }

    HMODULE hUx = GetModuleHandleW(L"uxtheme.dll");
    if (hUx)
    {
        using fnSetWindowTheme = HRESULT (WINAPI *)(HWND, LPCWSTR, LPCWSTR);
        auto pSetWindowTheme = reinterpret_cast<fnSetWindowTheme>(GetProcAddress(hUx, "SetWindowTheme"));
        if (pSetWindowTheme)
        {
            pSetWindowTheme(hWnd, L"DarkMode_Explorer", nullptr);
        }
    }
}

HICON CreateHaloTrayIcon()
{
    const int size = GetSystemMetrics(SM_CXSMICON);
    int width = size > 0 ? size : 16;
    int height = width;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    uint32_t* pixels = nullptr;
    HDC hdcScreen = GetDC(nullptr);
    HBITMAP hColor = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
    ReleaseDC(nullptr, hdcScreen);

    if (!hColor || !pixels)
    {
        return LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    }

    float cx = (width - 1.0f) * 0.5f;
    float cy = (height - 1.0f) * 0.5f;
    float outerR = cx - 1.0f;
    float innerR = outerR * 0.55f;

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            float dx = x - cx;
            float dy = y - cy;
            float dist = std::sqrt(dx * dx + dy * dy);

            // Halo ring shape: smooth anti-aliased ring between innerR and outerR
            float alpha = 0.0f;
            if (dist <= outerR + 1.0f && dist >= innerR - 1.0f)
            {
                float dEdgeOuter = outerR - dist;
                float dEdgeInner = dist - innerR;
                float dMin = (std::min)(dEdgeOuter, dEdgeInner);
                alpha = std::clamp(dMin + 1.0f, 0.0f, 1.0f);
            }
            // Small center spindle dot
            if (dist <= 1.5f)
            {
                alpha = (std::max)(alpha, std::clamp(1.5f - dist + 0.5f, 0.0f, 1.0f));
            }

            if (alpha > 0.0f)
            {
                uint8_t a = static_cast<uint8_t>(alpha * 255.0f);
                // Vibrant cyan-white halo accent (0x00E5FF)
                uint8_t r = static_cast<uint8_t>(220.0f * alpha);
                uint8_t g = static_cast<uint8_t>(235.0f * alpha);
                uint8_t b = static_cast<uint8_t>(255.0f * alpha);
                pixels[y * width + x] = (static_cast<uint32_t>(a) << 24) |
                                        (static_cast<uint32_t>(r) << 16) |
                                        (static_cast<uint32_t>(g) << 8)  |
                                        static_cast<uint32_t>(b);
            }
            else
            {
                pixels[y * width + x] = 0;
            }
        }
    }

    HBITMAP hMono = CreateBitmap(width, height, 1, 1, nullptr);
    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmColor = hColor;
    ii.hbmMask = hMono;

    HICON hIcon = CreateIconIndirect(&ii);

    DeleteObject(hColor);
    DeleteObject(hMono);

    return hIcon ? hIcon : LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
}

} // namespace

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

    const wchar_t CLASS_NAME[] = L"HaloMediaCardClass";

    HICON hIconBig = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    HICON hIconSm = static_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hIcon = hIconBig;
    wc.hIconSm = hIconSm;
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
        L"Halo Media Card",
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

    if (hIconBig)
    {
        SendMessageW(m_hWnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIconBig));
    }
    if (hIconSm)
    {
        SendMessageW(m_hWnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIconSm));
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

    // Register Explorer taskbar restart notification
    m_wmTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    m_wmActivateInstance = RegisterWindowMessageW(L"HaloDesktop_ActivateInstance_9F8D3B");

    // Enable native dark mode for popup menus
    EnableDarkModeForMenu(m_hWnd);

    // Initialize notification area system tray icon
    InitializeTray();

    // Register global toggle hotkeys:
    // Ctrl + Shift + M: Toggle Window Visibility
    // Ctrl + Shift + G: Toggle Liquid Glass vs Phase 4B Procedural Glass
    RegisterHotKey(m_hWnd, 101, MOD_CONTROL | MOD_SHIFT, 'M');
    RegisterHotKey(m_hWnd, 102, MOD_CONTROL | MOD_SHIFT, 'G');

    return true;
}

void MediaCardWindow::Shutdown()
{
    RemoveTray();

    if (m_hWnd)
    {
        UnregisterHotKey(m_hWnd, 101);
        UnregisterHotKey(m_hWnd, 102);
        m_renderer.Shutdown();
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }
}

bool MediaCardWindow::InitializeTray()
{
    if (m_trayAdded || !m_hWnd)
    {
        return true;
    }

    m_trayIcon = static_cast<HICON>(LoadImageW(
        m_hInstance,
        MAKEINTRESOURCEW(IDI_TRAY_ICON),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR
    ));
    if (!m_trayIcon)
    {
        m_trayIcon = LoadIconW(m_hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    }
    if (!m_trayIcon)
    {
        m_trayIcon = CreateHaloTrayIcon();
    }

    m_nid = {};
    m_nid.cbSize = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd = m_hWnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAYICON;
    m_nid.hIcon = m_trayIcon;
    wcscpy_s(m_nid.szTip, L"Halo Desktop");

    m_trayAdded = Shell_NotifyIconW(NIM_ADD, &m_nid) != FALSE;
    return m_trayAdded;
}

void MediaCardWindow::RemoveTray()
{
    if (m_trayAdded)
    {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_trayAdded = false;
    }
    if (m_trayIcon)
    {
        DestroyIcon(m_trayIcon);
        m_trayIcon = nullptr;
    }
}

void MediaCardWindow::ShowContextMenu()
{
    if (m_isMenuOpen)
    {
        return;
    }
    m_isMenuOpen = true;

    POINT pt = {};
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    HMENU hPlaybackMenu = CreatePopupMenu();

    // Query current playback status for dynamic labels
    bool isPlaying = false;
    if (m_watcher)
    {
        auto trackInfo = m_watcher->GetCurrentTrackInfo();
        isPlaying = m_hasOptimisticState ? m_optimisticIsPlaying : trackInfo.isPlaying;
    }

    AppendMenuW(hPlaybackMenu, MF_STRING, ID_TRAY_PLAY_PAUSE, isPlaying ? L"Pause" : L"Play");
    AppendMenuW(hPlaybackMenu, MF_STRING, ID_TRAY_PREV, L"Previous");
    AppendMenuW(hPlaybackMenu, MF_STRING, ID_TRAY_NEXT, L"Next");

    // Dynamic Show/Hide Media Card label
    std::wstring toggleLabel = m_visible ? L"Hide Media Card" : L"Show Media Card";
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_TOGGLE_HALO, toggleLabel.c_str());
    SetMenuDefaultItem(hMenu, ID_TRAY_TOGGLE_HALO, FALSE); // Bold primary entry

    // Dynamic Show/Hide Plasma Lighting label
    bool isPlasmaActive = m_plasmaVisibleChecker ? m_plasmaVisibleChecker() : false;
    std::wstring plasmaLabel = isPlasmaActive ? L"Hide Plasma Lighting" : L"Show Plasma Lighting";
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_TOGGLE_PLASMA, plasmaLabel.c_str());

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // Playback submenu
    AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hPlaybackMenu), L"Playback");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // Media Card Style submenu
    HMENU hStyleMenu = CreatePopupMenu();
    UINT solidFlags = MF_STRING | (m_renderer.GetCardMaterial() == settings::CardMaterial::Solid ? MF_CHECKED : MF_UNCHECKED);
    UINT glassFlags = MF_STRING | (m_renderer.GetCardMaterial() == settings::CardMaterial::LiquidGlass ? MF_CHECKED : MF_UNCHECKED);
    UINT glassmorphismFlags = MF_STRING | (m_renderer.GetCardMaterial() == settings::CardMaterial::Glassmorphism ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(hStyleMenu, solidFlags, ID_TRAY_MATERIAL_SOLID, L"Solid");
    AppendMenuW(hStyleMenu, glassFlags, ID_TRAY_MATERIAL_LIQUID_GLASS, L"Liquid Glass");
    AppendMenuW(hStyleMenu, glassmorphismFlags, ID_TRAY_MATERIAL_GLASSMORPHISM, L"Glassmorphism");
    AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hStyleMenu), L"Media Card Style");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // Always-On Display submenu
    HMENU hAodMenu = CreatePopupMenu();
    UINT blackFlags = MF_STRING | (m_aodMode == settings::AODMode::Black ? MF_CHECKED : MF_UNCHECKED);
    UINT wpFlags = MF_STRING | (m_aodMode == settings::AODMode::Wallpaper ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(hAodMenu, blackFlags, ID_TRAY_AOD_BLACK, L"AOD · Black");
    AppendMenuW(hAodMenu, wpFlags, ID_TRAY_AOD_WALLPAPER, L"AOD · Current Wallpaper");
    if (m_aodMode != settings::AODMode::None)
    {
        AppendMenuW(hAodMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hAodMenu, MF_STRING, ID_TRAY_AOD_OFF, L"Turn Off AOD");
    }
    AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hAodMenu), L"Always-On Display");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // Settings
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_SETTINGS, L"Settings");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_LIGHTING_STUDIO, L"Lighting Studio");

    // Start with Windows (checkable)
    bool isAutoStart = IsStartupEnabled();
    UINT startFlags = MF_STRING | (isAutoStart ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(hMenu, startFlags, ID_TRAY_STARTUP, L"Start with Windows");

    // About Halo
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_ABOUT, L"About Halo");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // Quit
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_QUIT, L"Quit");

    // Standard Win32 popup menu tracking recipe
    SetForegroundWindow(m_hWnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hWnd, nullptr);
    PostMessageW(m_hWnd, WM_NULL, 0, 0);

    // Drain any redundant context menu messages queued while the popup menu was open
    MSG msg = {};
    while (PeekMessageW(&msg, m_hWnd, WM_CONTEXTMENU, WM_CONTEXTMENU, PM_REMOVE)) {}

    DestroyMenu(hMenu);
    m_isMenuOpen = false;
}

bool MediaCardWindow::IsStartupEnabled() const
{
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        wchar_t buffer[MAX_PATH] = {};
        DWORD bufferSize = sizeof(buffer);
        DWORD type = 0;
        LSTATUS status = RegQueryValueExW(hKey, L"HaloDesktop", nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &bufferSize);
        RegCloseKey(hKey);
        return (status == ERROR_SUCCESS);
    }
    return false;
}

bool MediaCardWindow::SetStartupEnabled(bool enable)
{
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS)
    {
        LSTATUS status = ERROR_SUCCESS;
        if (enable)
        {
            wchar_t exePath[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, exePath, MAX_PATH);
            std::wstring quotedPath = L"\"" + std::wstring(exePath) + L"\"";
            status = RegSetValueExW(
                hKey,
                L"HaloDesktop",
                0,
                REG_SZ,
                reinterpret_cast<const BYTE*>(quotedPath.c_str()),
                static_cast<DWORD>((quotedPath.length() + 1) * sizeof(wchar_t))
            );
        }
        else
        {
            status = RegDeleteValueW(hKey, L"HaloDesktop");
        }
        RegCloseKey(hKey);
        return (status == ERROR_SUCCESS || (!enable && status == ERROR_FILE_NOT_FOUND));
    }
    return false;
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
        m_isDraggingCard = false;
        ReleaseCapture();
        m_hasOptimisticState = false;
        m_hasOptimisticSeek = false;
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

void MediaCardWindow::SetCardMaterial(settings::CardMaterial material)
{
    m_renderer.SetCardMaterial(material);
    if (m_onMaterialChanged)
    {
        m_onMaterialChanged(material);
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

    // Reconcile optimistic seek state during IPC transition
    bool effectiveIsScrubbing = m_isScrubbing;
    double effectiveFraction = m_scrubFraction;

    if (!m_isScrubbing && m_hasOptimisticSeek)
    {
        auto now = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_optimisticSeekTimestamp).count();

        int64_t targetPos100ns = static_cast<int64_t>(m_optimisticSeekFraction * trackInfo.duration100ns);
        int64_t currentPos100ns = trackInfo.GetCurrentPosition100ns(now, effectiveIsPlaying);
        int64_t diff100ns = std::abs(currentPos100ns - targetPos100ns);

        // Reconcile once GSMTC confirms the seek (within 1.5s) or after a 2000ms safety timeout
        if (diff100ns < 15000000LL || elapsedMs > 2000)
        {
            m_hasOptimisticSeek = false;
        }
        else
        {
            // Hold the seek position until authoritative confirmation arrives
            effectiveIsScrubbing = true;
            effectiveFraction = m_optimisticSeekFraction;
        }
    }

    m_renderer.Render(
        trackInfo,
        m_hoveredElement,
        m_pressedElement,
        effectiveIsScrubbing,
        effectiveFraction,
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

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            Hide();
            return 0;
        }
        break;

    case WM_SETCURSOR:
        if (m_hoveredElement == CardInteractiveElement::ProgressBar ||
            m_hoveredElement == CardInteractiveElement::PreviousButton ||
            m_hoveredElement == CardInteractiveElement::PlayPauseButton ||
            m_hoveredElement == CardInteractiveElement::NextButton ||
            m_hoveredElement == CardInteractiveElement::MinimizeButton)
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

        if (m_isDraggingCard)
        {
            POINT pt = {};
            GetCursorPos(&pt);
            SetWindowPos(hWnd, nullptr, pt.x - m_dragOffset.x, pt.y - m_dragOffset.y, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }

        int x = static_cast<short>(LOWORD(lParam));
        int y = static_cast<short>(HIWORD(lParam));
        double fraction = 0.0;
        CardInteractiveElement elem = m_renderer.HitTest(x, y, fraction);

        if (m_isScrubbing)
        {
            D2D1_RECT_F prog = MediaCardRenderer::GetProgressBarRect();
            float fx = static_cast<float>(x) / MediaCardRenderer::CARD_SCALE;
            m_scrubFraction = std::clamp<double>(static_cast<double>(fx - prog.left) / (prog.right - prog.left), 0.0, 1.0);
        }
        m_hoveredElement = elem;
        return 0;
    }

    case WM_MOUSELEAVE:
        m_mouseTracking = false;
        m_hoveredElement = CardInteractiveElement::None;
        if (!m_isScrubbing && !m_isDraggingCard)
        {
            m_pressedElement = CardInteractiveElement::None;
        }
        return 0;

    case WM_LBUTTONDOWN:
    {
        int x = static_cast<short>(LOWORD(lParam));
        int y = static_cast<short>(HIWORD(lParam));
        double fraction = 0.0;
        CardInteractiveElement elem = m_renderer.HitTest(x, y, fraction);

        if (elem == CardInteractiveElement::ProgressBar)
        {
            m_hasOptimisticSeek = false;
            m_isScrubbing = true;
            m_scrubFraction = fraction;
            m_pressedElement = elem;
            SetCapture(hWnd);
        }
        else if (elem == CardInteractiveElement::PreviousButton ||
                 elem == CardInteractiveElement::PlayPauseButton ||
                 elem == CardInteractiveElement::NextButton ||
                 elem == CardInteractiveElement::MinimizeButton)
        {
            m_pressedElement = elem;
            SetCapture(hWnd);
        }
        else if (elem == CardInteractiveElement::CardBody)
        {
            // Non-blocking window dragging: avoids modal Win32 WM_NCLBUTTONDOWN/SC_MOVE loop
            m_isDraggingCard = true;
            m_pressedElement = elem;
            SetCapture(hWnd);
            POINT pt = {};
            GetCursorPos(&pt);
            RECT rc = {};
            GetWindowRect(hWnd, &rc);
            m_dragOffset.x = pt.x - rc.left;
            m_dragOffset.y = pt.y - rc.top;
        }
        return 0;
    }

    case WM_LBUTTONUP:
    {
        bool wasDragging = m_isDraggingCard;
        m_isDraggingCard = false;
        ReleaseCapture();

        if (wasDragging)
        {
            m_pressedElement = CardInteractiveElement::None;
            return 0;
        }

        int x = static_cast<short>(LOWORD(lParam));
        int y = static_cast<short>(HIWORD(lParam));
        double fraction = 0.0;
        CardInteractiveElement elem = m_renderer.HitTest(x, y, fraction);

        if (m_isScrubbing)
        {
            m_isScrubbing = false;
            m_pressedElement = CardInteractiveElement::None;
            m_hasOptimisticSeek = true;
            m_optimisticSeekFraction = m_scrubFraction;
            m_optimisticSeekTimestamp = std::chrono::steady_clock::now();
            if (m_watcher)
            {
                m_watcher->SeekTo(m_scrubFraction);
            }
        }
        else if (m_pressedElement == CardInteractiveElement::MinimizeButton && elem == CardInteractiveElement::MinimizeButton)
        {
            m_pressedElement = CardInteractiveElement::None;
            Hide();
            return 0;
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

    case WM_CLOSE:
        // Hide window instead of destroying process
        Hide();
        return 0;

    case WM_TRAYICON:
    {
        UINT evt = LOWORD(lParam);
        if (evt == WM_LBUTTONUP || evt == NIN_SELECT)
        {
            ToggleVisibility();
            return 0;
        }
        else if (evt == WM_RBUTTONUP || evt == WM_CONTEXTMENU)
        {
            ShowContextMenu();
            return 0;
        }
        break;
    }

    case WM_CONTEXTMENU:
        ShowContextMenu();
        return 0;

    case WM_COMMAND:
    {
        UINT cmdId = LOWORD(wParam);
        switch (cmdId)
        {
        case ID_TRAY_TOGGLE_HALO:
            ToggleVisibility();
            return 0;

        case ID_TRAY_PLAY_PAUSE:
            if (m_watcher)
            {
                auto trackInfo = m_watcher->GetCurrentTrackInfo();
                bool currentPlaying = m_hasOptimisticState ? m_optimisticIsPlaying : trackInfo.isPlaying;
                bool desiredPlaying = !currentPlaying;

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
            return 0;

        case ID_TRAY_PREV:
            if (m_watcher)
            {
                m_watcher->PreviousTrack();
            }
            return 0;

        case ID_TRAY_NEXT:
            if (m_watcher)
            {
                m_watcher->NextTrack();
            }
            return 0;

        case ID_TRAY_AOD_BLACK:
            if (m_onAODModeChanged)
            {
                auto targetMode = (m_aodMode == settings::AODMode::Black) ? settings::AODMode::None : settings::AODMode::Black;
                m_onAODModeChanged(targetMode);
            }
            return 0;

        case ID_TRAY_AOD_WALLPAPER:
            if (m_onAODModeChanged)
            {
                auto targetMode = (m_aodMode == settings::AODMode::Wallpaper) ? settings::AODMode::None : settings::AODMode::Wallpaper;
                m_onAODModeChanged(targetMode);
            }
            return 0;

        case ID_TRAY_AOD_OFF:
            if (m_onAODModeChanged)
            {
                m_onAODModeChanged(settings::AODMode::None);
            }
            return 0;

        case ID_TRAY_MATERIAL_SOLID:
            SetCardMaterial(settings::CardMaterial::Solid);
            return 0;

        case ID_TRAY_MATERIAL_LIQUID_GLASS:
            SetCardMaterial(settings::CardMaterial::LiquidGlass);
            return 0;

        case ID_TRAY_MATERIAL_GLASSMORPHISM:
            SetCardMaterial(settings::CardMaterial::Glassmorphism);
            return 0;

        case ID_TRAY_SETTINGS:
            MessageBoxW(
                m_hWnd,
                L"Halo Desktop Settings\n\n"
                L"Hotkeys:\n"
                L"• Ctrl+Shift+M: Toggle Media Card\n"
                L"• Ctrl+Shift+G: Toggle Liquid Glass Mode\n"
                L"• ESC: Dismiss Active Interface\n"
                L"• Ctrl+Shift+Q: Exit Application\n\n"
                L"Start with Windows can be toggled directly from this tray menu.",
                L"Halo Desktop",
                MB_OK | MB_ICONINFORMATION
            );
            return 0;

        case ID_TRAY_TOGGLE_PLASMA:
            if (m_onTogglePlasma)
            {
                m_onTogglePlasma();
            }
            return 0;

        case ID_TRAY_LIGHTING_STUDIO:
            if (m_onOpenLightingStudio)
            {
                m_onOpenLightingStudio();
            }
            return 0;

        case ID_TRAY_STARTUP:
            SetStartupEnabled(!IsStartupEnabled());
            return 0;

        case ID_TRAY_ABOUT:
            MessageBoxW(
                m_hWnd,
                L"Halo Desktop v1.0\n\n"
                L"Fluid liquid-glass media controller and audio-reactive ambient lighting for Windows.\n\n"
                L"Direct3D 11 & Direct2D native rendering engine.",
                L"About Halo Desktop",
                MB_OK | MB_ICONINFORMATION
            );
            return 0;

        case ID_TRAY_QUIT:
            PostQuitMessage(0);
            return 0;
        }
        break;
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

    if (uMsg == m_wmTaskbarCreated && m_wmTaskbarCreated != 0)
    {
        if (m_trayAdded)
        {
            Shell_NotifyIconW(NIM_ADD, &m_nid);
        }
        return 0;
    }

    if (uMsg == m_wmActivateInstance && m_wmActivateInstance != 0)
    {
        Show();
        SetForegroundWindow(m_hWnd);
        return 0;
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

} // namespace lumn::ui

