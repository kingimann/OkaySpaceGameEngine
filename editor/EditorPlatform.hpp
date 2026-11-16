#pragma once
// The editor's platform seam (docs/sdl_removal_plan.md, stage E1): the main window, Dear ImGui's platform side (events in,
// display size, clipboard, cursor shapes, text input) and the hidden OpenGL context the 3D views render into. Two
// implementations, picked at start-up by OKAY_EDITOR_PLATFORM:
//
//   sdl  (the default)  SDL_CreateWindow, ImGui's SDL2 platform backend and SDL_GL_* for the viewport context: what the
//                       editor always did, call for call (EditorPlatformSdl.cpp).
//   own  (opt-in)       ImGui is fed by our own glue (shared/ImGuiOwnPlatform.*, the launcher's) from okay::platform events;
//                       the main window is made through okay::platform's sdl2 backend; the viewport's GL window and context
//                       are made through okay::platform::Default() (sdl2, or our X11 / Win32 code with
//                       OKAY_PLATFORM_BACKEND=own) (EditorPlatform.cpp).
//
// With `own` and SDL_Renderer drawing (stages E1 / E2): the main window is an SDL window made through okay::platform's sdl2 backend,
// and the events are read from SDL's queue (main.cpp reads the SDL_Event; `own` translates each one with
// okay::platform::TranslateSdl2Event for ImGui).
// With `own` and our own renderer (OKAY_EDITOR_RENDERER=own, stage E3): the main window is made by okay::platform::Default() (our
// own X11 / Win32 code with OKAY_PLATFORM_BACKEND=own) with an OpenGL context for our painter, and main.cpp reads its events from
// that backend (PollEvent) instead of SDL's queue (EventsFromPlatform). With OKAY_PLATFORM_BACKEND=own too, the editor makes no
// SDL call at all (scripts/editor_no_sdl_check.sh proves it).
// `own` has no ImGui multi-viewport windows (panels dragged out of the editor stay inside it).
//
// OWN CODE, NOT READY: `own` is off by default until the owner approves it (rule 17). Written from the okay::platform interfaces
// and imgui.h's contract; no code from ImGui's example backends or from SDL.
// Header rules (MinGW): no <mutex> / <thread> here, no SDL types (they stay in EditorPlatformSdl.cpp).
#include "okay/Platform/Platform.hpp"

#include <memory>
#include <string>

namespace okay::editorplat {

enum class PlatformKind { Sdl, Own };

/// What OKAY_EDITOR_PLATFORM asks for: "own" = Own; unset, empty, "sdl" or anything else = Sdl.
PlatformKind ParsePlatform(const char* value);
const char* PlatformName(PlatformKind k);   // "sdl" / "own"

/// What the 3D views need from OpenGL (main.cpp asked SDL for exactly this): 3.2 compatibility (framebuffer objects, multisample
/// renderbuffers and blits, vertex arrays, while still taking the #version 120 shaders), 24-bit depth.
okay::platform::GLAttributes ViewportGLAttributes();

class EditorPlatform {
public:
    virtual ~EditorPlatform() = default;
    virtual PlatformKind Kind() const = 0;
    /// One line for the log, e.g. "own (ImGui glue on okay::platform; viewport GL on sdl2)".
    virtual std::string Describe() const = 0;
    /// Why the last call failed (SDL_GetError or the okay::platform backend's LastError).
    virtual std::string LastError() const = 0;

    // ---- the main window -------------------------------------------------------------------------
    /// The editor's main window: centred, `w` x `h`, resizable, high-DPI, created HIDDEN. Returns the window's native handle (the
    /// SDL_Window* when it is an SDL window); null on failure (see LastError).
    /// `opengl`: the window will carry an OpenGL context (the drawing seam's own renderer, OKAY_EDITOR_RENDERER=own, stage E2). On
    /// `own` this also makes it an okay::platform::Default() window whose events main.cpp reads with PollEvent (stage E3).
    virtual void* CreateMainWindow(const std::string& title, int w, int h, bool opengl = false) = 0;
    /// okay::platform's view of the main window (null before CreateMainWindow).
    virtual okay::platform::Window* MainWindow() = 0;
    /// The okay::platform backend that made the main window: its GL functions (the own renderer), and the keyboard, mouse,
    /// cursor, text input and focus calls main.cpp makes (on the sdl2 backend these are the SDL calls main.cpp made itself).
    /// The sdl2 backend, except on `own` with an OpenGL main window (stage E3): then okay::platform::Default().
    virtual okay::platform::Backend* MainWindowBackend() = 0;
    /// Whether main.cpp reads the events from MainWindowBackend()->PollEvent (and gives them to ImGui with ProcessEvent) instead of
    /// SDL's queue: `own` with an OpenGL main window (stage E3). False before CreateMainWindow and on `sdl`.
    virtual bool EventsFromPlatform() const { return false; }
    virtual void DestroyMainWindow() = 0;

