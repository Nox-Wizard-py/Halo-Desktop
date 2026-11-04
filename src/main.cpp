#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <fstream>
#include <string>
#include <cmath>

#pragma comment(lib, "shell32.lib")

#include "platform/Window.hpp"
#include "render/D3D11Renderer.hpp"
#include "audio/MusicSyncClock.hpp"
#include "audio/WasapiLoopback.hpp"
#include "media/MediaSessionWatcher.hpp"
#include "ui/MediaCardWindow.hpp"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, PWSTR /*pCmdLine*/, int /*nCmdShow*/)
{
    // Ensure thread is attached to the interactive "default" desktop if launched from an isolated runner/sandbox
    HDESK hCurrentDesk = GetThreadDesktop(GetCurrentThreadId());
    char deskName[128] = {};
    if (GetUserObjectInformationA(hCurrentDesk, UOI_NAME, deskName, sizeof(deskName), nullptr))
    {
        if (_stricmp(deskName, "default") != 0)
        {
            HDESK hDefault = OpenDesktopA("default", 0, FALSE, GENERIC_ALL);
            if (hDefault)
            {
                SetThreadDesktop(hDefault);
                CloseDesktop(hDefault);
            }
        }
    }

    std::ofstream logFile("lumn_debug.log", std::ios::app);
    logFile << "[Lumn] Launched. Raw command line: " << GetCommandLineA() << "\n";

    // Parse command-line arguments using standard GetCommandLineW
    float testExitAfter = -1.0f; // < 0 means run continuously
    float customCornerRadius = 0.0f; // Windows default rectangular
    float customThickness = 1.0f;
    bool showCard = true; // Phase 4A Floating Media Card enabled by default
    bool useLiquidGlass = true; // Phase 4B.1 Liquid Glass prototype enabled by default

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv)
    {
        logFile << "[Lumn] Argc = " << argc << "\n";
        for (int i = 0; i < argc; ++i)
        {
            char argBuf[512] = { 0 };
            WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, argBuf, sizeof(argBuf), nullptr, nullptr);
            logFile << "  arg[" << i << "] = " << argBuf << "\n";
        }

        for (int i = 1; i < argc; ++i)
        {
            std::wstring arg = argv[i];
            if ((arg == L"--test-exit-after" || arg == L"-t") && i + 1 < argc)
            {
                testExitAfter = std::stof(argv[++i]);
                logFile << "[Lumn] Parsed testExitAfter = " << testExitAfter << "\n";
            }
            else if (arg == L"--corner-radius" && i + 1 < argc)
            {
                customCornerRadius = std::stof(argv[++i]);
            }
            else if (arg == L"--thickness" && i + 1 < argc)
            {
                customThickness = std::stof(argv[++i]);
            }
            else if (arg == L"--hide-card")
            {
                showCard = false;
            }
            else if (arg == L"--show-card")
            {
                showCard = true;
            }
            else if (arg == L"--procedural-glass" || arg == L"--fallback-glass")
            {
                useLiquidGlass = false;
            }
            else if (arg == L"--liquid-glass")
            {
                useLiquidGlass = true;
            }
        }
        LocalFree(argv);
    }
    logFile.flush();

    lumn::platform::Window window;
    lumn::render::D3D11Renderer renderer;

    if (customCornerRadius > 0.0f)
    {
        renderer.SetCornerRadius(customCornerRadius);
    }
    if (customThickness != 1.0f)
    {
        renderer.SetThickness(customThickness);
    }

    logFile << "[Lumn] Initializing window..." << std::endl;
    bool initWindow = window.Initialize(hInstance, [&renderer](uint32_t width, uint32_t height) {
        renderer.Resize(width, height);
    });

    logFile << "[Lumn] window.Initialize returned: " << initWindow << std::endl;
    if (!initWindow)
    {
        logFile << "[Lumn] ERROR: window.Initialize failed." << std::endl;
        return 1;
    }

    logFile << "[Lumn] Initializing renderer with size " << window.GetWidth() << "x" << window.GetHeight() << "..." << std::endl;
    std::wstring shaderPath = L"plasmaRim.hlsl";
    bool initRenderer = renderer.Initialize(window.GetHandle(), window.GetWidth(), window.GetHeight(), shaderPath);
    logFile << "[Lumn] renderer.Initialize returned: " << initRenderer << std::endl;
    if (!initRenderer)
    {
        logFile << "[Lumn] ERROR: renderer.Initialize failed." << std::endl;
        return 1;
    }

    // Initialize Phase 2 MusicSyncClock & WASAPI Loopback Capture
    auto clock = std::make_shared<lumn::audio::MusicSyncClock>();
    lumn::audio::WasapiLoopback wasapi;
    bool wasapiStarted = wasapi.Start(clock);
    logFile << "[Lumn] WASAPI audio capture started: " << wasapiStarted << std::endl;

    // Initialize Phase 3 GSMTC Media Session Watcher & Artwork Extractor
    auto mediaWatcher = std::make_shared<lumn::media::MediaSessionWatcher>();
    bool mediaStarted = mediaWatcher->Start();
    logFile << "[Lumn] GSMTC media session watcher started: " << mediaStarted << std::endl;

    // Initialize Phase 4A Floating Media Card Window
    lumn::ui::MediaCardWindow mediaCard;
    bool cardInitialized = mediaCard.Initialize(hInstance, mediaWatcher);
    logFile << "[Lumn] MediaCardWindow.Initialize returned: " << cardInitialized << std::endl;
    if (cardInitialized)
    {
        mediaCard.SetLiquidGlassEnabled(useLiquidGlass);
        logFile << "[Lumn] MediaCardWindow liquid glass mode: " << (useLiquidGlass ? "ENABLED" : "DISABLED (Fallback Glass)") << std::endl;
        if (showCard)
        {
            mediaCard.Show();
            logFile << "[Lumn] MediaCardWindow shown (Ctrl+Shift+M toggles visibility, Ctrl+Shift+G toggles glass mode)." << std::endl;
        }
    }

    // High-resolution timer setup
    LARGE_INTEGER frequency, startTime, currentTime;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&startTime);

    // Animation & message loop
    logFile << "[Lumn] Entering animation loop. IsRunning=" << window.IsRunning() << " testExitAfter=" << testExitAfter << std::endl;

    uint32_t frameCount = 0;
    while (window.IsRunning())
    {
        if (!window.ProcessMessages())
        {
            logFile << "[Lumn] ProcessMessages returned false, exiting loop." << std::endl;
            break;
        }

        // Pump media card window messages
        mediaCard.ProcessMessages();

        QueryPerformanceCounter(&currentTime);
        double elapsedSeconds = static_cast<double>(currentTime.QuadPart - startTime.QuadPart) / frequency.QuadPart;
        float timeSec = static_cast<float>(elapsedSeconds);

        // Optional automated timeout exit
        if (testExitAfter > 0.0f && timeSec >= testExitAfter)
        {
            logFile << "[Lumn] testExitAfter reached (" << timeSec << "s >= " << testExitAfter << "s). Exiting cleanly." << std::endl;
            break;
        }

        // Phase 3 & 3.1: Query current media session track and target palette with generation tracking
        auto trackInfo = mediaWatcher->GetCurrentTrackInfo();
        renderer.SetTargetPalette(trackInfo.palette.hot, trackInfo.palette.deep, trackInfo.palette.balance, trackInfo.generation);

        // Phase 2.1: Continuous audio-reactive motion with layered beat pulses
        // When audio is quiet/idle, Tick() smoothly transitions to restrained perimeter drift
        lumn::audio::AudioRenderState audioState = clock->Tick(timeSec);

        // Render pass-through rim lighting overlay
        renderer.Render(timeSec, audioState.phase, audioState.bloom, audioState.energy, audioState.beatPulse);

        // Render interactive floating media card
        if (mediaCard.IsVisible())
        {
            mediaCard.Render(timeSec);
        }

        frameCount++;
    }

    logFile << "[Lumn] Exited animation loop after " << frameCount << " frames. Shutting down." << std::endl;
    logFile.close();

    mediaCard.Shutdown();
    mediaWatcher->Stop();
    wasapi.Stop();
    renderer.Shutdown();
    window.Shutdown();

    return 0;
}
