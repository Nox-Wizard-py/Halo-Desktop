// ==============================================================================
// LUMN-REA - Interactive Liquid Glass Media Card Preview
// Phase 4B.1: Standalone Win32 / D3D11 / Direct2D Visual Prototype
// Reuses verified Kyant0-inspired HLSL shader: tests/shaders/kyantLiquidGlass.hlsl
// ==============================================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <directxmath.h>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <chrono>
#include <cmath>
#include <algorithm>

#include "../src/render/BackdropSampler.hpp"

using Microsoft::WRL::ComPtr;
using namespace DirectX;

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

// Window and Card Dimensions
static constexpr int WINDOW_WIDTH = 840;
static constexpr int WINDOW_HEIGHT = 540;
static constexpr int CARD_WIDTH = 380;
static constexpr int CARD_HEIGHT = 140;
static constexpr int CARD_POS_X = 230;
static constexpr int CARD_POS_Y = 140;

// Kyant0 HLSL Constant Buffer Layout (16-byte aligned)
struct GlassConstants
{
    XMFLOAT2 u_size;                // 8 bytes
    XMFLOAT2 u_offset;              // 8 bytes -> 16 bytes
    XMFLOAT4 u_cornerRadii;         // 16 bytes
    float u_refractionHeight;       // 4 bytes
    float u_refractionAmount;       // 4 bytes
    float u_depthEffect;            // 4 bytes
    float u_chromaticAberration;    // 4 bytes -> 16 bytes
    float u_highlightAngle;         // 4 bytes
    float u_highlightFalloff;       // 4 bytes
    float u_vibrancy;               // 4 bytes
    float u_blurRadius;             // 4 bytes -> 16 bytes
    XMFLOAT4 u_highlightColor;      // 16 bytes
    XMFLOAT4 u_tintColor;           // 16 bytes
};

enum class GlassMode
{
    KyantLiquidWithDispersion = 0,
    KyantLiquidNoDispersion = 1,
    BaselineGlass = 2
};

struct PreviewSettings
{
    GlassMode mode = GlassMode::KyantLiquidWithDispersion;
    bool liveDesktopBackdrop = false;
    float refractionAmount = -22.0f;
    float refractionHeight = 24.0f;
    float chromaticAberration = 0.85f;
    float specularStrength = 0.55f;
    float tintOpacity = 0.35f;
    float vibrancy = 1.50f;
    float highlightAngleDeg = 45.0f;
    std::string statusMessage = "Press [1/2/3] Modes | [B] Toggle Backdrop | [Space] Save Screenshot";
};

// Helper to save a 32-bit BGRA buffer as a BMP file
static bool SaveBMP32(const std::string& filename, const uint32_t* pixels, int width, int height)
{
    BITMAPFILEHEADER bfh = {};
    bfh.bfType = 0x4D42; // 'BM'
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bfh.bfSize = bfh.bfOffBits + width * height * 4;

    BITMAPINFOHEADER bih = {};
    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = width;
    bih.biHeight = -height; // Top-down
    bih.biPlanes = 1;
    bih.biBitCount = 32;
    bih.biCompression = BI_RGB;

    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;
    file.write(reinterpret_cast<const char*>(&bfh), sizeof(bfh));
    file.write(reinterpret_cast<const char*>(&bih), sizeof(bih));
    file.write(reinterpret_cast<const char*>(pixels), width * height * 4);
    return true;
}

class LiquidGlassPreviewApp
{
public:
    LiquidGlassPreviewApp() = default;
    ~LiquidGlassPreviewApp() { Cleanup(); }

    bool Initialize(HINSTANCE hInstance, int nCmdShow);
    int Run();

    void HandleKeyDown(WPARAM wParam, bool shift);
    void Render();
    bool CaptureAndSaveScreenshots(const std::string& windowBmpPath, const std::string& cardBmpPath);

    void SetScreenshotAndExit(const std::string& path) { m_autoScreenshotPath = path; m_autoExit = true; }
    void SetExitAfterSeconds(double sec) { m_testExitAfter = sec; }
    void SetMode(GlassMode mode) { m_settings.mode = mode; }

private:
    bool InitWindow(HINSTANCE hInstance, int nCmdShow);
    bool InitD3D11();
    bool InitDirect2D();
    bool InitShaders();
    bool InitBackdropTextures();
    bool InitCardResources();
    void GenerateSyntheticBackdrop();
    void DrawPlaceholderArtwork(ID2D1DeviceContext* ctx, const D2D1_RECT_F& rect);
    void DrawCardForegroundUI(ID2D1DeviceContext* ctx);
    void DrawHudOverlay(ID2D1DeviceContext* ctx);

    void Cleanup();

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND m_hWnd = nullptr;
    HINSTANCE m_hInstance = nullptr;

    // D3D11 Resources
    ComPtr<ID3D11Device> m_d3dDevice;
    ComPtr<ID3D11DeviceContext> m_d3dContext;
    ComPtr<IDXGISwapChain1> m_swapChain;
    ComPtr<ID3D11RenderTargetView> m_backBufferRTV;

    // Shader Resources
    ComPtr<ID3D11VertexShader> m_vertexShader;
    ComPtr<ID3D11PixelShader> m_pixelShader;
    ComPtr<ID3D11Buffer> m_constantBuffer;
    ComPtr<ID3D11SamplerState> m_linearSampler;
    ComPtr<ID3D11RasterizerState> m_rastState;

    // Backdrop Resources (840x540)
    std::vector<uint32_t> m_syntheticPixels;
    ComPtr<ID3D11Texture2D> m_backdropTexture;
    ComPtr<ID3D11ShaderResourceView> m_backdropSRV;
    lumn::render::BackdropSampler m_desktopSampler;

    // Card Offscreen Render Target (380x140)
    ComPtr<ID3D11Texture2D> m_cardRTTexture;
    ComPtr<ID3D11RenderTargetView> m_cardRTV;
    ComPtr<ID3D11ShaderResourceView> m_cardSRV;

    // Sub-region backdrop texture fed into the card shader (380x140)
    ComPtr<ID3D11Texture2D> m_cardBackdropTexture;
    ComPtr<ID3D11ShaderResourceView> m_cardBackdropSRV;

    // Staging Textures for Readback
    ComPtr<ID3D11Texture2D> m_windowStagingTex;
    ComPtr<ID3D11Texture2D> m_cardStagingTex;

