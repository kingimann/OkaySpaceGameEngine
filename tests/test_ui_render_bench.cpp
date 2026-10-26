// UI render performance acceptance (plan P0): 500 widgets in a 10k-object scene must
// draw with under 1 ms of UI CPU per frame, under 20 draw calls and no heap
// allocations once warmed up. Draws through ui2d::BatchPainter onto a counting
// backend (no GPU), so it measures the engine's own UI cost. Prints PASS/FAIL per
// criterion. The timing check only runs in optimized, non-sanitized builds.
#include "test_framework.hpp"
#include "okay/UI/UIBatcher.hpp"
#include "okay/UI/UIRenderer.hpp"
#include <Okay.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

// ---- Heap allocation counter (this test binary only) ----------------------------
static std::atomic<long> g_allocs{0};
void* operator new(std::size_t n) {
    g_allocs.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
    g_allocs.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace okay;
using namespace okay::ui2d;
using URect = okay::ui2d::Rect;

namespace {

// A backend that only counts what reaches it: the draw calls a real renderer would
// issue. It supports the atlas, so the batcher batches.
struct CountingBackend : Painter {
    int drawCalls = 0, triangles = 0, uploads = 0;
    int dummyTex = 0;
    void SetDrawColor(std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t) override {}
    void FillRect(const URect*) override { ++drawCalls; }
    void DrawRect(const URect*) override { ++drawCalls; }
    void DrawLine(int, int, int, int) override { ++drawCalls; }
    void DrawPoint(int, int) override { ++drawCalls; }
    bool clip = false; URect clipRect{0, 0, 0, 0};
    void SetClipRect(const URect* r) override { clip = r != nullptr; if (r) clipRect = *r; }
    void GetClipRect(URect* r) override { *r = clipRect; }
    bool IsClipEnabled() override { return clip; }
    void SetScale(float, float) override {}
    Tex LoadTexture(const std::string&) override { return nullptr; }
    Tex FontAtlas(TtfFont*) override { return nullptr; }
    void QueryTexture(Tex, int* w, int* h) override { *w = *h = 0; }
    void SetTextureColorMod(Tex, std::uint8_t, std::uint8_t, std::uint8_t) override {}
    void SetTextureAlphaMod(Tex, std::uint8_t) override {}
    void Copy(Tex, const URect*, const URect*) override { ++drawCalls; }
    void CopyF(Tex, const URect*, const FRect*) override { ++drawCalls; }
    void CopyEx(Tex, const URect*, const URect*, double, const Point*, int) override { ++drawCalls; }
    void CopyExF(Tex, const URect*, const FRect*, double, const FPoint*, int) override { ++drawCalls; }
    void Geometry(Tex, const Vertex*, int, const int*, int ni) override { ++drawCalls; triangles += ni / 3; }
    Tex UploadAtlas(const Image&, int, Tex existing) override { ++uploads; return existing ? existing : &dummyTex; }
};

void Report(const char* what, bool ok, const char* detail) {
    std::printf("  %s %s (%s)\n", ok ? "PASS" : "FAIL", what, detail);
}

} // namespace

int main() {
    RUN_SUITE("ui_render_bench");

    const std::string tex = std::string(OKAY_TEST_OUTPUT_DIR) + "/ui_bench_texture.png";
    {
        Image img(32, 24);
        for (int y = 0; y < 24; ++y)
            for (int x = 0; x < 32; ++x) img.SetPixel(x, y, Color::FromBytes((unsigned char)(x * 8), (unsigned char)(y * 10), 160, 255));
        CHECK(img.SavePNG(tex));
    }

    // ---- 10k objects, 500 of them widgets under one canvas ----
    Scene s("bench"); s.physicsEnabled = false;
    GameObject* cv = s.CreateGameObject("canvas");
    cv->AddComponent<Canvas>()->scaleMode = Canvas::ScaleMode::ConstantPixelSize;
    int made = 1;
    auto at = [](int i, float& x, float& y) { x = 10.0f + (i % 20) * 95.0f; y = 10.0f + (i / 20) * 42.0f; };
    for (int i = 0; i < 500; ++i) {
        GameObject* g = s.CreateGameObject("w");
        g->transform->SetParent(cv->transform, false);
        ++made;
        float x, y; at(i, x, y);
        switch (i % 10) {
            case 0: case 1: { auto* p = g->AddComponent<UIPanel>(); p->position = Vec2{x, y}; p->size = Vec2{90, 38};
                              p->borderWidth = (i % 3 == 0) ? 1.0f : 0.0f; break; }
            case 2: case 3: { auto* b = g->AddComponent<UIButton>(); b->position = Vec2{x, y}; b->size = Vec2{90, 38};
                              b->label = (i % 4 == 0) ? "Start a new game" : "Play"; break; }
            case 4: case 5: { auto* t = g->AddComponent<TextRenderer>(); t->screenSpace = true; t->screenPos = Vec2{x, y};
                              t->pixelSize = 1.5f; t->text = (i % 4 == 1) ? "Score: 123456 points so far" : "HP 100"; break; }
            case 6: { auto* pb = g->AddComponent<UIProgressBar>(); pb->position = Vec2{x, y}; pb->size = Vec2{90, 12}; pb->value = 0.4f; break; }
            case 7: { auto* im = g->AddComponent<UIImage>(); im->position = Vec2{x, y}; im->size = Vec2{32, 24}; im->texture = tex; break; }
            case 8: { auto* tg = g->AddComponent<UIToggle>(); tg->position = Vec2{x, y}; tg->label = "Sound"; break; }
            default: { auto* sl = g->AddComponent<UISlider>(); sl->position = Vec2{x, y}; sl->size = Vec2{90, 16}; break; }
        }
    }
    for (; made < 10000; ++made) s.CreateGameObject("prop");
    CHECK((int)s.Objects().size() == 10000);

    CountingBackend backend;
    BatchPainter batch(backend);
    auto frame = [&]() {
        backend.drawCalls = 0; backend.triangles = 0;
        RenderSceneUI(s, batch, 1920, 1080);
        batch.Flush();
    };
    for (int i = 0; i < 5; ++i) frame();   // warm up: caches, atlas, vertex buffers

    // ---- Draw calls ----
    frame();
    const int calls = backend.drawCalls, tris = backend.triangles;
    char d[160];
    std::snprintf(d, sizeof(d), "%d draw calls, %d triangles", calls, tris);
    Report("draw calls < 20", calls < 20, d);
    CHECK(calls > 0 && calls < 20);
    CHECK(tris > 500);   // the widgets really drew

    // ---- Steady-state allocations ----
    const long a0 = g_allocs.load();
    for (int i = 0; i < 50; ++i) frame();
    const long allocs = g_allocs.load() - a0;
    std::snprintf(d, sizeof(d), "%ld allocations over 50 frames", allocs);
    Report("0 allocations in steady state", allocs == 0, d);
    CHECK(allocs == 0);

    // ---- CPU time ----
    std::vector<double> ms;
    ms.reserve(200);
    for (int i = 0; i < 200; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        frame();
        ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
    std::sort(ms.begin(), ms.end());
    const double median = ms[ms.size() / 2];
    std::snprintf(d, sizeof(d), "median %.3f ms per frame", median);
#if defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
    Report("UI CPU < 1 ms per frame", median < 1.0, d);
    CHECK(median < 1.0);
#else
    std::printf("  SKIP UI CPU < 1 ms per frame (unoptimized or sanitized build; %s)\n", d);
#endif

    // ---- The caches follow the scene: a new widget shows up, a removed one goes ----
    {
        frame();
        const int before = backend.triangles;
        GameObject* g = s.CreateGameObject("late");
        g->transform->SetParent(cv->transform, false);
        auto* p = g->AddComponent<UIPanel>(); p->position = Vec2{1800, 1000}; p->size = Vec2{100, 60};
        p->shape = UIShape::Rectangle;
        frame();
        CHECK(backend.triangles > before);
        g->RemoveComponent<UIPanel>();
        frame();
        CHECK(backend.triangles == before);
        // A component added to an existing object counts too.
        s.Objects()[5000]->AddComponent<UIPanel>()->shape = UIShape::Rectangle;
        frame();
        CHECK(backend.triangles > before);
    }

    TEST_MAIN_RESULT();
}
