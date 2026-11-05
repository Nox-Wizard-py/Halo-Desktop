#include "MusicSyncClock.hpp"
#include <cmath>
#include <algorithm>
#include <fstream>
#include <string>

namespace lumn::audio
{

static void LogClock(const std::string& msg)
{
    std::ofstream f("lumn_debug.log", std::ios::app);
    f << msg << std::endl;
}

MusicSyncClock::MusicSyncClock()
{
    Reset();
}

void MusicSyncClock::Reset()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_bpm = 120.0;
    m_playing = false;
    m_interval = 0.50;
    m_energy = 0.0;
    m_continuousEnergy = 0.0;
    m_rawTargetLevel = 0.0;
    m_peakRef = 0.30;
    m_dynEase = 0.0;
    m_lastLoud = -1000.0;
    m_baseline = 0.0;
    m_bloomAt = -1000.0;
    m_lastBloom = -1000.0;
    m_lastBeat = -1000.0;
    m_beatPhase = 0.0;
    m_phaseCorr = 0.0;
    m_pos = 0.0;
    m_lastFrame = 0.0;
}

void MusicSyncClock::RecordBeat(double now)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const double dt = now - m_lastBeat;
    m_lastBeat = now;

    // Validate plausible inter-beat interval (30 to 240 BPM)
    if (dt < 0.25 || dt > 2.0)
    {
        return;
    }

    // Interval EMA & BPM calculation
    m_interval = INTERVAL_OLD * m_interval + INTERVAL_NEW * dt;
    m_bpm = 60.0 / m_interval;
    m_playing = true;

    LogClock("[CLOCK] Beat locked: dt=" + std::to_string(dt) + 
             "s, interval=" + std::to_string(m_interval) + 
             "s, BPM=" + std::to_string(m_bpm));

    // Phase error locking to nearest beat boundary
    const double frac = m_beatPhase - std::floor(m_beatPhase);
    const double error = (frac < 0.5) ? -frac : (1.0 - frac);
    m_phaseCorr = std::clamp(m_phaseCorr + error, -0.5, 0.5);
}

void MusicSyncClock::UpdateLevel(double level, double now)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const double clampedLevel = std::clamp(level, 0.0, 1.0);
    m_rawTargetLevel = clampedLevel;

    if (clampedLevel > 0.02)
    {
        m_lastLoud = now;
        m_playing = true;
    }

    m_energy = m_energy * ENERGY_OLD + clampedLevel * ENERGY_NEW;
    m_peakRef = std::max(clampedLevel, m_peakRef * PEAK_DECAY);

    // Auto-bloom surge check
    if ((now - m_lastBloom > 8.0) &&
        (clampedLevel > m_peakRef * 0.70) &&
        (clampedLevel > m_baseline * 1.60) &&
        (m_peakRef > m_baseline * 1.55))
    {
        m_bloomAt = now;
        m_lastBloom = now;
        LogClock("[CLOCK] Auto-Bloom surge triggered at t=" + std::to_string(now) + "s!");
    }

    m_baseline += FLOOR_ALPHA * (clampedLevel - m_baseline);
}

