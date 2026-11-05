#include "LightingStudioWindow.hpp"
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
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = m_hInstance;
    wc.lpszClassName = LIGHTING_STUDIO_CLASS;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // Direct2D paints client area

    RegisterClassExW(&wc);

    UINT dpi = GetWindowDpi(nullptr);
    float scale = static_cast<float>(dpi) / 96.0f;

    int clientW = static_cast<int>(DESIGN_WIDTH * scale + 0.5f);
    int clientH = static_cast<int>(DESIGN_HEIGHT * scale + 0.5f);

    RECT wr = { 0, 0, clientW, clientH };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    DWORD exStyle = WS_EX_APPWINDOW;

    AdjustWindowRectDpi(&wr, style, FALSE, exStyle, dpi);

    int winW = wr.right - wr.left;
    int winH = wr.bottom - wr.top;
    int posX = (GetSystemMetrics(SM_CXSCREEN) - winW) / 2;
    int posY = (GetSystemMetrics(SM_CYSCREEN) - winH) / 2;

    m_hWnd = CreateWindowExW(
        exStyle,
        LIGHTING_STUDIO_CLASS,
        L"Lighting Studio",
        style,
        posX, posY, winW, winH,
        nullptr, nullptr, m_hInstance, this
    );

    if (!m_hWnd) return false;

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

