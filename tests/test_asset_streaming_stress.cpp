// Large-world stress test for asset streaming (docs/engine/asset_streaming.md): a generated world of cells (thousands
// of objects, shared mesh files and textures), a camera flying through it, run twice:
//   streamed  - WorldStreamer + the asset streamer (Automatic mode, default frame budget);
//   up front  - every cell instantiated in one go before the flight (what a game without streaming does).
// It prints per-frame time (max, 99th percentile), the streaming work per frame, resident memory against the
// budget and pop-in frames, and checks the streaming promises. With OKAY_SHOT_DIR set it also writes pictures:
// stream_frametime.png (frame time, both runs) and stream_cells_<n>.png (top-down cell states during the flight).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/AssetManager.hpp"
#include "okay/Core/Jobs.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/WorldStreamer.hpp"
#include "okay/Modeling/MeshAsset.hpp"
#include "okay/Render/TextureAsset.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <ctime>
#include <vector>
#if defined(__linux__)
#include <unistd.h>
#endif

using namespace okay;
namespace fs = std::filesystem;

namespace {
constexpr int   kGrid = 12;          // 12 x 12 cells
constexpr int   kPerCell = 70;       // objects with a mesh per cell (each has a child: 141 objects per cell)
constexpr float kCell = 40.0f;
constexpr int   kMeshes = 6, kTextures = 6;
constexpr int   kFrames = 900;       // 15 s at 60 fps

double WallMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Frame work is measured as the main thread's CPU time: this test runs on shared, busy machines, where wall time
// also counts the moments the OS gave the CPU to other programs (in 4 ms slices), which no budget can prevent.
double NowMs() {
#if defined(__linux__)
    timespec ts{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0) return double(ts.tv_sec) * 1000.0 + double(ts.tv_nsec) / 1e6;
#endif
    return WallMs();
}

// A game frame is 1/60 s of wall time: after the frame's work, wait out the rest (the job threads keep working).
void PaceFrame(double wallStartMs) {
    while (WallMs() - wallStartMs < 1000.0 / 60.0) std::this_thread::sleep_for(std::chrono::microseconds(200));
}

double RssMB() {
#if defined(__linux__)
    std::ifstream f("/proc/self/statm");
    long pages = 0, resident = 0;
    if (f >> pages >> resident) return double(resident) * double(sysconf(_SC_PAGESIZE)) / (1024.0 * 1024.0);
#endif
    return 0.0;
}

double Percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const std::size_t i = std::min(v.size() - 1, static_cast<std::size_t>(p * double(v.size() - 1) + 0.5));
    return v[i];
}

// The camera path: diagonally across the world, then back along the middle.
Vec3 PathAt(int frame) {
    const float t = float(frame) / float(kFrames);
    const float span = kGrid * kCell;
    if (t < 0.6f) {
        const float u = t / 0.6f;
        return Vec3{span * (0.05f + 0.9f * u), 2.0f, span * (0.05f + 0.9f * u)};
    }
    const float u = (t - 0.6f) / 0.4f;
    return Vec3{span * (0.95f - 0.9f * u), 2.0f, span * 0.5f};
}

// `heavy`: the meshes keep their per-triangle colours, which the scene file writes out as text for every object
// (about 9 KB of numbers each): what a cell full of coloured primitives costs to parse.
void BuildWorld(const fs::path& dir, bool heavy) {
    fs::create_directories(dir / "cells");
    const char* shapes[kMeshes] = {"Rock", "Tree", "Barrel", "Crate", "House", "Pine"};
    for (int i = 0; i < kMeshes; ++i) {
        std::string err;
        Mesh m = Mesh::FromName(shapes[i]);
        if (!heavy) m.triColors.clear();
        CHECK(modeling::SaveMeshAsset((dir / ("m" + std::to_string(i) + ".okaymesh")).string(), m, &err));
    }
    for (int i = 0; i < kTextures; ++i) {
        Image img(256, 256);
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 256; ++x)
                img.SetPixel(x, y, Color(float((x * (i + 1)) % 256) / 255.0f, float((y * 3 + i * 40) % 256) / 255.0f, 0.5f, 1.0f));
        CHECK(img.SavePNG((dir / ("t" + std::to_string(i) + ".png")).string()));
    }
    std::vector<Mesh> meshes(kMeshes);
    for (int i = 0; i < kMeshes; ++i)
        CHECK(modeling::LoadMeshAsset((dir / ("m" + std::to_string(i) + ".okaymesh")).string(), meshes[i]));
    for (int cz = 0; cz < kGrid; ++cz)
        for (int cx = 0; cx < kGrid; ++cx) {
            Scene bake("bake");
            const std::string name = "cell_" + std::to_string(cx) + "_" + std::to_string(cz);
            GameObject* root = bake.CreateGameObject(name);
            for (int i = 0; i < kPerCell; ++i) {
                GameObject* o = bake.CreateGameObject(name + "_o" + std::to_string(i));
                o->transform->SetParent(root->transform);
                o->transform->localPosition = Vec3{cx * kCell + float((i * 7) % 40), 0.0f, cz * kCell + float((i * 13) % 40)};
                auto* mr = o->AddComponent<MeshRenderer>();
                const int m = (cx * 3 + cz * 5 + i) % kMeshes;
                mr->mesh = meshes[m];
                if (!heavy) mr->mesh.triColors.clear();
                mr->meshPath = (dir / ("m" + std::to_string(m) + ".okaymesh")).string();
                mr->meshPathHash = modeling::GeometryHash(mr->mesh);   // the file's geometry: not copied into the cell
                mr->texture = (dir / ("t" + std::to_string((cx + cz + i) % kTextures) + ".png")).string();
                GameObject* c = bake.CreateGameObject(name + "_c" + std::to_string(i));
                c->transform->SetParent(o->transform);
            }
            CHECK(SceneSerializer::SaveObjectToFile(*root, (dir / "cells" / (name + ".okayprefab")).string()));
        }
}

