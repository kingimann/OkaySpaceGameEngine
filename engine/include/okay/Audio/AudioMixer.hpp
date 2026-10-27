#pragma once
#include "okay/Math/Vec3.hpp"
#include "okay/Components/AudioSource.hpp"

namespace okay {

class Scene;

/// Mixes all active AudioSources in a scene. Backends (an SDL audio device)
/// call RenderStereo() to pull audio; tests call Render() for a mono mix.
struct AudioMixer {
    /// Interleaved stereo (L, R, L, R, ...) into `out`, `frames` frames long,
    /// clamped to [-1, 1]; advances each source's playhead. Returns the number
    /// of sources mixed. The mix is scaled by the source's group volume and by
    /// `masterVolume` (and silenced when `muted`).
    static int RenderStereo(Scene& scene, float* out, int frames);
    /// Mono mix (the two channels averaged), `frames` samples into `out`.
    static int Render(Scene& scene, float* out, int frames);

    /// Output rate the device runs at. Clips at another rate play at the right
    /// speed and pitch (the mixer steps through them at clipRate / sampleRate).
    static inline int sampleRate = 44100;

    /// Global output level [0..1] and mute, for settings menus.
    static inline float masterVolume = 1.0f;
    static inline bool  muted = false;

    /// Per-group level [0..1] and mute, under the master (AudioGroup order).
    static inline float groupVolume[(int)AudioGroup::Count] = {1.0f, 1.0f, 1.0f, 1.0f};
    static inline bool  groupMuted[(int)AudioGroup::Count]  = {false, false, false, false};
    static float GroupGain(AudioGroup g) {
        const int i = (int)g;
        if (i < 0 || i >= (int)AudioGroup::Count || groupMuted[i]) return 0.0f;
        return groupVolume[i];
    }
    static const char* GroupName(AudioGroup g) {
        static const char* names[] = {"SFX", "Music", "UI", "Voice"};
        const int i = (int)g;
        return (i >= 0 && i < (int)AudioGroup::Count) ? names[i] : "SFX";
    }
    /// "music", "SFX", ... (any case) -> group; false when the name is unknown.
    static bool GroupFromName(const char* name, AudioGroup& out);

    /// Ducking: while a Voice-group source plays, the Music group dips to
    /// duckLevel over duckAttack seconds and comes back over duckRelease.
    static inline bool  duckMusicUnderVoice = false;
    static inline float duckLevel = 0.35f;
    static inline float duckAttack = 0.15f;
    static inline float duckRelease = 0.6f;
    /// The Music group's current duck multiplier (1 = not ducked).
    static inline float duckGain = 1.0f;

    /// World-space listener for 3D (spatial) sources: set it each frame to the
    /// camera. `right` is the listener's right-hand direction; sources on that
    /// side pan right. Without it (zero), 3D sources are only faded, not panned.
    static inline Vec3 listener = Vec3::Zero;
    /// How far a 3D source to the listener's side pans (1 would silence the far ear).
    static constexpr float kSpatialPan = 0.7f;
    static inline Vec3 listenerRight = Vec3::Zero;
    static void SetListener(const Vec3& p) { listener = p; listenerRight = Vec3::Zero; }
    static void SetListener(const Vec3& p, const Vec3& right) { listener = p; listenerRight = right; }

    /// Distance fade [0..1] for one source at `distance` from the listener.
    static float Attenuation(const AudioSource& src, float distance);
    /// Left/right gains for a pan in [-1, 1]. The centre is 1 on both sides,
    /// so a centred mono clip comes out unchanged on each channel.
    static void PanGains(float pan, float& left, float& right);
};

} // namespace okay
