// Pausing the game pauses game sounds (#888 finding 64).
//
// While Game::SetPaused(true) holds, or Time::timeScale is 0, sources in a group that
// "pauses with the game" (SFX and Voice by default) make no sound and their playhead
// and one-shots do not move. Music and UI keep playing (menu music, button clicks).
// When the game resumes they carry on from the same sample.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <vector>

using namespace okay;

namespace {

void ResetAll() {
    AudioMixer::sampleRate = 44100;
    AudioMixer::masterVolume = 1.0f;
    AudioMixer::muted = false;
    AudioMixer::focusMuted = false;
    AudioMixer::duckMusicUnderVoice = false;
    AudioMixer::duckGain = 1.0f;
    for (int i = 0; i < (int)AudioGroup::Count; ++i) {
        AudioMixer::groupVolume[i] = 1.0f;
        AudioMixer::groupMuted[i] = false;
    }
    AudioMixer::pausesWithGame[(int)AudioGroup::SFX] = true;
    AudioMixer::pausesWithGame[(int)AudioGroup::Music] = false;
    AudioMixer::pausesWithGame[(int)AudioGroup::UI] = false;
    AudioMixer::pausesWithGame[(int)AudioGroup::Voice] = true;
    AudioMixer::SetListener(Vec3::Zero);
    Game::Reset();
    Time::ResetForTesting();
}

AudioSource* MakeSource(Scene& s, const char* name, float value, int count, AudioGroup g) {
    auto* a = s.CreateGameObject(name)->AddComponent<AudioSource>();
    a->clip = AudioClip::Const(value, count);
    a->group = g;
    return a;
}

// Mixes `frames` frames (mono) and returns the loudest absolute sample.
float MixPeak(Scene& s, int frames, float* first = nullptr) {
    std::vector<float> m(static_cast<std::size_t>(frames));
    AudioMixer::Render(s, m.data(), frames);
    float p = 0.0f;
    for (float v : m) p = std::max(p, std::fabs(v));
    if (first) *first = m.empty() ? 0.0f : m[0];
    return p;
}

} // namespace

