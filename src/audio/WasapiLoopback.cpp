#include "WasapiLoopback.hpp"

#include <avrt.h>
#include <iostream>
#include <fstream>
#include <chrono>

#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "ole32.lib")

namespace lumn::audio
{

static void LogAudio(const std::string& msg)
{
    std::ofstream f("lumn_debug.log", std::ios::app);
    f << msg << std::endl;
    OutputDebugStringA(msg.c_str());
}

WasapiLoopback::WasapiLoopback()
{
    m_shutdownEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

WasapiLoopback::~WasapiLoopback()
{
    Stop();
    if (m_shutdownEvent)
    {
        CloseHandle(m_shutdownEvent);
        m_shutdownEvent = nullptr;
    }
}

bool WasapiLoopback::Start(std::shared_ptr<MusicSyncClock> clock)
{
    if (m_running.load())
    {
        return true;
    }

    m_clock = std::move(clock);
    if (!m_clock)
    {
        LogAudio("[WASAPI] Cannot start: clock reference is null.");
        return false;
    }

    // Connect beat detector callbacks to MusicSyncClock
    m_beatDetector.SetCallbacks(
        [this](double beatTime) {
            if (m_clock)
            {
                m_clock->RecordBeat(beatTime);
            }
        },
        [this](double level, double levelTime) {
            if (m_clock)
            {
                m_clock->UpdateLevel(level, levelTime);
            }
        }
    );

    ResetEvent(m_shutdownEvent);
    m_running.store(true);
    m_thread = std::thread(&WasapiLoopback::CaptureThreadFunc, this);

    LogAudio("[WASAPI] Capture thread launched.");
    return true;
}

void WasapiLoopback::Stop()
{
    if (!m_running.load())
    {
        return;
    }

    m_running.store(false);
    if (m_shutdownEvent)
    {
        SetEvent(m_shutdownEvent);
    }

    if (m_thread.joinable())
    {
        m_thread.join();
    }

    m_capturing.store(false);
    LogAudio("[WASAPI] Capture stopped cleanly.");
}

bool WasapiLoopback::InitializeAudioClient(
    Microsoft::WRL::ComPtr<IAudioClient>& audioClient,
    Microsoft::WRL::ComPtr<IAudioCaptureClient>& captureClient,
    WAVEFORMATEX*& pwfx,
    HANDLE& hAudioEvent,
    bool& useEventSync
)
{
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(enumerator.GetAddressOf())
    );

    if (FAILED(hr))
    {
        LogAudio("[WASAPI] CoCreateInstance(MMDeviceEnumerator) failed hr=0x" + std::to_string(hr));
        return false;
    }

    Microsoft::WRL::ComPtr<IMMDevice> defaultDevice;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, defaultDevice.GetAddressOf());
    if (FAILED(hr))
    {
        LogAudio("[WASAPI] GetDefaultAudioEndpoint failed hr=0x" + std::to_string(hr));
        return false;
    }

    hr = defaultDevice->Activate(
        __uuidof(IAudioClient),
        CLSCTX_ALL,
        nullptr,
        reinterpret_cast<void**>(audioClient.GetAddressOf())
    );
    if (FAILED(hr))
    {
        LogAudio("[WASAPI] Device Activate(IAudioClient) failed hr=0x" + std::to_string(hr));
        return false;
    }

    hr = audioClient->GetMixFormat(&pwfx);
    if (FAILED(hr) || !pwfx)
    {
        LogAudio("[WASAPI] GetMixFormat failed hr=0x" + std::to_string(hr));
        return false;
    }

    m_sampleRate = pwfx->nSamplesPerSec;
    m_channels = pwfx->nChannels;
    LogAudio("[WASAPI] Mix format: " + std::to_string(m_sampleRate) + " Hz, " +
             std::to_string(m_channels) + " ch, " + std::to_string(pwfx->wBitsPerSample) + " bits");

    // Try event-driven loopback first
    hAudioEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    REFERENCE_TIME hnsBufferDuration = 10000000; // 1.0 second buffer

