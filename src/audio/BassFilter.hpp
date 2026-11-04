#pragma once

#include <cstdint>

namespace lumn::audio
{

struct FilteredRMS
{
    float bassRMS = 0.0f;
    float fullRMS = 0.0f;
};

// 1st-Order IIR Lowpass Bass Filter & RMS Energy Extractor
// Reconstructed from Lumn 1.1 AudioBeatDetector (scratch/Lumn_arm64 @ 0x10003478c)
// Formula: y[n] = y[n-1] + alpha * (x[n] - y[n-1])
class BassFilter
{
public:
    BassFilter();
    explicit BassFilter(uint32_t sampleRate);

    void Reset();
    void SetSampleRate(uint32_t sampleRate);

    // Process a block of mono Float32 audio samples and compute RMS energies
    FilteredRMS Process(const float* samples, uint32_t numSamples);

    float GetFilterState() const noexcept { return m_lpState; }
    float GetAlpha() const noexcept { return m_alpha; }

private:
    float m_alpha = 0.02f;     // Verified IEEE 754 float 0x3ca3d70a (cutoff ~140.4 Hz @ 44.1 kHz)
    float m_lpState = 0.0f;    // y[n-1] filter memory
    uint32_t m_sampleRate = 44100;
};

} // namespace lumn::audio

