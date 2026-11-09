#include "AODWindow.hpp"
#include "resource.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <vector>
#include <tlhelp32.h>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

namespace lumn::ui
{

static constexpr const wchar_t* AOD_WINDOW_CLASS = L"HaloAODWindowClass";

AODWindow::AODWindow() = default;

AODWindow::~AODWindow()
{
    Shutdown();
}

static std::string ReadFileToString(const std::wstring& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        return "";
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

static std::string ExtractJsonString(const std::string& json, const std::string& key)
{
    std::string searchKey = "\"" + key + "\"";
    size_t keyPos = json.find(searchKey);
    if (keyPos == std::string::npos)
    {
        return "";
    }

    size_t colonPos = json.find(':', keyPos + searchKey.length());
    if (colonPos == std::string::npos)
    {
        return "";
    }

    size_t quoteStart = json.find('"', colonPos + 1);
    if (quoteStart == std::string::npos)
    {
        return "";
    }

    size_t valEnd = quoteStart + 1;
    bool escaped = false;
    while (valEnd < json.length())
    {
        if (escaped)
        {
            escaped = false;
        }
        else if (json[valEnd] == '\\')
        {
            escaped = true;
        }
        else if (json[valEnd] == '"')
        {
            break;
        }
        valEnd++;
    }

    if (valEnd >= json.length())
    {
        return "";
    }

    std::string rawVal = json.substr(quoteStart + 1, valEnd - quoteStart - 1);
    std::string unescaped;
    unescaped.reserve(rawVal.length());
    for (size_t i = 0; i < rawVal.length(); ++i)
    {
        if (rawVal[i] == '\\' && i + 1 < rawVal.length())
        {
            char next = rawVal[i + 1];
            if (next == '/' || next == '\\' || next == '"')
            {
                unescaped += next;
                i++;
                continue;
            }
        }
        unescaped += rawVal[i];
    }
    return unescaped;
}

static std::wstring Utf8ToWide(const std::string& str)
{
    if (str.empty()) return L"";
    int req = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), nullptr, 0);
    if (req <= 0) return L"";
    std::wstring out(req, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), out.data(), req);
    return out;
}

bool AODWindow::IsWallpaperEngineRunning()
{
    if (FindWindowW(L"WPETrayWindow", nullptr) != nullptr)
    {
        return true;
    }
    if (FindWindowW(L"WPEEventWindow", nullptr) != nullptr)
    {
        return true;
    }

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    bool found = false;

    if (Process32FirstW(hSnapshot, &pe))
    {
        do
        {
            if (_wcsicmp(pe.szExeFile, L"wallpaper64.exe") == 0 ||
                _wcsicmp(pe.szExeFile, L"wallpaper32.exe") == 0)
            {
                found = true;
                break;
            }
        } while (Process32NextW(hSnapshot, &pe));
    }

    CloseHandle(hSnapshot);
    return found;
}

bool AODWindow::FindWallpaperEngineConfig(std::wstring& outConfigPath)
{
    auto testConfigExists = [](const std::wstring& path) -> bool {
        DWORD attrs = GetFileAttributesW(path.c_str());
        return (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
    };

    // 1. Check Steam Registry for SteamPath
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        wchar_t steamPath[MAX_PATH] = {};
        DWORD size = sizeof(steamPath);
        DWORD type = 0;
        if (RegQueryValueExW(hKey, L"SteamPath", nullptr, &type, reinterpret_cast<LPBYTE>(steamPath), &size) == ERROR_SUCCESS)
        {
            std::wstring sp = steamPath;
            std::replace(sp.begin(), sp.end(), L'/', L'\\');
            std::wstring candidate = sp + L"\\steamapps\\common\\wallpaper_engine\\config.json";
            if (testConfigExists(candidate))
            {
                RegCloseKey(hKey);
                outConfigPath = candidate;
                return true;
            }
        }
        RegCloseKey(hKey);
    }

    // 2. Check Steam App 431960 Uninstall registry key
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 431960", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        wchar_t installLoc[MAX_PATH] = {};
        DWORD size = sizeof(installLoc);
        DWORD type = 0;
        if (RegQueryValueExW(hKey, L"InstallLocation", nullptr, &type, reinterpret_cast<LPBYTE>(installLoc), &size) == ERROR_SUCCESS)
        {
            std::wstring candidate = std::wstring(installLoc) + L"\\config.json";
            if (testConfigExists(candidate))
            {
                RegCloseKey(hKey);
                outConfigPath = candidate;
                return true;
            }
        }
        RegCloseKey(hKey);
    }

    // 3. Fallback standard Steam install locations
    const wchar_t* standardPaths[] = {
        L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\wallpaper_engine\\config.json",
        L"C:\\Program Files\\Steam\\steamapps\\common\\wallpaper_engine\\config.json",
        L"D:\\Steam\\steamapps\\common\\wallpaper_engine\\config.json",
        L"D:\\SteamLibrary\\steamapps\\common\\wallpaper_engine\\config.json",
        L"E:\\Steam\\steamapps\\common\\wallpaper_engine\\config.json",
        L"E:\\SteamLibrary\\steamapps\\common\\wallpaper_engine\\config.json"
    };

    for (const auto* p : standardPaths)
    {
        if (testConfigExists(p))
        {
            outConfigPath = p;
            return true;
        }
    }

    return false;
}

