#include "okay/UI/UIImageScale.hpp"
#include "okay/Texture/Mip.hpp"
#include <algorithm>
#include <cmath>

namespace okay {
namespace ui2d {

namespace {
int QuantSize(int dev, int src) {
    if (dev <= 96) return std::max(1, dev);
    // Round up to the next step of 1.1x counted from 96, so the copy is never smaller than what is drawn.
    const double steps = std::ceil(std::log((double)dev / 96.0) / std::log(1.1) - 1e-9);
    const int q = (int)std::ceil(96.0 * std::pow(1.1, steps));
    return std::min(src, std::max(dev, q));
}
} // namespace

bool UIImageMipSize(int srcW, int srcH, int devW, int devH, int& outW, int& outH) {
    outW = srcW; outH = srcH;
    if (srcW <= 0 || srcH <= 0 || devW <= 0 || devH <= 0) return false;
    const bool shrinkX = (double)devW <= 0.75 * srcW, shrinkY = (double)devH <= 0.75 * srcH;
    if (!shrinkX && !shrinkY) return false;
    outW = shrinkX ? QuantSize(devW, srcW) : srcW;
    outH = shrinkY ? QuantSize(devH, srcH) : srcH;
    return outW < srcW || outH < srcH;
}

Image UIDownscaleImage(const Image& src, int w, int h, bool sharp) {
    if (src.Width() <= 0 || src.Height() <= 0 || w <= 0 || h <= 0) return Image();
    if (w >= src.Width() && h >= src.Height()) return src;
    tex::MipOptions opt;
    opt.filter = sharp ? tex::MipFilter::Lanczos3 : tex::MipFilter::Box;
    opt.srgb = true;
    const tex::Surface in = tex::FromRGBA8(src.Data(), src.Width(), src.Height(), true);
    const tex::Surface out = tex::Resample(in, std::min(w, src.Width()), std::min(h, src.Height()), opt);
    if (!out.Valid()) return src;
    Image img;
    img.Set(out.w, out.h, tex::ToRGBA8(out, true));
    return img;
}

} // namespace ui2d
} // namespace okay
