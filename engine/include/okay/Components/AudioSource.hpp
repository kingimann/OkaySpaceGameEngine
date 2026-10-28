#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Audio/AudioClip.hpp"
#include <memory>
#include <string>
#include <vector>

namespace okay {

/// Mixer group a source plays through. Each group has its own volume and mute
/// on AudioMixer, under the master volume (settings menus: music / effects / ...).
enum class AudioGroup { SFX = 0, Music, UI, Voice, Count };

/// How a spatial source fades between minDistance and maxDistance.
enum class AudioRolloff {
    Linear = 0,   ///< straight line from full (min) to silent (max)
    Inverse,      ///< min / distance, like real sound; faded to silence near max
    Logarithmic,  ///< loud close up, long quiet tail; silent at max
};

/// Plays an AudioClip. The AudioMixer reads active sources and advances their
/// playhead while mixing.
class AudioSource : public Behaviour {
public:
    AudioClip clip;
    /// Optional WAV file to load into `clip` (the windowed runtime loads it,
    /// resampled to the output rate). Empty means use whatever `clip` holds.
    std::string clipPath;
    float volume = 1.0f;
    /// Playback speed and pitch together: 2 is an octave up and twice as fast.
    float pitch = 1.0f;
    /// Stereo position: -1 left, 0 centre, 1 right. Adds to the 3D pan.
    float pan = 0.0f;
    AudioGroup group = AudioGroup::SFX;
    bool  loop = false;
    bool  playOnAwake = false;
    /// 3D positional audio: when true, the mixer fades the source by its
    /// distance to the listener (AudioMixer::SetListener) and pans it to the
    /// side it is on. Full volume within minDistance, silent past maxDistance.
    bool  spatial = false;
    float minDistance = 1.0f;
    float maxDistance = 25.0f;
    AudioRolloff rolloff = AudioRolloff::Linear;
    /// Low-pass cutoff in Hz: lower is more muffled (behind a wall, underwater).
    /// kLowPassOff or above leaves the sound untouched.
    float lowPass = kLowPassOff;
    static constexpr float kLowPassOff = 22000.0f;

    bool  IsPlaying() const { return m_playing; }
    /// Seconds left in the clip at the current pitch (0 when stopped).
    float RemainingSeconds() const {
        if (!m_playing || clip.sampleRate <= 0 || pitch <= 0.0f) return 0.0f;
        const double left = static_cast<double>(clip.samples.size()) - m_pos;
        return left > 0.0 ? static_cast<float>(left / clip.sampleRate / pitch) : 0.0f;
    }
    std::size_t Playhead() const { return static_cast<std::size_t>(m_pos); }
    /// One-shots still sounding (PlayOneShot), not counting the main clip.
    std::size_t ActiveOneShots() const { return m_shots.size(); }

    void Play()  { m_playing = true; m_pos = 0.0; m_fade = 1.0f; m_fadeTarget = 1.0f; m_fadeStop = false; }
    void Stop()  { m_playing = false; m_pos = 0.0; m_shots.clear(); m_fade = 1.0f; m_fadeTarget = 1.0f; m_fadeStop = false; }

    /// Start from silence and rise to full volume over `seconds` (restarts the
    /// clip unless it is already playing).
    void FadeIn(float seconds) {
        if (!m_playing) { m_playing = true; m_pos = 0.0; }
        m_fade = 0.0f; m_fadeStop = false;
        FadeTo(1.0f, seconds);
    }
    /// Fall to silence over `seconds`, then stop (or keep playing silently when
    /// `stopAtEnd` is false, e.g. to fade back in later).
    void FadeOut(float seconds, bool stopAtEnd = true) { m_fadeStop = stopAtEnd; FadeTo(0.0f, seconds); }
    /// Move the fade level (a multiplier on volume, 0..1) to `level` over `seconds`.
    void FadeTo(float level, float seconds) {
        m_fadeTarget = level < 0.0f ? 0.0f : (level > 1.0f ? 1.0f : level);
        const float span = m_fadeTarget > m_fade ? m_fadeTarget - m_fade : m_fade - m_fadeTarget;
        m_fadeRate = seconds > 0.0f ? span / seconds : 0.0f;
        if (seconds <= 0.0f) m_fade = m_fadeTarget;
    }
    float FadeLevel() const { return m_fade; }
    bool  IsFading() const { return m_fade != m_fadeTarget; }
    void Pause() { m_playing = false; }
    void Resume(){ m_playing = true; }

    /// Play `shot` once on top of whatever this source is playing, with this
    /// source's group, pitch, pan and 3D settings. Overlapping calls each get
    /// their own voice (footsteps, gunshots); the oldest is dropped past
    /// kMaxOneShots.
    void PlayOneShot(const AudioClip& shot, float volumeScale = 1.0f) {
        if (shot.samples.empty()) return;
        if (m_shots.size() >= kMaxOneShots) m_shots.erase(m_shots.begin());
        m_shots.push_back({std::make_shared<const AudioClip>(shot), 0.0, volumeScale});
    }
    /// Play this source's own clip as a one-shot.
    void PlayOneShot(float volumeScale = 1.0f) { PlayOneShot(clip, volumeScale); }

    void Awake() override { if (playOnAwake) Play(); }

    static constexpr std::size_t kMaxOneShots = 16;

private:
    friend struct AudioMixer;
    struct Shot { std::shared_ptr<const AudioClip> clip; double pos; float volume; };
    bool m_playing = false;
    double m_pos = 0.0;   // in clip samples; fractional when pitched or resampled
    float m_fade = 1.0f, m_fadeTarget = 1.0f, m_fadeRate = 0.0f;   // fade level, its target, change per second
    bool  m_fadeStop = false;   // stop once a fade reaches silence
    float m_lowPassState = 0.0f;   // one-pole filter memory
    std::vector<Shot> m_shots;
};

} // namespace okay