static std::wstring GetWindowsDesktopWallpaperPath()
{
    wchar_t wallpaperPath[MAX_PATH] = {};

    if (SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, wallpaperPath, 0) && wallpaperPath[0] != L'\0')
    {
        DWORD attrs = GetFileAttributesW(wallpaperPath);
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY))
        {
            return std::wstring(wallpaperPath);
        }
    }

    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD type = 0;
        DWORD size = sizeof(wallpaperPath);
        if (RegQueryValueExW(hKey, L"Wallpaper", nullptr, &type, reinterpret_cast<LPBYTE>(wallpaperPath), &size) == ERROR_SUCCESS)
        {
            RegCloseKey(hKey);
            DWORD attrs = GetFileAttributesW(wallpaperPath);
            if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY))
            {
                return std::wstring(wallpaperPath);
            }
        }
        else
        {
            RegCloseKey(hKey);
        }
    }

    wchar_t appData[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH) > 0)
    {
        std::wstring transcoded = std::wstring(appData) + L"\\Microsoft\\Windows\\Themes\\TranscodedWallpaper";
        DWORD attrs = GetFileAttributesW(transcoded.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY))
        {
            return transcoded;
        }
    }

    return L"";
}

static std::wstring ExtractScenePackageImage(const std::wstring& pkgPath, const std::wstring& wpDir)
{
    // 1. Check if cached extract already exists in wpDir or temp dir
    std::wstring cachedPng = wpDir + L"\\.lumn_scene_bg.png";
    DWORD attrs = GetFileAttributesW(cachedPng.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY))
    {
        return cachedPng;
    }
    std::wstring cachedJpg = wpDir + L"\\.lumn_scene_bg.jpg";
    attrs = GetFileAttributesW(cachedJpg.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY))
    {
        return cachedJpg;
    }

    std::ifstream f(pkgPath, std::ios::binary);
    if (!f.is_open()) return L"";

    uint32_t magicLen = 0;
    if (!f.read(reinterpret_cast<char*>(&magicLen), 4) || magicLen == 0 || magicLen > 64) return L"";
    std::string magic(magicLen, '\0');
    if (!f.read(&magic[0], magicLen)) return L"";

    uint32_t fileCount = 0;
    if (!f.read(reinterpret_cast<char*>(&fileCount), 4) || fileCount == 0 || fileCount > 1000) return L"";

    struct PkgEntry {
        std::string name;
        uint32_t offset = 0;
        uint32_t size = 0;
    };
    std::vector<PkgEntry> entries;
    entries.reserve(fileCount);

    for (uint32_t i = 0; i < fileCount; ++i)
    {
        uint32_t nameLen = 0;
        if (!f.read(reinterpret_cast<char*>(&nameLen), 4) || nameLen > 1024) break;
        std::string name(nameLen, '\0');
        if (!f.read(&name[0], nameLen)) break;
        uint32_t offset = 0, size = 0;
        if (!f.read(reinterpret_cast<char*>(&offset), 4)) break;
        if (!f.read(reinterpret_cast<char*>(&size), 4)) break;
        entries.push_back({ std::move(name), offset, size });
    }

    std::streampos dataStart = f.tellg();

    // Find candidate texture/image entry with the largest size (the primary scene background)
    uint32_t maxTexSize = 0;
    const PkgEntry* bestEntry = nullptr;
    for (const auto& e : entries)
    {
        bool isCandidate = (e.name.find(".tex") != std::string::npos ||
                            e.name.find(".png") != std::string::npos ||
                            e.name.find(".jpg") != std::string::npos ||
                            e.name.find(".jpeg") != std::string::npos);
        if (isCandidate && e.size > maxTexSize)
        {
            maxTexSize = e.size;
            bestEntry = &e;
        }
    }

    if (!bestEntry) return L"";

    f.seekg(dataStart + static_cast<std::streamoff>(bestEntry->offset));
    std::vector<char> buf(bestEntry->size);
    if (!f.read(buf.data(), bestEntry->size)) return L"";

    // Search for embedded PNG or JPEG magic in the texture buffer
    const char pngMagic[] = { '\x89', 'P', 'N', 'G', '\r', '\n', '\x1a', '\n' };
    const char jpgMagic[] = { '\xFF', '\xD8', '\xFF' };

    size_t imgStart = std::string::npos;
    bool isPng = false;

    for (size_t i = 0; i + 8 <= buf.size(); ++i)
    {
        if (memcmp(buf.data() + i, pngMagic, 8) == 0)
        {
            imgStart = i;
            isPng = true;
            break;
        }
        if (memcmp(buf.data() + i, jpgMagic, 3) == 0)
        {
            imgStart = i;
            isPng = false;
            break;
        }
    }

    if (imgStart == std::string::npos) return L"";

    size_t imgSize = buf.size() - imgStart;
    std::wstring outExt = isPng ? L".png" : L".jpg";

    // Attempt writing to wpDir first, fallback to %TEMP% if write-protected
    std::wstring targetPath = wpDir + L"\\.lumn_scene_bg" + outExt;
    std::ofstream out(targetPath, std::ios::binary);
    if (!out.is_open())
    {
        wchar_t tempPath[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tempPath);
        targetPath = std::wstring(tempPath) + L"lumn_scene_bg" + outExt;
        out.open(targetPath, std::ios::binary);
        if (!out.is_open()) return L"";
    }

    out.write(buf.data() + imgStart, imgSize);
    out.close();

    return targetPath;
}

