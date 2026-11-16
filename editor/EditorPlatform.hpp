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
// What stays SDL's in both: the main window is an SDL window (SDL_Renderer draws it by default; with the drawing seam's own
// renderer, OKAY_EDITOR_RENDERER=own (stage E2, EditorGfx.hpp), our GL painter draws it through a GL context SDL made), and the
// events are read from SDL's queue (main.cpp still reads the SDL_Event for its own shortcuts, drops and play-mode input; `own`
// translates each one with okay::platform::TranslateSdl2Event for ImGui).
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
    /// The editor's main window: centred, `w` x `h`, resizable, high-DPI, created HIDDEN. Returns the SDL_Window* (it is an SDL
    /// window in both implementations until stage E3: main.cpp reads SDL's event queue); null on failure (see LastError).
    /// `opengl`: the window will carry an OpenGL context (the drawing seam's own renderer, OKAY_EDITOR_RENDERER=own, stage E2).
    virtual void* CreateMainWindow(const std::string& title, int w, int h, bool opengl = false) = 0;
    /// okay::platform's view of the main window (null before CreateMainWindow).
    virtual okay::platform::Window* MainWindow() = 0;
    /// The okay::platform backend that made the main window (its GL functions, for the own renderer): the sdl2 backend in both
    /// implementations for now; null when okay::platform has no sdl2 backend.
    virtual okay::platform::Backend* MainWindowBackend() = 0;
    virtual void DestroyMainWindow() = 0;

    // ---- Dear ImGui's platform side (the ImGui context exists and is current) ------------------------
    /// Hook ImGui up to the main window. `sdlRenderer` is the SDL_Renderer* that draws it (the sdl side's backend wants it).
    virtual bool InitImGui(void* sdlRenderer) = 0;
    virtual void ShutdownImGui() = 0;
    /// Give ImGui one event that main.cpp read with SDL_PollEvent (`sdlEvent` is a const SDL_Event*). True when ImGui used it.
    virtual bool ProcessSdlEvent(const void* sdlEvent) = 0;
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
std::unique_ptr<EditorPlatform> CreateSdlEditorPlatform();
/// The own implementation (EditorPlatform.cpp). Null (and `err`) when okay::platform has no sdl2 backend compiled in.
std::unique_ptr<EditorPlatform> CreateOwnEditorPlatform(std::string* err = nullptr);
/// OKAY_EDITOR_PLATFORM picks; `own` that cannot start falls back to `sdl` (and says so in `note`). `note` gets one line for the
/// log, e.g. "editor: platform own (ImGui glue on okay::platform; viewport GL on sdl2)".
std::unique_ptr<EditorPlatform> CreateEditorPlatformFromEnv(std::string* note = nullptr);

// ---- helpers the own side needs from SDL while SDL_Renderer draws (EditorPlatformSdl.cpp) -------------
/// The GL context and window SDL has current on this thread (SDL_GL_GetCurrentContext / Window); both null when none.
void SdlCurrentGL(void*& sdlWindow, void*& sdlContext);
/// Make `sdlContext` on `sdlWindow` current through SDL again (both null: none). `changedBehindSdl`: another GL library (our own
/// X11 / Win32 backend) changed the current context without SDL knowing; SDL skips a MakeCurrent that matches what it believes is
/// current, so this first clears SDL's record.
void SdlRestoreGL(void* sdlWindow, void* sdlContext, bool changedBehindSdl);

} // namespace okay::editorplat
