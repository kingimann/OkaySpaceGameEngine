#pragma once
// OkayUI's own renderer inside an app that draws with an SDL_Renderer (the launcher and the editor).
//
// OWN CODE, OFF BY DEFAULT. The launcher and the editor end every OkayUI frame with okay::pb::OkayUIEndFrame (shared/OkayUIFrame.hpp).
// Without the switch that is OkayUI::EndFrame(SDL_Renderer*): OkayUI's frame goes to the screen as SDL_RenderGeometry calls on the same
// SDL renderer Dear ImGui's SDL backend draws with. With OKAY_UI_OWN_RENDER=1 the frame goes through SdlHost instead, which draws it with
// OkayUI's own render::Renderer into the same window, on top of what SDL and Dear ImGui drew, with the backend that matches SDL's renderer:
//
//   SDL "opengl" (needs a 3.3 context) or "opengles2" (needs ES 3.0)  ->  the OpenGL backend (OkayUIRender_GL.cpp) in SDL's GL context.
//   SDL "direct3d11" (the Windows default)                            ->  the Direct3D 11 backend (OkayUIRender_D3D11.cpp) on SDL's
//                                                                         device (SDL_RenderGetD3D11Device), into SDL's render target.
//
// Either way SDL's queued drawing is flushed first (SDL_RenderFlush, which SDL documents for mixing your own graphics calls with its
// renderer) and the backend puts back the GL or Direct3D state it changed, so SDL's renderer carries on as if nothing happened.
//
// Host images (an SDL_Texture handed to OkayUI::Image / DrawTexture) are drawn by the own renderer too, without reading pixels back:
//   OpenGL       SDL_GL_BindTexture tells which GL texture SDL keeps for it; the backend samples that texture directly.
//   Direct3D 11  SDL2 has no call that hands out a texture's Direct3D object, so each frame the SDL texture is copied by SDL itself
//                (SDL_RenderCopy, blending off) into a render-target texture of the same size that the host keeps for it (a "mirror");
//                that copy leaves the mirror bound as the render target, which is where its ID3D11Texture2D is read
//                (OMGetRenderTargets). One GPU copy per image per frame; a changed SDL texture shows its new pixels in the next frame.
// An image that has not been used for two frames is let go of (the mirror is freed).
//
// When the own renderer cannot draw a frame the frame takes the old path, OkayUI::DrawFinishedFrame (the same SDL_RenderGeometry calls),
// and HostFrame::reason says why. That happens when:
//   * the SDL renderer is neither OpenGL nor Direct3D 11 (Direct3D 9 "direct3d", Metal, the software renderer);
//   * the GL context is too old, an entry point is missing, a shader does not build, or SDL gives no Direct3D 11 device;
//   * a render target texture is set, a logical size is set, the viewport is offset, or the x and y scales differ;
//   * a host image cannot be shared (not an SDL texture of this renderer, a YUV texture on OpenGL, a padded or rectangle GL texture,
//     larger than 8192 pixels on Direct3D 11) - the whole frame falls back, so the draw order stays right;
//   * Direct3D 11: SDL's render target cannot be found after the flush, or its size is not the renderer's output size.
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
enum class HostPath { None, OwnGL, OwnD3D11, Sdl };

struct HostFrame {
    HostPath    path = HostPath::None;
    const char* reason = "";      // why the SDL path was taken, or "" (static text, never freed)
    GLFlavor    flavor = GLFlavor::Core33;   // OwnGL only
    Stats       stats;            // what the own renderer reported (OwnGL / OwnD3D11 only)
    int         images = 0;       // host images the own renderer drew from SDL textures this frame
};

/// The switch: OKAY_UI_OWN_RENDER. On for "1", "on", "true", "yes" (any case); off when unset, empty, "0" or anything else.
bool OwnRenderSwitchValue(const char* value);
bool OwnRenderSwitchOn();

/// The OpenGL flavour the own renderer would use with an SDL renderer called `driverName` (SDL_RendererInfo::name) whose context
/// reports `glVersion` (glGetString(GL_VERSION)). False, with `why`, when it cannot (not a GL renderer, a context below 3.3 / ES 3.0).
bool PickGLFlavor(const char* driverName, const char* glVersion, GLFlavor* flavor, const char** why);
/// Which own backend can draw into an SDL renderer called `driverName`: OwnGL ("opengl", "opengles2"), OwnD3D11 ("direct3d11"; it
/// exists on Windows only, so elsewhere no SDL renderer has that name), else Sdl with `why`.
HostPath HostBackendFor(const char* driverName, const char** why);

class SdlHost {
public:
    SdlHost();
    ~SdlHost();                   // makes no GPU or SDL calls (the contexts may be gone): Forget each renderer before it is destroyed
    SdlHost(const SdlHost&) = delete;
    SdlHost& operator=(const SdlHost&) = delete;

    /// Finishes OkayUI's frame (OkayUI::EndFrameData with the renderer's output size, which also builds the toasts) and draws it into
    /// `r`: with the own renderer when it can, else through OkayUI::DrawFinishedFrame. Does not clear and does not present.
    const HostFrame& EndFrame(SDL_Renderer* r);
    /// The same frame through the SDL path only (the switch is off): exactly OkayUI::EndFrame(r), recorded as HostPath::Sdl.
    const HostFrame& EndFrameSdl(SDL_Renderer* r);
    const HostFrame& Last() const;

    /// Free what the own renderer keeps for `r` (its textures, buffers, shaders and image mirrors) while `r` is still alive; call before
    /// it is destroyed.
    void Forget(SDL_Renderer* r);
    /// How many renderers have own-renderer state (tests).
    int Count() const;
    /// How many host images (SDL textures) the own renderer holds for all renderers (tests).
    int ImageCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

} // namespace render
} // namespace OkayUI
