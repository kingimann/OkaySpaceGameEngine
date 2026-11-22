// Asset streaming (feature map 2.13): texture mips stream on the JobSystem and are evicted under a budget, model
// meshes load in the background behind a placeholder, long sounds stream by default, and the numbers the Stats
// panel and the F3 overlay show.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Audio/AudioLoad.hpp"
#include "okay/Components/AssetStreamer.hpp"
#include "okay/Modeling/MeshAsset.hpp"
#include "okay/Render/AssetStreaming.hpp"
#include "okay/Render/TextureAsset.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>

using namespace okay;
namespace fs = std::filesystem;

static std::string WritePicture(const fs::path& dir, const char* name, int size) {
    Image img(size, size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            img.SetPixel(x, y, ((x / 8 + y / 8) & 1) ? Color(0.9f, 0.3f, 0.2f, 1) : Color(0.2f, 0.4f, 0.9f, 1));
    const std::string p = (dir / name).string();
    img.SavePNG(p);
    return p;
}

static GameObject* Panel(Scene& s, const std::string& tex, Vec3 at) {
    GameObject* go = s.CreateGameObject("Panel");
    go->transform->localPosition = at;
    auto* mr = go->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::Cube();
    mr->texture = tex;
    return go;
}

static std::string WriteWav(const fs::path& dir, const char* name, float seconds) {
    AudioClip c;
    c.sampleRate = 8000;
    c.channels = 1;
    c.samples.assign((std::size_t)(seconds * 8000), 0.0f);
    for (std::size_t i = 0; i < c.samples.size(); ++i) c.samples[i] = 0.2f * std::sin(0.05f * (float)i);
    const std::string p = (dir / name).string();
    c.SaveWAV(p);
    return p;
}

int main() {
    const fs::path dir = fs::temp_directory_path() / "okay_asset_streaming_test";
    fs::remove_all(dir);
    fs::create_directories(dir);

    RUN_SUITE("mip arithmetic");
    {
        int w = 0, h = 0;
        textures::MipSize(256, 128, 2, w, h);
        CHECK(w == 64 && h == 32);
        textures::MipSize(256, 128, 20, w, h);
        CHECK(w == 1 && h == 1);
        CHECK(textures::MipCountFor(256, 256) == 9);
        CHECK(textures::StartMipFor(256, 256, 64) == 2);
        CHECK(textures::StartMipFor(40, 40, 64) == 0);
    }

    RUN_SUITE("texture starts small and sharpens in the background");
    {
        textures::ResetForTests();
        const std::string pic = WritePicture(dir, "checker.png", 256);
        textures::SetStreamStartSize(32);
        textures::SetStreaming(true);
        const TextureAsset* a = textures::ForRender(pic);
        CHECK(a != nullptr);
        if (a) {
            CHECK(a->Width() == 32 && a->residentMip == 3);
            CHECK(a->FullWidth() == 256 && a->FullHeight() == 256);
        }
        Scene s;
        Panel(s, pic, {0, 0, 0});
        streaming::Config() = streaming::Settings{};
        streaming::Config().budgetMB = 64.0f;
        const std::uint64_t serial0 = a ? a->serial : 0;
        // Close up (the cube fills most of a 512 px tall view): the full picture is wanted.
        streaming::Update(s, {0, 0, 2.5f}, 60.0f, 512);
        textures::StreamCounters c = textures::GetStreamCounters();
        CHECK(c.loadsQueued == 1u);
        streaming::Stats st = streaming::GetStats();
        CHECK(st.texturesOn && st.textures == 1);
        CHECK(st.textureLoadsPending == 1);
        textures::FinishStreamLoads();
        a = textures::ForRender(pic);
        CHECK(a && a->residentMip <= 1);
        CHECK(a && a->Width() >= 128);
        CHECK(a && a->serial != serial0);   // renderers upload it again
        std::printf("  close up: %dx%d (mip %d)\n", a ? a->Width() : 0, a ? a->Height() : 0, a ? a->residentMip : -1);
        // Far away: dropped to a small mip at once.
        streaming::Update(s, {0, 0, 400.0f}, 60.0f, 512);
        a = textures::ForRender(pic);
        CHECK(a && a->residentMip >= 5);
        CHECK(textures::GetStreamCounters().evictions >= 1u);
        std::printf("  far away: %dx%d (mip %d)\n", a ? a->Width() : 0, a ? a->Height() : 0, a ? a->residentMip : -1);
        // The overlay line.
        const std::string line = streaming::OverlayLine();
        CHECK(line.find("streaming:") == 0);
        CHECK(line.find("1 textures") != std::string::npos);
    }

    RUN_SUITE("memory budget and upload cap");
    {
        textures::ResetForTests();
        textures::SetStreamStartSize(16);
        textures::SetStreaming(true);
        Scene s;
        std::vector<std::string> pics;
        for (int i = 0; i < 4; ++i) {
            pics.push_back(WritePicture(dir, ("p" + std::to_string(i) + ".png").c_str(), 256));
            Panel(s, pics.back(), {(float)i * 1.5f - 2.25f, 0, -(float)i});   // farther ones a little smaller
            textures::ForRender(pics.back());
        }
        // A budget below four full pictures (4 x 341 KB with chains): not every one may be sharp.
        streaming::Config() = streaming::Settings{};
        streaming::Config().budgetMB = 0.6f;
        streaming::Config().uploadMBPerFrame = 0.0f;
        for (int frame = 0; frame < 4; ++frame) {
            streaming::Update(s, {0, 0, 3.0f}, 60.0f, 1024);
            textures::FinishStreamLoads();
        }
        std::size_t bytes = 0;
        for (const auto& info : textures::StreamedTextures()) bytes += info.bytes;
        std::printf("  4 pictures under a 0.6 MB budget: %.2f MB resident\n", bytes / 1048576.0);
        CHECK(bytes <= (std::size_t)(0.6f * 1048576.0f) + 4096);
        CHECK(streaming::GetStats().budgetBytes == (std::size_t)(0.6f * 1048576.0f));
        // Upload cap: one picture's worth per frame, the most visible first.
        textures::ResetForTests();
        textures::SetStreaming(true);
        for (const auto& p : pics) textures::ForRender(p);
        streaming::Config().budgetMB = 64.0f;
        streaming::Config().uploadMBPerFrame = 0.1f;   // smaller than one full picture
        streaming::Update(s, {0, 0, 3.0f}, 60.0f, 1024);
        CHECK(textures::GetStreamCounters().loadsQueued == 1u);   // the first is always let through, the rest wait
        textures::FinishStreamLoads();
        streaming::Update(s, {0, 0, 3.0f}, 60.0f, 1024);
        CHECK(textures::GetStreamCounters().loadsQueued == 2u);
        textures::FinishStreamLoads();
    }

    RUN_SUITE("streaming off: whole pictures");
    {
        textures::ResetForTests();
        textures::SetStreaming(false);
        const std::string pic = WritePicture(dir, "whole.png", 128);
        const TextureAsset* a = textures::ForRender(pic);
        CHECK(a && a->Width() == 128 && a->residentMip == 0);
        CHECK(textures::RequestMip(pic, 0) == textures::StreamResult::Unchanged);
        CHECK(textures::RequestMip("@nothing", 2) == textures::StreamResult::NotStreamable);
    }

    RUN_SUITE("Asset Streaming component");
    {
        textures::ResetForTests();
        textures::SetStreaming(false);
        Scene s;
        GameObject* go = s.CreateGameObject("Streaming");
        auto* as = go->AddComponent<AssetStreamer>();
        as->startSize = 48;
        as->budgetMB = 100.0f;
        as->backgroundMeshes = true;
        std::string text = SceneSerializer::Serialize(s);
        CHECK(text.find("assetstreamer") != std::string::npos);
        CHECK(streaming::SceneTextAsksForAsyncMeshes(text));
        Scene back; std::string err;
        CHECK(SceneSerializer::Deserialize(back, text, &err));
        AssetStreamer* b = nullptr;
        for (const auto& up : back.Objects()) if (auto* c = up->GetComponent<AssetStreamer>()) b = c;
        CHECK(b && b->startSize == 48 && std::fabs(b->budgetMB - 100.0f) < 1e-3f && b->backgroundMeshes);
        as->backgroundMeshes = false;
        CHECK(!streaming::SceneTextAsksForAsyncMeshes(SceneSerializer::Serialize(s)));
        s.Start();   // Awake turns texture streaming on with the component's numbers
        CHECK(textures::Streaming());
        CHECK(textures::StreamStartSize() == 48);
        CHECK(std::fabs(streaming::Config().budgetMB - 100.0f) < 1e-3f);
        s.Destroy(go);
        s.FlushDestroyed();
        CHECK(!textures::Streaming());   // and off again when it goes
    }

    RUN_SUITE("meshes load in the background");
    {
        // A model file, loaded once to know its triangles.
        const std::string obj = (dir / "rock.obj").string();
        {
            std::ofstream o(obj);
            const int n = 16;
            for (int r = 0; r <= n; ++r) for (int c = 0; c <= n; ++c) {
                const float th = 3.14159265f * r / n, ph = 6.2831853f * c / n;
                o << "v " << std::sin(th) * std::cos(ph) << " " << std::cos(th) << " " << std::sin(th) * std::sin(ph) << "\n";
            }
            for (int r = 0; r < n; ++r) for (int c = 0; c < n; ++c) {
                const int a = r * (n + 1) + c + 1, b = a + n + 1;
                o << "f " << a << " " << b << " " << b + 1 << "\nf " << a << " " << b + 1 << " " << a + 1 << "\n";
            }
        }
        Mesh real; std::string tex, err;
        CHECK(modeling::LoadMeshAsset(obj, real, &tex, &err));
        Scene src;
        GameObject* go = src.CreateGameObject("Rock");
        auto* mr = go->AddComponent<MeshRenderer>();
        mr->mesh = real;
        mr->meshPath = obj;
        mr->meshPathHash = modeling::GeometryHash(real);
        auto* lod = go->AddComponent<LODGroup>();
        lod->levels.resize(2);
        lod->levels[1].ratio = 0.5f;
        lod->RebuildFromRatios(real);
        const std::string text = SceneSerializer::Serialize(src);

        streaming::ResetForTests();
        streaming::SetMeshAsyncMode(2);
        Scene s;
        CHECK(SceneSerializer::Deserialize(s, text, &err));
        MeshRenderer* got = nullptr;
        for (const auto& up : s.Objects()) if (auto* m = up->GetComponent<MeshRenderer>()) got = m;
        CHECK(got != nullptr);
        if (got) {
            CHECK(streaming::MeshPending(got));
            CHECK(got->mesh.TriangleCount() == Mesh::Cube().TriangleCount());   // the placeholder
            CHECK(streaming::GetStats().meshesWaiting == 1);
            streaming::FinishMeshLoads();
            CHECK(streaming::ApplyMeshLoads(s) == 1);
            CHECK(!streaming::MeshPending(got));
            CHECK(got->mesh.TriangleCount() == real.TriangleCount());
            CHECK(got->meshPathHash == modeling::GeometryHash(real));
            LODGroup* gl = got->gameObject->GetComponent<LODGroup>();
            CHECK(gl && gl->lodMeshes.size() == 1u && gl->lodMeshes[0].TriangleCount() < real.TriangleCount()
                  && gl->lodMeshes[0].TriangleCount() > Mesh::Cube().TriangleCount());
            CHECK(streaming::GetStats().meshesLoaded == 1u);
        }
        // Mode 1 (the player): only when the scene asks for it.
        streaming::SetMeshAsyncMode(1);
        Scene plain;
        CHECK(SceneSerializer::Deserialize(plain, text, &err));
        got = nullptr;
        for (const auto& up : plain.Objects()) if (auto* m = up->GetComponent<MeshRenderer>()) got = m;
        CHECK(got && !streaming::MeshPending(got) && got->mesh.TriangleCount() == real.TriangleCount());
        src.CreateGameObject("Streaming")->AddComponent<AssetStreamer>();
        Scene asks;
        CHECK(SceneSerializer::Deserialize(asks, SceneSerializer::Serialize(src), &err));
        got = nullptr;
        for (const auto& up : asks.Objects()) if (auto* m = up->GetComponent<MeshRenderer>()) got = m;
        CHECK(got && streaming::MeshPending(got));
        streaming::FinishMeshLoads();
        streaming::ApplyMeshLoads(asks);
        CHECK(got && got->mesh.TriangleCount() == real.TriangleCount());
        // Off (the editor): at once.
        streaming::SetMeshAsyncMode(0);
        Scene ed;
        CHECK(SceneSerializer::Deserialize(ed, SceneSerializer::Serialize(src), &err));
        got = nullptr;
        for (const auto& up : ed.Objects()) if (auto* m = up->GetComponent<MeshRenderer>()) got = m;
        CHECK(got && !streaming::MeshPending(got) && got->mesh.TriangleCount() == real.TriangleCount());
        streaming::ResetForTests();
    }

    RUN_SUITE("long sounds stream by default");
    {
        CHECK_NEAR(AudioStreamThresholdSeconds(), 30.0f, 1e-6f);
        const std::string longWav = WriteWav(dir, "long.wav", 40.0f), shortWav = WriteWav(dir, "short.wav", 2.0f);
        CHECK(AudioFileSeconds(longWav) > 39.0f);
        AudioSource a, b;
        CHECK(AttachAudioFile(a, longWav) == AudioAttach::Stream);
        CHECK(a.IsStreaming());
        CHECK(AttachAudioFile(b, shortWav) == AudioAttach::Clip);
        // An explicit "Decode" import setting keeps the whole file in memory.
        std::ofstream(longWav + ".okaymeta") << "importer audio 2\nset loadMode decode\n";
        AudioSource c;
        CHECK(AttachAudioFile(c, longWav) == AudioAttach::Clip);
        // The same rule when a scene loads.
        fs::remove(longWav + ".okaymeta");
        Scene s;
        auto* src = s.CreateGameObject("Music")->AddComponent<AudioSource>();
        src->clipPath = longWav;
        CHECK(LoadSceneAudio(s, dir.string(), 8000) == 1);
        CHECK(src->IsStreaming());
    }

    textures::SetStreaming(false);
    textures::ResetForTests();
    fs::remove_all(dir);
    TEST_MAIN_RESULT();
}
