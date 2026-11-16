// The editor drawing seam's sdl side (OKAY_EDITOR_RENDERER unset or "sdl", the default): exactly what editor/main.cpp did before
// the seam - SDL_Renderer on the main window (the driver hint on Windows, the fallbacks, vsync), and ImGui's SDL_Renderer backend
// (ImGui_ImplSDLRenderer2_*). The painter is okay::platform's sdl2 painter over that renderer, so each texture or frame call is the
// SDL call main.cpp made (SDL_CreateTexture in ABGR8888, SDL_UpdateTexture, SDL_SetTextureBlendMode / ScaleMode,
// SDL_RenderClear, SDL_RenderPresent, SDL_RenderReadPixels, ...). See EditorGfx.hpp.
#include "EditorGfx.hpp"

#include <SDL.h>

#include "imgui.h"
#include "backends/imgui_impl_sdlrenderer2.h"

#include <cstdlib>
#include <cstring>

namespace okay::editorgfx {
namespace {

class SdlEditorGfx final : public EditorGfx {
public:
    SdlEditorGfx(SDL_Renderer* r, std::unique_ptr<okay::platform::Painter> p) : m_ren(r), m_painter(std::move(p)) {}
    ~SdlEditorGfx() override {
        ShutdownImGui();
        m_painter.reset();   // a wrapper: it never destroys the renderer
        SDL_DestroyRenderer(m_ren);
    }
    RendererKind Kind() const override { return RendererKind::Sdl; }
    std::string Describe() const override { return "sdl (SDL_Renderer " + DriverName() + ", ImGui SDL_Renderer backend)"; }
    okay::platform::Painter& Painter() override { return *m_painter; }
    void* SdlRenderer() override { return m_ren; }
    std::string DriverName() const override {
        SDL_RendererInfo ri;
        std::memset(&ri, 0, sizeof(ri));
        return SDL_GetRendererInfo(m_ren, &ri) == 0 && ri.name ? std::string(ri.name) : std::string("?");
    }

    bool InitImGui() override {
        m_imgui = ImGui_ImplSDLRenderer2_Init(m_ren);
        return m_imgui;
    }
    void ShutdownImGui() override {
        if (m_imgui) ImGui_ImplSDLRenderer2_Shutdown();
        m_imgui = false;
    }
    void NewFrame() override { ImGui_ImplSDLRenderer2_NewFrame(); }
    void RenderImGui(ImDrawData* dd) override { ImGui_ImplSDLRenderer2_RenderDrawData(dd, m_ren); }
    void UpdateImGuiTexture(ImTextureData* t) override { ImGui_ImplSDLRenderer2_UpdateTexture(t); }

private:
    SDL_Renderer* m_ren;
    std::unique_ptr<okay::platform::Painter> m_painter;
    bool m_imgui = false;
};

} // namespace

std::unique_ptr<EditorGfx> CreateSdlEditorGfx(void* sdlWindow, bool vsync, std::string* err) {
    SDL_Window* window = static_cast<SDL_Window*>(sdlWindow);
    if (!window) { if (err) *err = "no SDL window"; return nullptr; }
#if defined(_WIN32)
    // Direct3D 11 (SDL's default on Windows is Direct3D 9), so the 3D renderer can share its device and draw the viewports
    // straight into the textures the UI shows: no GPU->CPU read-back and re-upload every frame. OKAY_RENDER_DRIVER overrides
    // (e.g. "direct3d", "opengl", "software").
    if (const char* rd = std::getenv("OKAY_RENDER_DRIVER")) SDL_SetHint(SDL_HINT_RENDER_DRIVER, rd);
    else SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d11");
#endif
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) { SDL_SetHint(SDL_HINT_RENDER_DRIVER, ""); renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED); }
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) { if (err) *err = SDL_GetError(); return nullptr; }
    // VSync off by default = uncapped FPS (no 60 Hz limit). Toggled in View > VSync.
    SDL_RenderSetVSync(renderer, vsync ? 1 : 0);
    std::unique_ptr<okay::platform::Painter> painter = okay::platform::WrapSdl2Renderer(renderer);
    if (!painter) { if (err) *err = "no sdl2 painter"; SDL_DestroyRenderer(renderer); return nullptr; }
    return std::unique_ptr<EditorGfx>(new SdlEditorGfx(renderer, std::move(painter)));
}

} // namespace okay::editorgfx
