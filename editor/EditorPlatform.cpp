// The editor platform seam: the switch, and the own side (OKAY_EDITOR_PLATFORM=own). See EditorPlatform.hpp.
//
// The own side, piece by piece:
//  * Main window: with SDL_Renderer drawing it (stages E1 / E2), okay::platform's sdl2 backend makes it (Backend::MakeWindow), because
//    main.cpp reads SDL's event queue for it. With OKAY_EDITOR_RENDERER=own it is an OpenGL window for our GL painter (EditorGfx.hpp)
//    and, since stage E3, okay::platform::Default() makes it: our own X11 / Win32 code with OKAY_PLATFORM_BACKEND=own. main.cpp then
//    reads that backend's events (EventsFromPlatform) and nothing goes through SDL's queue.
//  * ImGui: our glue (shared/ImGuiOwnPlatform.*, the same code as the launcher's OKAY_LAUNCHER_RENDERER=own) on the window's backend
//    (text input, cursor shapes and the clipboard must act on that window). Events: either okay::platform events straight from that
//    backend (stage E3), or main.cpp reads SDL's queue and each SDL_Event is turned into an okay::platform::Event
//    (TranslateSdl2Event) before the glue sees it.
//  * The views' OpenGL context: a hidden window and a GL context from okay::platform::Default(), so with OKAY_PLATFORM_BACKEND=own it
//    is our own GLX (Linux) or WGL (Windows) code. When SDL_Renderer draws with OpenGL (Linux), its context is given back through
//    SDL after each use (SdlRestoreGL), as SDL keeps its own record of what is current. When our painter draws the main window
//    (stage E3), the main window's own context is made current again instead (no SDL call).
//
// Written from the okay::platform interfaces (engine/include/okay/Platform/Platform.hpp) and imgui.h; no code from elsewhere.
#include "EditorPlatform.hpp"

#include "ImGuiOwnPlatform.hpp"

#include <cstdlib>
#include <cstring>

namespace okay::editorplat {

namespace plat = okay::platform;

PlatformKind ParsePlatform(const char* value) {
    return value && std::strcmp(value, "own") == 0 ? PlatformKind::Own : PlatformKind::Sdl;
}

const char* PlatformName(PlatformKind k) { return k == PlatformKind::Own ? "own" : "sdl"; }

plat::GLAttributes ViewportGLAttributes() {
    plat::GLAttributes a;
    a.major = 3; a.minor = 2;
    a.compatibility = true;
    a.depthBits = 24;
    return a;
}

namespace {

class OwnEditorPlatform final : public EditorPlatform {
public:
    explicit OwnEditorPlatform(plat::Backend* sdl) : m_sdl(sdl) {}
    ~OwnEditorPlatform() override {
        ShutdownImGui();
        DestroyViewportGL();
        DestroyMainWindow();
    }
    PlatformKind Kind() const override { return PlatformKind::Own; }
    std::string Describe() const override {
        std::string s = "own (ImGui glue on okay::platform; viewport GL on ";
        s += m_glBackend ? m_glBackend->Name() : BackendName(plat::DefaultKind());
        if (m_platEvents) s += std::string("; main window and events on ") + (m_winB ? m_winB->Name() : "?") + ")";
        else s += "; the main window is an SDL window)";
        return s;
    }
    std::string LastError() const override { return m_err; }

    void* CreateMainWindow(const std::string& title, int w, int h, bool opengl) override {
        plat::WindowDesc d;
        d.title = title;
        d.width = w; d.height = h;
        d.resizable = true;
        d.highDpi = true;
        d.hidden = true;
        d.opengl = opengl;
        // Our painter draws an OpenGL main window: then it is okay::platform::Default()'s and so are its events (stage E3). With
        // SDL_Renderer drawing, it must be an SDL window (the sdl2 backend's).
        plat::Backend* b = opengl ? plat::Default() : m_sdl;
        if (!b) { m_err = opengl ? "no okay::platform backend" : "okay::platform has no sdl2 backend"; return nullptr; }
        if (b != m_sdl) {   // our own backend: bring its video up (the sdl2 one is up: main.cpp ran SDL_Init)
            plat::InitFlags f;
            f.audio = false; f.gamepad = false;
            if (!b->Init(f)) { m_err = std::string(b->Name()) + ": " + b->LastError(); return nullptr; }
        }
        m_win = b->MakeWindow(d);
        if (!m_win) { m_err = std::string("main window (") + b->Name() + "): " + b->LastError(); return nullptr; }
        m_winB = b;
        m_platEvents = opengl;
        return m_win->NativeHandle();   // the SDL_Window* on the sdl2 backend
    }
    plat::Window* MainWindow() override { return m_win.get(); }
    plat::Backend* MainWindowBackend() override { return m_winB ? m_winB : m_sdl; }
    bool EventsFromPlatform() const override { return m_platEvents && m_win; }
    void DestroyMainWindow() override { m_win.reset(); }

    bool InitImGui(void*) override {
        if (!m_win) { m_err = "InitImGui before CreateMainWindow"; return false; }
        m_glue.reset(new okay::imguiown::ImGuiPlatformGlue(*MainWindowBackend(), *m_win));
        // SDL keeps text input on from start-up and the editor relies on that (typing into a playing game, OkayUI fields), as it
        // did with ImGui's SDL2 backend: do not switch it off between ImGui text fields.
        m_glue->SetTextInputAlwaysOn(true);
        m_glue->SetInputLog(m_log, "editor-input");
        return true;
    }
    void ShutdownImGui() override { m_glue.reset(); }
    bool ProcessSdlEvent(const void* sdlEvent) override {
        if (!m_glue) return false;
        plat::Event e;
        if (!plat::TranslateSdl2Event(sdlEvent, e)) return false;
        return m_glue->ProcessEvent(e);
    }
    bool ProcessEvent(const plat::Event& e) override { return m_glue ? m_glue->ProcessEvent(e) : false; }
    void NewFrame() override { if (m_glue) m_glue->NewFrame(); }
    bool SupportsImGuiViewports() const override { return false; }
    void SetInputLog(bool on) override {
        m_log = on;
        if (m_glue) m_glue->SetInputLog(on, "editor-input");
    }