    // Direct2D & DirectWrite
    ComPtr<ID2D1Factory1> m_d2dFactory;
    ComPtr<ID2D1Device> m_d2dDevice;
    ComPtr<ID2D1DeviceContext> m_d2dContext;
    ComPtr<ID2D1Bitmap1> m_d2dTargetBitmap;
    ComPtr<IDWriteFactory> m_dwriteFactory;
    ComPtr<IDWriteTextFormat> m_titleFormat;
    ComPtr<IDWriteTextFormat> m_artistFormat;
    ComPtr<IDWriteTextFormat> m_timeFormat;
    ComPtr<IDWriteTextFormat> m_hudFormat;
    ComPtr<IDWriteTextFormat> m_hudBoldFormat;

    // D2D Brushes
    ComPtr<ID2D1SolidColorBrush> m_brushWhite;
    ComPtr<ID2D1SolidColorBrush> m_brushSecondary;
    ComPtr<ID2D1SolidColorBrush> m_brushMuted;
    ComPtr<ID2D1SolidColorBrush> m_brushAccentCyan;
    ComPtr<ID2D1SolidColorBrush> m_brushAccentMagenta;
    ComPtr<ID2D1SolidColorBrush> m_brushHudBg;
    ComPtr<ID2D1SolidColorBrush> m_brushHudBorder;
    ComPtr<ID2D1LinearGradientBrush> m_cardBevelBrush;

    PreviewSettings m_settings;
    std::chrono::steady_clock::time_point m_startTime;
    double m_testExitAfter = 0.0;
    bool m_autoExit = false;
    std::string m_autoScreenshotPath;
    bool m_hasTakenAutoScreenshot = false;
};

// Global pointer for WndProc
static LiquidGlassPreviewApp* g_app = nullptr;

LRESULT CALLBACK LiquidGlassPreviewApp::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_KEYDOWN:
    {
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (g_app) g_app->HandleKeyDown(wParam, shift);
        return 0;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);
        if (g_app) g_app->Render();
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void LiquidGlassPreviewApp::HandleKeyDown(WPARAM wParam, bool shift)
{
    switch (wParam)
    {
    case '1':
        m_settings.mode = GlassMode::KyantLiquidWithDispersion;
        m_settings.statusMessage = "Mode: Kyant0 Liquid Glass (Dispersion ON)";
        break;
    case '2':
        m_settings.mode = GlassMode::KyantLiquidNoDispersion;
        m_settings.statusMessage = "Mode: Kyant0 Liquid Glass (Dispersion OFF)";
        break;
    case '3':
        m_settings.mode = GlassMode::BaselineGlass;
        m_settings.statusMessage = "Mode: Baseline Glass (Zero Refraction)";
        break;
    case 'B':
        m_settings.liveDesktopBackdrop = !m_settings.liveDesktopBackdrop;
        m_settings.statusMessage = m_settings.liveDesktopBackdrop
            ? "Backdrop: Live Desktop Screen Capture (WDA_EXCLUDEFROMCAPTURE)"
            : "Backdrop: Synthetic High-Contrast Optical Test Pattern";
        break;
    case 'R':
        if (shift) m_settings.refractionAmount = std::min(0.0f, m_settings.refractionAmount + 2.0f);
        else m_settings.refractionAmount = std::max(-60.0f, m_settings.refractionAmount - 2.0f);
        m_settings.statusMessage = "Refraction Amount: " + std::to_string(m_settings.refractionAmount) + " px";
        break;
    case 'H':
        if (shift) m_settings.refractionHeight = std::max(4.0f, m_settings.refractionHeight - 2.0f);
        else m_settings.refractionHeight = std::min(60.0f, m_settings.refractionHeight + 2.0f);
        m_settings.statusMessage = "Refraction Height: " + std::to_string(m_settings.refractionHeight) + " px";
        break;
    case 'C':
        if (shift) m_settings.chromaticAberration = std::max(0.0f, m_settings.chromaticAberration - 0.1f);
        else m_settings.chromaticAberration = std::min(2.0f, m_settings.chromaticAberration + 0.1f);
        m_settings.statusMessage = "Chromatic Dispersion: " + std::to_string(m_settings.chromaticAberration);
        break;
    case 'S':
        if (shift) m_settings.specularStrength = std::max(0.0f, m_settings.specularStrength - 0.05f);
        else m_settings.specularStrength = std::min(1.0f, m_settings.specularStrength + 0.05f);
        m_settings.statusMessage = "Specular Sheen: " + std::to_string(m_settings.specularStrength);
        break;
    case 'T':
        if (shift) m_settings.tintOpacity = std::max(0.0f, m_settings.tintOpacity - 0.05f);
        else m_settings.tintOpacity = std::min(0.9f, m_settings.tintOpacity + 0.05f);
        m_settings.statusMessage = "Glass Tint Opacity: " + std::to_string(m_settings.tintOpacity);
        break;
    case 'V':
        if (shift) m_settings.vibrancy = std::max(0.5f, m_settings.vibrancy - 0.1f);
        else m_settings.vibrancy = std::min(2.5f, m_settings.vibrancy + 0.1f);
        m_settings.statusMessage = "Backdrop Vibrancy: " + std::to_string(m_settings.vibrancy) + "x";
        break;
    case VK_SPACE:
    {
        bool ok = CaptureAndSaveScreenshots("tests/liquid_glass_preview.bmp", "tests/liquid_glass_card_only.bmp");
        m_settings.statusMessage = ok ? "Saved: tests/liquid_glass_preview.bmp & tests/liquid_glass_card_only.bmp" : "Failed to save screenshot!";
        std::cout << "[Preview] " << m_settings.statusMessage << std::endl;
        break;
    }
    case VK_ESCAPE:
        PostQuitMessage(0);
        break;
    }
}

bool LiquidGlassPreviewApp::InitWindow(HINSTANCE hInstance, int nCmdShow)
{
    m_hInstance = hInstance;
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"LUMN_LiquidGlassPreview";
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassExW(&wc);

    RECT rc = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);

    int posX = (GetSystemMetrics(SM_CXSCREEN) - (rc.right - rc.left)) / 2;
    int posY = (GetSystemMetrics(SM_CYSCREEN) - (rc.bottom - rc.top)) / 2;

    m_hWnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        wc.lpszClassName,
        L"LUMN - Kyant0 Liquid Glass Media Card Preview",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        posX, posY, rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!m_hWnd) return false;

    ShowWindow(m_hWnd, nCmdShow);
    UpdateWindow(m_hWnd);
    return true;
}

