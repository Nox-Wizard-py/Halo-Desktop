#pragma once

#include <functional>
#include <array>
#include <cstdint>
#include "BassFilter.hpp"

namespace lumn::audio
{

// Dynamic Beat Transient Detector and Level Analyzer
// Reconstructed from Lumn 1.1 AudioBeatDetector (scratch/Lumn_arm64 @ 0x100034564)
class BeatDetector
{
public:
    using BeatCallback = std::function<void(double timestamp)>;
    using LevelCallback = std::function<void(double level, double timestamp)>;

    static constexpr size_t HISTORY_CAPACITY = 43; // Verified Lumn ring buffer length (~1.0s context)

    BeatDetector();

    void SetCallbacks(BeatCallback onBeat, LevelCallback onLevel);
    void Reset();

    // Process a block of mono Float32 audio samples with exact timestamp in seconds
    void ProcessSamples(const float* samples, uint32_t numSamples, uint32_t sampleRate, double curTime);

    // Direct access to current filter and detector metrics
    float GetLastBassRMS() const noexcept { return m_lastBassRMS; }
    float GetLastFullRMS() const noexcept { return m_lastFullRMS; }
    double GetLastBeatTime() const noexcept { return m_lastBeatT; }

private:
    BassFilter m_bassFilter;

    BeatCallback m_onBeat;
    LevelCallback m_onLevel;

    std::array<float, HISTORY_CAPACITY> m_bassHistory{};
    std::array<float, HISTORY_CAPACITY> m_fullHistory{};
    size_t m_historyIdx = 0;
    size_t m_historyCount = 0;

    double m_lastBeatT = -1000.0;
    double m_lastLevelT = -1000.0;

    float m_lastBassRMS = 0.0f;
    float m_lastFullRMS = 0.0f;

    // Verified constants from Lumn 1.1 Mach-O disassembly
    static constexpr float BASS_THRESH = 0.003f;       // 0x3b449ba6
    static constexpr float FULL_THRESH = 0.006f;       // 0x3bc49ba6
    static constexpr float BASS_RATIO  = 1.35f;        // 0x3faccccd
    static constexpr float FULL_RATIO  = 1.50f;        // 0x3fc00000
    static constexpr float BASS_NORM   = 0.04f;        // 0x3d23d70a
    static constexpr float FULL_NORM   = 0.10f;        // 0x3dcccccd
    static constexpr double REFRACTORY_WINDOW = 0.25;  // 0.25s (max 240 BPM, 0x3fd0000000000000)
    static constexpr double LEVEL_INTERVAL = 0.10;     // 10 Hz level dispatch (0x3fb999999999999a)
};

} // namespace lumn::audio
