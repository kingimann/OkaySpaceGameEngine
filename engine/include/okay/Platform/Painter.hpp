#pragma once
// okay::platform::Painter: the 2D drawing surface of a window, one interface with these implementations.
//
//   software  (own backend; OkayUI::soft::CreateWindowPainter in okayui/soft) draws on the CPU into an
//             RGBA8 frame buffer with OkayUI's software rasteriser and hands the finished frame to the
//             window (Window::PresentPixels: XPutImage on X11, StretchDIBits on Win32). No GPU.
//   opengl    (own backend; CreateGLPainter below, engine/src/Platform/GLPainter.cpp) draws with OpenGL 3.3 core
//             (3.0 / GLSL 1.30 when that is all there is) on the window's own GL context (GLX on X11, WGL on
//             Win32). Entry points come from Backend::GLProcAddress, so there is no link-time GL.
//   sdl2      the web build only (Emscripten's SDL2 port, SdlPainter.cpp): wraps an SDL_Renderer. Not in desktop builds.
//
// It extends okay::ui2d::Painter (the primitive set the in-game UI pass already draws through: colour
// state, rects, lines, clip, textures, copies, triangle geometry) with what an app needs around that:
// the output size, clear, the draw blend mode, texture creation and upload, present and vsync.
//
// The OpenGL painter is the desktop default (shared/OwnPainter.hpp), the software one the fallback when a window has no GL.
// See docs/platform/platform_layer.md.
#include "okay/Platform/Platform.hpp"
#include "okay/UI/UIPainter.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay::platform {

enum class PainterKind { Sdl2, Software, OpenGL };
enum class BlendMode { None, Blend };      // SDL_BLENDMODE_NONE / SDL_BLENDMODE_BLEND (source-over)
enum class ScaleMode { Nearest, Linear };

struct TextureDesc {
    int width = 0, height = 0;
    BlendMode blend = BlendMode::Blend;
    ScaleMode scale = ScaleMode::Nearest;
    /// A texture that is re-uploaded every frame (the 3D view). A hint only; every backend can update any texture.
    bool streaming = false;
    /// A texture that can be drawn into (SetRenderTarget) and then drawn from.
    bool target = false;
};

class Painter : public ui2d::Painter {
public:
    ~Painter() override = default;

    virtual PainterKind Kind() const = 0;
    const char* Name() const { return Kind() == PainterKind::Sdl2 ? "sdl2" : Kind() == PainterKind::OpenGL ? "opengl" : "software"; }
    /// The SDL_Renderer* behind an sdl2 painter (the web build only), else null.
    virtual void* NativeRenderer() = 0;

    /// Size of the render target in pixels (the drawable size of the window).
    virtual void OutputSize(int* w, int* h) = 0;
    /// Fill the whole target with the draw colour (the clip rectangle does not apply), like SDL_RenderClear.
    virtual void Clear() = 0;
    virtual void SetBlendMode(BlendMode m) = 0;       // for FillRect / DrawRect / DrawLine / DrawPoint
    virtual BlendMode GetBlendMode() = 0;
    virtual void FillRectF(const ui2d::FRect* r) = 0;
    virtual void DrawLineF(float x0, float y0, float x1, float y1) = 0;

    // ---- textures (RGBA8, byte order R, G, B, A) ---------------------------------------------
    /// `rgba` (width * height * 4 bytes, top row first) may be null: the texture starts transparent black.
    virtual ui2d::Tex CreateTexture(const TextureDesc& d, const std::uint8_t* rgba) = 0;
    /// Replace `r` (null = the whole texture) with pixels laid out with `pitch` bytes per row.
    virtual bool UpdateTexture(ui2d::Tex t, const ui2d::Rect* r, const void* rgba, int pitch) = 0;
    virtual void DestroyTexture(ui2d::Tex t) = 0;
    virtual void SetTextureBlendMode(ui2d::Tex t, BlendMode m) = 0;
    virtual void SetTextureScaleMode(ui2d::Tex t, ScaleMode m) = 0;

