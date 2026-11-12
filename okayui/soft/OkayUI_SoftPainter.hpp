#pragma once
// The software implementation of okay::platform::Painter: draws on the CPU into an RGBA8 frame buffer with
// the same rasteriser as OkayUI::soft::Render (top-left fill rule, source-over blending), and hands the
// finished frame to the window (Window::PresentPixels). No GPU, no SDL.
//
// OWN CODE, NOT READY: used only when the player runs on the own platform backend
// (OKAY_PLATFORM_BACKEND=own at run time), which is off by default. See docs/platform_layer.md.
//
// What differs from the sdl2 painter, on purpose or because it is not built:
//  * Everything is nearest-neighbour or bilinear on the CPU. There is no GPU anti-aliasing of triangle edges
//    and no mip-mapping, so minified images are sharper and more aliased than a GPU draws them.
//  * It is not fast: the 3D view is already rendered by the engine's software rasteriser and copied here; a
//    full-window blend of many layers costs milliseconds per layer.
//  * Texture size limit 16384 x 16384.
#include "okay/Platform/Painter.hpp"
#include "okay/UI/OkayUI.hpp"

#include <memory>
#include <string>

namespace OkayUI {
namespace soft {

/// A painter on an in-memory frame buffer: no window; Present() is a no-op, ReadPixels() returns the frame.
/// Null for a size outside 1..16384.
std::unique_ptr<okay::platform::Painter> CreateBitmapPainter(int w, int h);

/// A painter on a window: the frame buffer follows the window's size, Present() calls
/// Window::PresentPixels. With PainterDesc::vsync, Present() also waits for the next refresh period
/// (Window::RefreshRateHz, else 60 Hz) so a loop without a frame cap does not spin. Null (and `err`)
/// when the window cannot take pixels.
std::unique_ptr<okay::platform::Painter> CreateWindowPainter(okay::platform::Window& w,
                                                             const okay::platform::PainterDesc& d = okay::platform::PainterDesc(),
                                                             std::string* err = nullptr);

/// Draws OkayUI's finished frame (GetDrawData after EndFrameData) on any platform painter: what
/// EndFrame(SDL_Renderer*) does, expressed through the Painter interface. The glyph atlas of the launcher
/// look and the fallback-font atlases are uploaded as painter textures and kept between frames; commands
/// that name a texture the toolkit's caller made (OkayUI::Image) are passed through as painter textures.
class FrameDrawer {
public:
    explicit FrameDrawer(okay::platform::Painter& p) : m_p(p) {}
    ~FrameDrawer();
    FrameDrawer(const FrameDrawer&) = delete;
    FrameDrawer& operator=(const FrameDrawer&) = delete;
    void Draw(const DrawData& data);
private:
    okay::platform::Painter& m_p;
    okay::ui2d::Tex m_atlas = nullptr;
    const void* m_atlasKey = nullptr;
    int m_atlasW = 0, m_atlasH = 0;
};

} // namespace soft
} // namespace OkayUI
