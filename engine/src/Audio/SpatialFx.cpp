#include "okay/Audio/SpatialFx.hpp"
#include "okay/Math/Mathf.hpp"
#include <algorithm>
#include <cmath>

namespace okay {

namespace {

// ---- RoomReverb tuning: our own numbers (docs/audio.md, "Reverb zones") -----------------------------------------
// Eight delay lines whose lengths spread geometrically from 22 ms to 41 ms, each rounded up to a prime number of samples
// at the output rate, so no two lines share a period and their echoes do not pile up on the same samples. 22 ms keeps
// the first reflection clear of the direct sound; 41 ms keeps the echo density high enough to sound smooth.
constexpr double kLineShortMs = 22.0;
constexpr double kLineLongMs  = 41.0;
// Two input all-pass diffusers (5.3 ms and 1.9 ms, also primes in samples) smear an impulse before it enters the network.
constexpr double kDiffuserMs[RoomReverb::kDiffusers] = {5.3, 1.9};
constexpr float  kDiffuserGain = 0.62f;
// Signs with which the input enters each line, so the lines start out of step with one another.
constexpr float  kInputSign[RoomReverb::kLines] = {1, -1, 1, 1, -1, 1, -1, -1};
// Settings mapping. Decay time to -60 dB: 0.2 s at room 0 up to 6.7 s at room 1, steeper near the top so the presets
// land on familiar spaces (Outdoor about 0.2 s, Room about 0.9 s, Hall about 3.9 s, Cave about 6.1 s). Damping sets a
// one-pole low-pass in every loop: 0.05 (bright) at damp 0 up to 0.65 (dark) at damp 1.
constexpr double kRt60Base  = 0.2;
constexpr double kRt60Range = 6.5;
constexpr double kRt60Curve = 2.5;
constexpr float  kDampMin   = 0.05f;
constexpr float  kDampRange = 0.60f;
// Input level: calibrated on the Room preset so the tail's energy over the first second matches the reverb this replaced
// within 0.5 dB, so the presets' `wet` values keep their meaning (tests/test_spatial_audio.cpp checks the level).
constexpr float  kInputScale = 0.226f;

bool IsPrime(int n) {
    if (n < 2) return false;
    if (n % 2 == 0) return n == 2;
    for (int d = 3; (long long)d * d <= n; d += 2) if (n % d == 0) return false;
    return true;
}

int PrimeAtLeast(int n) {
    if (n < 2) n = 2;
    while (!IsPrime(n)) ++n;
    return n;
}

int SamplesFor(double ms, int rate) {
    const long long n = std::llround(ms * (double)rate / 1000.0);
    return n < 1 ? 1 : (int)n;
}

float SnapDenormal(float x) {
    return (x > -1.0e-12f && x < 1.0e-12f) ? 0.0f : x;
}

} // namespace

ReverbSettings PresetSettings(ReverbPreset preset) {
    switch (preset) {
    case ReverbPreset::Hall:    return {0.80f, 0.28f, 0.34f, 1.00f};
    case ReverbPreset::Cave:    return {0.96f, 0.10f, 0.52f, 1.00f};
    case ReverbPreset::Outdoor: return {0.10f, 0.80f, 0.05f, 0.25f};
    case ReverbPreset::Room:
    default:                    return {0.42f, 0.50f, 0.22f, 0.55f};
    }
}

const char* ReverbPresetName(ReverbPreset preset) {
    switch (preset) {
    case ReverbPreset::Hall:    return "Hall";
    case ReverbPreset::Cave:    return "Cave";
    case ReverbPreset::Outdoor: return "Outdoor";
    case ReverbPreset::Room:
    default:                    return "Room";
    }
}

void RoomReverb::Ring::Resize(int n) {
    if (n < 1) n = 1;
    if ((int)buf.size() == n) return;
    buf.assign((std::size_t)n, 0.0f);
    pos = 0;
}

void RoomReverb::Ring::Wipe() {
    std::fill(buf.begin(), buf.end(), 0.0f);
    pos = 0;
}

void RoomReverb::Ring::WriteAdvance(float v) {
    if (buf.empty()) return;
    buf[(std::size_t)pos] = v;
    if (++pos >= (int)buf.size()) pos = 0;
}

void RoomReverb::Prepare(int rate) {
    if (rate < 8000) rate = 8000;
    if (rate > 192000) rate = 192000;
    if (rate == m_rate && !m_lines[0].buf.empty()) return;
    m_rate = rate;
    int previous = 0;
    for (int i = 0; i < kLines; ++i) {
        const double ms = kLineShortMs * std::pow(kLineLongMs / kLineShortMs, (double)i / (double)(kLines - 1));
        int n = PrimeAtLeast(SamplesFor(ms, rate));
        if (n <= previous) n = PrimeAtLeast(previous + 1);   // strictly longer than the line before
        m_lines[i].Resize(n);
        previous = n;
    }
    for (int d = 0; d < kDiffusers; ++d) m_diff[d].Resize(PrimeAtLeast(SamplesFor(kDiffuserMs[d], rate)));
    Clear();
    UpdateGains();
}

void RoomReverb::Clear() {
    for (Ring& r : m_lines) r.Wipe();
    for (Ring& r : m_diff) r.Wipe();
    for (float& z : m_lowpass) z = 0.0f;
    m_blockPeak = 0.0f;
}

void RoomReverb::Configure(const ReverbSettings& settings) {
    if (m_configured && settings.room == m_settings.room && settings.damp == m_settings.damp &&
        settings.wet == m_settings.wet && settings.width == m_settings.width) return;   // the mixer calls this every block
    m_settings = settings;
    m_configured = true;
    const double room = Mathf::Clamp(std::isfinite(settings.room) ? settings.room : 0.0f, 0.0f, 1.0f);
    m_rt60 = (float)(kRt60Base + kRt60Range * std::pow(room, kRt60Curve));
    m_damp = Mathf::Clamp(std::isfinite(settings.damp) ? settings.damp : 0.0f, 0.0f, 1.0f);
    m_wet = Mathf::Clamp(std::isfinite(settings.wet) ? settings.wet : 0.0f, 0.0f, 1.0f);
    m_width = Mathf::Clamp(std::isfinite(settings.width) ? settings.width : 0.0f, 0.0f, 1.0f);
    UpdateGains();
}

// Jot's rule: a line of d samples, passed n times per second, must lose 60 dB in rt60 seconds, so each pass multiplies
// by 10^(-3 d / (rt60 * rate)). Longer lines lose more per pass, which keeps every line decaying at the same speed.
void RoomReverb::UpdateGains() {
    if (m_rate <= 0) return;
    for (int i = 0; i < kLines; ++i) {
        const double d = (double)m_lines[i].buf.size();
        m_gain[i] = (float)std::pow(10.0, -3.0 * d / ((double)m_rt60 * (double)m_rate));
    }
}

void RoomReverb::BeginBlock() { m_blockPeak = 0.0f; }

void RoomReverb::Process(float input, float& left, float& right) {
    if (m_rate <= 0 || m_lines[0].buf.empty()) Prepare(m_rate > 0 ? m_rate : 44100);
    if (!std::isfinite(input)) input = 0.0f;
    float x = input * kInputScale;

    // Input diffusion: all-pass w[n] = x[n] + g w[n-M], y[n] = w[n-M] - g w[n].
    for (Ring& d : m_diff) {
        const float delayed = d.Read();
        const float w = SnapDenormal(x + kDiffuserGain * delayed);
        x = delayed - kDiffuserGain * w;
        d.WriteAdvance(w);
    }

    // Line outputs. Left and right take two orthogonal sign patterns (rows 0 and 1 of the Hadamard matrix), so a
    // full-width tail is uncorrelated between the ears.
    float v[kLines];
    float l = 0.0f, r = 0.0f;
    for (int i = 0; i < kLines; ++i) {
        v[i] = m_lines[i].Read();
        l += v[i];
        r += (i & 1) ? -v[i] : v[i];
    }

    // Feedback: low-pass and loss per line, then the lossless mix (an 8 x 8 Hadamard matrix divided by sqrt(8), done
    // as three butterfly passes), then the input, written back into the lines.
    const float a = kDampMin + kDampRange * m_damp;
    float u[kLines];
    for (int i = 0; i < kLines; ++i) {
        m_lowpass[i] = SnapDenormal((1.0f - a) * v[i] + a * m_lowpass[i]);
        u[i] = m_lowpass[i] * m_gain[i];
    }
    for (int h = 1; h < kLines; h *= 2)
        for (int b = 0; b < kLines; b += 2 * h)
            for (int k = b; k < b + h; ++k) { const float p = u[k], q = u[k + h]; u[k] = p + q; u[k + h] = p - q; }
    const float norm = 1.0f / std::sqrt((float)kLines);
    for (int i = 0; i < kLines; ++i) m_lines[i].WriteAdvance(SnapDenormal(u[i] * norm + kInputSign[i] * x));

    if (!std::isfinite(l) || !std::isfinite(r)) {
        Clear();
        left = right = 0.0f;
        return;
    }
    l *= norm; r *= norm;
    const float mag = std::fabs(l) > std::fabs(r) ? std::fabs(l) : std::fabs(r);
    if (mag > m_blockPeak) m_blockPeak = mag;
    // Width as mid and side: width 1 keeps the two patterns apart, width 0 plays their average on both sides.
    const float mid = 0.5f * (l + r), side = 0.5f * (l - r);
    left = m_wet * (mid + m_width * side);
    right = m_wet * (mid - m_width * side);
}

float DopplerRatio(const Vec3& listenerVelocity, const Vec3& sourceVelocity, const Vec3& dir,
                   float scale, float speedOfSound) {
    const float c = speedOfSound > 1.0f ? speedOfSound : 1.0f;
    if (!(scale > 1.0e-6f)) return 1.0f;
    const float limit = 0.9f * c;
    float listenerAlong = Vec3::Dot(listenerVelocity, dir);
    float sourceAlong = Vec3::Dot(sourceVelocity, dir);
    listenerAlong = Mathf::Clamp(listenerAlong, -limit, limit);
    sourceAlong = Mathf::Clamp(sourceAlong, -limit, limit);
    const float num = c + scale * listenerAlong;
    const float den = c + scale * sourceAlong;
    if (!(den > 1.0f) || !std::isfinite(num) || !std::isfinite(den)) return 1.0f;
    const float ratio = num / den;
    if (!(ratio > 0.0f) || !std::isfinite(ratio)) return 1.0f;
    return Mathf::Clamp(ratio, 0.25f, 4.0f);
}

float DistanceLowPassHz(float distance, float minDistance, float maxDistance,
                        float distantHz, bool enabled, float openHz) {
    if (!enabled) return openHz;
    if (!(distantHz > 10.0f)) distantHz = 10.0f;
    if (distance <= minDistance) return openHz;
    const float span = maxDistance - minDistance;
    if (!(span > 1.0e-4f)) return distantHz;
    float t = (distance - minDistance) / span;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    const float curve = t * t;
    return openHz + (distantHz - openHz) * curve;
}

OcclusionTone OcclusionAttenuation(bool blocked, float blockedVolume, float blockedHz, float openHz) {
    OcclusionTone tone;
    tone.volume = 1.0f;
    tone.lowPassHz = openHz;
    if (!blocked) return tone;
    tone.volume = Mathf::Clamp(blockedVolume, 0.0f, 1.0f);
    tone.lowPassHz = blockedHz > 10.0f ? blockedHz : 10.0f;
    return tone;
}

} // namespace okay
