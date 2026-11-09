#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <iostream>
#include <fstream>
#include <string>
#include <cmath>

#pragma comment(lib, "shell32.lib")

#include "resource.h"
#include "platform/Window.hpp"
#include "render/D3D11Renderer.hpp"
#include "audio/MusicSyncClock.hpp"
#include "audio/WasapiLoopback.hpp"
#include "media/MediaSessionWatcher.hpp"
#include "ui/MediaCardWindow.hpp"
#include "settings/LightingSettings.hpp"
#include "ui/LightingStudioWindow.hpp"
#include "ui/AODWindow.hpp"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, PWSTR /*pCmdLine*/, int /*nCmdShow*/)
{
    // Establish explicit Application User Model ID for Windows taskbar identity and window grouping
    SetCurrentProcessExplicitAppUserModelID(L"Halo.Desktop.App");

    // Single-instance enforcement via named mutex
    const wchar_t* const SINGLE_INSTANCE_MUTEX_NAME = L"Local\\HaloDesktop_SingleInstance_Mutex_9F8D3B";
    const wchar_t* const ACTIVATE_INSTANCE_MSG_NAME = L"HaloDesktop_ActivateInstance_9F8D3B";

    UINT wmActivateInstance = RegisterWindowMessageW(ACTIVATE_INSTANCE_MSG_NAME);

    HANDLE hSingleInstanceMutex = CreateMutexW(nullptr, TRUE, SINGLE_INSTANCE_MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        // An active instance is already running; signal it to reveal itself and exit cleanly
        PostMessageW(HWND_BROADCAST, wmActivateInstance, 0, 0);
        if (hSingleInstanceMutex)
        {
            CloseHandle(hSingleInstanceMutex);
        }
        return 0;
    }

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
    logFile << std::unitbuf;
    logFile << "[Halo] Launched. Raw command line: " << GetCommandLineA() << "\n";

    // Parse command-line arguments using standard GetCommandLineW
    float testExitAfter = -1.0f; // < 0 means run continuously
    float customCornerRadius = 0.0f; // Windows default rectangular
    float customThickness = 1.0f;
    bool showCard = true; // Phase 4A Floating Media Card enabled by default
    bool useLiquidGlass = true; // Phase 4B.1 Liquid Glass prototype enabled by default
    int forcedCorner = -1; // -1 = Random (normal operation), 0..3 = Deterministic corner override
    lumn::settings::AODMode cliAodMode = lumn::settings::AODMode::None;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv)
    {
        logFile << "[Halo] Argc = " << argc << "\n";
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
                logFile << "[Halo] Parsed testExitAfter = " << testExitAfter << "\n";
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
            else if (arg == L"--corner" && i + 1 < argc)
            {
                forcedCorner = std::stoi(argv[++i]);
                logFile << "[Halo] Deterministic forced corner = " << forcedCorner << "\n";
            }
            else if (arg == L"--aod-black")
            {
                cliAodMode = lumn::settings::AODMode::Black;
                logFile << "[Halo] Initial AOD mode set to Black via CLI.\n";
            }
            else if (arg == L"--aod-wallpaper")
            {
                cliAodMode = lumn::settings::AODMode::Wallpaper;
                logFile << "[Halo] Initial AOD mode set to Wallpaper via CLI.\n";
            }
        }
        LocalFree(argv);
    }
    logFile.flush();

    lumn::platform::Window window;
    lumn::render::D3D11Renderer renderer;

    // Load persisted lighting studio configuration
    lumn::settings::LightingConfig lightingConfig;
    lightingConfig.Load();

    if (customCornerRadius > 0.0f)
    {
        renderer.SetCornerRadius(customCornerRadius);
    }
    if (customThickness != 1.0f)
    {
        lightingConfig.thickness = customThickness;
    }
    renderer.SetThickness(lightingConfig.thickness);
    renderer.SetGlowMultiplier(lightingConfig.glowIntensity);

    logFile << "[Halo] Initializing window..." << std::endl;
    bool initWindow = window.Initialize(hInstance, [&renderer](uint32_t width, uint32_t height) {
        renderer.Resize(width, height);
    });

    logFile << "[Halo] window.Initialize returned: " << initWindow << std::endl;
    if (!initWindow)
    {
        logFile << "[Halo] ERROR: window.Initialize failed." << std::endl;
        return 1;
    }

    logFile << "[Halo] Initializing renderer with size " << window.GetWidth() << "x" << window.GetHeight() << "..." << std::endl;
    std::wstring shaderPath = L"plasmaRim.hlsl";
    bool initRenderer = renderer.Initialize(window.GetHandle(), window.GetWidth(), window.GetHeight(), shaderPath);
    logFile << "[Halo] renderer.Initialize returned: " << initRenderer << std::endl;
    if (!initRenderer)
    {
        logFile << "[Halo] ERROR: renderer.Initialize failed." << std::endl;
        return 1;
    }

    // Initialize Phase 2 MusicSyncClock & WASAPI Loopback Capture
    auto clock = std::make_shared<lumn::audio::MusicSyncClock>();
    lumn::audio::WasapiLoopback wasapi;
    bool wasapiStarted = wasapi.Start(clock);
    logFile << "[Halo] WASAPI audio capture started: " << wasapiStarted << std::endl;

    // Initialize Phase 3 GSMTC Media Session Watcher & Artwork Extractor
    auto mediaWatcher = std::make_shared<lumn::media::MediaSessionWatcher>();
    bool mediaStarted = mediaWatcher->Start();
    logFile << "[Halo] GSMTC media session watcher started: " << mediaStarted << std::endl;

    // Initialize Phase 4A Floating Media Card Window
    lumn::ui::MediaCardWindow mediaCard;
    bool cardInitialized = mediaCard.Initialize(hInstance, mediaWatcher);
    logFile << "[Halo] MediaCardWindow.Initialize returned: " << cardInitialized << std::endl;
    if (cardInitialized)
    {
        mediaCard.SetLiquidGlassEnabled(useLiquidGlass);
        mediaCard.SetCardMaterial(lightingConfig.mediaCard.material);
        mediaCard.SetSolidColorMode(lightingConfig.mediaCard.solidColorMode);
        mediaCard.SetCustomColors(lightingConfig.mediaCard.customColor1,
                                  lightingConfig.mediaCard.customColor2,
                                  lightingConfig.mediaCard.customColor3);
        logFile << "[Halo] MediaCardWindow liquid glass mode: " << (useLiquidGlass ? "ENABLED" : "DISABLED (Fallback Glass)") << std::endl;
        if (showCard)
        {
            mediaCard.Show();
            logFile << "[Halo] MediaCardWindow shown (Ctrl+Shift+M toggles visibility, Ctrl+Shift+G toggles glass mode)." << std::endl;
        }
    }

    // Initialize Lighting Studio Settings Window
    lumn::ui::LightingStudioWindow lightingStudio;
    lightingStudio.Initialize(hInstance, &lightingConfig, [&renderer, &lightingConfig, &mediaCard]() {
        renderer.SetThickness(lightingConfig.thickness);
        renderer.SetGlowMultiplier(lightingConfig.glowIntensity);
        mediaCard.SetCardMaterial(lightingConfig.mediaCard.material);
        mediaCard.SetSolidColorMode(lightingConfig.mediaCard.solidColorMode);
        mediaCard.SetCustomColors(lightingConfig.mediaCard.customColor1,
                                  lightingConfig.mediaCard.customColor2,
                                  lightingConfig.mediaCard.customColor3);
    });

    mediaCard.SetOnMaterialChanged([&lightingConfig](lumn::settings::CardMaterial mat) {
        lightingConfig.mediaCard.material = mat;
        lightingConfig.mediaCard.Save();
    });

    mediaCard.SetOnOpenLightingStudio([&lightingStudio]() {
        lightingStudio.Show();
    });

    // Initialize Always-On Display (AOD) Presentation Layer
    lumn::ui::AODWindow aodWindow;
    bool aodInitialized = aodWindow.Initialize(hInstance, window.GetHandle(), mediaCard.GetHandle());
    logFile << "[Halo] AODWindow.Initialize returned: " << aodInitialized << std::endl;

    auto updateAODMode = [&](lumn::settings::AODMode newMode) {
        aodWindow.SetMode(newMode);
        mediaCard.SetAODMode(newMode);
        lightingConfig.aodMode = newMode;
        lightingConfig.Save();
        logFile << "[Halo] AOD mode updated to: " << static_cast<int>(newMode) << std::endl;
    };

    mediaCard.SetOnAODModeChanged([updateAODMode](lumn::settings::AODMode mode) {
        updateAODMode(mode);
    });

    aodWindow.SetOnModeChanged([&mediaCard, &lightingConfig, &logFile](lumn::settings::AODMode mode) {
        mediaCard.SetAODMode(mode);
        lightingConfig.aodMode = mode;
        lightingConfig.Save();
        logFile << "[Halo] AODWindow dismissed/changed mode to: " << static_cast<int>(mode) << std::endl;
    });

    aodWindow.SetOnContextMenu([&mediaCard]() {
        mediaCard.ShowContextMenu();
    });

    // Unified ESC Key Dismissal Routing:
    // ESC dismisses the currently active visible interface in priority order.
    // ESC NEVER terminates the Halo Desktop application process; tray icon remains alive.
    window.SetOnEscape([&]() -> bool {
        // Priority 1: If AOD is active, dismiss AOD
        if (aodWindow.IsActive())
        {
            updateAODMode(lumn::settings::AODMode::None);
            return true;
        }
        // Priority 2: If Lighting Studio is open, close it
        if (lightingStudio.IsVisible())
        {
            lightingStudio.Hide();
            return true;
        }
        // Priority 3: If Media Card is visible, hide it
        if (mediaCard.IsVisible())
        {
            mediaCard.Hide();
            return true;
        }
        // Priority 4: If Plasma Overlay is visible, hide it
        if (window.IsVisible())
        {
            window.Hide();
            return true;
        }
        return true; // Interface dismissed; application remains running in tray
    });

    // Tray Menu toggle for Plasma Rim Overlay
    mediaCard.SetOnTogglePlasma([&window, &renderer, forcedCorner]() {
        if (window.IsVisible())
        {
            window.Hide();
        }
        else
        {
            window.Show();
            renderer.TriggerActivationAnimation(forcedCorner);
        }
    });
    mediaCard.SetPlasmaVisibleChecker([&window]() {
        return window.IsVisible();
    });

    if (cliAodMode != lumn::settings::AODMode::None)
    {
        updateAODMode(cliAodMode);
    }

    // High-resolution timer setup
    LARGE_INTEGER frequency, startTime, currentTime;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&startTime);

    // Trigger initial activation reveal animation right as render loop begins
    renderer.TriggerActivationAnimation(forcedCorner);

    // Animation & message loop
    logFile << "[Halo] Entering animation loop. IsRunning=" << window.IsRunning() << " testExitAfter=" << testExitAfter << std::endl;

    uint32_t frameCount = 0;
    double lastElapsedSeconds = 0.0;
    double accumulatedAnimTime = 0.0;

    bool appRunning = true;
    while (appRunning)
    {
        if (!window.ProcessMessages())
        {
            logFile << "[Halo] ProcessMessages returned false (WM_QUIT), exiting loop." << std::endl;
            appRunning = false;
            break;
        }

        // Pump media card and AOD window messages
        mediaCard.ProcessMessages();
        aodWindow.ProcessMessages();

        QueryPerformanceCounter(&currentTime);
        double elapsedSeconds = static_cast<double>(currentTime.QuadPart - startTime.QuadPart) / frequency.QuadPart;
        float timeSec = static_cast<float>(elapsedSeconds);

        double dt = (lastElapsedSeconds > 0.0) ? std::clamp(elapsedSeconds - lastElapsedSeconds, 0.0, 0.10) : 0.016;
        lastElapsedSeconds = elapsedSeconds;

        // Optional automated timeout exit
        if (testExitAfter > 0.0f && timeSec >= testExitAfter)
        {
            logFile << "[Halo] testExitAfter reached (" << timeSec << "s >= " << testExitAfter << "s). Exiting cleanly." << std::endl;
            break;
        }

        // Low-power background wait when all visual interfaces are dismissed
        bool hasVisibleUI = window.IsVisible() || mediaCard.IsVisible() || lightingStudio.IsVisible() || aodWindow.IsActive();
        if (!hasVisibleUI)
        {
            MsgWaitForMultipleObjectsEx(0, nullptr, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            continue;
        }

        // Query current media session track and target palette with generation tracking
        auto trackInfo = mediaWatcher->GetCurrentTrackInfo();

        if (lightingStudio.IsVisible())
        {
            lightingStudio.SetArtworkPalette(trackInfo.palette);
        }

        // Playback-aware animation freezing: advance continuous wave time only while actively playing
        float speedMultiplier = std::clamp(lightingConfig.animationSpeed, 0.2f, 3.0f);
        if (trackInfo.isPlaying)
        {
            accumulatedAnimTime += dt * speedMultiplier;
        }
        float animTime = static_cast<float>(accumulatedAnimTime);

        // Upload palette to renderer (Artwork, Mixed, or Solid)
        if (lightingConfig.colorSource == lumn::settings::ColorSourceMode::Solid)
        {
            // Solid mode: uniform single color across the entire perimeter (no blending with other colors)
            renderer.SetTargetPalette(
                lightingConfig.solidColor,
                lightingConfig.solidColor,
                lightingConfig.solidColor,
                lightingConfig.solidColor,
                0.50f,
                0
            );
        }
        else if (lightingConfig.colorSource == lumn::settings::ColorSourceMode::Mixed)
        {
            // Mixed mode: smooth interpolation between Color 2 -> Color 1 -> Color 3
            float deep[4] = {
                lightingConfig.color2[0] * 0.35f,
                lightingConfig.color2[1] * 0.35f,
                lightingConfig.color2[2] * 0.35f,
                1.0f
            };
            renderer.SetTargetPalette(
                lightingConfig.color1,
                deep,
                lightingConfig.color2,
                lightingConfig.color3,
                0.50f,
                0
            );
        }
        else
        {
            renderer.SetTargetPalette(
                trackInfo.palette.hot,
                trackInfo.palette.deep,
                trackInfo.palette.color2,
                trackInfo.palette.color3,
                trackInfo.palette.balance,
                trackInfo.generation
            );
        }

        // Audio clock synchronizes phase (preserves phase across speed changes and freezes when paused)
        lumn::audio::AudioRenderState audioState = clock->Tick(elapsedSeconds, trackInfo.isPlaying, speedMultiplier);

        // Render pass-through rim lighting overlay ONLY if window is visible
        if (window.IsVisible())
        {
            renderer.Render(animTime, audioState.phase, audioState.bloom, audioState.energy, audioState.beatPulse);
        }

        // Render interactive floating media card ONLY if media card is visible
        if (mediaCard.IsVisible())
        {
            mediaCard.Render(timeSec);
        }

        frameCount++;
    }

    logFile << "[Halo] Exited animation loop after " << frameCount << " frames. Shutting down." << std::endl;
    logFile.close();

    aodWindow.Shutdown();
    lightingStudio.Shutdown();
    mediaCard.Shutdown();
    mediaWatcher->Stop();
    wasapi.Stop();
    renderer.Shutdown();
    window.Shutdown();

    if (hSingleInstanceMutex)
    {
        ReleaseMutex(hSingleInstanceMutex);
        CloseHandle(hSingleInstanceMutex);
    }

    return 0;
}
