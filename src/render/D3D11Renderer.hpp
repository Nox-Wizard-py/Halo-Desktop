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

namespace lumn::render
{

// Constant buffer layout matching cbuffer RimConstants in plasmaRim.hlsl
// Must be 16-byte aligned and multiple of 16 bytes (total 80 bytes)
struct alignas(16) RimConstants
{
    float screenSize[2];      // Physical screen dimensions in pixels (e.g. 2560, 1440)
    float notchDimensions[2]; // Hardcoded to (0, 0) on Windows displays
    float hotColor[4];        // High-energy rim color (RGBA float)
    float deepColor[4];       // Low-energy bass rim color (RGBA float)
    float time;               // Continuous animation time in seconds
    float phase;              // Beat-synchronized phase [0.0, 1.0]
    float balance;            // Chromatic balance ratio [0.0, 1.0]
    float bloom;              // Transient bloom impulse [0.0, 1.0]
    float cornerRadius;       // Rounded corner radius (0.0 for rectangular displays)
    float thicknessMul;       // Rim thickness multiplier (default 1.0)
    float glowMultiplier;     // Overall glow multiplier (default 1.0)
    float audioLevel;         // Continuous dynamic normalized audio energy [0.0, 1.0]
    float beatPulse;          // Instantaneous beat transient impulse [0.0, 1.0]
    float padding[3];         // 16-byte alignment padding (total 96 bytes)
};

static_assert(sizeof(RimConstants) == 96, "RimConstants size must be exactly 96 bytes");

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
    void SetTargetPalette(const float hot[4], const float deep[4], float balance, uint64_t generation = 0);
    void SetCornerRadius(float radius) noexcept { m_constants.cornerRadius = radius; }
    void SetThickness(float thickness) noexcept { m_constants.thicknessMul = thickness; }
    void SetGlowMultiplier(float glow) noexcept { m_constants.glowMultiplier = glow; }
    void SetBalance(float balance) noexcept { m_constants.balance = balance; }

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

    float m_targetHot[4] = { 0.20f, 0.85f, 1.00f, 1.00f };
    float m_targetDeep[4] = { 0.55f, 0.10f, 0.95f, 1.00f };
    float m_targetBalance = 0.60f;
    float m_lastRenderTime = 0.0f;
    uint64_t m_activePaletteGeneration = 0;
    bool m_isPaletteTransitioning = false;
};

} // namespace lumn::render

