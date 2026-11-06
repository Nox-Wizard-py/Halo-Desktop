#include "D3D11Renderer.hpp"

#include <d3dcompiler.h>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <vector>
#include <random>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace lumn::render
{

static void LogMsg(const std::string& msg)
{
    std::ofstream f("lumn_debug.log", std::ios::app);
    f << std::unitbuf << msg << std::endl;
    OutputDebugStringA(msg.c_str());
}

D3D11Renderer::D3D11Renderer()
{
    // Initialize default constants according to Windows recreation specs
    m_constants.screenSize[0] = 0.0f;
    m_constants.screenSize[1] = 0.0f;
    m_constants.notchDimensions[0] = 0.0f; // Windows displays have no camera notch
    m_constants.notchDimensions[1] = 0.0f;

    // Default vibrant palette: Electric Cyan (Hot), Midnight Indigo (Deep), Neon Purple (Color2), Luminous Magenta (Color3)
    m_constants.hotColor[0] = 0.00f;
    m_constants.hotColor[1] = 0.85f;
    m_constants.hotColor[2] = 1.00f;
    m_constants.hotColor[3] = 1.00f;

    m_constants.deepColor[0] = 0.12f;
    m_constants.deepColor[1] = 0.04f;
    m_constants.deepColor[2] = 0.35f;
    m_constants.deepColor[3] = 1.00f;

    m_constants.color2[0] = 0.60f;
    m_constants.color2[1] = 0.15f;
    m_constants.color2[2] = 1.00f;
    m_constants.color2[3] = 1.00f;

    m_constants.color3[0] = 0.95f;
    m_constants.color3[1] = 0.25f;
    m_constants.color3[2] = 0.65f;
    m_constants.color3[3] = 1.00f;

    m_constants.time = 0.0f;
    m_constants.phase = 0.0f;
    m_constants.balance = 0.50f;
    m_constants.bloom = 0.0f;
    m_constants.cornerRadius = 0.0f; // 0.0 for rectangular Windows monitors
    m_constants.thicknessMul = 1.0f;
    m_constants.glowMultiplier = 1.0f;
    m_constants.audioLevel = 0.0f;
    m_constants.beatPulse = 0.0f;
    m_constants.revealProgress = 1.0f;
    m_constants.revealCorner = 0.0f;
    m_constants.revealPadding = 0.0f;
}

D3D11Renderer::~D3D11Renderer()
{
    Shutdown();
}

bool D3D11Renderer::Initialize(HWND hWnd, uint32_t width, uint32_t height, const std::wstring& shaderPath)
{
    m_hWnd = hWnd;
    m_width = width;
    m_height = height;

    m_constants.screenSize[0] = static_cast<float>(width);
    m_constants.screenSize[1] = static_cast<float>(height);

    LogMsg("[D3D11Renderer] Initializing device and swapchain...");
    if (!CreateDeviceAndSwapChain(hWnd, width, height))
    {
        LogMsg("[D3D11Renderer] Failed to create D3D11 device and swapchain.");
        return false;
    }

    LogMsg("[D3D11Renderer] Initializing DirectComposition...");
    if (!CreateDirectComposition(hWnd))
    {
        LogMsg("[D3D11Renderer] Failed to initialize DirectComposition.");
        return false;
    }

    LogMsg("[D3D11Renderer] Creating render target view...");
    if (!CreateRenderTarget(width, height))
    {
        LogMsg("[D3D11Renderer] Failed to create render target.");
        return false;
    }

    std::wstring resolvedShader = ResolveShaderPath(shaderPath);
    char resolvedNarrow[MAX_PATH] = { 0 };
    WideCharToMultiByte(CP_UTF8, 0, resolvedShader.c_str(), -1, resolvedNarrow, sizeof(resolvedNarrow), nullptr, nullptr);
    LogMsg(std::string("[D3D11Renderer] Compiling shaders from: ") + resolvedNarrow);

    if (!CreateShaders(resolvedShader))
    {
        LogMsg("[D3D11Renderer] Failed to compile or create shaders.");
        return false;
    }

    LogMsg("[D3D11Renderer] Creating constant buffer...");
    if (!CreateConstantBuffer())
    {
        LogMsg("[D3D11Renderer] Failed to create constant buffer.");
        return false;
    }

    LogMsg("[D3D11Renderer] Creating blend state...");
    if (!CreateBlendState())
    {
        LogMsg("[D3D11Renderer] Failed to create blend state.");
        return false;
    }

    LogMsg("[D3D11Renderer] Initialization successful.");
    return true;
}

void D3D11Renderer::TriggerActivationAnimation(int forcedCorner)
{
    if (forcedCorner >= 0 && forcedCorner <= 3)
    {
        m_revealCorner = static_cast<float>(forcedCorner);
    }
    else
    {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dist(0, 3);
        int prev = (m_revealCorner >= 0.0f && m_revealCorner <= 3.0f) ? static_cast<int>(m_revealCorner + 0.5f) : -1;
        int next = dist(gen);
        if (prev >= 0 && next == prev)
        {
            next = (next + 1 + (dist(gen) % 3)) % 4; // Guarantee a distinct corner on successive triggers
        }
        m_revealCorner = static_cast<float>(next);
    }

    m_revealProgress = 0.0f;
    m_isRevealing = true;
    m_revealStartTime = std::chrono::steady_clock::now();
    m_hasRevealStartTime = true;
    m_hasPhaseAnchor = false;
    LogMsg("[D3D11Renderer] Organic activation reveal triggered from corner " + std::to_string(static_cast<int>(m_revealCorner)));
}

void D3D11Renderer::Shutdown()
{
    if (m_context)
    {
        m_context->ClearState();
        m_context->Flush();
    }

    m_blendState.Reset();
    m_constantBuffer.Reset();
    m_pixelShader.Reset();
    m_vertexShader.Reset();
    m_renderTargetView.Reset();

    m_dcompVisual.Reset();
    m_dcompTarget.Reset();
    m_dcompDevice.Reset();

    m_swapChain.Reset();
    m_context.Reset();
    m_device.Reset();

    m_hWnd = nullptr;
}

std::wstring D3D11Renderer::ResolveShaderPath(const std::wstring& requestedPath)
{
    namespace fs = std::filesystem;

    if (!requestedPath.empty() && fs::exists(requestedPath))
    {
        return requestedPath;
    }

    // Check executable directory
    wchar_t exeBuffer[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, exeBuffer, MAX_PATH);
    fs::path exeDir = fs::path(exeBuffer).parent_path();

    std::vector<fs::path> candidates = {
        exeDir / L"plasmaRim.hlsl",
        exeDir / L"shaders" / L"plasmaRim.hlsl",
        exeDir / L"src" / L"render" / L"shaders" / L"plasmaRim.hlsl",
        fs::current_path() / L"plasmaRim.hlsl",
        fs::current_path() / L"shaders" / L"plasmaRim.hlsl",
        fs::current_path() / L"src" / L"render" / L"shaders" / L"plasmaRim.hlsl"
    };

    for (const auto& path : candidates)
    {
        if (fs::exists(path))
        {
            return path.wstring();
        }
    }

    return requestedPath;
}

bool D3D11Renderer::CreateDeviceAndSwapChain([[maybe_unused]] HWND hWnd, uint32_t width, uint32_t height)
{
    UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        creationFlags,
        featureLevels,
        static_cast<UINT>(std::size(featureLevels)),
        D3D11_SDK_VERSION,
        &m_device,
        &featureLevel,
        &m_context
    );

    // If debug layer creation failed, retry without debug flag
#if defined(_DEBUG)
    if (FAILED(hr))
    {
        creationFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            creationFlags,
            featureLevels,
            static_cast<UINT>(std::size(featureLevels)),
            D3D11_SDK_VERSION,
            &m_device,
            &featureLevel,
            &m_context
        );
    }
