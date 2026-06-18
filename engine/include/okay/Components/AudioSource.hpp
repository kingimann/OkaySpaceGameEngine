#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Audio/AudioClip.hpp"
#include <string>

namespace okay {

/// Plays an AudioClip. The AudioMixer reads active sources and advances their
/// playhead while mixing — analogous to Unity's AudioSource.
class AudioSource : public Behaviour {
public:
    AudioClip clip;
    /// Optional WAV file to load into `clip` (the windowed runtime loads it,
    /// resampled to the output rate). Empty means use whatever `clip` holds.
    std::string clipPath;
    float volume = 1.0f;
    bool  loop = false;
    bool  playOnAwake = false;

    bool  IsPlaying() const { return m_playing; }
    std::size_t Playhead() const { return m_playhead; }

    void Play()  { m_playing = true; m_playhead = 0; }
    void Stop()  { m_playing = false; m_playhead = 0; }
    void Pause() { m_playing = false; }
    void Resume(){ m_playing = true; }

    void Awake() override { if (playOnAwake) Play(); }

private:
    friend struct AudioMixer;
    bool m_playing = false;
    std::size_t m_playhead = 0;
};

} // namespace okay