ActiveWallpaperInfo AODWindow::QueryActiveWallpaper()
{
    ActiveWallpaperInfo info;

    // Check if Wallpaper Engine is active
    std::wstring weConfigPath;
    if (IsWallpaperEngineRunning() && FindWallpaperEngineConfig(weConfigPath))
    {
        std::string configContent = ReadFileToString(weConfigPath);
        if (!configContent.empty())
        {
            // Default to Cover for Wallpaper Engine (matches Wallpaper Engine layout 0)
            info.scalingMode = WallpaperScalingMode::Cover;

            // Extract general wallpaperconfig layout if specified
            size_t wpCfgPos = configContent.find("\"wallpaperconfig\"");
            if (wpCfgPos != std::string::npos)
            {
                size_t layoutPos = configContent.find("\"layout\"", wpCfgPos);
                if (layoutPos != std::string::npos && layoutPos < wpCfgPos + 2048)
                {
                    size_t colon = configContent.find(':', layoutPos);
                    if (colon != std::string::npos)
                    {
                        int layoutVal = 0;
                        if (sscanf_s(configContent.c_str() + colon + 1, "%d", &layoutVal) == 1)
                        {
                            if (layoutVal == 0) info.scalingMode = WallpaperScalingMode::Cover;
                            else if (layoutVal == 1) info.scalingMode = WallpaperScalingMode::Fit;
                            else if (layoutVal == 2) info.scalingMode = WallpaperScalingMode::Stretch;
                            else if (layoutVal == 3) info.scalingMode = WallpaperScalingMode::Center;
                        }
                    }
                }
            }

            // Extract the active wallpaper path from selectedwallpapers
            size_t selPos = configContent.find("\"selectedwallpapers\"");
            if (selPos != std::string::npos)
            {
                std::string selSubstr = configContent.substr(selPos, 2048);
                std::string rawWpFile = ExtractJsonString(selSubstr, "file");

                if (!rawWpFile.empty())
                {
                    std::wstring wpFile = Utf8ToWide(rawWpFile);
                    std::replace(wpFile.begin(), wpFile.end(), L'/', L'\\');

                    // Read scaling/alignment setting from wproperties if present
                    size_t wpropPos = configContent.find("\"wproperties\"");
                    if (wpropPos != std::string::npos)
                    {
                        size_t filePos = configContent.find(rawWpFile, wpropPos);
                        if (filePos != std::string::npos)
                        {
                            std::string propSubstr = configContent.substr(filePos, 512);
                            std::string align = ExtractJsonString(propSubstr, "alignment");
                            if (align == "cover")
                            {
                                info.scalingMode = WallpaperScalingMode::Cover;
                            }
                            else if (align == "fit")
                            {
                                info.scalingMode = WallpaperScalingMode::Fit;
                            }
                            else if (align == "stretch")
                            {
                                info.scalingMode = WallpaperScalingMode::Stretch;
                            }
                            else if (align == "center")
                            {
                                info.scalingMode = WallpaperScalingMode::Center;
                            }
                        }
                    }

                    info.sourceFilePath = wpFile;

                    // Inspect directory containing wallpaper for project.json
                    size_t lastSlash = wpFile.find_last_of(L"\\/");
                    std::wstring wpDir = (lastSlash != std::wstring::npos) ? wpFile.substr(0, lastSlash) : L"";
                    std::wstring projectJsonPath = wpDir + L"\\project.json";

                    DWORD pjAttrs = GetFileAttributesW(projectJsonPath.c_str());
                    if (pjAttrs != INVALID_FILE_ATTRIBUTES)
                    {
                        std::string pjContent = ReadFileToString(projectJsonPath);
                        info.title = Utf8ToWide(ExtractJsonString(pjContent, "title"));
                        std::string pjType = ExtractJsonString(pjContent, "type");
                        std::string pjPreview = ExtractJsonString(pjContent, "preview");
                        std::string pjFile = ExtractJsonString(pjContent, "file");

                        bool isScene = (wpFile.size() >= 4 && _wcsicmp(wpFile.c_str() + wpFile.size() - 4, L".pkg") == 0) ||
                                       pjFile.find("scene.json") != std::string::npos ||
                                       pjContent.find("\"type\" : \"scene\"") != std::string::npos ||
                                       pjContent.find("\"type\": \"scene\"") != std::string::npos ||
                                       pjContent.find("\"type\":\"scene\"") != std::string::npos;

                        bool isVideo = pjFile.find(".mp4") != std::string::npos ||
                                       pjFile.find(".webm") != std::string::npos ||
                                       pjContent.find("\"type\" : \"video\"") != std::string::npos ||
                                       pjContent.find("\"type\": \"video\"") != std::string::npos ||
                                       pjContent.find("\"type\":\"video\"") != std::string::npos;

                        bool isWeb = pjFile.find(".html") != std::string::npos ||
                                     pjFile.find(".htm") != std::string::npos ||
                                     pjContent.find("\"type\" : \"web\"") != std::string::npos ||
                                     pjContent.find("\"type\": \"web\"") != std::string::npos ||
                                     pjContent.find("\"type\":\"web\"") != std::string::npos;

                        bool isImage = pjContent.find("\"type\" : \"image\"") != std::string::npos ||
                                       pjContent.find("\"type\": \"image\"") != std::string::npos ||
                                       pjContent.find("\"type\":\"image\"") != std::string::npos;

                        if (isImage)
                        {
                            info.weType = WallpaperEngineType::Image;
                            DWORD fileAttr = GetFileAttributesW(wpFile.c_str());
                            if (fileAttr != INVALID_FILE_ATTRIBUTES && !(fileAttr & FILE_ATTRIBUTE_DIRECTORY))
                            {
                                info.renderImagePath = wpFile;
                            }
                            else
                            {
                                std::wstring altImage = wpDir + L"\\" + Utf8ToWide(pjFile);
                                if (GetFileAttributesW(altImage.c_str()) != INVALID_FILE_ATTRIBUTES)
                                {
                                    info.renderImagePath = altImage;
                                }
                            }
                        }
                        else if (isScene)
                        {
                            info.weType = WallpaperEngineType::Scene;
                            // 1. Try extracting the uncropped full-resolution background artwork directly from scene.pkg
                            std::wstring extracted = ExtractScenePackageImage(wpFile, wpDir);
                            if (!extracted.empty() && GetFileAttributesW(extracted.c_str()) != INVALID_FILE_ATTRIBUTES)
                            {
                                info.renderImagePath = extracted;
                            }
                            else
                            {
                                // 2. Fallback to creator-authored visual preview
                                std::wstring previewPath = wpDir + L"\\" + Utf8ToWide(pjPreview.empty() ? "preview.jpg" : pjPreview);
                                if (GetFileAttributesW(previewPath.c_str()) != INVALID_FILE_ATTRIBUTES)
                                {
                                    info.renderImagePath = previewPath;
                                }
                            }
                        }
                        else if (isVideo)
                        {
                            info.weType = WallpaperEngineType::Video;
                            std::wstring previewPath = wpDir + L"\\" + Utf8ToWide(pjPreview.empty() ? "preview.jpg" : pjPreview);
                            if (GetFileAttributesW(previewPath.c_str()) != INVALID_FILE_ATTRIBUTES)
                            {
                                info.renderImagePath = previewPath;
                            }
                        }
                        else if (isWeb)
                        {
                            info.weType = WallpaperEngineType::Web;
                            std::wstring previewPath = wpDir + L"\\" + Utf8ToWide(pjPreview.empty() ? "preview.jpg" : pjPreview);
                            if (GetFileAttributesW(previewPath.c_str()) != INVALID_FILE_ATTRIBUTES)
                            {
                                info.renderImagePath = previewPath;
                            }
                        }
                        else
                        {
                            info.weType = WallpaperEngineType::Application;
                            std::wstring previewPath = wpDir + L"\\" + Utf8ToWide(pjPreview.empty() ? "preview.jpg" : pjPreview);
                            if (GetFileAttributesW(previewPath.c_str()) != INVALID_FILE_ATTRIBUTES)
                            {
                                info.renderImagePath = previewPath;
                            }
                        }
                    }
                    else
                    {
                        // Direct file without project.json (e.g. custom local user file)
                        DWORD fileAttr = GetFileAttributesW(wpFile.c_str());
                        if (fileAttr != INVALID_FILE_ATTRIBUTES && !(fileAttr & FILE_ATTRIBUTE_DIRECTORY))
                        {
                            std::wstring ext = PathFindExtensionW(wpFile.c_str());
                            std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                            if (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".bmp" || ext == L".webp")
                            {
                                info.weType = WallpaperEngineType::Image;
                                info.renderImagePath = wpFile;
                            }
                            else if (ext == L".pkg")
                            {
                                info.weType = WallpaperEngineType::Scene;
                                std::wstring extracted = ExtractScenePackageImage(wpFile, wpDir);
                                if (!extracted.empty())
                                {
                                    info.renderImagePath = extracted;
                                }
                                else
                                {
                                    std::wstring previewPath = wpDir + L"\\preview.jpg";
                                    if (GetFileAttributesW(previewPath.c_str()) != INVALID_FILE_ATTRIBUTES)
                                    {
                                        info.renderImagePath = previewPath;
                                    }
                                }
                            }
                            else if (ext == L".mp4" || ext == L".webm" || ext == L".avi")
                            {
                                info.weType = WallpaperEngineType::Video;
                                std::wstring previewPath = wpDir + L"\\preview.jpg";
                                if (GetFileAttributesW(previewPath.c_str()) != INVALID_FILE_ATTRIBUTES)
                                {
                                    info.renderImagePath = previewPath;
                                }
                                else
                                {
                                    info.renderImagePath = wpFile;
                                }
                            }
                        }
                    }

                    if (!info.renderImagePath.empty() && GetFileAttributesW(info.renderImagePath.c_str()) != INVALID_FILE_ATTRIBUTES)
                    {
                        info.isWallpaperEngine = true;
                        return info;
                    }
                }
            }
        }
    }

    // Fallback: Windows desktop wallpaper
    info.isWallpaperEngine = false;
    info.weType = WallpaperEngineType::None;
    info.renderImagePath = GetWindowsDesktopWallpaperPath();
    info.sourceFilePath = info.renderImagePath;
    info.title = L"Windows Desktop Wallpaper";
    info.scalingMode = WallpaperScalingMode::Fit;

    return info;
}