#endif

    // Fallback to WARP if hardware adapter unavailable in current session
    if (FAILED(hr))
    {
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            creationFlags & ~D3D11_CREATE_DEVICE_DEBUG,
            featureLevels,
            static_cast<UINT>(std::size(featureLevels)),
            D3D11_SDK_VERSION,
            &m_device,
            &featureLevel,
            &m_context
        );
    }

    if (FAILED(hr))
    {
        LogMsg("[D3D11Renderer] D3D11CreateDevice failed with hr=" + std::to_string(static_cast<unsigned long>(hr)));
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    hr = m_device.As(&dxgiDevice);
    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<IDXGIAdapter> dxgiAdapter;
    hr = dxgiDevice->GetAdapter(&dxgiAdapter);
    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<IDXGIFactory2> dxgiFactory;
    hr = dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory));
    if (FAILED(hr)) return false;

    // Composition swapchain descriptor
    DXGI_SWAP_CHAIN_DESC1 scDesc = {};
    scDesc.Width = width;
    scDesc.Height = height;
    scDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scDesc.Stereo = FALSE;
    scDesc.SampleDesc.Count = 1;
    scDesc.SampleDesc.Quality = 0;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.BufferCount = 2;
    scDesc.Scaling = DXGI_SCALING_STRETCH; // Mandatory for composition swapchain
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // Windows 10/11 modern flip model
    scDesc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED; // Mandatory for transparent composition
    scDesc.Flags = 0;

    hr = dxgiFactory->CreateSwapChainForComposition(
        m_device.Get(),
        &scDesc,
        nullptr,
        &m_swapChain
    );

    if (FAILED(hr))
    {
        std::cerr << "[D3D11Renderer] CreateSwapChainForComposition failed with hr=0x" << std::hex << hr << std::dec << std::endl;
        return false;
    }

    return true;
}

