// The editor's platform seam (editor/EditorPlatform.*, OKAY_EDITOR_PLATFORM, docs/sdl_removal_plan.md stage E1), without the editor:
// this program links only the seam, the shared ImGui glue (shared/ImGuiOwnPlatform.*), Dear ImGui with its SDL2 backends,
// okay::platform and SDL2. Run under Xvfb (exit 77 = skipped when there is no display).
//
//   1. The switch: OKAY_EDITOR_PLATFORM values.
//   2. Each implementation (sdl, own): the main window (hidden, its size, okay::platform's view of it); the views' GL context
//      (3.2 compatibility, 24-bit depth, a clear read back through a framebuffer object, and SDL_Renderer's own context current
//      again afterwards, so the window still draws); ImGui fed through the seam: a real pointer warp from the X server, then
//      button, wheel, key and text events; the clipboard both ways; the cursor shape; a dropped file; shut-down.
//      With OKAY_PLATFORM_BACKEND=own (a second ctest entry) the own side's GL window and context are our X11 / GLX code.
//   3. The shared glue against ImGui's SDL2 backend: the same SDL events, one ImGui context each, the same window; after every
//      frame both contexts must hold the same mouse position, buttons, wheel, keys, modifiers, typed characters and focus.
#include "test_framework.hpp"

#include "EditorPlatform.hpp"
#include "okay/Platform/Platform.hpp"

#include <SDL.h>

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ep = okay::editorplat;
namespace plat = okay::platform;

