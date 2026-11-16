// The own implementation of the launcher's drawing seam (LauncherGfx.hpp), the renderer switch and the frame-time log.
// No SDL in this file: the window comes from okay::platform (whichever backend is the default), the painter is our
// OpenGL painter (engine/src/Platform/GlPainter.cpp) or, without GL, the software painter (okayui/soft), and ImGui's
// backends are our own (shared/ImGuiOwnRenderer.cpp, shared/ImGuiOwnPlatform.cpp).
#include "LauncherGfx.hpp"
#include "ImGuiOwnBackend.hpp"

#include "imgui.h"

#ifdef OKAY_LAUNCHER_HAVE_SOFT_PAINTER
#include "OkayUI_SoftPainter.hpp"
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace okay::launchergfx {

namespace plat = okay::platform;

RendererKind ParseRenderer(const char* v) {
    return v && std::strcmp(v, "own") == 0 ? RendererKind::Own : RendererKind::Sdl;
}

const char* RendererName(RendererKind k) { return k == RendererKind::Own ? "own" : "sdl"; }

bool FrameStats::Add(double ms) {
    if (!Active()) return false;
    if (!m_ms) m_ms.reset(new double[(std::size_t)m_target]);
    m_ms[(std::size_t)m_n++] = ms;
    if (m_n < m_target) return false;
    m_done = true;
    return true;
}

std::string FrameStats::Report(const char* what) const {
    if (!m_ms || m_n == 0) return std::string();
    std::vector<double> v(m_ms.get(), m_ms.get() + m_n);
    std::sort(v.begin(), v.end());
    const double med = v.size() % 2 ? v[v.size() / 2] : 0.5 * (v[v.size() / 2 - 1] + v[v.size() / 2]);
    const double p95 = v[std::min(v.size() - 1, (std::size_t)((double)v.size() * 0.95))];
    char buf[256];
    std::snprintf(buf, sizeof(buf), "launcher: frame time (%s, %d frames): median %.3f ms, p95 %.3f ms, min %.3f ms, max %.3f ms",
                  what, m_n, med, p95, v.front(), v.back());
    return buf;
}

namespace {

class OwnGfx final : public Gfx {
public:
    OwnGfx(plat::Backend& b, std::unique_ptr<plat::Window> w, std::unique_ptr<plat::Painter> p)
        : m_b(b), m_w(std::move(w)), m_p(std::move(p)) {}
    ~OwnGfx() override {
        ShutdownImGui();
        m_p.reset();   // the GL painter destroys its context; it must go before the window
        m_w.reset();
    }
    RendererKind Kind() const override { return RendererKind::Own; }
    std::string Describe() const override {
        return std::string("own (") + m_p->Name() + " painter on a " + plat::BackendName(m_w->Kind()) +
               " window, own ImGui renderer and platform glue)";
    }
    plat::Window& Window() override { return *m_w; }
    plat::Backend& Backend() override { return m_b; }
    plat::Painter& Painter() override { return *m_p; }
    void* SdlWindow() override { return m_w->Kind() == plat::BackendKind::Sdl2 ? m_w->NativeHandle() : nullptr; }
    void* SdlRenderer() override { return nullptr; }

    bool InitImGui() override {
        if (m_renderer) return true;
        m_renderer.reset(new ImGuiPainterRenderer(*m_p));
        m_glue.reset(new ImGuiPlatformGlue(m_b, *m_w));
        const char* log = std::getenv("OKAY_LAUNCHER_INPUT_LOG");
        m_glue->SetInputLog(log && *log && std::strcmp(log, "0") != 0);
        return true;
    }
    void ShutdownImGui() override { m_glue.reset(); m_renderer.reset(); }
    void NewFrame() override { if (m_glue) m_glue->NewFrame(); }
    bool FeedEvent(const plat::Event& e) override { return m_glue && m_glue->ProcessEvent(e); }
    bool PollEvent(plat::Event& e) override {
        if (!m_b.PollEvent(e)) return false;
        if (m_glue) m_glue->ProcessEvent(e);
        return true;
    }
    void RenderImGui(ImDrawData* dd) override { if (m_renderer) m_renderer->Render(dd); }

private:
    plat::Backend& m_b;
    std::unique_ptr<plat::Window> m_w;
    std::unique_ptr<plat::Painter> m_p;
    std::unique_ptr<ImGuiPainterRenderer> m_renderer;
    std::unique_ptr<ImGuiPlatformGlue> m_glue;
};

} // namespace

std::unique_ptr<Gfx> CreateOwnGfx(const WindowSpec& spec, std::string* err) {
    plat::Backend* b = plat::Default();
    if (!b) { if (err) *err = "okay::platform has no backend in this build"; return nullptr; }
    plat::InitFlags f;
    if (b->Kind() == plat::BackendKind::Sdl2) { f.audio = false; f.gamepad = false; }   // the launcher opens audio itself, later
    if (!b->Init(f)) { if (err) *err = "okay::platform could not start (" + b->LastError() + ")"; return nullptr; }

    plat::WindowDesc d;
    d.title = spec.title;
    d.x = spec.x; d.y = spec.y;
    d.width = spec.width; d.height = spec.height;
    d.resizable = true; d.hidden = true; d.highDpi = true;
    d.opengl = true;
    std::unique_ptr<plat::Window> w = b->MakeWindow(d);
    std::string why;
    std::unique_ptr<plat::Painter> p;
    plat::PainterDesc pd;
    pd.vsync = spec.vsync;
    if (w) p = plat::CreateGLPainter(*w, *b, pd, &why);
    else why = "no OpenGL window (" + b->LastError() + ")";
#ifdef OKAY_LAUNCHER_HAVE_SOFT_PAINTER
    if (!p) {   // no GL: the software painter, on a plain window
        std::string why2;
        if (!w) { d.opengl = false; w = b->MakeWindow(d); }
        if (w) p = OkayUI::soft::CreateWindowPainter(*w, pd, &why2);
        if (!p) why += "; software painter: " + (w ? why2 : "no window (" + b->LastError() + ")");
    }
#endif
    if (!p) { if (err) *err = why; return nullptr; }
    return std::unique_ptr<Gfx>(new OwnGfx(*b, std::move(w), std::move(p)));
}

} // namespace okay::launchergfx
