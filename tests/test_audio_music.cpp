#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdio>

using namespace okay;

namespace {

void ResetMixer() {
    AudioMixer::sampleRate = 1000;   // small numbers: 1000 frames = 1 second
    AudioMixer::masterVolume = 1.0f;
    AudioMixer::muted = false;
    for (int i = 0; i < (int)AudioGroup::Count; ++i) {
        AudioMixer::groupVolume[i] = 1.0f;
        AudioMixer::groupMuted[i] = false;
    }
    AudioMixer::duckMusicUnderVoice = false;
    AudioMixer::duckGain = 1.0f;
    AudioMixer::SetListener(Vec3::Zero);
}

AudioClip Const(float v, int n) { AudioClip c = AudioClip::Const(v, n, 1000); return c; }

float Mono(Scene& s, int frames, std::vector<float>* all = nullptr) {
    std::vector<float> b((std::size_t)frames);
    AudioMixer::Render(s, b.data(), frames);
    if (all) *all = b;
    return b.back();
}

std::string WriteTone(const char* name, float seconds) {
    AudioClip c = AudioClip::Const(0.5f, (int)(seconds * 1000), 1000);
    c.SaveWAV(name);
    return name;
}

} // namespace

int main() {
    RUN_SUITE("audio_music");

    // Fade in rises smoothly from silence; fade out falls and stops the source.
    {
        ResetMixer();
        Scene s("Fade"); s.physicsEnabled = false;
        auto* a = s.CreateGameObject("A")->AddComponent<AudioSource>();
        a->clip = Const(1.0f, 5000);
        s.Start();
        a->FadeIn(1.0f);
        std::vector<float> b;
        Mono(s, 500, &b);
        CHECK_NEAR(b[0], 0.0f, 0.01f);
        CHECK_NEAR(b[499], 0.5f, 0.01f);
        for (int i = 1; i < 500; ++i) CHECK(b[i] >= b[i - 1]);   // no steps down
        CHECK(a->IsFading());
        Mono(s, 600);
        CHECK_NEAR(a->FadeLevel(), 1.0f, 1e-4);
        CHECK(!a->IsFading());

        a->FadeOut(0.5f);
        Mono(s, 250);
        CHECK_NEAR(a->FadeLevel(), 0.5f, 0.01f);
        CHECK(a->IsPlaying());
        Mono(s, 300);
        CHECK(!a->IsPlaying());          // reached silence -> stopped
        CHECK_NEAR(a->FadeLevel(), 1.0f, 1e-4);   // ready for the next Play

        a->Play();
        a->FadeOut(0.2f, false);         // keep playing silently
        Mono(s, 300);
        CHECK(a->IsPlaying());
        CHECK_NEAR(a->FadeLevel(), 0.0f, 1e-4);
    }

    // Low-pass: a muffled source passes a slow signal but cuts a fast one.
    {
        ResetMixer();
        AudioMixer::sampleRate = 44100;
        Scene s("LowPass"); s.physicsEnabled = false;
        auto* a = s.CreateGameObject("A")->AddComponent<AudioSource>();
        a->clip = AudioClip::Square(8000.0f, 0.2f, 0.5f);   // harsh, high
        s.Start();
        auto rms = [&](float cutoff) {
            a->lowPass = cutoff; a->Play();
            std::vector<float> b; Mono(s, 4096, &b);
            double e = 0; for (float v : b) e += v * v;
            return std::sqrt(e / b.size());
        };
        const double open = rms(AudioSource::kLowPassOff);
        const double muffled = rms(400.0f);
        CHECK(open > 0.4);
        CHECK(muffled < open * 0.2);
    }

    // Ducking: music dips while a voice line plays and comes back after.
    {
        ResetMixer();
        Scene s("Duck"); s.physicsEnabled = false;
        auto* music = s.CreateGameObject("Music")->AddComponent<AudioSource>();
        music->clip = Const(0.5f, 10000); music->loop = true; music->group = AudioGroup::Music;
        auto* voice = s.CreateGameObject("Voice")->AddComponent<AudioSource>();
        voice->clip = Const(0.0f, 300); voice->group = AudioGroup::Voice;   // silent line, still "playing"
        s.Start();
        AudioMixer::duckMusicUnderVoice = true;
        AudioMixer::duckLevel = 0.4f; AudioMixer::duckAttack = 0.1f; AudioMixer::duckRelease = 0.2f;
        music->Play();
        CHECK_NEAR(Mono(s, 50), 0.5f, 1e-4);
        voice->Play();
        for (int i = 0; i < 4; ++i) Mono(s, 50);   // 0.2 s: past the attack
        CHECK_NEAR(AudioMixer::duckGain, 0.4f, 1e-4);
        CHECK_NEAR(Mono(s, 50), 0.2f, 1e-3);       // 0.5 * 0.4 while the line plays
        for (int i = 0; i < 6; ++i) Mono(s, 50);   // 0.3 s: past the release
        CHECK_NEAR(AudioMixer::duckGain, 1.0f, 1e-4);
        CHECK_NEAR(Mono(s, 50), 0.5f, 1e-3);

        AudioMixer::duckMusicUnderVoice = false;
        voice->Play();
        Mono(s, 200);
        CHECK_NEAR(AudioMixer::duckGain, 1.0f, 1e-4);   // off: never ducks
    }

    // MusicPlayer: plays track 0 on start, crossfades into the next before the
    // end, and loops back round.
    {
        ResetMixer();
        const std::string a = WriteTone("test_music_a.wav", 1.0f);
        const std::string b = WriteTone("test_music_b.wav", 1.0f);
        Scene s("Music"); s.physicsEnabled = false;
        auto* mp = s.CreateGameObject("Music")->AddComponent<MusicPlayer>();
        mp->tracks = {a, b};
        mp->crossfade = 0.25f;
        s.Start();
        s.Update(0.0f);
        CHECK(mp->CurrentIndex() == 0);
        CHECK(mp->IsPlaying());
        auto voices = s.FindObjectsOfType<AudioSource>();
        CHECK(voices.size() == 2);
        for (auto* v : voices) CHECK(v->group == AudioGroup::Music);

        // Run 0.8 s of game time: the switch happens at 0.75 s.
        for (int i = 0; i < 16; ++i) { Mono(s, 50); s.Update(0.05f); }
        CHECK(mp->CurrentIndex() == 1);
        int playing = 0;
        for (auto* v : voices) if (v->IsPlaying()) ++playing;
        CHECK(playing == 2);   // overlap
        // The overlap holds the level steady (fade out + fade in).
        CHECK_NEAR(Mono(s, 50), 0.5f, 0.06f);
        for (int i = 0; i < 6; ++i) { Mono(s, 50); s.Update(0.05f); }
        playing = 0;
        for (auto* v : voices) if (v->IsPlaying()) ++playing;
        CHECK(playing == 1);

        mp->Next();
        CHECK(mp->CurrentIndex() == 0);   // wraps
        mp->Previous();
        CHECK(mp->CurrentIndex() == 1);
        CHECK(mp->CurrentTrack() == b);

        mp->Stop(0.1f);
        for (int i = 0; i < 6; ++i) Mono(s, 50);
        for (auto* v : voices) CHECK(!v->IsPlaying());
        CHECK(!mp->IsPlaying());

        CHECK(!mp->Play(7));                    // out of range
        CHECK(mp->Play(std::string("test_music_a.wav")));
        CHECK(mp->CurrentIndex() == 0);
        CHECK(!mp->Play(std::string("missing.ogg")));
        std::remove(a.c_str()); std::remove(b.c_str());
    }

    // The new fields and the music player survive a save and load.
    {
        ResetMixer();
        Scene s("Save"); s.physicsEnabled = false;
        auto* go = s.CreateGameObject("Radio");
        go->AddComponent<AudioSource>()->lowPass = 900.0f;
        auto* mp = go->AddComponent<MusicPlayer>();
        mp->tracks = {"music/one.ogg", "music/two \"live\".mp3"};
        mp->shuffle = true; mp->playOnStart = false; mp->loopPlaylist = false;
        mp->crossfade = 3.5f; mp->volume = 0.6f;
        const std::string path = "test_audio_music.okayscene";
        CHECK(SceneSerializer::SaveToFile(s, path));
        Scene back("Back");
        CHECK(SceneSerializer::LoadFromFile(back, path));
        GameObject* r = back.Find("Radio");
        CHECK(r != nullptr);
        if (r) {
            auto* au = r->GetComponent<AudioSource>();
            auto* m = r->GetComponent<MusicPlayer>();
            CHECK(au && std::fabs(au->lowPass - 900.0f) < 1e-3f);
            CHECK(m != nullptr);
            if (m) {
                CHECK(m->tracks.size() == 2 && m->tracks[1] == "music/two \"live\".mp3");
                CHECK(m->shuffle && !m->playOnStart && !m->loopPlaylist);
                CHECK_NEAR(m->crossfade, 3.5f, 1e-4);
                CHECK_NEAR(m->volume, 0.6f, 1e-4);
            }
        }
        const auto assets = SceneSerializer::CollectAssetPaths(s);
        int found = 0;
        for (const auto& p : assets) if (p == "music/one.ogg" || p == "music/two \"live\".mp3") ++found;
        CHECK(found == 2);
        std::remove(path.c_str());
    }

    ResetMixer();
    AudioMixer::sampleRate = 44100;
    TEST_MAIN_RESULT();
}