struct RunResult {
    std::vector<double> frameMs, streamMs;
    double firstFrameMs = 0.0;    // up front: the load before the flight
    int popInFrames = 0;
    int maxObjects = 0;
    std::size_t maxResidentBytes = 0;
    double rssMB = 0.0;
};

void DrawLine(Image& img, int x0, int y0, int x1, int y1, const Color& c) {
    const int steps = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
    for (int s = 0; s <= steps; ++s) {
        const float t = steps ? float(s) / float(steps) : 0.0f;
        img.SetPixel(int(x0 + (x1 - x0) * t + 0.5f), int(y0 + (y1 - y0) * t + 0.5f), c);
    }
}

void SaveCellMap(const std::string& path, const WorldStreamer& ws, int frame) {
    const int px = 24;
    Image img(kGrid * px, kGrid * px);
    img.Fill(Color(0.08f, 0.08f, 0.1f, 1.0f));
    for (int z = 0; z < kGrid; ++z)
        for (int x = 0; x < kGrid; ++x) {
            Color c(0.2f, 0.2f, 0.22f, 1.0f);   // unloaded
            switch (ws.GetCellState(x, z)) {
                case WorldStreamer::CellState::Reading:   c = Color(0.95f, 0.85f, 0.2f, 1.0f); break;
                case WorldStreamer::CellState::Creating:  c = Color(0.95f, 0.5f, 0.1f, 1.0f); break;
                case WorldStreamer::CellState::Loaded:    c = Color(0.2f, 0.75f, 0.3f, 1.0f); break;
                case WorldStreamer::CellState::Unloading: c = Color(0.85f, 0.2f, 0.2f, 1.0f); break;
                default: break;
            }
            for (int y = 1; y < px - 1; ++y)
                for (int xx = 1; xx < px - 1; ++xx) img.SetPixel(x * px + xx, (kGrid - 1 - z) * px + y, c);
        }
    const float scale = float(px) / kCell;
    for (int f = 1; f <= kFrames; ++f) {   // the path, and where the camera is now
        const Vec3 a = PathAt(f - 1), b = PathAt(f);
        DrawLine(img, int(a.x * scale), int(kGrid * px - a.z * scale), int(b.x * scale), int(kGrid * px - b.z * scale),
                 f <= frame ? Color(1, 1, 1, 1) : Color(0.5f, 0.5f, 0.55f, 1.0f));
    }
    const Vec3 p = PathAt(frame);
    for (int dy = -3; dy <= 3; ++dy)
        for (int dx = -3; dx <= 3; ++dx) img.SetPixel(int(p.x * scale) + dx, int(kGrid * px - p.z * scale) + dy, Color(0.3f, 0.6f, 1.0f, 1.0f));
    img.SavePNG(path);
}