void LightingStudioWindow::Render()
{
    if (!CreateDeviceResources() || !m_config) return;

    m_renderTarget->BeginDraw();

    // Dark charcoal foundation background
    m_renderTarget->Clear(D2D1::ColorF(0.090f, 0.090f, 0.105f, 1.0f)); // #17171B

    // Active color retrieval
    float* activeCol = (m_activeSlot == 0) ? m_config->color1 :
                       (m_activeSlot == 1 ? m_config->color2 : m_config->color3);
    auto activeHsb = media::ColorExtractor::RGBtoHSB(activeCol[0], activeCol[1], activeCol[2]);

    GenerateWheelBitmap(activeHsb.b);

    // 1. Header Typography
    m_brushSolid->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
    m_renderTarget->DrawText(L"Lighting Studio", 15, m_tfHeader.Get(), D2D1::RectF(24.0f, 16.0f, 416.0f, 38.0f), m_brushSolid.Get());

    m_brushSolid->SetColor(D2D1::ColorF(0.55f, 0.55f, 0.60f, 1.0f));
    m_renderTarget->DrawText(L"Customize your plasma lighting", 30, m_tfSubheader.Get(), D2D1::RectF(24.0f, 38.0f, 416.0f, 56.0f), m_brushSolid.Get());

    // 2. Color Source Segmented Toggle
    D2D1_ROUNDED_RECT segTrack = D2D1::RoundedRect(D2D1::RectF(24.0f, 62.0f, 416.0f, 96.0f), 7.0f, 7.0f);
    m_brushSolid->SetColor(D2D1::ColorF(0.13f, 0.13f, 0.15f, 1.0f));
    m_renderTarget->FillRoundedRectangle(&segTrack, m_brushSolid.Get());
    m_brushSolid->SetColor(D2D1::ColorF(0.18f, 0.18f, 0.22f, 1.0f));
    m_renderTarget->DrawRoundedRectangle(&segTrack, m_brushSolid.Get(), 1.0f);

    bool isCustom = (m_config->colorSource == settings::ColorSourceMode::Custom);
    D2D1_ROUNDED_RECT activePill = isCustom
        ? D2D1::RoundedRect(D2D1::RectF(222.0f, 64.0f, 414.0f, 94.0f), 6.0f, 6.0f)
        : D2D1::RoundedRect(D2D1::RectF(26.0f, 64.0f, 218.0f, 94.0f), 6.0f, 6.0f);

    m_brushSolid->SetColor(D2D1::ColorF(0.24f, 0.25f, 0.30f, 1.0f));
    m_renderTarget->FillRoundedRectangle(&activePill, m_brushSolid.Get());

    // Segment labels
    m_brushSolid->SetColor(isCustom ? D2D1::ColorF(0.55f, 0.55f, 0.60f) : D2D1::ColorF(1.0f, 1.0f, 1.0f));
    m_renderTarget->DrawText(L"Artwork Colors", 14, m_tfLabelCenter.Get(), D2D1::RectF(26.0f, 70.0f, 218.0f, 88.0f), m_brushSolid.Get());

    m_brushSolid->SetColor(isCustom ? D2D1::ColorF(1.0f, 1.0f, 1.0f) : D2D1::ColorF(0.55f, 0.55f, 0.60f));
    m_renderTarget->DrawText(L"Custom Colors", 13, m_tfLabelCenter.Get(), D2D1::RectF(222.0f, 70.0f, 414.0f, 88.0f), m_brushSolid.Get());

    // 3. Custom Color Slots (Row of 3 cards)
    const wchar_t* slotTitles[3] = { L"Color 1", L"Color 2", L"Color 3" };
    const float* slotColors[3] = { m_config->color1, m_config->color2, m_config->color3 };

    for (int i = 0; i < 3; ++i)
    {
        float left = 24.0f + i * 134.0f;
        D2D1_ROUNDED_RECT slotBox = D2D1::RoundedRect(D2D1::RectF(left, 104.0f, left + 124.0f, 150.0f), 8.0f, 8.0f);

        bool isSlotSelected = (i == m_activeSlot);
        m_brushSolid->SetColor(isSlotSelected ? D2D1::ColorF(0.18f, 0.19f, 0.23f) : D2D1::ColorF(0.12f, 0.12f, 0.14f));
        m_renderTarget->FillRoundedRectangle(&slotBox, m_brushSolid.Get());

        m_brushSolid->SetColor(isSlotSelected ? D2D1::ColorF(0.0f, 0.55f, 1.0f, 1.0f) : D2D1::ColorF(0.18f, 0.18f, 0.22f));
        m_renderTarget->DrawRoundedRectangle(&slotBox, m_brushSolid.Get(), isSlotSelected ? 1.5f : 1.0f);

        // Color Swatch Pill
        D2D1_ROUNDED_RECT swatch = D2D1::RoundedRect(D2D1::RectF(left + 8.0f, 114.0f, left + 32.0f, 138.0f), 5.0f, 5.0f);
        m_brushSolid->SetColor(FloatToD2DColor(slotColors[i]));
        m_renderTarget->FillRoundedRectangle(&swatch, m_brushSolid.Get());
        m_brushSolid->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.40f));
        m_renderTarget->DrawRoundedRectangle(&swatch, m_brushSolid.Get(), 1.0f);

        // Slot title & Hex
        m_brushSolid->SetColor(D2D1::ColorF(0.95f, 0.95f, 1.0f));
        m_renderTarget->DrawText(slotTitles[i], static_cast<UINT32>(wcslen(slotTitles[i])), m_tfLabel.Get(), D2D1::RectF(left + 38.0f, 110.0f, left + 122.0f, 126.0f), m_brushSolid.Get());

        std::wstring hexStr = FloatToHex(slotColors[i]);
        m_brushSolid->SetColor(D2D1::ColorF(0.60f, 0.60f, 0.65f));
        m_renderTarget->DrawText(hexStr.c_str(), static_cast<UINT32>(hexStr.length()), m_tfSmall.Get(), D2D1::RectF(left + 38.0f, 126.0f, left + 122.0f, 142.0f), m_brushSolid.Get());
    }

    // 4. Circular Color Wheel
    D2D1_RECT_F wheelDest = D2D1::RectF(
        WHEEL_CENTER_X - WHEEL_RADIUS, WHEEL_CENTER_Y - WHEEL_RADIUS,
        WHEEL_CENTER_X + WHEEL_RADIUS, WHEEL_CENTER_Y + WHEEL_RADIUS
    );
    m_renderTarget->DrawBitmap(m_wheelBitmap.Get(), &wheelDest);

    // Subtle dark outer ring for color wheel definition
    m_brushSolid->SetColor(D2D1::ColorF(0.25f, 0.25f, 0.30f, 0.8f));
    m_renderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(WHEEL_CENTER_X, WHEEL_CENTER_Y), WHEEL_RADIUS + 0.5f, WHEEL_RADIUS + 0.5f), m_brushSolid.Get(), 1.0f);

    // Color Wheel Selection Thumb
    float thumbAngle = activeHsb.h * 2.0f * PI_F;
    float thumbDist = activeHsb.s * WHEEL_RADIUS;
    float tx = WHEEL_CENTER_X + thumbDist * std::cos(thumbAngle);
    float ty = WHEEL_CENTER_Y + thumbDist * std::sin(thumbAngle);

    // Drop shadow
    m_brushSolid->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.55f));
    m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(tx, ty + 1.0f), 8.0f, 8.0f), m_brushSolid.Get());

    // Outer white ring
    m_brushSolid->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
    m_renderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(tx, ty), 7.0f, 7.0f), m_brushSolid.Get(), 2.0f);

    // Center color dot
    m_brushSolid->SetColor(FloatToD2DColor(activeCol));
    m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(tx, ty), 4.5f, 4.5f), m_brushSolid.Get());

    // 5. Brightness Gradient Slider
    D2D1_ROUNDED_RECT briTrack = D2D1::RoundedRect(D2D1::RectF(100.0f, 312.0f, 340.0f, 318.0f), 3.0f, 3.0f);

    // Generate linear gradient for brightness track (Black -> Full brightness color)
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
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(100.0f, 315.0f), D2D1::Point2F(340.0f, 315.0f)),
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

    // Brightness thumb
    float briThumbX = 100.0f + std::clamp((activeHsb.b - 0.05f) / 0.95f, 0.0f, 1.0f) * 240.0f;
    m_brushSolid->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.40f));
    m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(briThumbX, 316.0f), 7.5f, 7.5f), m_brushSolid.Get());
    m_brushSolid->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
    m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(briThumbX, 315.0f), 6.5f, 6.5f), m_brushSolid.Get());
    m_brushSolid->SetColor(D2D1::ColorF(0.3f, 0.3f, 0.35f, 1.0f));
    m_renderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(briThumbX, 315.0f), 6.5f, 6.5f), m_brushSolid.Get(), 1.0f);

    // 6. Hex Code Indicator Pill
    D2D1_ROUNDED_RECT hexPill = D2D1::RoundedRect(D2D1::RectF(175.0f, 332.0f, 265.0f, 352.0f), 5.0f, 5.0f);
    m_brushSolid->SetColor(D2D1::ColorF(0.13f, 0.13f, 0.16f, 1.0f));
    m_renderTarget->FillRoundedRectangle(&hexPill, m_brushSolid.Get());
    m_brushSolid->SetColor(D2D1::ColorF(0.20f, 0.20f, 0.24f, 1.0f));
    m_renderTarget->DrawRoundedRectangle(&hexPill, m_brushSolid.Get(), 1.0f);

    std::wstring hexText = FloatToHex(activeCol);
    m_brushSolid->SetColor(D2D1::ColorF(0.90f, 0.90f, 0.95f, 1.0f));
    m_renderTarget->DrawText(hexText.c_str(), static_cast<UINT32>(hexText.length()), m_tfMono.Get(), D2D1::RectF(175.0f, 335.0f, 265.0f, 349.0f), m_brushSolid.Get());

    // 7. Appearance Card (Thickness, Glow, Speed)
    D2D1_ROUNDED_RECT cardApp = D2D1::RoundedRect(D2D1::RectF(24.0f, 362.0f, 416.0f, 506.0f), 8.0f, 8.0f);
    m_brushSolid->SetColor(D2D1::ColorF(0.12f, 0.12f, 0.14f, 1.0f));
    m_renderTarget->FillRoundedRectangle(&cardApp, m_brushSolid.Get());
    m_brushSolid->SetColor(D2D1::ColorF(0.17f, 0.17f, 0.20f, 1.0f));
    m_renderTarget->DrawRoundedRectangle(&cardApp, m_brushSolid.Get(), 1.0f);

    struct SliderDesc {
        const wchar_t* name;
        float value;
        float y;
    } sliders[3] = {
        { L"Rim Thickness", m_config->thickness, 374.0f },
        { L"Glow Intensity", m_config->glowIntensity, 418.0f },
        { L"Animation Speed", m_config->animationSpeed, 462.0f }
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

        // Slider Track
        float trackY = y + 20.0f;
        D2D1_ROUNDED_RECT trk = D2D1::RoundedRect(D2D1::RectF(38.0f, trackY, 402.0f, trackY + 5.0f), 2.5f, 2.5f);
        m_brushSolid->SetColor(D2D1::ColorF(0.19f, 0.19f, 0.23f));
        m_renderTarget->FillRoundedRectangle(&trk, m_brushSolid.Get());

        // Active fill
        float normVal = std::clamp((sliders[i].value - 0.2f) / (3.0f - 0.2f), 0.0f, 1.0f);
        float thumbX = 38.0f + normVal * 364.0f;
        D2D1_ROUNDED_RECT fillTrk = D2D1::RoundedRect(D2D1::RectF(38.0f, trackY, thumbX, trackY + 5.0f), 2.5f, 2.5f);
        m_brushSolid->SetColor(D2D1::ColorF(0.0f, 0.55f, 1.0f));
        m_renderTarget->FillRoundedRectangle(&fillTrk, m_brushSolid.Get());

        // Circular Thumb
        m_brushSolid->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.35f));
        m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, trackY + 3.5f), 7.5f, 7.5f), m_brushSolid.Get());
        m_brushSolid->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f));
        m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, trackY + 2.5f), 6.5f, 6.5f), m_brushSolid.Get());
        m_brushSolid->SetColor(D2D1::ColorF(0.4f, 0.4f, 0.45f));
        m_renderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, trackY + 2.5f), 6.5f, 6.5f), m_brushSolid.Get(), 1.0f);
    }

    // 8. Live Rim Gradient Preview Capsule
    m_brushSolid->SetColor(D2D1::ColorF(0.65f, 0.65f, 0.70f));
    m_renderTarget->DrawText(L"Edge Lighting Blend", 19, m_tfSmall.Get(), D2D1::RectF(24.0f, 516.0f, 250.0f, 532.0f), m_brushSolid.Get());

    D2D1_ROUNDED_RECT prevCapsule = D2D1::RoundedRect(D2D1::RectF(24.0f, 534.0f, 416.0f, 560.0f), 12.0f, 12.0f);

    D2D1_GRADIENT_STOP prevStops[4];
    if (m_config->colorSource == settings::ColorSourceMode::Custom)
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
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(24.0f, 547.0f), D2D1::Point2F(416.0f, 547.0f)),
            prevCollection.Get(),
            &prevBrush
        );
    }
    if (prevBrush)
    {
        m_renderTarget->FillRoundedRectangle(&prevCapsule, prevBrush.Get());
    }

    // Incandescent white core highlight at wave crest (~66% position)
    D2D1_ROUNDED_RECT coreAura = D2D1::RoundedRect(D2D1::RectF(260.0f, 537.0f, 310.0f, 557.0f), 10.0f, 10.0f);
    m_brushSolid->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.65f));
    m_renderTarget->FillRoundedRectangle(&coreAura, m_brushSolid.Get());

    m_brushSolid->SetColor(D2D1::ColorF(0.35f, 0.35f, 0.40f, 0.8f));
    m_renderTarget->DrawRoundedRectangle(&prevCapsule, m_brushSolid.Get(), 1.0f);

    // 9. Bottom Actions (Reset Defaults / Close)
    m_brushSolid->SetColor(D2D1::ColorF(0.60f, 0.60f, 0.65f));
    m_renderTarget->DrawText(L"Reset to Defaults", 17, m_tfLabel.Get(), D2D1::RectF(24.0f, 576.0f, 160.0f, 606.0f), m_brushSolid.Get());

    D2D1_ROUNDED_RECT btnDone = D2D1::RoundedRect(D2D1::RectF(326.0f, 570.0f, 416.0f, 608.0f), 7.0f, 7.0f);
    m_brushSolid->SetColor(D2D1::ColorF(0.18f, 0.19f, 0.23f));
    m_renderTarget->FillRoundedRectangle(&btnDone, m_brushSolid.Get());
    m_brushSolid->SetColor(D2D1::ColorF(0.25f, 0.26f, 0.32f));
    m_renderTarget->DrawRoundedRectangle(&btnDone, m_brushSolid.Get(), 1.0f);

    m_brushSolid->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f));
    m_renderTarget->DrawText(L"Close", 5, m_tfLabelCenter.Get(), D2D1::RectF(326.0f, 579.0f, 416.0f, 599.0f), m_brushSolid.Get());

    HRESULT hr = m_renderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
    {
        DiscardDeviceResources();
    }
}

