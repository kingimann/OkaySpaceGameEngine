// Play-start benchmark (headless). Pressing Play in the editor runs, on one frame: serialize the scene for the undo-stop
// snapshot, import pose clips for every ModelAnimator, load every AudioSource file, validate every visual script, bake
// navigation (editor only, timed here from the same engine call), then the first Tick starts every object. This test
// times each of those phases and then each of the first frames, and prints a table and the slowest frames. It reports
// numbers; it only fails on a very generous guard (so a real regression, like Play taking whole seconds, is caught).
#include "test_framework.hpp"
#include "EditorState.hpp"
#include "okay/Audio/AudioLoad.hpp"
#include "okay/Components/PoseCapture.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/VisualScript/VsTypes.hpp"
#include <Okay.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace okay;
namespace fs = std::filesystem;
using Clk = std::chrono::steady_clock;

namespace {

double Ms(Clk::time_point a, Clk::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }

template <class F> double Timed(F&& f) { const auto t0 = Clk::now(); f(); return Ms(t0, Clk::now()); }

void Put16(std::vector<unsigned char>& v, unsigned x) { v.push_back(x & 255); v.push_back((x >> 8) & 255); }
void Put32(std::vector<unsigned char>& v, unsigned x) { for (int i = 0; i < 4; ++i) v.push_back((x >> (8 * i)) & 255); }

// A stereo 16-bit 44.1 kHz wav of `seconds` of a tone.
void WriteWav(const fs::path& path, double seconds) {
    const int rate = 44100, ch = 2;
    const int n = (int)(seconds * rate);
    std::vector<unsigned char> data;
    for (int i = 0; i < n; ++i)
        for (int c = 0; c < ch; ++c) Put16(data, (unsigned)((int)(std::sin(i * 0.05 + c) * 12000) & 0xFFFF));
    std::vector<unsigned char> w;
    w.insert(w.end(), {'R', 'I', 'F', 'F'}); Put32(w, 36 + (unsigned)data.size());
    w.insert(w.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    Put32(w, 16); Put16(w, 1); Put16(w, ch); Put32(w, rate); Put32(w, rate * ch * 2); Put16(w, ch * 2); Put16(w, 16);
    w.insert(w.end(), {'d', 'a', 't', 'a'}); Put32(w, (unsigned)data.size());
    w.insert(w.end(), data.begin(), data.end());
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(w.data()), (std::streamsize)w.size());
}

struct Row { std::string name; double ms; };

void Print(const char* title, const std::vector<Row>& rows) {
    std::printf("\n== %s ==\n", title);
    for (const auto& r : rows) std::printf("  %-46s %9.2f ms\n", r.name.c_str(), r.ms);
}

// Time Play() and the first `frames` Ticks of whatever scene `ed` holds. Returns the per-frame times.
std::vector<double> PlayAndTick(okay::editor::EditorState& ed, int frames, double* playMs) {
    *playMs = Timed([&] { ed.Play(); });
    std::vector<double> t;
    for (int i = 0; i < frames; ++i) t.push_back(Timed([&] { ed.Tick(1.0f / 60.0f); }));
    return t;
}

void PrintFrames(const std::vector<double>& t) {
    std::vector<Row> rows;
    for (std::size_t i = 0; i < t.size(); ++i) rows.push_back({"tick #" + std::to_string(i), t[i]});
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.ms > b.ms; });
    rows.resize(std::min<std::size_t>(10, rows.size()));
    Print("10 slowest of the first ticks", rows);
    std::printf("  mean ms per block of 10 ticks:");
    for (std::size_t b = 0; b + 10 <= t.size(); b += 10) { double a = 0; for (std::size_t i = b; i < b + 10; ++i) a += t[i]; std::printf(" %.2f", a / 10); }
    std::printf("\n");
    double steady = 0; int n = 0;
    for (std::size_t i = t.size() > 60 ? t.size() - 60 : 0; i < t.size(); ++i) { steady += t[i]; ++n; }
    std::printf("  steady (mean of the last %d ticks): %.3f ms\n", n, n ? steady / n : 0.0);
}

}  // namespace