std::wstring AODWindow::QueryCurrentWallpaperPath()
{
    return QueryActiveWallpaper().renderImagePath;
}

void AODWindow::UpdateDimensions()
{
    HMONITOR hMonitor = MonitorFromWindow(m_hWnd ? m_hWnd : nullptr, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);

    if (GetMonitorInfoW(hMonitor, &mi))
    {
        m_posX = mi.rcMonitor.left;
        m_posY = mi.rcMonitor.top;
        m_width = static_cast<uint32_t>(mi.rcMonitor.right - mi.rcMonitor.left);
        m_height = static_cast<uint32_t>(mi.rcMonitor.bottom - mi.rcMonitor.top);
    }
    else
    {
        m_posX = 0;
        m_posY = 0;
        m_width = static_cast<uint32_t>(GetSystemMetrics(SM_CXSCREEN));
        m_height = static_cast<uint32_t>(GetSystemMetrics(SM_CYSCREEN));
    }
}

bool AODWindow::Initialize(HINSTANCE hInstance, HWND hPlasmaWnd, HWND hMediaCardWnd)
{
    m_hInstance = hInstance;
    m_hPlasmaWnd = hPlasmaWnd;
    m_hMediaCardWnd = hMediaCardWnd;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Register Window Class
    HICON hIconBig = LoadIconW(m_hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    HICON hIconSm = static_cast<HICON>(LoadImageW(m_hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = m_hInstance;
    wc.hIcon = hIconBig;
    wc.hIconSm = hIconSm;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = AOD_WINDOW_CLASS;

    if (!RegisterClassExW(&wc))
    {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS)
        {
            return false;
        }
    }

    UpdateDimensions();

    // Fix Issue 3: WS_EX_NOACTIVATE ensures mouse clicks on the AOD presentation layer
    // NEVER activate AODWindow or push it in front of the plasma overlay and media card.
    DWORD dwExStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    DWORD dwStyle = WS_POPUP;

    m_hWnd = CreateWindowExW(
        dwExStyle,
        AOD_WINDOW_CLASS,
        L"Halo AOD Presentation",
        dwStyle,
        m_posX,
        m_posY,
        static_cast<int>(m_width),
        static_cast<int>(m_height),
        nullptr,
        nullptr,
        m_hInstance,
        this
    );

    if (!m_hWnd)
    {
        return false;
    }

    if (hIconBig)
    {
        SendMessageW(m_hWnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIconBig));
    }
    if (hIconSm)
    {
        SendMessageW(m_hWnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIconSm));
    }

    // Initialize Direct2D factory
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.GetAddressOf());
    if (FAILED(hr))
    {
        return false;
    }

    // Initialize WIC factory
    hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(m_wicFactory.GetAddressOf())
    );
    if (FAILED(hr))
    {
        return false;
    }

    CreateDeviceResources();

    return true;
}

