// The sdl implementation of the launcher's drawing seam (LauncherGfx.hpp): the window, SDL_Renderer and Dear ImGui's
// SDL2 backends exactly as main.cpp made and drove them before the seam existed. The default.
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include "LauncherGfx.hpp"

#include <SDL.h>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

namespace okay::launchergfx {
namespace {

class SdlGfx final : public Gfx {
public:
    SdlGfx(SDL_Window* w, SDL_Renderer* r, std::unique_ptr<okay::platform::Window> pw, std::unique_ptr<okay::platform::Painter> p)
        : m_win(w), m_ren(r), m_pw(std::move(pw)), m_painter(std::move(p)) {}
    ~SdlGfx() override {
        if (m_imgui) ShutdownImGui();
        m_painter.reset();   // a wrapper: it never destroys the renderer
        SDL_DestroyRenderer(m_ren);
        m_pw.reset();        // the seam view goes before the window it looks at
        SDL_DestroyWindow(m_win);
    }
    RendererKind Kind() const override { return RendererKind::Sdl; }
    std::string Describe() const override {
        SDL_RendererInfo info;
        const char* name = SDL_GetRendererInfo(m_ren, &info) == 0 && info.name ? info.name : "?";
        return std::string("sdl (SDL_Renderer ") + name + ", ImGui SDL2 backends)";
    }
    okay::platform::Window& Window() override { return *m_pw; }
    okay::platform::Backend& Backend() override { return *okay::platform::Get(okay::platform::BackendKind::Sdl2); }
    okay::platform::Painter& Painter() override { return *m_painter; }
    void* SdlWindow() override { return m_win; }
    void* SdlRenderer() override { return m_ren; }

    bool InitImGui() override {
        m_imgui = ImGui_ImplSDL2_InitForSDLRenderer(m_win, m_ren) && ImGui_ImplSDLRenderer2_Init(m_ren);
        return m_imgui;
    }
    void ShutdownImGui() override {
        if (!m_imgui) return;
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        m_imgui = false;
    }
    void NewFrame() override {
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
    }
    void ProcessSdlEvent(const void* e) override { if (e) ImGui_ImplSDL2_ProcessEvent((const SDL_Event*)e); }
    void RenderImGui(ImDrawData* dd) override { ImGui_ImplSDLRenderer2_RenderDrawData(dd, m_ren); }

private:
    SDL_Window* m_win;
    SDL_Renderer* m_ren;
    std::unique_ptr<okay::platform::Window> m_pw;
    std::unique_ptr<okay::platform::Painter> m_painter;
    bool m_imgui = false;
};

} // namespace

std::unique_ptr<Gfx> CreateSdlGfx(const WindowSpec& spec, std::string* err) {
    // Created HIDDEN so the OS never composites a black, uninitialised rectangle; main.cpp shows it after the first splash frame.
    const int x = spec.x == okay::platform::kCentered ? (int)SDL_WINDOWPOS_CENTERED : spec.x;
    const int y = spec.y == okay::platform::kCentered ? (int)SDL_WINDOWPOS_CENTERED : spec.y;
    SDL_Window* window = SDL_CreateWindow(spec.title.c_str(), x, y, spec.width, spec.height,
                                          SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
    if (!window) { if (err) *err = SDL_GetError(); return nullptr; }
    std::unique_ptr<okay::platform::Window> pw = okay::platform::WrapSdl2Window(window);   // the seam view of the window
    if (!pw) {
        if (err) *err = "no okay::platform view of the window (the sdl2 platform backend is missing)";
        SDL_DestroyWindow(window);
        return nullptr;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | (spec.vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) { if (err) *err = SDL_GetError(); pw.reset(); SDL_DestroyWindow(window); return nullptr; }
    std::unique_ptr<okay::platform::Painter> painter = okay::platform::WrapSdl2Renderer(renderer);
    return std::unique_ptr<Gfx>(new SdlGfx(window, renderer, std::move(pw), std::move(painter)));
}

} // namespace okay::launchergfx
