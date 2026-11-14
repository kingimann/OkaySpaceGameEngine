// OkayUI render interface: the OpenGL backend (3.3 core and ES 3.0), run on a real GL context when the machine can make
// one (Mesa's llvmpipe, a CPU rasteriser, through EGL: no display needed) and compared with the software reference scene
// by scene. Where no context can be made the test says SKIPPED and passes, so a build machine without a driver is not
// failed by it; the shader text is still checked.
#include "test_framework.hpp"
#include "RenderTestKit.hpp"
#include "RenderGLContext.hpp"

#include <climits>
#include <cmath>
#include <cstring>
#include <limits>
#include <random>

using namespace okayuitest;
namespace UI = OkayUI;
namespace R = OkayUI::render;
using rtk::Hand;

namespace {

const float kNaN = std::numeric_limits<float>::quiet_NaN();

std::vector<std::uint8_t> SoftRef(const UI::DrawData& d, int w, int h, float dpi, bool images, const R::Options& o = R::Options()) {
    UI::soft::Bitmap bm(w, h); bm.Clear(12, 14, 20);
    auto be = R::CreateSoftBackend(bm); R::Renderer r(*be, o);
    if (images) rscenes::SetupImages(r);
    R::FrameInfo fi; fi.fbWidth = w; fi.fbHeight = h; fi.dpi = dpi;
    r.Render(d, fi);
    return bm.rgba;
}

struct GlRun {
    gltest::Context& ctx;
    std::unique_ptr<R::Backend> be;
    std::unique_ptr<R::Renderer> r;
    int w = 0, h = 0;
    GlRun(gltest::Context& c, R::GLFlavor f, std::string* err) : ctx(c) {
        be = R::CreateGLBackend(&GlRun::ProcThunk, f, err);
        if (be) r.reset(new R::Renderer(*be));
    }
    static gltest::Context* gProc;
    static void* ProcThunk(const char* n) { return gProc ? gProc->Proc(n) : nullptr; }
    bool Frame(const UI::DrawData& d, int W, int H, float dpi, bool images, std::vector<std::uint8_t>& out, R::Stats* st = nullptr) {
        if (W != w || H != h) { if (!ctx.MakeFbo(W, H)) return false; w = W; h = H; }
        ctx.g.BindFramebuffer(0x8D40, ctx.fbo());
        ctx.g.Viewport(0, 0, W, H);
        ctx.g.Disable(0x0C11);
        ctx.Clear(12 / 255.0f, 14 / 255.0f, 20 / 255.0f, 1.0f);
        if (images && !r->HasImage(rscenes::ImageKey())) rscenes::SetupImages(*r);
        R::FrameInfo fi; fi.fbWidth = W; fi.fbHeight = H; fi.dpi = dpi;
        const R::Stats s = r->Render(d, fi);
        if (st) *st = s;
        int rw = 0, rh = 0;
        return be->ReadPixels(out, rw, rh) && rw == W && rh == H;
    }
};
gltest::Context* GlRun::gProc = nullptr;

void TestShaders() {
    RUN_SUITE("render gl: shader text");
    for (R::GLFlavor f : { R::GLFlavor::Core33, R::GLFlavor::ES30 }) {
        const std::string vs = R::GLShaderSource(f, 0), fs = R::GLShaderSource(f, 1);
        const char* want = f == R::GLFlavor::Core33 ? "#version 330 core" : "#version 300 es";
        CHECK(vs.rfind(want, 0) == 0 && fs.rfind(want, 0) == 0);
        CHECK(vs.find("uScale") != std::string::npos && fs.find("texture(uTex, vUv)") != std::string::npos && fs.find("vCol *") != std::string::npos);
        if (f == R::GLFlavor::ES30) CHECK(fs.find("precision highp float") != std::string::npos);
    }
    const std::string hl = R::D3D11ShaderSource();
    CHECK(hl.find("VSMain") != std::string::npos && hl.find("PSMain") != std::string::npos && hl.find("tex0.Sample") != std::string::npos);
}

void RunFlavor(R::GLFlavor flavor, const char* label) {
    RUN_SUITE(label);
    gltest::Context ctx; std::string why;
    if (!ctx.Create(flavor == R::GLFlavor::ES30, &why)) { std::cout << "  SKIPPED: " << why << "\n"; return; }
    GlRun::gProc = &ctx;
    std::cout << "  context: " << ctx.Info() << "\n";
    std::string err;
    GlRun run(ctx, flavor, &err);
    if (!run.be) { std::cout << "  FAIL: backend not created: " << err << "\n"; CHECK(false); return; }
    CHECK(std::string(run.be->Name()).find(flavor == R::GLFlavor::Core33 ? "3.3" : "es") != std::string::npos);

    // ---- every scene against the software reference ----
    struct Row { std::string name; rscenes::Diff d; };
    std::vector<Row> rows;
    double worstMean = 0; int worstMax = 0; long long worstAbove = 0; double worstFrac = 0;
    for (const auto& s : rscenes::All()) {
        Sim sim; rscenes::Run(s, sim);
        const UI::DrawData d = UI::GetDrawData();
        const auto ref = SoftRef(d, s.w, s.h, 1.0f, s.usesImage);
        std::vector<std::uint8_t> got; R::Stats st;
        const bool ok = run.Frame(d, s.w, s.h, 1.0f, s.usesImage, got, &st);
        CHECK(ok && !st.refused);
        CHECK(ctx.g.GetError() == 0);
        if (ok) {
            const rscenes::Diff df = rscenes::Compare(got, ref, s.w, s.h, 8);
            rows.push_back({s.name, df});
            worstMean = std::max(worstMean, df.meanDiff); worstMax = std::max(worstMax, df.maxDiff); worstAbove = std::max(worstAbove, df.above);
            worstFrac = std::max(worstFrac, (double)df.above / (double)df.total);
            CHECK(df.sameSize && df.meanDiff < 0.5 && (double)df.above / (double)df.total < 0.01);
        }
        UI::UseLauncherLook(false);
    }
    std::cout << "  scene                          size       max   mean     >8   differing   (OpenGL vs software reference)\n";
    for (const Row& r : rows) {
        char line[200];
        std::snprintf(line, sizeof line, "  %-28s %4dx%-4d %5d %7.4f %7lld %9lld", r.name.c_str(), r.d.w, r.d.h, r.d.maxDiff, r.d.meanDiff, r.d.above, r.d.differing);
        std::cout << line << "\n";
    }
    std::cout << "  worst: max channel difference " << worstMax << ", mean " << worstMean << ", pixels above 8: " << worstAbove << " (" << worstFrac * 100.0 << " percent)\n";

    // ---- scissor per span: exactly the clip rectangle ----
    {
        Hand h; h.BeginSpan(true, 10, 12, 20, 8); h.Quad(0, 0, 64, 64, 255, 0, 0);
        std::vector<std::uint8_t> img; CHECK(run.Frame(h.Get(), 64, 64, 1.0f, false, img));
        int red = 0, wrong = 0;
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
            const bool in = x >= 10 && x < 30 && y >= 12 && y < 20;
            const bool isRed = rtk::Px(img, 64, x, y) == 0xff0000ffu;
            if (isRed) ++red;
            if (isRed != in) ++wrong;
        }
        CHECK(red == 160 && wrong == 0);
    }
    {   // three spans, three clips, different textures and one outside the target
        Hand h; h.BeginSpan(true, 0, 0, 16, 16); h.Quad(0, 0, 64, 64, 255, 0, 0);
        h.BeginSpan(true, 32, 32, 100, 100); h.Quad(0, 0, 64, 64, 0, 255, 0);
        h.BeginSpan(true, -50, -50, 60, 60); h.Quad(0, 0, 64, 64, 0, 0, 255);
        h.BeginSpan(); h.Quad(60, 60, 4, 4, 255, 255, 0);
        std::vector<std::uint8_t> img; CHECK(run.Frame(h.Get(), 64, 64, 1.0f, false, img));
        const auto ref = SoftRef(h.Get(), 64, 64, 1.0f, false);
        CHECK(rscenes::Compare(img, ref, 64, 64, 0).maxDiff == 0);
    }
    // ---- DPI: a clip rounded outward, geometry scaled, same as the reference ----
    for (float dpi : { 1.5f, 2.0f }) {
        Hand h; h.BeginSpan(true, 3, 3, 5, 5); h.Quad(0, 0, 100, 100, 255, 255, 255); h.BeginSpan(); h.Quad(20, 20, 7, 7, 128, 255, 0, 200);
        std::vector<std::uint8_t> img; CHECK(run.Frame(h.Get(), 80, 80, dpi, false, img));
        const auto ref = SoftRef(h.Get(), 80, 80, dpi, false);
        const rscenes::Diff df = rscenes::Compare(img, ref, 80, 80, 0);
        std::cout << "  dpi " << dpi << ": max diff " << df.maxDiff << ", differing pixels " << df.differing << "\n";
        CHECK(df.maxDiff <= 2);
    }
    {   // the demo window at DPI 2 against the reference at DPI 2
        const rscenes::Scene sc = rscenes::Find("demo_window_launcher");
        Sim sim; rscenes::Run(sc, sim);
        const UI::DrawData d = UI::GetDrawData();
        const auto ref = SoftRef(d, sc.w * 2, sc.h * 2, 2.0f, false);
        std::vector<std::uint8_t> img; CHECK(run.Frame(d, sc.w * 2, sc.h * 2, 2.0f, false, img));
        const rscenes::Diff df = rscenes::Compare(img, ref, sc.w * 2, sc.h * 2, 8);
        std::cout << "  demo_window_launcher at DPI 2: max " << df.maxDiff << " mean " << df.meanDiff << " above-8 " << df.above << "\n";
        CHECK(df.meanDiff < 0.5);
        UI::UseLauncherLook(false);
    }
    // ---- the GL state the host had is put back ----
    {
        ctx.g.BindFramebuffer(0x8D40, ctx.fbo());
        ctx.g.Enable(0x0B71 /* DEPTH_TEST */); ctx.g.Enable(0x0B44 /* CULL_FACE */); ctx.g.Disable(0x0BE2 /* BLEND */); ctx.g.Enable(0x0C11 /* SCISSOR_TEST */);
        ctx.g.Viewport(3, 4, 5, 6); ctx.g.Scissor(1, 2, 7, 8); ctx.g.BlendFunc(0x0306 /* DST_COLOR */, 0x0300 /* SRC_COLOR */);
        Hand h; h.BeginSpan(); h.Quad(0, 0, 8, 8, 255, 0, 0);
        R::FrameInfo fi; fi.fbWidth = 64; fi.fbHeight = 64;
        CHECK(!run.r->Render(h.Get(), fi).refused);
        int vp[4], sc[4], bsrc = 0, bdst = 0;
        ctx.g.GetIntegerv(0x0BA2, vp); ctx.g.GetIntegerv(0x0C10, sc); ctx.g.GetIntegerv(0x80C9 /* BLEND_SRC_RGB */, &bsrc); ctx.g.GetIntegerv(0x80C8 /* BLEND_DST_RGB */, &bdst);
        CHECK(vp[0] == 3 && vp[1] == 4 && vp[2] == 5 && vp[3] == 6 && sc[0] == 1 && sc[1] == 2 && sc[2] == 7 && sc[3] == 8);
        CHECK(ctx.g.IsEnabled(0x0B71) && ctx.g.IsEnabled(0x0B44) && !ctx.g.IsEnabled(0x0BE2) && ctx.g.IsEnabled(0x0C11));
        CHECK(bsrc == 0x0306 && bdst == 0x0300);
        CHECK(ctx.g.GetError() == 0);
        ctx.g.Disable(0x0B71); ctx.g.Disable(0x0B44); ctx.g.Disable(0x0C11);
    }
    // ---- textures: an update with a new size, many images, the white texel is not a user image ----
    {
        static char k;
        R::Renderer& r = *run.r;
        std::vector<std::uint8_t> a(8 * 8 * 4, 255), b(16 * 4 * 4);
        for (size_t i = 0; i < b.size(); i += 4) { b[i] = 255; b[i + 1] = 0; b[i + 2] = 0; b[i + 3] = 255; }
        CHECK(r.SetImage(&k, 8, 8, a.data(), false));
        Hand h; h.BeginSpan(false, 0, 0, 0, 0, &k); h.Quad(0, 0, 16, 4, 255, 255, 255, 255);
        std::vector<std::uint8_t> img; CHECK(run.Frame(h.Get(), 16, 8, 1.0f, false, img));
        CHECK(rtk::Px(img, 16, 3, 1) == 0xffffffffu);
        CHECK(r.SetImage(&k, 16, 4, b.data(), false));     // same key, new size
        CHECK(run.Frame(h.Get(), 16, 8, 1.0f, false, img));
        CHECK(rtk::Px(img, 16, 3, 1) == 0xff0000ffu && rtk::Px(img, 16, 15, 3) == 0xff0000ffu);
        for (int i = 0; i < 40; ++i) { static char keys[40]; CHECK(r.SetImage(&keys[i], 4, 4, a.data())); }
        r.RemoveImage(&k);
        CHECK(ctx.g.GetError() == 0);
    }
    // ---- hostile frames: no GL error, no crash ----
    {
        std::mt19937 rng(777);
        auto rnd = [&](int n) { return (int)(rng() % (unsigned)n); };
        Sim sim; rscenes::Run(rscenes::All()[1], sim);
        rtk::Frozen base(UI::GetDrawData());
        UI::UseLauncherLook(false);
        const int ints[] = { 0, -1, 1, 3, INT_MAX, INT_MIN, 1 << 30, 99999999 };
        int errors = 0, refused = 0;
        for (int it = 0; it < 300; ++it) {
            std::vector<UI::DrawVertex> v = base.v; std::vector<int> idx = base.idx; std::vector<UI::DrawCmd> cmds = base.cmds;
            UI::DrawData d = base.d;
            for (int e = 0; e < 1 + rnd(5); ++e) {
                switch (rnd(7)) {
                    case 0: v[(size_t)rnd((int)v.size())].x = kNaN; break;
                    case 1: v[(size_t)rnd((int)v.size())].y = 1e30f; break;
                    case 2: idx[(size_t)rnd((int)idx.size())] = ints[rnd(8)]; break;
                    case 3: cmds[(size_t)rnd((int)cmds.size())].idxOffset = ints[rnd(8)]; break;
                    case 4: cmds[(size_t)rnd((int)cmds.size())].idxCount = ints[rnd(8)]; break;
                    case 5: { auto& c = cmds[(size_t)rnd((int)cmds.size())]; c.clipOn = true; c.clipX = ints[rnd(8)]; c.clipW = ints[rnd(8)]; c.clipY = ints[rnd(8)]; c.clipH = ints[rnd(8)]; } break;
                    case 6: v[(size_t)rnd((int)v.size())].u = kNaN; break;
                }
            }
            d.vertices = v.data(); d.indices = idx.data(); d.cmds = cmds.data();
            std::vector<std::uint8_t> img; R::Stats st;
            run.Frame(d, 800, 780, 1.0f, false, img, &st);
            if (st.refused) ++refused;
            if (ctx.g.GetError() != 0) ++errors;
        }
        std::cout << "  300 damaged frames through OpenGL: " << errors << " GL errors, " << refused << " refused\n";
        CHECK(errors == 0);
    }
    run.r.reset(); run.be.reset();
    GlRun::gProc = nullptr;
}

} // namespace

int main() {
    RUN_SUITE("okayui render: OpenGL backend");
    TestShaders();
    RunFlavor(R::GLFlavor::Core33, "render gl: OpenGL 3.3 core (EGL, Mesa)");
    RunFlavor(R::GLFlavor::ES30, "render gl: OpenGL ES 3.0 (EGL, Mesa)");
    TEST_MAIN_RESULT();
}
