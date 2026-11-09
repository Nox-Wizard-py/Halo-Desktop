#include "MediaSessionWatcher.hpp"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <fstream>
#include <iostream>
#include <vector>

#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")

using namespace winrt;
using namespace Windows::Media::Control;
using namespace Windows::Storage::Streams;

namespace lumn::media
{

static void LogMedia(const std::string& msg)
{
    std::ofstream f("lumn_debug.log", std::ios::app);
    f << msg << std::endl;
    OutputDebugStringA(msg.c_str());
}

static std::string ToUtf8(const std::wstring& wstr)
{
    if (wstr.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    std::string str(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), str.data(), size, nullptr, nullptr);
    return str;
}

static uint64_t ComputeFnv1a(const uint8_t* data, size_t length)
{
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < length; ++i)
    {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

MediaSessionWatcher::MediaSessionWatcher()
{
    m_shutdownEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_updateEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

MediaSessionWatcher::~MediaSessionWatcher()
{
    Stop();
    if (m_shutdownEvent) CloseHandle(m_shutdownEvent);
    if (m_updateEvent) CloseHandle(m_updateEvent);
}

bool MediaSessionWatcher::Start()
{
    if (m_running.load()) return true;

    ResetEvent(m_shutdownEvent);
    ResetEvent(m_updateEvent);
    m_running.store(true);

    m_workerThread = std::thread(&MediaSessionWatcher::WorkerThreadFunc, this);
    LogMedia("[GSMTC] Media session watcher worker thread launched.");
    return true;
}

void MediaSessionWatcher::Stop()
{
    if (!m_running.load()) return;

    m_running.store(false);
    if (m_shutdownEvent) SetEvent(m_shutdownEvent);

    if (m_workerThread.joinable())
    {
        m_workerThread.join();
    }
    LogMedia("[GSMTC] Media session watcher stopped cleanly.");
}

MediaTrackInfo MediaSessionWatcher::GetCurrentTrackInfo() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentTrack;
}

void MediaSessionWatcher::EnqueueCommand(CommandType type, double seekFraction)
{
    {
        std::lock_guard<std::mutex> lock(m_cmdMutex);
        m_pendingCommands.push_back({ type, seekFraction });
    }
    if (m_updateEvent)
    {
        SetEvent(m_updateEvent);
    }
}

void MediaSessionWatcher::TogglePlayPause()
{
    EnqueueCommand(CommandType::TogglePlayPause);
}

void MediaSessionWatcher::PlayTrack()
{
    EnqueueCommand(CommandType::Play);
}

void MediaSessionWatcher::PauseTrack()
{
    EnqueueCommand(CommandType::Pause);
}

void MediaSessionWatcher::PreviousTrack()
{
    EnqueueCommand(CommandType::Previous);
}

void MediaSessionWatcher::NextTrack()
{
    EnqueueCommand(CommandType::Next);
}

void MediaSessionWatcher::SeekTo(double normalizedFraction)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_currentTrack.duration100ns > 0)
        {
            m_currentTrack.position100ns = std::clamp<int64_t>(
                static_cast<int64_t>(normalizedFraction * m_currentTrack.duration100ns),
                0,
                m_currentTrack.duration100ns
            );
            m_currentTrack.lastUpdatedTime = std::chrono::steady_clock::now();
        }
    }
    EnqueueCommand(CommandType::Seek, normalizedFraction);
}

