#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>
#include <thread>
#include <atomic>
#include <vector>
#include <memory>
#include <functional>

#include "BeatDetector.hpp"
#include "MusicSyncClock.hpp"

namespace lumn::audio
{

// WASAPI System-Output Audio Loopback Capture
// Captures default rendering endpoint audio and drives BeatDetector and MusicSyncClock
class WasapiLoopback
{
public:
    WasapiLoopback();
    ~WasapiLoopback();

    WasapiLoopback(const WasapiLoopback&) = delete;
    WasapiLoopback& operator=(const WasapiLoopback&) = delete;

    // Start background capture thread with reference to clock
    bool Start(std::shared_ptr<MusicSyncClock> clock);
    void Stop();

    bool IsCapturing() const noexcept { return m_capturing; }
    uint32_t GetSampleRate() const noexcept { return m_sampleRate; }
    uint32_t GetChannels() const noexcept { return m_channels; }

private:
    void CaptureThreadFunc();
    bool InitializeAudioClient(
        Microsoft::WRL::ComPtr<IAudioClient>& audioClient,
        Microsoft::WRL::ComPtr<IAudioCaptureClient>& captureClient,
        WAVEFORMATEX*& pwfx,
        HANDLE& hAudioEvent,
        bool& useEventSync
    );

    void ConvertToMonoFloat(
        const BYTE* pData,
        UINT32 numFrames,
        const WAVEFORMATEX* pwfx,
        bool isSilent,
        std::vector<float>& monoBuffer
    );

    std::shared_ptr<MusicSyncClock> m_clock;
    BeatDetector m_beatDetector;

    std::thread m_thread;
    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_capturing{ false };

    HANDLE m_shutdownEvent = nullptr;

    uint32_t m_sampleRate = 44100;
    uint32_t m_channels = 2;
};

} // namespace lumn::audio

