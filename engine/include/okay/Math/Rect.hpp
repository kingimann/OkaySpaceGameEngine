#pragma once
#include "okay/Math/Vec2.hpp"

namespace okay {

/// 2D axis-aligned rectangle defined by a min corner (x, y) and a size, like
/// Unity's `Rect`.
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
        return XMin() <= o.XMax() && XMax() >= o.XMin() &&
               YMin() <= o.YMax() && YMax() >= o.YMin();
    }
};

} // namespace okay
