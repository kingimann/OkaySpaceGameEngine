#include "okay/Audio/AudioMixer.hpp"
#include "okay/Audio/SpatialFx.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/Volume.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Core/Prefs.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
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

namespace {
std::string GroupKey(int i) {
    std::string k = std::string("audio.") + AudioMixer::GroupName((AudioGroup)i);
    for (char& c : k) c = (char)std::tolower((unsigned char)c);
    return k;
}
} // namespace

void AudioMixer::LoadSettings() {
    masterVolume = Mathf::Clamp(Prefs::GetFloat("audio.master", masterVolume), 0.0f, 1.0f);
    muted = Prefs::GetInt("audio.muted", muted ? 1 : 0) != 0;
    for (int i = 0; i < (int)AudioGroup::Count; ++i) {
        const std::string k = GroupKey(i);
        groupVolume[i] = Mathf::Clamp(Prefs::GetFloat(k, groupVolume[i]), 0.0f, 1.0f);
        groupMuted[i] = Prefs::GetInt(k + ".muted", groupMuted[i] ? 1 : 0) != 0;
    }
}

void AudioMixer::SaveSettings() {
    Prefs::SetFloat("audio.master", masterVolume);
    Prefs::SetInt("audio.muted", muted ? 1 : 0);
    for (int i = 0; i < (int)AudioGroup::Count; ++i) {
        const std::string k = GroupKey(i);
        Prefs::SetFloat(k, groupVolume[i]);
        Prefs::SetInt(k + ".muted", groupMuted[i] ? 1 : 0);
    }
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

// The two channels of a clip at a fractional frame position, linearly interpolated
// (a mono clip gives the same value on both).
inline void FrameAt(const AudioClip& c, double pos, std::size_t frames, float& l, float& r) {
    const std::size_t i = static_cast<std::size_t>(pos);
    const std::size_t ia = i < frames ? i : frames - 1;
    const std::size_t ib = i + 1 < frames ? i + 1 : ia;
    const float t = i + 1 >= frames ? 0.0f : static_cast<float>(pos - static_cast<double>(i));
    const float* s = c.samples.data();
    if (c.channels >= 2) {
        l = s[2 * ia] + (s[2 * ib] - s[2 * ia]) * t;
        r = s[2 * ia + 1] + (s[2 * ib + 1] - s[2 * ia + 1]) * t;
    } else {
        l = r = s[ia] + (s[ib] - s[ia]) * t;
    }
}

// Adds one in-memory voice into the L/R buffers with `gain`. Returns false when
// the voice ran out (and does not loop).
bool MixVoice(const AudioClip& clip, double& pos, bool loop, float step, float gain,
              float* L, float* R, int frames) {
    const std::size_t n = clip.FrameCount();
    const double len = static_cast<double>(n);
    for (int i = 0; i < frames; ++i) {
        if (pos >= len) {
            if (!loop) return false;
            pos = std::fmod(pos, len);
        }
        float l, r;
        FrameAt(clip, pos, n, l, r);
        L[i] += l * gain;
        R[i] += r * gain;
        pos += step;
    }
    return true;
}

// Adds a streaming voice. `pos` counts frames on the stream's own timeline
// (it keeps counting through loops). The stream is pumped first (the decode
// step), and again when a buffer reads further than the ring held; if the data
// still isn't there the rest of the buffer is silence and the playhead waits
// (an underrun), so a slow disk drops out instead of skipping or crashing.
// Returns false when a non-looping stream has played to its end.
bool MixStream(AudioStream& st, double& pos, bool loop, float step, float* L, float* R, int frames) {
    st.loop = loop;
    st.Release(static_cast<std::uint64_t>(pos));
    st.Pump();
    const bool stereo = st.Channels() >= 2;
    int refills = 0;
    float a[2] = {0, 0}, b[2] = {0, 0};
    for (int i = 0; i < frames; ++i) {
        const std::uint64_t i0 = static_cast<std::uint64_t>(pos);
        const bool endKnown = st.Ended();
        if (!st.Has(i0 + 1) && !endKnown && refills < 8) {
            st.Release(i0);
            st.Pump();
            ++refills;
        }
        if (!st.Has(i0)) {
            if (st.Ended()) return false;
            st.NoteUnderrun();
            return true;            // silence for the rest of this buffer, playhead stays
        }
        if (!st.Has(i0 + 1) && !st.Ended()) {
            // The next frame (for interpolation) is not decoded yet: wait for it rather than
            // hold the value, so the join between chunks is exact.
            st.NoteUnderrun();
            return true;
        }
        st.Frame(i0, a);
        if (!st.Frame(i0 + 1, b)) { b[0] = a[0]; b[1] = a[1]; }
        const float t = static_cast<float>(pos - static_cast<double>(i0));
        if (stereo) {
            L[i] += a[0] + (b[0] - a[0]) * t;
            R[i] += a[1] + (b[1] - a[1]) * t;
        } else {
            const float v = a[0] + (b[0] - a[0]) * t;
            L[i] += v;
            R[i] += v;
        }
        pos += step;
    }
    return true;
}

// True when a source would make sound this buffer.
bool Sounding(const AudioSource& s, bool playing, bool shots) {
    return s.enabled && s.gameObject && s.gameObject->active && (playing || shots);
}

// A solid collider between the listener and the source. Triggers do not count,
// nor does the source's own object, nor a hit sitting on the listener.
bool RayOccludes(Scene& scene, const Vec3& from, const Vec3& to, GameObject* source) {
    const Vec3 delta = to - from;
    const float dist = delta.Magnitude();
    if (!(dist > 0.1f)) return false;
    const Vec3 dir = delta * (1.0f / dist);
    const RaycastHit3D hit = scene.physics3D().Raycast(scene, from, dir, dist, source, Physics3D::kAllLayers, false);
    return hit.hit && hit.distance > 0.05f && hit.distance < dist - 0.05f;
}

struct MotionMemory {
    Vec3 listenerPos{};
    bool hasListener = false;
    std::uint64_t scene = 0;
};
MotionMemory& Motion() { static MotionMemory m; return m; }
SchroederReverb& ReverbBus() { static SchroederReverb bus; return bus; }

// The listener's reverb: a weighted blend of every Reverb volume that reaches
// it. Wet is scaled by each zone's weight, so the edge fades to dry. Room,
// damp and width are a weighted average. No zone means a dry (inactive) result.
struct ZoneReverb {
    ReverbSettings settings{};
    bool active = false;
    ZoneReverb() { settings.wet = 0.0f; }
};
ZoneReverb ListenerReverb(Scene& scene) {
    ZoneReverb zone;
    float wsum = 0.0f, room = 0.0f, damp = 0.0f, wet = 0.0f, width = 0.0f;
    for (Volume* v : scene.FindObjectsOfType<Volume>()) {
        if (!v || v->kind != Volume::Kind::Reverb || !v->enabled) continue;
        if (!v->gameObject || !v->gameObject->ActiveInHierarchy() || !v->transform) continue;
        const float w = v->WeightAt(AudioMixer::listener);
        if (!(w > 0.0f)) continue;
        int preset = v->reverbPreset;
        if (preset < 0 || preset >= (int)ReverbPreset::Count) preset = 0;
        const ReverbSettings p = PresetSettings((ReverbPreset)preset);
        float amount = v->reverbAmount;
        if (amount < 0.0f) amount = 0.0f;
        if (amount > 1.0f) amount = 1.0f;
        room += p.room * w;
        damp += p.damp * w;
        width += p.width * w;
        wet += p.wet * amount * w;
        wsum += w;
    }
    if (!(wsum > 0.0f)) return zone;
    zone.active = true;
    zone.settings.room = room / wsum;
    zone.settings.damp = damp / wsum;
    zone.settings.width = width / wsum;
    zone.settings.wet = wet > 1.0f ? 1.0f : wet;
    return zone;
}

Vec3 MotionVelocity(const Vec3& now, const Vec3& then, float dt) {
    if (!(dt > 1.0e-6f)) return Vec3::Zero;
    const Vec3 vel = (now - then) * (1.0f / dt);
    const float limit = 0.9f * kSpeedOfSound;
    if (vel.SqrMagnitude() > limit * limit) return Vec3::Zero;   // a teleport, not motion
    return vel;
}

} // namespace

