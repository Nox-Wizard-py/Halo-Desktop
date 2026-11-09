#pragma once

#include <cstdint>
#include <string>
#include <windows.h>
#include <algorithm>
#include <cmath>

namespace lumn::settings
{

enum class ColorSourceMode : int
{
    Artwork = 0,
    Mixed   = 1,
    Solid   = 2
};

enum class AODMode : int
{
    None      = 0,
    Black     = 1,
    Wallpaper = 2
};

enum class CardMaterial : int
{
    LiquidGlass   = 0,
    Solid         = 1,
    Glassmorphism = 2 // Disabled / Coming Soon
};

enum class SolidColorMode : int
{
    DynamicArtwork = 0,
    MixCustom      = 1
};

struct MediaCardConfig
{
    CardMaterial   material       = CardMaterial::LiquidGlass;
    SolidColorMode solidColorMode = SolidColorMode::DynamicArtwork;

    // Custom 3-colour palette for Solid Mix Custom mode (RGBA float [0.0, 1.0])
    // Elegant deep Material 3 tones with great contrast for white text
    float customColor1[4] = { 0.12f, 0.22f, 0.35f, 1.0f }; // Slate Navy
    float customColor2[4] = { 0.18f, 0.12f, 0.28f, 1.0f }; // Deep Indigo
    float customColor3[4] = { 0.08f, 0.18f, 0.24f, 1.0f }; // Midnight Teal

    void ResetToDefaults()
    {
        material = CardMaterial::LiquidGlass;
        solidColorMode = SolidColorMode::DynamicArtwork;
        customColor1[0] = 0.12f; customColor1[1] = 0.22f; customColor1[2] = 0.35f; customColor1[3] = 1.0f;
        customColor2[0] = 0.18f; customColor2[1] = 0.12f; customColor2[2] = 0.28f; customColor2[3] = 1.0f;
        customColor3[0] = 0.08f; customColor3[1] = 0.18f; customColor3[2] = 0.24f; customColor3[3] = 1.0f;
    }

