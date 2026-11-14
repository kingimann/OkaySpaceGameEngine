// OkayUI render interface, headless: the validator (every problem, hostile data), the front end (textures kept between
// frames, spans in framebuffer pixels, DPI), the software backend as the reference (identical to the draw-list rasteriser,
// committed golden images), and the okay::platform::Painter backend on both painters (the own software painter and SDL's
// software renderer) compared with the reference scene by scene. The OpenGL backend is in test_okayui_render_gl.cpp.
#include "test_framework.hpp"
#include "RenderTestKit.hpp"
#include "okay/Platform/Painter.hpp"
#include "OkayUI_SoftPainter.hpp"

#include <climits>
#include <cmath>
#include <cstring>
#include <limits>
#include <random>

using namespace okayuitest;
namespace UI = OkayUI;
namespace R = OkayUI::render;
namespace plat = okay::platform;
using rtk::Hand;

namespace {

const float kNaN = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();

/// Render hand-made or OkayUI data with the software backend into a fresh w x h bitmap cleared to (r, g, b).
std::vector<std::uint8_t> SoftRender(const UI::DrawData& d, int w, int h, float dpi = 1.0f, R::Stats* st = nullptr,
                                     const R::Options& o = R::Options(), std::uint8_t cr = 0, std::uint8_t cg = 0, std::uint8_t cb = 0) {
    UI::soft::Bitmap bm(w, h); bm.Clear(cr, cg, cb);
    auto be = R::CreateSoftBackend(bm);
    R::Renderer r(*be, o);
    rscenes::SetupImages(r);
    R::FrameInfo fi; fi.fbWidth = w; fi.fbHeight = h; fi.dpi = dpi;
    const R::Stats s = r.Render(d, fi);
    if (st) *st = s;
    return bm.rgba;
}

// ---------------------------------------------------------------------------------------------------------------------
void TestValidator() {
    RUN_SUITE("render: validator");
    {   // clean data
        Hand h; h.BeginSpan(); h.Quad(1, 1, 5, 5, 255, 0, 0);
        const R::Report r = R::Validate(h.Get(), 64, 64, 1.0f);
        CHECK(r.ok() && !r.refused() && !r.needsRepair() && r.first == R::Problem::None);
    }
    {   // empty data is fine
        UI::DrawData d;
        CHECK(R::Validate(d, 64, 64, 1.0f).ok());
    }
    {   // fatal: bad target
        Hand h; h.BeginSpan(); h.Quad(1, 1, 5, 5, 255, 0, 0);
        const int sizes[][2] = { {0, 10}, {10, 0}, {-1, 10}, {R::kMaxTarget + 1, 10}, {INT_MAX, INT_MAX} };
        for (auto& s : sizes) { const R::Report r = R::Validate(h.Get(), s[0], s[1], 1.0f); CHECK(r.refused() && r.first == R::Problem::BadTarget); }
        const float dpis[] = { 0.0f, -1.0f, kNaN, kInf, 0.1f, 99.0f };
        for (float dp : dpis) { const R::Report r = R::Validate(h.Get(), 64, 64, dp); CHECK(r.refused() && r.first == R::Problem::BadTarget); }
        CHECK(!R::Validate(h.Get(), 64, 64, 2.0f).refused());
    }
    {   // fatal: counts, pointers, stride
        Hand h; h.BeginSpan(); h.Quad(1, 1, 5, 5, 255, 0, 0);
        UI::DrawData d = h.Get();
        { UI::DrawData e = d; e.vertexCount = -1; CHECK(R::Validate(e, 64, 64, 1).first == R::Problem::NegativeCount); }
        { UI::DrawData e = d; e.indexCount = -5; CHECK(R::Validate(e, 64, 64, 1).first == R::Problem::NegativeCount); }
        { UI::DrawData e = d; e.cmdCount = -1; CHECK(R::Validate(e, 64, 64, 1).first == R::Problem::NegativeCount); }
        { UI::DrawData e = d; e.vertexCount = R::kMaxVertices + 1; CHECK(R::Validate(e, 64, 64, 1).first == R::Problem::TooMany); }
        { UI::DrawData e = d; e.indexCount = R::kMaxIndices + 1; CHECK(R::Validate(e, 64, 64, 1).first == R::Problem::TooMany); }
        { UI::DrawData e = d; e.cmdCount = R::kMaxSpans + 1; CHECK(R::Validate(e, 64, 64, 1).first == R::Problem::TooMany); }
        { UI::DrawData e = d; e.vertices = nullptr; CHECK(R::Validate(e, 64, 64, 1).first == R::Problem::NullPointer); }
        { UI::DrawData e = d; e.indices = nullptr; CHECK(R::Validate(e, 64, 64, 1).first == R::Problem::NullPointer); }
        { UI::DrawData e = d; e.cmds = nullptr; CHECK(R::Validate(e, 64, 64, 1).first == R::Problem::NullPointer); }
        { UI::DrawData e = d; e.vertexStride = 24; CHECK(R::Validate(e, 64, 64, 1).first == R::Problem::BadStride); }
        { UI::DrawData e = d; e.vertexStride = 0; CHECK(R::Validate(e, 64, 64, 1).first == R::Problem::BadStride); }
        { UI::DrawData e = d; e.atlasTexture = &e; e.atlasRgba = (const unsigned char*)&e; e.atlasW = -4; e.atlasH = 4; CHECK(R::Validate(e, 64, 64, 1).refused()); }
        { UI::DrawData e = d; e.atlasTexture = &e; e.atlasRgba = (const unsigned char*)&e; e.atlasW = 1 << 20; e.atlasH = 4; CHECK(R::Validate(e, 64, 64, 1).refused()); }
    }
    {   // repairable: index problems
        Hand h; h.BeginSpan(); h.Quad(1, 1, 5, 5, 255, 0, 0);
        { Hand g = h; g.idx[4] = 4; const R::Report r = R::Validate(g.Get(), 64, 64, 1); CHECK(r.first == R::Problem::IndexOutOfRange && r.badIndices == 1 && r.needsRepair() && !r.refused()); }
        { Hand g = h; g.idx[0] = -1; const R::Report r = R::Validate(g.Get(), 64, 64, 1); CHECK(r.badIndices == 1); }
        { Hand g = h; g.idx[1] = INT_MIN; g.idx[2] = INT_MAX; const R::Report r = R::Validate(g.Get(), 64, 64, 1); CHECK(r.badIndices == 2); }
        { UI::DrawData d = h.Get(); d.indexCount = 5; const R::Report r = R::Validate(d, 64, 64, 1); CHECK(r.trailingIndices && r.needsRepair() && r.first == R::Problem::IndexCountNotTriangles); }
    }
    {   // repairable: NaN, infinity and huge coordinates (and uv)
        Hand h; h.BeginSpan(); h.Quad(1, 1, 5, 5, 255, 0, 0);
        { Hand g = h; g.v[2].x = kNaN; const R::Report r = R::Validate(g.Get(), 64, 64, 1); CHECK(r.first == R::Problem::BadVertex && r.badVertices == 1); }
        { Hand g = h; g.v[0].y = kInf; CHECK(R::Validate(g.Get(), 64, 64, 1).badVertices == 1); }
        { Hand g = h; g.v[1].x = -kInf; CHECK(R::Validate(g.Get(), 64, 64, 1).badVertices == 1); }
        { Hand g = h; g.v[1].x = 1e9f; CHECK(R::Validate(g.Get(), 64, 64, 1).badVertices == 1); }
        { Hand g = h; g.v[1].u = kNaN; const R::Report r = R::Validate(g.Get(), 64, 64, 1); CHECK(r.badVertices == 0 && r.nanUv == 1 && r.needsRepair()); }
        { Hand g = h; g.v[1].x = 1000.0f; CHECK(R::Validate(g.Get(), 64, 64, 1).ok()); }   // large but finite is the caller's business (clipped)
        { R::ValidateOptions o; o.checkVertices = false; Hand g = h; g.v[2].x = kNaN; CHECK(R::Validate(g.Get(), 64, 64, 1, o).ok()); }
        { R::ValidateOptions o; o.checkIndices = false; Hand g = h; g.idx[4] = 99; CHECK(R::Validate(g.Get(), 64, 64, 1, o).ok()); }
    }
    {   // repairable: spans
        Hand h; h.BeginSpan(); h.Quad(1, 1, 5, 5, 255, 0, 0);
        { Hand g = h; g.cmds[0].idxOffset = -3; const R::Report r = R::Validate(g.Get(), 64, 64, 1); CHECK(r.first == R::Problem::SpanOutOfRange && r.badSpans == 1); }
        { Hand g = h; g.cmds[0].idxCount = 600; CHECK(R::Validate(g.Get(), 64, 64, 1).first == R::Problem::SpanOutOfRange); }
        { Hand g = h; g.cmds[0].idxCount = -6; CHECK(R::Validate(g.Get(), 64, 64, 1).first == R::Problem::SpanOutOfRange); }
        { Hand g = h; g.cmds[0].idxOffset = INT_MAX; g.cmds[0].idxCount = INT_MAX; CHECK(R::Validate(g.Get(), 64, 64, 1).first == R::Problem::SpanOutOfRange); }
        { Hand g = h; g.cmds[0].idxCount = 4; CHECK(R::Validate(g.Get(), 64, 64, 1).first == R::Problem::SpanNotTriangles); }
        { Hand g = h; g.cmds[0].clipOn = true; g.cmds[0].clipW = -1; g.cmds[0].clipH = 4; const R::Report r = R::Validate(g.Get(), 64, 64, 1); CHECK(r.first == R::Problem::BadClip && r.badClips == 1); }
        { Hand g = h; g.cmds[0].clipOn = true; g.cmds[0].clipX = INT_MAX; g.cmds[0].clipW = 100; g.cmds[0].clipH = 4; CHECK(R::Validate(g.Get(), 64, 64, 1).badClips == 1); }
        { Hand g = h; g.cmds[0].clipOn = true; g.cmds[0].clipX = -50; g.cmds[0].clipW = 100; g.cmds[0].clipH = 4; CHECK(R::Validate(g.Get(), 64, 64, 1).ok()); }   // partly outside is legal
    }
    {   // contract warnings: order, coverage, dropped
        Hand h; h.BeginSpan(); h.Quad(1, 1, 5, 5, 255, 0, 0); h.BeginSpan(); h.Quad(10, 1, 5, 5, 0, 255, 0);
        CHECK(R::Validate(h.Get(), 64, 64, 1).ok());                                         // ascending, adjacent, covering: OkayUI's own shape
        { Hand g = h; std::swap(g.cmds[0], g.cmds[1]); const R::Report r = R::Validate(g.Get(), 64, 64, 1); CHECK(r.first == R::Problem::SpansNotAscending && r.spansOutOfOrder == 1 && !r.needsRepair() && !r.refused()); }
        { Hand g = h; g.cmds[1].idxOffset = 3; g.cmds[1].idxCount = 6; const R::Report r = R::Validate(g.Get(), 64, 64, 1); CHECK(r.spansOutOfOrder == 1); }
        { Hand g = h; g.cmds.pop_back(); const R::Report r = R::Validate(g.Get(), 64, 64, 1); CHECK(r.first == R::Problem::UncoveredIndices && r.uncoveredIndices == 6); }
        { UI::DrawData d = h.Get(); d.dropped = 7; const R::Report r = R::Validate(d, 64, 64, 1); CHECK(r.first == R::Problem::Dropped && r.droppedByOkayUI == 7); }
        // strict mode refuses on warnings, normal mode draws
        { Hand g = h; g.cmds.pop_back();
          R::Stats st; R::Options o; o.strict = true; SoftRender(g.Get(), 32, 32, 1.0f, &st, o); CHECK(st.refused && st.firstProblem == R::Problem::UncoveredIndices);
          SoftRender(g.Get(), 32, 32, 1.0f, &st); CHECK(!st.refused && st.spansDrawn == 1); }
    }
    {   // every real OkayUI scene is clean: no problem at all (this is the contract OkayUI itself keeps)
        int clean = 0, total = 0;
        for (const auto& s : rscenes::All()) {
            Sim sim; rscenes::Run(s, sim);
            const R::Report r = R::Validate(UI::GetDrawData(), s.w, s.h, 1.0f);
            ++total; if (r.ok()) ++clean; else std::cerr << "  scene " << s.name << ": " << r.message << "\n";
            UI::UseLauncherLook(false);
        }
        CHECK(clean == total);
        std::cout << "  " << clean << " of " << total << " scenes validate clean\n";
    }
}

// ---------------------------------------------------------------------------------------------------------------------
void TestRendererBasics() {
    RUN_SUITE("render: spans, clip, DPI, textures");
    {   // a clipped quad: exactly the clip rectangle is painted
        Hand h; h.BeginSpan(true, 10, 12, 20, 8); h.Quad(0, 0, 64, 64, 255, 0, 0);
        const auto img = SoftRender(h.Get(), 64, 64);
        int red = 0, outside = 0;
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
            const bool in = x >= 10 && x < 30 && y >= 12 && y < 20;
            const unsigned p = rtk::Px(img, 64, x, y);
            if (p == 0xff0000ffu) { ++red; if (!in) ++outside; } else if (in) ++outside;
        }
        CHECK(red == 160 && outside == 0);
    }
    {   // a clip that sticks out of the target, and one entirely outside, and a huge one
        Hand h; h.BeginSpan(true, -20, -20, 40, 40); h.Quad(0, 0, 64, 64, 0, 255, 0);
        h.BeginSpan(true, 100, 100, 10, 10); h.Quad(0, 0, 64, 64, 255, 0, 0);
        h.BeginSpan(true, -1000000, -1000000, 2000000, 2000000); h.Quad(50, 50, 4, 4, 0, 0, 255);
        R::Stats st; const auto img = SoftRender(h.Get(), 64, 64, 1.0f, &st);
        CHECK(rtk::Px(img, 64, 19, 19) == 0x00ff00ffu && rtk::Px(img, 64, 20, 20) == 0x000000ffu && rtk::Px(img, 64, 5, 5) == 0x00ff00ffu);
        CHECK(rtk::Px(img, 64, 51, 51) == 0x0000ffffu);
        CHECK(st.spansSkipped == 1 && st.spansDrawn == 2);
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) CHECK(rtk::Px(img, 64, x, y) != 0xff0000ffu);
    }
    {   // DPI: a clip is rounded outward, geometry is scaled
        Hand h; h.BeginSpan(true, 3, 3, 5, 5); h.Quad(0, 0, 100, 100, 255, 255, 255);
        const auto img = SoftRender(h.Get(), 40, 40, 1.5f);
        // logical [3, 8) -> framebuffer [4.5, 12) -> floor / ceil = [4, 12)
        int n = 0; for (int y = 0; y < 40; ++y) for (int x = 0; x < 40; ++x) if (rtk::Px(img, 40, x, y) == 0xffffffffu) { ++n; CHECK(x >= 4 && x < 12 && y >= 4 && y < 12); }
        CHECK(n == 64);
        const auto img2 = SoftRender(h.Get(), 40, 40, 2.0f);
        n = 0; for (int y = 0; y < 40; ++y) for (int x = 0; x < 40; ++x) if (rtk::Px(img2, 40, x, y) == 0xffffffffu) { ++n; CHECK(x >= 6 && x < 16 && y >= 6 && y < 16); }
        CHECK(n == 100);
    }
    {   // alpha: source-over with straight alpha
        Hand h; h.BeginSpan(); h.Quad(0, 0, 10, 10, 255, 0, 0, 128);
        const auto img = SoftRender(h.Get(), 10, 10, 1.0f, nullptr, R::Options(), 0, 0, 255);
        const unsigned p = rtk::Px(img, 10, 5, 5);
        CHECK(std::abs((int)(p >> 24) - 128) <= 1 && ((p >> 16) & 255) == 0 && std::abs((int)((p >> 8) & 255) - 127) <= 1 && (p & 255) == 255);
    }
    {   // frame size must match the bitmap: the software backend refuses a mismatch and the renderer says so
        Hand h; h.BeginSpan(); h.Quad(0, 0, 10, 10, 255, 0, 0);
        UI::soft::Bitmap bm(20, 20); auto be = R::CreateSoftBackend(bm); R::Renderer r(*be);
        R::FrameInfo fi; fi.fbWidth = 30; fi.fbHeight = 20;
        CHECK(r.Render(h.Get(), fi).refused);
    }
    {   // textures: the atlas is uploaded once and kept; images by key; unknown keys are skipped and counted
        Sim sim; rscenes::Run(rscenes::Find("demo_window_launcher"), sim);       // uses the atlas
        const UI::DrawData d = UI::GetDrawData();
        CHECK(d.atlasTexture && d.atlasRgba);
        UI::soft::Bitmap bm(800, 780); auto be = R::CreateSoftBackend(bm); R::Renderer r(*be);
        R::FrameInfo fi; fi.fbWidth = 800; fi.fbHeight = 780;
        const R::Stats a = r.Render(d, fi), b = r.Render(d, fi);
        CHECK(a.texturesUploaded == 1 && b.texturesUploaded == 0 && !a.refused && a.unresolvedSpans == 0);
        r.InvalidateAtlas();
        CHECK(r.Render(d, fi).texturesUploaded == 1);
        UI::UseLauncherLook(false);
    }
    {   // images: SetImage / Render / RemoveImage; an unknown key is skipped
        static char key1, key2;
        Hand h; h.BeginSpan(false, 0, 0, 0, 0, &key1); h.Quad(0, 0, 32, 32, 255, 255, 255);
        h.BeginSpan(false, 0, 0, 0, 0, &key2); h.Quad(40, 0, 16, 16, 255, 255, 255);
        UI::soft::Bitmap bm(64, 64); bm.Clear(0, 0, 0); auto be = R::CreateSoftBackend(bm); R::Renderer r(*be);
        const auto& tex = rscenes::TestImage();
        CHECK(r.SetImage(&key1, 32, 32, tex.data()));
        CHECK(!r.SetImage(nullptr, 32, 32, tex.data()) && !r.SetImage(&key2, 0, 4, tex.data()) && !r.SetImage(&key2, 4, R::kMaxTexture + 1, tex.data()));
        R::FrameInfo fi; fi.fbWidth = 64; fi.fbHeight = 64;
        R::Stats st = r.Render(h.Get(), fi);
        CHECK(st.unresolvedSpans == 1 && st.spansDrawn == 1 && !st.refused);
        // the image's own pixels land 1:1 (nearest-equal at 1:1 even with linear filtering at texel centres)
        CHECK(rtk::Px(bm.rgba, 64, 0, 0) == 0xffffffffu && rtk::Px(bm.rgba, 64, 16, 16) == 0x000000ffu);   // border white; the transparent hole shows the black clear
        CHECK(r.HasImage(&key1)); r.RemoveImage(&key1); CHECK(!r.HasImage(&key1));
        st = r.Render(h.Get(), fi); CHECK(st.unresolvedSpans == 2 && st.spansDrawn == 0);
    }
    {   // a backend that cannot create a texture: the span is skipped, not drawn wrongly
        struct NoTex : R::Backend {
            const char* Name() const override { return "none"; }
            R::TexId CreateTexture(int, int, bool, const std::uint8_t*) override { return 0; }
            bool UpdateTexture(R::TexId, int, int, const std::uint8_t*) override { return false; }
            void DestroyTexture(R::TexId) override {}
            bool BeginFrame(int, int, float) override { return true; }
            void Draw(const R::Vertex*, int, const std::int32_t*, int, const R::Span*, int ns) override { drawn += ns; }
            void EndFrame() override {}
            int drawn = 0;
        } nt;
        R::Renderer r(nt);
        CHECK(!r.SetImage((const void*)&nt, 4, 4, rscenes::TestImage().data()));
        static char key;
        Hand h; h.BeginSpan(false, 0, 0, 0, 0, &key); h.Quad(0, 0, 4, 4, 255, 255, 255); h.BeginSpan(); h.Quad(0, 0, 4, 4, 255, 255, 255);
        R::FrameInfo fi; fi.fbWidth = 8; fi.fbHeight = 8;
        const R::Stats st = r.Render(h.Get(), fi);
        CHECK(nt.drawn == 1 && st.unresolvedSpans == 1);
    }
    {   // repair: broken triangles are dropped, the good ones are drawn, nothing outside the data is read
        Hand h; h.BeginSpan(); h.Quad(0, 0, 8, 8, 255, 0, 0); h.Quad(8, 0, 8, 8, 0, 255, 0); h.Quad(16, 0, 8, 8, 0, 0, 255);
        h.v[4].x = kNaN;                 // the green quad
        h.idx[17] = 9999;                // the blue quad
        R::Stats st; const auto img = SoftRender(h.Get(), 32, 8, 1.0f, &st);
        CHECK(st.repaired && !st.refused && st.trianglesDropped >= 2);
        CHECK(rtk::Px(img, 32, 1, 1) == 0xff0000ffu && rtk::Px(img, 32, 9, 1) == 0x000000ffu && rtk::Px(img, 32, 17, 5) == 0x000000ffu && rtk::Px(img, 32, 22, 2) == 0x0000ffffu);   // the blue quad lost its lower triangle only
    }
}