bool D3D11Renderer::CreateDirectComposition(HWND hWnd)
{
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = m_device.As(&dxgiDevice);
    if (FAILED(hr)) return false;

    hr = DCompositionCreateDevice(
        dxgiDevice.Get(),
        __uuidof(IDCompositionDevice),
        reinterpret_cast<void**>(m_dcompDevice.GetAddressOf())
    );
    if (FAILED(hr))
    {
        std::cerr << "[D3D11Renderer] DCompositionCreateDevice failed with hr=0x" << std::hex << hr << std::dec << std::endl;
        return false;
    }

    hr = m_dcompDevice->CreateTargetForHwnd(hWnd, TRUE, &m_dcompTarget);
    if (FAILED(hr))
    {
        std::cerr << "[D3D11Renderer] CreateTargetForHwnd failed with hr=0x" << std::hex << hr << std::dec << std::endl;
        return false;
    }

    hr = m_dcompDevice->CreateVisual(&m_dcompVisual);
    if (FAILED(hr)) return false;

    hr = m_dcompVisual->SetContent(m_swapChain.Get());
    if (FAILED(hr)) return false;

    hr = m_dcompTarget->SetRoot(m_dcompVisual.Get());
    if (FAILED(hr)) return false;

    hr = m_dcompDevice->Commit();
    if (FAILED(hr))
    {
        std::cerr << "[D3D11Renderer] DirectComposition Commit failed with hr=0x" << std::hex << hr << std::dec << std::endl;
        return false;
    }

    return true;
}

