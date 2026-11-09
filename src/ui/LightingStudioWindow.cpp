#include "LightingStudioWindow.hpp"
#include "resource.h"
#include <dwmapi.h>
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace
{
constexpr wchar_t LIGHTING_STUDIO_CLASS[] = L"HaloLightingStudioDirect2DClass";
constexpr float PI_F = 3.14159265358979323846f;

D2D1_COLOR_F FloatToD2DColor(const float col[4])
{
    return D2D1::ColorF(col[0], col[1], col[2], col[3]);
}

std::wstring FloatToHex(const float col[4])
{
    int r = std::clamp(static_cast<int>(col[0] * 255.0f + 0.5f), 0, 255);
    int g = std::clamp(static_cast<int>(col[1] * 255.0f + 0.5f), 0, 255);
    int b = std::clamp(static_cast<int>(col[2] * 255.0f + 0.5f), 0, 255);
    wchar_t buf[16] = {};
    swprintf_s(buf, L"#%02X%02X%02X", r, g, b);
    return buf;
}

UINT GetWindowDpi(HWND hWnd)
{
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32)
    {
        using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
        auto fn = reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(hUser32, "GetDpiForWindow"));
        if (fn && hWnd)
        {
            UINT dpi = fn(hWnd);
            if (dpi > 0) return dpi;
        }

        using GetDpiForSystemFn = UINT(WINAPI*)();
        auto fnSys = reinterpret_cast<GetDpiForSystemFn>(GetProcAddress(hUser32, "GetDpiForSystem"));
        if (fnSys)
        {
            UINT dpi = fnSys();
            if (dpi > 0) return dpi;
        }
    }

    HDC hdc = GetDC(nullptr);
    UINT dpi = 96;
    if (hdc)
    {
        dpi = static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSY));
        ReleaseDC(nullptr, hdc);
    }
    return dpi > 0 ? dpi : 96;
}

BOOL AdjustWindowRectDpi(LPRECT prc, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle, UINT dpi)
{
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32)
    {
        using AdjustRectFn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
        auto fn = reinterpret_cast<AdjustRectFn>(GetProcAddress(hUser32, "AdjustWindowRectExForDpi"));
        if (fn)
        {
            return fn(prc, dwStyle, bMenu, dwExStyle, dpi);
        }
    }
    return AdjustWindowRectEx(prc, dwStyle, bMenu, dwExStyle);
}
} // namespace

namespace lumn::ui
{

LightingStudioWindow::LightingStudioWindow() = default;

LightingStudioWindow::~LightingStudioWindow()
{
    Shutdown();
}

void LightingStudioWindow::ScreenPixelToDip(HWND hWnd, LPARAM lParam, float& outX, float& outY) const
{
    float dpiX = 96.0f, dpiY = 96.0f;
    if (m_renderTarget)
    {
        m_renderTarget->GetDpi(&dpiX, &dpiY);
    }
    else
    {
        UINT dpi = GetWindowDpi(hWnd);
        dpiX = dpiY = static_cast<float>(dpi);
    }
    outX = static_cast<float>(static_cast<short>(LOWORD(lParam))) * 96.0f / dpiX;
    outY = static_cast<float>(static_cast<short>(HIWORD(lParam))) * 96.0f / dpiY;
}

bool LightingStudioWindow::Initialize(HINSTANCE hInstance, settings::LightingConfig* config, OnChangedCallback onChanged)
{
    m_hInstance = hInstance;
    m_config = config;
    m_onChanged = std::move(onChanged);

    // Initialize D2D1 & DirectWrite factories
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.GetAddressOf());
    if (FAILED(hr)) return false;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf()));
    if (FAILED(hr)) return false;

    // Register Win32 Window Class
    HICON hIconBig = LoadIconW(m_hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    HICON hIconSm = static_cast<HICON>(LoadImageW(m_hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = m_hInstance;
    wc.hIcon = hIconBig;
    wc.hIconSm = hIconSm;
    wc.lpszClassName = LIGHTING_STUDIO_CLASS;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // Direct2D paints client area

    RegisterClassExW(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    DWORD exStyle = WS_EX_APPWINDOW;

    // Create window initially
    m_hWnd = CreateWindowExW(
        exStyle,
        LIGHTING_STUDIO_CLASS,
        L"Halo Lighting Studio",
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        static_cast<int>(DESIGN_WIDTH), static_cast<int>(DESIGN_HEIGHT),
        nullptr, nullptr, m_hInstance, this
    );

    if (!m_hWnd) return false;

    if (hIconBig)
    {
        SendMessageW(m_hWnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIconBig));
    }
    if (hIconSm)
    {
        SendMessageW(m_hWnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIconSm));
    }

    // Determine actual DPI for the newly created window on its active monitor
    UINT dpi = GetWindowDpi(m_hWnd);
    float scale = static_cast<float>(dpi) / 96.0f;

    int clientW = static_cast<int>(DESIGN_WIDTH * scale + 0.5f);
    int clientH = static_cast<int>(DESIGN_HEIGHT * scale + 0.5f);

    RECT wr = { 0, 0, clientW, clientH };
    AdjustWindowRectDpi(&wr, style, FALSE, exStyle, dpi);

    int winW = wr.right - wr.left;
    int winH = wr.bottom - wr.top;
    int posX = (GetSystemMetrics(SM_CXSCREEN) - winW) / 2;
    int posY = (GetSystemMetrics(SM_CYSCREEN) - winH) / 2;

    SetWindowPos(m_hWnd, nullptr, posX, posY, winW, winH, SWP_NOZORDER | SWP_NOACTIVATE);

    // Enable Windows 11 immersive dark mode and rounded window corners
    BOOL useDarkMode = TRUE;
    DwmSetWindowAttribute(m_hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));

    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(m_hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    m_wheelPixels.resize(WHEEL_SIZE * WHEEL_SIZE);

    return true;
}

void LightingStudioWindow::Shutdown()
{
    DiscardDeviceResources();

    if (m_hWnd)
    {
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }

    m_dwriteFactory.Reset();
    m_d2dFactory.Reset();
}

void LightingStudioWindow::Show()
{
    if (m_hWnd)
    {
        ShowWindow(m_hWnd, SW_SHOWNORMAL);
        SetForegroundWindow(m_hWnd);
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }
}

void LightingStudioWindow::Hide()
{
    if (m_hWnd)
    {
        ShowWindow(m_hWnd, SW_HIDE);
    }
}

bool LightingStudioWindow::IsVisible() const
{
    return m_hWnd && IsWindowVisible(m_hWnd);
}

void LightingStudioWindow::SetArtworkPalette(const media::RimPalette& palette)
{
    m_artworkPalette = palette;
    if (IsVisible())
    {
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }
}

bool LightingStudioWindow::CreateDeviceResources()
{
    if (m_renderTarget) return true;

    RECT rc = {};
    GetClientRect(m_hWnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

    UINT dpi = GetWindowDpi(m_hWnd);
    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        static_cast<float>(dpi),
        static_cast<float>(dpi)
    );

    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(m_hWnd, size);

    HRESULT hr = m_d2dFactory->CreateHwndRenderTarget(rtProps, hwndProps, &m_renderTarget);
    if (FAILED(hr)) return false;

    m_renderTarget->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);

    hr = m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f), &m_brushSolid);
    if (FAILED(hr)) return false;

    // Create typography text formats
    m_dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 15.0f, L"en-us", &m_tfHeader
    );
    m_dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 11.5f, L"en-us", &m_tfSubheader
    );
    m_dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-us", &m_tfLabel
    );
    m_dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-us", &m_tfLabelCenter
    );
    if (m_tfLabelCenter) m_tfLabelCenter->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);

    m_dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 10.5f, L"en-us", &m_tfSmall
    );
    m_dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 10.5f, L"en-us", &m_tfSmallCenter
    );
    if (m_tfSmallCenter) m_tfSmallCenter->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_dwriteFactory->CreateTextFormat(
        L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 10.5f, L"en-us", &m_tfMono
    );

    if (m_tfMono) m_tfMono->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);

    // Create dynamic color wheel bitmap
    D2D1_BITMAP_PROPERTIES bmpProps = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        static_cast<float>(dpi),
        static_cast<float>(dpi)
    );
    hr = m_renderTarget->CreateBitmap(D2D1::SizeU(WHEEL_SIZE, WHEEL_SIZE), bmpProps, &m_wheelBitmap);
    if (FAILED(hr)) return false;

    m_lastWheelBrightness = -1.0f; // Force regeneration
    return true;
}

void LightingStudioWindow::DiscardDeviceResources()
{
    m_wheelBitmap.Reset();
    m_brushSolid.Reset();
    m_tfMono.Reset();
    m_tfSmall.Reset();
    m_tfSmallCenter.Reset();
    m_tfLabelCenter.Reset();
    m_tfLabel.Reset();
    m_tfSubheader.Reset();
    m_tfHeader.Reset();
    m_renderTarget.Reset();
}

void LightingStudioWindow::GenerateWheelBitmap(float brightness)
{
    if (!m_wheelBitmap || std::abs(brightness - m_lastWheelBrightness) < 0.005f) return;

    m_lastWheelBrightness = brightness;
    const float cx = WHEEL_SIZE * 0.5f;
    const float cy = WHEEL_SIZE * 0.5f;
    const float R = WHEEL_RADIUS;

    for (uint32_t y = 0; y < WHEEL_SIZE; ++y)
    {
        for (uint32_t x = 0; x < WHEEL_SIZE; ++x)
        {
            float dx = (static_cast<float>(x) + 0.5f) - cx;
            float dy = (static_cast<float>(y) + 0.5f) - cy;
            float dist = std::sqrt(dx * dx + dy * dy);

            if (dist > R + 0.5f)
            {
                m_wheelPixels[y * WHEEL_SIZE + x] = 0x00000000;
            }
            else
            {
                float alpha = std::clamp(R + 0.5f - dist, 0.0f, 1.0f);
                float sat = std::clamp(dist / R, 0.0f, 1.0f);
                float angle = std::atan2(dy, dx);
                if (angle < 0.0f) angle += 2.0f * PI_F;
                float hue = angle / (2.0f * PI_F);

                auto rgb = media::ColorExtractor::HSBtoRGB(hue, sat, brightness);
                float r = rgb.r * alpha;
                float g = rgb.g * alpha;
                float b = rgb.b * alpha;

                uint8_t aByte = static_cast<uint8_t>(std::clamp(alpha * 255.0f + 0.5f, 0.0f, 255.0f));
                uint8_t rByte = static_cast<uint8_t>(std::clamp(r * 255.0f + 0.5f, 0.0f, 255.0f));
                uint8_t gByte = static_cast<uint8_t>(std::clamp(g * 255.0f + 0.5f, 0.0f, 255.0f));
                uint8_t bByte = static_cast<uint8_t>(std::clamp(b * 255.0f + 0.5f, 0.0f, 255.0f));

                m_wheelPixels[y * WHEEL_SIZE + x] = (static_cast<uint32_t>(aByte) << 24) |
                                                    (static_cast<uint32_t>(rByte) << 16) |
                                                    (static_cast<uint32_t>(gByte) << 8)  |
                                                     static_cast<uint32_t>(bByte);
            }
        }
    }

    D2D1_RECT_U destRect = D2D1::RectU(0, 0, WHEEL_SIZE, WHEEL_SIZE);
    m_wheelBitmap->CopyFromMemory(&destRect, m_wheelPixels.data(), WHEEL_SIZE * sizeof(uint32_t));
}