namespace {

// ---- GL bits the test needs (numbers from the OpenGL 3.2 specification) ----
using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
constexpr GLenum kGL_VERSION = 0x1F02, kGL_COLOR_BUFFER_BIT = 0x4000, kGL_RGBA = 0x1908, kGL_UNSIGNED_BYTE = 0x1401;
constexpr GLenum kGL_FRAMEBUFFER = 0x8D40, kGL_RENDERBUFFER = 0x8D41, kGL_COLOR_ATTACHMENT0 = 0x8CE0, kGL_RGBA8 = 0x8058;
constexpr GLenum kGL_FRAMEBUFFER_COMPLETE = 0x8CD5, kGL_CONTEXT_PROFILE_MASK = 0x9126, kGL_DEPTH_BITS = 0x0D56;
constexpr GLenum kGL_DEPTH_COMPONENT24 = 0x81A6, kGL_DEPTH_ATTACHMENT = 0x8D00;
constexpr GLint kCompatibilityBit = 0x2;
#if defined(_WIN32)
#  define TGL_API __stdcall
#else
#  define TGL_API
#endif
struct Gl {
    const unsigned char*(TGL_API* GetString)(GLenum) = nullptr;
    void(TGL_API* GetIntegerv)(GLenum, GLint*) = nullptr;
    GLenum(TGL_API* GetError)() = nullptr;
    void(TGL_API* ClearColor)(float, float, float, float) = nullptr;
    void(TGL_API* Clear)(GLenum) = nullptr;
    void(TGL_API* Viewport)(GLint, GLint, GLint, GLint) = nullptr;
    void(TGL_API* ReadPixels)(GLint, GLint, GLint, GLint, GLenum, GLenum, void*) = nullptr;
    void(TGL_API* GenFramebuffers)(GLint, GLuint*) = nullptr;
    void(TGL_API* BindFramebuffer)(GLenum, GLuint) = nullptr;
    void(TGL_API* DeleteFramebuffers)(GLint, const GLuint*) = nullptr;
    void(TGL_API* GenRenderbuffers)(GLint, GLuint*) = nullptr;
    void(TGL_API* BindRenderbuffer)(GLenum, GLuint) = nullptr;
    void(TGL_API* DeleteRenderbuffers)(GLint, const GLuint*) = nullptr;
    void(TGL_API* RenderbufferStorage)(GLenum, GLenum, GLint, GLint) = nullptr;
    void(TGL_API* FramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint) = nullptr;
    GLenum(TGL_API* CheckFramebufferStatus)(GLenum) = nullptr;
    template <class F> bool Load(ep::EditorPlatform& p, F& f, const char* name) {
        f = reinterpret_cast<F>(p.GLProcAddress(name));
        if (!f) std::fprintf(stderr, "  no %s\n", name);
        return f != nullptr;
    }
    bool LoadAll(ep::EditorPlatform& p) {
        bool ok = true;
        ok &= Load(p, GetString, "glGetString"); ok &= Load(p, GetIntegerv, "glGetIntegerv"); ok &= Load(p, GetError, "glGetError");
        ok &= Load(p, ClearColor, "glClearColor"); ok &= Load(p, Clear, "glClear"); ok &= Load(p, Viewport, "glViewport");
        ok &= Load(p, ReadPixels, "glReadPixels");
        ok &= Load(p, GenFramebuffers, "glGenFramebuffers"); ok &= Load(p, BindFramebuffer, "glBindFramebuffer");
        ok &= Load(p, DeleteFramebuffers, "glDeleteFramebuffers"); ok &= Load(p, GenRenderbuffers, "glGenRenderbuffers");
        ok &= Load(p, BindRenderbuffer, "glBindRenderbuffer"); ok &= Load(p, DeleteRenderbuffers, "glDeleteRenderbuffers");
        ok &= Load(p, RenderbufferStorage, "glRenderbufferStorage");
        ok &= Load(p, FramebufferRenderbuffer, "glFramebufferRenderbuffer");
        ok &= Load(p, CheckFramebufferStatus, "glCheckFramebufferStatus");
        return ok;
    }
};

// ---- SDL events built the way SDL's X11 driver fills them in ----
SDL_Event Motion(Uint32 win, int x, int y) {
    SDL_Event e; SDL_zero(e);
    e.type = SDL_MOUSEMOTION; e.motion.windowID = win; e.motion.x = x; e.motion.y = y;
    return e;
}
SDL_Event Button(Uint32 win, bool down, Uint8 b, int x, int y) {
    SDL_Event e; SDL_zero(e);
    e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    e.button.windowID = win; e.button.button = b; e.button.state = down ? SDL_PRESSED : SDL_RELEASED;
    e.button.clicks = 1; e.button.x = x; e.button.y = y;
    return e;
}
SDL_Event Wheel(Uint32 win, int x, int y) {
    SDL_Event e; SDL_zero(e);
    e.type = SDL_MOUSEWHEEL; e.wheel.windowID = win; e.wheel.x = x; e.wheel.y = y;
    e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
#if SDL_VERSION_ATLEAST(2, 0, 18)
    e.wheel.preciseX = (float)x; e.wheel.preciseY = (float)y;
#endif
    return e;
}
SDL_Event Key(Uint32 win, bool down, SDL_Scancode sc, SDL_Keycode sym, Uint16 mod) {
    SDL_Event e; SDL_zero(e);
    e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.windowID = win; e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    e.key.keysym.scancode = sc; e.key.keysym.sym = sym; e.key.keysym.mod = mod;
    return e;
}
SDL_Event Text(Uint32 win, const char* utf8) {
    SDL_Event e; SDL_zero(e);
    e.type = SDL_TEXTINPUT; e.text.windowID = win;
    std::snprintf(e.text.text, sizeof(e.text.text), "%s", utf8);
    return e;
}
SDL_Event Focus(Uint32 win, bool gained) {
    SDL_Event e; SDL_zero(e);
    e.type = SDL_WINDOWEVENT; e.window.windowID = win;
    e.window.event = gained ? SDL_WINDOWEVENT_FOCUS_GAINED : SDL_WINDOWEVENT_FOCUS_LOST;
    return e;
}

/// Read SDL's queue until `pred` holds for an event or `ms` pass; every event read is handed to `each`.
template <class P, class E> bool PumpUntil(P pred, E each, int ms = 2000) {
    const Uint32 t0 = SDL_GetTicks();
    bool hit = false;
    while (!hit && SDL_GetTicks() - t0 < (Uint32)ms) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) { each(e); if (pred(e)) hit = true; }
        if (!hit) SDL_Delay(5);
    }
    return hit;
}

/// Move the real pointer (an X server warp) into the window and wait until SDL says the pointer is over it.
bool WarpInto(SDL_Window* w, int x, int y, std::function<void(const SDL_Event&)> each) {
    SDL_WarpMouseInWindow(w, x, y);
    return PumpUntil([&](const SDL_Event& e) { return e.type == SDL_MOUSEMOTION && e.motion.x == x && e.motion.y == y; }, each) &&
           SDL_GetMouseFocus() == w;
}