bool LiquidGlassPreviewApp::InitD3D11()
{
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
    D3D_FEATURE_LEVEL outLevel;

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        featureLevels, 2, D3D11_SDK_VERSION, &m_d3dDevice, &outLevel, &m_d3dContext);

    if (FAILED(hr))
    {
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
            featureLevels, 2, D3D11_SDK_VERSION, &m_d3dDevice, &outLevel, &m_d3dContext);
    }
    if (FAILED(hr)) return false;

    ComPtr<IDXGIDevice> dxgiDevice;
    hr = m_d3dDevice.As(&dxgiDevice);
    if (FAILED(hr)) return false;

    ComPtr<IDXGIAdapter> dxgiAdapter;
    hr = dxgiDevice->GetAdapter(&dxgiAdapter);
    if (FAILED(hr)) return false;

    ComPtr<IDXGIFactory2> dxgiFactory;
    hr = dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory));
    if (FAILED(hr)) return false;

    DXGI_SWAP_CHAIN_DESC1 scDesc = {};
    scDesc.Width = WINDOW_WIDTH;
    scDesc.Height = WINDOW_HEIGHT;
    scDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scDesc.SampleDesc.Count = 1;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.BufferCount = 2;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    hr = dxgiFactory->CreateSwapChainForHwnd(
        m_d3dDevice.Get(), m_hWnd, &scDesc, nullptr, nullptr, &m_swapChain);
    if (FAILED(hr)) return false;

    ComPtr<ID3D11Texture2D> backBuffer;
    hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) return false;

    hr = m_d3dDevice->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_backBufferRTV);
    if (FAILED(hr)) return false;

    // Window Staging Texture for screenshot readback
    D3D11_TEXTURE2D_DESC stageDesc = {};
    stageDesc.Width = WINDOW_WIDTH;
    stageDesc.Height = WINDOW_HEIGHT;
    stageDesc.MipLevels = 1;
    stageDesc.ArraySize = 1;
    stageDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    stageDesc.SampleDesc.Count = 1;
    stageDesc.Usage = D3D11_USAGE_STAGING;
    stageDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    m_d3dDevice->CreateTexture2D(&stageDesc, nullptr, &m_windowStagingTex);

    return true;
}

bool LiquidGlassPreviewApp::InitDirect2D()
{
    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = m_d3dDevice.As(&dxgiDevice);
    if (FAILED(hr)) return false;

    D2D1_FACTORY_OPTIONS options = {};
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options, (void**)&m_d2dFactory);
    if (FAILED(hr)) return false;

    hr = m_d2dFactory->CreateDevice(dxgiDevice.Get(), &m_d2dDevice);
    if (FAILED(hr)) return false;

    hr = m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_d2dContext);
    if (FAILED(hr)) return false;

    ComPtr<IDXGISurface> backBufferSurface;
    hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBufferSurface));
    if (FAILED(hr)) return false;

    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );
    hr = m_d2dContext->CreateBitmapFromDxgiSurface(backBufferSurface.Get(), &bp, &m_d2dTargetBitmap);
    if (FAILED(hr)) return false;

    m_d2dContext->SetTarget(m_d2dTargetBitmap.Get());

    // DirectWrite Typography
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&m_dwriteFactory);
    if (FAILED(hr)) return false;

    m_dwriteFactory->CreateTextFormat(
        L"Segoe UI Variable Display", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 15.0f, L"en-US", &m_titleFormat);
    m_dwriteFactory->CreateTextFormat(
        L"Segoe UI Variable Text", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-US", &m_artistFormat);
    m_dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"en-US", &m_timeFormat);
    m_dwriteFactory->CreateTextFormat(
        L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-US", &m_hudFormat);
    m_dwriteFactory->CreateTextFormat(
        L"Consolas", nullptr, DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.5f, L"en-US", &m_hudBoldFormat);

    // D2D Brushes
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.95f), &m_brushWhite);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.85f, 0.88f, 0.95f, 0.70f), &m_brushSecondary);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.70f, 0.75f, 0.85f, 0.50f), &m_brushMuted);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.10f, 0.85f, 0.95f, 1.00f), &m_brushAccentCyan);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.20f, 0.65f, 1.00f), &m_brushAccentMagenta);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.04f, 0.05f, 0.08f, 0.85f), &m_brushHudBg);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.00f, 1.00f, 1.00f, 0.15f), &m_brushHudBorder);

    return true;
}

bool LiquidGlassPreviewApp::InitShaders()
{
    // Fullscreen Triangle Vertex Shader
    const char* vsCode =
        "struct VS_OUTPUT { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };\n"
        "VS_OUTPUT VSMain(uint vertexId : SV_VertexID) {\n"
        "    VS_OUTPUT output;\n"
        "    output.uv = float2((vertexId << 1) & 2, vertexId & 2);\n"
        "    output.pos = float4(output.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);\n"
        "    return output;\n"
        "}\n";

    ComPtr<ID3DBlob> vsBlob, errorBlob;
    HRESULT hr = D3DCompile(vsCode, strlen(vsCode), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &vsBlob, &errorBlob);
    if (FAILED(hr)) return false;
    hr = m_d3dDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_vertexShader);
    if (FAILED(hr)) return false;

    // Load translated Kyant0 HLSL shader
    ComPtr<ID3DBlob> psBlob;
    const wchar_t* shaderPaths[] = {
        L"tests/shaders/kyantLiquidGlass.hlsl",
        L"kyantLiquidGlass.hlsl",
        L"../tests/shaders/kyantLiquidGlass.hlsl"
    };
    for (const wchar_t* path : shaderPaths)
    {
        hr = D3DCompileFromFile(path, nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &psBlob, &errorBlob);
        if (SUCCEEDED(hr)) break;
    }
    if (FAILED(hr))
    {
        if (errorBlob) std::cerr << "[Shader Error] " << (const char*)errorBlob->GetBufferPointer() << std::endl;
        return false;
    }
    hr = m_d3dDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_pixelShader);
    if (FAILED(hr)) return false;

    // Sampler State
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    hr = m_d3dDevice->CreateSamplerState(&sampDesc, &m_linearSampler);
    if (FAILED(hr)) return false;

    // Constant Buffer
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(GlassConstants);
    cbDesc.Usage = D3D11_USAGE_DEFAULT;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    hr = m_d3dDevice->CreateBuffer(&cbDesc, nullptr, &m_constantBuffer);
    if (FAILED(hr)) return false;

    // Rasterizer state (Cull None)
    D3D11_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode = D3D11_FILL_SOLID;
    rastDesc.CullMode = D3D11_CULL_NONE;
    hr = m_d3dDevice->CreateRasterizerState(&rastDesc, &m_rastState);
    if (FAILED(hr)) return false;

    return true;
}

