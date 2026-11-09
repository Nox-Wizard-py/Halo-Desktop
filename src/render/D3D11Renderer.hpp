#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>
#include <chrono>

namespace lumn::render
{

// Constant buffer layout matching cbuffer RimConstants in plasmaRim.hlsl
// Must be 16-byte aligned and multiple of 16 bytes (total 128 bytes)
struct alignas(16) RimConstants
{
    float screenSize[2];      // Physical screen dimensions in pixels (e.g. 2560, 1440)
    float notchDimensions[2]; // Hardcoded to (0, 0) on Windows displays
    float hotColor[4];        // Primary vibrant rim accent (Color 1 - RGBA float)
    float deepColor[4];       // Deep foundation rim color (Color 4 - RGBA float)
    float color2[4];          // Secondary contrasting accent (Color 2 - RGBA float)
    float color3[4];          // Tertiary luminous highlight (Color 3 - RGBA float)
    float time;               // Continuous animation time in seconds
    float phase;              // Beat-synchronized phase [0.0, 1.0]
    float balance;            // Chromatic balance ratio [0.0, 1.0]
    float bloom;              // Transient bloom impulse [0.0, 1.0]
    float cornerRadius;       // Rounded corner radius (0.0 for rectangular displays)
    float thicknessMul;       // Rim thickness multiplier (default 1.0)
    float glowMultiplier;     // Overall glow multiplier (default 1.0)
    float audioLevel;         // Continuous dynamic normalized audio energy [0.0, 1.0]
    float beatPulse;          // Instantaneous beat transient impulse [0.0, 1.0]
    float revealProgress;     // Organic activation reveal progress [0.0, 1.0] (1.0 = normal plasma)
    float revealCorner;       // Origin corner: 0=Top-Left, 1=Top-Right, 2=Bottom-Right, 3=Bottom-Left
    float revealPadding;      // 16-byte alignment padding (total 128 bytes)
};

static_assert(sizeof(RimConstants) == 128, "RimConstants size must be exactly 128 bytes");

class D3D11Renderer
{
public:
    D3D11Renderer();
    ~D3D11Renderer();

    D3D11Renderer(const D3D11Renderer&) = delete;
    D3D11Renderer& operator=(const D3D11Renderer&) = delete;

    bool Initialize(HWND hWnd, uint32_t width, uint32_t height, const std::wstring& shaderPath);
    void Shutdown();

    void Resize(uint32_t width, uint32_t height);
    void Render(float time, float phase, float bloom, float audioLevel = 0.0f, float beatPulse = 0.0f);

    void SetColors(const float hot[4], const float deep[4]);
    void SetTargetPalette(const float hot[4], const float deep[4], const float color2[4], const float color3[4], float balance, uint64_t generation = 0);
    void SetTargetPalette(const float hot[4], const float deep[4], float balance, uint64_t generation = 0);
    void SetCornerRadius(float radius) noexcept { m_constants.cornerRadius = radius; }
    void SetThickness(float thickness) noexcept { m_constants.thicknessMul = thickness; }
    void SetGlowMultiplier(float glow) noexcept { m_constants.glowMultiplier = glow; }
    void SetBalance(float balance) noexcept { m_constants.balance = balance; }

    // Organic Plasma Activation Animation
    void TriggerActivationAnimation(int forcedCorner = -1);
    bool IsActivationAnimationActive() const noexcept { return m_isRevealing; }
    float GetRevealProgress() const noexcept { return m_revealProgress; }
    int GetRevealCorner() const noexcept { return static_cast<int>(m_revealCorner + 0.5f); }

private:
    bool CreateDeviceAndSwapChain(HWND hWnd, uint32_t width, uint32_t height);
    bool CreateDirectComposition(HWND hWnd);
    bool CreateRenderTarget(uint32_t width, uint32_t height);
    bool CreateShaders(const std::wstring& shaderPath);
    bool CreateConstantBuffer();
    bool CreateBlendState();

    std::wstring ResolveShaderPath(const std::wstring& requestedPath);

    HWND m_hWnd = nullptr;
    uint32_t m_width = 0;
    uint32_t m_height = 0;

    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_renderTargetView;

    Microsoft::WRL::ComPtr<IDCompositionDevice> m_dcompDevice;
    Microsoft::WRL::ComPtr<IDCompositionTarget> m_dcompTarget;
    Microsoft::WRL::ComPtr<IDCompositionVisual> m_dcompVisual;

    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_constantBuffer;
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_blendState;

    D3D11_VIEWPORT m_viewport{};
    RimConstants m_constants{};

    float m_targetHot[4]    = { 0.00f, 0.85f, 1.00f, 1.00f };
    float m_targetDeep[4]   = { 0.12f, 0.04f, 0.35f, 1.00f };
    float m_targetColor2[4] = { 0.60f, 0.15f, 1.00f, 1.00f };
    float m_targetColor3[4] = { 0.95f, 0.25f, 0.65f, 1.00f };
    float m_targetBalance   = 0.50f;
    std::chrono::steady_clock::time_point m_lastWallTime{};
    bool m_hasLastWallTime = false;
    uint64_t m_activePaletteGeneration = 0;
    bool m_isPaletteTransitioning = false;

    // Activation animation parameters
    static constexpr float REVEAL_DURATION_SECONDS = 1.85f;
    float m_revealProgress = 1.0f;
    float m_revealCorner   = -1.0f;
    bool  m_isRevealing    = false;
    std::chrono::steady_clock::time_point m_revealStartTime{};
    bool  m_hasRevealStartTime = false;
    float m_phaseAnchor    = 0.0f;
    bool  m_hasPhaseAnchor = false;
};

} // namespace lumn::render