    // ---- render targets, flush, driver (each has a default that says "not supported") ------------
    /// Draw into `t` (a texture made with TextureDesc::target) until the next call; null = back to the window. Clip rectangle and scale are
    /// kept per target. False when unsupported or `t` is not a target. The texture must not be drawn from while it is the target.
    virtual bool SetRenderTarget(ui2d::Tex t) { (void)t; return false; }
    /// The current render target (null = the window).
    virtual ui2d::Tex GetRenderTarget() { return nullptr; }
    /// The current draw colour (false = this painter cannot say; the outputs are then 0).
    virtual bool GetDrawColor(std::uint8_t* r, std::uint8_t* g, std::uint8_t* b, std::uint8_t* a) { if (r) *r = 0; if (g) *g = 0; if (b) *b = 0; if (a) *a = 0; return false; }
    /// Send what was queued to the graphics API now (before code that draws on the same surface another way, e.g. raw GL).
    virtual void Flush() {}
    /// The graphics driver's name ("direct3d11", "opengl", "software", "" = unknown) and whether it is GPU accelerated.
    virtual std::string DriverName() { return std::string(); }
    virtual bool IsAccelerated() { return false; }
    /// Triangles from separate position / colour / uv arrays with byte strides, like SDL_RenderGeometryRaw: positions are float pairs,
    /// colours RGBA bytes, uvs float pairs; `indices` (null = none) are `indexSize` 1, 2 or 4 byte unsigned numbers. The default builds
    /// a Vertex array and calls Geometry, so every painter has it.
    virtual void GeometryRaw(ui2d::Tex t, const float* xy, int xyStride, const std::uint8_t* rgba, int rgbaStride, const float* uv, int uvStride,
                             int numVerts, const void* indices, int numIndices, int indexSize);

    // ---- the frame --------------------------------------------------------------------------
    /// Show what was drawn. Software: copies the frame buffer to the window, then (with SetVSync(true))
    /// waits for the next refresh period, because a software present has no display sync to lean on.
    virtual void Present() = 0;
    /// Ask for vertical sync. Returns true when the display really syncs the present (sdl2 with a
    /// driver that can); false when it only paces (software) or cannot.
    virtual bool SetVSync(bool on) = 0;
    /// The target's pixels as RGBA8, top row first (screenshots, tests). False when unsupported.
    virtual bool ReadPixels(std::vector<std::uint8_t>& rgba, int& w, int& h) = 0;

    // ---- asset helpers shared by every implementation ---------------------------------------
    /// Where LoadTexture also looks for a relative path (the game folder).
    void SetBaseDir(const std::string& dir) { m_baseDir = dir; }
    /// Decode an image file once and cache the texture, blended, nearest-sampled (a miss is cached too).
    /// Tries `path`, then the base directory + `path`.
    ui2d::Tex LoadTexture(const std::string& path) override;
    /// How cached image textures are sampled when drawn at another size: linear (smooth UI) or nearest
    /// (pixel-art). Textures start nearest, as before; the UI pass sets it each frame (UIPainter.hpp).
    void SetImageFilter(bool linear) override;
    void SetTextureFilter(ui2d::Tex t, bool linear) override;
    /// The glyph atlas of a TTF font, uploaded again when the font baked new characters.
    ui2d::Tex FontAtlas(okay::TtfFont* font) override;
    /// An atlas page of the UI batcher (ui2d::BatchPainter) as one painter texture, updated in place or replaced
    /// when its size changed. Page 0 (font glyphs) is sampled linearly, page 1 (images) nearest.
    ui2d::Tex UploadAtlas(const okay::Image& page, int pageIndex, ui2d::Tex existing) override;
    /// The texture bound to `path` in the cache (null when not loaded), without loading.
    ui2d::Tex CachedTexture(const std::string& path) const;

protected:
    /// Derived destructors call this first: destroys the cached textures through the virtual DestroyTexture.
    void ReleaseCachedTextures();

private:
    struct FontUp { ui2d::Tex tex = nullptr; std::uint32_t gen = 0; int w = 0, h = 0; };
    std::string m_baseDir;
    bool m_imgLinear = false;
    std::unordered_map<std::string, ui2d::Tex> m_cache;
    std::unordered_map<okay::TtfFont*, FontUp> m_fonts;
};

struct PainterDesc {
    bool vsync = true;
    /// Fail instead of falling back to a software renderer when no accelerated one can be made (the caller tries something else first).
    bool requireAccelerated = false;
};

#if defined(__EMSCRIPTEN__)
/// sdl2 (the web build only, Emscripten's SDL2 port): a renderer on an sdl2 window (accelerated, else any).
/// Null when `w` is not an sdl2 window or the renderer cannot be made (`err`, when given, says why).
std::unique_ptr<Painter> CreateSdl2Painter(Window& w, const PainterDesc& d = PainterDesc(), std::string* err = nullptr);
#endif

/// opengl (own code, off by default): a GPU painter on a window of the own backend made with WindowDesc::opengl. It creates the
/// window's GL context (Window::CreateGLContext, 3.3 core first, else what the driver gives), loads its entry points through
/// `b.GLProcAddress` and owns the context (destroyed with the painter, which must go before the window). Pixels and rules follow
/// the software painter (same device rounding, source-over blending, nearest = floor(u * w), clip = scissor), so the two agree
/// within GPU rounding. Null (and `err`) for an sdl2 window, a window without a GL pixel format, or a GL older than 3.0.
std::unique_ptr<Painter> CreateGLPainter(Window& w, Backend& b, const PainterDesc& d = PainterDesc(), std::string* err = nullptr);

} // namespace okay::platform