void LiquidGlassPreviewApp::GenerateSyntheticBackdrop()
{
    m_syntheticPixels.resize(WINDOW_WIDTH * WINDOW_HEIGHT);

    for (int y = 0; y < WINDOW_HEIGHT; ++y)
    {
        float ny = (float)y / (float)WINDOW_HEIGHT;
        for (int x = 0; x < WINDOW_WIDTH; ++x)
        {
            float nx = (float)x / (float)WINDOW_WIDTH;

            // Rich flowing macOS Sonoma/Lumn style wallpaper:
            // Smooth gradient blending deep indigo (0x180E29), vibrant violet (0x651A94), and warm amber (0xD85A20)
            float wave1 = sinf(nx * 3.5f + ny * 2.2f) * 0.5f + 0.5f;
            float wave2 = cosf(nx * 4.2f - ny * 3.1f) * 0.5f + 0.5f;

            float r = (1.0f - wave1) * 0.12f + wave1 * (wave2 * 0.85f + (1.0f - wave2) * 0.45f);
            float g = (1.0f - wave1) * 0.06f + wave1 * (wave2 * 0.25f + (1.0f - wave2) * 0.12f);
            float b = (1.0f - wave1) * 0.28f + wave1 * (wave2 * 0.20f + (1.0f - wave2) * 0.65f);

            // Optical Bench Test Patterns (positioned right under the card at (230, 140))
            // 1. High-frequency vertical RGB calibration stripes
            if (x >= 200 && x <= 620 && y >= 110 && y <= 310)
            {
                int stripeIdx = ((x - 200) / 18) % 7;
                float stripeR = 0.0f, stripeG = 0.0f, stripeB = 0.0f;
                switch (stripeIdx)
                {
                case 0: stripeR = 0.95f; stripeG = 0.15f; stripeB = 0.20f; break; // Crimson
                case 1: stripeR = 0.95f; stripeG = 0.55f; stripeB = 0.10f; break; // Tangerine
                case 2: stripeR = 0.95f; stripeG = 0.90f; stripeB = 0.15f; break; // Yellow
                case 3: stripeR = 0.15f; stripeG = 0.85f; stripeB = 0.35f; break; // Lime
                case 4: stripeR = 0.10f; stripeG = 0.80f; stripeB = 0.95f; break; // Cyan
                case 5: stripeR = 0.20f; stripeG = 0.35f; stripeB = 0.95f; break; // Royal Blue
                case 6: stripeR = 0.80f; stripeG = 0.20f; stripeB = 0.90f; break; // Violet
                }
                r = std::lerp(r, stripeR, 0.40f);
                g = std::lerp(g, stripeG, 0.40f);
                b = std::lerp(b, stripeB, 0.40f);

                // 2. High-contrast fine horizontal grid lines every 8px
                if ((y % 8) == 0)
                {
                    r = 1.0f; g = 1.0f; b = 1.0f;
                }
                // 3. Sharp high-contrast diagonal cyan border
                if (abs((x - 200) - (y - 110) * 2) < 3)
                {
                    r = 0.0f; g = 1.0f; b = 1.0f;
                }
            }

            uint32_t ir = (uint32_t)std::clamp(r * 255.0f, 0.0f, 255.0f);
            uint32_t ig = (uint32_t)std::clamp(g * 255.0f, 0.0f, 255.0f);
            uint32_t ib = (uint32_t)std::clamp(b * 255.0f, 0.0f, 255.0f);
            m_syntheticPixels[y * WINDOW_WIDTH + x] = 0xFF000000 | (ir << 16) | (ig << 8) | ib;
        }
    }
}

bool LiquidGlassPreviewApp::InitBackdropTextures()
{
    GenerateSyntheticBackdrop();

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = WINDOW_WIDTH;
    desc.Height = WINDOW_HEIGHT;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA data = {};
    data.pSysMem = m_syntheticPixels.data();
    data.SysMemPitch = WINDOW_WIDTH * 4;

    HRESULT hr = m_d3dDevice->CreateTexture2D(&desc, &data, &m_backdropTexture);
    if (FAILED(hr)) return false;

    hr = m_d3dDevice->CreateShaderResourceView(m_backdropTexture.Get(), nullptr, &m_backdropSRV);
    if (FAILED(hr)) return false;

    // Initialize Desktop Live Sampler (840x540)
    m_desktopSampler.Initialize(m_d3dDevice.Get(), m_hWnd, WINDOW_WIDTH, WINDOW_HEIGHT);

    return true;
}

bool LiquidGlassPreviewApp::InitCardResources()
{
    // 1. Offscreen Render Target for the Card (380x140)
    D3D11_TEXTURE2D_DESC rtDesc = {};
    rtDesc.Width = CARD_WIDTH;
    rtDesc.Height = CARD_HEIGHT;
    rtDesc.MipLevels = 1;
    rtDesc.ArraySize = 1;
    rtDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    rtDesc.SampleDesc.Count = 1;
    rtDesc.Usage = D3D11_USAGE_DEFAULT;
    rtDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = m_d3dDevice->CreateTexture2D(&rtDesc, nullptr, &m_cardRTTexture);
    if (FAILED(hr)) return false;
    hr = m_d3dDevice->CreateRenderTargetView(m_cardRTTexture.Get(), nullptr, &m_cardRTV);
    if (FAILED(hr)) return false;
    hr = m_d3dDevice->CreateShaderResourceView(m_cardRTTexture.Get(), nullptr, &m_cardSRV);
    if (FAILED(hr)) return false;

    // 2. Texture to hold the backdrop cropped under the card (380x140)
    D3D11_TEXTURE2D_DESC cropDesc = rtDesc;
    cropDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    hr = m_d3dDevice->CreateTexture2D(&cropDesc, nullptr, &m_cardBackdropTexture);
    if (FAILED(hr)) return false;
    hr = m_d3dDevice->CreateShaderResourceView(m_cardBackdropTexture.Get(), nullptr, &m_cardBackdropSRV);
    if (FAILED(hr)) return false;

    // 3. Staging Texture for Card Readback
    D3D11_TEXTURE2D_DESC stageDesc = cropDesc;
    stageDesc.Usage = D3D11_USAGE_STAGING;
    stageDesc.BindFlags = 0;
    stageDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    m_d3dDevice->CreateTexture2D(&stageDesc, nullptr, &m_cardStagingTex);

    return true;
}

