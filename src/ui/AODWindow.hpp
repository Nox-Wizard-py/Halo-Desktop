#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <functional>
#include <string>
#include <memory>
#include "../settings/LightingSettings.hpp"

namespace lumn::ui
{

enum class WallpaperEngineType
{
    None,
    Image,
    Video,
    Scene,
    Web,
    Application
};

enum class WallpaperScalingMode
{
    Fit,      // Aspect-fit: preserves entire composition, zero cropping, zero distortion
    Cover,    // Aspect-cover / fill: fills screen cropping edges
    Stretch,  // Non-uniform stretch to screen
    Center    // 1:1 unscaled centered
};

struct ActiveWallpaperInfo
{
    bool isWallpaperEngine = false;
    WallpaperEngineType weType = WallpaperEngineType::None;
    std::wstring title;
    std::wstring sourceFilePath;   // original wallpaper file (.png, .mp4, scene.pkg, etc.)
    std::wstring renderImagePath;  // actual image to render (source image, high-res preview/poster)
    WallpaperScalingMode scalingMode = WallpaperScalingMode::Fit;
};

class AODWindow
{
public:
    using ModeChangedCallback = std::function<void(settings::AODMode)>;
    using ContextMenuCallback = std::function<void()>;

    AODWindow();
    ~AODWindow();

    AODWindow(const AODWindow&) = delete;
    AODWindow& operator=(const AODWindow&) = delete;

    bool Initialize(HINSTANCE hInstance, HWND hPlasmaWnd = nullptr, HWND hMediaCardWnd = nullptr);
    void Shutdown();

    void SetMode(settings::AODMode mode);
    settings::AODMode GetMode() const noexcept { return m_mode; }
    bool IsActive() const noexcept { return m_mode != settings::AODMode::None && m_visible; }

    void SetAssociatedWindows(HWND hPlasmaWnd, HWND hMediaCardWnd) noexcept
    {
        m_hPlasmaWnd = hPlasmaWnd;
        m_hMediaCardWnd = hMediaCardWnd;
    }

    void SetOnModeChanged(ModeChangedCallback cb) { m_onModeChanged = std::move(cb); }
    void SetOnContextMenu(ContextMenuCallback cb) { m_onContextMenu = std::move(cb); }

    bool ProcessMessages();
    void Render();

    HWND GetHandle() const noexcept { return m_hWnd; }
    uint32_t GetWidth() const noexcept { return m_width; }
    uint32_t GetHeight() const noexcept { return m_height; }

    // Helper for testing wallpaper retrieval
    static bool IsWallpaperEngineRunning();
    static bool FindWallpaperEngineConfig(std::wstring& outConfigPath);
    static ActiveWallpaperInfo QueryActiveWallpaper();
    static std::wstring QueryCurrentWallpaperPath();

    const ActiveWallpaperInfo& GetActiveWallpaperInfo() const noexcept { return m_activeWallpaperInfo; }

private:
    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    void CreateDeviceResources();
    void DiscardDeviceResources();
    void LoadWallpaperBitmap();
    void UpdateDimensions();
    void MaintainZOrder();
    void UpdateExecutionState(bool active);

    HWND m_hWnd = nullptr;
    HINSTANCE m_hInstance = nullptr;
    HWND m_hPlasmaWnd = nullptr;
    HWND m_hMediaCardWnd = nullptr;

    settings::AODMode m_mode = settings::AODMode::None;
    bool m_visible = false;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    int m_posX = 0;
    int m_posY = 0;

    std::wstring m_currentWallpaperPath;
    ActiveWallpaperInfo m_activeWallpaperInfo;
    std::wstring m_weConfigPath;
    FILETIME m_weConfigFileTime = {};
    FILETIME m_renderImageFileTime = {};

    Microsoft::WRL::ComPtr<ID2D1Factory> m_d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> m_renderTarget;
    Microsoft::WRL::ComPtr<IWICImagingFactory> m_wicFactory;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_wallpaperBitmap;

    ModeChangedCallback m_onModeChanged;
    ContextMenuCallback m_onContextMenu;

    static constexpr int HOTKEY_ID_AOD_ESC = 3001;
};

} // namespace lumn::ui

