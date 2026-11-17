#pragma once
// The editor's drawing seam (docs/sdl_removal_plan.md, stage E2): what draws the main window. The painter (textures from RGBA
// pixels, the frame's clear and present, vsync, read-back) and Dear ImGui's renderer side. Two implementations, picked at start-up
// by OKAY_EDITOR_RENDERER:
//
//   sdl  (the default)  SDL_Renderer on the SDL main window (Direct3D 11 on Windows, unless OKAY_RENDER_DRIVER says otherwise),
//                       okay::platform's sdl2 painter over it (each painter call is the SDL call main.cpp made before) and ImGui's
//                       SDL_Renderer backend: what the editor always did (EditorGfxSdl.cpp). Textures are SDL_Texture*.
//   own  (opt-in)       our OpenGL painter (engine/src/Platform/GlPainter.cpp) on the main window's own GL context and our own ImGui
//                       renderer (shared/ImGuiOwnRenderer.*, the launcher's). No SDL_Renderer, no ImGui SDL_Renderer backend
//                       (EditorGfx.cpp). Textures are the GL painter's.
//
// `own` needs OKAY_EDITOR_PLATFORM=own too (the platform seam then makes the main window with an OpenGL context); with the sdl
// platform the editor says so in the log and stays on `sdl`.
//
// Multi-window panels on `own` (stage E4): our ImGui renderer draws each panel window with this painter, retargeted to the panel's
// window (Painter::SetTarget) with the main window's GL context, so a panel window shows every texture (the sdl side's panel windows
// have an SDL_Renderer each and skip the editor's pictures). What the own side does not do yet (it keeps SDL_Renderer and only runs
// on `sdl`): the Direct3D 11 views drawn straight into SDL's textures on Windows (on `own` the views use the read-back path),
// and the OkayUI test overlay is drawn with OkayUI's own painter backend when the build has it.
//
// The 3D views render with OpenGL in a separate hidden context (EditorPlatform's viewport GL) and are read back into RGBA pixels,
// which are uploaded into a painter texture each frame (UpdateTexture), on both sides. The contexts are not shared.
//
// OWN CODE, NOT READY: `own` is off by default until the owner approves it (rule 17). Written from the okay::platform Painter
// interface and imgui.h's contract; no code from ImGui's example backends or from SDL.
// Header rules (MinGW): no <mutex> / <thread> here, no SDL types (they stay in EditorGfxSdl.cpp).
#include "okay/Platform/Painter.hpp"
#include "okay/Platform/Platform.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct ImDrawData;
struct ImTextureData;

namespace okay::editorgfx {

enum class RendererKind { Sdl, Own };

/// What OKAY_EDITOR_RENDERER asks for: "own" = Own; unset, empty, "sdl" or anything else = Sdl.
RendererKind ParseRenderer(const char* value);
const char* RendererName(RendererKind k);   // "sdl" / "own"
/// Whether the own renderer can be used with this platform choice: only together with OKAY_EDITOR_PLATFORM=own (the main window
/// then gets an OpenGL context). `ownPlatform` = the platform seam that really started is the own one.
inline bool OwnRendererAllowed(RendererKind want, bool ownPlatform) { return want == RendererKind::Own && ownPlatform; }

/// A texture description in one line: `w` x `h`, RGBA8, blended unless `blend` is false, linear or nearest sampling, and the
/// streaming hint for a texture uploaded every frame (the 3D views).
inline okay::platform::TextureDesc TexDesc(int w, int h, bool linear, bool streaming = false, bool blend = true) {
    okay::platform::TextureDesc d;
    d.width = w; d.height = h;
    d.blend = blend ? okay::platform::BlendMode::Blend : okay::platform::BlendMode::None;
    d.scale = linear ? okay::platform::ScaleMode::Linear : okay::platform::ScaleMode::Nearest;
    d.streaming = streaming;
    return d;
}

class EditorGfx {
public:
    virtual ~EditorGfx() = default;
    virtual RendererKind Kind() const = 0;
    /// One line for the log, e.g. "own (opengl painter on a sdl2 window, own ImGui renderer)".
    virtual std::string Describe() const = 0;
    /// The painter that draws the main window. Every texture the panels show is one of its textures (ImTextureID = the
    /// okay::ui2d::Tex): an SDL_Texture* on `sdl`, the GL painter's on `own`.
    virtual okay::platform::Painter& Painter() = 0;
    /// The SDL_Renderer* on `sdl` (for what still speaks SDL: multi-window panels, OkayUI::EndFrame, the Direct3D 11 device);
    /// null on `own`.
    virtual void* SdlRenderer() = 0;
    /// The renderer's name for crash reports ("direct3d11", "opengl", ...; "own opengl painter" on own).
    virtual std::string DriverName() const = 0;