bool D3D11Renderer::CreateRenderTarget(uint32_t width, uint32_t height)
{
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr))
    {
        std::cerr << "[D3D11Renderer] Failed to retrieve swapchain backbuffer hr=0x" << std::hex << hr << std::dec << std::endl;
        return false;
    }

    hr = m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_renderTargetView);
    if (FAILED(hr))
    {
        std::cerr << "[D3D11Renderer] CreateRenderTargetView failed hr=0x" << std::hex << hr << std::dec << std::endl;
        return false;
    }

    m_viewport.TopLeftX = 0.0f;
    m_viewport.TopLeftY = 0.0f;
    m_viewport.Width = static_cast<float>(width);
    m_viewport.Height = static_cast<float>(height);
    m_viewport.MinDepth = 0.0f;
    m_viewport.MaxDepth = 1.0f;

    return true;
}

bool D3D11Renderer::CreateShaders(const std::wstring& shaderPath)
{
    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3DCompileFromFile(
        shaderPath.c_str(),
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "VSMain",
        "vs_5_0",
        compileFlags,
        0,
        &vsBlob,
        &errorBlob
    );

    if (FAILED(hr))
    {
        if (errorBlob)
        {
            std::string errStr(static_cast<const char*>(errorBlob->GetBufferPointer()));
            LogMsg("[D3D11Renderer] VS Compile Error:\n" + errStr);
        }
        else
        {
            LogMsg("[D3D11Renderer] VS Compile failed with hr=0x" + std::to_string(hr));
        }
        return false;
    }

    hr = m_device->CreateVertexShader(
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        nullptr,
        &m_vertexShader
    );
    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    errorBlob.Reset();

    hr = D3DCompileFromFile(
        shaderPath.c_str(),
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "PSMain",
        "ps_5_0",
        compileFlags,
        0,
        &psBlob,
        &errorBlob
    );

    if (FAILED(hr))
    {
        if (errorBlob)
        {
            std::string errStr(static_cast<const char*>(errorBlob->GetBufferPointer()));
            LogMsg("[D3D11Renderer] PS Compile Error:\n" + errStr);
        }
        else
        {
            LogMsg("[D3D11Renderer] PS Compile failed with hr=0x" + std::to_string(hr));
        }
        return false;
    }

    hr = m_device->CreatePixelShader(
        psBlob->GetBufferPointer(),
        psBlob->GetBufferSize(),
        nullptr,
        &m_pixelShader
    );
    if (FAILED(hr)) return false;

    return true;
}

bool D3D11Renderer::CreateConstantBuffer()
{
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(RimConstants);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = m_device->CreateBuffer(&desc, nullptr, &m_constantBuffer);
    if (FAILED(hr))
    {
        std::cerr << "[D3D11Renderer] CreateBuffer (constant buffer) failed hr=0x" << std::hex << hr << std::dec << std::endl;
        return false;
    }

    return true;
}

bool D3D11Renderer::CreateBlendState()
{
    // Premultiplied alpha blending:
    // Dest.rgb = Src.rgb * 1 + Dest.rgb * (1 - Src.a)
    // Dest.a   = Src.a * 1 + Dest.a * (1 - Src.a)
    D3D11_BLEND_DESC desc = {};
    desc.RenderTarget[0].BlendEnable = TRUE;
    desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    HRESULT hr = m_device->CreateBlendState(&desc, &m_blendState);
    if (FAILED(hr))
    {
        std::cerr << "[D3D11Renderer] CreateBlendState failed hr=0x" << std::hex << hr << std::dec << std::endl;
        return false;
    }

    return true;
}

void D3D11Renderer::Resize(uint32_t width, uint32_t height)
{
    if (!m_swapChain || width == 0 || height == 0)
    {
        return;
    }

    m_width = width;
    m_height = height;
    m_constants.screenSize[0] = static_cast<float>(width);
    m_constants.screenSize[1] = static_cast<float>(height);

    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_renderTargetView.Reset();

    HRESULT hr = m_swapChain->ResizeBuffers(
        2,
        width,
        height,
        DXGI_FORMAT_B8G8R8A8_UNORM,
        0
    );

    if (FAILED(hr))
    {
        std::cerr << "[D3D11Renderer] ResizeBuffers failed hr=0x" << std::hex << hr << std::dec << std::endl;
        return;
    }

    CreateRenderTarget(width, height);
}

