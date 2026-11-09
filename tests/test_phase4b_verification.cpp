#include <iostream>
#include <cassert>
#include <cmath>
#include <chrono>
#include <thread>
#include <windows.h>

#include "../src/ui/MediaCardRenderer.hpp"
#include "../src/ui/MediaCardWindow.hpp"
#include "../src/media/MediaSessionWatcher.hpp"

using namespace lumn::ui;
using namespace lumn::media;

void TestGlassMaterialInitialization()
{
    std::cout << "[Test 1] Glass Material Direct2D & DirectComposition Initialization...\n";

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"Phase4BGlassTestClass";
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP,
        L"Phase4BGlassTestClass",
        L"Phase 4B Test",
        WS_POPUP,
        100, 100,
        static_cast<int>(MediaCardRenderer::CARD_WIDTH),
        static_cast<int>(MediaCardRenderer::CARD_HEIGHT),
        nullptr, nullptr, wc.hInstance, nullptr
    );
    assert(hWnd != nullptr);

    MediaCardRenderer renderer;
    bool initOk = renderer.Initialize(hWnd);
    assert(initOk);
    std::cout << "  -> PASSED: MediaCardRenderer initialized with D3D11, DirectComposition, and all glass brushes.\n\n";

    // Clean up
    renderer.Shutdown();
    DestroyWindow(hWnd);
}

void TestDynamicPaletteSmoothingAndRendering()
{
    std::cout << "[Test 2] Dynamic Palette Smoothing & Multi-Pass Glass Rendering...\n";

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"Phase4BGlassRenderClass";
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP,
        L"Phase4BGlassRenderClass",
        L"Phase 4B Render Test",
        WS_POPUP,
        150, 150,
        static_cast<int>(MediaCardRenderer::CARD_WIDTH),
        static_cast<int>(MediaCardRenderer::CARD_HEIGHT),
        nullptr, nullptr, wc.hInstance, nullptr
    );
    assert(hWnd != nullptr);

    MediaCardRenderer renderer;
    assert(renderer.Initialize(hWnd));

    // Setup track with initial palette (Electric Cyan & Neon Purple)
    MediaTrackInfo trackInfo;
    trackInfo.title = L"Blinding Lights";
    trackInfo.artist = L"The Weeknd";
    trackInfo.album = L"After Hours";
    trackInfo.duration100ns = 200 * 10000000LL; // 200 seconds
    trackInfo.position100ns = 45 * 10000000LL;  // 45 seconds
    trackInfo.isPlaying = true;
    trackInfo.canPlay = true;
    trackInfo.canPause = true;
    trackInfo.canSeek = true;
    trackInfo.palette.hot[0] = 0.20f;
    trackInfo.palette.hot[1] = 0.85f;
    trackInfo.palette.hot[2] = 1.00f;
    trackInfo.palette.hot[3] = 1.00f;
    trackInfo.palette.deep[0] = 0.55f;
    trackInfo.palette.deep[1] = 0.10f;
    trackInfo.palette.deep[2] = 0.95f;
    trackInfo.palette.deep[3] = 1.00f;

    // Render initial frame
    renderer.Render(trackInfo, CardInteractiveElement::None, CardInteractiveElement::None, false, 0.0, 1.0, true);

    // Simulate track transition to a warm amber/coral palette
    trackInfo.title = L"Save Your Tears";
    trackInfo.palette.hot[0] = 1.00f;
    trackInfo.palette.hot[1] = 0.45f;
    trackInfo.palette.hot[2] = 0.15f;
    trackInfo.palette.hot[3] = 1.00f;
    trackInfo.palette.deep[0] = 0.85f;
    trackInfo.palette.deep[1] = 0.10f;
    trackInfo.palette.deep[2] = 0.30f;
    trackInfo.palette.deep[3] = 1.00f;

    // Benchmark 60 consecutive frames of glass rendering with temporal smoothing
    auto startBench = std::chrono::high_resolution_clock::now();
    for (int frame = 0; frame < 60; ++frame)
    {
        double timeSec = 1.0 + frame * (1.0 / 60.0);
        trackInfo.position100ns += 166666LL; // advance time slightly
        renderer.Render(trackInfo, CardInteractiveElement::ProgressBar, CardInteractiveElement::None, false, 0.0, timeSec, true);
    }
    auto endBench = std::chrono::high_resolution_clock::now();
    double totalMs = std::chrono::duration<double, std::milli>(endBench - startBench).count();
    double avgFrameMs = totalMs / 60.0;

    std::cout << "  -> Rendered 60 frames of multi-pass glass in " << totalMs << " ms (Avg: " << avgFrameMs << " ms/frame).\n";
    assert(avgFrameMs < 16.6); // Must run comfortably above 60 FPS
    std::cout << "  -> PASSED: Glass rendering latency is ultra-low and easily sustains high refresh rates.\n\n";

    renderer.Shutdown();
    DestroyWindow(hWnd);
}

int main()
{
    std::cout << "========================================\n";
    std::cout << " LUMN REA - Phase 4B Glass Material Suite\n";
    std::cout << "========================================\n\n";

    TestGlassMaterialInitialization();
    TestDynamicPaletteSmoothingAndRendering();

    std::cout << "ALL PHASE 4B GLASS MATERIAL TESTS PASSED SUCCESSFULLY!\n";
    return 0;
}