    // ---- Dear ImGui's renderer side (the ImGui context exists and is current) ----------------------------
    virtual bool InitImGui() = 0;
    virtual void ShutdownImGui() = 0;
    /// Before ImGui::NewFrame.
    virtual void NewFrame() = 0;
    /// Draw ImGui's draw data on the painter (after Clear, before Present).
    virtual void RenderImGui(ImDrawData* dd) = 0;
    /// Honour one ImGui texture request without drawing (the multi-window path draws the draw data itself).
    virtual void UpdateImGuiTexture(ImTextureData* t) = 0;
    /// Whether this side draws ImGui's panel windows itself (ImGuiBackendFlags_RendererHasViewports, set by InitImGui): `own` with our
    /// GL painter (stage E4). The sdl side's panel windows are main.cpp's SDL_Renderer code (OkayVp*): false here.
    virtual bool SupportsImGuiViewports() const { return false; }
    /// Panel-window draws so far (own; 0 on sdl). For the log.
    virtual int PanelsDrawn() const { return 0; }

    // ---- the frame (the same on both: the painter does it) ------------------------------------------------
    /// Ask for vertical sync (View > VSync).
    void SetVSync(bool on) { Painter().SetVSync(on); }
    /// The drawable size in pixels.
    void OutputSize(int& w, int& h) { w = h = 0; Painter().OutputSize(&w, &h); }
    /// Start a frame: the framebuffer scale, then fill with (r, g, b).
    void BeginFrame(float scaleX, float scaleY, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        Painter().SetScale(scaleX, scaleY);
        Painter().SetDrawColor(r, g, b, 255);
        Painter().Clear();
    }
    void Present() { Painter().Present(); }
    /// What was drawn this frame as RGBA8, top row first (screenshots). Call before Present.
    bool ReadPixels(std::vector<std::uint8_t>& rgba, int& w, int& h) { return Painter().ReadPixels(rgba, w, h); }

    // ---- textures (the same on both) -------------------------------------------------------------------
    /// A texture from tightly packed RGBA8 pixels (R, G, B, A bytes, top row first; null = transparent). Null on failure.
    okay::ui2d::Tex CreateTexture(const okay::platform::TextureDesc& d, const void* rgba) {
        return Painter().CreateTexture(d, static_cast<const std::uint8_t*>(rgba));
    }
    /// Replace the whole texture with `pitch` bytes per row.
    bool UpdateTexture(okay::ui2d::Tex t, const void* rgba, int pitch) { return Painter().UpdateTexture(t, nullptr, rgba, pitch); }
    void DestroyTexture(okay::ui2d::Tex t) { if (t) Painter().DestroyTexture(t); }
    void QueryTexture(okay::ui2d::Tex t, int& w, int& h) { w = h = 0; if (t) Painter().QueryTexture(t, &w, &h); }
    void SetTextureLinear(okay::ui2d::Tex t, bool linear) {
        if (t) Painter().SetTextureScaleMode(t, linear ? okay::platform::ScaleMode::Linear : okay::platform::ScaleMode::Nearest);
    }
};

/// The sdl implementation (EditorGfxSdl.cpp): SDL_Renderer on `sdlWindow` (an SDL_Window*), made exactly as main.cpp made it:
/// on Windows the "direct3d11" driver unless OKAY_RENDER_DRIVER names another, then any accelerated driver, then any; vsync as
/// asked. Null (and `err`) when SDL cannot make a renderer.
#ifdef OKAY_PLATFORM_HAVE_SDL2
std::unique_ptr<EditorGfx> CreateSdlEditorGfx(void* sdlWindow, bool vsync, std::string* err = nullptr);
#else
/// -DOKAY_WITH_SDL=OFF: EditorGfxSdl.cpp is not built; there is no sdl renderer.
inline std::unique_ptr<EditorGfx> CreateSdlEditorGfx(void*, bool, std::string* err = nullptr) {
    if (err) *err = "this build has no SDL (OKAY_WITH_SDL=OFF)";
    return nullptr;
}
#endif
/// The own implementation (EditorGfx.cpp): our GL painter on `w` (made by `b`, with WindowDesc::opengl). Null (and `err`) when
/// the window cannot carry a GL context.
std::unique_ptr<EditorGfx> CreateOwnEditorGfx(okay::platform::Window& w, okay::platform::Backend& b, bool vsync,
                                              std::string* err = nullptr);

/// OKAY_EDITOR_RENDERER picks. `own` is used only when `ownPlatform` (OKAY_EDITOR_PLATFORM=own started) and `glWindow` / `b`
/// are given; otherwise, or when it cannot start, `sdl` on `sdlWindow` (and `note` says why). `note` gets one line for the log,
/// e.g. "editor: renderer own (opengl painter on a sdl2 window, own ImGui renderer)". Null only when `sdl` fails too (`err`).
std::unique_ptr<EditorGfx> CreateEditorGfxFromEnv(void* sdlWindow, okay::platform::Window* glWindow, okay::platform::Backend* b,
                                                  bool ownPlatform, bool vsync, std::string* note, std::string* err);

} // namespace okay::editorgfx