void MediaSessionWatcher::WorkerThreadFunc()
{
    init_apartment();
    HRESULT hrWic = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* pWicFactory = nullptr;
    if (SUCCEEDED(hrWic))
    {
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pWicFactory));
    }

    GlobalSystemMediaTransportControlsSessionManager sessionManager = nullptr;
    try
    {
        sessionManager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        LogMedia("[GSMTC] Successfully acquired GSMTC SessionManager.");
    }
    catch (const hresult_error& ex)
    {
        LogMedia("[GSMTC] Failed to acquire SessionManager: " + ToUtf8(ex.message().c_str()));
    }

    event_token currentSessionToken{};
    event_token sessionsChangedToken{};

    if (sessionManager)
    {
        try
        {
            currentSessionToken = sessionManager.CurrentSessionChanged([this](auto&&, auto&&) {
                SetEvent(m_updateEvent);
            });
            sessionsChangedToken = sessionManager.SessionsChanged([this](auto&&, auto&&) {
                SetEvent(m_updateEvent);
            });
        }
        catch (...)
        {
            LogMedia("[GSMTC] Warning: could not attach session manager event listeners.");
        }
    }

    // Active session event monitoring tokens
    GlobalSystemMediaTransportControlsSession monitoredSession = nullptr;
    event_token sessionMediaPropsToken{};
    event_token sessionPlaybackInfoToken{};
    event_token sessionTimelineToken{};

    auto detachSessionListeners = [&]() {
        if (monitoredSession)
        {
            try
            {
                if (sessionMediaPropsToken.value != 0) monitoredSession.MediaPropertiesChanged(sessionMediaPropsToken);
                if (sessionPlaybackInfoToken.value != 0) monitoredSession.PlaybackInfoChanged(sessionPlaybackInfoToken);
                if (sessionTimelineToken.value != 0) monitoredSession.TimelinePropertiesChanged(sessionTimelineToken);
            }
            catch (...) {}
            sessionMediaPropsToken = {};
            sessionPlaybackInfoToken = {};
            sessionTimelineToken = {};
            monitoredSession = nullptr;
        }
    };

    auto attachSessionListeners = [&](GlobalSystemMediaTransportControlsSession session) {
        detachSessionListeners();
        monitoredSession = session;
        if (monitoredSession)
        {
            try
            {
                sessionMediaPropsToken = monitoredSession.MediaPropertiesChanged([this](auto&&, auto&&) {
                    SetEvent(m_updateEvent);
                });
                sessionPlaybackInfoToken = monitoredSession.PlaybackInfoChanged([this](auto&&, auto&&) {
                    SetEvent(m_updateEvent);
                });
                sessionTimelineToken = monitoredSession.TimelinePropertiesChanged([this](auto&&, auto&&) {
                    SetEvent(m_updateEvent);
                });
            }
            catch (...)
            {
                LogMedia("[GSMTC] Warning: could not attach active session property listeners.");
            }
        }
    };

    // Helper lambda to decode thumbnail buffer into high-res (128x128) and palette buffer (48x48)
    auto decodeThumbnailDual = [&](const uint8_t* buffer, uint32_t bufferSize,
                                   RimPalette& outPalette,
                                   std::vector<uint8_t>& outCardArtwork,
                                   uint32_t& outCardW, uint32_t& outCardH) -> bool
    {
        if (!buffer || bufferSize == 0 || !pWicFactory) return false;

        IStream* pMemStream = SHCreateMemStream(buffer, bufferSize);
        if (!pMemStream) return false;

        bool success = false;
        IWICBitmapDecoder* pDecoder = nullptr;
        HRESULT hr = pWicFactory->CreateDecoderFromStream(pMemStream, nullptr, WICDecodeMetadataCacheOnDemand, &pDecoder);
        if (SUCCEEDED(hr))
        {
            IWICBitmapFrameDecode* pFrame = nullptr;
            hr = pDecoder->GetFrame(0, &pFrame);
            if (SUCCEEDED(hr))
            {
                // 1. Scale to 256x256 for Media Card high-res artwork display
                IWICBitmapScaler* pCardScaler = nullptr;
                hr = pWicFactory->CreateBitmapScaler(&pCardScaler);
                if (SUCCEEDED(hr))
                {
                    hr = pCardScaler->Initialize(pFrame, 256, 256, WICBitmapInterpolationModeFant);
                    if (SUCCEEDED(hr))
                    {
                        IWICFormatConverter* pCardConverter = nullptr;
                        hr = pWicFactory->CreateFormatConverter(&pCardConverter);
                        if (SUCCEEDED(hr))
                        {
                            hr = pCardConverter->Initialize(
                                pCardScaler,
                                GUID_WICPixelFormat32bppRGBA,
                                WICBitmapDitherTypeNone,
                                nullptr, 0.0, WICBitmapPaletteTypeCustom
                            );
                            if (SUCCEEDED(hr))
                            {
                                outCardArtwork.resize(256 * 256 * 4);
                                hr = pCardConverter->CopyPixels(nullptr, 256 * 4, static_cast<UINT>(outCardArtwork.size()), outCardArtwork.data());
                                if (SUCCEEDED(hr))
                                {
                                    outCardW = 256;
                                    outCardH = 256;
                                }
                            }
                            pCardConverter->Release();
                        }
                    }
                    pCardScaler->Release();
                }

                // 2. Scale to 48x48 for OKLab palette extraction
                IWICBitmapScaler* pPaletteScaler = nullptr;
                hr = pWicFactory->CreateBitmapScaler(&pPaletteScaler);
                if (SUCCEEDED(hr))
                {
                    hr = pPaletteScaler->Initialize(pFrame, ColorExtractor::DOWNSAMPLE_WIDTH, ColorExtractor::DOWNSAMPLE_HEIGHT, WICBitmapInterpolationModeFant);
                    if (SUCCEEDED(hr))
                    {
                        IWICFormatConverter* pPaletteConverter = nullptr;
                        hr = pWicFactory->CreateFormatConverter(&pPaletteConverter);
                        if (SUCCEEDED(hr))
                        {
                            hr = pPaletteConverter->Initialize(
                                pPaletteScaler,
                                GUID_WICPixelFormat32bppRGBA,
                                WICBitmapDitherTypeNone,
                                nullptr, 0.0, WICBitmapPaletteTypeCustom
                            );
                            if (SUCCEEDED(hr))
                            {
                                std::vector<uint8_t> pixelBuffer(ColorExtractor::BUFFER_SIZE);
                                hr = pPaletteConverter->CopyPixels(nullptr, ColorExtractor::DOWNSAMPLE_WIDTH * 4, static_cast<UINT>(pixelBuffer.size()), pixelBuffer.data());
                                if (SUCCEEDED(hr))
                                {
                                    outPalette = ColorExtractor::ExtractFromRGBA48x48(pixelBuffer.data());
                                    success = true;
                                }
                            }
                            pPaletteConverter->Release();
                        }
                    }
                    pPaletteScaler->Release();
                }

                pFrame->Release();
            }
            pDecoder->Release();
        }
        pMemStream->Release();
        return success;
    };

    HANDLE waitHandles[2] = { m_shutdownEvent, m_updateEvent };
    std::wstring lastActiveApp;
    std::wstring currentTrackIdentity;
    int64_t lastAuthoritativePos100ns = -1;
    int64_t lastAuthoritativeDur100ns = -1;
    int64_t lastTimelineSystemTimeRaw = -1;
    bool lastAuthoritativePlaying = false;
    uint64_t lastArtworkHash = 0;
    std::chrono::steady_clock::time_point lastDiagLogTime = std::chrono::steady_clock::now();

    while (m_running.load())
    {
        // 250ms periodic fallback polling interval
        DWORD waitRes = WaitForMultipleObjects(2, waitHandles, FALSE, 250);
        if (waitRes == WAIT_OBJECT_0)
        {
            break; // Shutdown signaled
        }

        if (!sessionManager)
        {
            try
            {
                sessionManager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
            }
            catch (...)
            {
                continue;
            }
        }

        // Multi-tier session resolution (handles Playing, Paused, and background browser sessions)
        GlobalSystemMediaTransportControlsSession activeSession = nullptr;
        try
        {
            // Tier 1: Check GetCurrentSession()
            auto current = sessionManager.GetCurrentSession();
            if (current)
            {
                auto info = current.GetPlaybackInfo();
                if (info)
                {
                    auto status = info.PlaybackStatus();
                    if (status == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing ||
                        status == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused)
                    {
                        activeSession = current;
                    }
                }
            }

            if (!activeSession)
            {
                auto allSessions = sessionManager.GetSessions();

                // Tier 2: Look for any session with PlaybackStatus::Playing
                for (const auto& s : allSessions)
                {
                    auto info = s.GetPlaybackInfo();
                    if (info && info.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing)
                    {
                        activeSession = s;
                        break;
                    }
                }

                // Tier 3: If no session is playing, retain the previously monitored session if it is still in allSessions
                if (!activeSession && monitoredSession)
                {
                    std::wstring monitoredApp = monitoredSession.SourceAppUserModelId().c_str();
                    for (const auto& s : allSessions)
                    {
                        if (s.SourceAppUserModelId().c_str() == monitoredApp)
                        {
                            activeSession = s;
                            break;
                        }
                    }
                }

                // Tier 4: Look for any session with Paused or Stopped state having metadata
                if (!activeSession)
                {
                    for (const auto& s : allSessions)
                    {
                        auto info = s.GetPlaybackInfo();
                        if (info && (info.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused ||
                                     info.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Stopped))
                        {
                            activeSession = s;
                            break;
                        }
                    }
                }

                // Tier 5: Fall back to first session in list if present
                if (!activeSession && allSessions.Size() > 0)
                {
                    activeSession = allSessions.GetAt(0);
                }
            }
        }
        catch (...)
        {
            activeSession = nullptr;
        }

        // Process pending UI commands on the active session immediately
        {
            std::vector<PendingCommand> cmds;
            {
                std::lock_guard<std::mutex> lock(m_cmdMutex);
                cmds = std::move(m_pendingCommands);
            }
            for (const auto& cmd : cmds)
            {
                if (activeSession)
                {
                    try
                    {
                        if (cmd.type == CommandType::Play)
                        {
                            try
                            {
                                activeSession.TryPlayAsync();
                                LogMedia("[GSMTC] Dispatched Play command.");
                            }
                            catch (...)
                            {
                                activeSession.TryTogglePlayPauseAsync();
                                LogMedia("[GSMTC] Dispatched TogglePlayPause fallback for Play.");
                            }
                        }
                        else if (cmd.type == CommandType::Pause)
                        {
                            try
                            {
                                activeSession.TryPauseAsync();
                                LogMedia("[GSMTC] Dispatched Pause command.");
                            }
                            catch (...)
                            {
                                activeSession.TryTogglePlayPauseAsync();
                                LogMedia("[GSMTC] Dispatched TogglePlayPause fallback for Pause.");
                            }
                        }
                        else if (cmd.type == CommandType::TogglePlayPause)
                        {
                            activeSession.TryTogglePlayPauseAsync();
                            LogMedia("[GSMTC] Dispatched TogglePlayPause command.");
                        }
                        else if (cmd.type == CommandType::Previous)
                        {
                            activeSession.TrySkipPreviousAsync();
                            LogMedia("[GSMTC] Dispatched PreviousTrack command.");
                        }
                        else if (cmd.type == CommandType::Next)
                        {
                            activeSession.TrySkipNextAsync();
                            LogMedia("[GSMTC] Dispatched NextTrack command.");
                        }
                        else if (cmd.type == CommandType::Seek)
                        {
                            int64_t dur = 0;
                            auto timeline = activeSession.GetTimelineProperties();
                            if (timeline)
                            {
                                dur = timeline.EndTime().count();
                            }
                            if (dur <= 0)
                            {
                                std::lock_guard<std::mutex> lock(m_mutex);
                                dur = m_currentTrack.duration100ns;
                            }
                            if (dur > 0)
                            {
                                int64_t target = std::clamp<int64_t>(static_cast<int64_t>(cmd.seekFraction * dur), 0, dur);
                                activeSession.TryChangePlaybackPositionAsync(target);
                                lastAuthoritativePos100ns = target;
                                lastTimelineSystemTimeRaw = 0; // Force acceptance of the next authoritative sample
                                {
                                    std::lock_guard<std::mutex> lock(m_mutex);
                                    m_currentTrack.position100ns = target;
                                    m_currentTrack.lastUpdatedTime = std::chrono::steady_clock::now();
                                }
                                LogMedia("[GSMTC] Dispatched Seek command to: " + std::to_string(target / 10000000.0) + "s");
                            }
                        }
                    }
                    catch (const hresult_error& ex)
                    {
                        LogMedia("[GSMTC] Error executing command: " + ToUtf8(ex.message().c_str()));
                    }
                }
            }
        }

        // Manage active session listener lifecycle
        if (activeSession != monitoredSession)
        {
            attachSessionListeners(activeSession);
        }

        if (!activeSession)
        {
            if (!currentTrackIdentity.empty())
            {
                uint64_t reqGen = ++m_currentGeneration;
                LogMedia("[GSMTC][Gen " + std::to_string(reqGen) + "] No active media session. Resetting state.");
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_currentTrack = MediaTrackInfo{};
                    m_currentTrack.generation = reqGen;
                }
                currentTrackIdentity.clear();
                lastActiveApp.clear();
                lastAuthoritativePos100ns = -1;
                lastAuthoritativeDur100ns = -1;
                lastTimelineSystemTimeRaw = -1;
                lastAuthoritativePlaying = false;
                lastArtworkHash = 0;
            }
            continue;
        }

        try
        {
            std::wstring sourceApp = activeSession.SourceAppUserModelId().c_str();
            if (sourceApp != lastActiveApp)
            {
                LogMedia("[GSMTC] Active session changed to: " + ToUtf8(sourceApp));
                lastActiveApp = sourceApp;
            }

            bool isPlaying = false;
            bool canPlay = false, canPause = false, canPrev = false, canNext = false, canSeek = false;
            double playbackRate = 1.0;

            auto playbackInfo = activeSession.GetPlaybackInfo();
            if (playbackInfo)
            {
                isPlaying = (playbackInfo.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing);
                if (auto rateProp = playbackInfo.PlaybackRate())
                {
                    playbackRate = rateProp.Value();
                    if (playbackRate <= 0.0) playbackRate = 1.0;
                }

                auto controls = playbackInfo.Controls();
                if (controls)
                {
                    canPlay = controls.IsPlayEnabled();
                    canPause = controls.IsPauseEnabled();
                    canPrev = controls.IsPreviousEnabled();
                    canNext = controls.IsNextEnabled();
                    canSeek = controls.IsPlaybackPositionEnabled();
                }
            }

            // Timeline properties
            int64_t authPos100ns = 0;
            int64_t authDur100ns = 0;
            int64_t authTimeRaw = 0;
            auto timeline = activeSession.GetTimelineProperties();
            if (timeline)
            {
                authPos100ns = timeline.Position().count();
                authDur100ns = timeline.EndTime().count();
                authTimeRaw = timeline.LastUpdatedTime().time_since_epoch().count();
            }

            auto mediaProps = activeSession.TryGetMediaPropertiesAsync().get();
            if (!mediaProps)
            {
                continue;
            }

            std::wstring title = mediaProps.Title().c_str();
            std::wstring artist = mediaProps.Artist().c_str();
            std::wstring album = mediaProps.AlbumTitle().c_str();
            std::wstring trackIdentity = sourceApp + L"|" + title + L"|" + artist + L"|" + album;

            bool isNewTrack = (trackIdentity != currentTrackIdentity);

            if (isNewTrack)
            {
                currentTrackIdentity = trackIdentity;
                uint64_t reqGen = ++m_currentGeneration;
                lastAuthoritativePos100ns = authPos100ns;
                lastAuthoritativeDur100ns = authDur100ns;
                lastTimelineSystemTimeRaw = authTimeRaw;
                lastAuthoritativePlaying = isPlaying;
                lastArtworkHash = 0; // Reset so any incoming artwork for the new track will be evaluated

                // Immediately publish metadata with placeholder artwork
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_currentTrack.sourceApp = sourceApp;
                    m_currentTrack.title = title;
                    m_currentTrack.artist = artist;
                    m_currentTrack.album = album;
                    m_currentTrack.isPlaying = isPlaying;
                    m_currentTrack.hasArtwork = false; // Deliberate placeholder
                    m_currentTrack.palette = RimPalette{};
                    m_currentTrack.cardArtworkRGBA.clear();
                    m_currentTrack.cardArtworkWidth = 0;
                    m_currentTrack.cardArtworkHeight = 0;
                    m_currentTrack.position100ns = authPos100ns;
                    m_currentTrack.duration100ns = authDur100ns;
                    m_currentTrack.lastUpdatedTime = std::chrono::steady_clock::now();
                    m_currentTrack.playbackRate = playbackRate;
                    m_currentTrack.canPlay = canPlay;
                    m_currentTrack.canPause = canPause;
                    m_currentTrack.canPrevious = canPrev;
                    m_currentTrack.canNext = canNext;
                    m_currentTrack.canSeek = canSeek;
                    m_currentTrack.generation = reqGen;
                }
                LogMedia("[GSMTC][Gen " + std::to_string(reqGen) + "] Track changed: \"" +
                         ToUtf8(title) + "\" by " + ToUtf8(artist) + " (Placeholder active, awaiting artwork)");
            }
            else
            {
                // Same track: calculate current local interpolated position
                int64_t currentLocalPos100ns = 0;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    currentLocalPos100ns = m_currentTrack.GetCurrentPosition100ns(std::chrono::steady_clock::now(), m_currentTrack.isPlaying);
                }

                bool playStateChanged = (isPlaying != lastAuthoritativePlaying);
                lastAuthoritativePlaying = isPlaying;

                // Did the media player provide a genuinely new timeline sample?
                bool posChanged = (authPos100ns != lastAuthoritativePos100ns);
                bool timeChanged = (authTimeRaw != 0 && authTimeRaw != lastTimelineSystemTimeRaw);
                bool hasNewSample = (posChanged || timeChanged);

                if (playStateChanged)
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_currentTrack.isPlaying = isPlaying;
                    if (!isPlaying)
                    {
                        // Pausing: freeze anchor at the current local interpolated position
                        m_currentTrack.position100ns = currentLocalPos100ns;
                        m_currentTrack.lastUpdatedTime = std::chrono::steady_clock::now();
                        lastAuthoritativePos100ns = authPos100ns;
                        lastTimelineSystemTimeRaw = authTimeRaw;
                        LogMedia("[GSMTC] Playback PAUSED at " + std::to_string(currentLocalPos100ns / 10000000.0) + "s (frozen)");
                    }
                    else
                    {
                        // Resuming: check if user seeked while paused
                        if (posChanged && authPos100ns > 0)
                        {
                            m_currentTrack.position100ns = authPos100ns;
                        }
                        else
                        {
                            m_currentTrack.position100ns = currentLocalPos100ns;
                        }
                        m_currentTrack.lastUpdatedTime = std::chrono::steady_clock::now();
                        lastAuthoritativePos100ns = authPos100ns;
                        lastTimelineSystemTimeRaw = authTimeRaw;
                        LogMedia("[GSMTC] Playback RESUMED from " + std::to_string(m_currentTrack.position100ns / 10000000.0) + "s");
                    }
                }
                else if (!isPlaying && hasNewSample && posChanged)
                {
                    // Media is paused and user seeked (either via UI or external player)
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_currentTrack.position100ns = authPos100ns;
                    if (authDur100ns > 0) m_currentTrack.duration100ns = authDur100ns;
                    m_currentTrack.lastUpdatedTime = std::chrono::steady_clock::now();
                    lastAuthoritativePos100ns = authPos100ns;
                    lastTimelineSystemTimeRaw = authTimeRaw;
                    LogMedia("[GSMTC Rebase] PAUSED_SEEK | Target: " + std::to_string(authPos100ns / 10000000.0) + "s");
                }
                else if (isPlaying && hasNewSample)
                {
                    // Media is playing and a new authoritative sample arrived from GSMTC
                    int64_t delta100ns = authPos100ns - currentLocalPos100ns;

                    // Seek Detection:
                    // Genuine seek occurs when the authoritative position differs significantly (> 1.0s forward or backward)
                    // AND the position actually changed from the previous authoritative sample.
                    bool isForwardSeek = (posChanged && delta100ns > 10000000LL);   // > +1.0s
                    bool isBackwardSeek = (posChanged && delta100ns < -10000000LL); // < -1.0s

                    // Periodic streaming sync (e.g. Spotify sending 1s updates):
                    // If drift between local and remote exceeds 0.5s, gently re-anchor
                    bool isDriftRebase = (posChanged && std::abs(delta100ns) > 5000000LL && std::abs(delta100ns) <= 10000000LL);

                    if (isForwardSeek || isBackwardSeek || isDriftRebase)
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_currentTrack.position100ns = authPos100ns;
                        if (authDur100ns > 0) m_currentTrack.duration100ns = authDur100ns;
                        m_currentTrack.lastUpdatedTime = std::chrono::steady_clock::now();

                        std::string reason = isForwardSeek ? "FORWARD_SEEK" : (isBackwardSeek ? "BACKWARD_SEEK" : "STREAM_SYNC");
                        LogMedia("[GSMTC Rebase] " + reason + " | Target: " + std::to_string(authPos100ns / 10000000.0) +
                                 "s | Delta: " + std::to_string(delta100ns / 10000000.0) + "s");
                    }

                    // Always record the accepted sample so we don't re-process it
                    lastAuthoritativePos100ns = authPos100ns;
                    lastTimelineSystemTimeRaw = authTimeRaw;
                }

                // Update controls and capabilities without touching the timeline anchor
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_currentTrack.canPlay = canPlay;
                    m_currentTrack.canPause = canPause;
                    m_currentTrack.canPrevious = canPrev;
                    m_currentTrack.canNext = canNext;
                    m_currentTrack.canSeek = canSeek;
                    m_currentTrack.playbackRate = playbackRate;
                    if (authDur100ns > 0) m_currentTrack.duration100ns = authDur100ns;
                }
            }

            // Artwork Acquisition: check if artwork is needed or if new thumbnail is available
            bool needArtwork = false;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                needArtwork = (!m_currentTrack.hasArtwork || isNewTrack);
            }

            IRandomAccessStreamReference thumbRef = mediaProps.Thumbnail();
            if (thumbRef)
            {
                try
                {
                    auto stream = thumbRef.OpenReadAsync().get();
                    uint32_t streamSize = static_cast<uint32_t>(stream.Size());

                    if (streamSize > 0 && streamSize <= 32 * 1024 * 1024)
                    {
                        Buffer buf(streamSize);
                        stream.ReadAsync(buf, streamSize, InputStreamOptions::None).get();

                        uint64_t streamHash = ComputeFnv1a(buf.data(), streamSize);

                        // If artwork is needed OR if a new/different thumbnail arrived:
                        if (streamHash != lastArtworkHash || needArtwork)
                        {
                            RimPalette currentPalette;
                            std::vector<uint8_t> cardArtwork;
                            uint32_t cardW = 0, cardH = 0;
                            bool decoded = false;

                            std::wstring fullCacheKey = trackIdentity + L"|" + std::to_wstring(streamSize) + L"|" + std::to_wstring(streamHash);
                            auto it = m_artworkCache.find(fullCacheKey);
                            if (it != m_artworkCache.end())
                            {
                                currentPalette = it->second.palette;
                                cardArtwork = it->second.cardArtworkRGBA;
                                cardW = it->second.width;
                                cardH = it->second.height;
                                decoded = true;
                                LogMedia("[GSMTC] Cache hit for artwork (" + std::to_string(streamSize) + " bytes).");
                            }
                            else
                            {
                                if (decodeThumbnailDual(buf.data(), streamSize, currentPalette, cardArtwork, cardW, cardH))
                                {
                                    decoded = true;
                                    if (m_artworkCache.size() < 50)
                                    {
                                        m_artworkCache[fullCacheKey] = { currentPalette, cardArtwork, cardW, cardH };
                                    }
                                    LogMedia("[GSMTC] Decoded new artwork (" + std::to_string(streamSize) + " bytes, hash " + std::to_string(streamHash) + ").");
                                }
                            }

                            if (decoded)
                            {
                                // Verify that the track hasn't changed during decoding
                                uint64_t artGen = 0;
                                {
                                    std::lock_guard<std::mutex> lock(m_mutex);
                                    if (m_currentTrack.sourceApp == sourceApp &&
                                        m_currentTrack.title == title &&
                                        m_currentTrack.artist == artist)
                                    {
                                        m_currentTrack.hasArtwork = true;
                                        m_currentTrack.palette = currentPalette;
                                        m_currentTrack.cardArtworkRGBA = std::move(cardArtwork);
                                        m_currentTrack.cardArtworkWidth = cardW;
                                        m_currentTrack.cardArtworkHeight = cardH;
                                        artGen = ++m_currentGeneration;
                                        m_currentTrack.generation = artGen;
                                    }
                                }
                                if (artGen > 0)
                                {
                                    lastArtworkHash = streamHash;
                                    LogMedia("[GSMTC][Gen " + std::to_string(artGen) + "] Artwork attached to: \"" + ToUtf8(title) + "\"");
                                }
                                else
                                {
                                    LogMedia("[GSMTC] Discarded stale decoded artwork: track identity changed during decode.");
                                }
                            }
                        }
                    }
                }
                catch (const hresult_error& ex)
                {
                    LogMedia("[GSMTC] Failed to read thumbnail stream: " + ToUtf8(ex.message().c_str()));
                }
            }

            // Throttled periodic diagnostic logging (every 3 seconds)
            auto nowClock = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(nowClock - lastDiagLogTime).count() >= 3)
            {
                lastDiagLogTime = nowClock;
                int64_t curLocalPos = 0;
                int64_t curDur = 0;
                bool curPlaying = false;
                bool curArt = false;
                uint64_t curGen = 0;
                double curRate = 1.0;
                double sampleAgeSec = 0.0;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    curLocalPos = m_currentTrack.GetCurrentPosition100ns(nowClock, m_currentTrack.isPlaying);
                    curDur = m_currentTrack.duration100ns;
                    curPlaying = m_currentTrack.isPlaying;
                    curArt = m_currentTrack.hasArtwork;
                    curGen = m_currentTrack.generation;
                    curRate = m_currentTrack.playbackRate;
                    auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(nowClock - m_currentTrack.lastUpdatedTime).count();
                    sampleAgeSec = ageMs / 1000.0;
                }
                LogMedia("[GSMTC Diag] Gen " + std::to_string(curGen) +
                         " | Playing: " + (curPlaying ? "YES" : "NO") +
                         " | Rate: " + std::to_string(curRate) +
                         " | DisplayPos: " + std::to_string(curLocalPos / 10000000.0) + "s / " + std::to_string(curDur / 10000000.0) + "s" +
                         " | RawAuthPos: " + std::to_string(authPos100ns / 10000000.0) + "s" +
                         " | RawAuthTime: " + std::to_string(authTimeRaw) +
                         " | AnchorAge: " + std::to_string(sampleAgeSec) + "s" +
                         " | Artwork: " + (curArt ? "ATTACHED" : "PLACEHOLDER"));
            }
        }
        catch (const hresult_error& ex)
        {
            LogMedia("[GSMTC] Exception in worker loop: " + ToUtf8(ex.message().c_str()));
        }
        catch (...)
        {
            LogMedia("[GSMTC] Unknown exception in worker loop.");
        }
    }

    detachSessionListeners();

    if (sessionManager)
    {
        try
        {
            if (currentSessionToken.value != 0) sessionManager.CurrentSessionChanged(currentSessionToken);
            if (sessionsChangedToken.value != 0) sessionManager.SessionsChanged(sessionsChangedToken);
        }
        catch (...) {}
    }

    if (pWicFactory)
    {
        pWicFactory->Release();
    }

    CoUninitialize();
    uninit_apartment();
    LogMedia("[GSMTC] Worker thread exited.");
}

} // namespace lumn::media