void LightingStudioWindow::SetActiveSection(int section)
{
    if (section >= 0 && section <= 1 && m_currentSection != section)
    {
        m_currentSection = section;
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }
}

void LightingStudioWindow::SelectCardColorSlot(int index)
{
    if (index >= 0 && index < 3)
    {
        m_cardActiveSlot = index;
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }
}

void LightingStudioWindow::ResetMediaCardDefaults()
{
    if (!m_config) return;
    m_config->mediaCard.ResetToDefaults();
    NotifyChanged();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void LightingStudioWindow::Render()
{
    if (!CreateDeviceResources() || !m_config) return;

    m_renderTarget->BeginDraw();

    // Dark charcoal foundation background
    m_renderTarget->Clear(D2D1::ColorF(0.090f, 0.090f, 0.105f, 1.0f)); // #17171B

    // Header Typography
    m_brushSolid->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
    m_renderTarget->DrawText(L"Lighting Studio", 15, m_tfHeader.Get(), D2D1::RectF(24.0f, 14.0f, 416.0f, 34.0f), m_brushSolid.Get());

    m_brushSolid->SetColor(D2D1::ColorF(0.55f, 0.55f, 0.60f, 1.0f));
    const wchar_t* subTitle = (m_currentSection == 0) ? L"Customize your plasma lighting" : L"Media card material and color appearance";
    m_renderTarget->DrawText(subTitle, static_cast<UINT32>(wcslen(subTitle)), m_tfSubheader.Get(), D2D1::RectF(24.0f, 34.0f, 416.0f, 50.0f), m_brushSolid.Get());

    // Top Section Tabs: [ Plasma Lighting ] [ Media Card ]
    D2D1_ROUNDED_RECT tabTrack = D2D1::RoundedRect(D2D1::RectF(24.0f, 52.0f, 416.0f, 84.0f), 7.0f, 7.0f);
    m_brushSolid->SetColor(D2D1::ColorF(0.13f, 0.13f, 0.15f, 1.0f));
    m_renderTarget->FillRoundedRectangle(&tabTrack, m_brushSolid.Get());
    m_brushSolid->SetColor(D2D1::ColorF(0.18f, 0.18f, 0.22f, 1.0f));
    m_renderTarget->DrawRoundedRectangle(&tabTrack, m_brushSolid.Get(), 1.0f);

    D2D1_ROUNDED_RECT activeTabPill = (m_currentSection == 0)
        ? D2D1::RoundedRect(D2D1::RectF(26.0f, 54.0f, 218.0f, 82.0f), 6.0f, 6.0f)
        : D2D1::RoundedRect(D2D1::RectF(222.0f, 54.0f, 414.0f, 82.0f), 6.0f, 6.0f);

    m_brushSolid->SetColor(D2D1::ColorF(0.24f, 0.25f, 0.30f, 1.0f));
    m_renderTarget->FillRoundedRectangle(&activeTabPill, m_brushSolid.Get());

    m_brushSolid->SetColor((m_currentSection == 0) ? D2D1::ColorF(1.0f, 1.0f, 1.0f) : D2D1::ColorF(0.55f, 0.55f, 0.60f));
    m_renderTarget->DrawText(L"Plasma Lighting", 15, m_tfLabelCenter.Get(), D2D1::RectF(26.0f, 59.0f, 218.0f, 77.0f), m_brushSolid.Get());

    m_brushSolid->SetColor((m_currentSection == 1) ? D2D1::ColorF(1.0f, 1.0f, 1.0f) : D2D1::ColorF(0.55f, 0.55f, 0.60f));
    m_renderTarget->DrawText(L"Media Card", 10, m_tfLabelCenter.Get(), D2D1::RectF(222.0f, 59.0f, 414.0f, 77.0f), m_brushSolid.Get());

    if (m_currentSection == 0)
    {
        RenderPlasmaSection();
    }
    else
    {
        RenderMediaCardSection();
    }

    HRESULT hr = m_renderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
    {
        DiscardDeviceResources();
    }
}

void LightingStudioWindow::RenderPlasmaSection()
{
    // Active color retrieval
    float* activeCol = nullptr;
    if (m_config->colorSource == settings::ColorSourceMode::Solid)
    {
        activeCol = m_config->solidColor;
    }
    else if (m_config->colorSource == settings::ColorSourceMode::Mixed)
    {
        activeCol = (m_activeSlot == 0) ? m_config->color1 :
                    (m_activeSlot == 1 ? m_config->color2 : m_config->color3);
    }
    else
    {
        activeCol = m_artworkPalette.hot;
    }
    auto activeHsb = media::ColorExtractor::RGBtoHSB(activeCol[0], activeCol[1], activeCol[2]);

    GenerateWheelBitmap(activeHsb.b);

    // 2. Color Source 3-Mode Segmented Toggle (Artwork, Mixed, Solid)
    D2D1_ROUNDED_RECT segTrack = D2D1::RoundedRect(D2D1::RectF(24.0f, 92.0f, 416.0f, 122.0f), 7.0f, 7.0f);
    m_brushSolid->SetColor(D2D1::ColorF(0.13f, 0.13f, 0.15f, 1.0f));
    m_renderTarget->FillRoundedRectangle(&segTrack, m_brushSolid.Get());
    m_brushSolid->SetColor(D2D1::ColorF(0.18f, 0.18f, 0.22f, 1.0f));
    m_renderTarget->DrawRoundedRectangle(&segTrack, m_brushSolid.Get(), 1.0f);

    bool isArt = (m_config->colorSource == settings::ColorSourceMode::Artwork);
    bool isMix = (m_config->colorSource == settings::ColorSourceMode::Mixed);
    bool isSol = (m_config->colorSource == settings::ColorSourceMode::Solid);

    D2D1_ROUNDED_RECT activePill = isArt
        ? D2D1::RoundedRect(D2D1::RectF(26.0f, 94.0f, 154.0f, 120.0f), 6.0f, 6.0f)
        : (isMix ? D2D1::RoundedRect(D2D1::RectF(156.0f, 94.0f, 284.0f, 120.0f), 6.0f, 6.0f)
                 : D2D1::RoundedRect(D2D1::RectF(286.0f, 94.0f, 414.0f, 120.0f), 6.0f, 6.0f));

    m_brushSolid->SetColor(D2D1::ColorF(0.24f, 0.25f, 0.30f, 1.0f));
    m_renderTarget->FillRoundedRectangle(&activePill, m_brushSolid.Get());

    // Segment labels
    m_brushSolid->SetColor(isArt ? D2D1::ColorF(1.0f, 1.0f, 1.0f) : D2D1::ColorF(0.55f, 0.55f, 0.60f));
    m_renderTarget->DrawText(L"Artwork", 7, m_tfLabelCenter.Get(), D2D1::RectF(26.0f, 98.0f, 154.0f, 116.0f), m_brushSolid.Get());

    m_brushSolid->SetColor(isMix ? D2D1::ColorF(1.0f, 1.0f, 1.0f) : D2D1::ColorF(0.55f, 0.55f, 0.60f));
    m_renderTarget->DrawText(L"Mixed", 5, m_tfLabelCenter.Get(), D2D1::RectF(156.0f, 98.0f, 284.0f, 116.0f), m_brushSolid.Get());

    m_brushSolid->SetColor(isSol ? D2D1::ColorF(1.0f, 1.0f, 1.0f) : D2D1::ColorF(0.55f, 0.55f, 0.60f));
    m_renderTarget->DrawText(L"Solid", 5, m_tfLabelCenter.Get(), D2D1::RectF(286.0f, 98.0f, 414.0f, 116.0f), m_brushSolid.Get());

    // 3. Palette Slots depending on Mode
    if (isArt)
    {
        D2D1_ROUNDED_RECT infoBox = D2D1::RoundedRect(D2D1::RectF(24.0f, 128.0f, 416.0f, 170.0f), 8.0f, 8.0f);
        m_brushSolid->SetColor(D2D1::ColorF(0.12f, 0.12f, 0.14f, 1.0f));
        m_renderTarget->FillRoundedRectangle(&infoBox, m_brushSolid.Get());
        m_brushSolid->SetColor(D2D1::ColorF(0.18f, 0.18f, 0.22f, 1.0f));
        m_renderTarget->DrawRoundedRectangle(&infoBox, m_brushSolid.Get(), 1.0f);

        const float* artColors[4] = { m_artworkPalette.hot, m_artworkPalette.color2, m_artworkPalette.color3, m_artworkPalette.deep };
        for (int i = 0; i < 4; ++i)
        {
            float dotX = 42.0f + i * 18.0f;
            D2D1_ROUNDED_RECT dot = D2D1::RoundedRect(D2D1::RectF(dotX, 142.0f, dotX + 13.0f, 156.0f), 3.0f, 3.0f);
            m_brushSolid->SetColor(FloatToD2DColor(artColors[i]));
            m_renderTarget->FillRoundedRectangle(&dot, m_brushSolid.Get());
        }

        m_brushSolid->SetColor(D2D1::ColorF(0.92f, 0.92f, 0.96f));
        m_renderTarget->DrawText(L"Dynamic Artwork Extraction Active", 33, m_tfLabel.Get(), D2D1::RectF(124.0f, 134.0f, 410.0f, 150.0f), m_brushSolid.Get());

        m_brushSolid->SetColor(D2D1::ColorF(0.55f, 0.55f, 0.60f));
        m_renderTarget->DrawText(L"Synchronized with active media playback", 38, m_tfSmall.Get(), D2D1::RectF(124.0f, 150.0f, 410.0f, 166.0f), m_brushSolid.Get());
    }
    else if (isSol)
    {
        D2D1_ROUNDED_RECT slotBox = D2D1::RoundedRect(D2D1::RectF(140.0f, 128.0f, 300.0f, 170.0f), 8.0f, 8.0f);
        m_brushSolid->SetColor(D2D1::ColorF(0.18f, 0.19f, 0.23f));
        m_renderTarget->FillRoundedRectangle(&slotBox, m_brushSolid.Get());
        m_brushSolid->SetColor(D2D1::ColorF(0.0f, 0.55f, 1.0f, 1.0f));
        m_renderTarget->DrawRoundedRectangle(&slotBox, m_brushSolid.Get(), 1.5f);

        D2D1_ROUNDED_RECT swatch = D2D1::RoundedRect(D2D1::RectF(150.0f, 136.0f, 178.0f, 162.0f), 5.0f, 5.0f);
        m_brushSolid->SetColor(FloatToD2DColor(m_config->solidColor));
        m_renderTarget->FillRoundedRectangle(&swatch, m_brushSolid.Get());
        m_brushSolid->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.40f));
        m_renderTarget->DrawRoundedRectangle(&swatch, m_brushSolid.Get(), 1.0f);

        m_brushSolid->SetColor(D2D1::ColorF(0.95f, 0.95f, 1.0f));
        m_renderTarget->DrawText(L"Solid Color", 11, m_tfLabel.Get(), D2D1::RectF(186.0f, 132.0f, 296.0f, 148.0f), m_brushSolid.Get());

        std::wstring hexStr = FloatToHex(m_config->solidColor);
        m_brushSolid->SetColor(D2D1::ColorF(0.60f, 0.60f, 0.65f));
        m_renderTarget->DrawText(hexStr.c_str(), static_cast<UINT32>(hexStr.length()), m_tfSmall.Get(), D2D1::RectF(186.0f, 148.0f, 296.0f, 164.0f), m_brushSolid.Get());
    }
    else
    {
        const wchar_t* slotTitles[3] = { L"Color 1", L"Color 2", L"Color 3" };
        const float* slotColors[3] = { m_config->color1, m_config->color2, m_config->color3 };

        for (int i = 0; i < 3; ++i)
        {
            float left = 24.0f + i * 134.0f;
            D2D1_ROUNDED_RECT slotBox = D2D1::RoundedRect(D2D1::RectF(left, 128.0f, left + 124.0f, 170.0f), 8.0f, 8.0f);

            bool isSlotSelected = (i == m_activeSlot);
            m_brushSolid->SetColor(isSlotSelected ? D2D1::ColorF(0.18f, 0.19f, 0.23f) : D2D1::ColorF(0.12f, 0.12f, 0.14f));
            m_renderTarget->FillRoundedRectangle(&slotBox, m_brushSolid.Get());

            m_brushSolid->SetColor(isSlotSelected ? D2D1::ColorF(0.0f, 0.55f, 1.0f, 1.0f) : D2D1::ColorF(0.18f, 0.18f, 0.22f));
            m_renderTarget->DrawRoundedRectangle(&slotBox, m_brushSolid.Get(), isSlotSelected ? 1.5f : 1.0f);

            D2D1_ROUNDED_RECT swatch = D2D1::RoundedRect(D2D1::RectF(left + 8.0f, 136.0f, left + 32.0f, 162.0f), 5.0f, 5.0f);
            m_brushSolid->SetColor(FloatToD2DColor(slotColors[i]));
            m_renderTarget->FillRoundedRectangle(&swatch, m_brushSolid.Get());
            m_brushSolid->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.40f));
            m_renderTarget->DrawRoundedRectangle(&swatch, m_brushSolid.Get(), 1.0f);

            m_brushSolid->SetColor(D2D1::ColorF(0.95f, 0.95f, 1.0f));
            m_renderTarget->DrawText(slotTitles[i], static_cast<UINT32>(wcslen(slotTitles[i])), m_tfLabel.Get(), D2D1::RectF(left + 38.0f, 132.0f, left + 122.0f, 148.0f), m_brushSolid.Get());

            std::wstring hexStr = FloatToHex(slotColors[i]);
            m_brushSolid->SetColor(D2D1::ColorF(0.60f, 0.60f, 0.65f));
            m_renderTarget->DrawText(hexStr.c_str(), static_cast<UINT32>(hexStr.length()), m_tfSmall.Get(), D2D1::RectF(left + 38.0f, 148.0f, left + 122.0f, 164.0f), m_brushSolid.Get());
        }
    }

    // 4. Circular Color Wheel
    D2D1_RECT_F wheelDest = D2D1::RectF(
        WHEEL_CENTER_X - WHEEL_RADIUS, WHEEL_CENTER_Y - WHEEL_RADIUS,
        WHEEL_CENTER_X + WHEEL_RADIUS, WHEEL_CENTER_Y + WHEEL_RADIUS
    );
    m_renderTarget->DrawBitmap(m_wheelBitmap.Get(), &wheelDest);

    m_brushSolid->SetColor(D2D1::ColorF(0.25f, 0.25f, 0.30f, 0.8f));
    m_renderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(WHEEL_CENTER_X, WHEEL_CENTER_Y), WHEEL_RADIUS + 0.5f, WHEEL_RADIUS + 0.5f), m_brushSolid.Get(), 1.0f);

    float thumbAngle = activeHsb.h * 2.0f * PI_F;
    float thumbDist = activeHsb.s * WHEEL_RADIUS;
    float tx = WHEEL_CENTER_X + thumbDist * std::cos(thumbAngle);
    float ty = WHEEL_CENTER_Y + thumbDist * std::sin(thumbAngle);

    m_brushSolid->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.55f));
    m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(tx, ty + 1.0f), 8.0f, 8.0f), m_brushSolid.Get());
    m_brushSolid->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
    m_renderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(tx, ty), 7.0f, 7.0f), m_brushSolid.Get(), 2.0f);
    m_brushSolid->SetColor(FloatToD2DColor(activeCol));
    m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(tx, ty), 4.5f, 4.5f), m_brushSolid.Get());

    // 5. Brightness Gradient Slider
    D2D1_ROUNDED_RECT briTrack = D2D1::RoundedRect(D2D1::RectF(100.0f, 316.0f, 340.0f, 322.0f), 3.0f, 3.0f);
    auto pureRgb = media::ColorExtractor::HSBtoRGB(activeHsb.h, activeHsb.s, 1.0f);
    D2D1_GRADIENT_STOP stops[2] = {
        { 0.0f, D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f) },
        { 1.0f, D2D1::ColorF(pureRgb.r, pureRgb.g, pureRgb.b, 1.0f) }
    };
    Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> stopCollection;
    m_renderTarget->CreateGradientStopCollection(stops, 2, &stopCollection);

    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> briBrush;
    if (stopCollection)
    {
        m_renderTarget->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(100.0f, 319.0f), D2D1::Point2F(340.0f, 319.0f)),
            stopCollection.Get(),
            &briBrush
        );
    }
    if (briBrush)
    {
        m_renderTarget->FillRoundedRectangle(&briTrack, briBrush.Get());
    }
    m_brushSolid->SetColor(D2D1::ColorF(0.25f, 0.25f, 0.30f, 0.8f));
    m_renderTarget->DrawRoundedRectangle(&briTrack, m_brushSolid.Get(), 1.0f);

    float briThumbX = 100.0f + std::clamp((activeHsb.b - 0.05f) / 0.95f, 0.0f, 1.0f) * 240.0f;
    m_brushSolid->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.40f));
    m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(briThumbX, 320.0f), 7.5f, 7.5f), m_brushSolid.Get());
    m_brushSolid->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
    m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(briThumbX, 319.0f), 6.5f, 6.5f), m_brushSolid.Get());
    m_brushSolid->SetColor(D2D1::ColorF(0.3f, 0.3f, 0.35f, 1.0f));
    m_renderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(briThumbX, 319.0f), 6.5f, 6.5f), m_brushSolid.Get(), 1.0f);

    // 6. Hex Code Indicator Pill
    D2D1_ROUNDED_RECT hexPill = D2D1::RoundedRect(D2D1::RectF(175.0f, 334.0f, 265.0f, 354.0f), 5.0f, 5.0f);
    m_brushSolid->SetColor(D2D1::ColorF(0.13f, 0.13f, 0.16f, 1.0f));
    m_renderTarget->FillRoundedRectangle(&hexPill, m_brushSolid.Get());
    m_brushSolid->SetColor(D2D1::ColorF(0.20f, 0.20f, 0.24f, 1.0f));
    m_renderTarget->DrawRoundedRectangle(&hexPill, m_brushSolid.Get(), 1.0f);

    if (isArt)
    {
        m_brushSolid->SetColor(D2D1::ColorF(0.60f, 0.60f, 0.65f, 1.0f));
        m_renderTarget->DrawText(L"Artwork", 7, m_tfMono.Get(), D2D1::RectF(175.0f, 337.0f, 265.0f, 351.0f), m_brushSolid.Get());

        D2D1_ROUNDED_RECT veil = D2D1::RoundedRect(D2D1::RectF(80.0f, 176.0f, 360.0f, 358.0f), 10.0f, 10.0f);
        m_brushSolid->SetColor(D2D1::ColorF(0.090f, 0.090f, 0.105f, 0.60f));
        m_renderTarget->FillRoundedRectangle(&veil, m_brushSolid.Get());
    }
    else
    {
        std::wstring hexText = FloatToHex(activeCol);
        m_brushSolid->SetColor(D2D1::ColorF(0.90f, 0.90f, 0.95f, 1.0f));
        m_renderTarget->DrawText(hexText.c_str(), static_cast<UINT32>(hexText.length()), m_tfMono.Get(), D2D1::RectF(175.0f, 337.0f, 265.0f, 351.0f), m_brushSolid.Get());
    }

    // 7. Appearance Card (Thickness, Glow, Speed)
    D2D1_ROUNDED_RECT cardApp = D2D1::RoundedRect(D2D1::RectF(24.0f, 362.0f, 416.0f, 504.0f), 8.0f, 8.0f);
    m_brushSolid->SetColor(D2D1::ColorF(0.12f, 0.12f, 0.14f, 1.0f));
    m_renderTarget->FillRoundedRectangle(&cardApp, m_brushSolid.Get());
    m_brushSolid->SetColor(D2D1::ColorF(0.17f, 0.17f, 0.20f, 1.0f));
    m_renderTarget->DrawRoundedRectangle(&cardApp, m_brushSolid.Get(), 1.0f);

    struct SliderDesc {
        const wchar_t* name;
        float value;
        float y;
    } sliders[3] = {
        { L"Rim Thickness", m_config->thickness, 372.0f },
        { L"Glow Intensity", m_config->glowIntensity, 416.0f },
        { L"Animation Speed", m_config->animationSpeed, 460.0f }
    };

    for (int i = 0; i < 3; ++i)
    {
        float y = sliders[i].y;
        m_brushSolid->SetColor(D2D1::ColorF(0.85f, 0.85f, 0.90f));
        m_renderTarget->DrawText(sliders[i].name, static_cast<UINT32>(wcslen(sliders[i].name)), m_tfLabel.Get(), D2D1::RectF(38.0f, y, 220.0f, y + 16.0f), m_brushSolid.Get());

        wchar_t valBuf[16] = {};
        swprintf_s(valBuf, L"%.1fx", sliders[i].value);
        m_brushSolid->SetColor(D2D1::ColorF(0.0f, 0.65f, 1.0f));
        m_renderTarget->DrawText(valBuf, static_cast<UINT32>(wcslen(valBuf)), m_tfMono.Get(), D2D1::RectF(350.0f, y, 402.0f, y + 16.0f), m_brushSolid.Get());

        float trackY = y + 20.0f;
        D2D1_ROUNDED_RECT trk = D2D1::RoundedRect(D2D1::RectF(38.0f, trackY, 402.0f, trackY + 5.0f), 2.5f, 2.5f);
        m_brushSolid->SetColor(D2D1::ColorF(0.19f, 0.19f, 0.23f));
        m_renderTarget->FillRoundedRectangle(&trk, m_brushSolid.Get());

        float normVal = std::clamp((sliders[i].value - 0.2f) / (3.0f - 0.2f), 0.0f, 1.0f);
        float thumbX = 38.0f + normVal * 364.0f;
        D2D1_ROUNDED_RECT fillTrk = D2D1::RoundedRect(D2D1::RectF(38.0f, trackY, thumbX, trackY + 5.0f), 2.5f, 2.5f);
        m_brushSolid->SetColor(D2D1::ColorF(0.0f, 0.55f, 1.0f));
        m_renderTarget->FillRoundedRectangle(&fillTrk, m_brushSolid.Get());

        m_brushSolid->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.35f));
        m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, trackY + 3.5f), 7.5f, 7.5f), m_brushSolid.Get());
        m_brushSolid->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f));
        m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, trackY + 2.5f), 6.5f, 6.5f), m_brushSolid.Get());
        m_brushSolid->SetColor(D2D1::ColorF(0.4f, 0.4f, 0.45f));
        m_renderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, trackY + 2.5f), 6.5f, 6.5f), m_brushSolid.Get(), 1.0f);
    }

    // 8. Live Rim Gradient Preview Capsule
    m_brushSolid->SetColor(D2D1::ColorF(0.65f, 0.65f, 0.70f));
    m_renderTarget->DrawText(L"Edge Lighting Blend", 19, m_tfSmall.Get(), D2D1::RectF(24.0f, 514.0f, 250.0f, 530.0f), m_brushSolid.Get());

    D2D1_ROUNDED_RECT prevCapsule = D2D1::RoundedRect(D2D1::RectF(24.0f, 530.0f, 416.0f, 554.0f), 12.0f, 12.0f);

    D2D1_GRADIENT_STOP prevStops[4];
    if (isSol)
    {
        prevStops[0] = { 0.00f, FloatToD2DColor(m_config->solidColor) };
        prevStops[1] = { 0.33f, FloatToD2DColor(m_config->solidColor) };
        prevStops[2] = { 0.66f, FloatToD2DColor(m_config->solidColor) };
        prevStops[3] = { 1.00f, FloatToD2DColor(m_config->solidColor) };
    }
    else if (isMix)
    {
        prevStops[0] = { 0.00f, D2D1::ColorF(m_config->color2[0] * 0.35f, m_config->color2[1] * 0.35f, m_config->color2[2] * 0.35f, 1.0f) };
        prevStops[1] = { 0.33f, FloatToD2DColor(m_config->color2) };
        prevStops[2] = { 0.66f, FloatToD2DColor(m_config->color1) };
        prevStops[3] = { 1.00f, FloatToD2DColor(m_config->color3) };
    }
    else
    {
        prevStops[0] = { 0.00f, FloatToD2DColor(m_artworkPalette.deep) };
        prevStops[1] = { 0.33f, FloatToD2DColor(m_artworkPalette.color2) };
        prevStops[2] = { 0.66f, FloatToD2DColor(m_artworkPalette.hot) };
        prevStops[3] = { 1.00f, FloatToD2DColor(m_artworkPalette.color3) };
    }

    Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> prevCollection;
    m_renderTarget->CreateGradientStopCollection(prevStops, 4, &prevCollection);
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> prevBrush;
    if (prevCollection)
    {
        m_renderTarget->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(24.0f, 542.0f), D2D1::Point2F(416.0f, 542.0f)),
            prevCollection.Get(),
            &prevBrush
        );
    }
    if (prevBrush)
    {
        m_renderTarget->FillRoundedRectangle(&prevCapsule, prevBrush.Get());
    }

    D2D1_ROUNDED_RECT coreAura = D2D1::RoundedRect(D2D1::RectF(260.0f, 532.0f, 310.0f, 552.0f), 10.0f, 10.0f);
    m_brushSolid->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.65f));
    m_renderTarget->FillRoundedRectangle(&coreAura, m_brushSolid.Get());

    m_brushSolid->SetColor(D2D1::ColorF(0.35f, 0.35f, 0.40f, 0.8f));
    m_renderTarget->DrawRoundedRectangle(&prevCapsule, m_brushSolid.Get(), 1.0f);

    // 9. Bottom Actions (Reset Defaults / Close)
    m_brushSolid->SetColor(D2D1::ColorF(0.60f, 0.60f, 0.65f));
    m_renderTarget->DrawText(L"Reset to Defaults", 17, m_tfLabel.Get(), D2D1::RectF(24.0f, 574.0f, 160.0f, 604.0f), m_brushSolid.Get());

    D2D1_ROUNDED_RECT btnDone = D2D1::RoundedRect(D2D1::RectF(326.0f, 568.0f, 416.0f, 606.0f), 7.0f, 7.0f);
    m_brushSolid->SetColor(D2D1::ColorF(0.18f, 0.19f, 0.23f));
    m_renderTarget->FillRoundedRectangle(&btnDone, m_brushSolid.Get());
    m_brushSolid->SetColor(D2D1::ColorF(0.25f, 0.26f, 0.32f));
    m_renderTarget->DrawRoundedRectangle(&btnDone, m_brushSolid.Get(), 1.0f);

    m_brushSolid->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f));
    m_renderTarget->DrawText(L"Close", 5, m_tfLabelCenter.Get(), D2D1::RectF(326.0f, 577.0f, 416.0f, 597.0f), m_brushSolid.Get());
}

