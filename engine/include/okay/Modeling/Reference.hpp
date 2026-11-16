#pragma once
// Where a reference image (blueprint plane) sits in the world: a flat rectangle in the front (XY), side (ZY) or top (XZ) plane.
#include "okay/Math/Vec3.hpp"
#include <array>
#include <cmath>

namespace okay::modeling {

enum class RefPlane { Front = 0, Side = 1, Top = 2 };

/// The four corners (top-left, top-right, bottom-right, bottom-left as the picture is seen from the viewing side) of a picture
/// `width` world units wide, as tall as its `aspect` (height / width) says, centred on `centre`. Front is seen from +Z, Side from +X
/// (its left edge is toward +Z), Top from above (its top edge is toward -Z). `flipX` mirrors it left to right.
/// A width that is not a positive number, or an aspect that is not, gives all four corners at `centre`.
inline std::array<Vec3, 4> RefPlaneCorners(RefPlane plane, const Vec3& centre, float width, float aspect, bool flipX = false) {
    std::array<Vec3, 4> c = {centre, centre, centre, centre};
    if (!(width > 0.0f) || !(aspect > 0.0f) || !std::isfinite(width + aspect + centre.x + centre.y + centre.z)) return c;
    const float hw = width * 0.5f, hh = width * aspect * 0.5f;
    const float l = flipX ? hw : -hw, r = flipX ? -hw : hw;
    for (int k = 0; k < 4; ++k) {
        const float x = (k == 0 || k == 3) ? l : r, y = (k < 2) ? hh : -hh;
        if (plane == RefPlane::Front) c[k] = centre + Vec3{x, y, 0};
        else if (plane == RefPlane::Side) c[k] = centre + Vec3{0, y, -x};
        else c[k] = centre + Vec3{x, 0, -y};
    }
    return c;
}

} // namespace okay::modeling
