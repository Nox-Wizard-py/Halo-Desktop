#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <algorithm>

#include "ColorExtractor.hpp"

namespace lumn::media
{

struct MediaTrackInfo
{
    std::wstring title;
    std::wstring artist;
    std::wstring album;
    std::wstring sourceApp;
    bool isPlaying = false;
    bool hasArtwork = false;
    RimPalette palette;
    uint64_t generation = 0; // Monotonically increasing request/track generation ID

    // Phase 4A & 4A.1: Monotonic Timeline, Progress & Capabilities
    int64_t position100ns = 0;       // Playback position in 100ns units (1s = 10,000,000)
    int64_t duration100ns = 0;       // Total track duration in 100ns units
    double playbackRate = 1.0;       // Playback rate (normally 1.0)
    std::chrono::steady_clock::time_point lastUpdatedTime = std::chrono::steady_clock::now();

    bool canPlay = false;
    bool canPause = false;
    bool canPrevious = false;
    bool canNext = false;
    bool canSeek = false;

    // High-resolution decoded artwork for the Media Card (128x128 RGBA)
    std::vector<uint8_t> cardArtworkRGBA;
    uint32_t cardArtworkWidth = 0;
    uint32_t cardArtworkHeight = 0;

    // Phase 4A.3: Single authoritative timeline computation for UI & rendering
    int64_t GetCurrentPosition100ns(std::chrono::steady_clock::time_point now, bool effectiveIsPlaying) const noexcept
    {
        if (duration100ns <= 0)
        {
            return std::max<int64_t>(0, position100ns);
        }
        if (!effectiveIsPlaying)
        {
            return std::clamp<int64_t>(position100ns, 0, duration100ns);
        }

        auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(now - lastUpdatedTime).count();
        if (elapsedUs <= 0)
        {
            return std::clamp<int64_t>(position100ns, 0, duration100ns);
        }

        double rate = (playbackRate > 0.0 && playbackRate <= 4.0) ? playbackRate : 1.0;
        int64_t elapsed100ns = static_cast<int64_t>(elapsedUs * 10.0 * rate);
        int64_t computed = position100ns + elapsed100ns;
        return std::clamp<int64_t>(computed, 0, duration100ns);
    }
};

// Global System Media Transport Controls (GSMTC) Session Watcher, Artwork Extractor & Command Dispatcher
class MediaSessionWatcher
{
public:
    MediaSessionWatcher();
    ~MediaSessionWatcher();

    MediaSessionWatcher(const MediaSessionWatcher&) = delete;
    MediaSessionWatcher& operator=(const MediaSessionWatcher&) = delete;

    bool Start();
    void Stop();

    // Thread-safe snapshot read (called by render and UI threads)
    MediaTrackInfo GetCurrentTrackInfo() const;

    // Playback control commands (dispatched safely to the active GSMTC session)
    void TogglePlayPause();
    void PlayTrack();
    void PauseTrack();
    void PreviousTrack();
    void NextTrack();
    void SeekTo(double normalizedFraction); // 0.0 to 1.0

private:
    void WorkerThreadFunc();

    enum class CommandType
    {
        TogglePlayPause,
        Play,
        Pause,
        Previous,
        Next,
        Seek
    };

    struct PendingCommand
    {
        CommandType type = CommandType::TogglePlayPause;
        double seekFraction = 0.0;
    };

    void EnqueueCommand(CommandType type, double seekFraction = 0.0);

    mutable std::mutex m_mutex;
    MediaTrackInfo m_currentTrack;

    std::mutex m_cmdMutex;
    std::vector<PendingCommand> m_pendingCommands;

    std::thread m_workerThread;
    std::atomic<bool> m_running{ false };
    std::atomic<uint64_t> m_currentGeneration{ 0 };

    HANDLE m_shutdownEvent = nullptr;
    HANDLE m_updateEvent = nullptr;

    // Cache of decoded artwork palettes and high-res buffers
    struct CachedArtwork
    {
        RimPalette palette;
        std::vector<uint8_t> cardArtworkRGBA;
        uint32_t width = 0;
        uint32_t height = 0;
    };
    std::unordered_map<std::wstring, CachedArtwork> m_artworkCache;
};

} // namespace lumn::media
