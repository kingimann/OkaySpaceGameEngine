#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
#include <string>

namespace okay {

/// A top-down map panel (HUD minimap). Draws a bordered box and plots every object
/// carrying a MinimapBlip as a small marker, centered on `target` (an object name)
/// or the world origin. `worldPerPixel` is the zoom (world units per map pixel;
/// smaller = more zoomed in). In 3D (`useXZ`) world X is horizontal and world Z
/// vertical; in 2D it uses X and Y.
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

    /// Map a world position to a pixel offset (dx,dy) from the map's top-left, given
    /// the map's center world position. Returns false if it falls outside the map
    /// rect. Shared by the renderer and unit tests so the projection isn't duplicated.
    static bool WorldToMap(const Minimap& m, const Vec3& center, const Vec3& world,
                           float mapW, float mapH, float& outX, float& outY) {
        float wpp = m.worldPerPixel > 1e-4f ? m.worldPerPixel : 1e-4f;
        float ax = world.x - center.x;
        float ay = m.useXZ ? (world.z - center.z) : (world.y - center.y);
        // +Z (or +Y) is "up" on the map => screen y grows downward, so invert.
        float px = mapW * 0.5f + ax / wpp;
        float py = mapH * 0.5f - ay / wpp;
        outX = px; outY = py;
        return px >= 0 && px <= mapW && py >= 0 && py <= mapH;
    }
};

/// A tiny marker placed on objects that should appear on every Minimap. Drawn as a
/// small square (or dot) of `color` at the object's projected map position.
class MinimapBlip : public Behaviour {
public:
    Color color = Color::FromBytes(230, 90, 90);
    float size = 4.0f;                               // px half-extent
    bool  square = true;                             // else dot
};

} // namespace okay