bool LiquidGlassPreviewApp::Initialize(HINSTANCE hInstance, int nCmdShow)
{
    g_app = this;
    m_startTime = std::chrono::steady_clock::now();

    if (!InitWindow(hInstance, nCmdShow)) return false;
    if (!InitD3D11()) return false;
    if (!InitDirect2D()) return false;
    if (!InitShaders()) return false;
    if (!InitBackdropTextures()) return false;
    if (!InitCardResources()) return false;

    std::cout << "[Preview] Initialized successfully. Window: 840x540, Card: 380x140 at (230, 140)." << std::endl;
    return true;
}

void LiquidGlassPreviewApp::DrawPlaceholderArtwork(ID2D1DeviceContext* ctx, const D2D1_RECT_F& rect)
{
    // Sgt. Pepper's inspired colorful album artwork
    D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(rect, 10.0f, 10.0f);

    ComPtr<ID2D1SolidColorBrush> bgBrush;
    ctx->CreateSolidColorBrush(D2D1::ColorF(0.85f, 0.15f, 0.25f, 1.0f), &bgBrush);
    ctx->FillRoundedRectangle(rrect, bgBrush.Get());

    // Concentric yellow/gold Sgt. Pepper drum ring
    float cx = (rect.left + rect.right) * 0.5f;
    float cy = (rect.top + rect.bottom) * 0.5f;

    ComPtr<ID2D1SolidColorBrush> goldBrush;
    ctx->CreateSolidColorBrush(D2D1::ColorF(0.98f, 0.85f, 0.20f, 0.90f), &goldBrush);
    ctx->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 36.0f, 36.0f), goldBrush.Get(), 3.5f);

    ComPtr<ID2D1SolidColorBrush> cyanBrush;
    ctx->CreateSolidColorBrush(D2D1::ColorF(0.15f, 0.85f, 0.95f, 0.85f), &cyanBrush);
    ctx->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 26.0f, 26.0f), cyanBrush.Get(), 2.0f);

    // Center star / emblem
    ComPtr<ID2D1SolidColorBrush> starBrush;
    ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.95f), &starBrush);
    ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 8.0f, 8.0f), starBrush.Get());

    // Thin inner border definition
    ComPtr<ID2D1SolidColorBrush> borderBrush;
    ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.25f), &borderBrush);
    ctx->DrawRoundedRectangle(rrect, borderBrush.Get(), 1.0f);
}

void LiquidGlassPreviewApp::DrawCardForegroundUI(ID2D1DeviceContext* ctx)
{
    // 1. Album Artwork Thumbnail (108x108 at (16, 16))
    D2D1_RECT_F artRect = D2D1::RectF(16.0f, 16.0f, 124.0f, 124.0f);
    DrawPlaceholderArtwork(ctx, artRect);

    // 2. Track Title Text (Right Side)
    std::wstring titleText = L"Sgt. Pepper's Lonely Hearts Club Band";
    D2D1_RECT_F titleRect = D2D1::RectF(136.0f, 16.0f, 364.0f, 38.0f);
    ctx->DrawText(titleText.c_str(), (UINT32)titleText.length(), m_titleFormat.Get(), titleRect, m_brushWhite.Get());

    // 3. Artist & Album Text
    std::wstring artistText = L"The Beatles \x2022 1967 Remaster";
    D2D1_RECT_F artistRect = D2D1::RectF(136.0f, 38.0f, 364.0f, 58.0f);
    ctx->DrawText(artistText.c_str(), (UINT32)artistText.length(), m_artistFormat.Get(), artistRect, m_brushSecondary.Get());

    // 4. Playback Progress Bar (136px to 364px, y=74)
    D2D1_ROUNDED_RECT progBgRect = D2D1::RoundedRect(D2D1::RectF(136.0f, 74.0f, 364.0f, 78.0f), 2.0f, 2.0f);
    ctx->FillRoundedRectangle(progBgRect, m_brushMuted.Get());

    // 45% Progress Fill
    float fillRight = 136.0f + 0.45f * (364.0f - 136.0f); // ~238px
    D2D1_ROUNDED_RECT fillRect = D2D1::RoundedRect(D2D1::RectF(136.0f, 74.0f, fillRight, 78.0f), 2.0f, 2.0f);
    ctx->FillRoundedRectangle(fillRect, m_brushAccentCyan.Get());

    // Glowing scrub thumb indicator
    ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(fillRight, 76.0f), 7.0f, 7.0f), m_brushAccentCyan.Get());
    ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(fillRight, 76.0f), 4.5f, 4.5f), m_brushWhite.Get());

    // 5. Time Labels (1:16 / 2:49)
    D2D1_RECT_F posLabelRect = D2D1::RectF(136.0f, 82.0f, 200.0f, 96.0f);
    ctx->DrawText(L"1:16", 4, m_timeFormat.Get(), posLabelRect, m_brushMuted.Get());

    m_timeFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    D2D1_RECT_F durLabelRect = D2D1::RectF(280.0f, 82.0f, 364.0f, 96.0f);
    ctx->DrawText(L"2:49", 4, m_timeFormat.Get(), durLabelRect, m_brushMuted.Get());
    m_timeFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    // 6. Media Controls (|<<, ||, >>|)
    // Previous |<<
    float prevCx = 165.0f, prevCy = 115.0f;
    ctx->DrawLine(D2D1::Point2F(prevCx - 7.0f, prevCy - 6.0f), D2D1::Point2F(prevCx - 7.0f, prevCy + 6.0f), m_brushWhite.Get(), 2.0f);

    ComPtr<ID2D1PathGeometry> prevGeo;
    m_d2dFactory->CreatePathGeometry(&prevGeo);
    if (prevGeo)
    {
        ComPtr<ID2D1GeometrySink> sink;
        prevGeo->Open(&sink);
        sink->BeginFigure(D2D1::Point2F(prevCx + 7.0f, prevCy - 6.0f), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1::Point2F(prevCx, prevCy));
        sink->AddLine(D2D1::Point2F(prevCx + 7.0f, prevCy + 6.0f));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->BeginFigure(D2D1::Point2F(prevCx, prevCy - 6.0f), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1::Point2F(prevCx - 6.0f, prevCy));
        sink->AddLine(D2D1::Point2F(prevCx, prevCy + 6.0f));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        ctx->FillGeometry(prevGeo.Get(), m_brushWhite.Get());
    }

    // Play/Pause circular frosted button at center (cx=250, cy=115)
    float playCx = 250.0f, playCy = 115.0f;
    ComPtr<ID2D1SolidColorBrush> btnBg;
    ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.12f), &btnBg);
    ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(playCx, playCy), 18.0f, 18.0f), btnBg.Get());

    ComPtr<ID2D1SolidColorBrush> btnRim;
    ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.28f), &btnRim);
    ctx->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(playCx, playCy), 18.0f, 18.0f), btnRim.Get(), 1.0f);

    // Pause bars ||
    D2D1_RECT_F bar1 = D2D1::RectF(playCx - 5.0f, playCy - 7.0f, playCx - 2.0f, playCy + 7.0f);
    D2D1_RECT_F bar2 = D2D1::RectF(playCx + 2.0f, playCy - 7.0f, playCx + 5.0f, playCy + 7.0f);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(bar1, 1.5f, 1.5f), m_brushWhite.Get());
    ctx->FillRoundedRectangle(D2D1::RoundedRect(bar2, 1.5f, 1.5f), m_brushWhite.Get());

    // Next >>|
    float nextCx = 335.0f, nextCy = 115.0f;
    ctx->DrawLine(D2D1::Point2F(nextCx + 7.0f, nextCy - 6.0f), D2D1::Point2F(nextCx + 7.0f, nextCy + 6.0f), m_brushWhite.Get(), 2.0f);

    ComPtr<ID2D1PathGeometry> nextGeo;
    m_d2dFactory->CreatePathGeometry(&nextGeo);
    if (nextGeo)
    {
        ComPtr<ID2D1GeometrySink> sink;
        nextGeo->Open(&sink);
        sink->BeginFigure(D2D1::Point2F(nextCx - 7.0f, nextCy - 6.0f), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1::Point2F(nextCx, nextCy));
        sink->AddLine(D2D1::Point2F(nextCx - 7.0f, nextCy + 6.0f));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->BeginFigure(D2D1::Point2F(nextCx, nextCy - 6.0f), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1::Point2F(nextCx + 6.0f, nextCy));
        sink->AddLine(D2D1::Point2F(nextCx, nextCy + 6.0f));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        ctx->FillGeometry(nextGeo.Get(), m_brushWhite.Get());
    }

    // 7. Subtle 3D Glass Border (18px radius)
    D2D1_ROUNDED_RECT cardBorder = D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, CARD_WIDTH - 0.5f, CARD_HEIGHT - 0.5f), 18.0f, 18.0f);
    ComPtr<ID2D1SolidColorBrush> glassBorderBrush;
    ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.22f), &glassBorderBrush);
    ctx->DrawRoundedRectangle(cardBorder, glassBorderBrush.Get(), 1.0f);
}

