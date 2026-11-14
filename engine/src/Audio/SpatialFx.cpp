#include "okay/Audio/SpatialFx.hpp"
#include "okay/Math/Mathf.hpp"
#include <algorithm>
#include <cmath>

namespace okay {

namespace {

constexpr float kInputGain = 0.015f;   // eight combs would otherwise sum far too hot
constexpr float kAllpassFeedback = 0.5f;
constexpr int kSpread = 23;
constexpr int kCombTuning[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
constexpr int kAllpassTuning[4] = {556, 441, 341, 225};

int ScaledDelay(int tuning, int rate) {
    const int n = (int)std::lround((double)tuning * (double)rate / 44100.0);
    return n < 1 ? 1 : n;
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

void SchroederReverb::Delay::Resize(int n) {
    if (n < 1) n = 1;
    if ((int)buf.size() == n) return;
    buf.assign((std::size_t)n, 0.0f);
    idx = 0;
    store = 0.0f;
}

void SchroederReverb::Delay::Wipe() {
    std::fill(buf.begin(), buf.end(), 0.0f);
    idx = 0;
    store = 0.0f;
}

float SchroederReverb::Delay::Comb(float input, float feedback, float damp) {
    if (buf.empty()) return 0.0f;
    const float output = buf[(std::size_t)idx];
    store = SnapDenormal(output * (1.0f - damp) + store * damp);
    buf[(std::size_t)idx] = SnapDenormal(input + store * feedback);
    if (++idx >= (int)buf.size()) idx = 0;
    return output;
}

float SchroederReverb::Delay::Allpass(float input, float feedback) {
    if (buf.empty()) return 0.0f;
    const float buffered = buf[(std::size_t)idx];
    buf[(std::size_t)idx] = SnapDenormal(input + buffered * feedback);
    if (++idx >= (int)buf.size()) idx = 0;
    return -input + buffered;
}

void SchroederReverb::Prepare(int rate) {
    if (rate < 8000) rate = 8000;
    if (rate > 192000) rate = 192000;
    if (rate == m_rate && !m_combL[0].buf.empty()) return;
    m_rate = rate;
    for (int i = 0; i < 8; ++i) {
        m_combL[i].Resize(ScaledDelay(kCombTuning[i], rate));
        m_combR[i].Resize(ScaledDelay(kCombTuning[i] + kSpread, rate));
    }
    for (int i = 0; i < 4; ++i) {
        m_allL[i].Resize(ScaledDelay(kAllpassTuning[i], rate));
        m_allR[i].Resize(ScaledDelay(kAllpassTuning[i] + kSpread, rate));
    }
    Clear();
}

void SchroederReverb::Clear() {
    for (int i = 0; i < 8; ++i) { m_combL[i].Wipe(); m_combR[i].Wipe(); }
    for (int i = 0; i < 4; ++i) { m_allL[i].Wipe(); m_allR[i].Wipe(); }
    m_blockPeak = 0.0f;
}

void SchroederReverb::Configure(const ReverbSettings& settings) {
    const float room = Mathf::Clamp(settings.room, 0.0f, 1.0f);
    m_feedback = Mathf::Clamp(room * 0.28f + 0.70f, 0.0f, 0.98f);
    m_damp = Mathf::Clamp(settings.damp, 0.0f, 1.0f);
    m_wet = Mathf::Clamp(settings.wet, 0.0f, 1.0f);
    m_width = Mathf::Clamp(settings.width, 0.0f, 1.0f);
}

void SchroederReverb::BeginBlock() { m_blockPeak = 0.0f; }

void SchroederReverb::Process(float input, float& left, float& right) {
    if (m_rate <= 0 || m_combL[0].buf.empty()) Prepare(m_rate > 0 ? m_rate : 44100);
    if (!std::isfinite(input)) input = 0.0f;
    const float x = input * kInputGain;
    float l = 0.0f, r = 0.0f;
    for (int i = 0; i < 8; ++i) {
        l += m_combL[i].Comb(x, m_feedback, m_damp);
        r += m_combR[i].Comb(x, m_feedback, m_damp);
    }
    for (int i = 0; i < 4; ++i) {
        l = m_allL[i].Allpass(l, kAllpassFeedback);
        r = m_allR[i].Allpass(r, kAllpassFeedback);
    }
    if (!std::isfinite(l) || !std::isfinite(r)) {
        Clear();
        left = right = 0.0f;
        return;
    }
    const float mag = std::fabs(l) > std::fabs(r) ? std::fabs(l) : std::fabs(r);
    if (mag > m_blockPeak) m_blockPeak = mag;
    const float wet1 = m_wet * (m_width * 0.5f + 0.5f);
    const float wet2 = m_wet * ((1.0f - m_width) * 0.5f);
    left = l * wet1 + r * wet2;
    right = r * wet1 + l * wet2;
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
