#pragma once
#include "okay/Audio/AudioLoad.hpp"
#include "okay/Audio/AudioMixer.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Core/Random.hpp"
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace okay {

/// Background music: a playlist of audio files (WAV, OGG, MP3) on the Music
/// group, crossfading from one track to the next. It plays through two child
/// AudioSources it creates ("Music A" / "Music B") so the outgoing track can
/// fade out while the next fades in.
class MusicPlayer : public Behaviour {
public:
    std::vector<std::string> tracks;   ///< file paths, played in order (or shuffled)
    bool  playOnStart = true;
    bool  shuffle = false;
    bool  loopPlaylist = true;         ///< after the last track, start over
    float crossfade = 2.0f;            ///< seconds two tracks overlap
    float volume = 1.0f;

    void Start() override {
        m_rng.Seed(0x6d757369636bULL ^ reinterpret_cast<std::uintptr_t>(this));   // each player shuffles differently
        EnsureVoices();
        if (playOnStart && !tracks.empty()) Play(shuffle ? PickShuffled(-1) : 0);
    }

    void Update(float) override {
        AudioSource* cur = Current();
        if (!cur || m_index < 0) return;
        cur->volume = volume;
        if (AudioSource* other = Other()) other->volume = volume;
        if (!cur->IsPlaying() || m_stopping) return;
        // Start the next track early enough that the two overlap by `crossfade`.
        const float lead = crossfade > 0.0f ? crossfade : 0.0f;
        if (cur->RemainingSeconds() <= lead) {
            const int next = NextIndex();
            if (next >= 0) Play(next);
            else cur->FadeOut(cur->RemainingSeconds());
        }
    }

    /// Crossfade to track `index` (no fade when nothing is playing yet).
    bool Play(int index) {
        if (index < 0 || index >= (int)tracks.size()) return false;
        EnsureVoices();
        auto clip = LoadAudioFile(tracks[(std::size_t)index]);
        if (!clip) return false;
        AudioSource* out = Current();
        const bool wasPlaying = out && out->IsPlaying() && m_index >= 0;
        m_cur = 1 - m_cur;
        AudioSource* in = Current();
        if (!in) return false;
        in->clip = *clip;
        in->loop = tracks.size() == 1 && loopPlaylist;
        in->volume = volume;
        if (wasPlaying && crossfade > 0.0f) {
            in->FadeIn(crossfade);
            out->FadeOut(crossfade);
        } else {
            in->Play();
            if (out) out->Stop();
        }
        m_index = index;
        m_stopping = false;
        return true;
    }
    bool Play(const std::string& track) {
        for (std::size_t i = 0; i < tracks.size(); ++i)
            if (tracks[i] == track) return Play((int)i);
        tracks.push_back(track);
        return Play((int)tracks.size() - 1);
    }
    void Next()     { const int n = NextIndex(true); if (n >= 0) Play(n); }
    void Previous() { if (!tracks.empty()) Play(m_index <= 0 ? (int)tracks.size() - 1 : m_index - 1); }
    /// Fade the music out over `seconds` (0 = cut) and stop.
    void Stop(float seconds = 1.0f) {
        m_stopping = true;
        for (AudioSource* s : {Current(), Other()})
            if (s) { if (seconds > 0.0f) s->FadeOut(seconds); else s->Stop(); }
    }
    void Pause()  { for (AudioSource* s : {Current(), Other()}) if (s) s->Pause(); }
    void Resume() { for (AudioSource* s : {Current(), Other()}) if (s && s->clip.samples.size()) s->Resume(); }

    bool IsPlaying() const {
        const AudioSource* c = m_voice[m_cur];
        return c && c->IsPlaying() && !m_stopping;
    }
    int CurrentIndex() const { return m_index; }
    std::string CurrentTrack() const {
        return (m_index >= 0 && m_index < (int)tracks.size()) ? tracks[(std::size_t)m_index] : std::string{};
    }

private:
    AudioSource* Current() const { return m_voice[m_cur]; }
    AudioSource* Other() const   { return m_voice[1 - m_cur]; }

    void EnsureVoices() {
        if (m_voice[0] && m_voice[1]) return;
        Scene* scene = gameObject ? gameObject->scene() : nullptr;
        if (!scene) return;
        for (int i = 0; i < 2; ++i) {
            if (m_voice[i]) continue;
            GameObject* go = scene->CreateGameObject(i == 0 ? "Music A" : "Music B");
            go->transform->SetParent(transform, false);
            m_voice[i] = go->AddComponent<AudioSource>();
            m_voice[i]->group = AudioGroup::Music;
        }
    }

    int NextIndex(bool forced = false) {
        const int n = (int)tracks.size();
        if (n == 0) return -1;
        if (shuffle && n > 1) return PickShuffled(m_index);
        if (m_index + 1 < n) return m_index + 1;
        return (loopPlaylist || forced) ? 0 : -1;
    }
    int PickShuffled(int avoid) {
        const int n = (int)tracks.size();
        if (n <= 1) return n - 1;
        int i = m_rng.Range(0, n);
        if (i == avoid) i = (i + 1) % n;
        return i;
    }

    AudioSource* m_voice[2] = {nullptr, nullptr};
    int  m_cur = 0;
    int  m_index = -1;
    bool m_stopping = false;
    Random m_rng{1};
};

} // namespace okay