    DWORD streamFlags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    hr = audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        streamFlags,
        hnsBufferDuration,
        0,
        pwfx,
        nullptr
    );

    if (SUCCEEDED(hr))
    {
        hr = audioClient->SetEventHandle(hAudioEvent);
        if (SUCCEEDED(hr))
        {
            useEventSync = true;
            LogAudio("[WASAPI] Initialized with event-driven synchronization.");
        }
    }

    // Fallback to timer/polling-driven loopback if driver rejects event callbacks
    if (!useEventSync)
    {
        if (hAudioEvent)
        {
            CloseHandle(hAudioEvent);
            hAudioEvent = nullptr;
        }

        audioClient.Reset();
        hr = defaultDevice->Activate(
            __uuidof(IAudioClient),
            CLSCTX_ALL,
            nullptr,
            reinterpret_cast<void**>(audioClient.GetAddressOf())
        );
        if (FAILED(hr)) return false;

        streamFlags = AUDCLNT_STREAMFLAGS_LOOPBACK;
        hr = audioClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            streamFlags,
            hnsBufferDuration,
            0,
            pwfx,
            nullptr
        );

        if (FAILED(hr))
        {
            LogAudio("[WASAPI] audioClient->Initialize (polling fallback) failed hr=0x" + std::to_string(hr));
            return false;
        }
        LogAudio("[WASAPI] Initialized with polling loopback synchronization.");
    }

    hr = audioClient->GetService(
        __uuidof(IAudioCaptureClient),
        reinterpret_cast<void**>(captureClient.GetAddressOf())
    );
    if (FAILED(hr))
    {
        LogAudio("[WASAPI] GetService(IAudioCaptureClient) failed hr=0x" + std::to_string(hr));
        return false;
    }

    return true;
}

