// Play-start hitch fixes: (1) sources on one audio file share ONE decoded clip, and the shared decode cache never
// serves a stale clip and can be trimmed; (2) a Nav Mesh Surface is not baked again when nothing the bake reads changed.
#include "test_framework.hpp"
#include "okay/Audio/AudioLoad.hpp"
#include <Okay.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace okay;
namespace fs = std::filesystem;

namespace {
void Put16(std::vector<unsigned char>& v, unsigned x) { v.push_back(x & 255); v.push_back((x >> 8) & 255); }
void Put32(std::vector<unsigned char>& v, unsigned x) { for (int i = 0; i < 4; ++i) v.push_back((x >> (8 * i)) & 255); }
void WriteWav(const fs::path& path, double seconds, double freq) {
    const int rate = 44100;
    const int n = (int)(seconds * rate);
    std::vector<unsigned char> data;
    for (int i = 0; i < n; ++i) Put16(data, (unsigned)((int)(std::sin(i * freq) * 12000) & 0xFFFF));
    std::vector<unsigned char> w;
    w.insert(w.end(), {'R', 'I', 'F', 'F'}); Put32(w, 36 + (unsigned)data.size());
    w.insert(w.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    Put32(w, 16); Put16(w, 1); Put16(w, 1); Put32(w, rate); Put32(w, rate * 2); Put16(w, 2); Put16(w, 16);
    w.insert(w.end(), {'d', 'a', 't', 'a'}); Put32(w, (unsigned)data.size());
    w.insert(w.end(), data.begin(), data.end());
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(w.data()), (std::streamsize)w.size());
}
}  // namespace