void LiquidGlassPreviewApp::DrawHudOverlay(ID2D1DeviceContext* ctx)
{
    // Draw translucent HUD control panel at bottom (0, 390) to (840, 540)
    D2D1_ROUNDED_RECT hudRect = D2D1::RoundedRect(D2D1::RectF(15.0f, 395.0f, (float)WINDOW_WIDTH - 15.0f, (float)WINDOW_HEIGHT - 15.0f), 12.0f, 12.0f);
    ctx->FillRoundedRectangle(hudRect, m_brushHudBg.Get());
    ctx->DrawRoundedRectangle(hudRect, m_brushHudBorder.Get(), 1.0f);

    std::wstring modeStr;
    switch (m_settings.mode)
    {
    case GlassMode::KyantLiquidWithDispersion:
        modeStr = L"[1] Kyant0 Liquid Glass (Dispersion ON)";
        break;
    case GlassMode::KyantLiquidNoDispersion:
        modeStr = L"[2] Kyant0 Liquid Glass (Dispersion OFF)";
        break;
    case GlassMode::BaselineGlass:
        modeStr = L"[3] Baseline Glass (Zero Refraction)";
        break;
    }

    std::wstringstream ss1;
    ss1 << L"MODE: " << modeStr << L"   |   BACKDROP: "
        << (m_settings.liveDesktopBackdrop ? L"[B] Live Desktop Capture (WDA_EXCLUDEFROMCAPTURE)" : L"[B] Synthetic Optical Test Pattern");
    std::wstring line1 = ss1.str();

    std::wstringstream ss2;
    ss2 << std::fixed << std::setprecision(1)
        << L"OPTICS: RefractAmount=" << m_settings.refractionAmount << L"px [R]  |  "
        << L"RefractHeight=" << m_settings.refractionHeight << L"px [H]  |  "
        << L"Dispersion=" << m_settings.chromaticAberration << L" [C]  |  "
        << L"Specular=" << m_settings.specularStrength << L" [S]  |  "
        << L"Tint=" << m_settings.tintOpacity << L" [T]  |  "
        << L"Vibrancy=" << m_settings.vibrancy << L"x [V]";
    std::wstring line2 = ss2.str();

    std::wstringstream ss3;
    ss3 << L"STATUS: " << std::wstring(m_settings.statusMessage.begin(), m_settings.statusMessage.end())
        << L"  |  [SPACE] Save Screenshot  |  [ESC] Exit";
    std::wstring line3 = ss3.str();

    ctx->DrawText(line1.c_str(), (UINT32)line1.length(), m_hudBoldFormat.Get(), D2D1::RectF(30.0f, 408.0f, 810.0f, 428.0f), m_brushAccentCyan.Get());
    ctx->DrawText(line2.c_str(), (UINT32)line2.length(), m_hudFormat.Get(), D2D1::RectF(30.0f, 434.0f, 810.0f, 454.0f), m_brushWhite.Get());
    ctx->DrawText(line3.c_str(), (UINT32)line3.length(), m_hudFormat.Get(), D2D1::RectF(30.0f, 460.0f, 810.0f, 480.0f), m_brushSecondary.Get());
}