ImGuiContext* NewContext() {
    ImGuiContext* c = ImGui::CreateContext();
    ImGui::SetCurrentContext(c);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigInputTrickleEventQueue = false;   // every queued event this frame, so the two contexts compare frame by frame
    return c;
}

void Frame(SDL_Renderer* r) {
    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), r);
}

// ---- 1. the switch ----
void TestSwitch() {
    RUN_SUITE("OKAY_EDITOR_PLATFORM");
    CHECK(ep::ParsePlatform(nullptr) == ep::PlatformKind::Sdl);
    CHECK(ep::ParsePlatform("") == ep::PlatformKind::Sdl);
    CHECK(ep::ParsePlatform("sdl") == ep::PlatformKind::Sdl);
    CHECK(ep::ParsePlatform("OWN") == ep::PlatformKind::Sdl);   // exact spelling only
    CHECK(ep::ParsePlatform("own") == ep::PlatformKind::Own);
    CHECK(std::strcmp(ep::PlatformName(ep::PlatformKind::Own), "own") == 0);
    CHECK(std::strcmp(ep::PlatformName(ep::PlatformKind::Sdl), "sdl") == 0);
    const plat::GLAttributes a = ep::ViewportGLAttributes();
    CHECK(a.major == 3 && a.minor == 2 && a.compatibility && a.depthBits == 24);   // what main.cpp asked SDL for
}