int main() {
    RUN_SUITE("play_hitch");
    Log::console = false;
    const fs::path root = fs::path(OKAY_TEST_OUTPUT_DIR) / "play_hitch";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    // ---- (1) one decoded clip for every source on the same file ----
    {
        AudioClip::ClearCache();
        WriteWav(root / "a.wav", 1.0, 0.05);
        WriteWav(root / "b.wav", 0.5, 0.07);
        Scene s("Share"); s.physicsEnabled = false;
        std::vector<AudioSource*> srcs;
        for (int i = 0; i < 6; ++i) {
            auto* au = s.CreateGameObject("S" + std::to_string(i))->AddComponent<AudioSource>();
            au->clipPath = (i < 4 ? "a.wav" : "b.wav");
            srcs.push_back(au);
        }
        CHECK(LoadSceneAudio(s, root.string(), 44100) == 6);
        CHECK(srcs[0]->UsesSharedClip());
        // Same file: the very same samples in memory (no per-source copy). Different file: different samples.
        CHECK(&srcs[0]->Clip() == &srcs[3]->Clip());
        CHECK(srcs[0]->Clip().samples.data() == srcs[1]->Clip().samples.data());
        CHECK(&srcs[0]->Clip() != &srcs[4]->Clip());
        CHECK(&srcs[4]->Clip() == &srcs[5]->Clip());
        CHECK(AudioClip::CacheCount() == 2);
        CHECK(srcs[0]->HasAudio() && !srcs[0]->IsStreaming());
        CHECK(std::fabs(srcs[0]->Duration() - 1.0f) < 0.01f);
        // The shared clip still plays through the mixer, on every source at once.
        s.Start();
        for (auto* a : srcs) a->Play();
        float buf[512] = {0};
        CHECK(AudioMixer::Render(s, buf, 512) == 6);
        // A clip the game sets itself wins over the shared one.
        srcs[1]->clip = AudioClip::Sine(440.0f, 0.1f);
        CHECK(!srcs[1]->UsesSharedClip() && &srcs[1]->Clip() == &srcs[1]->clip);
        CHECK(srcs[0]->UsesSharedClip());   // the others are untouched
        // PlayOneShot of the shared clip does not copy it.
        srcs[0]->PlayOneShot();
        CHECK(srcs[0]->ActiveOneShots() == 1);
    }

    // ---- the cache never serves a stale clip ----
    {
        AudioClip::ClearCache();
        const std::string p = (root / "c.wav").string();
        WriteWav(p, 1.0, 0.05);
        auto first = AudioClip::LoadCached(p, 44100);
        CHECK(first && AudioClip::LoadCached(p, 44100) == first);
        CHECK(AudioClip::LoadCached((root / "." / "c.wav").string(), 44100) == first);   // another spelling of the same file
        WriteWav(p, 2.0, 0.05);   // the file changes on disk (a different length, so a different size)
        auto second = AudioClip::LoadCached(p, 44100);
        CHECK(second && second != first);
        CHECK(std::fabs(second->Duration() - 2.0f) < 0.01f);
        CHECK(std::fabs(first->Duration() - 1.0f) < 0.01f);   // a clip already handed out keeps its own copy
        // Same size, new content (a rewrite of a same-length file): the write time tells.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        WriteWav(p, 2.0, 0.2);
        fs::last_write_time(p, fs::last_write_time(p) + std::chrono::seconds(2), ec);
        auto third = AudioClip::LoadCached(p, 44100);
        CHECK(third && third != second && third->samples != second->samples);
    }

    // ---- the cache can be trimmed (no leak) but never drops a clip something still holds ----
    {
        AudioClip::ClearCache();
        WriteWav(root / "d.wav", 1.0, 0.05);
        WriteWav(root / "e.wav", 1.0, 0.06);
        auto held = AudioClip::LoadCached((root / "d.wav").string(), 44100);
        { auto tmp = AudioClip::LoadCached((root / "e.wav").string(), 44100); }
        CHECK(AudioClip::CacheCount() == 2 && AudioClip::CacheBytes() > 0);
        AudioClip::TrimCache(0);
        CHECK(AudioClip::CacheCount() == 1);   // the unheld one went, the held one stays
        CHECK(AudioClip::LoadCached((root / "d.wav").string(), 44100) == held);
        held.reset();
        AudioClip::TrimCache(0);
        CHECK(AudioClip::CacheCount() == 0 && AudioClip::CacheBytes() == 0);
    }

    // ---- several threads loading the same file get one shared clip ----
    {
        AudioClip::ClearCache();
        WriteWav(root / "t.wav", 1.0, 0.05);
        const std::string p = (root / "t.wav").string();
        std::vector<std::shared_ptr<const AudioClip>> got(8);
        std::vector<std::thread> th;
        for (int i = 0; i < 8; ++i) th.emplace_back([&, i] { got[i] = AudioClip::LoadCached(p, 44100); });
        for (auto& t : th) t.join();
        bool same = true;
        for (auto& g : got) same = same && g && g == got[0];
        CHECK(same);
        CHECK(AudioClip::CacheCount() == 1);
    }

    // ---- (2) Bake only when something the bake reads changed ----
    {
        Scene s("Nav"); s.physicsEnabled = false;
        GameObject* floor = s.CreateGameObject("Floor");
        floor->transform->localPosition = {0, -0.5f, 0}; floor->transform->localScale = {10, 1, 10};
        floor->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
        GameObject* crate = s.CreateGameObject("Crate");
        crate->transform->localPosition = {2, 0.5f, 2};
        crate->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
        GameObject* ball = s.CreateGameObject("Ball");   // a dynamic body: only baked when includeDynamic is on
        ball->transform->localPosition = {-2, 0.5f, -2};
        ball->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
        ball->AddComponent<Rigidbody3D>();
        auto* ns = s.CreateGameObject("Nav")->AddComponent<NavMeshSurface>();
        ns->settings.cellSize = 0.5f;
        ns->navPath = (root / "nav.okaynav").string();

        bool skipped = true;
        CHECK(ns->BakeIfChanged(s, &skipped) && !skipped);       // nothing baked yet: bake
        CHECK(ns->HasMesh());
        CHECK(ns->Save());
        CHECK(ns->BakeIfChanged(s, &skipped) && skipped);        // unchanged: skip
        const NavMesh* before = ns->Mesh();

        ns->settings.agentRadius += 0.1f;                         // a surface setting
        CHECK(ns->BakeIfChanged(s, &skipped) && !skipped);
        CHECK(ns->BakeIfChanged(s, &skipped) && skipped);
        ns->includeDynamic = true;                                // an option that changes what is gathered
        CHECK(ns->BakeIfChanged(s, &skipped) && !skipped);
        ns->includeDynamic = false;
        CHECK(ns->BakeIfChanged(s, &skipped) && !skipped);
        CHECK(ns->BakeIfChanged(s, &skipped) && skipped);

        crate->transform->localPosition = {3, 0.5f, 2};           // a collider moved
        CHECK(ns->BakeIfChanged(s, &skipped) && !skipped);
        CHECK(ns->BakeIfChanged(s, &skipped) && skipped);
        crate->GetComponent<BoxCollider3D>()->size = {1, 2, 1};   // a collider resized
        CHECK(ns->BakeIfChanged(s, &skipped) && !skipped);
        CHECK(ns->BakeIfChanged(s, &skipped) && skipped);
        crate->GetComponent<BoxCollider3D>()->enabled = false;    // a collider switched off
        CHECK(ns->BakeIfChanged(s, &skipped) && !skipped);
        CHECK(ns->BakeIfChanged(s, &skipped) && skipped);
        s.CreateGameObject("Wall")->AddComponent<BoxCollider3D>()->size = {1, 1, 1};   // a collider added
        CHECK(ns->BakeIfChanged(s, &skipped) && !skipped);
        CHECK(ns->BakeIfChanged(s, &skipped) && skipped);
        (void)before;

        // A mesh dropped (Clear) is baked again even though the scene is the same.
        ns->Clear();
        CHECK(ns->BakeIfChanged(s, &skipped) && !skipped && ns->HasMesh());

        // The editor's Play/Stop rebuilds the surface from the scene text: a fresh surface reading the saved file
        // skips when the file is the one baked from this very input, and bakes when the file changed.
        CHECK(ns->Save());
        auto* fresh = s.CreateGameObject("Nav2")->AddComponent<NavMeshSurface>();
        fresh->settings = ns->settings; fresh->navPath = ns->navPath;
        CHECK(fresh->BakeIfChanged(s, &skipped) && skipped);
        auto* fresh2 = s.CreateGameObject("Nav3")->AddComponent<NavMeshSurface>();
        fresh2->settings = ns->settings; fresh2->navPath = ns->navPath;
        {   // someone rewrote the file since: not provably the baked one
            std::ofstream f(ns->navPath, std::ios::binary | std::ios::app); f << "x";
        }
        std::string err;
        const bool ok = fresh2->BakeIfChanged(s, &skipped, nullptr, &err);
        CHECK(!skipped);   // whether the damaged file loads or not, it must not be trusted
        (void)ok;
        // A surface with a file nobody baked in this session also bakes.
        auto* other = s.CreateGameObject("Nav4")->AddComponent<NavMeshSurface>();
        other->settings = ns->settings; other->navPath = (root / "never.okaynav").string();
        CHECK(other->BakeIfChanged(s, &skipped) && !skipped);
    }
    TEST_MAIN_RESULT();
}
