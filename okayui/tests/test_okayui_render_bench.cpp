// OkayUI render interface: what a heavy frame costs. Three scenes (a code editor on a 100,000-line file, a table of 5000 rows,
// a plot of 20,000 points) are built by OkayUI, validated, and submitted through each backend that can run here; the numbers
// printed are CPU milliseconds (steady_clock, release build): building the frame in OkayUI, validating it, and
// Renderer::Render per backend (for the CPU rasterisers that is the whole drawing; for OpenGL on Mesa's llvmpipe it is the
// submit, and "+ finish" adds glFinish, which waits for llvmpipe's CPU rasteriser; with a real GPU the submit number is the CPU
// side only and the GPU time is not measured here). Nothing is asserted but that the frame is accepted: a build machine is
// not a benchmark machine.
#include "test_framework.hpp"
#include "RenderTestKit.hpp"
#include "RenderGLContext.hpp"
#include "okay/Platform/Painter.hpp"
#include "OkayUI_SoftPainter.hpp"
#include "okay/UI/OkayUICodeEdit.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace okayuitest;
namespace UI = OkayUI;
namespace R = OkayUI::render;
namespace plat = okay::platform;

namespace {

const int W = 1280, H = 800;

struct Frame {
    std::string name;
    double buildMs = 0;
    std::unique_ptr<rtk::Frozen> data;
};

double Median(std::vector<double> v) { std::sort(v.begin(), v.end()); return v.empty() ? 0 : v[v.size() / 2]; }

Frame BuildCode() {
    Frame f; f.name = "code editor, 100k lines";
    Reset(); UI::UseLauncherLook(true); UI::Style().caretBlink = false;
    static UI::CodeEdit ce;
    std::string text; text.reserve(6 * 1024 * 1024);
    for (int i = 0; i < 100000; ++i) { text += "    local v" + std::to_string(i) + " = Compute(" + std::to_string(i) + ", 3.5) + x  // line " + std::to_string(i) + "\n"; }
    ce.SetLanguage(UI::OkayScriptLanguage());
    ce.SetText(text);
    ce.GoToLine(50000);
    Sim sim((float)W, (float)H);
    auto ui = [&] { if (UI::Begin("W", 0, 0, (float)W, (float)H)) ce.Draw("ed", W - 20.0f, H - 50.0f); UI::End(); };
    sim.Frames(3, ui);
    std::vector<double> t;
    for (int i = 0; i < 9; ++i) { const auto t0 = std::chrono::steady_clock::now(); sim.Frame(ui); t.push_back(rtk::MsSince(t0)); }
    f.buildMs = Median(t);
    f.data.reset(new rtk::Frozen(UI::GetDrawData()));
    return f;
}

Frame BuildTable(int cols) {
    Frame f; f.name = cols == 3 ? "table, 5000 rows x 3" : "table, 5000 rows x 1";
    Reset(); UI::UseLauncherLook(true); UI::Style().caretBlink = false;
    Sim sim((float)W, (float)H);
    auto ui = [&] {
        if (UI::Begin("T", 0, 0, (float)W, (float)H)) {
            if (UI::BeginTable("big", cols, UI::Table_ScrollY | UI::Table_RowBg | UI::Table_BordersInnerH, UI::Vec2{(float)W - 30, (float)H - 60})) {
                UI::TableSetupColumn("Id", UI::Col_WidthFixed, 120);
                if (cols > 1) { UI::TableSetupColumn("Name", UI::Col_WidthFixed, 200); UI::TableSetupColumn("Value", UI::Col_WidthFixed, 120); }
                UI::TableSetupScrollFreeze(0, 1);
                UI::TableHeadersRow();
                for (int i = 0; i < 5000; ++i) {
                    UI::TableNextRow(); char b[24];
                    UI::TableNextColumn(); std::snprintf(b, sizeof b, "%d", i); UI::Text(b);
                    if (cols > 1) {
                        UI::TableNextColumn(); UI::Text(i % 2 ? "alpha" : "beta");
                        UI::TableNextColumn(); std::snprintf(b, sizeof b, "%.1f", i * 0.37); UI::Text(b);
                    }
                }
                UI::EndTable();
            }
        }
        UI::End();
    };
    sim.Frames(3, ui);
    std::vector<double> t;
    for (int i = 0; i < 5; ++i) { const auto t0 = std::chrono::steady_clock::now(); sim.Frame(ui); t.push_back(rtk::MsSince(t0)); }
    f.buildMs = Median(t);
    f.data.reset(new rtk::Frozen(UI::GetDrawData()));
    return f;
}

Frame BuildPlot() {
    Frame f; f.name = "plot, 20000 points";
    Reset(); UI::UseLauncherLook(true); UI::Style().caretBlink = false;
    static std::vector<float> v(20000);
    for (size_t i = 0; i < v.size(); ++i) v[i] = std::sin((float)i * 0.013f) * 0.7f + 0.2f * std::sin((float)i * 0.37f);
    Sim sim((float)W, (float)H);
    auto ui = [&] {
        if (UI::Begin("P", 0, 0, (float)W, (float)H)) {
            UI::PlotOptions o; o.size = UI::Vec2{(float)W - 30, (float)H - 60}; o.overlay = "20000 samples";
            UI::PlotLines("signal", v.data(), (int)v.size(), o);
        }
        UI::End();
    };
    sim.Frames(3, ui);
    std::vector<double> t;
    for (int i = 0; i < 7; ++i) { const auto t0 = std::chrono::steady_clock::now(); sim.Frame(ui); t.push_back(rtk::MsSince(t0)); }
    f.buildMs = Median(t);
    f.data.reset(new rtk::Frozen(UI::GetDrawData()));
    return f;
}

struct Res { double submit = -1, finish = -1; bool ok = false; };

Res TimeBackend(R::Backend& be, const UI::DrawData& d, int iters, void (*finish)(void*), void* fin, void (*pre)(void*) = nullptr, void* preArg = nullptr) {
    Res r;
    R::Renderer rn(be);
    R::FrameInfo fi; fi.fbWidth = W; fi.fbHeight = H;
    if (pre) pre(preArg);
    R::Stats st = rn.Render(d, fi);     // first frame uploads the atlas: not timed
    r.ok = !st.refused;
    std::vector<double> sub, fin2;
    for (int i = 0; i < iters; ++i) {
        if (pre) pre(preArg);
        const auto t0 = std::chrono::steady_clock::now();
        rn.Render(d, fi);
        sub.push_back(rtk::MsSince(t0));
        if (finish) { finish(fin); fin2.push_back(rtk::MsSince(t0)); }
    }
    r.submit = Median(sub); r.finish = fin2.empty() ? -1 : Median(fin2);
    return r;
}

} // namespace