void LightingStudioWindow::RenderMediaCardSection()
{
    // Active Media Card color retrieval for wheel/slider
    float* activeCol = (m_cardActiveSlot == 0) ? m_config->mediaCard.customColor1 :
                       (m_cardActiveSlot == 1 ? m_config->mediaCard.customColor2 : m_config->mediaCard.customColor3);

    auto activeHsb = media::ColorExtractor::RGBtoHSB(activeCol[0], activeCol[1], activeCol[2]);
    GenerateWheelBitmap(activeHsb.b);

    bool isSolid = (m_config->mediaCard.material == settings::CardMaterial::Solid);
    bool isGlass = (m_config->mediaCard.material == settings::CardMaterial::LiquidGlass);
    bool isGlassmorphism = (m_config->mediaCard.material == settings::CardMaterial::Glassmorphism);

    // 1. Card Material Selector Label
    m_brushSolid->SetColor(D2D1::ColorF(0.85f, 0.85f, 0.90f));
    m_renderTarget->DrawText(L"Card Material", 13, m_tfLabel.Get(), D2D1::RectF(24.0f, 92.0f, 416.0f, 108.0f), m_brushSolid.Get());

    // 3-option Segmented Selector: Solid | Liquid Glass | Glassmorphism
    D2D1_ROUNDED_RECT matTrack = D2D1::RoundedRect(D2D1::RectF(24.0f, 110.0f, 416.0f, 142.0f), 7.0f, 7.0f);
    m_brushSolid->SetColor(D2D1::ColorF(0.13f, 0.13f, 0.15f, 1.0f));
    m_renderTarget->FillRoundedRectangle(&matTrack, m_brushSolid.Get());
    m_brushSolid->SetColor(D2D1::ColorF(0.18f, 0.18f, 0.22f, 1.0f));
    m_renderTarget->DrawRoundedRectangle(&matTrack, m_brushSolid.Get(), 1.0f);

    if (isSolid)
    {
        D2D1_ROUNDED_RECT pill = D2D1::RoundedRect(D2D1::RectF(26.0f, 112.0f, 150.0f, 140.0f), 6.0f, 6.0f);
        m_brushSolid->SetColor(D2D1::ColorF(0.24f, 0.25f, 0.30f, 1.0f));
        m_renderTarget->FillRoundedRectangle(&pill, m_brushSolid.Get());
    }
    else if (isGlass)
    {
        D2D1_ROUNDED_RECT pill = D2D1::RoundedRect(D2D1::RectF(154.0f, 112.0f, 282.0f, 140.0f), 6.0f, 6.0f);
        m_brushSolid->SetColor(D2D1::ColorF(0.24f, 0.25f, 0.30f, 1.0f));
        m_renderTarget->FillRoundedRectangle(&pill, m_brushSolid.Get());
    }
    else if (isGlassmorphism)
    {
        D2D1_ROUNDED_RECT pill = D2D1::RoundedRect(D2D1::RectF(286.0f, 112.0f, 414.0f, 140.0f), 6.0f, 6.0f);
        m_brushSolid->SetColor(D2D1::ColorF(0.24f, 0.25f, 0.30f, 1.0f));
        m_renderTarget->FillRoundedRectangle(&pill, m_brushSolid.Get());
    }

    // Material Option Labels
    m_brushSolid->SetColor(isSolid ? D2D1::ColorF(1.0f, 1.0f, 1.0f) : D2D1::ColorF(0.55f, 0.55f, 0.60f));
    m_renderTarget->DrawText(L"Solid", 5, m_tfLabelCenter.Get(), D2D1::RectF(26.0f, 117.0f, 150.0f, 135.0f), m_brushSolid.Get());

    m_brushSolid->SetColor(isGlass ? D2D1::ColorF(1.0f, 1.0f, 1.0f) : D2D1::ColorF(0.55f, 0.55f, 0.60f));
    m_renderTarget->DrawText(L"Liquid Glass", 12, m_tfLabelCenter.Get(), D2D1::RectF(154.0f, 117.0f, 282.0f, 135.0f), m_brushSolid.Get());

    m_brushSolid->SetColor(isGlassmorphism ? D2D1::ColorF(1.0f, 1.0f, 1.0f) : D2D1::ColorF(0.55f, 0.55f, 0.60f));
    m_renderTarget->DrawText(L"Glassmorphism", 13, m_tfLabelCenter.Get(), D2D1::RectF(286.0f, 117.0f, 414.0f, 135.0f), m_brushSolid.Get());

    if (isGlass)
    {
        // Liquid Glass info card
        D2D1_ROUNDED_RECT infoBox = D2D1::RoundedRect(D2D1::RectF(24.0f, 154.0f, 416.0f, 218.0f), 8.0f, 8.0f);
        m_brushSolid->SetColor(D2D1::ColorF(0.12f, 0.12f, 0.14f, 1.0f));
        m_renderTarget->FillRoundedRectangle(&infoBox, m_brushSolid.Get());
        m_brushSolid->SetColor(D2D1::ColorF(0.18f, 0.18f, 0.22f, 1.0f));
        m_renderTarget->DrawRoundedRectangle(&infoBox, m_brushSolid.Get(), 1.0f);

        m_brushSolid->SetColor(D2D1::ColorF(0.92f, 0.92f, 0.96f));
        m_renderTarget->DrawText(L"Liquid Glass Material Active", 28, m_tfLabel.Get(), D2D1::RectF(38.0f, 162.0f, 402.0f, 178.0f), m_brushSolid.Get());

        m_brushSolid->SetColor(D2D1::ColorF(0.55f, 0.55f, 0.60f));
        m_renderTarget->DrawText(L"Hardware-accelerated real-time backdrop refraction, chromatic dispersion and ambient lighting are active. Switch to Solid above to customize colors.",
            145, m_tfSmall.Get(), D2D1::RectF(38.0f, 180.0f, 402.0f, 212.0f), m_brushSolid.Get());

        // Dimmed color wheel & brightness slider
        D2D1_RECT_F wheelDest = D2D1::RectF(
            WHEEL_CENTER_X - CARD_WHEEL_RADIUS, CARD_WHEEL_CENTER_Y - CARD_WHEEL_RADIUS,
            WHEEL_CENTER_X + CARD_WHEEL_RADIUS, CARD_WHEEL_CENTER_Y + CARD_WHEEL_RADIUS
        );
        m_renderTarget->DrawBitmap(m_wheelBitmap.Get(), &wheelDest);
        D2D1_ROUNDED_RECT veil = D2D1::RoundedRect(D2D1::RectF(80.0f, 256.0f, 360.0f, 436.0f), 10.0f, 10.0f);
        m_brushSolid->SetColor(D2D1::ColorF(0.090f, 0.090f, 0.105f, 0.70f));
        m_renderTarget->FillRoundedRectangle(&veil, m_brushSolid.Get());

        m_brushSolid->SetColor(D2D1::ColorF(0.60f, 0.60f, 0.65f));
        m_renderTarget->DrawText(L"Colour customization is active in Solid mode", 44, m_tfSmallCenter.Get(), D2D1::RectF(80.0f, 336.0f, 360.0f, 356.0f), m_brushSolid.Get());
    }
    else if (isGlassmorphism)
    {
        // Glassmorphism info card
        D2D1_ROUNDED_RECT infoBox = D2D1::RoundedRect(D2D1::RectF(24.0f, 154.0f, 416.0f, 218.0f), 8.0f, 8.0f);
        m_brushSolid->SetColor(D2D1::ColorF(0.12f, 0.12f, 0.14f, 1.0f));
        m_renderTarget->FillRoundedRectangle(&infoBox, m_brushSolid.Get());
        m_brushSolid->SetColor(D2D1::ColorF(0.18f, 0.18f, 0.22f, 1.0f));
        m_renderTarget->DrawRoundedRectangle(&infoBox, m_brushSolid.Get(), 1.0f);

        m_brushSolid->SetColor(D2D1::ColorF(0.92f, 0.92f, 0.96f));
        m_renderTarget->DrawText(L"Glassmorphism Material Active", 29, m_tfLabel.Get(), D2D1::RectF(38.0f, 162.0f, 402.0f, 178.0f), m_brushSolid.Get());

        m_brushSolid->SetColor(D2D1::ColorF(0.55f, 0.55f, 0.60f));
        m_renderTarget->DrawText(L"Frosted translucent surface with real-time backdrop blur, neutral charcoal tint and soft edge depth. Media content remains sharp and unaffected.",
            140, m_tfSmall.Get(), D2D1::RectF(38.0f, 180.0f, 402.0f, 212.0f), m_brushSolid.Get());

        // Dimmed color wheel & brightness slider
        D2D1_RECT_F wheelDest = D2D1::RectF(
            WHEEL_CENTER_X - CARD_WHEEL_RADIUS, CARD_WHEEL_CENTER_Y - CARD_WHEEL_RADIUS,
            WHEEL_CENTER_X + CARD_WHEEL_RADIUS, CARD_WHEEL_CENTER_Y + CARD_WHEEL_RADIUS
        );
        m_renderTarget->DrawBitmap(m_wheelBitmap.Get(), &wheelDest);
        D2D1_ROUNDED_RECT veil = D2D1::RoundedRect(D2D1::RectF(80.0f, 256.0f, 360.0f, 436.0f), 10.0f, 10.0f);
        m_brushSolid->SetColor(D2D1::ColorF(0.090f, 0.090f, 0.105f, 0.70f));
        m_renderTarget->FillRoundedRectangle(&veil, m_brushSolid.Get());

        m_brushSolid->SetColor(D2D1::ColorF(0.60f, 0.60f, 0.65f));
        m_renderTarget->DrawText(L"Frosted backdrop blur active", 28, m_tfSmallCenter.Get(), D2D1::RectF(80.0f, 336.0f, 360.0f, 356.0f), m_brushSolid.Get());
    }
    else
    {
        bool isDyn = (m_config->mediaCard.solidColorMode == settings::SolidColorMode::DynamicArtwork);
        bool isMix = (m_config->mediaCard.solidColorMode == settings::SolidColorMode::MixCustom);

        // 2. Solid Colour Configuration Label
        m_brushSolid->SetColor(D2D1::ColorF(0.85f, 0.85f, 0.90f));
        m_renderTarget->DrawText(L"Solid Colour Configuration", 26, m_tfLabel.Get(), D2D1::RectF(24.0f, 150.0f, 416.0f, 166.0f), m_brushSolid.Get());

        // 2-option Segmented Mode Selector: Dynamic Artwork Colour | Mix Custom Colours
        D2D1_ROUNDED_RECT modeTrack = D2D1::RoundedRect(D2D1::RectF(24.0f, 168.0f, 416.0f, 200.0f), 7.0f, 7.0f);
        m_brushSolid->SetColor(D2D1::ColorF(0.13f, 0.13f, 0.15f, 1.0f));
        m_renderTarget->FillRoundedRectangle(&modeTrack, m_brushSolid.Get());
        m_brushSolid->SetColor(D2D1::ColorF(0.18f, 0.18f, 0.22f, 1.0f));
        m_renderTarget->DrawRoundedRectangle(&modeTrack, m_brushSolid.Get(), 1.0f);

        D2D1_ROUNDED_RECT activeModePill = isDyn
            ? D2D1::RoundedRect(D2D1::RectF(26.0f, 170.0f, 218.0f, 198.0f), 6.0f, 6.0f)
            : D2D1::RoundedRect(D2D1::RectF(222.0f, 170.0f, 414.0f, 198.0f), 6.0f, 6.0f);

        m_brushSolid->SetColor(D2D1::ColorF(0.24f, 0.25f, 0.30f, 1.0f));
        m_renderTarget->FillRoundedRectangle(&activeModePill, m_brushSolid.Get());

        m_brushSolid->SetColor(isDyn ? D2D1::ColorF(1.0f, 1.0f, 1.0f) : D2D1::ColorF(0.55f, 0.55f, 0.60f));
        m_renderTarget->DrawText(L"Dynamic Artwork Colour", 22, m_tfLabelCenter.Get(), D2D1::RectF(26.0f, 175.0f, 218.0f, 193.0f), m_brushSolid.Get());

        m_brushSolid->SetColor(isMix ? D2D1::ColorF(1.0f, 1.0f, 1.0f) : D2D1::ColorF(0.55f, 0.55f, 0.60f));
        m_renderTarget->DrawText(L"Mix Custom Colours", 18, m_tfLabelCenter.Get(), D2D1::RectF(222.0f, 175.0f, 414.0f, 193.0f), m_brushSolid.Get());

        if (isDyn)
        {
            // Dynamic Artwork Mode Info Card
            D2D1_ROUNDED_RECT dynCard = D2D1::RoundedRect(D2D1::RectF(24.0f, 206.0f, 416.0f, 248.0f), 8.0f, 8.0f);
            m_brushSolid->SetColor(D2D1::ColorF(0.12f, 0.12f, 0.14f, 1.0f));
            m_renderTarget->FillRoundedRectangle(&dynCard, m_brushSolid.Get());
            m_brushSolid->SetColor(D2D1::ColorF(0.18f, 0.18f, 0.22f, 1.0f));
            m_renderTarget->DrawRoundedRectangle(&dynCard, m_brushSolid.Get(), 1.0f);

            // Calculate derived M3 container color from artwork hot
            const float* dominant = m_artworkPalette.hot;
            auto hsb = media::ColorExtractor::RGBtoHSB(dominant[0], dominant[1], dominant[2]);
            float sat = std::clamp(hsb.s * 0.40f, 0.08f, 0.30f);
            float bri = 0.11f + (1.0f - sat) * 0.04f;
            auto rgb = media::ColorExtractor::HSBtoRGB(hsb.h, sat, bri);

            D2D1_ROUNDED_RECT dot = D2D1::RoundedRect(D2D1::RectF(38.0f, 218.0f, 54.0f, 234.0f), 4.0f, 4.0f);
            m_brushSolid->SetColor(D2D1::ColorF(rgb.r, rgb.g, rgb.b, 1.0f));
            m_renderTarget->FillRoundedRectangle(&dot, m_brushSolid.Get());
            m_brushSolid->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.30f));
            m_renderTarget->DrawRoundedRectangle(&dot, m_brushSolid.Get(), 1.0f);

            m_brushSolid->SetColor(D2D1::ColorF(0.92f, 0.92f, 0.96f));
            m_renderTarget->DrawText(L"Dynamic Artwork Tone Active", 27, m_tfLabel.Get(), D2D1::RectF(64.0f, 212.0f, 402.0f, 228.0f), m_brushSolid.Get());

            m_brushSolid->SetColor(D2D1::ColorF(0.55f, 0.55f, 0.60f));
            m_renderTarget->DrawText(L"Material 3 surface container derived with >= 10:1 contrast", 58, m_tfSmall.Get(), D2D1::RectF(64.0f, 228.0f, 402.0f, 244.0f), m_brushSolid.Get());

            // Dimmed wheel & brightness slider
            D2D1_RECT_F wheelDest = D2D1::RectF(
                WHEEL_CENTER_X - CARD_WHEEL_RADIUS, CARD_WHEEL_CENTER_Y - CARD_WHEEL_RADIUS,
                WHEEL_CENTER_X + CARD_WHEEL_RADIUS, CARD_WHEEL_CENTER_Y + CARD_WHEEL_RADIUS
            );
            m_renderTarget->DrawBitmap(m_wheelBitmap.Get(), &wheelDest);
            D2D1_ROUNDED_RECT veil = D2D1::RoundedRect(D2D1::RectF(80.0f, 256.0f, 360.0f, 436.0f), 10.0f, 10.0f);
            m_brushSolid->SetColor(D2D1::ColorF(0.090f, 0.090f, 0.105f, 0.70f));
            m_renderTarget->FillRoundedRectangle(&veil, m_brushSolid.Get());

            m_brushSolid->SetColor(D2D1::ColorF(0.60f, 0.60f, 0.65f));
            m_renderTarget->DrawText(L"Synchronized with active media playback", 38, m_tfSmallCenter.Get(), D2D1::RectF(80.0f, 336.0f, 360.0f, 356.0f), m_brushSolid.Get());
        }
        else
        {
            // Mix Custom Mode: Row of 3 custom color slots
            const wchar_t* slotTitles[3] = { L"Colour 1", L"Colour 2", L"Colour 3" };
            const float* slotColors[3] = {
                m_config->mediaCard.customColor1,
                m_config->mediaCard.customColor2,
                m_config->mediaCard.customColor3
            };

            for (int i = 0; i < 3; ++i)
            {
                float left = 24.0f + i * 134.0f;
                D2D1_ROUNDED_RECT slotBox = D2D1::RoundedRect(D2D1::RectF(left, 206.0f, left + 124.0f, 248.0f), 8.0f, 8.0f);

                bool isSelected = (i == m_cardActiveSlot);
                m_brushSolid->SetColor(isSelected ? D2D1::ColorF(0.18f, 0.19f, 0.23f) : D2D1::ColorF(0.12f, 0.12f, 0.14f));
                m_renderTarget->FillRoundedRectangle(&slotBox, m_brushSolid.Get());

                m_brushSolid->SetColor(isSelected ? D2D1::ColorF(0.0f, 0.55f, 1.0f, 1.0f) : D2D1::ColorF(0.18f, 0.18f, 0.22f));
                m_renderTarget->DrawRoundedRectangle(&slotBox, m_brushSolid.Get(), isSelected ? 1.5f : 1.0f);

                // Swatch Pill
                D2D1_ROUNDED_RECT swatch = D2D1::RoundedRect(D2D1::RectF(left + 8.0f, 214.0f, left + 30.0f, 236.0f), 5.0f, 5.0f);
                m_brushSolid->SetColor(FloatToD2DColor(slotColors[i]));
                m_renderTarget->FillRoundedRectangle(&swatch, m_brushSolid.Get());
                m_brushSolid->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.40f));
                m_renderTarget->DrawRoundedRectangle(&swatch, m_brushSolid.Get(), 1.0f);

                // Title & Hex
                m_brushSolid->SetColor(D2D1::ColorF(0.95f, 0.95f, 1.0f));
                m_renderTarget->DrawText(slotTitles[i], static_cast<UINT32>(wcslen(slotTitles[i])), m_tfLabel.Get(), D2D1::RectF(left + 36.0f, 210.0f, left + 122.0f, 226.0f), m_brushSolid.Get());

                std::wstring hexStr = FloatToHex(slotColors[i]);
                m_brushSolid->SetColor(D2D1::ColorF(0.60f, 0.60f, 0.65f));
                m_renderTarget->DrawText(hexStr.c_str(), static_cast<UINT32>(hexStr.length()), m_tfSmall.Get(), D2D1::RectF(left + 36.0f, 226.0f, left + 122.0f, 242.0f), m_brushSolid.Get());
            }

            // Interactive Color Wheel
            D2D1_RECT_F wheelDest = D2D1::RectF(
                WHEEL_CENTER_X - CARD_WHEEL_RADIUS, CARD_WHEEL_CENTER_Y - CARD_WHEEL_RADIUS,
                WHEEL_CENTER_X + CARD_WHEEL_RADIUS, CARD_WHEEL_CENTER_Y + CARD_WHEEL_RADIUS
            );
            m_renderTarget->DrawBitmap(m_wheelBitmap.Get(), &wheelDest);

            m_brushSolid->SetColor(D2D1::ColorF(0.25f, 0.25f, 0.30f, 0.8f));
            m_renderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(WHEEL_CENTER_X, CARD_WHEEL_CENTER_Y), CARD_WHEEL_RADIUS + 0.5f, CARD_WHEEL_RADIUS + 0.5f), m_brushSolid.Get(), 1.0f);

            // Wheel Thumb
            float thumbAngle = activeHsb.h * 2.0f * PI_F;
            float thumbDist = activeHsb.s * CARD_WHEEL_RADIUS;
            float tx = WHEEL_CENTER_X + thumbDist * std::cos(thumbAngle);
            float ty = CARD_WHEEL_CENTER_Y + thumbDist * std::sin(thumbAngle);

            m_brushSolid->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.55f));
            m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(tx, ty + 1.0f), 8.0f, 8.0f), m_brushSolid.Get());
            m_brushSolid->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
            m_renderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(tx, ty), 7.0f, 7.0f), m_brushSolid.Get(), 2.0f);
            m_brushSolid->SetColor(FloatToD2DColor(activeCol));
            m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(tx, ty), 4.5f, 4.5f), m_brushSolid.Get());

            // Brightness Slider
            D2D1_ROUNDED_RECT briTrack = D2D1::RoundedRect(D2D1::RectF(100.0f, 396.0f, 340.0f, 402.0f), 3.0f, 3.0f);
            auto pureRgb = media::ColorExtractor::HSBtoRGB(activeHsb.h, activeHsb.s, 1.0f);
            D2D1_GRADIENT_STOP stops[2] = {
                { 0.0f, D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f) },
                { 1.0f, D2D1::ColorF(pureRgb.r, pureRgb.g, pureRgb.b, 1.0f) }
            };
            Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> stopCollection;
            m_renderTarget->CreateGradientStopCollection(stops, 2, &stopCollection);

            Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> briBrush;
            if (stopCollection)
            {
                m_renderTarget->CreateLinearGradientBrush(
                    D2D1::LinearGradientBrushProperties(D2D1::Point2F(100.0f, 399.0f), D2D1::Point2F(340.0f, 399.0f)),
                    stopCollection.Get(),
                    &briBrush
                );
            }
            if (briBrush)
            {
                m_renderTarget->FillRoundedRectangle(&briTrack, briBrush.Get());
            }
            m_brushSolid->SetColor(D2D1::ColorF(0.25f, 0.25f, 0.30f, 0.8f));
            m_renderTarget->DrawRoundedRectangle(&briTrack, m_brushSolid.Get(), 1.0f);

            float briThumbX = 100.0f + std::clamp((activeHsb.b - 0.05f) / 0.95f, 0.0f, 1.0f) * 240.0f;
            m_brushSolid->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.40f));
            m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(briThumbX, 400.0f), 7.5f, 7.5f), m_brushSolid.Get());
            m_brushSolid->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
            m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(briThumbX, 399.0f), 6.5f, 6.5f), m_brushSolid.Get());
            m_brushSolid->SetColor(D2D1::ColorF(0.3f, 0.3f, 0.35f, 1.0f));
            m_renderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(briThumbX, 399.0f), 6.5f, 6.5f), m_brushSolid.Get(), 1.0f);

            // Hex Pill
            D2D1_ROUNDED_RECT hexPill = D2D1::RoundedRect(D2D1::RectF(175.0f, 414.0f, 265.0f, 434.0f), 5.0f, 5.0f);
            m_brushSolid->SetColor(D2D1::ColorF(0.13f, 0.13f, 0.16f, 1.0f));
            m_renderTarget->FillRoundedRectangle(&hexPill, m_brushSolid.Get());
            m_brushSolid->SetColor(D2D1::ColorF(0.20f, 0.20f, 0.24f, 1.0f));
            m_renderTarget->DrawRoundedRectangle(&hexPill, m_brushSolid.Get(), 1.0f);

            std::wstring hexText = FloatToHex(activeCol);
            m_brushSolid->SetColor(D2D1::ColorF(0.90f, 0.90f, 0.95f, 1.0f));
            m_renderTarget->DrawText(hexText.c_str(), static_cast<UINT32>(hexText.length()), m_tfMono.Get(), D2D1::RectF(175.0f, 417.0f, 265.0f, 431.0f), m_brushSolid.Get());
        }
    }

    // 4. Live Media Card Preview Capsule
    m_brushSolid->SetColor(D2D1::ColorF(0.65f, 0.65f, 0.70f));
    m_renderTarget->DrawText(L"Media Card Background Preview", 30, m_tfSmall.Get(), D2D1::RectF(24.0f, 444.0f, 250.0f, 460.0f), m_brushSolid.Get());

    D2D1_ROUNDED_RECT prevCard = D2D1::RoundedRect(D2D1::RectF(24.0f, 462.0f, 416.0f, 550.0f), 12.0f, 12.0f);

    if (isSolid)
    {
        if (m_config->mediaCard.solidColorMode == settings::SolidColorMode::DynamicArtwork)
        {
            // Dynamic Artwork container fill
            const float* dominant = m_artworkPalette.hot;
            auto hsb = media::ColorExtractor::RGBtoHSB(dominant[0], dominant[1], dominant[2]);
            float sat = std::clamp(hsb.s * 0.40f, 0.08f, 0.30f);
            float bri = 0.11f + (1.0f - sat) * 0.04f;
            auto rgb = media::ColorExtractor::HSBtoRGB(hsb.h, sat, bri);

            m_brushSolid->SetColor(D2D1::ColorF(rgb.r, rgb.g, rgb.b, 1.0f));
            m_renderTarget->FillRoundedRectangle(&prevCard, m_brushSolid.Get());
        }
        else
        {
            // 3-color custom blend
            D2D1_GRADIENT_STOP stops[3] = {
                { 0.0f, FloatToD2DColor(m_config->mediaCard.customColor1) },
                { 0.5f, FloatToD2DColor(m_config->mediaCard.customColor2) },
                { 1.0f, FloatToD2DColor(m_config->mediaCard.customColor3) }
            };
            Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> stopCollection;
            m_renderTarget->CreateGradientStopCollection(stops, 3, &stopCollection);
            Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> gradBrush;
            if (stopCollection)
            {
                m_renderTarget->CreateLinearGradientBrush(
                    D2D1::LinearGradientBrushProperties(D2D1::Point2F(24.0f, 462.0f), D2D1::Point2F(416.0f, 550.0f)),
                    stopCollection.Get(),
                    &gradBrush
                );
            }
            if (gradBrush)
            {
                m_renderTarget->FillRoundedRectangle(&prevCard, gradBrush.Get());
            }
        }
    }
    else if (isGlass)
    {
        // Liquid Glass replica preview
        m_brushSolid->SetColor(D2D1::ColorF(0.06f, 0.06f, 0.10f, 0.70f));
        m_renderTarget->FillRoundedRectangle(&prevCard, m_brushSolid.Get());

        D2D1_ROUNDED_RECT sheen = D2D1::RoundedRect(D2D1::RectF(25.0f, 463.0f, 415.0f, 500.0f), 11.0f, 11.0f);
        m_brushSolid->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.07f));
        m_renderTarget->FillRoundedRectangle(&sheen, m_brushSolid.Get());
    }
    else if (isGlassmorphism)
    {
        // Glassmorphism frosted preview — dark charcoal tint #1B1C1F at 38% opacity; see-through frosted glass
        m_brushSolid->SetColor(D2D1::ColorF(0.108f, 0.110f, 0.122f, 0.38f));
        m_renderTarget->FillRoundedRectangle(&prevCard, m_brushSolid.Get());

        D2D1_ROUNDED_RECT sheen = D2D1::RoundedRect(D2D1::RectF(25.0f, 463.0f, 415.0f, 495.0f), 11.0f, 11.0f);
        m_brushSolid->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.04f));
        m_renderTarget->FillRoundedRectangle(&sheen, m_brushSolid.Get());
    }

    // Outer border for preview card
    m_brushSolid->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.12f));
    m_renderTarget->DrawRoundedRectangle(&prevCard, m_brushSolid.Get(), 1.0f);

    // Mini UI mockup inside preview card
    // Mini artwork square at left (48x48)
    D2D1_ROUNDED_RECT miniArt = D2D1::RoundedRect(D2D1::RectF(36.0f, 474.0f, 96.0f, 534.0f), 8.0f, 8.0f);
    m_brushSolid->SetColor(FloatToD2DColor(m_artworkPalette.hot));
    m_renderTarget->FillRoundedRectangle(&miniArt, m_brushSolid.Get());
    m_brushSolid->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.20f));
    m_renderTarget->DrawRoundedRectangle(&miniArt, m_brushSolid.Get(), 1.0f);

    // Mini Title Bar
    D2D1_ROUNDED_RECT barTitle = D2D1::RoundedRect(D2D1::RectF(108.0f, 480.0f, 260.0f, 492.0f), 3.0f, 3.0f);
    m_brushSolid->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.85f));
    m_renderTarget->FillRoundedRectangle(&barTitle, m_brushSolid.Get());

    // Mini Artist Bar
    D2D1_ROUNDED_RECT barArtist = D2D1::RoundedRect(D2D1::RectF(108.0f, 498.0f, 210.0f, 508.0f), 3.0f, 3.0f);
    m_brushSolid->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.40f));
    m_renderTarget->FillRoundedRectangle(&barArtist, m_brushSolid.Get());

    // Mini Seekbar
    D2D1_ROUNDED_RECT miniSeek = D2D1::RoundedRect(D2D1::RectF(108.0f, 518.0f, 400.0f, 522.0f), 2.0f, 2.0f);
    m_brushSolid->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.15f));
    m_renderTarget->FillRoundedRectangle(&miniSeek, m_brushSolid.Get());

    D2D1_ROUNDED_RECT miniSeekFill = D2D1::RoundedRect(D2D1::RectF(108.0f, 518.0f, 230.0f, 522.0f), 2.0f, 2.0f);
    m_brushSolid->SetColor(D2D1::ColorF(0.0f, 0.65f, 1.0f, 0.90f));
    m_renderTarget->FillRoundedRectangle(&miniSeekFill, m_brushSolid.Get());

    // 5. Bottom Actions: Reset to Default (left) and Close (right)
    m_brushSolid->SetColor(D2D1::ColorF(0.60f, 0.60f, 0.65f));
    m_renderTarget->DrawText(L"Reset to Defaults", 17, m_tfLabel.Get(), D2D1::RectF(24.0f, 574.0f, 160.0f, 604.0f), m_brushSolid.Get());

    D2D1_ROUNDED_RECT btnDone = D2D1::RoundedRect(D2D1::RectF(326.0f, 568.0f, 416.0f, 606.0f), 7.0f, 7.0f);
    m_brushSolid->SetColor(D2D1::ColorF(0.18f, 0.19f, 0.23f));
    m_renderTarget->FillRoundedRectangle(&btnDone, m_brushSolid.Get());
    m_brushSolid->SetColor(D2D1::ColorF(0.25f, 0.26f, 0.32f));
    m_renderTarget->DrawRoundedRectangle(&btnDone, m_brushSolid.Get(), 1.0f);

    m_brushSolid->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f));
    m_renderTarget->DrawText(L"Close", 5, m_tfLabelCenter.Get(), D2D1::RectF(326.0f, 577.0f, 416.0f, 597.0f), m_brushSolid.Get());
}