void LiquidGlassPreviewApp::Render()
{
    if (!m_d3dContext || !m_swapChain) return;

    // 1. Update Backdrop: Live Desktop or Synthetic Pattern
    ID3D11ShaderResourceView* currentBackdropSRV = m_backdropSRV.Get();
    if (m_settings.liveDesktopBackdrop)
    {
        if (m_desktopSampler.Capture(m_hWnd, m_d3dContext.Get()))
        {
            currentBackdropSRV = m_desktopSampler.GetShaderResourceView();
        }
    }

    // 2. Extract 380x140 region directly behind the card into m_cardBackdropTexture
    // Using D3D11 CopySubresourceRegion from current backdrop at (CARD_POS_X, CARD_POS_Y)
    ID3D11Resource* srcRes = nullptr;
    currentBackdropSRV->GetResource(&srcRes);
    if (srcRes)
    {
        D3D11_BOX srcBox = {};
        srcBox.left = CARD_POS_X;
        srcBox.top = CARD_POS_Y;
        srcBox.right = CARD_POS_X + CARD_WIDTH;
        srcBox.bottom = CARD_POS_Y + CARD_HEIGHT;
        srcBox.front = 0;
        srcBox.back = 1;

        m_d3dContext->CopySubresourceRegion(
            m_cardBackdropTexture.Get(), 0, 0, 0, 0,
            srcRes, 0, &srcBox);
        srcRes->Release();
    }

    // 3. Render Glass Material into offscreen m_cardRTV (380x140) using kyantLiquidGlass.hlsl
    GlassConstants gc = {};
    gc.u_size = XMFLOAT2((float)CARD_WIDTH, (float)CARD_HEIGHT);
    gc.u_offset = XMFLOAT2(0.0f, 0.0f);
    gc.u_cornerRadii = XMFLOAT4(24.0f, 24.0f, 24.0f, 24.0f);
    gc.u_refractionHeight = m_settings.refractionHeight;
    gc.u_refractionAmount = (m_settings.mode == GlassMode::BaselineGlass) ? 0.0f : m_settings.refractionAmount;
    gc.u_depthEffect = (m_settings.mode == GlassMode::BaselineGlass) ? 0.0f : 0.5f;
    gc.u_chromaticAberration = (m_settings.mode == GlassMode::KyantLiquidWithDispersion) ? m_settings.chromaticAberration : 0.0f;
    gc.u_highlightAngle = m_settings.highlightAngleDeg * (3.14159265f / 180.0f);
    gc.u_highlightFalloff = 1.5f;
    gc.u_vibrancy = m_settings.vibrancy;
    gc.u_blurRadius = 0.0f;
    gc.u_highlightColor = XMFLOAT4(1.0f, 1.0f, 1.0f, m_settings.specularStrength);
    gc.u_tintColor = XMFLOAT4(0.06f, 0.06f, 0.10f, m_settings.tintOpacity);

    m_d3dContext->UpdateSubresource(m_constantBuffer.Get(), 0, nullptr, &gc, 0, 0);

    D3D11_VIEWPORT cardVP = {};
    cardVP.Width = (float)CARD_WIDTH;
    cardVP.Height = (float)CARD_HEIGHT;
    cardVP.MaxDepth = 1.0f;
    m_d3dContext->RSSetViewports(1, &cardVP);
    m_d3dContext->RSSetState(m_rastState.Get());

    float clearTransparent[4] = { 0, 0, 0, 0 };
    m_d3dContext->ClearRenderTargetView(m_cardRTV.Get(), clearTransparent);
    ID3D11RenderTargetView* cardRTVs[] = { m_cardRTV.Get() };
    m_d3dContext->OMSetRenderTargets(1, cardRTVs, nullptr);

    m_d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_d3dContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    m_d3dContext->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    m_d3dContext->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    m_d3dContext->PSSetShaderResources(0, 1, m_cardBackdropSRV.GetAddressOf());
    m_d3dContext->PSSetSamplers(0, 1, m_linearSampler.GetAddressOf());
    m_d3dContext->Draw(3, 0);

    // 4. Render Sharp Foreground UI on top of m_cardRTV using Direct2D
    // Create D2D Bitmap from m_cardRTTexture
    ComPtr<IDXGISurface> cardSurface;
    HRESULT hr = m_cardRTTexture.As(&cardSurface);
    if (SUCCEEDED(hr))
    {
        D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );
        ComPtr<ID2D1Bitmap1> cardD2DBitmap;
        hr = m_d2dContext->CreateBitmapFromDxgiSurface(cardSurface.Get(), &bp, &cardD2DBitmap);
        if (SUCCEEDED(hr))
        {
            m_d2dContext->SetTarget(cardD2DBitmap.Get());
            m_d2dContext->BeginDraw();
            DrawCardForegroundUI(m_d2dContext.Get());
            m_d2dContext->EndDraw();
        }
    }

    // 5. Composite Full Scene into Backbuffer (840x540)
    // First, draw the full backdrop
    D3D11_VIEWPORT windowVP = {};
    windowVP.Width = (float)WINDOW_WIDTH;
    windowVP.Height = (float)WINDOW_HEIGHT;
    windowVP.MaxDepth = 1.0f;
    m_d3dContext->RSSetViewports(1, &windowVP);

    float blackBg[4] = { 0.02f, 0.02f, 0.04f, 1.0f };
    m_d3dContext->ClearRenderTargetView(m_backBufferRTV.Get(), blackBg);
    ID3D11RenderTargetView* windowRTVs[] = { m_backBufferRTV.Get() };
    m_d3dContext->OMSetRenderTargets(1, windowRTVs, nullptr);

    // Render backdrop through a simple blit pass or D2D
    m_d2dContext->SetTarget(m_d2dTargetBitmap.Get());
    m_d2dContext->BeginDraw();

    // Draw backdrop image
    ComPtr<IDXGISurface> bgSurface;
    ID3D11Resource* bgRes = nullptr;
    currentBackdropSRV->GetResource(&bgRes);
    if (bgRes)
    {
        ComPtr<ID3D11Texture2D> bgTex;
        bgRes->QueryInterface(IID_PPV_ARGS(&bgTex));
        if (bgTex)
        {
            bgTex.As(&bgSurface);
            if (bgSurface)
            {
                D2D1_BITMAP_PROPERTIES1 bpBg = D2D1::BitmapProperties1(
                    D2D1_BITMAP_OPTIONS_NONE,
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)
                );
                ComPtr<ID2D1Bitmap1> bgBitmap;
                if (SUCCEEDED(m_d2dContext->CreateBitmapFromDxgiSurface(bgSurface.Get(), &bpBg, &bgBitmap)))
                {
                    m_d2dContext->DrawBitmap(bgBitmap.Get(), D2D1::RectF(0, 0, (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT));
                }
            }
        }
        bgRes->Release();
    }

    // Drop shadow under card
    D2D1_ROUNDED_RECT shadowRect = D2D1::RoundedRect(
        D2D1::RectF(CARD_POS_X - 1.0f, CARD_POS_Y + 4.0f, CARD_POS_X + CARD_WIDTH + 1.0f, CARD_POS_Y + CARD_HEIGHT + 14.0f),
        26.0f, 26.0f);
    ComPtr<ID2D1SolidColorBrush> shadowBrush;
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.45f), &shadowBrush);
    m_d2dContext->FillRoundedRectangle(shadowRect, shadowBrush.Get());

    // Blit rendered liquid glass card at (CARD_POS_X, CARD_POS_Y)
    if (cardSurface)
    {
        D2D1_BITMAP_PROPERTIES1 bpCard = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_NONE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );
        ComPtr<ID2D1Bitmap1> finalCardBitmap;
        if (SUCCEEDED(m_d2dContext->CreateBitmapFromDxgiSurface(cardSurface.Get(), &bpCard, &finalCardBitmap)))
        {
            m_d2dContext->DrawBitmap(
                finalCardBitmap.Get(),
                D2D1::RectF((float)CARD_POS_X, (float)CARD_POS_Y, (float)(CARD_POS_X + CARD_WIDTH), (float)(CARD_POS_Y + CARD_HEIGHT))
            );
        }
    }

    // Draw HUD Overlay
    DrawHudOverlay(m_d2dContext.Get());

    m_d2dContext->EndDraw();

    // Auto-screenshot if requested (BEFORE Present, so the flip model backbuffer contains the rendered frame)
    if (m_autoExit && !m_hasTakenAutoScreenshot)
    {
        CaptureAndSaveScreenshots(m_autoScreenshotPath, "tests/liquid_glass_card_only.bmp");
        m_hasTakenAutoScreenshot = true;
    }

    m_swapChain->Present(1, 0);
}

