#pragma once
// Distance, motion and room effects for the mixer. The math is plain functions so
// tests can check a pitch ratio, a muffling curve and an occlusion level without
// a scene. The reverberator is a Schroeder design: eight parallel comb filters
// with a damped feedback, then four all-pass filters in series, run per channel
// with a different set of delays on the right. Delay lengths are primes we chose
// (25 to 45 ms combs, 5 to 12 ms all-passes at 44.1 kHz), scaled to the output rate;
// the tail time is an RT60 worked out from `room` (see SpatialFx.cpp). Nothing here
// is on unless a caller asks.
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

/// Schroeder reverberator. `Process` takes one mono sample and writes a stereo
/// tail. The dry signal is not mixed in: the caller keeps it and adds this.
/// `Silent` is true when the last block produced no tail, so the mixer can
/// skip the bus entirely.
class SchroederReverb {
public:
    void Prepare(int rate);
    void Clear();
    void Configure(const ReverbSettings& settings);
    /// Call once before a block of `Process`. The silence check uses the
    /// previous block's tail, then this starts a new measurement.
    void BeginBlock();
    void Process(float input, float& left, float& right);
    bool Silent() const { return m_blockPeak < 1.0e-6f; }
    int Rate() const { return m_rate; }

private:
    struct Delay {
        std::vector<float> buf;
        int idx = 0;
        float store = 0.0f;
        float gain = 0.8f, damp = 0.0f;   // comb loop gain and low-pass coefficient, set by Retune
        void Resize(int n);
        void Wipe();
        float Comb(float input);
        float Allpass(float input, float g);
    };
    void Retune();

    Delay m_combL[8], m_combR[8], m_allL[4], m_allR[4];
    int m_rate = 0;
    float m_rt60 = 1.0f;   // seconds, from `room`
    float m_damp = 0.5f;
    float m_wet = 0.0f;
    float m_width = 1.0f;
    float m_blockPeak = 0.0f;
};

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