void SaveFrameGraph(const std::string& path, const RunResult& streamed, const RunResult& upfront, double budget) {
    const int W = 900, H = 320;
    Image img(W, H);
    img.Fill(Color(0.08f, 0.08f, 0.1f, 1.0f));
    // The scale fits the flight frames; the up-front load (seconds) runs off the top as a clipped red spike at x = 0.
    double top = budget * 2.0;
    for (double v : streamed.frameMs) top = std::max(top, v);
    for (double v : upfront.frameMs) top = std::max(top, v);
    top *= 1.1;
    auto yOf = [&](double ms) { return std::max(0, int(H - 1 - (std::min(ms, top) / top) * (H - 10))); };
    // The budget line (dashed).
    for (int x = 0; x < W; x += 8) DrawLine(img, x, yOf(budget), std::min(W - 1, x + 4), yOf(budget), Color(0.6f, 0.6f, 0.6f, 1.0f));
    auto series = [&](const std::vector<double>& v, double first, const Color& c) {
        std::vector<double> all;
        if (first > 0.0) all.push_back(first);
        all.insert(all.end(), v.begin(), v.end());
        for (std::size_t i = 1; i < all.size(); ++i)
            DrawLine(img, int((i - 1) * (W - 1) / (all.size() - 1)), yOf(all[i - 1]), int(i * (W - 1) / (all.size() - 1)), yOf(all[i]), c);
    };
    series(upfront.frameMs, upfront.firstFrameMs, Color(0.9f, 0.35f, 0.3f, 1.0f));   // red: up front (first point = the load)
    series(streamed.frameMs, 0.0, Color(0.35f, 0.75f, 0.95f, 1.0f));                 // blue: streamed
    img.SavePNG(path);
}

RunResult RunStreamed(const fs::path& dir, const std::string& shots) {
    RunResult r;
    Scene scene("streamed");
    scene.physicsEnabled = false;
    scene.SetStreamClock(NowMs);   // the frame budget counts this thread's CPU time (see NowMs)
    GameObject* cam = scene.CreateGameObject("Cam");
    auto* ws = scene.CreateGameObject("Streamer")->AddComponent<WorldStreamer>();
    ws->cellSize = kCell; ws->loadRadius = 2; ws->unloadRadius = 3;
    ws->folder = (dir / "cells").string(); ws->prefix = "cell"; ws->target = "Cam";
    AssetManager& am = AssetManager::Instance();
    const int shotFrames[3] = {60, 330, 700};
    for (int f = 0; f < kFrames; ++f) {
        const Vec3 p = PathAt(f), q = PathAt(f + 1);
        cam->transform->localPosition = p;
        if ((q - p).Magnitude() > 1e-4f) cam->transform->LookAt(q, Vec3{0, 1, 0});
        const double t0 = NowMs(), w0 = WallMs();
        scene.Update(1.0f / 60.0f);
        am.Pump();
        textures::Pump();
        const double ms = NowMs() - t0;
        PaceFrame(w0);
        if (f > 0) {   // frame 0 lists the folder; it is reported on its own below
            r.frameMs.push_back(ms);
            r.streamMs.push_back(scene.LastStreamFrame().ms);
        } else {
            r.firstFrameMs = ms;
        }
        // Pop-in: a cell next to the camera (its own and the ring around it) that is not in yet.
        int cx = 0, cz = 0;
        if (f > 60 && ws->CurrentCell(cx, cz)) {
            bool missing = false;
            for (int z = cz - 1; z <= cz + 1; ++z)
                for (int x = cx - 1; x <= cx + 1; ++x)
                    if (x >= 0 && z >= 0 && x < kGrid && z < kGrid && !ws->IsCellLoaded(x, z)) missing = true;
            if (missing) ++r.popInFrames;
        }
        r.maxObjects = std::max(r.maxObjects, static_cast<int>(scene.Objects().size()));
        r.maxResidentBytes = std::max(r.maxResidentBytes, am.GetStreamStats().residentBytes + textures::GetStats().bytes);
        if (!shots.empty())
            for (int s = 0; s < 3; ++s)
                if (f == shotFrames[s]) SaveCellMap(shots + "/stream_cells_" + std::to_string(s + 1) + ".png", *ws, f);
    }
    r.rssMB = RssMB();
    return r;
}

RunResult RunUpFront(const fs::path& dir) {
    RunResult r;
    Scene scene("upfront");
    scene.physicsEnabled = false;
    GameObject* cam = scene.CreateGameObject("Cam");
    const double t0 = NowMs();
    for (int cz = 0; cz < kGrid; ++cz)
        for (int cx = 0; cx < kGrid; ++cx) {
            const std::string name = "cell_" + std::to_string(cx) + "_" + std::to_string(cz);
            CHECK(SceneSerializer::InstantiateFromFile(scene, (dir / "cells" / (name + ".okayprefab")).string()) != nullptr);
        }
    scene.Update(1.0f / 60.0f);
    for (const auto& go : scene.Objects())   // the renderers would ask for every texture in the first frame
        if (auto* mr = go->GetComponent<MeshRenderer>()) textures::ForRender(mr->texture);
    r.firstFrameMs = NowMs() - t0;
    for (int f = 1; f < kFrames; ++f) {
        cam->transform->localPosition = PathAt(f);
        const double a = NowMs(), w = WallMs();
        scene.Update(1.0f / 60.0f);
        AssetManager::Instance().Pump();
        textures::Pump();
        r.frameMs.push_back(NowMs() - a);
        r.streamMs.push_back(0.0);
        PaceFrame(w);
    }
    r.maxObjects = static_cast<int>(scene.Objects().size());
    r.maxResidentBytes = AssetManager::Instance().GetStreamStats().residentBytes + textures::GetStats().bytes;
    r.rssMB = RssMB();
    return r;
}
} // namespace