// ---- 2. one implementation, end to end ----
void TestImplementation(ep::PlatformKind kind) {
    std::printf("[suite] EditorPlatform %s (GL backend default: %s)\n", ep::PlatformName(kind), plat::BackendName(plat::DefaultKind()));
    std::unique_ptr<ep::EditorPlatform> p = kind == ep::PlatformKind::Own ? ep::CreateOwnEditorPlatform() : ep::CreateSdlEditorPlatform();
    CHECK(p != nullptr);
    if (!p) return;
    CHECK(p->Kind() == kind);
    std::printf("  %s\n", p->Describe().c_str());

    SDL_Window* win = static_cast<SDL_Window*>(p->CreateMainWindow("editor-platform-test", 320, 240));
    CHECK(win != nullptr);
    if (!win) { std::fprintf(stderr, "  main window: %s\n", p->LastError().c_str()); return; }
    CHECK(p->MainWindow() != nullptr);
    CHECK((SDL_GetWindowFlags(win) & SDL_WINDOW_HIDDEN) != 0);          // created hidden, like the editor's
    CHECK((SDL_GetWindowFlags(win) & SDL_WINDOW_RESIZABLE) != 0);
    int mw = 0, mh = 0; p->MainWindow()->GetSize(mw, mh);
    CHECK(mw == 320 && mh == 240);
    CHECK(p->MainWindow()->Id() == SDL_GetWindowID(win));
    const Uint32 wid = SDL_GetWindowID(win);

    // SDL_Renderer on OpenGL where it can (Linux's default): its context must be current again after the views' GL work.
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) { SDL_SetHint(SDL_HINT_RENDER_DRIVER, ""); ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE); }
    CHECK(ren != nullptr);
    if (!ren) return;
    SDL_RendererInfo ri; SDL_GetRendererInfo(ren, &ri);
    std::printf("  SDL renderer: %s\n", ri.name);
    SDL_GLContext rendererCtx = SDL_GL_GetCurrentContext();

    // ---- the views' GL context ----
    const bool glOk = p->CreateViewportGL();
    if (!glOk) std::fprintf(stderr, "  viewport GL: %s\n", p->LastError().c_str());
    CHECK(glOk);
    CHECK(SDL_GL_GetCurrentContext() == rendererCtx);   // creating it gave the renderer's context back
    if (glOk) {
        CHECK(p->HasViewportGL());
        CHECK(p->BeginViewportGL());
        Gl gl;
        const bool loaded = gl.LoadAll(*p);
        CHECK(loaded);
        if (loaded) {
            const char* ver = (const char*)gl.GetString(kGL_VERSION);
            std::printf("  viewport GL: %s\n", ver ? ver : "?");
            int maj = 0, min = 0;
            CHECK(ver && std::sscanf(ver, "%d.%d", &maj, &min) == 2 && (maj > 3 || (maj == 3 && min >= 2)));
            GLint prof = 0;
            gl.GetIntegerv(kGL_CONTEXT_PROFILE_MASK, &prof);
            // A 3.2 compatibility request may come back as a higher compatibility version; a pre-3.2 driver reports no mask.
            CHECK((prof & kCompatibilityBit) != 0 || (maj == 3 && min < 2));
            // A framebuffer object with a colour and a 24-bit depth buffer, cleared and read back (what the views do).
            GLuint fbo = 0, rb[2] = {0, 0};
            gl.GenFramebuffers(1, &fbo);
            gl.BindFramebuffer(kGL_FRAMEBUFFER, fbo);
            gl.GenRenderbuffers(2, rb);
            gl.BindRenderbuffer(kGL_RENDERBUFFER, rb[0]);
            gl.RenderbufferStorage(kGL_RENDERBUFFER, kGL_RGBA8, 8, 8);
            gl.FramebufferRenderbuffer(kGL_FRAMEBUFFER, kGL_COLOR_ATTACHMENT0, kGL_RENDERBUFFER, rb[0]);
            gl.BindRenderbuffer(kGL_RENDERBUFFER, rb[1]);
            gl.RenderbufferStorage(kGL_RENDERBUFFER, kGL_DEPTH_COMPONENT24, 8, 8);
            gl.FramebufferRenderbuffer(kGL_FRAMEBUFFER, kGL_DEPTH_ATTACHMENT, kGL_RENDERBUFFER, rb[1]);
            CHECK(gl.CheckFramebufferStatus(kGL_FRAMEBUFFER) == kGL_FRAMEBUFFER_COMPLETE);
            GLint depth = 0; gl.GetIntegerv(kGL_DEPTH_BITS, &depth);
            CHECK(depth >= 24);
            gl.Viewport(0, 0, 8, 8);
            gl.ClearColor(0.0f, 1.0f, 0.0f, 1.0f);
            gl.Clear(kGL_COLOR_BUFFER_BIT);
            unsigned char px[4] = {0, 0, 0, 0};
            gl.ReadPixels(3, 3, 1, 1, kGL_RGBA, kGL_UNSIGNED_BYTE, px);
            CHECK(px[0] == 0 && px[1] == 255 && px[2] == 0);
            gl.BindFramebuffer(kGL_FRAMEBUFFER, 0);
            gl.DeleteRenderbuffers(2, rb);
            gl.DeleteFramebuffers(1, &fbo);
            CHECK(gl.GetError() == 0);
        }
        p->EndViewportGL();
        CHECK(SDL_GL_GetCurrentContext() == rendererCtx);
    }
    // SDL_Renderer still draws into its window: clear to red and read a pixel back.
    SDL_SetRenderDrawColor(ren, 255, 0, 0, 255);
    SDL_RenderClear(ren);
    Uint32 pix = 0;
    SDL_Rect one{5, 5, 1, 1};
    CHECK(SDL_RenderReadPixels(ren, &one, SDL_PIXELFORMAT_RGBA32, &pix, 4) == 0);
    const unsigned char* pb = (const unsigned char*)&pix;
    CHECK(pb[0] == 255 && pb[1] == 0 && pb[2] == 0);

    // ---- ImGui through the seam ----
    ImGuiContext* ctx = NewContext();
    CHECK(p->InitImGui(ren));
    CHECK(ImGui_ImplSDLRenderer2_Init(ren));
    ImGuiIO& io = ImGui::GetIO();
    CHECK(io.BackendPlatformName != nullptr);
    std::printf("  ImGui platform backend: %s\n", io.BackendPlatformName ? io.BackendPlatformName : "?");
    CHECK(p->SupportsImGuiViewports() == (kind == ep::PlatformKind::Sdl));

    auto feed = [&](const SDL_Event& e) { p->ProcessSdlEvent(&e); };
    auto frame = [&](const std::vector<SDL_Event>& evs) {
        for (SDL_Event e : evs) SDL_PushEvent(&e);
        SDL_Event e;
        while (SDL_PollEvent(&e)) feed(e);
        ImGui_ImplSDLRenderer2_NewFrame();
        p->NewFrame();
        ImGui::NewFrame();
    };
    frame({});
    CHECK(io.DisplaySize.x == 320 && io.DisplaySize.y == 240);
    Frame(ren);

    // A real pointer motion from the X server (the window is shown for it), then queued button / wheel / key / text events.
    SDL_ShowWindow(win);
    PumpUntil([](const SDL_Event& e) { return e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_EXPOSED; }, feed, 1000);
    const bool warped = WarpInto(win, 70, 80, feed);
    CHECK(warped);
    frame({});
    CHECK(io.MousePos.x == 70 && io.MousePos.y == 80);
    Frame(ren);
    frame({Button(wid, true, SDL_BUTTON_LEFT, 70, 80), Wheel(wid, 0, 2)});
    CHECK(io.MouseDown[0]);
    CHECK(io.MouseWheel == 2.0f);
    Frame(ren);
    frame({Button(wid, false, SDL_BUTTON_LEFT, 70, 80), Key(wid, true, SDL_SCANCODE_A, SDLK_a, KMOD_LCTRL),
           Text(wid, "h\xC3\xA9")});
    CHECK(!io.MouseDown[0]);
    CHECK(ImGui::IsKeyDown(ImGuiKey_A));
    CHECK(io.KeyCtrl);
    CHECK(io.InputQueueCharacters.Size == 2 && io.InputQueueCharacters[0] == 'h' && io.InputQueueCharacters[1] == 0xE9);
    Frame(ren);

    // The clipboard, both ways, through ImGui (the editor's Ctrl+C / Ctrl+V in its text fields).
    ImGui::SetClipboardText("editor clipboard 1");
    char* sdlClip = SDL_GetClipboardText();
    CHECK(sdlClip && std::strcmp(sdlClip, "editor clipboard 1") == 0);
    SDL_free(sdlClip);
    SDL_SetClipboardText("from the system");
    const char* imClip = ImGui::GetClipboardText();
    CHECK(imClip && std::strcmp(imClip, "from the system") == 0);

    // The cursor shape: ImGui asks for a hand; the next frame puts it on the window (pointer over it).
    frame({Key(wid, false, SDL_SCANCODE_A, SDLK_a, 0)});
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    Frame(ren);
    SDL_Cursor* before = SDL_GetCursor();
    frame({});
    CHECK(SDL_GetCursor() != SDL_GetDefaultCursor());
    CHECK(SDL_GetCursor() != nullptr);
    (void)before;
    Frame(ren);

    // A file dropped on the window: ImGui does not use it, main.cpp reads it from the SDL_Event and frees SDL's string, so the
    // seam must neither eat nor free it. okay::platform's translation copies the path.
    {
        SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
        SDL_Event d; SDL_zero(d);
        d.type = SDL_DROPFILE; d.drop.file = SDL_strdup("/tmp/dropped asset.png"); d.drop.windowID = wid;
        SDL_PushEvent(&d);
        SDL_Event got; bool seen = false;
        while (SDL_PollEvent(&got)) {
            p->ProcessSdlEvent(&got);
            if (got.type == SDL_DROPFILE) {
                seen = true;
                plat::Event pe;
                CHECK(plat::TranslateSdl2Event(&got, pe));
                CHECK(pe.type == plat::EventType::DropFile && pe.text == "/tmp/dropped asset.png");
                CHECK(got.drop.file && std::strcmp(got.drop.file, "/tmp/dropped asset.png") == 0);   // still SDL's, unfreed
                SDL_free(got.drop.file);
            }
        }
        CHECK(seen);
    }

    // ---- shut down in the editor's order ----
    ImGui_ImplSDLRenderer2_Shutdown();
    p->ShutdownImGui();
    CHECK(io.BackendPlatformName == nullptr);
    ImGui::DestroyContext(ctx);
    if (p->HasViewportGL()) {
        CHECK(p->BeginViewportGL());
        p->EndViewportGL();
    }
    p->DestroyViewportGL();
    CHECK(!p->HasViewportGL());
    SDL_DestroyRenderer(ren);
    p->DestroyMainWindow();
    CHECK(p->MainWindow() == nullptr);
    SDL_SetCursor(SDL_GetDefaultCursor());
}

