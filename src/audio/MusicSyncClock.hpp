#pragma once

#include <mutex>
#include <cstdint>

namespace lumn::audio
{

struct AudioRenderState
{
    float phase = 0.0f;      // Continuous accumulated perimeter phase argument
    float bloom = 0.0f;      // Dynamic bloom impulse [0.0, 1.0]
    float energy = 0.0f;     // Continuous dynamic audio energy envelope [0.0, 1.0]
    float beatPulse = 0.0f;  // Instantaneous Gaussian beat transient [0.0, 1.0]
    float bpm = 120.0f;      // Smoothed tempo estimation
    bool isPlaying = false;  // Active audio presence flag
};

// Rhythm & Phase Synchronization Clock
// Reconstructed from Lumn 1.1 MusicSyncClock (scratch/Lumn_arm64 @ 0x10007d47c)
class MusicSyncClock
{
public:
    MusicSyncClock();

    void Reset();

    // Invoked by beat detector on rhythm events
    void RecordBeat(double now);

    // Invoked by beat detector at 10 Hz with normalized RMS energy [0, 1]
    void UpdateLevel(double level, double now);

    // Per-frame evaluation on render thread; returns current render state
    // When isPlaying is false, phase and position freeze completely (zero drift, zero jump on resume)
    AudioRenderState Tick(double now, bool isPlaying = true);

    // Direct queries
    double GetBPM() const;
    bool IsPlaying() const;
    double GetPhase() const;
    float GetBloom(double now) const;

private:
    mutable std::mutex m_mutex;

    // Tempo & Phase State
    double m_bpm = 120.0;
    bool m_playing = false;
    double m_interval = 0.50;      // Seconds per beat (0.50s for 120 BPM)
    double m_energy = 0.0;         // Smoothed dynamic energy [0, 1]
    double m_continuousEnergy = 0.0; // Fast-attack, smooth-decay continuous envelope [0, 1]
    double m_rawTargetLevel = 0.0; // Latest input level target
    double m_peakRef = 0.30;       // Rolling peak envelope reference
    double m_dynEase = 0.0;        // Eased dynamic drive factor [0, 1]
    double m_lastLoud = -1000.0;   // Timestamp of last loud packet (> 0.02)
    double m_baseline = 0.0;       // Long-term noise floor
    double m_bloomAt = -1000.0;    // Timestamp when current bloom triggered
    double m_lastBloom = -1000.0;  // Timestamp of previous bloom
    double m_lastBeat = -1000.0;   // Timestamp of previous detected beat
    double m_beatPhase = 0.0;      // Continuous phase ramp [0, inf)
    double m_phaseCorr = 0.0;      // Proportional error correction term
    double m_pos = 0.0;            // Accumulated shader phase argument
    double m_lastFrame = 0.0;      // Previous frame timestamp

    // Idle animation drift state
    double m_idleSpeed = 0.01818;  // Verified Lumn idle perimeter drift speed

    // Verified constants from Lumn 1.1 disassembly
    static constexpr double SCALE         = 0.154;              // 0x3fc3b645a1cac083
    static constexpr double INTERVAL_OLD  = 0.70;               // 0x3fe6666666666666
    static constexpr double INTERVAL_NEW  = 0.30;               // 0x3fd3333333333333
    static constexpr double ENERGY_OLD    = 0.82;               // 0x3fea3d70a3d70a3d
    static constexpr double ENERGY_NEW    = 0.18;               // 0x3fc70a3d70a3d70a
    static constexpr double PEAK_DECAY    = 0.988029;           // 0x3fef9def9db22d0e
    static constexpr double FLOOR_ALPHA   = 0.04;               // 0x3fa47ae147ae147b
    static constexpr double BLOOM_ATTACK  = 0.12078;            // 0x3fbeeb851eb851eb
    static constexpr double BLOOM_DECAY   = 1.08;               // 0x3ff147ae147ae148
    static constexpr double BLOOM_DUR     = 1.20;               // 0x3ff3333333333333
    static constexpr double GAUSSIAN_K    = -22.0;              // 0xc036000000000000
    static constexpr double SILENCE_TIMEOUT = 1.50;             // 1.5s loud audio timeout
};

} // namespace lumn::audio