void D3D11Renderer::SetColors(const float hot[4], const float deep[4])
{
    for (int i = 0; i < 4; ++i)
    {
        m_constants.hotColor[i] = hot[i];
        m_constants.deepColor[i] = deep[i];
        m_targetHot[i] = hot[i];
        m_targetDeep[i] = deep[i];
    }
}

void D3D11Renderer::SetTargetPalette(const float hot[4], const float deep[4], const float color2[4], const float color3[4], float balance, uint64_t generation)
{
    bool colorsChanged = false;
    for (int i = 0; i < 4; ++i)
    {
        if (std::abs(m_targetHot[i] - hot[i]) > 0.001f ||
            std::abs(m_targetDeep[i] - deep[i]) > 0.001f ||
            std::abs(m_targetColor2[i] - color2[i]) > 0.001f ||
            std::abs(m_targetColor3[i] - color3[i]) > 0.001f)
        {
            colorsChanged = true;
            break;
        }
    }
    if (std::abs(m_targetBalance - balance) > 0.001f)
    {
        colorsChanged = true;
    }

    if (colorsChanged || (generation != m_activePaletteGeneration && generation > 0))
    {
        m_activePaletteGeneration = generation;
        for (int i = 0; i < 4; ++i)
        {
            m_targetHot[i] = hot[i];
            m_targetDeep[i] = deep[i];
            m_targetColor2[i] = color2[i];
            m_targetColor3[i] = color3[i];
        }
        m_targetBalance = balance;
        m_isPaletteTransitioning = true;
        if (generation > 0)
        {
            LogMsg("[D3D11Renderer][Gen " + std::to_string(generation) + "] 4-color palette transition started.");
        }
    }
}

void D3D11Renderer::SetTargetPalette(const float hot[4], const float deep[4], float balance, uint64_t generation)
{
    float c2[4] = { (hot[0] + deep[0]) * 0.5f, (hot[1] + deep[1]) * 0.5f, (hot[2] + deep[2]) * 0.5f, 1.0f };
    float c3[4] = { std::min(hot[0] + 0.15f, 1.0f), std::min(hot[1] + 0.15f, 1.0f), std::min(hot[2] + 0.15f, 1.0f), 1.0f };
    SetTargetPalette(hot, deep, c2, c3, balance, generation);
}

