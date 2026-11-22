#pragma once
// OkayUI's own OpenGL renderer inside an app that draws with an SDL_Renderer (the launcher and the editor).
//
// OWN CODE, OFF BY DEFAULT. The launcher and the editor end every OkayUI frame with okay::pb::OkayUIEndFrame (shared/OkayUIFrame.hpp).
// Without the switch that is OkayUI::EndFrame(SDL_Renderer*): OkayUI's frame goes to the screen as SDL_RenderGeometry calls on the same
// SDL renderer Dear ImGui's SDL backend draws with. With OKAY_UI_OWN_RENDER=1 the frame goes through SdlGLHost instead: when the SDL
// renderer runs on OpenGL ("opengl", which needs a 3.3 context, or "opengles2", which needs an ES 3.0 one), SDL's queued drawing is
// flushed, SDL's GL context is made current, and OkayUI's own render::Renderer with the OpenGL backend (OkayUIRender_GL.cpp) draws the
// frame into the same window, on top of what SDL and Dear ImGui drew. The GL state the backend changes is put back, so SDL's renderer
// carries on as if nothing happened (SDL documents SDL_RenderFlush for exactly this: mixing your own GL calls with its renderer).
//
// When the own renderer cannot draw a frame the frame takes the old path, OkayUI::DrawFinishedFrame (the same SDL_RenderGeometry calls),
// and HostFrame::reason says why. That happens when:
//   * the SDL renderer is not an OpenGL one (Windows uses "direct3d11" by default: there the old path stays, see
//     docs/ui/own_ui_toolkit.md section 7; OKAY_RENDER_DRIVER=opengl selects SDL's OpenGL renderer there);
//   * the GL context is too old, an entry point is missing or the shader does not build;
//   * a render target texture is set, a logical size is set, the viewport is offset, or the x and y scales differ;
//   * the frame shows a host image (an SDL_Texture handed to OkayUI::Image / DrawTexture): the own renderer only holds images given to
//     Renderer::SetImage, and the SDL path can draw SDL textures (a whole frame falls back, so the draw order stays right).
//
// Coordinates: OkayUI draws in the renderer's logical pixels; the DPI scale is SDL_RenderGetScale and the framebuffer is
// SDL_GetRendererOutputSize, the same mapping SDL_RenderGeometry applies on the old path.
//
// Threads: none. Call it from the thread that owns the SDL renderer.
#include "okay/UI/OkayUIRender.hpp"
#include "okay/UI/OkayUIRenderBackends.hpp"   // GLFlavor

#include <memory>

struct SDL_Renderer;

namespace OkayUI {
namespace render {

/// Which renderer drew the last frame.
enum class HostPath { None, OwnGL, Sdl };

struct HostFrame {
    HostPath    path = HostPath::None;
    const char* reason = "";      // why the SDL path was taken, or "" (static text, never freed)
    GLFlavor    flavor = GLFlavor::Core33;
    Stats       stats;            // what the own renderer reported (OwnGL only)
};

/// The switch: OKAY_UI_OWN_RENDER. On for "1", "on", "true", "yes" (any case); off when unset, empty, "0" or anything else.
bool OwnRenderSwitchValue(const char* value);
bool OwnRenderSwitchOn();

/// The OpenGL flavour the own renderer would use with an SDL renderer called `driverName` (SDL_RendererInfo::name) whose context
/// reports `glVersion` (glGetString(GL_VERSION)). False, with `why`, when it cannot (not a GL renderer, a context below 3.3 / ES 3.0).
bool PickGLFlavor(const char* driverName, const char* glVersion, GLFlavor* flavor, const char** why);

class SdlGLHost {
public:
    SdlGLHost();
    ~SdlGLHost();                 // makes no GL calls (the contexts may be gone): Forget each renderer before it is destroyed
    SdlGLHost(const SdlGLHost&) = delete;
    SdlGLHost& operator=(const SdlGLHost&) = delete;

    /// Finishes OkayUI's frame (OkayUI::EndFrameData with the renderer's output size, which also builds the toasts) and draws it into
    /// `r`: with the own OpenGL renderer when it can, else through OkayUI::DrawFinishedFrame. Does not clear and does not present.
    const HostFrame& EndFrame(SDL_Renderer* r);
    /// The same frame through the SDL path only (the switch is off): exactly OkayUI::EndFrame(r), recorded as HostPath::Sdl.
    const HostFrame& EndFrameSdl(SDL_Renderer* r);
    const HostFrame& Last() const;

    /// Free what the own renderer keeps for `r` (its textures, buffers and shader) while `r` is still alive; call before it is destroyed.
    void Forget(SDL_Renderer* r);
    /// How many renderers have own-renderer state (tests).
    int Count() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

} // namespace render
} // namespace OkayUI