void AudioMixer::ResetSpatial() {
    ReverbBus().Clear();
    Motion() = MotionMemory{};
}

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

    if (scene.Uid() != Motion().scene) {
        Motion().scene = scene.Uid();
        Motion().hasListener = false;
        ReverbBus().Clear();
    }
    const float motionDt = (float)frames / (float)outRate;
    Vec3 listenerVel = Vec3::Zero;
    if (Motion().hasListener) listenerVel = MotionVelocity(listener, Motion().listenerPos, motionDt);
    Motion().listenerPos = listener;
    Motion().hasListener = true;

    const ZoneReverb zone = ListenerReverb(scene);
    const bool feedReverb = zone.active && zone.settings.wet > 1.0e-5f;
    const bool runReverb = feedReverb || !ReverbBus().Silent();
    std::vector<float> send;
    if (feedReverb) send.assign(static_cast<std::size_t>(frames), 0.0f);

    std::vector<float> bufL(static_cast<std::size_t>(frames)), bufR(static_cast<std::size_t>(frames));
    int mixed = 0;
    for (AudioSource* src : sources) {
        if (!Sounding(*src, src->m_playing, !src->m_shots.empty())) continue;

        float vol = src->volume * GroupGain(src->group);
        float pan = src->pan;
        float dist = 0.0f;
        float doppler = 1.0f;
        if (src->spatial && src->transform) {
            const Vec3 pos = src->transform->Position();
            const Vec3 to = pos - listener;
            dist = to.Magnitude();
            vol *= Attenuation(*src, dist);
            // Pan toward the side the source is on, at most kSpatialPan, so the
            // far ear still hears it; straight ahead or on top stays centred.
            if (dist > 1e-4f && right.SqrMagnitude() > 0.0f)
                pan += kSpatialPan * Vec3::Dot(to / dist, right);

            const Vec3 srcVel = src->m_hasSpatialPos ? MotionVelocity(pos, src->m_spatialPos, motionDt) : Vec3::Zero;
            src->m_spatialPos = pos;
            src->m_hasSpatialPos = true;
            if (src->doppler && dist > 1e-4f) {
                float scale = src->dopplerScale;
                if (!(scale >= 0.0f)) scale = 0.0f;
                doppler = DopplerRatio(listenerVel, srcVel, to / dist, scale);
            }
            if (src->occlude && dist > 0.05f) {
                const bool blocked = RayOccludes(scene, listener, pos, src->gameObject);
                const float target = blocked ? 1.0f : 0.0f;
                const float follow = 1.0f - std::exp(-motionDt / 0.05f);
                src->m_occlusion += (target - src->m_occlusion) * follow;
                const float amt = Mathf::Clamp01(src->m_occlusion);
                const OcclusionTone full = OcclusionAttenuation(true, src->occlusionVolume, src->occlusionLowPass);
                vol *= 1.0f + (full.volume - 1.0f) * amt;
            } else {
                src->m_occlusion = 0.0f;
            }
        }
        float gl = 1.0f, gr = 1.0f;
        PanGains(pan, gl, gr);
        const float pitch = (src->pitch > 0.01f ? src->pitch : 0.01f) * doppler;

        std::fill(bufL.begin(), bufL.end(), 0.0f);
        std::fill(bufR.begin(), bufR.end(), 0.0f);
        bool any = false;
        if (src->m_playing) {
            const AudioClip& c = src->clip;
            if (!c.samples.empty()) {
                any = true;
                const float step = pitch * (c.sampleRate > 0 ? (float)c.sampleRate / outRate : 1.0f);
                if (!MixVoice(c, src->m_pos, src->loop, step, 1.0f, bufL.data(), bufR.data(), frames)) {
                    src->m_playing = false;
                    src->m_pos = 0.0;
                }
            } else if (src->m_stream) {
                any = true;
                AudioStream& st = *src->m_stream;
                const float step = pitch * (st.SampleRate() > 0 ? (float)st.SampleRate() / outRate : 1.0f);
                if (!MixStream(st, src->m_pos, src->loop, step, bufL.data(), bufR.data(), frames)) {
                    src->m_playing = false;
                    src->m_pos = 0.0;
                }
            } else {
                src->m_playing = false;
            }
        }
        for (std::size_t k = 0; k < src->m_shots.size();) {
            auto& sh = src->m_shots[k];
            any = true;
            const float step = pitch * (sh.clip->sampleRate > 0 ? (float)sh.clip->sampleRate / outRate : 1.0f);
            if (!MixVoice(*sh.clip, sh.pos, false, step, sh.volume, bufL.data(), bufR.data(), frames))
                src->m_shots.erase(src->m_shots.begin() + static_cast<std::ptrdiff_t>(k));
            else
                ++k;
        }
        if (!any) continue;
        ++mixed;

        // A 3D source is a point in space: whatever channels it has are averaged to one
        // before it is placed by distance and direction. (2D stereo keeps left and right,
        // and `pan` is a balance between them.)
        if (src->spatial)
            for (int i = 0; i < frames; ++i) bufL[i] = bufR[i] = 0.5f * (bufL[i] + bufR[i]);

        // Muffle: a one-pole low-pass. The manual cutoff, the distance curve and
        // occlusion each offer a cutoff; the lowest one wins. With every effect
        // off this is the source's own cutoff, unchanged.
        float cutoff = src->lowPass;
        if (src->spatial && src->distanceLowPass)
            cutoff = std::min(cutoff, DistanceLowPassHz(dist, src->minDistance, src->maxDistance,
                                                       src->distanceLowPassHz, true));
        if (src->spatial && src->occlude && src->m_occlusion > 1.0e-4f) {
            const OcclusionTone full = OcclusionAttenuation(true, src->occlusionVolume, src->occlusionLowPass);
            const float occCut = Mathf::Lerp(AudioSource::kLowPassOff, full.lowPassHz, Mathf::Clamp01(src->m_occlusion));
            cutoff = std::min(cutoff, occCut);
        }
        if (cutoff < AudioSource::kLowPassOff) {
            const float fc = std::max(10.0f, cutoff);
            const float a = 1.0f - std::exp(-2.0f * Mathf::PI * fc / (float)outRate);
            float y = src->m_lowPassState, yr = src->m_lowPassStateR;
            for (int i = 0; i < frames; ++i) {
                y += a * (bufL[i] - y); bufL[i] = y;
                yr += a * (bufR[i] - yr); bufR[i] = yr;
            }
            src->m_lowPassState = y;
            src->m_lowPassStateR = yr;
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
            if (feedReverb && src->spatial) send[static_cast<std::size_t>(i)] += bufL[i] * g;
            out[2 * i]     += bufL[i] * g * gl;
            out[2 * i + 1] += bufR[i] * g * gr;
        }
        if (src->m_fadeStop && src->m_fade <= 0.0f) src->Stop();
    }

    // Reverb tail for 3D sources while the listener is inside a Reverb volume.
    // Outside, the bus keeps decaying but is not added, so a dry scene stays
    // exactly as it was. 2D sources are never sent.
    if (runReverb && frames > 0) {
        SchroederReverb& bus = ReverbBus();
        if (bus.Rate() != outRate) bus.Prepare(outRate);
        bus.BeginBlock();
        bus.Configure(zone.settings);
        for (int i = 0; i < frames; ++i) {
            const float in = (feedReverb && (std::size_t)i < send.size()) ? send[(std::size_t)i] : 0.0f;
            float l = 0.0f, r = 0.0f;
            bus.Process(in, l, r);
            if (feedReverb) {
                out[2 * i]     += l;
                out[2 * i + 1] += r;
            }
        }
    }

    const float gain = (muted || focusMuted) ? 0.0f : masterVolume;
    peak[0] = peak[1] = 0.0f;
    for (int i = 0; i < 2 * frames; ++i) {
        const float v = out[i] * gain;
        peak[i & 1] = std::max(peak[i & 1], std::fabs(v));
        out[i] = Mathf::Clamp(v, -1.0f, 1.0f);
    }
    return mixed;
}

int AudioMixer::Render(Scene& scene, float* out, int frames) {
    std::vector<float> st(static_cast<std::size_t>(frames) * 2);
    const int mixed = RenderStereo(scene, st.data(), frames);
    for (int i = 0; i < frames; ++i) out[i] = 0.5f * (st[2 * i] + st[2 * i + 1]);
    return mixed;
}

} // namespace okay
