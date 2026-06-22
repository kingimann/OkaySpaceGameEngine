#pragma once
#include "okay/Math/Mathf.hpp"
#include <algorithm>
#include <cmath>

namespace okay {

/// The silhouette a UI widget fills. Rectangle is the classic hard box; Rounded
/// gives the soft corners of modern flat UI kits; Circle and Pill (capsule) cover
/// avatars, badges, icon buttons and toggle tracks. The renderer turns these into
/// per-row spans so the same code fills any of them (and clips text/children to it).
enum class UIShape { Rectangle, Rounded, Circle, Pill };

/// For a widget occupying the box [0,w] x [0,h] (local pixels), compute the inside
/// horizontal span [x0,x1) at integer row `row`. Returns false when the row is
/// entirely outside the shape. `radius` is the corner radius for Rounded (pixels);
/// it's ignored by the other shapes. This is the single source of truth both the
/// fill and the hit-test use, so the visible shape and the clickable area agree.
inline bool UIShapeRowSpan(UIShape shape, float w, float h, float radius, int row,
                           float& x0, float& x1) {
    if (w <= 0.0f || h <= 0.0f || row < 0 || (float)row >= h) return false;
    const float cy = (float)row + 0.5f;

    if (shape == UIShape::Rectangle) { x0 = 0.0f; x1 = w; return true; }

    if (shape == UIShape::Circle) {
        const float rx = w * 0.5f, ry = h * 0.5f;
        const float ny = (cy - ry) / ry;                 // -1..1 down the height
        if (ny <= -1.0f || ny >= 1.0f) return false;
        const float dx = rx * std::sqrt(std::max(0.0f, 1.0f - ny * ny));
        x0 = rx - dx; x1 = rx + dx;
        return x1 > x0;
    }

    // Rounded / Pill: straight sides with circular caps top and bottom. A Pill is
    // just a Rounded box whose radius is half the short side (fully round ends).
    const float r = (shape == UIShape::Pill)
                        ? std::min(w, h) * 0.5f
                        : Mathf::Clamp(radius, 0.0f, std::min(w, h) * 0.5f);
    float inset = 0.0f;
    if (cy < r) {                                        // inside the top caps
        const float dy = r - cy;
        inset = r - std::sqrt(std::max(0.0f, r * r - dy * dy));
    } else if (cy > h - r) {                             // inside the bottom caps
        const float dy = cy - (h - r);
        inset = r - std::sqrt(std::max(0.0f, r * r - dy * dy));
    }
    x0 = inset; x1 = w - inset;
    return x1 > x0;
}

/// Whether the local point (px,py) lies inside the shape — for click hit-testing
/// that respects rounded/circular widgets (corners and outside-the-circle misses).
inline bool UIShapeContains(UIShape shape, float w, float h, float radius,
                            float px, float py) {
    if (px < 0.0f || py < 0.0f || px >= w || py >= h) return false;
    float x0, x1;
    if (!UIShapeRowSpan(shape, w, h, radius, (int)py, x0, x1)) return false;
    return px >= x0 && px < x1;
}

} // namespace okay
