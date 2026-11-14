#pragma once
// Distance, motion and room effects for the mixer. The math is plain functions so
// tests can check a pitch ratio, a muffling curve and an occlusion level without
// a scene. The reverberator is a feedback delay network of our own design (see
// RoomReverb below and docs/audio.md): written from the published papers on delay
// networks, with delay lengths, gains and the settings mapping chosen and checked
// here. Nothing here is on unless a caller asks.
#include "okay/Math/Vec3.hpp"
#include <vector>

namespace okay {

/// Speed of sound used by the doppler ratio, in the same units as velocities
/// (metres per second when the scene is in metres).
constexpr float kSpeedOfSound = 343.0f;

enum class ReverbPreset { Room = 0, Hall, Cave, Outdoor, Count };

/// Room character. `room` is 0..1 (longer tail when higher), `damp` is 0..1
/// (more high-frequency loss when higher), `wet` is how loud the tail is
/// against a full-scale input, `width` is 0..1 stereo spread.
struct ReverbSettings {
    float room = 0.42f;
    float damp = 0.50f;
    float wet = 0.22f;
    float width = 0.55f;
};

ReverbSettings PresetSettings(ReverbPreset preset);
const char* ReverbPresetName(ReverbPreset preset);

/// Reverberator: a feedback delay network (FDN). The idea is from the papers, not
/// from any program: Stautner and Puckette, "Designing Multi-Channel Reverberators"
/// (Computer Music Journal, 1982), for delay lines fed back through a lossless
/// mixing matrix, and Jot and Chaigne, "Digital Delay Networks for Designing
/// Artificial Reverberators" (AES, 1991), for setting each line's loss from the
/// wanted decay time and putting a low-pass in each loop for frequency-dependent
/// decay. Two all-pass diffusers (Schroeder, "Natural Sounding Artificial
/// Reverberation", JAES 1962) thicken the input first. All lengths, gains and the
/// settings mapping are our own choices, listed in SpatialFx.cpp and docs/audio.md.
///
/// `Process` takes one mono sample and writes a stereo tail. The dry signal is not
/// mixed in: the caller keeps it and adds this. `Silent` is true when the last
/// block produced no tail, so the mixer can skip the bus entirely.
class RoomReverb {
public:
    static constexpr int kLines = 8;       ///< delay lines in the network (a power of two for the Hadamard mix)
    static constexpr int kDiffusers = 2;   ///< all-pass diffusers on the input

    void Prepare(int rate);
    void Clear();
    void Configure(const ReverbSettings& settings);
    /// Call once before a block of `Process`. The silence check uses the
    /// previous block's tail, then this starts a new measurement.
    void BeginBlock();
    void Process(float input, float& left, float& right);
    bool Silent() const { return m_blockPeak < 1.0e-6f; }
    int Rate() const { return m_rate; }
    /// Decay time to -60 dB (seconds) the current settings ask for, at low frequencies.
    float DecaySeconds() const { return m_rt60; }
    /// Length of delay line `i` in samples at the prepared rate (0 before Prepare).
    int LineLength(int i) const { return (i >= 0 && i < kLines) ? (int)m_lines[i].buf.size() : 0; }

private:
    struct Ring {
        std::vector<float> buf;
        int pos = 0;
        void Resize(int n);
        void Wipe();
        float Read() const { return buf.empty() ? 0.0f : buf[(std::size_t)pos]; }
        void WriteAdvance(float v);
    };
    void UpdateGains();

    Ring m_lines[kLines];
    Ring m_diff[kDiffusers];
    float m_lowpass[kLines] = {};   // one-pole low-pass state per line
    float m_gain[kLines] = {};      // per-line loss for the wanted decay
    int m_rate = 0;
    float m_rt60 = 1.0f;
    float m_damp = 0.5f;
    float m_wet = 0.0f;
    float m_width = 1.0f;
    float m_blockPeak = 0.0f;
    ReverbSettings m_settings{};
    bool m_configured = false;
};

/// Old name of the reverb bus, kept so existing code still builds. The class is no
/// longer a Schroeder comb/all-pass design; new code should say RoomReverb.
using SchroederReverb = RoomReverb;

/// Pitch ratio from motion along `dir` (a unit vector from the listener toward
/// the source). Listener speed along `dir` (moving toward the source) raises
/// the pitch; source speed along `dir` (moving away) lowers it. `scale` 0
/// leaves the ratio at 1. Speeds are clamped so a value past the speed of
/// sound cannot divide by zero or flip the sign.
float DopplerRatio(const Vec3& listenerVelocity, const Vec3& sourceVelocity, const Vec3& dir,
                   float scale, float speedOfSound = kSpeedOfSound);

/// Cutoff in Hz. Off, or a distance inside `minDistance`, returns `openHz`.
/// From there to `maxDistance` the cutoff moves toward `distantHz` along t^2,
/// so the middle of the range stays brighter than a straight line.
float DistanceLowPassHz(float distance, float minDistance, float maxDistance,
                        float distantHz, bool enabled, float openHz = 22000.0f);

/// Steady occlusion. Clear sound is full volume and `openHz`. A blocked sound
/// is `blockedVolume` (clamped 0..1) and `blockedHz`.
struct OcclusionTone {
    float volume = 1.0f;
    float lowPassHz = 22000.0f;
};
OcclusionTone OcclusionAttenuation(bool blocked, float blockedVolume, float blockedHz,
                                   float openHz = 22000.0f);

} // namespace okay
