#include "okay/Audio/AudioMixer.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Mathf.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace okay {

bool AudioMixer::GroupFromName(const char* name, AudioGroup& out) {
    std::string n = name ? name : "";
    for (char& c : n) c = (char)std::tolower((unsigned char)c);
    for (int i = 0; i < (int)AudioGroup::Count; ++i) {
        std::string g = GroupName((AudioGroup)i);
        for (char& c : g) c = (char)std::tolower((unsigned char)c);
        if (n == g) { out = (AudioGroup)i; return true; }
    }
    return false;
}

float AudioMixer::Attenuation(const AudioSource& src, float dist) {
    const float lo = src.minDistance, hi = src.maxDistance;
    if (dist <= lo) return 1.0f;
    if (dist >= hi) return 0.0f;
    switch (src.rolloff) {
    case AudioRolloff::Inverse: {
        // Physical 1/d falloff, with a fade over the last 10% so it reaches 0 at max.
        const float inv = lo > 1e-4f ? lo / dist : 0.0f;
        const float edge = 0.9f * hi + 0.1f * lo;
        const float tail = dist > edge ? (hi - dist) / (hi - edge) : 1.0f;
        return Mathf::Clamp(inv * tail, 0.0f, 1.0f);
    }
    case AudioRolloff::Logarithmic: {
        if (lo <= 1e-4f) return Mathf::Clamp(1.0f - dist / hi, 0.0f, 1.0f);
        return Mathf::Clamp(1.0f - std::log(dist / lo) / std::log(hi / lo), 0.0f, 1.0f);
    }
    case AudioRolloff::Linear:
    default: {
        const float span = hi - lo;
        return span > 1e-4f ? Mathf::Clamp(1.0f - (dist - lo) / span, 0.0f, 1.0f) : 0.0f;
    }
    }
}

void AudioMixer::PanGains(float pan, float& left, float& right) {
    pan = Mathf::Clamp(pan, -1.0f, 1.0f);
    left  = pan > 0.0f ? 1.0f - pan : 1.0f;
    right = pan < 0.0f ? 1.0f + pan : 1.0f;
}

namespace {

// Linear interpolation at a fractional sample position.
inline float SampleAt(const std::vector<float>& s, double pos) {
    const std::size_t i = static_cast<std::size_t>(pos);
    if (i + 1 >= s.size()) return s[i < s.size() ? i : s.size() - 1];
    const float t = static_cast<float>(pos - static_cast<double>(i));
    return s[i] + (s[i + 1] - s[i]) * t;
}

// Adds one voice into the mono buffer `out`. Returns false when the voice ran
// out (and does not loop).
bool MixVoice(const AudioClip& clip, double& pos, bool loop, float step, float gain,
              float* out, int frames) {
    const auto& s = clip.samples;
    const double len = static_cast<double>(s.size());
    for (int i = 0; i < frames; ++i) {
        if (pos >= len) {
            if (!loop) return false;
            pos = std::fmod(pos, len);
        }
        out[i] += SampleAt(s, pos) * gain;
        pos += step;
    }
    return true;
}

// True when a source would make sound this buffer.
bool Sounding(const AudioSource& s, bool playing, bool shots) {
    return s.enabled && s.gameObject && s.gameObject->active && (playing || shots);
}

} // namespace

