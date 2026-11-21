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
#include <cstdint>

namespace lumn::render
{

// Phase 4B.1: Localized Desktop Backdrop Sampler
// Captures only the rectangular bounding region directly behind the floating media card.
// Uses WDA_NONE display affinity so the window remains capturable in standard screenshots,
// and maps pixels to a dynamic D3D11 texture for real-time liquid glass refraction.
class BackdropSampler
{
public:
    BackdropSampler();
    ~BackdropSampler();

    BackdropSampler(const BackdropSampler&) = delete;
    BackdropSampler& operator=(const BackdropSampler&) = delete;

    bool Initialize(ID3D11Device* device, HWND hWnd, uint32_t width, uint32_t height, uint32_t margin = 0);
    void Shutdown();

    // Captures the current desktop region beneath hWnd (with margin) and uploads to the D3D11 texture
    bool Capture(HWND hWnd, ID3D11DeviceContext* context);

    ID3D11ShaderResourceView* GetShaderResourceView() const noexcept { return m_srv.Get(); }
    ID3D11Texture2D* GetTexture() const noexcept { return m_texture.Get(); }
    bool IsValid() const noexcept { return m_srv != nullptr; }
    uint32_t GetMargin() const noexcept { return m_margin; }
    uint32_t GetCapturedWidth() const noexcept { return m_width + 2 * m_margin; }
    uint32_t GetCapturedHeight() const noexcept { return m_height + 2 * m_margin; }

private:
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_margin = 0;

    // GDI capture resources
    HDC m_memDC = nullptr;
    HBITMAP m_hBitmap = nullptr;
    HGDIOBJ m_oldBitmap = nullptr;
    void* m_pBits = nullptr;

    // D3D11 texture & shader resource view
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srv;
};

} // namespace lumn::render