    bool Load(HKEY hParent = nullptr)
    {
        HKEY hKey = nullptr;
        bool shouldClose = false;
        if (hParent)
        {
            if (RegOpenKeyExW(hParent, L"MediaCard", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
            {
                shouldClose = true;
            }
        }
        if (!hKey)
        {
            if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\HaloDesktop\\MediaCard", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
            {
                shouldClose = true;
            }
        }
        if (!hKey)
        {
            return false;
        }

        DWORD dwVal = 0;
        DWORD dwSize = sizeof(dwVal);
        DWORD dwType = 0;

        if (RegQueryValueExW(hKey, L"Material", nullptr, &dwType, reinterpret_cast<LPBYTE>(&dwVal), &dwSize) == ERROR_SUCCESS)
        {
            if (dwVal == 1) material = CardMaterial::Solid;
            else if (dwVal == 2) material = CardMaterial::Glassmorphism;
            else material = CardMaterial::LiquidGlass;
        }

        dwSize = sizeof(dwVal);
        if (RegQueryValueExW(hKey, L"SolidColorMode", nullptr, &dwType, reinterpret_cast<LPBYTE>(&dwVal), &dwSize) == ERROR_SUCCESS)
        {
            solidColorMode = (dwVal == 1) ? SolidColorMode::MixCustom : SolidColorMode::DynamicArtwork;
        }

        auto loadCol = [&](const wchar_t* name, float col[4]) {
            dwSize = sizeof(dwVal);
            if (RegQueryValueExW(hKey, name, nullptr, &dwType, reinterpret_cast<LPBYTE>(&dwVal), &dwSize) == ERROR_SUCCESS)
            {
                col[0] = static_cast<float>((dwVal >> 16) & 0xFF) / 255.0f;
                col[1] = static_cast<float>((dwVal >> 8) & 0xFF) / 255.0f;
                col[2] = static_cast<float>(dwVal & 0xFF) / 255.0f;
                col[3] = 1.0f;
            }
        };

        loadCol(L"CustomColor1", customColor1);
        loadCol(L"CustomColor2", customColor2);
        loadCol(L"CustomColor3", customColor3);

        if (shouldClose && hKey) RegCloseKey(hKey);
        return true;
    }

    bool Save(HKEY hParent = nullptr) const
    {
        HKEY hKey = nullptr;
        bool shouldClose = false;
        if (hParent)
        {
            if (RegCreateKeyExW(hParent, L"MediaCard", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
            {
                shouldClose = true;
            }
        }
        if (!hKey)
        {
            if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\HaloDesktop\\MediaCard", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
            {
                shouldClose = true;
            }
        }
        if (!hKey)
        {
            return false;
        }

        DWORD dwMat = static_cast<DWORD>(material);
        RegSetValueExW(hKey, L"Material", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dwMat), sizeof(dwMat));

        DWORD dwMode = static_cast<DWORD>(solidColorMode);
        RegSetValueExW(hKey, L"SolidColorMode", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dwMode), sizeof(dwMode));

        auto saveCol = [&](const wchar_t* name, const float col[4]) {
            uint32_t r = std::clamp(static_cast<int>(col[0] * 255.0f + 0.5f), 0, 255);
            uint32_t g = std::clamp(static_cast<int>(col[1] * 255.0f + 0.5f), 0, 255);
            uint32_t b = std::clamp(static_cast<int>(col[2] * 255.0f + 0.5f), 0, 255);
            DWORD packed = (r << 16) | (g << 8) | b;
            RegSetValueExW(hKey, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&packed), sizeof(packed));
        };

        saveCol(L"CustomColor1", customColor1);
        saveCol(L"CustomColor2", customColor2);
        saveCol(L"CustomColor3", customColor3);

        if (shouldClose && hKey) RegCloseKey(hKey);
        return true;
    }
};

struct LightingConfig
{
    ColorSourceMode colorSource = ColorSourceMode::Artwork;
    AODMode         aodMode     = AODMode::None;
    MediaCardConfig mediaCard;

    // Mixed mode: 3 independently configurable colors (RGBA float [0.0, 1.0])
    float color1[4] = { 0.00f, 0.82f, 1.00f, 1.00f }; // Color 1: Primary vibrant accent (Cyan #00D2FF)
    float color2[4] = { 0.62f, 0.00f, 1.00f, 1.00f }; // Color 2: Secondary contrasting accent (Purple #9E00FF)
    float color3[4] = { 1.00f, 0.00f, 0.50f, 1.00f }; // Color 3: Tertiary luminous highlight (Magenta/Pink #FF007F)

    // Solid mode: 1 independently configurable solid color (RGBA float [0.0, 1.0])
    float solidColor[4] = { 0.00f, 0.82f, 1.00f, 1.00f }; // Solid Color (Default Cyan #00D2FF)

    // Appearance multipliers
    float thickness      = 1.0f; // [0.2x, 3.0x], default 1.0
    float glowIntensity  = 1.0f; // [0.2x, 3.0x], default 1.0
    float animationSpeed = 1.0f; // [0.2x, 3.0x], default 1.0

    // Registry persistence: HKEY_CURRENT_USER\Software\HaloDesktop\Lighting
    bool Load()
    {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\HaloDesktop\\Lighting", 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        {
            return false;
        }

        DWORD dwVal = 0;
        DWORD dwSize = sizeof(dwVal);
        DWORD dwType = 0;

        if (RegQueryValueExW(hKey, L"ColorSource", nullptr, &dwType, reinterpret_cast<LPBYTE>(&dwVal), &dwSize) == ERROR_SUCCESS)
        {
            if (dwVal == 2)
            {
                colorSource = ColorSourceMode::Solid;
            }
            else if (dwVal == 1)
            {
                colorSource = ColorSourceMode::Mixed;
            }
            else
            {
                colorSource = ColorSourceMode::Artwork;
            }
        }

