#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <functional>
#include <vector>
#include "../settings/LightingSettings.hpp"
#include "../media/ColorExtractor.hpp"

namespace lumn::ui
{

class LightingStudioWindow
{
public:
    using OnChangedCallback = std::function<void()>;

    LightingStudioWindow();
    ~LightingStudioWindow();

    LightingStudioWindow(const LightingStudioWindow&) = delete;
    LightingStudioWindow& operator=(const LightingStudioWindow&) = delete;

    bool Initialize(HINSTANCE hInstance, settings::LightingConfig* config, OnChangedCallback onChanged);
    void Shutdown();

    void Show();
    void Hide();
    bool IsVisible() const;

    void SetArtworkPalette(const media::RimPalette& palette);
    void SetActiveSection(int section);
    HWND GetHandle() const noexcept { return m_hWnd; }

private:
    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    bool CreateDeviceResources();
    void DiscardDeviceResources();
    void Render();

    void RenderPlasmaSection();
    void RenderMediaCardSection();

    void GenerateWheelBitmap(float brightness);
    void UpdateColorFromWheel(float mouseX, float mouseY);
    void UpdateBrightnessFromSlider(float mouseX);
    void UpdateAppearanceSlider(int sliderIndex, float mouseX);
    void SelectColorSlot(int index);
    void SelectCardColorSlot(int index);
    void ResetDefaults();
    void ResetMediaCardDefaults();
    void NotifyChanged();
    void ScreenPixelToDip(HWND hWnd, LPARAM lParam, float& outX, float& outY) const;

    HINSTANCE m_hInstance = nullptr;
    HWND m_hWnd = nullptr;
    settings::LightingConfig* m_config = nullptr;
    OnChangedCallback m_onChanged;
    media::RimPalette m_artworkPalette;

    // Active Section: 0 = Plasma Lighting, 1 = Media Card Appearance
    int m_currentSection = 0;
    // Active Media Card color slot: 0 = Custom Color 1, 1 = Custom Color 2, 2 = Custom Color 3
    int m_cardActiveSlot = 0;

    // Direct2D & DirectWrite COM resources
    Microsoft::WRL::ComPtr<ID2D1Factory> m_d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> m_renderTarget;
    Microsoft::WRL::ComPtr<IDWriteFactory> m_dwriteFactory;

    // Typography
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_tfHeader;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_tfSubheader;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_tfLabel;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_tfLabelCenter;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_tfSmall;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_tfSmallCenter;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_tfMono;

    // Solid color brush
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushSolid;

    // Design-time DIP dimensions
    static constexpr float DESIGN_WIDTH = 440.0f;
    static constexpr float DESIGN_HEIGHT = 640.0f;

    // Interactive Color Wheel Bitmap
    static constexpr uint32_t WHEEL_SIZE = 136;
    static constexpr float WHEEL_RADIUS = 60.0f;
    static constexpr float WHEEL_CENTER_X = 220.0f;
    static constexpr float WHEEL_CENTER_Y = 246.0f;

    static constexpr float CARD_WHEEL_CENTER_Y = 328.0f;
    static constexpr float CARD_WHEEL_RADIUS   = 56.0f;

    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_wheelBitmap;
    std::vector<uint32_t> m_wheelPixels;
    float m_lastWheelBrightness = -1.0f;

    // Active color slot being edited: 0 (Color 1), 1 (Color 2), 2 (Color 3)
    int m_activeSlot = 0;

    enum class DragState
    {
        None,
        Window,
        ColorWheel,
        BrightnessSlider,
        ThicknessSlider,
        GlowSlider,
        SpeedSlider
    } m_dragState = DragState::None;

    POINT m_dragOffset = { 0, 0 };
};

} // namespace lumn::ui