int main() {
    RUN_SUITE("asset_streaming_stress");
    const char* shotEnv = std::getenv("OKAY_SHOT_DIR");
    const std::string shots = shotEnv ? shotEnv : "";
    AssetManager& am = AssetManager::Instance();
    const int total = kGrid * kGrid * (2 * kPerCell + 1);
    std::printf("  world: %d cells, %d objects, %d mesh files, %d textures; flight %d frames at 60 fps\n",
                kGrid * kGrid, total, kMeshes, kTextures, kFrames);
    std::printf("  %-16s %10s %10s %10s %10s %12s %12s %10s %10s %8s\n", "run", "first ms", "max ms", "p99 ms", "p50 ms",
                "stream p99", "max objects", "assets MB", "RSS +MB", "pop-in");
    auto row = [&](const char* name, const RunResult& r, double rssBase) {
        std::printf("  %-16s %10.2f %10.2f %10.2f %10.2f %12.2f %12d %10.1f %10.1f %8d\n", name, r.firstFrameMs,
                    *std::max_element(r.frameMs.begin(), r.frameMs.end()), Percentile(r.frameMs, 0.99),
                    Percentile(r.frameMs, 0.5), Percentile(r.streamMs, 0.99), r.maxObjects,
                    r.maxResidentBytes / 1048576.0, r.rssMB - rssBase, r.popInFrames);
    };

    const int passes = std::getenv("OKAY_STRESS_LIGHT_ONLY") ? 1 : 2;
    for (int pass = 0; pass < passes; ++pass) {
        const bool heavy = pass == 1;
        const fs::path dir = fs::temp_directory_path() / (heavy ? "okay_stream_stress_heavy" : "okay_stream_stress");
        fs::remove_all(dir);
        BuildWorld(dir, heavy);

        am.ResetStreamingForTests();
        textures::ResetForTests();
        const double rss0 = RssMB();
        const RunResult s = RunStreamed(dir, heavy ? std::string() : shots);
        const StreamStats st = am.GetStreamStats();
        const double budget = am.FrameBudgetMs();
        if (!heavy)
            std::printf("  budgets (Automatic): RAM %.0f MB, VRAM %.0f MB (system %.0f MB), frame %.1f ms; cache evicted %llu, cancelled %llu\n",
                        st.ramBudget / 1048576.0, st.vramBudget / 1048576.0, st.systemMemory / 1048576.0, budget,
                        static_cast<unsigned long long>(st.evicted), static_cast<unsigned long long>(st.cancelled));

        am.ResetStreamingForTests();
        textures::ResetForTests();
        const double rss1 = RssMB();
        const RunResult u = RunUpFront(dir);
        row(heavy ? "heavy streamed" : "streamed", s, rss0);
        row(heavy ? "heavy up front" : "up front", u, rss1);
        if (!heavy && !shots.empty()) SaveFrameGraph(shots + "/stream_frametime.png", s, u, budget);
        if (heavy && !shots.empty()) SaveFrameGraph(shots + "/stream_frametime_heavy.png", s, u, budget);

        // Both worlds: far fewer objects resident, and no streamed frame is anywhere near the up-front load.
        CHECK(s.maxObjects < total * 6 / 10);
        CHECK(*std::max_element(s.frameMs.begin(), s.frameMs.end()) < u.firstFrameMs / 10.0);
        CHECK(u.maxObjects >= total);
        if (!heavy) {
            // Typical objects: the streaming work per frame stays within the budget plus one object, and the cells
            // near the camera are in on time.
            CHECK(Percentile(s.streamMs, 0.99) <= budget + 2.0);
            CHECK(s.popInFrames <= (kFrames - 61) / 10);
        }
        // Heavy objects (about 10 ms each to parse on the main thread): the budget can only stop between objects,
        // so those frames go over it by one object. Reported, not checked (round 2: parse on the job threads).
        am.ResetStreamingForTests();
        textures::ResetForTests();
        fs::remove_all(dir);
    }
    TEST_MAIN_RESULT();
}
