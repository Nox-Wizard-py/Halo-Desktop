#include "BassFilter.hpp"
#include <cmath>

namespace lumn::audio
{

BassFilter::BassFilter()
    : BassFilter(44100)
{
}

BassFilter::BassFilter(uint32_t sampleRate)
{
    SetSampleRate(sampleRate);
    Reset();
}

void BassFilter::Reset()
{
    m_lpState = 0.0f;
}

void BassFilter::SetSampleRate(uint32_t sampleRate)
{
    m_sampleRate = (sampleRate > 0) ? sampleRate : 44100;

    // Base alpha is exactly 0.02f at 44.1 kHz (IEEE 754 float 0x3ca3d70a)
    // Scale proportionally with sample rate to maintain ~140.4 Hz physical cutoff frequency:
    // fc = (alpha * fs) / (2 * pi) => alpha = (2 * pi * fc) / fs
    constexpr float BASE_ALPHA = 0.02f;
    constexpr float BASE_RATE = 44100.0f;
    m_alpha = BASE_ALPHA * (BASE_RATE / static_cast<float>(m_sampleRate));
}

FilteredRMS BassFilter::Process(const float* samples, uint32_t numSamples)
{
    FilteredRMS result{};
    if (!samples || numSamples == 0)
    {
        return result;
    }

    float bassSum = 0.0f;
    float fullSum = 0.0f;
    float lp = m_lpState;
    const float alpha = m_alpha;

    for (uint32_t i = 0; i < numSamples; ++i)
    {
        const float x = samples[i];
        lp += alpha * (x - lp);
        bassSum += lp * lp;
        fullSum += x * x;
    }

    m_lpState = lp;

    const float invN = 1.0f / static_cast<float>(numSamples);
    result.bassRMS = std::sqrt(bassSum * invN);
    result.fullRMS = std::sqrt(fullSum * invN);

    return result;
}

} // namespace lumn::audio