void D3D11Renderer::Render(float time, float phase, float bloom, float audioLevel, float beatPulse)
{
    if (!m_context || !m_renderTargetView)
    {
        return;
    }

    // Smooth exponential LERP towards target palette (reconstructed from Lumn 1.1 RimColorModel)
    // Use high-resolution wall-clock time so palette transitions remain smooth and responsive even when playback is paused
    auto nowWall = std::chrono::steady_clock::now();
    float dt = 0.016f;
    if (m_hasLastWallTime)
    {
        std::chrono::duration<float> elapsed = nowWall - m_lastWallTime;
        dt = std::clamp(elapsed.count(), 0.001f, 0.10f);
    }
    m_lastWallTime = nowWall;
    m_hasLastWallTime = true;

    // Rate: ~9% per 60 Hz frame tick (decay constant ~5.8)
    const float lerpFactor = 1.0f - std::exp(-dt * 5.8f);

    float diff = 0.0f;
    for (int i = 0; i < 4; ++i)
    {
        diff += std::abs(m_targetHot[i] - m_constants.hotColor[i]);
        diff += std::abs(m_targetDeep[i] - m_constants.deepColor[i]);
        diff += std::abs(m_targetColor2[i] - m_constants.color2[i]);
        diff += std::abs(m_targetColor3[i] - m_constants.color3[i]);
    }

    if (diff < 0.004f)
    {
        // Snap to target when within threshold
        for (int i = 0; i < 4; ++i)
        {
            m_constants.hotColor[i] = m_targetHot[i];
            m_constants.deepColor[i] = m_targetDeep[i];
            m_constants.color2[i] = m_targetColor2[i];
            m_constants.color3[i] = m_targetColor3[i];
        }
        m_constants.balance = m_targetBalance;
        if (m_isPaletteTransitioning)
        {
            m_isPaletteTransitioning = false;
            LogMsg("[D3D11Renderer][Gen " + std::to_string(m_activePaletteGeneration) + "] Palette transition completed.");
        }
    }
    else
    {
        for (int i = 0; i < 4; ++i)
        {
            m_constants.hotColor[i] += (m_targetHot[i] - m_constants.hotColor[i]) * lerpFactor;
            m_constants.deepColor[i] += (m_targetDeep[i] - m_constants.deepColor[i]) * lerpFactor;
            m_constants.color2[i] += (m_targetColor2[i] - m_constants.color2[i]) * lerpFactor;
            m_constants.color3[i] += (m_targetColor3[i] - m_constants.color3[i]) * lerpFactor;
        }
        m_constants.balance += (m_targetBalance - m_constants.balance) * lerpFactor;
    }

    // Advance organic activation reveal animation
    if (m_isRevealing)
    {
        if (m_hasRevealStartTime)
        {
            auto now = std::chrono::steady_clock::now();
            std::chrono::duration<float> elapsed = now - m_revealStartTime;
            float t = elapsed.count() / REVEAL_DURATION_SECONDS;
            if (t >= 1.0f)
            {
                m_revealProgress = 1.0f;
                m_isRevealing = false;
            }
            else
            {
                // Smooth quintic smootherstep easing for soft, organic propagation
                float tClamped = std::clamp(t, 0.0f, 1.0f);
                m_revealProgress = tClamped * tClamped * tClamped * (tClamped * (tClamped * 6.0f - 15.0f) + 10.0f);
            }
        }
    }
    else
    {
        m_revealProgress = 1.0f;
    }

    if (!m_hasPhaseAnchor)
    {
        m_phaseAnchor = phase;
        m_hasPhaseAnchor = true;
    }

    float relativePhase = phase - m_phaseAnchor;
    if (relativePhase < 0.0f)
    {
        relativePhase += 1.0f;
    }

    m_constants.time = time;
    m_constants.phase = relativePhase;
    m_constants.bloom = bloom;
    m_constants.audioLevel = audioLevel;
    m_constants.beatPulse = beatPulse;
    m_constants.revealProgress = m_revealProgress;
    m_constants.revealCorner = m_revealCorner;

    // Update dynamic constant buffer
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = m_context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr))
    {
        std::memcpy(mapped.pData, &m_constants, sizeof(RimConstants));
        m_context->Unmap(m_constantBuffer.Get(), 0);
    }

    // Clear render target to full transparency (0, 0, 0, 0)
    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_context->ClearRenderTargetView(m_renderTargetView.Get(), clearColor);

    // Bind pipeline state
    ID3D11RenderTargetView* rtvList[] = { m_renderTargetView.Get() };
    m_context->OMSetRenderTargets(1, rtvList, nullptr);

    const float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_context->OMSetBlendState(m_blendState.Get(), blendFactor, 0xffffffff);
    m_context->RSSetViewports(1, &m_viewport);

    m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);

    ID3D11Buffer* cbList[] = { m_constantBuffer.Get() };
    m_context->PSSetConstantBuffers(0, 1, cbList);

    // Draw procedural fullscreen triangle (3 vertices, no VB/IB needed)
    m_context->IASetInputLayout(nullptr);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->Draw(3, 0);

    // Present with V-Sync (interval = 1)
    hr = m_swapChain->Present(1, 0);
    if (FAILED(hr) && hr != DXGI_STATUS_OCCLUDED)
    {
        std::cerr << "[D3D11Renderer] Present failed hr=0x" << std::hex << hr << std::dec << std::endl;
    }
}

} // namespace lumn::render