// ---- 3. the shared glue against ImGui's SDL2 backend ----
struct Snapshot {
    float mx = 0, my = 0, wheel = 0, wheelH = 0;
    bool down[5] = {};
    bool ctrl = false, shift = false, alt = false, super = false, focusLost = false;
    std::vector<int> keys;
    std::vector<unsigned> chars;
};

Snapshot Snap() {
    static const ImGuiKey kKeys[] = {ImGuiKey_A, ImGuiKey_Z, ImGuiKey_0, ImGuiKey_9, ImGuiKey_Enter, ImGuiKey_Escape,
        ImGuiKey_Backspace, ImGuiKey_Tab, ImGuiKey_Space, ImGuiKey_Delete, ImGuiKey_LeftArrow, ImGuiKey_RightArrow,
        ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_F1, ImGuiKey_F5, ImGuiKey_F12, ImGuiKey_Keypad7, ImGuiKey_KeypadEnter,
        ImGuiKey_LeftCtrl, ImGuiKey_LeftShift, ImGuiKey_RightAlt, ImGuiKey_Home, ImGuiKey_PageDown, ImGuiKey_Minus,
        ImGuiKey_Slash, ImGuiKey_LeftSuper};
    const ImGuiIO& io = ImGui::GetIO();
    Snapshot s;
    s.mx = io.MousePos.x; s.my = io.MousePos.y; s.wheel = io.MouseWheel; s.wheelH = io.MouseWheelH;
    for (int i = 0; i < 5; ++i) s.down[i] = io.MouseDown[i];
    s.ctrl = io.KeyCtrl; s.shift = io.KeyShift; s.alt = io.KeyAlt; s.super = io.KeySuper; s.focusLost = io.AppFocusLost;
    for (ImGuiKey k : kKeys) if (ImGui::IsKeyDown(k)) s.keys.push_back((int)k);
    for (int i = 0; i < io.InputQueueCharacters.Size; ++i) s.chars.push_back(io.InputQueueCharacters[i]);
    return s;
}

