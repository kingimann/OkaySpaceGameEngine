#pragma once
// Pictures drawn smaller than they are (an avatar, an icon, a card art shown at half size). A plain linear
// sample of a picture shrunk by more than about 1.5 times skips texels and shimmers; the UI pass instead asks the
// painter for a copy filtered down to the size it will be drawn at (Painter::LoadTextureSized), made with the
// engine's own mip filters (okay::tex, Mip.hpp). Written for this engine from the usual area-average and
// windowed-sinc definitions.
#include "okay/Graphics/Image.hpp"

namespace okay {
namespace ui2d {

/// The size to filter a `srcW` x `srcH` picture down to when it is drawn `devW` x `devH` device pixels big, or false
/// when it should be drawn as it is (not shrunk by 25% or more on either side, or grown). Sizes up to 96 pixels are
/// exact (an icon drawn at 32 is copied one to one); bigger ones round up to 10% steps, so a picture that is zoomed
/// smoothly makes a handful of copies, not one per frame, and is never sampled below its drawn size.
bool UIImageMipSize(int srcW, int srcH, int devW, int devH, int& outW, int& outH);

/// `src` filtered to `w` x `h` (both no bigger than the source): colour in linear light, weighted by alpha so
/// transparent pixels add no colour. `sharp` uses a windowed sinc (a little crisper), else an area average.
Image UIDownscaleImage(const Image& src, int w, int h, bool sharp);

} // namespace ui2d
} // namespace okay