void LightingStudioWindow::UpdateColorFromWheel(float mouseX, float mouseY)
{
    if (!m_config) return;

    float centerY = (m_currentSection == 0) ? WHEEL_CENTER_Y : CARD_WHEEL_CENTER_Y;
    float radius = (m_currentSection == 0) ? WHEEL_RADIUS : CARD_WHEEL_RADIUS;

    float* col = nullptr;
    if (m_currentSection == 0)
    {
        if (m_config->colorSource == settings::ColorSourceMode::Artwork) return;
        col = (m_config->colorSource == settings::ColorSourceMode::Solid)
            ? m_config->solidColor
            : ((m_activeSlot == 0) ? m_config->color1 : (m_activeSlot == 1 ? m_config->color2 : m_config->color3));
    }
    else
    {
        if (m_config->mediaCard.material != settings::CardMaterial::Solid ||
            m_config->mediaCard.solidColorMode != settings::SolidColorMode::MixCustom) return;
        col = (m_cardActiveSlot == 0) ? m_config->mediaCard.customColor1 :
              (m_cardActiveSlot == 1 ? m_config->mediaCard.customColor2 : m_config->mediaCard.customColor3);
    }

    if (!col) return;

    float dx = mouseX - WHEEL_CENTER_X;
    float dy = mouseY - centerY;
    float dist = std::sqrt(dx * dx + dy * dy);

    float sat = std::clamp(dist / radius, 0.0f, 1.0f);
    float angle = std::atan2(dy, dx);
    if (angle < 0.0f) angle += 2.0f * PI_F;
    float hue = angle / (2.0f * PI_F);

    auto hsb = media::ColorExtractor::RGBtoHSB(col[0], col[1], col[2]);
    float bri = (hsb.b < 0.05f) ? 1.0f : hsb.b;

    auto rgb = media::ColorExtractor::HSBtoRGB(hue, sat, bri);
    col[0] = rgb.r;
    col[1] = rgb.g;
    col[2] = rgb.b;
    col[3] = 1.0f;

    NotifyChanged();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void LightingStudioWindow::UpdateBrightnessFromSlider(float mouseX)
{
    if (!m_config) return;

    float* col = nullptr;
    if (m_currentSection == 0)
    {
        if (m_config->colorSource == settings::ColorSourceMode::Artwork) return;
        col = (m_config->colorSource == settings::ColorSourceMode::Solid)
            ? m_config->solidColor
            : ((m_activeSlot == 0) ? m_config->color1 : (m_activeSlot == 1 ? m_config->color2 : m_config->color3));
    }
    else
    {
        if (m_config->mediaCard.material != settings::CardMaterial::Solid ||
            m_config->mediaCard.solidColorMode != settings::SolidColorMode::MixCustom) return;
        col = (m_cardActiveSlot == 0) ? m_config->mediaCard.customColor1 :
              (m_cardActiveSlot == 1 ? m_config->mediaCard.customColor2 : m_config->mediaCard.customColor3);
    }

    if (!col) return;

    float bri = std::clamp((mouseX - 100.0f) / 240.0f, 0.05f, 1.0f);
    auto hsb = media::ColorExtractor::RGBtoHSB(col[0], col[1], col[2]);
    auto rgb = media::ColorExtractor::HSBtoRGB(hsb.h, hsb.s, bri);
    col[0] = rgb.r;
    col[1] = rgb.g;
    col[2] = rgb.b;

    NotifyChanged();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void LightingStudioWindow::UpdateAppearanceSlider(int sliderIndex, float mouseX)
{
    if (!m_config) return;

    float norm = std::clamp((mouseX - 38.0f) / 364.0f, 0.0f, 1.0f);
    float val = 0.2f + norm * (3.0f - 0.2f);

    if (sliderIndex == 0)
    {
        m_config->thickness = val;
    }
    else if (sliderIndex == 1)
    {
        m_config->glowIntensity = val;
    }
    else if (sliderIndex == 2)
    {
        m_config->animationSpeed = val;
    }

    NotifyChanged();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void LightingStudioWindow::SelectColorSlot(int index)
{
    if (index >= 0 && index < 3)
    {
        m_activeSlot = index;
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }
}

void LightingStudioWindow::ResetDefaults()
{
    if (!m_config) return;

    auto savedMediaCard = m_config->mediaCard;
    *m_config = settings::LightingConfig();
    m_config->mediaCard = savedMediaCard;
    NotifyChanged();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void LightingStudioWindow::NotifyChanged()
{
    if (m_config)
    {
        m_config->Save();
    }
    if (m_onChanged)
    {
        m_onChanged();
    }
}

LRESULT CALLBACK LightingStudioWindow::WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    LightingStudioWindow* self = nullptr;
    if (uMsg == WM_NCCREATE)
    {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<LightingStudioWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<LightingStudioWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }

    if (self)
    {
        return self->HandleMessage(hWnd, uMsg, wParam, lParam);
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

LRESULT LightingStudioWindow::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            Hide();
            return 0;
        }
        break;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);
        Render();
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1; // Prevent GDI background erase flicker

    case WM_SIZE:
        if (m_renderTarget)
        {
            m_renderTarget->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;

    case WM_DPICHANGED:
    {
        auto lprc = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hWnd, nullptr, lprc->left, lprc->top, lprc->right - lprc->left, lprc->bottom - lprc->top, SWP_NOZORDER | SWP_NOACTIVATE);
        if (m_renderTarget)
        {
            float newDpi = static_cast<float>(LOWORD(wParam));
            m_renderTarget->SetDpi(newDpi, newDpi);
            RECT rc = {};
            GetClientRect(hWnd, &rc);
            m_renderTarget->Resize(D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top));
        }
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_SETCURSOR:
    {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hWnd, &pt);

        float dpiX = 96.0f, dpiY = 96.0f;
        if (m_renderTarget) m_renderTarget->GetDpi(&dpiX, &dpiY);
        float cx = static_cast<float>(pt.x) * 96.0f / dpiX;
        float cy = static_cast<float>(pt.y) * 96.0f / dpiY;

        bool isHand = false;

        // Section Tabs
        if (cy >= 52.0f && cy <= 84.0f && cx >= 24.0f && cx <= 416.0f)
        {
            isHand = true;
        }
        else if (m_currentSection == 0)
        {
            // Plasma 3-mode toggle
            if (cy >= 92.0f && cy <= 122.0f && cx >= 24.0f && cx <= 416.0f)
            {
                isHand = true;
            }
            // Slots
            else if (cy >= 128.0f && cy <= 170.0f)
            {
                if (m_config && m_config->colorSource == settings::ColorSourceMode::Mixed && cx >= 24.0f && cx <= 416.0f)
                {
                    isHand = true;
                }
                else if (m_config && m_config->colorSource == settings::ColorSourceMode::Solid && cx >= 140.0f && cx <= 300.0f)
                {
                    isHand = true;
                }
            }
            // Wheel & Brightness Slider
            else if (m_config && m_config->colorSource != settings::ColorSourceMode::Artwork)
            {
                if (std::hypot(cx - WHEEL_CENTER_X, cy - WHEEL_CENTER_Y) <= WHEEL_RADIUS + 6.0f)
                {
                    isHand = true;
                }
                else if (cy >= 310.0f && cy <= 330.0f && cx >= 95.0f && cx <= 345.0f)
                {
                    isHand = true;
                }
            }
            // Appearance sliders
            if (!isHand && cy >= 368.0f && cy <= 496.0f && cx >= 32.0f && cx <= 408.0f)
            {
                isHand = true;
            }
        }
        else if (m_currentSection == 1)
        {
            // Material selector (Solid, Liquid Glass, Glassmorphism)
            if (cy >= 110.0f && cy <= 142.0f && cx >= 24.0f && cx <= 416.0f)
            {
                isHand = true;
            }
            // If Solid:
            else if (m_config && m_config->mediaCard.material == settings::CardMaterial::Solid)
            {
                // Mode selector
                if (cy >= 168.0f && cy <= 200.0f && cx >= 24.0f && cx <= 416.0f)
                {
                    isHand = true;
                }
                // Custom color slots
                else if (m_config->mediaCard.solidColorMode == settings::SolidColorMode::MixCustom)
                {
                    if (cy >= 206.0f && cy <= 248.0f && cx >= 24.0f && cx <= 416.0f)
                    {
                        isHand = true;
                    }
                    else if (std::hypot(cx - WHEEL_CENTER_X, cy - CARD_WHEEL_CENTER_Y) <= CARD_WHEEL_RADIUS + 6.0f)
                    {
                        isHand = true;
                    }
                    else if (cy >= 390.0f && cy <= 410.0f && cx >= 95.0f && cx <= 345.0f)
                    {
                        isHand = true;
                    }
                }
            }
        }

        // Bottom Actions (Reset Defaults / Close)
        if (!isHand && cy >= 566.0f && cy <= 610.0f && ((cx >= 24.0f && cx <= 160.0f) || (cx >= 326.0f && cx <= 416.0f)))
        {
            isHand = true;
        }

        if (isHand)
        {
            SetCursor(LoadCursor(nullptr, IDC_HAND));
            return TRUE;
        }
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        return TRUE;
    }

    case WM_LBUTTONDOWN:
    {
        float x = 0.0f, y = 0.0f;
        ScreenPixelToDip(hWnd, lParam, x, y);

        SetCapture(hWnd);

        // Section Tabs
        if (y >= 52.0f && y <= 84.0f && x >= 24.0f && x <= 416.0f)
        {
            if (x < 220.0f) SetActiveSection(0);
            else SetActiveSection(1);
            return 0;
        }

        if (m_currentSection == 0)
        {
            // 1. Color Source Mode Toggle (Artwork, Mixed, Solid)
            if (y >= 92.0f && y <= 122.0f && x >= 24.0f && x <= 416.0f)
            {
                if (m_config)
                {
                    if (x < 155.0f) m_config->colorSource = settings::ColorSourceMode::Artwork;
                    else if (x < 285.0f) m_config->colorSource = settings::ColorSourceMode::Mixed;
                    else m_config->colorSource = settings::ColorSourceMode::Solid;
                    NotifyChanged();
                    InvalidateRect(hWnd, nullptr, FALSE);
                }
                return 0;
            }

            // 2. Custom Color Slots
            if (y >= 128.0f && y <= 170.0f && m_config)
            {
                if (m_config->colorSource == settings::ColorSourceMode::Mixed)
                {
                    if (x >= 24.0f && x <= 148.0f)       { SelectColorSlot(0); return 0; }
                    else if (x >= 158.0f && x <= 282.0f) { SelectColorSlot(1); return 0; }
                    else if (x >= 292.0f && x <= 416.0f) { SelectColorSlot(2); return 0; }
                }
                else if (m_config->colorSource == settings::ColorSourceMode::Solid)
                {
                    if (x >= 140.0f && x <= 300.0f)      { SelectColorSlot(0); return 0; }
                }
            }

            // 3. Color Wheel & Brightness Slider
            if (m_config && m_config->colorSource != settings::ColorSourceMode::Artwork)
            {
                float distToWheel = std::hypot(x - WHEEL_CENTER_X, y - WHEEL_CENTER_Y);
                if (distToWheel <= WHEEL_RADIUS + 8.0f)
                {
                    m_dragState = DragState::ColorWheel;
                    UpdateColorFromWheel(x, y);
                    return 0;
                }

                if (y >= 310.0f && y <= 330.0f && x >= 95.0f && x <= 345.0f)
                {
                    m_dragState = DragState::BrightnessSlider;
                    UpdateBrightnessFromSlider(x);
                    return 0;
                }
            }

            // 4. Appearance Sliders (Thickness, Glow, Speed)
            if (x >= 32.0f && x <= 408.0f)
            {
                if (y >= 384.0f && y <= 408.0f)
                {
                    m_dragState = DragState::ThicknessSlider;
                    UpdateAppearanceSlider(0, x);
                    return 0;
                }
                else if (y >= 426.0f && y <= 450.0f)
                {
                    m_dragState = DragState::GlowSlider;
                    UpdateAppearanceSlider(1, x);
                    return 0;
                }
                else if (y >= 468.0f && y <= 492.0f)
                {
                    m_dragState = DragState::SpeedSlider;
                    UpdateAppearanceSlider(2, x);
                    return 0;
                }
            }

            // 5. Reset Defaults (Plasma)
            if (y >= 566.0f && y <= 610.0f && x >= 24.0f && x <= 160.0f)
            {
                ResetDefaults();
                return 0;
            }
        }
        else if (m_currentSection == 1)
        {
            // Material selection
            if (y >= 110.0f && y <= 142.0f && x >= 24.0f && x <= 416.0f)
            {
                if (m_config)
                {
                    if (x < 152.0f)
                    {
                        m_config->mediaCard.material = settings::CardMaterial::Solid;
                    }
                    else if (x < 284.0f)
                    {
                        m_config->mediaCard.material = settings::CardMaterial::LiquidGlass;
                    }
                    else
                    {
                        m_config->mediaCard.material = settings::CardMaterial::Glassmorphism;
                    }
                    NotifyChanged();
                    InvalidateRect(hWnd, nullptr, FALSE);
                }
                return 0;
            }

            // If Solid:
            if (m_config && m_config->mediaCard.material == settings::CardMaterial::Solid)
            {
                // Mode selector
                if (y >= 168.0f && y <= 200.0f && x >= 24.0f && x <= 416.0f)
                {
                    if (x < 220.0f) m_config->mediaCard.solidColorMode = settings::SolidColorMode::DynamicArtwork;
                    else m_config->mediaCard.solidColorMode = settings::SolidColorMode::MixCustom;
                    NotifyChanged();
                    InvalidateRect(hWnd, nullptr, FALSE);
                    return 0;
                }

                // If Mix Custom:
                if (m_config->mediaCard.solidColorMode == settings::SolidColorMode::MixCustom)
                {
                    // Slots
                    if (y >= 206.0f && y <= 248.0f)
                    {
                        if (x >= 24.0f && x <= 148.0f)       { SelectCardColorSlot(0); return 0; }
                        else if (x >= 158.0f && x <= 282.0f) { SelectCardColorSlot(1); return 0; }
                        else if (x >= 292.0f && x <= 416.0f) { SelectCardColorSlot(2); return 0; }
                    }

                    // Wheel
                    float distToWheel = std::hypot(x - WHEEL_CENTER_X, y - CARD_WHEEL_CENTER_Y);
                    if (distToWheel <= CARD_WHEEL_RADIUS + 8.0f)
                    {
                        m_dragState = DragState::ColorWheel;
                        UpdateColorFromWheel(x, y);
                        return 0;
                    }

                    // Brightness slider
                    if (y >= 390.0f && y <= 410.0f && x >= 95.0f && x <= 345.0f)
                    {
                        m_dragState = DragState::BrightnessSlider;
                        UpdateBrightnessFromSlider(x);
                        return 0;
                    }
                }
            }

            // Reset Defaults (Media Card)
            if (y >= 566.0f && y <= 610.0f && x >= 24.0f && x <= 160.0f)
            {
                ResetMediaCardDefaults();
                return 0;
            }
        }

        // Close Button
        if (y >= 566.0f && y <= 610.0f && x >= 326.0f && x <= 416.0f)
        {
            Hide();
            return 0;
        }

        // Header / Window Dragging
        if (y < 52.0f || y > 610.0f || (x > 345.0f && y >= 170.0f && y <= 360.0f) || (x < 95.0f && y >= 170.0f && y <= 360.0f))
        {
            m_dragState = DragState::Window;
            POINT pt = {};
            GetCursorPos(&pt);
            RECT rc = {};
            GetWindowRect(hWnd, &rc);
            m_dragOffset.x = pt.x - rc.left;
            m_dragOffset.y = pt.y - rc.top;
            return 0;
        }

        return 0;
    }

    case WM_MOUSEMOVE:
    {
        float x = 0.0f, y = 0.0f;
        ScreenPixelToDip(hWnd, lParam, x, y);

        if (m_dragState == DragState::Window)
        {
            POINT pt = {};
            GetCursorPos(&pt);
            SetWindowPos(hWnd, nullptr, pt.x - m_dragOffset.x, pt.y - m_dragOffset.y, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        else if (m_dragState == DragState::ColorWheel)
        {
            UpdateColorFromWheel(x, y);
            return 0;
        }
        else if (m_dragState == DragState::BrightnessSlider)
        {
            UpdateBrightnessFromSlider(x);
            return 0;
        }
        else if (m_dragState == DragState::ThicknessSlider)
        {
            UpdateAppearanceSlider(0, x);
            return 0;
        }
        else if (m_dragState == DragState::GlowSlider)
        {
            UpdateAppearanceSlider(1, x);
            return 0;
        }
        else if (m_dragState == DragState::SpeedSlider)
        {
            UpdateAppearanceSlider(2, x);
            return 0;
        }
        return 0;
    }

    case WM_LBUTTONUP:
        m_dragState = DragState::None;
        ReleaseCapture();
        return 0;

    case WM_CAPTURECHANGED:
        m_dragState = DragState::None;
        return 0;

    case WM_CLOSE:
        // Closing hides the window without quitting Halo Desktop
        Hide();
        return 0;
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

} // namespace lumn::ui
