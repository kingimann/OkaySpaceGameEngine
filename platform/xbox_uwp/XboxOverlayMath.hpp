#pragma once
// Pure maths for the Xbox UI overlay (the software-painted 2D / UI layer composited over the 3D frame): how big the UI buffer is, the DIP / pixel
// scale, when the overlay needs drawing and uploading. No engine, no Windows. Tested on Linux. UNVERIFIED on a console.
#include "XboxDisplay.hpp"
#include "XboxSoftRaster.hpp"
#include <algorithm>

namespace okay::xbox {

/// The UI buffer for a back buffer of `back` pixels. The UI is drawn at the back buffer's size (1:1, so it matches the picture) but never larger
/// than `maxPixels` in total (a 4K back buffer would make a 33 MB buffer: past the cap the buffer is scaled down and stretched when composited).
inline SizePx UiBufferSize(SizePx back, long long maxPixels = 1920ll * 1080ll) {
    if (back.w < 1 || back.h < 1) return {1, 1};
    const long long px = (long long)back.w * back.h;
    if (px <= maxPixels) return back;
    const double s = std::sqrt((double)maxPixels / (double)px);
    return {std::max(1, (int)(back.w * s)), std::max(1, (int)(back.h * s))};
}

/// The size the engine's UI pass is asked to lay out for: the UI buffer's size in pixels (the engine's UI works in pixels of its output, like the
/// player's window). `uiScale` (>= 0.5) makes the UI larger or smaller (a TV across the room needs more): the layout size is buffer / scale.
inline SizePx UiLayoutSize(SizePx buffer, float uiScale = 1.0f) {
    const float s = uiScale < 0.5f ? 0.5f : uiScale > 4.0f ? 4.0f : uiScale;
    return {std::max(1, (int)((float)buffer.w / s)), std::max(1, (int)((float)buffer.h / s))};
}

/// Pointer position in DIPs (CoreWindow) to UI buffer pixels, so mouse clicks hit the widgets drawn there.
inline void DipsToUiPixel(float dipX, float dipY, SizePx windowDips, SizePx buffer, int& outX, int& outY) {
    if (windowDips.w < 1 || windowDips.h < 1) { outX = outY = 0; return; }
    outX = std::clamp((int)(dipX / (float)windowDips.w * (float)buffer.w), 0, buffer.w - 1);
    outY = std::clamp((int)(dipY / (float)windowDips.h * (float)buffer.h), 0, buffer.h - 1);
}

/// What to do this frame with the overlay, so a scene without UI costs nothing: draw + upload while there is UI; clear + upload once when it
/// disappears; do nothing otherwise. `dirty` = what the painter reports after drawing.
enum class OverlayAction { Nothing, DrawAndUpload, ClearOnce };
class OverlayState {
public:
    OverlayAction Next(bool sceneHasUi) {
        if (sceneHasUi) { shown_ = true; return OverlayAction::DrawAndUpload; }
        if (shown_) { shown_ = false; return OverlayAction::ClearOnce; }
        return OverlayAction::Nothing;
    }
    bool Shown() const { return shown_; }
private:
    bool shown_ = false;
};

/// The rows to copy into a mapped GPU texture whose row pitch may be larger than w * 4: copies the dirty rows only (a full upload when `dirty` is
/// the whole buffer). Returns the number of bytes copied.
inline std::size_t CopyDirtyRows(const std::uint8_t* src, int w, int h, const IRect& dirty, std::uint8_t* dst, std::size_t dstPitch) {
    const IRect d = Intersect(dirty, IRect{0, 0, w, h});
    if (d.Empty() || !src || !dst) return 0;
    std::size_t n = 0;
    for (int y = d.y; y < d.y + d.h; ++y) { std::memcpy(dst + (std::size_t)y * dstPitch, src + (std::size_t)y * (std::size_t)w * 4, (std::size_t)w * 4); n += (std::size_t)w * 4; }
    return n;
}

} // namespace okay::xbox