int main() {
    RUN_SUITE("pause_sound");

    // Game::SetPaused: an SFX source falls silent and holds its playhead; a UI source
    // plays on. Resuming continues from the same sample.
    {
        ResetAll();
        Scene s("Pause"); s.physicsEnabled = false;
        auto* sfx = MakeSource(s, "Sfx", 0.5f, 44100, AudioGroup::SFX);
        auto* ui = MakeSource(s, "Ui", 0.25f, 44100, AudioGroup::UI);
        s.Start();
        sfx->Play(); ui->Play();
        float first = 0.0f;
        MixPeak(s, 1000, &first);
        CHECK_NEAR(first, 0.75f, 1e-4);
        CHECK(sfx->Playhead() == 1000);

        Game::SetPaused(true);
        MixPeak(s, 1000, &first);
        CHECK_NEAR(first, 0.25f, 1e-4);          // only the UI source sounds
        CHECK(sfx->Playhead() == 1000);           // the game sound did not move
        CHECK(sfx->IsPlaying());                  // ...and is still "playing" (held)
        CHECK(ui->Playhead() == 2000);            // the UI sound did

        // A long pause does not play the sound out.
        for (int i = 0; i < 100; ++i) MixPeak(s, 1000);
        CHECK(sfx->Playhead() == 1000);
        CHECK(sfx->IsPlaying());

        Game::SetPaused(false);
        MixPeak(s, 1000, &first);
        CHECK_NEAR(first, 0.5f, 1e-4);            // UI clip has ended; SFX is back
        CHECK(sfx->Playhead() == 2000);           // continued from where it stopped
    }

    // Voice pauses, Music keeps playing (the defaults); one-shots on a game source
    // wait too.
    {
        ResetAll();
        Scene s("Groups"); s.physicsEnabled = false;
        auto* voice = MakeSource(s, "Voice", 0.5f, 44100, AudioGroup::Voice);
        auto* music = MakeSource(s, "Music", 0.125f, 44100, AudioGroup::Music);
        auto* shot = MakeSource(s, "Shot", 0.25f, 44100, AudioGroup::SFX);
        s.Start();
        voice->Play(); music->Play(); shot->PlayOneShot();
        MixPeak(s, 500);
        Game::SetPaused(true);
        float first = 0.0f;
        MixPeak(s, 44100, &first);
        CHECK_NEAR(first, 0.125f, 1e-4);
        CHECK(voice->Playhead() == 500);
        CHECK(!music->IsPlaying());         // music played on through the pause, to its end
        CHECK(shot->ActiveOneShots() == 1); // the one-shot is held, not finished
        Game::SetPaused(false);
        MixPeak(s, 100, &first);
        CHECK_NEAR(first, 0.75f, 1e-4);     // voice 0.5 + shot 0.25
        CHECK(voice->Playhead() == 600);
    }

    // Time.timeScale = 0 pauses game sounds the same way; a scale above 0 does not.
    {
        ResetAll();
        Scene s("Scale"); s.physicsEnabled = false;
        auto* sfx = MakeSource(s, "Sfx", 0.5f, 44100, AudioGroup::SFX);
        s.Start();
        sfx->Play();
        MixPeak(s, 100);
        Time::SetTimeScale(0.0f);
        CHECK_NEAR(MixPeak(s, 1000), 0.0f, 1e-6);
        CHECK(sfx->Playhead() == 100);
        Time::SetTimeScale(0.25f);         // slow motion: audio speed is not changed
        MixPeak(s, 100);
        CHECK(sfx->Playhead() == 200);
        Time::SetTimeScale(1.0f);
        MixPeak(s, 100);
        CHECK(sfx->Playhead() == 300);
    }

    // A hit stop (a short freeze frame) is not a pause: the impact sound plays on.
    {
        ResetAll();
        Scene s("HitStop"); s.physicsEnabled = false;
        auto* sfx = MakeSource(s, "Sfx", 0.5f, 44100, AudioGroup::SFX);
        s.Start();
        sfx->Play();
        Time::HitStop(1.0f, 0.0f);
        CHECK(Time::HitStopActive());
        CHECK_NEAR(MixPeak(s, 100), 0.5f, 1e-4);
        CHECK(sfx->Playhead() == 100);
    }

    // The group table: a game can make Music pause too, or let SFX play on.
    {
        ResetAll();
        Scene s("Table"); s.physicsEnabled = false;
        auto* music = MakeSource(s, "Music", 0.5f, 44100, AudioGroup::Music);
        s.Start();
        music->Play();
        AudioMixer::pausesWithGame[(int)AudioGroup::Music] = true;
        CHECK(AudioMixer::PausesWithGame(AudioGroup::Music));
        Game::SetPaused(true);
        CHECK(AudioMixer::GamePausedForAudio());
        CHECK_NEAR(MixPeak(s, 100), 0.0f, 1e-6);
        CHECK(music->Playhead() == 0);
        AudioMixer::pausesWithGame[(int)AudioGroup::Music] = false;
        CHECK_NEAR(MixPeak(s, 100), 0.5f, 1e-4);
        CHECK(music->Playhead() == 100);
        Game::SetPaused(false);
        CHECK(!AudioMixer::GamePausedForAudio());
    }

    // A fade on a paused source waits as well: a FadeOut that is half done when the
    // pause starts is still half done when it ends (it does not stop during the pause).
    {
        ResetAll();
        Scene s("Fade"); s.physicsEnabled = false;
        auto* sfx = MakeSource(s, "Sfx", 0.5f, 441000, AudioGroup::SFX);
        s.Start();
        sfx->Play();
        sfx->FadeOut(0.1f);                 // 4410 frames to silence
        MixPeak(s, 2205);
        const float half = sfx->FadeLevel();
        CHECK_NEAR(half, 0.5f, 0.01);
        Game::SetPaused(true);
        MixPeak(s, 44100);
        CHECK_NEAR(sfx->FadeLevel(), half, 1e-6);
        CHECK(sfx->IsPlaying());
        Game::SetPaused(false);
        MixPeak(s, 4410);
        CHECK(!sfx->IsPlaying());           // the fade finished after the pause
    }

    // Ducking: a paused voice line does not hold the music down.
    {
        ResetAll();
        AudioMixer::duckMusicUnderVoice = true;
        AudioMixer::duckAttack = 0.0f;
        AudioMixer::duckRelease = 0.0f;
        Scene s("Duck"); s.physicsEnabled = false;
        auto* voice = MakeSource(s, "Voice", 0.0f, 441000, AudioGroup::Voice);
        auto* music = MakeSource(s, "Music", 0.5f, 441000, AudioGroup::Music);
        s.Start();
        voice->Play(); music->Play();
        MixPeak(s, 100);
        CHECK(AudioMixer::duckGain < 1.0f);
        Game::SetPaused(true);
        MixPeak(s, 100);
        CHECK_NEAR(AudioMixer::duckGain, 1.0f, 1e-6);
        Game::SetPaused(false);
        AudioMixer::duckMusicUnderVoice = false;
        AudioMixer::duckAttack = 0.15f;
        AudioMixer::duckRelease = 0.6f;
    }

    ResetAll();
    TEST_MAIN_RESULT();
}
