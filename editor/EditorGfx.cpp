// The editor drawing seam: the switch, and the own side (OKAY_EDITOR_RENDERER=own, with OKAY_EDITOR_PLATFORM=own). See EditorGfx.hpp.
//
// The own side: our OpenGL painter (okay::platform::CreateGLPainter) on the main window's GL context (the platform seam made the
// window with WindowDesc::opengl), and our own ImGui renderer (shared/ImGuiOwnRenderer.*, the same code as the launcher's
// OKAY_LAUNCHER_RENDERER=own). No SDL in this file.
//
// The GL painter remembers which painter made its context current last and makes its own current again before it draws. The
// editor's 3D views use a second, hidden GL context (EditorPlatform::BeginViewportGL / EndViewportGL): End gives the painter's
// context back, so the painter never draws into the views' context.
//
// Written from the okay::platform interfaces (engine/include/okay/Platform/Painter.hpp, Platform.hpp); no code from elsewhere.
#include "EditorGfx.hpp"

#include "ImGuiOwnRenderer.hpp"
#include "okay/Core/ThirdPartySwitch.hpp"

#include <cstdlib>
#include <cstring>

namespace okay::editorgfx {

namespace plat = okay::platform;

RendererKind ParseRenderer(const char* value) {
#ifndef OKAY_PLATFORM_HAVE_SDL2
    (void)value;
    return RendererKind::Own;   // -DOKAY_WITH_SDL=OFF: our own renderer is the only one
#else
    // Our own renderer is the default (owner's decision, 2026-11-17). "sdl" (or any other word) is SDL_Renderer; unset follows
    // OKAY_THIRD_PARTY=1 back to SDL.
    if (!value || !*value) return okay::ThirdPartyForced() ? RendererKind::Sdl : RendererKind::Own;
    return std::strcmp(value, "own") == 0 ? RendererKind::Own : RendererKind::Sdl;
#endif
}

const char* RendererName(RendererKind k) { return k == RendererKind::Own ? "own" : "sdl"; }

namespace {

class OwnEditorGfx final : public EditorGfx {
public:
    OwnEditorGfx(plat::Window& w, std::unique_ptr<plat::Painter> p) : m_win(w), m_p(std::move(p)) {}
    ~OwnEditorGfx() override {
        ShutdownImGui();
        m_p.reset();   // destroys the GL context (the window stays: the platform seam owns it)
    }
    RendererKind Kind() const override { return RendererKind::Own; }
    std::string Describe() const override {
        return std::string("own (") + m_p->Name() + " painter on a " + plat::BackendName(m_win.Kind()) +
               " window, own ImGui renderer)";
    }
    plat::Painter& Painter() override { return *m_p; }
    void* SdlRenderer() override { return nullptr; }
    std::string DriverName() const override { return std::string("own ") + m_p->Name() + " painter"; }

    bool InitImGui() override {
        if (!m_renderer) m_renderer.reset(new okay::imguiown::ImGuiPainterRenderer(*m_p));
        return true;
    }
    void ShutdownImGui() override { m_renderer.reset(); }
    void NewFrame() override {}
    void RenderImGui(ImDrawData* dd) override { if (m_renderer) m_renderer->Render(dd); }
    void UpdateImGuiTexture(ImTextureData* t) override { if (m_renderer && t) m_renderer->UpdateTexture(t); }

private:
    plat::Window& m_win;
    std::unique_ptr<plat::Painter> m_p;
    std::unique_ptr<okay::imguiown::ImGuiPainterRenderer> m_renderer;
};

} // namespace

std::unique_ptr<EditorGfx> CreateOwnEditorGfx(plat::Window& w, plat::Backend& b, bool vsync, std::string* err) {
    if (okay::OwnStartForcedToFail("renderer")) { if (err) *err = "OKAY_TEST_OWN_FAIL=renderer (a test asked our renderer to fail)"; return nullptr; }
    plat::PainterDesc pd;
    pd.vsync = vsync;
    std::string why;
    std::unique_ptr<plat::Painter> p = plat::CreateGLPainter(w, b, pd, &why);
    if (!p) { if (err) *err = why; return nullptr; }
    return std::unique_ptr<EditorGfx>(new OwnEditorGfx(w, std::move(p)));
}

std::unique_ptr<EditorGfx> CreateEditorGfxFromEnv(void* sdlWindow, plat::Window* glWindow, plat::Backend* b, bool ownPlatform,
                                                  bool vsync, std::string* note, std::string* err) {
    const RendererKind want = ParseRenderer(std::getenv("OKAY_EDITOR_RENDERER"));
    std::unique_ptr<EditorGfx> g;
    std::string why;
    if (want == RendererKind::Own) {
        if (!OwnRendererAllowed(want, ownPlatform)) why = "it needs the own platform side (OKAY_EDITOR_PLATFORM) as well";
        else if (!glWindow || !b) why = "no OpenGL main window";
        else g = CreateOwnEditorGfx(*glWindow, *b, vsync, &why);
    }
    // SDL_Renderer only on an SDL window: with the main window made by okay::platform (sdlWindow null) there is nothing SDL can
    // draw on, and the caller starts again on SDL (the editor's automatic fallback).
    if (!g && sdlWindow) g = CreateSdlEditorGfx(sdlWindow, vsync, err);
    else if (!g && err) *err = why;
    if (note) {
        *note = g ? "editor: renderer " + g->Describe() : std::string("editor: renderer: none");
        if (want == RendererKind::Own && (!g || g->Kind() != RendererKind::Own))
            *note += " (our own renderer could not start: " + why + ")";
    }
    return g;
}

} // namespace okay::editorgfx
