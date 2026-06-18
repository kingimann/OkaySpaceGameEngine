#pragma once
#include "okay/Math/Mathf.hpp"

namespace okay {

/// RGBA color with components in the [0, 1] range, like Unity's `Color`.
struct Color {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

    constexpr Color() = default;
    constexpr Color(float r, float g, float b, float a = 1.0f) : r(r), g(g), b(b), a(a) {}

    static Color FromBytes(int r, int g, int b, int a = 255) {
        return {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
    }

    static Color Lerp(const Color& x, const Color& y, float t) {
        t = Mathf::Clamp01(t);
        return {x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t,
                x.b + (y.b - x.b) * t, x.a + (y.a - x.a) * t};
    }

    /// Perceived luminance (Rec. 601), handy for ASCII rendering.
    float Luminance() const { return 0.299f * r + 0.587f * g + 0.114f * b; }

    static const Color Black;
    static const Color White;
    static const Color Red;
    static const Color Green;
    static const Color Blue;
    static const Color Yellow;
    static const Color Cyan;
    static const Color Magenta;
    static const Color Clear;
};

inline const Color Color::Black   {0, 0, 0, 1};
inline const Color Color::White   {1, 1, 1, 1};
inline const Color Color::Red     {1, 0, 0, 1};
inline const Color Color::Green   {0, 1, 0, 1};
inline const Color Color::Blue    {0, 0, 1, 1};
inline const Color Color::Yellow  {1, 1, 0, 1};
inline const Color Color::Cyan    {0, 1, 1, 1};
inline const Color Color::Magenta {1, 0, 1, 1};
inline const Color Color::Clear   {0, 0, 0, 0};

} // namespace okay