bool Same(const Snapshot& a, const Snapshot& b, int frame) {
    bool ok = a.mx == b.mx && a.my == b.my && a.wheel == b.wheel && a.wheelH == b.wheelH && a.ctrl == b.ctrl && a.shift == b.shift &&
              a.alt == b.alt && a.super == b.super && a.focusLost == b.focusLost && a.keys == b.keys && a.chars == b.chars;
    for (int i = 0; i < 5; ++i) ok = ok && a.down[i] == b.down[i];
    if (!ok) {
        std::fprintf(stderr, "  frame %d differs: sdl2 pos %.0f,%.0f wheel %.1f/%.1f ctrl%d shift%d alt%d super%d lost%d keys %zu chars %zu"
                             " | ours pos %.0f,%.0f wheel %.1f/%.1f ctrl%d shift%d alt%d super%d lost%d keys %zu chars %zu\n", frame,
                     a.mx, a.my, a.wheel, a.wheelH, a.ctrl, a.shift, a.alt, a.super, a.focusLost, a.keys.size(), a.chars.size(),
                     b.mx, b.my, b.wheel, b.wheelH, b.ctrl, b.shift, b.alt, b.super, b.focusLost, b.keys.size(), b.chars.size());
        for (int k : a.keys) std::fprintf(stderr, "    sdl2 key %d\n", k);
        for (int k : b.keys) std::fprintf(stderr, "    ours key %d\n", k);
    }
    return ok;
}

