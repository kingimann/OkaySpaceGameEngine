// Printed benchmark of the entity/component system (docs/ecs_upgrade.md).
// Written from the plain description of what each operation costs; nothing is copied from another engine.
//   - Prints one line per measurement (milliseconds or nanoseconds). Times are NOT asserted (machines differ);
//     only the counts the loops must reach are checked, so the run is deterministic.
//   - OKAY_ECS_BENCH_N=<objects> changes the size (default 10000).
#include "test_framework.hpp"
#include <Okay.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>

using namespace okay;
using Clock_ = std::chrono::steady_clock;

// Counts every heap allocation of the process so "memory per object" is measured, not guessed.
static std::size_t g_bytes = 0, g_allocs = 0;
void* operator new(std::size_t n) { g_bytes += n; ++g_allocs; if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace {
long g_updates = 0;
struct A : Behaviour { void Update(float) override { ++g_updates; } };
struct B : Behaviour { void Update(float) override { ++g_updates; } };
struct C : Behaviour { void Update(float) override { ++g_updates; } };
struct Needle : Behaviour {};

double MsSince(Clock_::time_point t) { return std::chrono::duration<double, std::milli>(Clock_::now() - t).count(); }
double NsSince(Clock_::time_point t) { return std::chrono::duration<double, std::nano>(Clock_::now() - t).count(); }
}

int main() {
    int N = 10000;
    if (const char* e = std::getenv("OKAY_ECS_BENCH_N")) N = std::max(100, std::atoi(e));
    std::printf("ECS benchmark, %d objects, 3 components each\n", N);
    Scene scene("bench");

    std::size_t b0 = g_bytes, a0 = g_allocs;
    auto t = Clock_::now();
    std::vector<GameObject*> objs;
    for (int i = 0; i < N; ++i) objs.push_back(scene.CreateGameObject("Obj"));
    std::printf("create %d objects            : %8.2f ms\n", N, MsSince(t));
    std::size_t bObj = g_bytes - b0, aObj = g_allocs - a0;
    b0 = g_bytes; a0 = g_allocs;
    t = Clock_::now();
    for (GameObject* g : objs) { g->AddComponent<A>(); g->AddComponent<B>(); g->AddComponent<C>(); }
    std::printf("add 3 components to each     : %8.2f ms\n", MsSince(t));
    std::printf("memory per object (bare)     : %8.1f bytes in %.1f allocations\n", double(bObj) / N, double(aObj) / N);
    std::printf("memory per component         : %8.1f bytes in %.1f allocations\n", double(g_bytes - b0) / (3.0 * N), double(g_allocs - a0) / (3.0 * N));
    objs.back()->AddComponent<Needle>();   // the worst place for a search: the last object

    t = Clock_::now();
    scene.Start();
    std::printf("first Start (Awake+Start)    : %8.2f ms\n", MsSince(t));

    // GetComponent<T>: first / last of 4 components on one object.
    GameObject* o = objs.back();
    const int reps = 2000000;
    volatile void* sink = nullptr;
    t = Clock_::now();
    for (int i = 0; i < reps; ++i) sink = o->GetComponent<A>();
    std::printf("GetComponent<A> (1st of 3)   : %8.1f ns/call\n", NsSince(t) / reps);
    t = Clock_::now();
    for (int i = 0; i < reps; ++i) sink = o->GetComponent<Needle>();
    std::printf("GetComponent<Needle> (last)  : %8.1f ns/call\n", NsSince(t) / reps);
    (void)sink;

    // Scene-wide queries.
    t = Clock_::now();
    Needle* n = scene.FindObjectOfType<Needle>();
    std::printf("FindObjectOfType<Needle> cold: %8.3f ms\n", MsSince(t));
    CHECK(n != nullptr);
    t = Clock_::now();
    for (int i = 0; i < 100; ++i) n = scene.FindObjectOfType<Needle>();
    std::printf("FindObjectOfType<Needle> warm: %8.3f ms/call\n", MsSince(t) / 100);
    t = Clock_::now();
    std::size_t total = 0;
    for (int i = 0; i < 100; ++i) total += scene.FindObjectsOfType<A>().size();
    std::printf("FindObjectsOfType<A> warm    : %8.3f ms/call (%d found)\n", MsSince(t) / 100, N);
    CHECK(total == std::size_t(N) * 100);

    // Update dispatch: 3 components per object.
    g_updates = 0;
    scene.Update(0.016f);   // first frame (any Start still owed)
    g_updates = 0;
    t = Clock_::now();
    for (int i = 0; i < 20; ++i) scene.Update(0.016f);
    std::printf("Scene::Update (%d x3 comps)  : %8.3f ms/frame\n", N, MsSince(t) / 20);
    CHECK(g_updates == 20L * 3 * N);

    // Remove: drop the C of every object.
    t = Clock_::now();
    for (GameObject* g : objs) g->RemoveComponent<C>();
    std::printf("RemoveComponent<C> on each   : %8.2f ms\n", MsSince(t));
    CHECK(scene.FindObjectsOfType<C>().empty());

    // Destroy everything: queued, then freed at the end of the frame.
    t = Clock_::now();
    for (GameObject* g : objs) scene.Destroy(g);
    scene.Update(0.016f);
    std::printf("Destroy %d + frame           : %8.2f ms\n", N, MsSince(t));
    CHECK(scene.Objects().empty() || scene.Stats().objects == 0);

    // Churn: create+destroy 10k small objects again (pool / allocator reuse).
    t = Clock_::now();
    for (int i = 0; i < N; ++i) { GameObject* g = scene.CreateGameObject("Churn"); g->AddComponent<A>(); scene.Destroy(g); }
    scene.Update(0.016f);
    std::printf("create+destroy churn %d      : %8.2f ms\n", N, MsSince(t));
    TEST_MAIN_RESULT();
}