    bool CreateViewportGL() override {
        DestroyViewportGL();
        plat::Backend* b = plat::Default();
        if (!b) { m_err = "no okay::platform backend"; return false; }
        if (b != m_sdl) {   // our own backend: bring its video up (the sdl2 one is up: SDL_Init ran)
            plat::InitFlags f;
            f.audio = false; f.gamepad = false;
            if (!b->Init(f)) { m_err = std::string(b->Name()) + ": " + b->LastError(); return false; }
        }
        void* prevWin = nullptr; void* prevCtx = nullptr;
        if (!m_platEvents) SdlCurrentGL(prevWin, prevCtx);
        plat::WindowDesc d;
        d.title = "okay-gl";
        d.x = 0; d.y = 0; d.width = 16; d.height = 16;
        d.resizable = false;
        d.opengl = true;
        d.hidden = true;
        m_glWin = b->MakeWindow(d);
        bool ok = false;
        if (!m_glWin) m_err = std::string(b->Name()) + " GL window: " + b->LastError();
        else if (!m_glWin->CreateGLContext(ViewportGLAttributes())) {
            m_err = std::string(b->Name()) + " GL 3.2 compatibility context: " + b->LastError();
            m_glWin.reset();
        } else {
            ok = true;
            m_glBackend = b;
        }
        // CreateGLContext left the new context current: give back SDL_Renderer's (or none), or our painter's (stage E3).
        if (m_platEvents) GiveBackMainContext(b);
        else {
            if (b != m_sdl) b->ReleaseCurrent();
            SdlRestoreGL(prevWin, prevCtx, b != m_sdl);
        }
        return ok;
    }
    bool HasViewportGL() const override { return m_glWin != nullptr; }
    bool BeginViewportGL() override {
        if (!m_platEvents) SdlCurrentGL(m_prevWin, m_prevCtx);
        return m_glWin && m_glWin->MakeCurrent();
    }
    void EndViewportGL() override {
        if (m_platEvents) { GiveBackMainContext(m_glBackend); return; }
        const bool behind = m_glBackend && m_glBackend != m_sdl;
        if (behind) m_glBackend->ReleaseCurrent();
        SdlRestoreGL(m_prevWin, m_prevCtx, behind);
    }
    void* GLProcAddress(const char* name) override { return m_glBackend ? m_glBackend->GLProcAddress(name) : nullptr; }
    void DestroyViewportGL() override {
        if (!m_glWin) return;
        m_glWin->DestroyGLContext();
        m_glWin.reset();
        m_glBackend = nullptr;
    }

private:
    // Stage E3: the views' context is done; the main window's context (our GL painter's) is current again. When the views' backend
    // is not the main window's, its context is released first (two GL libraries must not both think they have one current).
    void GiveBackMainContext(plat::Backend* glB) {
        if (glB && glB != m_winB) glB->ReleaseCurrent();
        if (!m_win || !m_win->MakeCurrent()) {
            if (m_winB) m_winB->ReleaseCurrent();   // the main window has no context yet: leave none current
        }
    }

    plat::Backend* m_sdl;           // the sdl2 backend (null when not compiled in; never called in stage E3 with our own backend)
    plat::Backend* m_winB = nullptr; // the backend that made the main window
    bool m_platEvents = false;      // stage E3: an OpenGL main window from okay::platform::Default(), events from its PollEvent
    std::unique_ptr<plat::Window> m_win;
    std::unique_ptr<okay::imguiown::ImGuiPlatformGlue> m_glue;
    bool m_log = false;
    plat::Backend* m_glBackend = nullptr;
    std::unique_ptr<plat::Window> m_glWin;
    void* m_prevWin = nullptr;
    void* m_prevCtx = nullptr;
    std::string m_err;
};

} // namespace

std::unique_ptr<EditorPlatform> CreateOwnEditorPlatform(std::string* err) {
    plat::Backend* sdl = plat::Get(plat::BackendKind::Sdl2);
    if (!sdl && !plat::Default()) {
        if (err) *err = "okay::platform has no backend in this build";
        return nullptr;
    }
    // Without the sdl2 backend only an OpenGL main window (our renderer, stage E3) can be made; CreateMainWindow says so otherwise.
    return std::unique_ptr<EditorPlatform>(new OwnEditorPlatform(sdl));
}

std::unique_ptr<EditorPlatform> CreateEditorPlatformFromEnv(std::string* note) {
    const PlatformKind want = ParsePlatform(std::getenv("OKAY_EDITOR_PLATFORM"));
    std::unique_ptr<EditorPlatform> p;
    std::string why;
    if (want == PlatformKind::Own) p = CreateOwnEditorPlatform(&why);
    if (!p) p = CreateSdlEditorPlatform();
    if (note) {
        *note = "editor: platform " + p->Describe();
        if (want == PlatformKind::Own && p->Kind() != PlatformKind::Own) *note += " (OKAY_EDITOR_PLATFORM=own could not start: " + why + ")";
    }
    if (const char* l = std::getenv("OKAY_EDITOR_INPUT_LOG"); l && std::strcmp(l, "1") == 0) p->SetInputLog(true);
    return p;
}

} // namespace okay::editorplat