void AODWindow::CreateDeviceResources()
{
    if (m_renderTarget || !m_hWnd || !m_d2dFactory)
    {
        return;
    }

    // Set 96x96 DPI explicitly so Direct2D coordinates map 1:1 to physical display pixels
    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        96.0f,
        96.0f
    );

    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(
        m_hWnd,
        D2D1::SizeU(m_width, m_height),
        D2D1_PRESENT_OPTIONS_IMMEDIATELY
    );

    m_d2dFactory->CreateHwndRenderTarget(rtProps, hwndProps, m_renderTarget.GetAddressOf());
}

void AODWindow::DiscardDeviceResources()
{
    m_wallpaperBitmap.Reset();
    m_renderTarget.Reset();
}

void AODWindow::LoadWallpaperBitmap()
{
    m_wallpaperBitmap.Reset();

    m_activeWallpaperInfo = QueryActiveWallpaper();
    m_currentWallpaperPath = m_activeWallpaperInfo.renderImagePath;

    if (m_currentWallpaperPath.empty())
    {
        return;
    }

    // Record last write time of wallpaper file to monitor changes
    HANDLE hFile = CreateFileW(
        m_currentWallpaperPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (hFile != INVALID_HANDLE_VALUE)
    {
        GetFileTime(hFile, nullptr, nullptr, &m_renderImageFileTime);
        CloseHandle(hFile);
    }

    // Record Wallpaper Engine config last write time if available
    if (FindWallpaperEngineConfig(m_weConfigPath))
    {
        HANDLE hCfg = CreateFileW(
            m_weConfigPath.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        if (hCfg != INVALID_HANDLE_VALUE)
        {
            GetFileTime(hCfg, nullptr, nullptr, &m_weConfigFileTime);
            CloseHandle(hCfg);
        }
    }

    if (!m_wicFactory)
    {
        CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(m_wicFactory.GetAddressOf())
        );
        if (!m_wicFactory) return;
    }

    if (!m_renderTarget)
    {
        CreateDeviceResources();
        if (!m_renderTarget) return;
    }

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = m_wicFactory->CreateDecoderFromFilename(
        m_currentWallpaperPath.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,
        decoder.GetAddressOf()
    );
    if (FAILED(hr)) return;

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.GetAddressOf());
    if (FAILED(hr)) return;

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    hr = m_wicFactory->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(hr)) return;

    hr = converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0f,
        WICBitmapPaletteTypeCustom
    );
    if (FAILED(hr)) return;

    m_renderTarget->CreateBitmapFromWicBitmap(
        converter.Get(),
        nullptr,
        m_wallpaperBitmap.GetAddressOf()
    );
}