void WasapiLoopback::ConvertToMonoFloat(
    const BYTE* pData,
    UINT32 numFrames,
    const WAVEFORMATEX* pwfx,
    bool isSilent,
    std::vector<float>& monoBuffer
)
{
    monoBuffer.resize(numFrames);

    if (isSilent || !pData)
    {
        std::fill(monoBuffer.begin(), monoBuffer.end(), 0.0f);
        return;
    }

    const uint32_t channels = pwfx->nChannels;
    const float invCh = 1.0f / static_cast<float>(channels);

    // IEEE Float 32-bit
    if (pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
        (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
         reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(pwfx)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
    {
        const float* floatSamples = reinterpret_cast<const float*>(pData);
        for (UINT32 f = 0; f < numFrames; ++f)
        {
            float sum = 0.0f;
            for (uint32_t c = 0; c < channels; ++c)
            {
                sum += floatSamples[f * channels + c];
            }
            monoBuffer[f] = sum * invCh;
        }
        return;
    }

    // PCM 16-bit
    if (pwfx->wBitsPerSample == 16)
    {
        const int16_t* pcm16 = reinterpret_cast<const int16_t*>(pData);
        constexpr float inv32768 = 1.0f / 32768.0f;
        for (UINT32 f = 0; f < numFrames; ++f)
        {
            float sum = 0.0f;
            for (uint32_t c = 0; c < channels; ++c)
            {
                sum += static_cast<float>(pcm16[f * channels + c]) * inv32768;
            }
            monoBuffer[f] = sum * invCh;
        }
        return;
    }

    // PCM 24-bit in 32-bit container or 32-bit int
    if (pwfx->wBitsPerSample == 32)
    {
        const int32_t* pcm32 = reinterpret_cast<const int32_t*>(pData);
        constexpr float invMax32 = 1.0f / 2147483648.0f;
        for (UINT32 f = 0; f < numFrames; ++f)
        {
            float sum = 0.0f;
            for (uint32_t c = 0; c < channels; ++c)
            {
                sum += static_cast<float>(pcm32[f * channels + c]) * invMax32;
            }
            monoBuffer[f] = sum * invCh;
        }
        return;
    }

    // Fallback zero
    std::fill(monoBuffer.begin(), monoBuffer.end(), 0.0f);
}

void WasapiLoopback::CaptureThreadFunc()
{
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hrCo))
    {
        LogAudio("[WASAPI] CoInitializeEx failed hr=0x" + std::to_string(hrCo));
        return;
    }

    DWORD taskIndex = 0;
    HANDLE hMmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    if (hMmcss)
    {
        LogAudio("[WASAPI] MMCSS Pro Audio task registered.");
    }

    LARGE_INTEGER qpcFreq, qpcStart, qpcNow;
    QueryPerformanceFrequency(&qpcFreq);
    QueryPerformanceCounter(&qpcStart);

    auto getTimeNow = [&]() -> double {
        QueryPerformanceCounter(&qpcNow);
        return static_cast<double>(qpcNow.QuadPart - qpcStart.QuadPart) / qpcFreq.QuadPart;
    };

    while (m_running.load())
    {
        Microsoft::WRL::ComPtr<IAudioClient> audioClient;
        Microsoft::WRL::ComPtr<IAudioCaptureClient> captureClient;
        WAVEFORMATEX* pwfx = nullptr;
        HANDLE hAudioEvent = nullptr;
        bool useEventSync = false;

        if (!InitializeAudioClient(audioClient, captureClient, pwfx, hAudioEvent, useEventSync))
        {
            LogAudio("[WASAPI] Initialization failed, retrying in 1s...");
            if (pwfx) CoTaskMemFree(pwfx);
            if (hAudioEvent) CloseHandle(hAudioEvent);
            WaitForSingleObject(m_shutdownEvent, 1000);
            continue;
        }

        HRESULT hr = audioClient->Start();
        if (FAILED(hr))
        {
            LogAudio("[WASAPI] audioClient->Start() failed hr=0x" + std::to_string(hr));
            if (pwfx) CoTaskMemFree(pwfx);
            if (hAudioEvent) CloseHandle(hAudioEvent);
            WaitForSingleObject(m_shutdownEvent, 1000);
            continue;
        }

        m_capturing.store(true);
        LogAudio("[WASAPI] Audio capture started successfully.");

        std::vector<float> monoBuffer;
        monoBuffer.reserve(4096);

        HANDLE waitHandles[2] = { m_shutdownEvent, hAudioEvent };
        DWORD waitCount = useEventSync ? 2 : 1;

        while (m_running.load())
        {
            DWORD waitRes = WaitForMultipleObjects(waitCount, waitHandles, FALSE, useEventSync ? 25 : 10);
            if (waitRes == WAIT_OBJECT_0)
            {
                // Shutdown signaled
                break;
            }

            UINT32 packetLength = 0;
            hr = captureClient->GetNextPacketSize(&packetLength);
            if (FAILED(hr))
            {
                if (hr == AUDCLNT_E_DEVICE_INVALIDATED || hr == AUDCLNT_E_RESOURCES_INVALIDATED)
                {
                    LogAudio("[WASAPI] Audio device invalidated or removed. Re-initializing...");
                    break;
                }
                continue;
            }

            if (packetLength == 0)
            {
                // No active audio packet available (system output quiet)
                // Periodically update detector with silence so level decays cleanly
                double now = getTimeNow();
                static double s_lastSilenceTime = 0.0;
                if (now - s_lastSilenceTime >= 0.05)
                {
                    s_lastSilenceTime = now;
                    float silentSample = 0.0f;
                    m_beatDetector.ProcessSamples(&silentSample, 1, m_sampleRate, now);
                }
                continue;
            }

            while (packetLength > 0 && m_running.load())
            {
                BYTE* pData = nullptr;
                UINT32 numFramesRead = 0;
                DWORD flags = 0;
                UINT64 devicePosition = 0;
                UINT64 qpcPosition = 0;

                hr = captureClient->GetBuffer(&pData, &numFramesRead, &flags, &devicePosition, &qpcPosition);
                if (FAILED(hr))
                {
                    break;
                }

                const bool isSilent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                ConvertToMonoFloat(pData, numFramesRead, pwfx, isSilent, monoBuffer);

                double curTime = getTimeNow();
                m_beatDetector.ProcessSamples(monoBuffer.data(), numFramesRead, m_sampleRate, curTime);

                captureClient->ReleaseBuffer(numFramesRead);

                hr = captureClient->GetNextPacketSize(&packetLength);
                if (FAILED(hr))
                {
                    break;
                }
            }
        }

        m_capturing.store(false);
        if (audioClient)
        {
            audioClient->Stop();
        }

        if (pwfx)
        {
            CoTaskMemFree(pwfx);
            pwfx = nullptr;
        }

        if (hAudioEvent)
        {
            CloseHandle(hAudioEvent);
            hAudioEvent = nullptr;
        }

        if (!m_running.load())
        {
            break;
        }
    }

    if (hMmcss)
    {
        AvRevertMmThreadCharacteristics(hMmcss);
    }

    CoUninitialize();
    LogAudio("[WASAPI] Capture thread exited.");
}

} // namespace lumn::audio
