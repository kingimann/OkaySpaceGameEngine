#pragma once
#include "okay/Math/Vec3.hpp"
#include "okay/Components/AudioSource.hpp"

namespace okay {

class Scene;

/// Mixes all active AudioSources in a scene. Backends (an SDL audio device)
/// call RenderStereo() to pull audio; tests call Render() for a mono mix.
struct AudioMixer {
    /// Interleaved stereo (L, R, L, R, ...) into `out`, `frames` frames long,
    /// clamped to [-1, 1]; advances each source's playhead (except sources held by a
    /// game pause, see pausesWithGame). Returns the number
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
    /// Silences output while the window is in the background (the host sets it);
    /// unlike `muted` it is not a player setting and is never saved.
    static inline bool  focusMuted = false;

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
    /// Which groups stop while the game is paused (Game::SetPaused, a pause hold, or
    /// Time timeScale 0). A paused source makes no sound, and its playhead, one-shots
    /// and fade wait; when the game resumes it carries on from the same sample. By
    /// default SFX and Voice pause, and Music and UI keep playing (menu music and
    /// button clicks over the Pause Menu). A game can change any entry. Slow motion
    /// (a scale above 0) and a hit stop do not pause or slow sound.
    static inline bool pausesWithGame[(int)AudioGroup::Count] = {true, false, false, true};
    static bool PausesWithGame(AudioGroup g) {
        const int i = (int)g;
        return i >= 0 && i < (int)AudioGroup::Count && pausesWithGame[i];
    }
    /// True while the game counts as paused for sound: Game paused (Time frozen) or a
    /// time scale of 0. A hit stop is not a pause.
    static bool GamePausedForAudio();
    /// "music", "SFX", ... (any case) -> group; false when the name is unknown.
    static bool GroupFromName(const char* name, AudioGroup& out);

    /// The player's volume settings (master, each group, mutes) to and from
    /// Prefs, under "audio.*" keys. The player loads them at start and saves
    /// them with the rest of its prefs; keys that aren't there keep the default.
    static void LoadSettings();
    static void SaveSettings();

    /// Loudest output sample per channel in the last RenderStereo (before the
    /// clamp), for level meters.
    static inline float peak[2] = {0.0f, 0.0f};

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
    /// Motion for doppler is the change in this position (and each source's)
    /// between mixes. A jump faster than most of the speed of sound is ignored.
    static inline Vec3 listener = Vec3::Zero;
    /// How far a 3D source to the listener's side pans (1 would silence the far ear).
    static constexpr float kSpatialPan = 0.7f;
    static inline Vec3 listenerRight = Vec3::Zero;
    static void SetListener(const Vec3& p) { listener = p; listenerRight = Vec3::Zero; }
    static void SetListener(const Vec3& p, const Vec3& right) { listener = p; listenerRight = right; }

    /// Drop the reverb tail and the remembered listener motion. The editor calls
    /// this when Play stops so the next session does not inherit a frozen tail.
    /// A new scene does the same on its first mix.
    static void ResetSpatial();

    /// Distance fade [0..1] for one source at `distance` from the listener.
    static float Attenuation(const AudioSource& src, float distance);
    /// Left/right gains for a pan in [-1, 1]. The centre is 1 on both sides,
    /// so a centred mono clip comes out unchanged on each channel.
    static void PanGains(float pan, float& left, float& right);
};

} // namespace okay