bool LiquidGlassPreviewApp::CaptureAndSaveScreenshots(const std::string& windowBmpPath, const std::string& cardBmpPath)
{
    // 1. Read back full window backbuffer
    ComPtr<ID3D11Texture2D> backBuffer;
    m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (!backBuffer || !m_windowStagingTex) return false;

    m_d3dContext->CopyResource(m_windowStagingTex.Get(), backBuffer.Get());

    D3D11_MAPPED_SUBRESOURCE mappedWin = {};
    HRESULT hr = m_d3dContext->Map(m_windowStagingTex.Get(), 0, D3D11_MAP_READ, 0, &mappedWin);
    if (FAILED(hr)) return false;

    std::vector<uint32_t> winPixels(WINDOW_WIDTH * WINDOW_HEIGHT);
    const uint8_t* srcWin = (const uint8_t*)mappedWin.pData;
    for (int y = 0; y < WINDOW_HEIGHT; ++y)
    {
        memcpy(&winPixels[y * WINDOW_WIDTH], srcWin + y * mappedWin.RowPitch, WINDOW_WIDTH * 4);
    }
    m_d3dContext->Unmap(m_windowStagingTex.Get(), 0);

    bool okWin = SaveBMP32(windowBmpPath, winPixels.data(), WINDOW_WIDTH, WINDOW_HEIGHT);

    // 2. Read back card texture
    if (!m_cardRTTexture || !m_cardStagingTex) return okWin;

    m_d3dContext->CopyResource(m_cardStagingTex.Get(), m_cardRTTexture.Get());

    D3D11_MAPPED_SUBRESOURCE mappedCard = {};
    hr = m_d3dContext->Map(m_cardStagingTex.Get(), 0, D3D11_MAP_READ, 0, &mappedCard);
    if (FAILED(hr)) return okWin;

    std::vector<uint32_t> cardPixels(CARD_WIDTH * CARD_HEIGHT);
    const uint8_t* srcCard = (const uint8_t*)mappedCard.pData;
    for (int y = 0; y < CARD_HEIGHT; ++y)
    {
        memcpy(&cardPixels[y * CARD_WIDTH], srcCard + y * mappedCard.RowPitch, CARD_WIDTH * 4);
    }
    m_d3dContext->Unmap(m_cardStagingTex.Get(), 0);

    bool okCard = SaveBMP32(cardBmpPath, cardPixels.data(), CARD_WIDTH, CARD_HEIGHT);

    return okWin && okCard;
}

int LiquidGlassPreviewApp::Run()
{
    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        else
        {
            Render();

            if (m_testExitAfter > 0.0)
            {
                auto now = std::chrono::steady_clock::now();
                double elapsed = std::chrono::duration<double>(now - m_startTime).count();
                if (elapsed >= m_testExitAfter)
                {
                    std::cout << "[Preview] Exiting after test timeout (" << elapsed << "s >= " << m_testExitAfter << "s)." << std::endl;
                    return 0;
                }
            }
            else if (m_autoExit && m_hasTakenAutoScreenshot)
            {
                std::cout << "[Preview] Auto-screenshot captured. Exiting cleanly." << std::endl;
                return 0;
            }
        }
    }
    return (int)msg.wParam;
}

void LiquidGlassPreviewApp::Cleanup()
{
    m_desktopSampler.Shutdown();
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int nCmdShow)
{
    std::string cmd(lpCmdLine ? lpCmdLine : "");
    std::cout << "[CmdLine] '" << cmd << "'" << std::endl;

    LiquidGlassPreviewApp app;

    // Check command line arguments
    if (cmd.find("--mode") != std::string::npos)
    {
        size_t pos = cmd.find("--mode");
        int m = 0;
        std::stringstream ss(cmd.substr(pos + strlen("--mode")));
        if (ss >> m)
        {
            if (m == 0) app.SetMode(GlassMode::KyantLiquidWithDispersion);
            else if (m == 1) app.SetMode(GlassMode::KyantLiquidNoDispersion);
            else if (m == 2) app.SetMode(GlassMode::BaselineGlass);
        }
    }

    if (cmd.find("--screenshot-and-exit") != std::string::npos)
    {
        std::string path = "tests/liquid_glass_preview.bmp";
        size_t pos = cmd.find("--screenshot-and-exit");
        if (pos != std::string::npos)
        {
            std::stringstream ss(cmd.substr(pos + strlen("--screenshot-and-exit")));
            std::string customPath;
            if (ss >> customPath) path = customPath;
        }
        app.SetScreenshotAndExit(path);
    }
    if (cmd.find("--test-exit-after") != std::string::npos)
    {
        size_t pos = cmd.find("--test-exit-after");
        double sec = 2.0;
        if (pos != std::string::npos)
        {
            std::stringstream ss(cmd.substr(pos + strlen("--test-exit-after")));
            ss >> sec;
        }
        app.SetExitAfterSeconds(sec);
    }

    if (!app.Initialize(hInstance, nCmdShow))
    {
        std::cerr << "[Error] Failed to initialize LiquidGlassPreviewApp." << std::endl;
        return 1;
    }

    return app.Run();
}

int main(int argc, char* argv[])
{
    std::string cmdLine;
    for (int i = 1; i < argc; ++i)
    {
        cmdLine += argv[i];
        cmdLine += " ";
    }
    return WinMain(GetModuleHandle(nullptr), nullptr, const_cast<char*>(cmdLine.c_str()), SW_SHOW);
}
