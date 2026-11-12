#pragma once
// okay::platform::Painter: the 2D drawing surface of a window, one interface with two implementations.
//
//   sdl2      wraps an SDL_Renderer. Every call is the SDL call it replaces, so the picture is the one
//             the app drew before the interface existed.
//   software  (own backend; OkayUI::soft::CreateWindowPainter in okayui/soft) draws on the CPU into an
//             RGBA8 frame buffer with OkayUI's software rasteriser and hands the finished frame to the
//             window (Window::PresentPixels: XPutImage on X11, StretchDIBits on Win32). No GPU.
//
// It extends okay::ui2d::Painter (the primitive set the in-game UI pass already draws through: colour
// state, rects, lines, clip, textures, copies, triangle geometry) with what an app needs around that:
// the output size, clear, the draw blend mode, texture creation and upload, present and vsync.
//
// OWN CODE, NOT READY: the software implementation is opt-in (OKAY_PLATFORM_BACKEND=own at run time),
// off by default, and has no user-visible setting. See docs/platform_layer.md.
#include "okay/Platform/Platform.hpp"
#include "okay/UI/UIPainter.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay::platform {

enum class PainterKind { Sdl2, Software };
enum class BlendMode { None, Blend };      // SDL_BLENDMODE_NONE / SDL_BLENDMODE_BLEND (source-over)
enum class ScaleMode { Nearest, Linear };

struct TextureDesc {
    int width = 0, height = 0;
    BlendMode blend = BlendMode::Blend;
    ScaleMode scale = ScaleMode::Nearest;
    /// A texture that is re-uploaded every frame (the 3D view). A hint only; every backend can update any texture.
    bool streaming = false;
};

class Painter : public ui2d::Painter {
public:
    ~Painter() override = default;

    virtual PainterKind Kind() const = 0;
    const char* Name() const { return Kind() == PainterKind::Sdl2 ? "sdl2" : "software"; }
    /// The SDL_Renderer* behind an sdl2 painter, else null. Only for libraries that still speak SDL
    /// (OkayUI::EndFrame(SDL_Renderer*), Dear ImGui's backend).
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
    /// The glyph atlas of a TTF font, uploaded again when the font baked new characters.
    ui2d::Tex FontAtlas(okay::TtfFont* font) override;
    /// The texture bound to `path` in the cache (null when not loaded), without loading.
    ui2d::Tex CachedTexture(const std::string& path) const;

protected:
    /// Derived destructors call this first: destroys the cached textures through the virtual DestroyTexture.
    void ReleaseCachedTextures();

private:
    struct FontUp { ui2d::Tex tex = nullptr; std::uint32_t gen = 0; int w = 0, h = 0; };
    std::string m_baseDir;
    std::unordered_map<std::string, ui2d::Tex> m_cache;
    std::unordered_map<okay::TtfFont*, FontUp> m_fonts;
};

struct PainterDesc {
    bool vsync = true;
};

/// sdl2: a renderer on an sdl2 window (accelerated, else any), as the player always made one.
/// Null when `w` is not an sdl2 window or the renderer cannot be made (`err`, when given, says why).
std::unique_ptr<Painter> CreateSdl2Painter(Window& w, const PainterDesc& d = PainterDesc(), std::string* err = nullptr);
/// sdl2 over a renderer somebody else owns (the editor's and the launcher's). `sdlRenderer` is an
/// SDL_Renderer*; the painter never destroys it.
std::unique_ptr<Painter> WrapSdl2Renderer(void* sdlRenderer);
/// Headless sdl2 painter: SDL's software renderer on an RGBA surface. For tests and tools that need the
/// sdl2 implementation without a window. Null when SDL is not compiled in.
std::unique_ptr<Painter> CreateSdl2SurfacePainter(int w, int h);

} // namespace okay::platform
