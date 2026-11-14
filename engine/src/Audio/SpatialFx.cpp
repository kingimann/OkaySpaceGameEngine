#include "okay/Audio/SpatialFx.hpp"
#include "okay/Math/Mathf.hpp"
#include <algorithm>
#include <cmath>

namespace okay {

namespace {

// Reverb tuning, chosen and derived here (nothing is taken from another reverb's published numbers).
//
// Shape: eight parallel feedback combs with a one-pole low-pass in the loop, then four all-pass filters in series;
// the textbook Schroeder / Moorer layout. The numbers below are ours:
//  * Comb delays are primes between 1102 and 1984 samples, i.e. 25 to 45 ms at 44.1 kHz, spread roughly evenly
//    (1151, 1303, 1427, ... 1949; steps of 100 to 150 samples). Distinct primes share no common factor, so the
//    combs' echo trains do not line up into a ringing pitch (they are not harmonics of one another). Long enough
//    for a smooth tail, short enough that the first echo is heard as part of the room and not as a slapback.
//  * The right channel uses a second set of primes a few samples to a few dozen longer, so the two tails differ
//    (stereo width) without a fixed copy of the left one.
//  * All-pass delays are primes between 233 and 521 samples, i.e. 5.3 to 11.5 ms at 44.1 kHz, shortest last.
//    They add echo density without colouring the level (an all-pass has a flat magnitude).
//  * At another sample rate every delay is scaled by rate / 44100, rounded, and moved up to the next prime so
//    the delays stay mutually prime.
//  * The tail time comes from `room` as an RT60 (seconds for the tail to fall by 60 dB): rt60 = 0.3 * 20^room,
//    so 0.3 s to 6 s. A comb of d samples that must lose 60 dB in rt60 seconds needs, per pass round its loop,
//    the gain g = 10^(-3 d / (rt60 * rate)) (60 dB is a factor of 10^-3; a pass takes d / rate seconds).
//  * Damping makes the high frequencies die faster: the in-loop one-pole low-pass y = (1 - a) x + a y has gain 1 at
//    DC and (1 - a) / (1 + a) at half the sample rate. `damp` 0..1 sets the RT60 at the top of the band to
//    rt60 * (1 - 0.9 * damp); the comb then needs g_hf = 10^(-3 d / (rt60_hf * rate)), and a follows from
//    (1 - a) / (1 + a) = g_hf / g. a is capped at 0.98.
//  * g is capped at 0.995 so the loop gain is always below 1: the reverb cannot blow up, whatever the settings.
//  * All-pass gain 0.65 (Schroeder's classic range is 0.5 to 0.7). The all-pass is the exact form
//    v = x - g * buffer, y = buffer + g * v, whose magnitude response is 1 at every frequency.
//  * Input gain 1/80: the eight combs add, so a hot input is scaled down; chosen so a full-scale noise input
//    stays well inside the range at the longest RT60.
// The sound is therefore not the same as the earlier tuning's; the API and the presets' names are unchanged.
constexpr float kInputGain = 1.0f / 80.0f;
constexpr float kAllpassGain = 0.65f;
constexpr int kCombLeft[8]   = {1151, 1303, 1427, 1571, 1693, 1787, 1873, 1949};
constexpr int kCombRight[8]  = {1163, 1319, 1439, 1583, 1709, 1801, 1889, 1951};
constexpr int kAllLeft[4]    = {509, 401, 311, 233};
constexpr int kAllRight[4]   = {521, 409, 317, 239};
constexpr double kMinRt60 = 0.05, kMaxCombGain = 0.995, kMaxDamp = 0.98;

bool IsPrime(int n) {
    if (n < 2) return false;
    for (int k = 2; (long long)k * k <= n; ++k) if (n % k == 0) return false;
    return true;
}

int ScaledDelay(int tuning, int rate) {
    int n = (int)std::lround((double)tuning * (double)rate / 44100.0);
    if (n < 2) n = 2;
    while (!IsPrime(n)) ++n;
    return n;
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

float SchroederReverb::Delay::Comb(float input) {
    if (buf.empty()) return 0.0f;
    const float output = buf[(std::size_t)idx];
    store = SnapDenormal(output * (1.0f - damp) + store * damp);
    buf[(std::size_t)idx] = SnapDenormal(input + store * gain);
    if (++idx >= (int)buf.size()) idx = 0;
    return output;
}

float SchroederReverb::Delay::Allpass(float input, float g) {
    if (buf.empty()) return 0.0f;
    const float buffered = buf[(std::size_t)idx];
    const float v = SnapDenormal(input - g * buffered);
    buf[(std::size_t)idx] = v;
    if (++idx >= (int)buf.size()) idx = 0;
    return buffered + g * v;
}

void SchroederReverb::Prepare(int rate) {
    if (rate < 8000) rate = 8000;
    if (rate > 192000) rate = 192000;
    if (rate == m_rate && !m_combL[0].buf.empty()) return;
    m_rate = rate;
    for (int i = 0; i < 8; ++i) {
        m_combL[i].Resize(ScaledDelay(kCombLeft[i], rate));
        m_combR[i].Resize(ScaledDelay(kCombRight[i], rate));
    }
    for (int i = 0; i < 4; ++i) {
        m_allL[i].Resize(ScaledDelay(kAllLeft[i], rate));
        m_allR[i].Resize(ScaledDelay(kAllRight[i], rate));
    }
    Retune();
    Clear();
}

void SchroederReverb::Retune() {
    // per comb: loop gain for the wanted RT60 and a low-pass coefficient for the shorter RT60 at the top of the band
    const double rate = (double)m_rate;
    const double rt60 = std::max(kMinRt60, (double)m_rt60), rt60hf = std::max(kMinRt60, rt60 * (1.0 - 0.9 * (double)m_damp));
    Delay* combs[2] = {m_combL, m_combR};
    for (Delay* set : combs) {
        for (int i = 0; i < 8; ++i) {
            const double d = (double)set[i].buf.size();
            const double g = std::min(kMaxCombGain, std::pow(10.0, -3.0 * d / (rt60 * rate)));
            const double ghf = std::min(g, std::pow(10.0, -3.0 * d / (rt60hf * rate)));
            const double a = std::min(kMaxDamp, std::max(0.0, (1.0 - ghf / g) / (1.0 + ghf / g)));
            set[i].gain = (float)g;
            set[i].damp = (float)a;
        }
    }
}

void SchroederReverb::Clear() {
    for (int i = 0; i < 8; ++i) { m_combL[i].Wipe(); m_combR[i].Wipe(); }
    for (int i = 0; i < 4; ++i) { m_allL[i].Wipe(); m_allR[i].Wipe(); }
    m_blockPeak = 0.0f;
}

void SchroederReverb::Configure(const ReverbSettings& settings) {
    const auto fin = [](float v, float fallback) { return std::isfinite(v) ? v : fallback; };   // NaN keeps the default
    const float room = Mathf::Clamp(fin(settings.room, 0.42f), 0.0f, 1.0f);
    m_rt60 = (float)(0.3 * std::pow(20.0, (double)room));   // 0.3 s to 6 s
    m_damp = Mathf::Clamp(fin(settings.damp, 0.5f), 0.0f, 1.0f);
    m_wet = Mathf::Clamp(fin(settings.wet, 0.22f), 0.0f, 1.0f);
    m_width = Mathf::Clamp(fin(settings.width, 0.55f), 0.0f, 1.0f);
    if (m_rate > 0 && !m_combL[0].buf.empty()) Retune();
}

void SchroederReverb::BeginBlock() { m_blockPeak = 0.0f; }

void SchroederReverb::Process(float input, float& left, float& right) {
    if (m_rate <= 0 || m_combL[0].buf.empty()) Prepare(m_rate > 0 ? m_rate : 44100);
    if (!std::isfinite(input)) input = 0.0f;
    const float x = input * kInputGain;
    float l = 0.0f, r = 0.0f;
    for (int i = 0; i < 8; ++i) {
        l += m_combL[i].Comb(x);
        r += m_combR[i].Comb(x);
    }
    for (int i = 0; i < 4; ++i) {
        l = m_allL[i].Allpass(l, kAllpassGain);
        r = m_allR[i].Allpass(r, kAllpassGain);
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