void TestGlueMatchesSdlBackend() {
    RUN_SUITE("shared glue (okay::platform events) == ImGui's SDL2 backend (SDL events)");
    std::unique_ptr<ep::EditorPlatform> own = ep::CreateOwnEditorPlatform();
    CHECK(own != nullptr);
    if (!own) return;
    SDL_Window* win = static_cast<SDL_Window*>(own->CreateMainWindow("glue-vs-sdl2", 400, 300));
    CHECK(win != nullptr);
    if (!win) return;
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    CHECK(ren != nullptr);
    if (!ren) return;
    const Uint32 wid = SDL_GetWindowID(win);

    ImGuiContext* a = NewContext();   // ImGui's SDL2 backend
    CHECK(ImGui_ImplSDL2_InitForSDLRenderer(win, ren));
    CHECK(ImGui_ImplSDLRenderer2_Init(ren));
    ImGuiContext* b = NewContext();   // our glue, through the editor seam
    CHECK(own->InitImGui(ren));
    CHECK(ImGui_ImplSDLRenderer2_Init(ren));

    auto feedBoth = [&](const SDL_Event& e) {
        ImGui::SetCurrentContext(a); ImGui_ImplSDL2_ProcessEvent(&e);
        ImGui::SetCurrentContext(b); own->ProcessSdlEvent(&e);
    };
    // Show the window and put the real pointer over it at 40,50 (both backends then know the pointer is inside).
    SDL_ShowWindow(win);
    PumpUntil([](const SDL_Event& e) { return e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_EXPOSED; }, feedBoth, 1000);
    CHECK(WarpInto(win, 40, 50, feedBoth));

    const Uint16 ctrl = KMOD_LCTRL, shift = KMOD_LSHIFT;
    const std::vector<std::vector<SDL_Event>> frames = {
        {Motion(wid, 40, 50)},
        {Button(wid, true, SDL_BUTTON_LEFT, 40, 50)},
        {Button(wid, false, SDL_BUTTON_LEFT, 40, 50), Button(wid, true, SDL_BUTTON_RIGHT, 40, 50)},
        {Button(wid, false, SDL_BUTTON_RIGHT, 40, 50), Button(wid, true, SDL_BUTTON_MIDDLE, 40, 50)},
        {Button(wid, false, SDL_BUTTON_MIDDLE, 40, 50), Button(wid, true, SDL_BUTTON_X1, 40, 50), Button(wid, true, SDL_BUTTON_X2, 40, 50)},
        {Button(wid, false, SDL_BUTTON_X1, 40, 50), Button(wid, false, SDL_BUTTON_X2, 40, 50), Wheel(wid, 0, 1)},
        {Wheel(wid, 0, -3)},
        {Wheel(wid, 1, 0)},
        {Wheel(wid, -2, 1)},
        {Key(wid, true, SDL_SCANCODE_LCTRL, SDLK_LCTRL, ctrl), Key(wid, true, SDL_SCANCODE_Z, SDLK_z, ctrl)},
        {Key(wid, false, SDL_SCANCODE_Z, SDLK_z, ctrl), Key(wid, false, SDL_SCANCODE_LCTRL, SDLK_LCTRL, 0)},
        {Key(wid, true, SDL_SCANCODE_LSHIFT, SDLK_LSHIFT, shift), Key(wid, true, SDL_SCANCODE_A, SDLK_a, shift), Text(wid, "A")},
        {Key(wid, false, SDL_SCANCODE_A, SDLK_a, shift), Key(wid, false, SDL_SCANCODE_LSHIFT, SDLK_LSHIFT, 0)},
        {Key(wid, true, SDL_SCANCODE_RALT, SDLK_RALT, KMOD_RALT), Key(wid, true, SDL_SCANCODE_LGUI, SDLK_LGUI, KMOD_RALT | KMOD_LGUI)},
        {Key(wid, false, SDL_SCANCODE_RALT, SDLK_RALT, KMOD_LGUI), Key(wid, false, SDL_SCANCODE_LGUI, SDLK_LGUI, 0)},
        {Key(wid, true, SDL_SCANCODE_F5, SDLK_F5, 0), Key(wid, true, SDL_SCANCODE_RETURN, SDLK_RETURN, 0),
         Key(wid, true, SDL_SCANCODE_KP_7, SDLK_KP_7, 0), Key(wid, true, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE, 0),
         Key(wid, true, SDL_SCANCODE_KP_ENTER, SDLK_KP_ENTER, 0), Key(wid, true, SDL_SCANCODE_F12, SDLK_F12, 0)},
        {Key(wid, false, SDL_SCANCODE_F5, SDLK_F5, 0), Key(wid, false, SDL_SCANCODE_RETURN, SDLK_RETURN, 0),
         Key(wid, false, SDL_SCANCODE_KP_7, SDLK_KP_7, 0), Key(wid, false, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE, 0),
         Key(wid, false, SDL_SCANCODE_KP_ENTER, SDLK_KP_ENTER, 0), Key(wid, false, SDL_SCANCODE_F12, SDLK_F12, 0)},
        {Key(wid, true, SDL_SCANCODE_LEFT, SDLK_LEFT, 0), Key(wid, true, SDL_SCANCODE_HOME, SDLK_HOME, 0),
         Key(wid, true, SDL_SCANCODE_PAGEDOWN, SDLK_PAGEDOWN, 0), Key(wid, true, SDL_SCANCODE_DELETE, SDLK_DELETE, 0),
         Key(wid, true, SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE, 0), Key(wid, true, SDL_SCANCODE_TAB, SDLK_TAB, 0),
         Key(wid, true, SDL_SCANCODE_SPACE, SDLK_SPACE, 0), Key(wid, true, SDL_SCANCODE_0, SDLK_0, 0),
         Key(wid, true, SDL_SCANCODE_9, SDLK_9, 0), Key(wid, true, SDL_SCANCODE_MINUS, SDLK_MINUS, 0),
         Key(wid, true, SDL_SCANCODE_SLASH, SDLK_SLASH, 0), Key(wid, true, SDL_SCANCODE_F1, SDLK_F1, 0)},
        {Key(wid, false, SDL_SCANCODE_LEFT, SDLK_LEFT, 0), Key(wid, false, SDL_SCANCODE_HOME, SDLK_HOME, 0),
         Key(wid, false, SDL_SCANCODE_PAGEDOWN, SDLK_PAGEDOWN, 0), Key(wid, false, SDL_SCANCODE_DELETE, SDLK_DELETE, 0),
         Key(wid, false, SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE, 0), Key(wid, false, SDL_SCANCODE_TAB, SDLK_TAB, 0),
         Key(wid, false, SDL_SCANCODE_SPACE, SDLK_SPACE, 0), Key(wid, false, SDL_SCANCODE_0, SDLK_0, 0),
         Key(wid, false, SDL_SCANCODE_9, SDLK_9, 0), Key(wid, false, SDL_SCANCODE_MINUS, SDLK_MINUS, 0),
         Key(wid, false, SDL_SCANCODE_SLASH, SDLK_SLASH, 0), Key(wid, false, SDL_SCANCODE_F1, SDLK_F1, 0)},
        {Text(wid, "h\xC3\xA9\xE2\x82\xAC")},   // "hé€"
        {Text(wid, "\xF0\x9F\x98\x80")},        // an emoji: outside the BMP
        {Focus(wid, false)},
        {Focus(wid, true)},
        {Motion(wid, 41, 52), Button(wid, true, SDL_BUTTON_LEFT, 41, 52)},
        {Button(wid, false, SDL_BUTTON_LEFT, 41, 52), Motion(wid, 40, 50)},
    };
    int sameFrames = 0;
    for (std::size_t f = 0; f < frames.size(); ++f) {
        for (SDL_Event e : frames[f]) SDL_PushEvent(&e);
        SDL_Event e;
        while (SDL_PollEvent(&e)) feedBoth(e);
        ImGui::SetCurrentContext(a);
        ImGui_ImplSDLRenderer2_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
        const Snapshot sa = Snap();
        Frame(ren);
        ImGui::SetCurrentContext(b);
        ImGui_ImplSDLRenderer2_NewFrame(); own->NewFrame(); ImGui::NewFrame();
        const Snapshot sb = Snap();
        Frame(ren);
        if (Same(sa, sb, (int)f)) ++sameFrames;
    }
    std::printf("  %d of %zu frames identical\n", sameFrames, frames.size());
    CHECK(sameFrames == (int)frames.size());

    ImGui::SetCurrentContext(a);
    ImGui_ImplSDLRenderer2_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext(a);
    ImGui::SetCurrentContext(b);
    ImGui_ImplSDLRenderer2_Shutdown(); own->ShutdownImGui(); ImGui::DestroyContext(b);
    SDL_DestroyRenderer(ren);
    own->DestroyMainWindow();
}

} // namespace

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    TestSwitch();
    if (!std::getenv("DISPLAY")) { std::printf("SKIP: no display (run under Xvfb)\n"); return 77; }
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
        std::printf("SKIP: SDL video: %s\n", SDL_GetError());
        return 77;
    }
    TestImplementation(ep::PlatformKind::Sdl);
    TestImplementation(ep::PlatformKind::Own);
    TestGlueMatchesSdlBackend();
    SDL_Quit();
    TEST_MAIN_RESULT();
}