void LightingStudioWindow::UpdateColorFromWheel(float mouseX, float mouseY)
{
    if (!m_config) return;

    float dx = mouseX - WHEEL_CENTER_X;
    float dy = mouseY - WHEEL_CENTER_Y;
    float dist = std::sqrt(dx * dx + dy * dy);

    float sat = std::clamp(dist / WHEEL_RADIUS, 0.0f, 1.0f);
    float angle = std::atan2(dy, dx);
    if (angle < 0.0f) angle += 2.0f * PI_F;
    float hue = angle / (2.0f * PI_F);

    float* col = (m_activeSlot == 0) ? m_config->color1 :
                 (m_activeSlot == 1 ? m_config->color2 : m_config->color3);
    auto hsb = media::ColorExtractor::RGBtoHSB(col[0], col[1], col[2]);
    float bri = (hsb.b < 0.05f) ? 1.0f : hsb.b;

    auto rgb = media::ColorExtractor::HSBtoRGB(hue, sat, bri);
    col[0] = rgb.r;
    col[1] = rgb.g;
    col[2] = rgb.b;
    col[3] = 1.0f;

    m_config->colorSource = settings::ColorSourceMode::Custom;
    NotifyChanged();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void LightingStudioWindow::UpdateBrightnessFromSlider(float mouseX)
{
    if (!m_config) return;

    float bri = std::clamp((mouseX - 100.0f) / 240.0f, 0.05f, 1.0f);

    float* col = (m_activeSlot == 0) ? m_config->color1 :
                 (m_activeSlot == 1 ? m_config->color2 : m_config->color3);
    auto hsb = media::ColorExtractor::RGBtoHSB(col[0], col[1], col[2]);

    auto rgb = media::ColorExtractor::HSBtoRGB(hsb.h, hsb.s, bri);
    col[0] = rgb.r;
    col[1] = rgb.g;
    col[2] = rgb.b;

    m_config->colorSource = settings::ColorSourceMode::Custom;
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

    *m_config = settings::LightingConfig();
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

        // Hover hand cursor over interactive regions
        if ((cy >= 62.0f && cy <= 96.0f && cx >= 24.0f && cx <= 416.0f) ||
            (cy >= 104.0f && cy <= 150.0f && cx >= 24.0f && cx <= 416.0f) ||
            (std::hypot(cx - WHEEL_CENTER_X, cy - WHEEL_CENTER_Y) <= WHEEL_RADIUS + 6.0f) ||
            (cy >= 305.0f && cy <= 325.0f && cx >= 95.0f && cx <= 345.0f) ||
            (cy >= 368.0f && cy <= 498.0f && cx >= 32.0f && cx <= 408.0f) ||
            (cy >= 570.0f && cy <= 612.0f && ((cx >= 24.0f && cx <= 160.0f) || (cx >= 326.0f && cx <= 416.0f))))
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

        // 1. Color Source Mode Toggle
        if (y >= 62.0f && y <= 96.0f && x >= 24.0f && x <= 416.0f)
        {
            if (m_config)
            {
                m_config->colorSource = (x < 220.0f) ? settings::ColorSourceMode::Artwork : settings::ColorSourceMode::Custom;
                NotifyChanged();
                InvalidateRect(hWnd, nullptr, FALSE);
            }
            return 0;
        }

        // 2. Custom Color Slots (Row of 3)
        if (y >= 104.0f && y <= 150.0f)
        {
            if (x >= 24.0f && x <= 148.0f)       { SelectColorSlot(0); return 0; }
            else if (x >= 158.0f && x <= 282.0f) { SelectColorSlot(1); return 0; }
            else if (x >= 292.0f && x <= 416.0f) { SelectColorSlot(2); return 0; }
        }

        // 3. Color Wheel
        float distToWheel = std::hypot(x - WHEEL_CENTER_X, y - WHEEL_CENTER_Y);
        if (distToWheel <= WHEEL_RADIUS + 8.0f)
        {
            m_dragState = DragState::ColorWheel;
            UpdateColorFromWheel(x, y);
            return 0;
        }

        // 4. Brightness Slider
        if (y >= 305.0f && y <= 325.0f && x >= 95.0f && x <= 345.0f)
        {
            m_dragState = DragState::BrightnessSlider;
            UpdateBrightnessFromSlider(x);
            return 0;
        }

        // 5. Appearance Sliders (Thickness, Glow, Speed)
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

        // 6. Reset Defaults
        if (y >= 570.0f && y <= 610.0f && x >= 24.0f && x <= 160.0f)
        {
            ResetDefaults();
            return 0;
        }

        // 7. Close Button
        if (y >= 570.0f && y <= 610.0f && x >= 326.0f && x <= 416.0f)
        {
            Hide();
            return 0;
        }

        // 8. Header / Window Dragging
        if (y < 60.0f || y > 610.0f || (x > 345.0f && y >= 160.0f && y <= 360.0f) || (x < 95.0f && y >= 160.0f && y <= 360.0f))
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
