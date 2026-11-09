#pragma once
#include "okay/Math/Vec2.hpp"

namespace okay {

/// 2D axis-aligned rectangle defined by a min corner (x, y) and a size, like
/// `Rect`.
struct Rect {
    float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;

    constexpr Rect() = default;
    constexpr Rect(float x, float y, float w, float h) : x(x), y(y), width(w), height(h) {}

    float XMin() const { return x; }
    float YMin() const { return y; }
    float XMax() const { return x + width; }
    float YMax() const { return y + height; }
    Vec2  Center() const { return {x + width * 0.5f, y + height * 0.5f}; }
    Vec2  Size() const { return {width, height}; }

    static Rect MinMax(const Vec2& min, const Vec2& max) {
        return {min.x, min.y, max.x - min.x, max.y - min.y};
    }
    static Rect Centered(const Vec2& center, const Vec2& size) {
        return {center.x - size.x * 0.5f, center.y - size.y * 0.5f, size.x, size.y};
    }

    bool Contains(const Vec2& p) const {
        return p.x >= XMin() && p.x <= XMax() && p.y >= YMin() && p.y <= YMax();
    }
    bool Overlaps(const Rect& o) const {
        // Strict: rects that only share an edge or a corner do not overlap. Negative
        // sizes are treated as the same rectangle with a positive size.
        const float aMinX = x < XMax() ? x : XMax(), aMaxX = x < XMax() ? XMax() : x;
        const float aMinY = y < YMax() ? y : YMax(), aMaxY = y < YMax() ? YMax() : y;
        const float bMinX = o.x < o.XMax() ? o.x : o.XMax(), bMaxX = o.x < o.XMax() ? o.XMax() : o.x;
        const float bMinY = o.y < o.YMax() ? o.y : o.YMax(), bMaxY = o.y < o.YMax() ? o.YMax() : o.y;
        return aMinX < bMaxX && aMaxX > bMinX && aMinY < bMaxY && aMaxY > bMinY;
    }
};

} // namespace okay
