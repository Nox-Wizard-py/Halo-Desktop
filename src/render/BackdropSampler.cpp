#include "BackdropSampler.hpp"
#include <iostream>

namespace lumn::render
{

BackdropSampler::BackdropSampler() = default;

BackdropSampler::~BackdropSampler()
{
    Shutdown();
}

bool BackdropSampler::Initialize(ID3D11Device* device, HWND hWnd, uint32_t width, uint32_t height, uint32_t margin)
{
    Shutdown();

    if (!device || width == 0 || height == 0)
    {
        return false;
    }

    m_width = width;
    m_height = height;
    m_margin = margin;

    uint32_t totalWidth = m_width + 2 * m_margin;
    uint32_t totalHeight = m_height + 2 * m_margin;

    // 1. Ensure window display affinity allows screen capture (WDA_NONE = 0)
    // Avoid WDA_EXCLUDEFROMCAPTURE which prevents Snipping Tool (Win+Shift+S),
    // OBS, and standard screen capture APIs from recording the media card.
    if (hWnd)
    {
        SetWindowDisplayAffinity(hWnd, WDA_NONE);
    }

    // 2. Initialize GDI Memory DC and 32-bit Top-Down DIB Section
    HDC screenDC = GetDC(nullptr);
    m_memDC = CreateCompatibleDC(screenDC);
    ReleaseDC(nullptr, screenDC);
    if (!m_memDC)
    {
        Shutdown();
        return false;
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = static_cast<LONG>(totalWidth);
    bmi.bmiHeader.biHeight = -static_cast<LONG>(totalHeight); // Negative for top-down scanlines
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    m_hBitmap = CreateDIBSection(m_memDC, &bmi, DIB_RGB_COLORS, &m_pBits, nullptr, 0);
    if (!m_hBitmap || !m_pBits)
    {
        Shutdown();
        return false;
    }

    m_oldBitmap = SelectObject(m_memDC, m_hBitmap);

    // 3. Initialize Dynamic D3D11 Texture2D & Shader Resource View
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = totalWidth;
    texDesc.Height = totalHeight;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Usage = D3D11_USAGE_DYNAMIC;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    texDesc.MiscFlags = 0;

    HRESULT hr = device->CreateTexture2D(&texDesc, nullptr, &m_texture);
    if (FAILED(hr))
    {
        Shutdown();
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;

    hr = device->CreateShaderResourceView(m_texture.Get(), &srvDesc, &m_srv);
    if (FAILED(hr))
    {
        Shutdown();
        return false;
    }

    return true;
}

void BackdropSampler::Shutdown()
{
    m_srv.Reset();
    m_texture.Reset();

    if (m_memDC && m_oldBitmap)
    {
        SelectObject(m_memDC, m_oldBitmap);
        m_oldBitmap = nullptr;
    }
    if (m_hBitmap)
    {
        DeleteObject(m_hBitmap);
        m_hBitmap = nullptr;
    }
    if (m_memDC)
    {
        DeleteDC(m_memDC);
        m_memDC = nullptr;
    }

    m_pBits = nullptr;
    m_width = 0;
    m_height = 0;
    m_margin = 0;
}

bool BackdropSampler::Capture(HWND hWnd, ID3D11DeviceContext* context)
{
    if (!m_memDC || !m_pBits || !m_texture || !context || !hWnd)
    {
        return false;
    }

    if (!IsWindowVisible(hWnd))
    {
        return false;
    }

    RECT rc = {};
    if (!GetWindowRect(hWnd, &rc))
    {
        return false;
    }

    HDC screenDC = GetDC(nullptr);
    if (!screenDC)
    {
        return false;
    }

    int srcX = rc.left - static_cast<int>(m_margin);
    int srcY = rc.top - static_cast<int>(m_margin);
    int totalWidth = static_cast<int>(m_width + 2 * m_margin);
    int totalHeight = static_cast<int>(m_height + 2 * m_margin);

    // Capture the screen region behind the media card with margin for refraction
    // Because WDA_EXCLUDEFROMCAPTURE is active, the window itself is not drawn in screenDC
    BOOL bitBltOk = BitBlt(m_memDC, 0, 0, totalWidth, totalHeight,
                           screenDC, srcX, srcY, SRCCOPY);
    ReleaseDC(nullptr, screenDC);

    if (!bitBltOk)
    {
        std::cerr << "[Capture] BitBlt failed: " << GetLastError() << "\n";
        return false;
    }

    // Upload to dynamic D3D11 texture
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = context->Map(m_texture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr))
    {
        std::cerr << "[Capture] Map failed: 0x" << std::hex << hr << std::dec << "\n";
        return false;
    }

    const uint8_t* srcRow = static_cast<const uint8_t*>(m_pBits);
    uint8_t* dstRow = static_cast<uint8_t*>(mapped.pData);
    const uint32_t rowBytes = totalWidth * 4;

    for (uint32_t y = 0; y < static_cast<uint32_t>(totalHeight); ++y)
    {
        memcpy(dstRow, srcRow, rowBytes);
        srcRow += rowBytes;
        dstRow += mapped.RowPitch;
    }

    context->Unmap(m_texture.Get(), 0);
    return true;
}

} // namespace lumn::render
