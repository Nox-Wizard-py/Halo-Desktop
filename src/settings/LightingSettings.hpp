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
    Custom  = 1
};

struct LightingConfig
{
    ColorSourceMode colorSource = ColorSourceMode::Artwork;

    // Custom 3-color palette (RGBA float [0.0, 1.0])
    // Default palette: vibrant Cyan (#00D2FF), Neon Purple (#9D00FF), Warm Pink (#FF007F)
    float color1[4] = { 0.00f, 0.82f, 1.00f, 1.00f }; // Color 1: Primary vibrant accent (Cyan)
    float color2[4] = { 0.62f, 0.00f, 1.00f, 1.00f }; // Color 2: Secondary contrasting accent (Purple)
    float color3[4] = { 1.00f, 0.00f, 0.50f, 1.00f }; // Color 3: Tertiary luminous highlight (Magenta/Pink)

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
            colorSource = (dwVal == 1) ? ColorSourceMode::Custom : ColorSourceMode::Artwork;
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

        DWORD dwVal = (colorSource == ColorSourceMode::Custom) ? 1 : 0;
        RegSetValueExW(hKey, L"ColorSource", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dwVal), sizeof(dwVal));

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

        auto saveFloatPercent = [&](const wchar_t* name, float val) {
            DWORD percent = static_cast<DWORD>(std::clamp(val * 100.0f + 0.5f, 20.0f, 300.0f));
            RegSetValueExW(hKey, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&percent), sizeof(percent));
        };

        saveFloatPercent(L"Thickness", thickness);
        saveFloatPercent(L"GlowIntensity", glowIntensity);
        saveFloatPercent(L"AnimationSpeed", animationSpeed);

        RegCloseKey(hKey);
        return true;
    }
};

} // namespace lumn::settings

