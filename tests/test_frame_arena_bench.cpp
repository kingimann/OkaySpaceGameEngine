// Benchmark and check for the frame arena in the Scene's per-frame path (docs/core_runtime_upgrade.md).
// Scene::Update builds a list of cameras every frame (ResolveMainCamera) and, on a frame with a mouse press, a
// snapshot of which components were on. Both used to be heap vectors; they now live in Scene's FrameArena.
// Part 1 times the real Scene::Update on a busy scene. Part 2 times the old and the new list pattern side by
// side (isolated, same sizes) so the saving is visible without noise from the rest of the frame.
// Timing is printed, never a pass/fail condition. The checks are that the arena stays small (no growth per frame).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/Memory.hpp"

#include <chrono>
#include <cstdio>
#include <vector>

using namespace okay;
using Clock = std::chrono::steady_clock;
static double MsSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

int main() {
    Scene scene;
    for (int i = 0; i < 3; ++i) {
        GameObject* g = scene.CreateGameObject("Cam" + std::to_string(i));
        g->AddComponent<Camera>()->main = (i == 0);
    }
    for (int i = 0; i < 2000; ++i) scene.CreateGameObject("Obj" + std::to_string(i));
    for (int i = 0; i < 5; ++i) scene.Update(0.016f);   // warm up
    const int frames = 3000;
    auto t = Clock::now();
    for (int i = 0; i < frames; ++i) scene.Update(0.016f);
    const double ms = MsSince(t);
    std::printf("Scene::Update, 2003 objects, 3 cameras: %.2f us per frame (%d frames)\n", ms * 1000.0 / frames, frames);
    CHECK(scene.mainCamera != nullptr);

    // Isolated: the camera list (3 cameras) built per frame the old way and the new way.
    std::vector<Camera*> cams;
    for (const auto& up : scene.Objects()) if (up) if (Camera* c = up->GetComponent<Camera>()) cams.push_back(c);
    CHECK(cams.size() == 3);
    const int reps = 2000000;
    volatile std::size_t sink = 0;
    t = Clock::now();
    for (int i = 0; i < reps; ++i) { std::vector<Camera*> v; for (Camera* c : cams) v.push_back(c); sink = sink + v.size(); }
    const double heapMs = MsSince(t);
    FrameArena arena(4096);
    t = Clock::now();
    for (int i = 0; i < reps; ++i) {
        ArenaScope sc(arena);
        std::vector<Camera*, ArenaAllocator<Camera*>> v{ArenaAllocator<Camera*>(&arena)};
        for (Camera* c : cams) v.push_back(c);
        sink = sink + v.size();
    }
    const double arenaMs = MsSince(t);
    std::printf("camera list, %d frames: heap vector %.1f ns/frame, frame arena %.1f ns/frame (%.2fx)\n", reps,
                heapMs * 1e6 / reps, arenaMs * 1e6 / reps, arenaMs > 0 ? heapMs / arenaMs : 0.0);
    // The click snapshot: one byte per component, 2003 components
    std::vector<char> src(2003, 1);
    const int reps2 = 200000;
    t = Clock::now();
    for (int i = 0; i < reps2; ++i) { std::vector<char> w; w.reserve(src.size()); for (char c : src) w.push_back(c); sink = sink + w.size(); }
    const double heap2 = MsSince(t);
    t = Clock::now();
    for (int i = 0; i < reps2; ++i) {
        ArenaScope sc(arena);
        std::vector<char, ArenaAllocator<char>> w{ArenaAllocator<char>(&arena)};
        w.reserve(src.size()); for (char c : src) w.push_back(c); sink = sink + w.size();
    }
    const double arena2 = MsSince(t);
    std::printf("click snapshot (2003 flags), %d frames: heap vector %.0f ns/frame, frame arena %.0f ns/frame (%.2fx)\n", reps2,
                heap2 * 1e6 / reps2, arena2 * 1e6 / reps2, arena2 > 0 ? heap2 / arena2 : 0.0);
    CHECK(arena.Reserved() < 1024 * 1024);   // the same few KB are reused, nothing grows per frame
    TEST_MAIN_RESULT();
}
