#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
#include <string>
#include <cmath>
#include <algorithm>

namespace okay {

/// A top-down map panel (HUD minimap). Draws a bordered box (or circle) and plots
/// every object carrying a MinimapBlip as a small marker, centered on `target`
/// (an object name) or the world origin. `worldPerPixel` is the zoom (world units
/// per map pixel; smaller = more zoomed in). In 3D (`useXZ`) world X is horizontal
/// and world Z vertical; in 2D it uses X and Y.
///
/// Expanded (Unity-style) options: a circular mask, heading-up rotation that turns
/// the whole map so the target always faces "up", a directional player arrow at the
/// centre, off-map blips clamped to the edge as direction pointers, and an optional
/// reference grid.
class Minimap : public Behaviour {
public:
    Vec2 position{20.0f, 20.0f};
    Vec2 size{180.0f, 180.0f};
    UIAnchor anchor = UIAnchor::TopRight;

    Color background = Color::FromBytes(20, 24, 30, 210);
    Color border = Color::FromBytes(180, 190, 210);
    float borderWidth = 2.0f;

    std::string target;                              // object to center on; empty = world origin
    Color targetColor = Color::FromBytes(90, 200, 255);

    float worldPerPixel = 0.5f;                      // zoom: world units per map pixel
    bool  useXZ = true;                              // 3D: plot X/Z; false: 2D X/Y
    float blipSize = 4.0f;                           // default blip square half-extent (px)

    // ---- Expanded options ----
    bool  circular = false;                          // round map (mask + circular border)
    bool  rotateWithTarget = false;                  // spin the map so the target faces up
    bool  playerArrow = true;                        // draw the centre marker as a facing arrow
    bool  clampBlips = false;                         // pin off-map blips to the edge (objective markers)
    bool  showGrid = false;                          // faint reference grid
    Color gridColor = Color::FromBytes(80, 90, 105, 120);
    float gridSpacing = 24.0f;                        // grid line spacing (map pixels)

    /// Heading (radians) of a forward vector in the map plane, measured from "north"
    /// (the up axis of the map) toward "east" (its right axis). Used for heading-up
    /// rotation and for orienting directional blips.
    static float HeadingOf(const Vec3& forward, bool useXZ) {
        float east  = forward.x;
        float north = useXZ ? forward.z : forward.y;
        return std::atan2(east, north);
    }

    /// Map a world position to a pixel offset (dx,dy) from the map's top-left, given
    /// the map's center world position. Returns false if it falls outside the map
    /// rect. Shared by the renderer and unit tests so the projection isn't duplicated.
    static bool WorldToMap(const Minimap& m, const Vec3& center, const Vec3& world,
                           float mapW, float mapH, float& outX, float& outY) {
        return WorldToMapR(m, center, world, mapW, mapH, 0.0f, outX, outY);
    }

    /// Like WorldToMap but rotates the offset by `headingRad` (for heading-up maps)
    /// and honours the circular mask when testing "inside". Returns false when the
    /// point falls outside the visible map area (rect or circle).
    static bool WorldToMapR(const Minimap& m, const Vec3& center, const Vec3& world,
                            float mapW, float mapH, float headingRad,
                            float& outX, float& outY) {
        float wpp = m.worldPerPixel > 1e-4f ? m.worldPerPixel : 1e-4f;
        float ax = world.x - center.x;
        float ay = m.useXZ ? (world.z - center.z) : (world.y - center.y);
        if (headingRad != 0.0f) {
            float c = std::cos(headingRad), s = std::sin(headingRad);
            float rx = ax * c - ay * s;
            float ry = ax * s + ay * c;
            ax = rx; ay = ry;
        }
        // +Z (or +Y) is "up" on the map => screen y grows downward, so invert.
        float px = mapW * 0.5f + ax / wpp;
        float py = mapH * 0.5f - ay / wpp;
        outX = px; outY = py;
        if (m.circular) {
            float r = std::min(mapW, mapH) * 0.5f;
            float dx = px - mapW * 0.5f, dy = py - mapH * 0.5f;
            return (dx * dx + dy * dy) <= r * r;
        }
        return px >= 0 && px <= mapW && py >= 0 && py <= mapH;
    }
};

/// A tiny marker placed on objects that should appear on every Minimap. Drawn as a
/// small square, dot, triangle, or facing arrow of `color` at the object's projected
/// map position. Set `rotateWithObject` so a triangle/arrow points along the
/// object's heading (great for the player and vehicles).
class MinimapBlip : public Behaviour {
public:
    enum class Shape { Square, Dot, Triangle, Arrow };

    Color color = Color::FromBytes(230, 90, 90);
    float size = 4.0f;                               // px half-extent
    bool  square = true;                             // legacy: true=square, false=dot
    Shape shape = Shape::Square;                      // preferred shape selector
    bool  rotateWithObject = false;                   // point triangle/arrow along heading
};

} // namespace okay
