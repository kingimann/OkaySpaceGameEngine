#pragma once
#include "okay/Math/Mathf.hpp"
#include <algorithm>
#include <cmath>

namespace okay {

/// The silhouette a UI widget fills. Rectangle is the classic hard box; Rounded
/// gives the soft corners of modern flat UI kits; Circle and Pill (capsule) cover
/// avatars, badges, icon buttons and toggle tracks. The remaining shapes are
/// convex polygons for badges, gems, banners and arrows:
///   Triangle      — points up (apex top, full base bottom)
///   Diamond       — rhombus (point top & bottom)
///   Hexagon       — flat top/bottom with sloped sides
///   Octagon       — rectangle with 45° chamfered corners (cut size = corner radius)
///   Parallelogram — slanted box (skew = corner radius)
///   Trapezoid     — narrower at the top (top inset = corner radius)
///   Pentagon      — house/badge: triangular cap over a square base
///   Squircle      — rounded superellipse (modern app-icon look)
///   TabTop        — only the top corners rounded (cards, tab headers; radius)
///   ArrowRight/Left/Up/Down — block arrows for nav, carousels, callouts
/// The renderer turns every shape into per-row spans so the same fill/clip/hit-test
/// code works for all of them. New values are appended so saved scenes stay valid.
enum class UIShape { Rectangle, Rounded, Circle, Pill,
                     Triangle, Diamond, Hexagon, Octagon, Parallelogram, Trapezoid,
                     Pentagon, Squircle, TabTop,
                     ArrowRight, ArrowLeft, ArrowUp, ArrowDown };

/// Number of UIShape values + their display names (index by (int)UIShape) — shared
/// by the editor's shape dropdowns so they all list the same set.
inline constexpr int kUIShapeCount = 17;
inline const char* UIShapeName(int i) {
    static const char* names[kUIShapeCount] = {
        "Rectangle", "Rounded", "Circle", "Pill",
        "Triangle", "Diamond", "Hexagon", "Octagon", "Parallelogram", "Trapezoid",
        "Pentagon", "Squircle", "Tab (top round)",
        "Arrow Right", "Arrow Left", "Arrow Up", "Arrow Down" };
    return (i >= 0 && i < kUIShapeCount) ? names[i] : "?";
}

/// Whether the shape reads the corner-radius value (Rounded corners, the Octagon
/// chamfer, the Parallelogram skew, the Trapezoid top inset and the TabTop caps all
/// scale with it), so editors know when to show the radius control.
inline bool UIShapeUsesRadius(UIShape s) {
    return s == UIShape::Rounded || s == UIShape::Octagon ||
           s == UIShape::Parallelogram || s == UIShape::Trapezoid ||
           s == UIShape::TabTop;
}

/// Corner bits for `cornerMask` (which corners a Rounded/Pill shape actually
/// rounds). All four set (0xF) = every corner rounded, the default.
enum UICorner { UICornerTL = 1, UICornerTR = 2, UICornerBR = 4, UICornerBL = 8,
                UICornerAll = 15 };

/// For a widget occupying the box [0,w] x [0,h] (local pixels), compute the inside
/// horizontal span [x0,x1) at integer row `row`. Returns false when the row is
/// entirely outside the shape. `radius` is the corner radius for Rounded (pixels);
/// it's ignored by the other shapes. `cornerMask` selects which corners a
/// Rounded/Pill shape rounds (default all); an un-masked corner stays square. This
/// is the single source of truth both the fill and the hit-test use, so the visible
/// shape and the clickable area agree.
inline bool UIShapeRowSpan(UIShape shape, float w, float h, float radius, int row,
                           float& x0, float& x1, int cornerMask = UICornerAll) {
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

    if (shape == UIShape::Triangle) {                    // apex top, full base bottom
        const float frac = cy / h;                       // 0 at top .. 1 at bottom
        const float half = (w * 0.5f) * frac;
        x0 = w * 0.5f - half; x1 = w * 0.5f + half;
        return x1 > x0;
    }

    if (shape == UIShape::Diamond) {                     // widest at the middle
        const float frac = 1.0f - std::fabs(2.0f * cy / h - 1.0f);
        const float half = (w * 0.5f) * frac;
        x0 = w * 0.5f - half; x1 = w * 0.5f + half;
        return x1 > x0;
    }

    if (shape == UIShape::Hexagon) {                     // flat top/bottom, sloped sides
        const float cap = h * 0.25f;                     // height of the sloped caps
        const float edgeHalf = w * 0.25f, fullHalf = w * 0.5f;
        float half = fullHalf;
        if (cy < cap)            half = edgeHalf + (fullHalf - edgeHalf) * (cy / cap);
        else if (cy > h - cap)   half = edgeHalf + (fullHalf - edgeHalf) * ((h - cy) / cap);
        x0 = w * 0.5f - half; x1 = w * 0.5f + half;
        return x1 > x0;
    }

    if (shape == UIShape::Octagon) {                     // 45° chamfered corners
        const float c = Mathf::Clamp(radius > 0.0f ? radius : std::min(w, h) * 0.3f,
                                     0.0f, std::min(w, h) * 0.5f);
        float inset = 0.0f;
        if (cy < c)              inset = c - cy;
        else if (cy > h - c)     inset = c - (h - cy);
        x0 = inset; x1 = w - inset;
        return x1 > x0;
    }

    if (shape == UIShape::Parallelogram) {               // skewed box, constant width
        const float s = Mathf::Clamp(radius > 0.0f ? radius : w * 0.25f, 0.0f, w * 0.9f);
        const float frac = cy / h;                       // top row shifted right by s
        x0 = s * (1.0f - frac);
        x1 = w - s * frac;
        return x1 > x0;
    }

    if (shape == UIShape::Trapezoid) {                   // narrower at the top
        const float inset = Mathf::Clamp(radius > 0.0f ? radius : w * 0.2f, 0.0f, w * 0.45f);
        const float topInset = inset * (1.0f - cy / h);  // full inset at top -> 0 at base
        x0 = topInset; x1 = w - topInset;
        return x1 > x0;
    }

    if (shape == UIShape::Pentagon) {                    // triangular cap over a square base
        const float cap = h * 0.42f;                     // height of the roof
        if (cy < cap) {                                  // roof: widen apex -> full width
            const float half = (w * 0.5f) * (cy / cap);
            x0 = w * 0.5f - half; x1 = w * 0.5f + half;
        } else { x0 = 0.0f; x1 = w; }                    // square body
        return x1 > x0;
    }

    if (shape == UIShape::Squircle) {                    // |x|^n + |y|^n = 1 superellipse
        const float n = 4.0f, rx = w * 0.5f, ry = h * 0.5f;
        const float ny = (cy - ry) / ry;                 // -1..1
        const float t = 1.0f - std::pow(std::fabs(ny), n);
        if (t <= 0.0f) return false;
        const float dx = rx * std::pow(t, 1.0f / n);
        x0 = rx - dx; x1 = rx + dx;
        return x1 > x0;
    }

    if (shape == UIShape::TabTop) {                      // only the top corners rounded
        const float r = Mathf::Clamp(radius, 0.0f, std::min(w, h) * 0.5f);
        float inset = 0.0f;
        if (cy < r) { const float dy = r - cy; inset = r - std::sqrt(std::max(0.0f, r * r - dy * dy)); }
        x0 = inset; x1 = w - inset;
        return x1 > x0;
    }

    // Block arrows: a triangular head plus a centred rectangular shaft. The span
    // stays a single interval per row (so fill + hit-test work), even though the
    // overall silhouette is concave.
    if (shape == UIShape::ArrowRight || shape == UIShape::ArrowLeft) {
        const float headBase = w * 0.5f;                 // where the head meets the shaft
        const float band = h * 0.27f;                    // shaft half-height
        const float t = 1.0f - std::fabs(cy - h * 0.5f) / (h * 0.5f); // 0 at edges, 1 mid
        const bool inShaft = cy >= h * 0.5f - band && cy <= h * 0.5f + band;
        if (shape == UIShape::ArrowRight) {
            x0 = inShaft ? 0.0f : headBase;
            x1 = headBase + (w - headBase) * t;          // head tip reaches w at mid-row
        } else {
            x0 = headBase * (1.0f - t);                  // head tip reaches 0 at mid-row
            x1 = inShaft ? w : headBase;
        }
        return x1 > x0;
    }
    if (shape == UIShape::ArrowUp || shape == UIShape::ArrowDown) {
        const float headH = h * 0.5f, shaftHalf = w * 0.25f;
        float headFrac;                                  // 0 at the head's base .. 1 at its tip
        bool inHead;
        if (shape == UIShape::ArrowUp) { inHead = cy < headH; headFrac = inHead ? cy / headH : 0.0f; }
        else                           { inHead = cy > h - headH; headFrac = inHead ? (h - cy) / headH : 0.0f; }
        if (inHead) { const float half = (w * 0.5f) * headFrac; x0 = w * 0.5f - half; x1 = w * 0.5f + half; }
        else        { x0 = w * 0.5f - shaftHalf; x1 = w * 0.5f + shaftHalf; }
        return x1 > x0;
    }

    // Rounded / Pill: straight sides with circular caps top and bottom. A Pill is
    // just a Rounded box whose radius is half the short side (fully round ends).
    // `cornerMask` lets each corner be rounded or left square independently.
    const float r = (shape == UIShape::Pill)
                        ? std::min(w, h) * 0.5f
                        : Mathf::Clamp(radius, 0.0f, std::min(w, h) * 0.5f);
    float leftInset = 0.0f, rightInset = 0.0f;
    if (cy < r) {                                        // inside the top caps
        const float dy = r - cy;
        const float ins = r - std::sqrt(std::max(0.0f, r * r - dy * dy));
        if (cornerMask & UICornerTL) leftInset  = ins;
        if (cornerMask & UICornerTR) rightInset = ins;
    } else if (cy > h - r) {                             // inside the bottom caps
        const float dy = cy - (h - r);
        const float ins = r - std::sqrt(std::max(0.0f, r * r - dy * dy));
        if (cornerMask & UICornerBL) leftInset  = ins;
        if (cornerMask & UICornerBR) rightInset = ins;
    }
    x0 = leftInset; x1 = w - rightInset;
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