int AudioMixer::RenderStereo(Scene& scene, float* out, int frames) {
    for (int i = 0; i < 2 * frames; ++i) out[i] = 0.0f;
    const int outRate = sampleRate > 0 ? sampleRate : 44100;
    const float dt = (float)frames / (float)outRate;
    const Vec3 right = listenerRight.SqrMagnitude() > 1e-8f ? listenerRight.Normalized() : Vec3::Zero;
    const auto sources = scene.FindObjectsOfType<AudioSource>();

    // Ducking: while anything in the Voice group plays, the Music group dips to
    // duckLevel (attack), then comes back up (release). Ramped across the buffer.
    bool voiceActive = false;
    if (duckMusicUnderVoice)
        for (AudioSource* src : sources)
            if (src->group == AudioGroup::Voice && Sounding(*src, src->m_playing, !src->m_shots.empty())) { voiceActive = true; break; }
    const float duckFrom = duckGain;
    const float duckTarget = (duckMusicUnderVoice && voiceActive) ? Mathf::Clamp(duckLevel, 0.0f, 1.0f) : 1.0f;
    const float duckTime = duckTarget < duckGain ? duckAttack : duckRelease;
    if (duckTime <= 0.0f) duckGain = duckTarget;
    else {
        const float stepD = dt / duckTime;
        duckGain = duckGain < duckTarget ? std::min(duckTarget, duckGain + stepD) : std::max(duckTarget, duckGain - stepD);
    }

    std::vector<float> mono(static_cast<std::size_t>(frames));
    int mixed = 0;
    for (AudioSource* src : sources) {
        if (!Sounding(*src, src->m_playing, !src->m_shots.empty())) continue;

        float vol = src->volume * GroupGain(src->group);
        float pan = src->pan;
        if (src->spatial && src->transform) {
            const Vec3 to = src->transform->Position() - listener;
            const float dist = to.Magnitude();
            vol *= Attenuation(*src, dist);
            // Pan toward the side the source is on, at most kSpatialPan, so the
            // far ear still hears it; straight ahead or on top stays centred.
            if (dist > 1e-4f && right.SqrMagnitude() > 0.0f)
                pan += kSpatialPan * Vec3::Dot(to / dist, right);
        }
        float gl = 1.0f, gr = 1.0f;
        PanGains(pan, gl, gr);
        const float pitch = src->pitch > 0.01f ? src->pitch : 0.01f;

        std::fill(mono.begin(), mono.end(), 0.0f);
        bool any = false;
        if (src->m_playing) {
            const AudioClip& c = src->clip;
            if (c.samples.empty()) {
                src->m_playing = false;
            } else {
                any = true;
                const float step = pitch * (c.sampleRate > 0 ? (float)c.sampleRate / outRate : 1.0f);
                if (!MixVoice(c, src->m_pos, src->loop, step, 1.0f, mono.data(), frames)) {
                    src->m_playing = false;
                    src->m_pos = 0.0;
                }
            }
        }
        for (std::size_t k = 0; k < src->m_shots.size();) {
            auto& sh = src->m_shots[k];
            any = true;
            const float step = pitch * (sh.clip->sampleRate > 0 ? (float)sh.clip->sampleRate / outRate : 1.0f);
            if (!MixVoice(*sh.clip, sh.pos, false, step, sh.volume, mono.data(), frames))
                src->m_shots.erase(src->m_shots.begin() + static_cast<std::ptrdiff_t>(k));
            else
                ++k;
        }
        if (!any) continue;
        ++mixed;

        // Muffle: a one-pole low-pass at the source's cutoff.
        if (src->lowPass < AudioSource::kLowPassOff) {
            const float fc = std::max(10.0f, src->lowPass);
            const float a = 1.0f - std::exp(-2.0f * Mathf::PI * fc / (float)outRate);
            float y = src->m_lowPassState;
            for (int i = 0; i < frames; ++i) { y += a * (mono[i] - y); mono[i] = y; }
            src->m_lowPassState = y;
        }

        // Fade and duck change smoothly sample by sample, never in a step.
        const bool ducked = src->group == AudioGroup::Music && (duckFrom < 1.0f || duckGain < 1.0f);
        const float fadeStep = src->m_fadeRate / (float)outRate;
        for (int i = 0; i < frames; ++i) {
            if (src->m_fade != src->m_fadeTarget) {
                src->m_fade = src->m_fade < src->m_fadeTarget
                    ? std::min(src->m_fadeTarget, src->m_fade + fadeStep)
                    : std::max(src->m_fadeTarget, src->m_fade - fadeStep);
            }
            float g = vol * src->m_fade;
            if (ducked) g *= duckFrom + (duckGain - duckFrom) * ((float)i / (float)frames);
            out[2 * i]     += mono[i] * g * gl;
            out[2 * i + 1] += mono[i] * g * gr;
        }
        if (src->m_fadeStop && src->m_fade <= 0.0f) src->Stop();
    }

    const float gain = muted ? 0.0f : masterVolume;
    for (int i = 0; i < 2 * frames; ++i) out[i] = Mathf::Clamp(out[i] * gain, -1.0f, 1.0f);
    return mixed;
}

int AudioMixer::Render(Scene& scene, float* out, int frames) {
    std::vector<float> st(static_cast<std::size_t>(frames) * 2);
    const int mixed = RenderStereo(scene, st.data(), frames);
    for (int i = 0; i < frames; ++i) out[i] = 0.5f * (st[2 * i] + st[2 * i + 1]);
    return mixed;
}

} // namespace okay