int main() {
    RUN_SUITE("play_start_bench");
    Log::console = false;
    const fs::path root = fs::path(OKAY_TEST_OUTPUT_DIR) / "play_bench_project";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Assets" / "Audio", ec);
    for (int i = 0; i < 6; ++i) WriteWav(root / "Assets" / "Audio" / ("clip" + std::to_string(i) + ".wav"), 8.0);

    // ---- A heavy synthetic scene: many bodies, nav surface that bakes before Play, audio, animators, scripts ----
    {
        okay::editor::EditorState ed;
        ed.setProjectDir(root.string());
        Scene& s = ed.scene();
        GameObject* floor = s.CreateGameObject("Ground");
        floor->transform->localPosition = {0, -0.5f, 0}; floor->transform->localScale = {80, 1, 80};
        floor->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
        floor->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
        for (int i = 0; i < 300; ++i) {   // static scenery with a mesh and a collider
            GameObject* g = s.CreateGameObject("Prop" + std::to_string(i));
            g->transform->localPosition = {(float)(i % 20) * 3.5f - 35.0f, 1.0f, (float)(i / 20) * 4.0f - 30.0f};
            g->transform->localScale = {1, 2, 1};
            auto* mr = g->AddComponent<MeshRenderer>(); mr->mesh = Mesh::Cube(1.0f);
            g->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
            g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
        }
        for (int i = 0; i < 150; ++i) {   // dynamic bodies
            GameObject* g = s.CreateGameObject("Crate" + std::to_string(i));
            g->transform->localPosition = {(float)(i % 15) * 2.0f - 15.0f, 3.0f + (float)(i / 15), 0.0f};
            auto* mr = g->AddComponent<MeshRenderer>(); mr->mesh = Mesh::Cube(1.0f);
            g->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
            g->AddComponent<Rigidbody3D>();
        }
        for (int i = 0; i < 60; ++i) s.CreateGameObject("Rig" + std::to_string(i))->AddComponent<ModelAnimator>();
        for (int i = 0; i < 12; ++i) {
            auto* au = s.CreateGameObject("Sound" + std::to_string(i))->AddComponent<AudioSource>();
            au->clipPath = "Assets/Audio/clip" + std::to_string(i % 6) + ".wav";
        }
        for (int i = 0; i < 20; ++i) {   // a real graph from the examples on each
            auto* vc = s.CreateGameObject("Script" + std::to_string(i))->AddComponent<VisualScriptComponent>();
            vc->LoadFromFile((fs::path(OKAY_EXAMPLES_DIR) / "visual_script" / "playground" / "combat.okayvs").string());
        }
        auto* ns = s.CreateGameObject("Nav Mesh")->AddComponent<NavMeshSurface>();
        ns->bakeBeforePlay = true;
        for (int i = 0; i < 20; ++i) {
            GameObject* g = s.CreateGameObject("Agent" + std::to_string(i));
            g->transform->localPosition = {(float)i - 10.0f, 0.1f, -36.0f};
        }
        // One ModelAnimator would otherwise pose nothing; this is the scan cost per object (no clip files here).
        std::printf("\nSynthetic scene: %zu objects\n", s.Objects().size());

        std::vector<Row> rows;
        NavMeshSurface::SetAssetBaseDir(root.string());
        rows.push_back({"Nav bake (NavigationBeforePlay, editor only)", Timed([&] { std::string e; ns->Bake(s, nullptr, &e); })});
        std::string snap;
        rows.push_back({"SceneSerializer::Serialize (undo snapshot)", Timed([&] { snap = SceneSerializer::Serialize(s); })});
        rows.push_back({"PoseCapture::ImportFolder x animators", Timed([&] {
            for (const auto& up : s.Objects()) if (auto* ma = up->GetComponent<ModelAnimator>()) PoseCapture::ImportFolder(*ma, (root / "Assets").string()); })});
        rows.push_back({"LoadSceneAudio (decode+resample)", Timed([&] { LoadSceneAudio(s, root.string(), AudioMixer::sampleRate); })});
        rows.push_back({"vs::ValidateSceneGraphs", Timed([&] { vs::ValidateSceneGraphs(s, root.string()); })});
        Print("Synthetic scene, phases timed one by one (cold)", rows);

        double playMs = 0;
        const auto ticks = PlayAndTick(ed, 120, &playMs);
        std::printf("\n  EditorState::Play() total (incl. the phases above, warm caches): %.2f ms\n", playMs);
        PrintFrames(ticks);
        std::printf("  first tick %.2f ms, total of first 120 ticks %.1f ms\n", ticks[0], [&] { double a = 0; for (double x : ticks) a += x; return a; }());
        ed.Stop();
        double play2 = 0;
        const auto ticks2 = PlayAndTick(ed, 5, &play2);
        std::printf("  Play() again after Stop (decoded audio cached by the engine): %.2f ms; its first tick %.2f ms\n", play2, ticks2[0]);
        ed.Stop();
        CHECK(playMs < 20000.0);   // generous guard only
    }

    // ---- Every example scene: Play() and the first ticks ----
    {
        std::vector<fs::path> scenes;
        for (const auto& e : fs::recursive_directory_iterator(fs::path(OKAY_EXAMPLES_DIR), ec))
            if (e.is_regular_file() && e.path().extension() == ".okayscene") scenes.push_back(e.path());
        std::sort(scenes.begin(), scenes.end());
        struct R { std::string name; std::size_t objs; double play, first, rest; };
        std::vector<R> rs;
        for (const auto& p : scenes) {
            okay::editor::EditorState ed;
            std::string err;
            if (!ed.Load(p.string(), &err)) continue;
            R r{p.parent_path().filename().string() + "/" + p.filename().string(), ed.scene().Objects().size(), 0, 0, 0};
            const auto t = PlayAndTick(ed, 30, &r.play);
            r.first = t[0];
            for (std::size_t i = 1; i < t.size(); ++i) r.rest = std::max(r.rest, t[i]);
            ed.Stop();
            rs.push_back(r);
        }
        std::sort(rs.begin(), rs.end(), [](const R& a, const R& b) { return a.play + a.first > b.play + b.first; });
        std::printf("\n== Example scenes (slowest 12 of %zu): objects, Play() ms, first tick ms, worst later tick ms ==\n", rs.size());
        for (std::size_t i = 0; i < rs.size() && i < 12; ++i)
            std::printf("  %-44s %5zu objs  play %8.2f  first %8.2f  worst-later %8.2f\n", rs[i].name.c_str(), rs[i].objs, rs[i].play, rs[i].first, rs[i].rest);
        for (const auto& r : rs) CHECK(r.play < 20000.0);
    }
    TEST_MAIN_RESULT();
}