    // ---- Dear ImGui's platform side (the ImGui context exists and is current) ------------------------
    /// Hook ImGui up to the main window. `sdlRenderer` is the SDL_Renderer* that draws it (the sdl side's backend wants it).
    virtual bool InitImGui(void* sdlRenderer) = 0;
    virtual void ShutdownImGui() = 0;
    /// Give ImGui one event that main.cpp read with SDL_PollEvent (`sdlEvent` is a const SDL_Event*). True when ImGui used it.
    virtual bool ProcessSdlEvent(const void* sdlEvent) = 0;
    /// Give ImGui one event read from MainWindowBackend()->PollEvent (EventsFromPlatform()). True when ImGui used it.
    virtual bool ProcessEvent(const okay::platform::Event& e) { (void)e; return false; }
    /// Before ImGui::NewFrame.
    virtual void NewFrame() = 0;
    /// Whether ImGui panels can become their own OS windows (ImGuiConfigFlags_ViewportsEnable). False on `own`.
    virtual bool SupportsImGuiViewports() const = 0;
    /// OKAY_EDITOR_INPUT_LOG=1 on `own`: every mouse button, key and text event ImGui is given goes to stderr ("editor-input: ...").
    virtual void SetInputLog(bool on) { (void)on; }

    // ---- the 3D views' OpenGL context ---------------------------------------------------------------
    /// A hidden 16 x 16 window with a GL context of ViewportGLAttributes(). Leaves whatever context was current before it current
    /// again. False (LastError says why) when the driver cannot give one: the editor keeps its software renderer.
    virtual bool CreateViewportGL() = 0;
    virtual bool HasViewportGL() const = 0;
    /// Remember the context that is current (SDL_Renderer's own one on its OpenGL driver, or none) and make the views' context
    /// current. False when it could not; call EndViewportGL either way.
    virtual bool BeginViewportGL() = 0;
    /// Make the context remembered by BeginViewportGL current again.
    virtual void EndViewportGL() = 0;
    /// A GL function of the views' context (it must be current: inside Begin / End, or right after CreateViewportGL succeeded).
    virtual void* GLProcAddress(const char* name) = 0;
    /// Drop the context and its window (call EndViewportGL first if a Begin is open).
    virtual void DestroyViewportGL() = 0;
};

/// The sdl implementation (EditorPlatformSdl.cpp): SDL directly, as main.cpp did.
#ifdef OKAY_PLATFORM_HAVE_SDL2
std::unique_ptr<EditorPlatform> CreateSdlEditorPlatform();
#else
/// -DOKAY_WITH_SDL=OFF: EditorPlatformSdl.cpp is not built; there is no sdl platform side.
inline std::unique_ptr<EditorPlatform> CreateSdlEditorPlatform() { return nullptr; }
#endif
/// The own implementation (EditorPlatform.cpp). Null (and `err`) when okay::platform has no sdl2 backend compiled in.
std::unique_ptr<EditorPlatform> CreateOwnEditorPlatform(std::string* err = nullptr);
/// OKAY_EDITOR_PLATFORM picks; `own` that cannot start falls back to `sdl` (and says so in `note`). `note` gets one line for the
/// log, e.g. "editor: platform own (ImGui glue on okay::platform; viewport GL on sdl2)".
std::unique_ptr<EditorPlatform> CreateEditorPlatformFromEnv(std::string* note = nullptr);

// ---- helpers the own side needs from SDL while SDL_Renderer draws (EditorPlatformSdl.cpp) -------------
/// The GL context and window SDL has current on this thread (SDL_GL_GetCurrentContext / Window); both null when none.
#ifdef OKAY_PLATFORM_HAVE_SDL2
void SdlCurrentGL(void*& sdlWindow, void*& sdlContext);
/// Make `sdlContext` on `sdlWindow` current through SDL again (both null: none). `changedBehindSdl`: another GL library (our own
/// X11 / Win32 backend) changed the current context without SDL knowing; SDL skips a MakeCurrent that matches what it believes is
/// current, so this first clears SDL's record.
void SdlRestoreGL(void* sdlWindow, void* sdlContext, bool changedBehindSdl);
#else
// -DOKAY_WITH_SDL=OFF: SDL has no context to remember or give back.
inline void SdlCurrentGL(void*& sdlWindow, void*& sdlContext) { sdlWindow = nullptr; sdlContext = nullptr; }
inline void SdlRestoreGL(void*, void*, bool) {}
#endif

} // namespace okay::editorplat
