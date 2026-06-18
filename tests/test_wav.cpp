#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdio>

using namespace okay;

int main() {
    RUN_SUITE("wav");

    // --- WAV save/load round-trip preserves rate, length, and samples ---
    {
        AudioClip clip = AudioClip::Sine(220.0f, 0.05f, 0.8f); // ~2205 samples @ 44100
        const char* path = "test_clip.wav";
        CHECK(clip.SaveWAV(path));

        AudioClip loaded;
        std::string err;
        bool ok = loaded.LoadWAV(path, &err);
        CHECK(ok);
        if (!ok) std::cerr << "  load error: " << err << "\n";
        CHECK(loaded.sampleRate == 44100);
        CHECK(loaded.samples.size() == clip.samples.size());
        // 16-bit quantization tolerance.
        bool closeEnough = true;
        for (std::size_t i = 0; i < loaded.samples.size(); ++i)
            if (std::abs(loaded.samples[i] - clip.samples[i]) > 0.01f) { closeEnough = false; break; }
        CHECK(closeEnough);

        std::remove(path);
    }

    // --- Loading a bad/missing file fails cleanly ---
    {
        AudioClip c;
        CHECK(!c.LoadWAV("definitely_missing.wav"));
        CHECK(c.samples.empty());
    }

    // --- Resampling changes the rate and the proportional length ---
    {
        AudioClip clip = AudioClip::Const(0.5f, 1000, 44100);
        AudioClip down = clip.Resampled(22050);
        CHECK(down.sampleRate == 22050);
        CHECK(down.samples.size() == 500);
        CHECK_NEAR(down.samples[100], 0.5f, 0.001f); // constant signal preserved
        AudioClip same = clip.Resampled(44100);
        CHECK(same.samples.size() == 1000);
    }

    // --- AudioSource clip path + params survive serialization ---
    {
        Scene scene("A");
        GameObject* go = scene.CreateGameObject("Speaker");
        auto* au = go->AddComponent<AudioSource>();
        au->clipPath = "boom.wav";
        au->volume = 0.7f;
        au->loop = true;
        au->playOnAwake = true;

        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        auto* r = loaded.Find("Speaker")->GetComponent<AudioSource>();
        CHECK(r != nullptr);
        CHECK(r->clipPath == "boom.wav");
        CHECK_NEAR(r->volume, 0.7f, 0.001f);
        CHECK(r->loop);
        CHECK(r->playOnAwake);
    }

    TEST_MAIN_RESULT();
}