int main() {
    RUN_SUITE("okayui render: performance of heavy frames");
    std::vector<Frame> frames;
    frames.push_back(BuildCode());
    frames.push_back(BuildTable(3));
    frames.push_back(BuildTable(1));
    frames.push_back(BuildPlot());
    UI::UseLauncherLook(false);

    // GL contexts (optional)
    gltest::Context glCore, glEs; std::string whyCore, whyEs;
    const bool hasCore = glCore.Create(false, &whyCore);
    std::cout << "  OpenGL 3.3 core: " << (hasCore ? glCore.Info() : "not available: " + whyCore) << "\n";
    // (a second context is made after the first is done: EGL contexts do not share a thread's current slot)

    std::cout << "\n  scene                       build   valid  verts   idx    spans  upload   soft   painter-soft  painter-sdl2  GL core submit (+finish)\n";
    for (const Frame& f : frames) {
        const UI::DrawData& d = f.data->d;
              const auto t0 = std::chrono::steady_clock::now();
        R::Report rep;
        for (int i = 0; i < 5; ++i) rep = R::Validate(d, W, H, 1.0f);
        const double valid = rtk::MsSince(t0) / 5;
        CHECK(!rep.refused());
        const double bytes = ((double)d.vertexCount * sizeof(UI::DrawVertex) + (double)d.indexCount * 4) / 1024.0;
        // software reference
        UI::soft::Bitmap bm(W, H); bm.Clear(12, 14, 20);
        auto soft = R::CreateSoftBackend(bm);
        const Res rs = TimeBackend(*soft, d, 3, nullptr, nullptr);
        CHECK(rs.ok);
        // painters
        auto pp = UI::soft::CreateBitmapPainter(W, H);
        auto pb = R::CreatePainterBackend(*pp);
        const Res rp = TimeBackend(*pb, d, 3, nullptr, nullptr);
        auto sd = plat::CreateSdl2SurfacePainter(W, H);
        Res rd; if (sd) { auto sb = R::CreatePainterBackend(*sd); rd = TimeBackend(*sb, d, 3, nullptr, nullptr); }
        // OpenGL core
        Res rg;
        if (hasCore) {
            struct G { gltest::Context* c; };
            static gltest::Context* gc; gc = &glCore;
            auto proc = [](const char* n) -> void* { return gc->Proc(n); };
            std::string err;
            auto gb = R::CreateGLBackend(proc, R::GLFlavor::Core33, &err);
            if (gb && glCore.MakeFbo(W, H)) {
                auto fin = [](void*) { gc->g.Finish(); };
                auto pre = [](void*) { gc->g.BindFramebuffer(0x8D40, gc->fbo()); gc->g.Viewport(0, 0, W, H); gc->Clear(0.05f, 0.05f, 0.08f, 1.0f); gc->g.Finish(); };
                rg = TimeBackend(*gb, d, 20, fin, nullptr, pre, nullptr);
                CHECK(rg.ok);
            } else std::cout << "  (GL backend: " << err << ")\n";
        }
        char line[400];
        std::snprintf(line, sizeof line, "  %-26s %6.2f %6.2f %7d %7d %5d %6.0fK %7.1f %10.1f %13.1f %12.2f (%.2f)", f.name.c_str(), f.buildMs, valid, d.vertexCount, d.indexCount, d.cmdCount, bytes,
                      rs.submit, rp.submit, rd.submit, rg.submit, rg.finish);
        std::cout << line << (d.dropped ? "  (OkayUI dropped shapes: " + std::to_string(d.dropped) + ")" : std::string()) << "\n";
    }
    std::cout << "  (milliseconds, medians; 'upload' is the vertex plus index data the frame sends; -1 = not run)\n";
    TEST_MAIN_RESULT();
}