        dwSize = sizeof(dwVal);
        if (RegQueryValueExW(hKey, L"AODMode", nullptr, &dwType, reinterpret_cast<LPBYTE>(&dwVal), &dwSize) == ERROR_SUCCESS)
        {
            if (dwVal == 1)
            {
                aodMode = AODMode::Black;
            }
            else if (dwVal == 2)
            {
                aodMode = AODMode::Wallpaper;
            }
            else
            {
                aodMode = AODMode::None;
            }
        }

        auto loadColor = [&](const wchar_t* name, float col[4]) {
            dwSize = sizeof(dwVal);
            if (RegQueryValueExW(hKey, name, nullptr, &dwType, reinterpret_cast<LPBYTE>(&dwVal), &dwSize) == ERROR_SUCCESS)
            {
                col[0] = static_cast<float>((dwVal >> 16) & 0xFF) / 255.0f;
                col[1] = static_cast<float>((dwVal >> 8) & 0xFF) / 255.0f;
                col[2] = static_cast<float>(dwVal & 0xFF) / 255.0f;
                col[3] = 1.0f;
            }
        };

        loadColor(L"Color1", color1);
        loadColor(L"Color2", color2);
        loadColor(L"Color3", color3);
        loadColor(L"SolidColor", solidColor);

        auto loadFloatPercent = [&](const wchar_t* name, float& val) {
            dwSize = sizeof(dwVal);
            if (RegQueryValueExW(hKey, name, nullptr, &dwType, reinterpret_cast<LPBYTE>(&dwVal), &dwSize) == ERROR_SUCCESS)
            {
                if (dwVal >= 20 && dwVal <= 300)
                {
                    val = static_cast<float>(dwVal) / 100.0f;
                }
            }
        };

        loadFloatPercent(L"Thickness", thickness);
        loadFloatPercent(L"GlowIntensity", glowIntensity);
        loadFloatPercent(L"AnimationSpeed", animationSpeed);

        mediaCard.Load(hKey);

        RegCloseKey(hKey);
        return true;
    }

    bool Save() const
    {
        HKEY hKey = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\HaloDesktop\\Lighting", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
        {
            return false;
        }

        DWORD dwVal = static_cast<DWORD>(colorSource);
        RegSetValueExW(hKey, L"ColorSource", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dwVal), sizeof(dwVal));

        DWORD dwAod = static_cast<DWORD>(aodMode);
        RegSetValueExW(hKey, L"AODMode", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dwAod), sizeof(dwAod));

        auto saveColor = [&](const wchar_t* name, const float col[4]) {
            uint32_t r = std::clamp(static_cast<int>(col[0] * 255.0f + 0.5f), 0, 255);
            uint32_t g = std::clamp(static_cast<int>(col[1] * 255.0f + 0.5f), 0, 255);
            uint32_t b = std::clamp(static_cast<int>(col[2] * 255.0f + 0.5f), 0, 255);
            DWORD packed = (r << 16) | (g << 8) | b;
            RegSetValueExW(hKey, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&packed), sizeof(packed));
        };

        saveColor(L"Color1", color1);
        saveColor(L"Color2", color2);
        saveColor(L"Color3", color3);
        saveColor(L"SolidColor", solidColor);

        auto saveFloatPercent = [&](const wchar_t* name, float val) {
            DWORD percent = static_cast<DWORD>(std::clamp(val * 100.0f + 0.5f, 20.0f, 300.0f));
            RegSetValueExW(hKey, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&percent), sizeof(percent));
        };

        saveFloatPercent(L"Thickness", thickness);
        saveFloatPercent(L"GlowIntensity", glowIntensity);
        saveFloatPercent(L"AnimationSpeed", animationSpeed);

        mediaCard.Save(hKey);

        RegCloseKey(hKey);
        return true;
    }
};

} // namespace lumn::settings