AudioRenderState MusicSyncClock::Tick(double now, bool isPlaying)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    AudioRenderState state{};
    double dt = 0.016;
    if (m_lastFrame > 0.0)
    {
        dt = std::clamp(now - m_lastFrame, 0.0, 0.10);
    }
    m_lastFrame = now;

    // Check if loud audio was heard recently
    const bool isAudioActive = (now - m_lastLoud < SILENCE_TIMEOUT);
    if (!isAudioActive)
    {
        m_playing = false;
        m_rawTargetLevel = 0.0;
    }

    // Continuously update asymmetric attack/release continuous energy envelope
    if (m_rawTargetLevel > m_continuousEnergy)
    {
        // Fast attack (~25 ms time constant: alpha = 1 - exp(-dt / 0.025))
        const double attackAlpha = std::min(1.0, dt * 40.0);
        m_continuousEnergy += attackAlpha * (m_rawTargetLevel - m_continuousEnergy);
    }
    else
    {
        // Smooth release (~250 ms time constant: alpha = 1 - exp(-dt / 0.250))
        const double releaseAlpha = std::min(1.0, dt * 4.0);
        m_continuousEnergy += releaseAlpha * (m_rawTargetLevel - m_continuousEnergy);
    }

    // Playback-aware freezing: when playback is paused, freeze phase and position completely
    if (!isPlaying)
    {
        m_dynEase = std::max(0.0, m_dynEase - dt * 2.0);
        m_energy = std::max(0.0, m_energy - dt * 1.5);

        state.phase = static_cast<float>(m_pos); // Frozen at current position
        state.bloom = 0.0f;
        state.beatPulse = 0.0f;
        state.energy = static_cast<float>(m_continuousEnergy);
        state.bpm = static_cast<float>(m_bpm);
        state.isPlaying = false;
        return state;
    }

    if (!m_playing)
    {
        // Smooth restrained continuous idle rotation while active session is quiet
        m_pos += m_idleSpeed * dt;
        m_dynEase = std::max(0.0, m_dynEase - dt * 2.0);
        m_energy = std::max(0.0, m_energy - dt * 1.5);

        state.phase = static_cast<float>(m_pos);
        state.bloom = 0.0f;
        state.beatPulse = 0.0f;
        state.energy = static_cast<float>(m_continuousEnergy);
        state.bpm = static_cast<float>(m_bpm);
        state.isPlaying = false;
        return state;
    }

    // Active music playback synchronization:
    // 1. Proportional phase relaxation
    const double dCorr = std::min(dt * 6.0, 1.0) * m_phaseCorr;
    m_beatPhase += (dt / std::max(m_interval, 0.25)) + dCorr;
    m_phaseCorr -= dCorr;

    // 2. Gaussian beat pulse (peaks at 1.0 at each beat boundary)
    const double frac = m_beatPhase - std::floor(m_beatPhase);
    const double dist = std::min(frac, 1.0 - frac);
    const double pulse = std::exp(GAUSSIAN_K * dist * dist);

    // 3. Dynamic easing tracking squared dynamic energy
    const double normEnergy = std::min(1.0, m_energy / std::max(m_peakRef, 0.10));
    const double targetEase = normEnergy * normEnergy;
    m_dynEase += std::min(dt * 4.0, 1.0) * (targetEase - m_dynEase);

    // 4. Instantaneous phase velocity integration
    const double baselineSpeed = 0.18 + 0.22 * m_dynEase;
    const double pulseAmp = 0.06 + 1.94 * m_dynEase;
    const double speed = (baselineSpeed + pulse * pulseAmp) / m_interval * SCALE;
    m_pos += speed * dt;

    // 5. Evaluate bloom envelope
    float bloom = 0.0f;
    const double tBloom = now - m_bloomAt;
    if (tBloom >= 0.0 && tBloom < BLOOM_DUR && isAudioActive)
    {
        if (tBloom < BLOOM_ATTACK)
        {
            bloom = static_cast<float>(tBloom / BLOOM_ATTACK);
        }
        else
        {
            bloom = static_cast<float>(std::exp(-3.5 * ((tBloom - BLOOM_ATTACK) / BLOOM_DECAY)));
        }
    }

    state.phase = static_cast<float>(m_pos);
    state.bloom = bloom;
    state.beatPulse = static_cast<float>(pulse);
    state.energy = static_cast<float>(m_continuousEnergy);
    state.bpm = static_cast<float>(m_bpm);
    state.isPlaying = true;

    return state;
}

double MusicSyncClock::GetBPM() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_bpm;
}

bool MusicSyncClock::IsPlaying() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_playing;
}

double MusicSyncClock::GetPhase() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pos;
}

float MusicSyncClock::GetBloom(double now) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (now - m_lastLoud >= SILENCE_TIMEOUT) return 0.0f;
    const double t = now - m_bloomAt;
    if (t < 0.0 || t >= BLOOM_DUR) return 0.0f;
    if (t < BLOOM_ATTACK) return static_cast<float>(t / BLOOM_ATTACK);
    return static_cast<float>(std::exp(-3.5 * ((t - BLOOM_ATTACK) / BLOOM_DECAY)));
}

} // namespace lumn::audio
