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

#include "ImGuiOwnPlatform.hpp"
#include "ImGuiOwnRenderer.hpp"

#include <cstdlib>
#include <cstring>

namespace okay::editorgfx {

namespace plat = okay::platform;

RendererKind ParseRenderer(const char* value) {
#ifndef OKAY_PLATFORM_HAVE_SDL2
    (void)value;
    return RendererKind::Own;   // -DOKAY_WITH_SDL=OFF: our own renderer is the only one
#else
    return value && std::strcmp(value, "own") == 0 ? RendererKind::Own : RendererKind::Sdl;
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
        if (!m_renderer) {
            m_renderer.reset(new okay::imguiown::ImGuiPainterRenderer(*m_p));
            // Stage E4: the panel windows ImGui makes (the platform glue keeps each one's window in its viewport) are drawn by this
            // painter, retargeted to the panel's window with the main window's context, cleared to the colour the sdl side used.
            m_renderer->EnableViewports(&okay::imguiown::ImGuiPlatformGlue::WindowOf, 15, 16, 20);
        }
        return true;
    }
    bool SupportsImGuiViewports() const override { return m_renderer && m_renderer->ViewportsEnabled(); }
    int PanelsDrawn() const override { return m_renderer ? m_renderer->PanelsDrawn() : 0; }
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
        if (!OwnRendererAllowed(want, ownPlatform)) why = "it needs OKAY_EDITOR_PLATFORM=own as well";
        else if (!glWindow || !b) why = "no OpenGL main window";
        else g = CreateOwnEditorGfx(*glWindow, *b, vsync, &why);
    }
    if (!g) g = CreateSdlEditorGfx(sdlWindow, vsync, err);
    if (note) {
        *note = g ? "editor: renderer " + g->Describe() : std::string("editor: renderer: none");
        if (want == RendererKind::Own && (!g || g->Kind() != RendererKind::Own))
            *note += " (OKAY_EDITOR_RENDERER=own could not start: " + why + ")";
    }
    return g;
}

} // namespace okay::editorgfx