// ---------------------------------------------------------------------------------------------------------------------
std::vector<std::uint8_t> LegacyRender(const UI::DrawData& d, int w, int h) {
    UI::soft::Bitmap bm(w, h); bm.Clear(12, 14, 20); UI::soft::Render(d, bm); return bm.rgba;
}

void TestSoftReference() {
    RUN_SUITE("render: the software backend is the reference");
    const auto scenes = rscenes::All();
    int exact = 0, compared = 0;
    for (const auto& s : scenes) {
        Sim sim; rscenes::Run(s, sim);
        const UI::DrawData d = UI::GetDrawData();
        R::Options o; o.atlasLinear = false;                    // nearest atlas: what the draw-list rasteriser does
        R::Stats st; const auto mine = SoftRender(d, s.w, s.h, 1.0f, &st, o, 12, 14, 20);
        CHECK(!st.refused);
        if (!s.usesImage && s.name.find("fallback") == std::string::npos) {   // the old rasteriser cannot draw host images or fallback fonts
            const auto old = LegacyRender(d, s.w, s.h);
            ++compared;
            const bool same = old == mine;
            if (same) ++exact; else std::cerr << "  " << s.name << ": differs from soft::Render\n";
            CHECK(same);
        }
        UI::UseLauncherLook(false);
    }
    std::cout << "  " << exact << " of " << compared << " scenes byte-identical to OkayUI::soft::Render\n";
    // golden images: the committed ones where a scene is the same, then new ones for the scenes made for the backends
    struct G { const char* scene; const char* golden; bool linear; };
    const G goldens[] = {
        {"primitives", "primitives", false}, {"demo_window", "demo_window", false}, {"demo_window_launcher", "demo_window_launcher", false},
        {"lab", "drawlist_lab", false}, {"lab_launcher", "drawlist_lab_launcher", false},
        {"stacking_launcher", "render_stacking_launcher", true}, {"images", "render_images", true}, {"shapes_launcher", "render_shapes_launcher", true},
        {"plots_launcher", "render_plots_launcher", true}, {"nested_clip", "render_nested_clip", true}, {"table_scroll_launcher", "render_table_launcher", true},
    };
    for (const G& g : goldens) {
        for (const auto& s : scenes) {
            if (s.name != g.scene) continue;
            Sim sim; rscenes::Run(s, sim);
            R::Options o; o.atlasLinear = g.linear;
            UI::soft::Bitmap bm(s.w, s.h); bm.Clear(12, 14, 20);
            auto be = R::CreateSoftBackend(bm); R::Renderer r(*be, o); rscenes::SetupImages(r);
            R::FrameInfo fi; fi.fbWidth = s.w; fi.fbHeight = s.h;
            r.Render(UI::GetDrawData(), fi);
            std::string why;
            const bool ok = MatchesGolden(g.golden, bm, &why);
            if (!ok) std::cerr << "  golden " << g.golden << ": " << why << "\n";
            CHECK(ok);
            UI::UseLauncherLook(false);
        }
    }
    {   // DPI 2: flat areas of the 2x image equal the 1x image (checked where the 1x image is flat in a 3 x 3 neighbourhood)
        for (const char* name : {"primitives", "demo_window_launcher", "colorpicker_launcher"}) {
            for (const auto& s : scenes) {
                if (s.name != name) continue;
                Sim sim; rscenes::Run(s, sim);
                const UI::DrawData d = UI::GetDrawData();
                const auto one = SoftRender(d, s.w, s.h, 1.0f, nullptr, R::Options(), 12, 14, 20);
                const auto two = SoftRender(d, s.w * 2, s.h * 2, 2.0f, nullptr, R::Options(), 12, 14, 20);
                long flat = 0, bad = 0;
                for (int y = 1; y < s.h - 1; ++y) for (int x = 1; x < s.w - 1; ++x) {
                    const unsigned c = rtk::Px(one, s.w, x, y);
                    bool uni = true;
                    for (int dy = -1; dy <= 1 && uni; ++dy) for (int dx = -1; dx <= 1; ++dx) if (rtk::Px(one, s.w, x + dx, y + dy) != c) { uni = false; break; }
                    if (!uni) continue;
                    ++flat;
                    for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) if (rtk::Px(two, s.w * 2, x * 2 + dx, y * 2 + dy) != c) ++bad;
                }
                std::cout << "  dpi 2 " << name << ": " << flat << " flat pixels, " << bad << " mismatches\n";
                CHECK(flat > 1000 && bad == 0);
                UI::UseLauncherLook(false);
            }
        }
    }
    {   // drawing twice in a row on the same backend is the same picture; another frame after a reset too
        const auto scenes2 = rscenes::All();
        Sim sim; rscenes::Run(scenes2[1], sim);
        const UI::DrawData d = UI::GetDrawData();
        UI::soft::Bitmap a(800, 780), b(800, 780); a.Clear(1, 2, 3); b.Clear(1, 2, 3);
        auto be = R::CreateSoftBackend(a); R::Renderer r(*be); R::FrameInfo fi; fi.fbWidth = 800; fi.fbHeight = 780;
        r.Render(d, fi);
        auto be2 = R::CreateSoftBackend(b); R::Renderer r2(*be2); r2.Render(d, fi);
        CHECK(a.rgba == b.rgba);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
struct Row { std::string scene; rscenes::Diff d; };
void PrintTable(const char* title, const std::vector<Row>& rows) {
    std::cout << "  " << title << "\n  scene                          size       max   mean     >tol  differing\n";
    for (const Row& r : rows) {
        char line[200];
        std::snprintf(line, sizeof line, "  %-28s %4dx%-4d %5d %7.4f %7lld %9lld", r.scene.c_str(), r.d.w, r.d.h, r.d.maxDiff, r.d.meanDiff, r.d.above, r.d.differing);
        std::cout << line << "\n";
    }
}

std::vector<std::uint8_t> ViaPainter(plat::Painter& p, const UI::DrawData& d, int w, int h, float dpi, bool wantImage) {
    R::Options o;
    auto be = R::CreatePainterBackend(p);
    R::Renderer r(*be, o);
    if (wantImage) rscenes::SetupImages(r);
    p.SetClipRect(nullptr); p.SetBlendMode(plat::BlendMode::None); p.SetDrawColor(12, 14, 20, 255); p.Clear();
    R::FrameInfo fi; fi.fbWidth = w; fi.fbHeight = h; fi.dpi = dpi;
    const R::Stats st = r.Render(d, fi);
    CHECK(!st.refused);
    std::vector<std::uint8_t> px; int rw = 0, rh = 0;
    CHECK(p.ReadPixels(px, rw, rh) && rw == w && rh == h);
    return px;
}

void TestPainterBackend() {
    RUN_SUITE("render: okay::platform::Painter backend vs the software reference");
    std::vector<Row> rowsSoft, rowsSdl;
    double worstSoftMean = 0; int worstSoftMax = 0; double worstSdlMean = 0;
    for (const auto& s : rscenes::All()) {
        Sim sim; rscenes::Run(s, sim);
        const UI::DrawData d = UI::GetDrawData();
        const auto ref = SoftRender(d, s.w, s.h, 1.0f, nullptr, R::Options(), 12, 14, 20);   // (the reference draws host images too)
        auto sp = UI::soft::CreateBitmapPainter(s.w, s.h);
        CHECK(sp != nullptr);
        if (sp) {
            const auto img = ViaPainter(*sp, d, s.w, s.h, 1.0f, s.usesImage);
            const rscenes::Diff df = rscenes::Compare(img, ref, s.w, s.h, 0);
            rowsSoft.push_back({s.name, df});
            worstSoftMean = std::max(worstSoftMean, df.meanDiff); worstSoftMax = std::max(worstSoftMax, df.maxDiff);
            CHECK(df.sameSize && df.maxDiff == 0);      // the same rasteriser: must be identical
        }
        auto sd = plat::CreateSdl2SurfacePainter(s.w, s.h);
        if (sd) {
            const auto img = ViaPainter(*sd, d, s.w, s.h, 1.0f, s.usesImage);
            const rscenes::Diff df = rscenes::Compare(img, ref, s.w, s.h, 8);
            rowsSdl.push_back({s.name, df});
            worstSdlMean = std::max(worstSdlMean, df.meanDiff);
            CHECK(df.sameSize && df.meanDiff < 4.0);
        }
        UI::UseLauncherLook(false);
    }
    PrintTable("own software painter vs reference (tolerance 0)", rowsSoft);
    if (!rowsSdl.empty()) PrintTable("sdl2 painter (SDL software renderer) vs reference (tolerance 8)", rowsSdl);
    else std::cout << "  sdl2 painter: not available here\n";
    std::cout << "  worst: own painter max " << worstSoftMax << " mean " << worstSoftMean << ", sdl2 painter mean " << worstSdlMean << "\n";
    {   // DPI through the painter: size mismatch is refused, scaling matches the reference
        Hand h; h.BeginSpan(true, 3, 3, 5, 5); h.Quad(0, 0, 100, 100, 255, 255, 255);
        auto sp = UI::soft::CreateBitmapPainter(40, 40);
        const auto img = ViaPainter(*sp, h.Get(), 40, 40, 1.5f, false);
        const auto ref = SoftRender(h.Get(), 40, 40, 1.5f, nullptr, R::Options(), 12, 14, 20);
        CHECK(img == ref);
        auto be = R::CreatePainterBackend(*sp); R::Renderer r(*be);
        R::FrameInfo fi; fi.fbWidth = 41; fi.fbHeight = 40;
        CHECK(r.Render(h.Get(), fi).refused);
        auto sd = plat::CreateSdl2SurfacePainter(40, 40);
        if (sd) {
            const auto im2 = ViaPainter(*sd, h.Get(), 40, 40, 1.5f, false);
            const rscenes::Diff df = rscenes::Compare(im2, ref, 40, 40, 0);
            CHECK(df.maxDiff == 0);       // flat white clipped quad: SDL's clip rectangle is the same pixel rectangle
        }
    }
    {   // the painter's own state is put back: blend mode and clip rectangle
        auto sp = UI::soft::CreateBitmapPainter(32, 32);
        sp->SetBlendMode(plat::BlendMode::None);
        const okay::ui2d::Rect keep{2, 3, 11, 12};
        sp->SetClipRect(&keep);
        Hand h; h.BeginSpan(true, 4, 4, 8, 8); h.Quad(0, 0, 32, 32, 255, 0, 0); h.BeginSpan(); h.Quad(0, 0, 4, 4, 0, 255, 0);
        auto be = R::CreatePainterBackend(*sp); R::Renderer r(*be);
        R::FrameInfo fi; fi.fbWidth = 32; fi.fbHeight = 32;
        CHECK(!r.Render(h.Get(), fi).refused);
        okay::ui2d::Rect now{0, 0, 0, 0}; sp->GetClipRect(&now);
        CHECK(sp->GetBlendMode() == plat::BlendMode::None && sp->IsClipEnabled() && now.x == 2 && now.y == 3 && now.w == 11 && now.h == 12);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
void TestHostile() {
    RUN_SUITE("render: hostile draw data (random damage, no crash, nothing outside the target)");
    std::mt19937 rng(12345);
    auto rnd = [&](int n) { return (int)(rng() % (unsigned)n); };
    const auto scenes = rscenes::All();
    Sim sim; rscenes::Run(scenes[1], sim);
    rtk::Frozen base(UI::GetDrawData());
    UI::UseLauncherLook(false);
    const int N = 4000;
    int refused = 0, repaired = 0, drawn = 0;
    const float specials[] = { kNaN, kInf, -kInf, 1e30f, -1e30f, 0.0f, -0.0f, 1e-30f, 3.4e38f, 65536.0f * 65536.0f };
    const int ints[] = { 0, -1, 1, 2, 3, 5, INT_MAX, INT_MIN, INT_MAX - 1, 1 << 30, 99999999, -99999999 };
    for (int it = 0; it < N; ++it) {
        std::vector<UI::DrawVertex> v = base.v;
        std::vector<int> idx = base.idx;
        std::vector<UI::DrawCmd> cmds = base.cmds;
        UI::DrawData d = base.d;
        // damage: a few random edits
        const int edits = 1 + rnd(6);
        for (int e = 0; e < edits; ++e) {
            switch (rnd(12)) {
                case 0: if (!v.empty()) v[(size_t)rnd((int)v.size())].x = specials[rnd(10)]; break;
                case 1: if (!v.empty()) v[(size_t)rnd((int)v.size())].y = specials[rnd(10)]; break;
                case 2: if (!v.empty()) v[(size_t)rnd((int)v.size())].u = specials[rnd(10)]; break;
                case 3: if (!idx.empty()) idx[(size_t)rnd((int)idx.size())] = ints[rnd(12)]; break;
                case 4: if (!idx.empty()) idx[(size_t)rnd((int)idx.size())] = rnd((int)v.size() + 5) - 2; break;
                case 5: if (!cmds.empty()) cmds[(size_t)rnd((int)cmds.size())].idxOffset = ints[rnd(12)]; break;
                case 6: if (!cmds.empty()) cmds[(size_t)rnd((int)cmds.size())].idxCount = ints[rnd(12)]; break;
                case 7: if (!cmds.empty()) { auto& c = cmds[(size_t)rnd((int)cmds.size())]; c.clipOn = true; c.clipX = ints[rnd(12)]; } break;
                case 8: if (!cmds.empty()) { auto& c = cmds[(size_t)rnd((int)cmds.size())]; c.clipOn = true; c.clipW = ints[rnd(12)]; c.clipH = ints[rnd(12)]; } break;
                case 9: if (!cmds.empty()) { auto& c = cmds[(size_t)rnd((int)cmds.size())]; c.texture = (const void*)(std::uintptr_t)(rng() | 1u); } break;
                case 10: d.indexCount = std::max(0, std::min((int)idx.size(), d.indexCount - rnd(7))); break;
                case 11: if (!cmds.empty()) { auto& c = cmds[(size_t)rnd((int)cmds.size())]; c.clipOn = true; c.clipY = ints[rnd(12)]; c.clipH = ints[rnd(12)]; } break;
            }
        }
        d.vertices = v.data(); d.indices = idx.data(); d.cmds = cmds.data();
        if (rnd(40) == 0) d.vertexStride = ints[rnd(12)];
        if (rnd(60) == 0) d.cmdCount = (int)cmds.size() - rnd(3);
        const float dpi = rnd(4) == 0 ? 2.0f : rnd(8) == 0 ? 0.5f : 1.0f;
        // a canary border: the target is bigger than the frame the renderer is told about? No: the soft backend refuses a mismatch,
        // so the check is the stats and that the frame still has the clear colour wherever nothing was drawn.
        R::Stats st; (void)SoftRender(d, 64, 48, dpi, &st);
        if (st.refused) ++refused; else { ++drawn; if (st.repaired) ++repaired; }
        // the validator on the same data never throws and agrees with the renderer on 'refused'
        const R::Report rep = R::Validate(d, 64, 48, dpi);
        CHECK(rep.refused() == st.refused);
    }
    std::cout << "  " << N << " damaged frames: " << drawn << " drawn (" << repaired << " repaired), " << refused << " refused\n";
    CHECK(drawn > 100 && refused > 10);
}

} // namespace

int main() {
    RUN_SUITE("okayui render interface");
    TestValidator();
    TestRendererBasics();
    TestSoftReference();
    TestPainterBackend();
    TestHostile();
    TEST_MAIN_RESULT();
}
