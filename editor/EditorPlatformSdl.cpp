// The editor platform seam's sdl side (OKAY_EDITOR_PLATFORM unset or "sdl", the default): exactly what editor/main.cpp did before
// the seam, call for call - SDL_CreateWindow, ImGui's SDL2 platform backend (ImGui_ImplSDL2_*), and the views' GL context through
// SDL_GL_*. See EditorPlatform.hpp. Also the two SDL helpers the own side needs while SDL_Renderer draws the main window.
#include "EditorPlatform.hpp"

#include <SDL.h>

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"

namespace okay::editorplat {

void SdlCurrentGL(void*& sdlWindow, void*& sdlContext) {
    sdlWindow = SDL_GL_GetCurrentWindow();
    sdlContext = SDL_GL_GetCurrentContext();
}

void SdlRestoreGL(void* sdlWindow, void* sdlContext, bool changedBehindSdl) {
    if (changedBehindSdl && sdlContext) SDL_GL_MakeCurrent(nullptr, nullptr);   // SDL's record no longer says "already current"
    SDL_GL_MakeCurrent(static_cast<SDL_Window*>(sdlWindow), static_cast<SDL_GLContext>(sdlContext));
}

namespace {

class SdlEditorPlatform final : public EditorPlatform {
public:
    ~SdlEditorPlatform() override {
        DestroyViewportGL();
        DestroyMainWindow();
    }
    PlatformKind Kind() const override { return PlatformKind::Sdl; }
    std::string Describe() const override { return "sdl (SDL window, ImGui SDL2 backend, viewport GL through SDL)"; }
    std::string LastError() const override { return SDL_GetError(); }

    void* CreateMainWindow(const std::string& title, int w, int h) override {
        m_win = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
                                 SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN);
        if (m_win) m_view = okay::platform::WrapSdl2Window(m_win);
        return m_win;
    }
    okay::platform::Window* MainWindow() override { return m_view.get(); }
    void DestroyMainWindow() override {
        m_view.reset();
        if (m_win) SDL_DestroyWindow(m_win);
        m_win = nullptr;
    }

    bool InitImGui(void* sdlRenderer) override {
        m_imgui = ImGui_ImplSDL2_InitForSDLRenderer(m_win, static_cast<SDL_Renderer*>(sdlRenderer));
        return m_imgui;
    }
    void ShutdownImGui() override {
        if (m_imgui) ImGui_ImplSDL2_Shutdown();
        m_imgui = false;
    }
    bool ProcessSdlEvent(const void* sdlEvent) override {
        return ImGui_ImplSDL2_ProcessEvent(static_cast<const SDL_Event*>(sdlEvent));
    }
    void NewFrame() override { ImGui_ImplSDL2_NewFrame(); }
    bool SupportsImGuiViewports() const override { return true; }

    bool CreateViewportGL() override {
        const okay::platform::GLAttributes a = ViewportGLAttributes();
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, a.major);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, a.minor);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            a.compatibility ? SDL_GL_CONTEXT_PROFILE_COMPATIBILITY : SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, a.depthBits);
        SDL_Window* prevWin = SDL_GL_GetCurrentWindow();
        SDL_GLContext prevCtx = SDL_GL_GetCurrentContext();
        m_glWin = SDL_CreateWindow("okay-gl", 0, 0, 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        if (m_glWin) {
            m_glCtx = SDL_GL_CreateContext(m_glWin);
            if (!m_glCtx) { SDL_DestroyWindow(m_glWin); m_glWin = nullptr; }
        }
        // SDL_GL_CreateContext leaves the new context current: give back what was current (SDL_Renderer's own context on its
        // OpenGL driver, or none). The caller makes ours current with BeginViewportGL to load the GL functions.
        SDL_GL_MakeCurrent(prevWin, prevCtx);
        return m_glCtx != nullptr;
    }
    bool HasViewportGL() const override { return m_glWin && m_glCtx; }
    bool BeginViewportGL() override {
        m_prevWin = SDL_GL_GetCurrentWindow();
        m_prevCtx = SDL_GL_GetCurrentContext();
        return HasViewportGL() && SDL_GL_MakeCurrent(m_glWin, m_glCtx) == 0;
    }
    void EndViewportGL() override { SDL_GL_MakeCurrent(m_prevWin, m_prevCtx); }
    void* GLProcAddress(const char* name) override { return SDL_GL_GetProcAddress(name); }
    void DestroyViewportGL() override {
        if (m_glCtx) { SDL_GL_DeleteContext(m_glCtx); m_glCtx = nullptr; }
        if (m_glWin) { SDL_DestroyWindow(m_glWin); m_glWin = nullptr; }
    }

private:
    SDL_Window* m_win = nullptr;
    std::unique_ptr<okay::platform::Window> m_view;
    bool m_imgui = false;
    SDL_Window* m_glWin = nullptr;
    SDL_GLContext m_glCtx = nullptr;
    SDL_Window* m_prevWin = nullptr;
    SDL_GLContext m_prevCtx = nullptr;
};

} // namespace

std::unique_ptr<EditorPlatform> CreateSdlEditorPlatform() { return std::unique_ptr<EditorPlatform>(new SdlEditorPlatform()); }

} // namespace okay::editorplat