void AODWindow::MaintainZOrder()
{
    if (!m_visible || !m_hWnd)
    {
        return;
    }

    // 1. Position AOD Window at topmost band (conceals desktop icons, taskbar, normal applications)
    SetWindowPos(
        m_hWnd,
        HWND_TOPMOST,
        m_posX,
        m_posY,
        static_cast<int>(m_width),
        static_cast<int>(m_height),
        SWP_SHOWWINDOW | SWP_NOACTIVATE
    );

    // 2. Position Media Card above AOD background
    if (m_hMediaCardWnd && IsWindow(m_hMediaCardWnd) && IsWindowVisible(m_hMediaCardWnd))
    {
        SetWindowPos(m_hMediaCardWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    // 3. Position Edge Plasma Overlay on the very top
    if (m_hPlasmaWnd && IsWindow(m_hPlasmaWnd) && IsWindowVisible(m_hPlasmaWnd))
    {
        SetWindowPos(m_hPlasmaWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void AODWindow::UpdateExecutionState(bool active)
{
    if (active)
    {
        SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED);
    }
    else
    {
        SetThreadExecutionState(ES_CONTINUOUS);
    }
}

void AODWindow::SetMode(settings::AODMode mode)
{
    if (m_mode == mode && m_visible == (mode != settings::AODMode::None))
    {
        return;
    }

    m_mode = mode;

    if (m_mode == settings::AODMode::None)
    {
        if (m_visible)
        {
            m_visible = false;
            ShowWindow(m_hWnd, SW_HIDE);
            UnregisterHotKey(m_hWnd, HOTKEY_ID_AOD_ESC);
            UpdateExecutionState(false);
            DiscardDeviceResources();
        }
    }
    else
    {
        UpdateDimensions();
        CreateDeviceResources();

        if (m_mode == settings::AODMode::Wallpaper)
        {
            LoadWallpaperBitmap();
        }
        else
        {
            m_wallpaperBitmap.Reset();
        }

        m_visible = true;
        UpdateExecutionState(true);

        // Register ESC hotkey exclusively while AOD is visible
        RegisterHotKey(m_hWnd, HOTKEY_ID_AOD_ESC, MOD_NOREPEAT, VK_ESCAPE);

        MaintainZOrder();
        Render();
    }

    if (m_onModeChanged)
    {
        m_onModeChanged(m_mode);
    }
}

void AODWindow::Render()
{
    if (!m_visible || !m_hWnd)
    {
        return;
    }

    if (!m_renderTarget)
    {
        CreateDeviceResources();
        if (!m_renderTarget) return;
    }

    m_renderTarget->BeginDraw();

    // True black background for Black mode or fallback letterbox
    m_renderTarget->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f));

    if (m_mode == settings::AODMode::Wallpaper)
    {
        if (!m_wallpaperBitmap)
        {
            LoadWallpaperBitmap();
        }

        if (m_wallpaperBitmap)
        {
            D2D1_SIZE_F rtSize = m_renderTarget->GetSize();
            D2D1_SIZE_U bmpPixelSize = m_wallpaperBitmap->GetPixelSize();

            float screenW = rtSize.width;
            float screenH = rtSize.height;
            float bmpW = static_cast<float>(bmpPixelSize.width);
            float bmpH = static_cast<float>(bmpPixelSize.height);

            if (bmpW > 0.0f && bmpH > 0.0f && screenW > 0.0f && screenH > 0.0f)
            {
                float screenAspect = screenW / screenH;
                float bmpAspect = bmpW / bmpH;

                D2D1_RECT_F destRect;

                if (m_activeWallpaperInfo.scalingMode == WallpaperScalingMode::Stretch)
                {
                    destRect = D2D1::RectF(0.0f, 0.0f, screenW, screenH);
                }
                else if (m_activeWallpaperInfo.scalingMode == WallpaperScalingMode::Center)
                {
                    float offsetX = (screenW - bmpW) * 0.5f;
                    float offsetY = (screenH - bmpH) * 0.5f;
                    destRect = D2D1::RectF(offsetX, offsetY, offsetX + bmpW, offsetY + bmpH);
                }
                else if (m_activeWallpaperInfo.scalingMode == WallpaperScalingMode::Fit)
                {
                    // If the aspect ratio difference is very small (< 1.5%), fill completely without cropping or bars
                    if (std::abs(screenAspect - bmpAspect) < 0.015f)
                    {
                        destRect = D2D1::RectF(0.0f, 0.0f, screenW, screenH);
                    }
                    else
                    {
                        // Aspect Fit: preserves entire composition, zero cropping, zero distortion
                        float scale = (std::min)(screenW / bmpW, screenH / bmpH);
                        float drawW = bmpW * scale;
                        float drawH = bmpH * scale;
                        float offsetX = (screenW - drawW) * 0.5f;
                        float offsetY = (screenH - drawH) * 0.5f;
                        destRect = D2D1::RectF(offsetX, offsetY, offsetX + drawW, offsetY + drawH);
                    }
                }
                else // Default: WallpaperScalingMode::Cover (matches Wallpaper Engine desktop presentation)
                {
                    float scale = (std::max)(screenW / bmpW, screenH / bmpH);
                    float drawW = bmpW * scale;
                    float drawH = bmpH * scale;
                    float offsetX = (screenW - drawW) * 0.5f;
                    float offsetY = (screenH - drawH) * 0.5f;
                    destRect = D2D1::RectF(offsetX, offsetY, offsetX + drawW, offsetY + drawH);
                }

                m_renderTarget->DrawBitmap(
                    m_wallpaperBitmap.Get(),
                    destRect,
                    1.0f,
                    D2D1_BITMAP_INTERPOLATION_MODE_LINEAR
                );
            }
        }
    }

    HRESULT hr = m_renderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
    {
        DiscardDeviceResources();
    }
}

bool AODWindow::ProcessMessages()
{
    if (!m_hWnd)
    {
        return false;
    }

    // Periodically check if Wallpaper Engine active wallpaper changed
    if (m_visible && m_mode == settings::AODMode::Wallpaper && !m_weConfigPath.empty())
    {
        static auto lastCheck = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCheck).count() > 1500)
        {
            lastCheck = now;
            HANDLE hCfg = CreateFileW(
                m_weConfigPath.c_str(),
                GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr
            );
            if (hCfg != INVALID_HANDLE_VALUE)
            {
                FILETIME ft = {};
                GetFileTime(hCfg, nullptr, nullptr, &ft);
                CloseHandle(hCfg);

                if (ft.dwLowDateTime != m_weConfigFileTime.dwLowDateTime ||
                    ft.dwHighDateTime != m_weConfigFileTime.dwHighDateTime)
                {
                    LoadWallpaperBitmap();
                    Render();
                }
            }
        }
    }

    MSG msg = {};
    while (PeekMessageW(&msg, m_hWnd, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT)
        {
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return true;
}

void AODWindow::Shutdown()
{
    UpdateExecutionState(false);

    if (m_hWnd)
    {
        UnregisterHotKey(m_hWnd, HOTKEY_ID_AOD_ESC);
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }

    DiscardDeviceResources();

    if (m_hInstance)
    {
        UnregisterClassW(AOD_WINDOW_CLASS, m_hInstance);
    }

    m_mode = settings::AODMode::None;
    m_visible = false;
}

LRESULT CALLBACK AODWindow::WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    AODWindow* self = nullptr;

    if (uMsg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<AODWindow*>(cs->lpCreateParams);
        if (self)
        {
            self->m_hWnd = hWnd;
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
    }
    else
    {
        self = reinterpret_cast<AODWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }

    if (self)
    {
        return self->HandleMessage(hWnd, uMsg, wParam, lParam);
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

LRESULT AODWindow::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);
        Render();
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1; // Prevent GDI flicker

    // Fix Issue 3: Prevent mouse clicks from activating the AOD background window
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    {
        // Re-assert proper Z-order on any mouse click
        MaintainZOrder();
        break;
    }

    case WM_ACTIVATE:
    {
        if (wParam != WA_INACTIVE)
        {
            MaintainZOrder();
        }
        break;
    }

    case WM_WINDOWPOSCHANGING:
    {
        auto* pwp = reinterpret_cast<WINDOWPOS*>(lParam);
        if (pwp)
        {
            pwp->flags |= SWP_NOACTIVATE;
        }
        break;
    }

    case WM_HOTKEY:
    {
        if (wParam == HOTKEY_ID_AOD_ESC)
        {
            SetMode(settings::AODMode::None);
            return 0;
        }
        break;
    }

    case WM_KEYDOWN:
    {
        if (wParam == VK_ESCAPE)
        {
            SetMode(settings::AODMode::None);
            return 0;
        }
        break;
    }

    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
    {
        if (m_onContextMenu)
        {
            m_onContextMenu();
        }
        return 0;
    }

    case WM_SETTINGCHANGE:
    {
        if (wParam == SPI_SETDESKWALLPAPER || (lParam && wcscmp(reinterpret_cast<LPCWSTR>(lParam), L"Desktop") == 0))
        {
            if (m_mode == settings::AODMode::Wallpaper)
            {
                m_wallpaperBitmap.Reset();
                LoadWallpaperBitmap();
                InvalidateRect(m_hWnd, nullptr, FALSE);
            }
        }
        break;
    }

    case WM_DISPLAYCHANGE:
    {
        UpdateDimensions();
        if (m_renderTarget)
        {
            m_renderTarget->Resize(D2D1::SizeU(m_width, m_height));
        }
        MaintainZOrder();
        InvalidateRect(m_hWnd, nullptr, FALSE);
        break;
    }

    case WM_DESTROY:
    {
        DiscardDeviceResources();
        m_hWnd = nullptr;
        return 0;
    }
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

} // namespace lumn::ui
