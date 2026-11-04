#include "BeatDetector.hpp"
#include <numeric>
#include <algorithm>
#include <fstream>
#include <string>

namespace lumn::audio
{

static void LogBeat(const std::string& msg)
{
    std::ofstream f("lumn_debug.log", std::ios::app);
    f << msg << std::endl;
}

BeatDetector::BeatDetector()
{
    Reset();
}

void BeatDetector::SetCallbacks(BeatCallback onBeat, LevelCallback onLevel)
{
    m_onBeat = std::move(onBeat);
    m_onLevel = std::move(onLevel);
}

void BeatDetector::Reset()
{
    m_bassFilter.Reset();
    m_bassHistory.fill(0.0f);
    m_fullHistory.fill(0.0f);
    m_historyIdx = 0;
    m_historyCount = 0;
    m_lastBeatT = -1000.0;
    m_lastLevelT = -1000.0;
    m_lastBassRMS = 0.0f;
    m_lastFullRMS = 0.0f;
}

void BeatDetector::ProcessSamples(const float* samples, uint32_t numSamples, uint32_t sampleRate, double curTime)
{
    if (!samples || numSamples == 0)
    {
        return;
    }

    m_bassFilter.SetSampleRate(sampleRate);
    FilteredRMS rms = m_bassFilter.Process(samples, numSamples);

    m_lastBassRMS = rms.bassRMS;
    m_lastFullRMS = rms.fullRMS;

    // Record into 43-frame circular ring buffers
    m_bassHistory[m_historyIdx] = rms.bassRMS;
    m_fullHistory[m_historyIdx] = rms.fullRMS;
    m_historyIdx = (m_historyIdx + 1) % HISTORY_CAPACITY;
    if (m_historyCount < HISTORY_CAPACITY)
    {
        m_historyCount++;
    }

    // Compute rolling average over available history
    float bassSum = 0.0f;
    float fullSum = 0.0f;
    for (size_t i = 0; i < m_historyCount; ++i)
    {
        bassSum += m_bassHistory[i];
        fullSum += m_fullHistory[i];
    }
    const float invCount = 1.0f / static_cast<float>(m_historyCount);
    const float avgBass = bassSum * invCount;
    const float avgFull = fullSum * invCount;

    // Dynamic Beat Decision Criteria (Lumn 1.1 AudioBeatDetector)
    const bool isBassBeat = (rms.bassRMS > BASS_THRESH) && (rms.bassRMS > BASS_RATIO * avgBass);
    const bool isFullBeat = (rms.fullRMS > FULL_THRESH) && (rms.fullRMS > FULL_RATIO * avgFull);

    if ((isBassBeat || isFullBeat) && (curTime - m_lastBeatT >= REFRACTORY_WINDOW))
    {
        m_lastBeatT = curTime;
        LogBeat("[BEAT] Beat event detected at t=" + std::to_string(curTime) + 
                "s (bassRMS=" + std::to_string(rms.bassRMS) + 
                ", avgBass=" + std::to_string(avgBass) + 
                ", fullRMS=" + std::to_string(rms.fullRMS) + ")");
        if (m_onBeat)
        {
            m_onBeat(curTime);
        }
    }

    // 10 Hz Level Dispatch
    if (curTime - m_lastLevelT >= LEVEL_INTERVAL)
    {
        m_lastLevelT = curTime;
        const float normBass = rms.bassRMS / BASS_NORM;
        const float normFull = rms.fullRMS / FULL_NORM;
        const float level = std::clamp(std::max(normBass, normFull), 0.0f, 1.0f);

        if (m_onLevel)
        {
            m_onLevel(static_cast<double>(level), curTime);
        }
    }
}

} // namespace lumn::audio
