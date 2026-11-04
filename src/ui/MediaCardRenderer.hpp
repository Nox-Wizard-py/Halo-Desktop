#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <wrl/client.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dcomp.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <string>
#include <memory>

#include "../media/MediaSessionWatcher.hpp"
#include "../render/BackdropSampler.hpp"

namespace lumn::ui
{

struct LiquidGlassConstants
{
    float cardSize[2];          // Physical card dimensions (width, height)
    float margin[2];            // Backdrop sampling margin (e.g. 24.0, 24.0)
    float cornerRadii[4];       // (topLeft, topRight, bottomRight, bottomLeft)
    float refractionHeight;     // Refraction rim thickness in pixels (~22.0 px)
    float refractionAmount;     // Lens displacement magnitude (~ -22.0 px, inward bend)
    float depthEffect;          // Spherical depth contribution (0.0 to 1.0)
    float chromaticAberration;  // Corner chromatic dispersion intensity (~0.40)
    float highlightAngle;       // Specular light direction angle in radians (45 deg)
    float highlightFalloff;     // Specular sharpness exponent (~1.50)
    float vibrancy;             // Backdrop saturation boost (~2.50x)
    float specularStrength;     // Specular highlight opacity (~0.50)
    float highlightColor[4];    // Directional rim light color
    float tintColor[4];         // Base glass body tint (e.g. 0.06, 0.06, 0.10, 0.10)
    float hotColor[4];          // Dynamic hot accent color from OKLab artwork palette
    float deepColor[4];         // Dynamic deep color from OKLab artwork palette
    float frostAmount;          // Frosted blur diffusion radius in pixels (~4.5f * CARD_SCALE)
    float frostOpacity;         // Soft frosted translucent substrate scatter (~0.14f)
    float padding[2];           // 16-byte alignment padding
};

enum class CardInteractiveElement
{
    None,
    CardBody,
    Artwork,
    ProgressBar,
    PreviousButton,
    PlayPauseButton,
    NextButton,
    MinimizeButton
};

class MediaCardRenderer
{
public:
    static constexpr uint32_t BASE_CARD_WIDTH = 380;
    static constexpr uint32_t BASE_CARD_HEIGHT = 140;
    static constexpr float CARD_SCALE = 1.8f;
    static constexpr uint32_t CARD_WIDTH = static_cast<uint32_t>(BASE_CARD_WIDTH * CARD_SCALE);   // 684
    static constexpr uint32_t CARD_HEIGHT = static_cast<uint32_t>(BASE_CARD_HEIGHT * CARD_SCALE); // 252

    MediaCardRenderer();
    ~MediaCardRenderer();

    MediaCardRenderer(const MediaCardRenderer&) = delete;
    MediaCardRenderer& operator=(const MediaCardRenderer&) = delete;

    bool Initialize(HWND hWnd);
    void Shutdown();

    // Renders one frame with current track information, hover, scrub state, and effective play state
    void Render(const media::MediaTrackInfo& trackInfo,
                CardInteractiveElement hoveredElement,
                CardInteractiveElement pressedElement,
                bool isScrubbing,
                double scrubFraction,
                double currentTimeSec,
                bool effectiveIsPlaying);

    // Hit testing helper
    CardInteractiveElement HitTest(int x, int y, double& outProgressFraction) const;

    // Phase 4B.1: Liquid Glass Prototype Mode Toggle & Query
    void SetLiquidGlassEnabled(bool enabled) noexcept { m_useLiquidGlass = enabled; }
    bool IsLiquidGlassEnabled() const noexcept { return m_useLiquidGlass; }
    void ToggleLiquidGlass() noexcept { m_useLiquidGlass = !m_useLiquidGlass; }

    // Direct2D geometry and layout boundaries
    static D2D1_RECT_F GetProgressBarRect() noexcept { return D2D1::RectF(136.0f, 68.0f, 364.0f, 84.0f); }
    static D2D1_RECT_F GetPreviousButtonRect() noexcept { return D2D1::RectF(164.0f, 100.0f, 196.0f, 132.0f); }
    static D2D1_RECT_F GetPlayPauseButtonRect() noexcept { return D2D1::RectF(220.0f, 96.0f, 260.0f, 136.0f); }
    static D2D1_RECT_F GetNextButtonRect() noexcept { return D2D1::RectF(284.0f, 100.0f, 316.0f, 132.0f); }
    static D2D1_RECT_F GetMinimizeButtonRect() noexcept { return D2D1::RectF(350.0f, 12.0f, 368.0f, 30.0f); }

private:
    bool CreateDeviceAndSwapChain(HWND hWnd);
    bool CreateDirectComposition(HWND hWnd);
    bool CreateDirect2D();
    bool CreateTextFormats();
    bool CreateLiquidGlassResources();

    void RenderLiquidGlassPass(const media::MediaTrackInfo& trackInfo, double currentTimeSec);
    void UpdateArtworkBitmap(const media::MediaTrackInfo& trackInfo);
    void UpdateDynamicPaletteBrushes(const media::MediaTrackInfo& trackInfo);
    void DrawPlaceholderArtwork(const D2D1_RECT_F& rect);
    void DrawVectorControls(const media::MediaTrackInfo& trackInfo,
                            CardInteractiveElement hoveredElement,
                            CardInteractiveElement pressedElement,
                            bool effectiveIsPlaying);
    void DrawMinimizeButton(CardInteractiveElement hoveredElement,
                            CardInteractiveElement pressedElement);

    HWND m_hWnd = nullptr;

    // D3D11 & DXGI Swapchain
    Microsoft::WRL::ComPtr<ID3D11Device> m_d3dDevice;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_d3dContext;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swapChain;

    // DirectComposition
    Microsoft::WRL::ComPtr<IDCompositionDevice> m_dcompDevice;
    Microsoft::WRL::ComPtr<IDCompositionTarget> m_dcompTarget;
    Microsoft::WRL::ComPtr<IDCompositionVisual> m_dcompVisual;

    // Direct2D 1.1 & DirectWrite
    Microsoft::WRL::ComPtr<ID2D1Factory1> m_d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1Device> m_d2dDevice;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> m_d2dContext;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> m_d2dTargetBitmap;

    Microsoft::WRL::ComPtr<IDWriteFactory> m_dwriteFactory;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_titleFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_artistFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_timeFormat;

    // Artwork cache
    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_artworkBitmap;
    uint64_t m_artworkGeneration = 0;

    // Brushes
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_cardBgBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_cardBorderBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_textWhiteBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_textSecondaryBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_textMutedBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_progressBgBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_progressFillBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_btnHoverBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_btnPressedBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_btnIconBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_btnDisabledBrush;

    // Glass material brushes & resources
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> m_glassBaseBrush;
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> m_glassSheenBrush;
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> m_glassBevelBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_causticBrush;
    Microsoft::WRL::ComPtr<ID2D1RadialGradientBrush> m_artworkGlowBrush;
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> m_progressGradientBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_cardShadowBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_outerBorderTintBrush;

    // Phase 4B.1: Liquid Glass D3D11 Shaders & Resources
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_liquidVS;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_liquidPS;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_liquidConstantBuffer;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_liquidSampler;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_renderTargetView;
    render::BackdropSampler m_backdropSampler;
    bool m_useLiquidGlass = true;
    float m_dpiScale = 1.0f;

    // Smoothed palette colors for fluid glass transitions
    float m_smoothedHot[4] = { 0.20f, 0.85f, 1.00f, 1.00f };
    float m_smoothedDeep[4] = { 0.55f, 0.10f, 0.95f, 1.00f };
    bool m_paletteInitialized = false;
};

} // namespace lumn::ui

