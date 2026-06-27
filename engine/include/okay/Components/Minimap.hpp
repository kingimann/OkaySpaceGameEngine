#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Input/Input.hpp"
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
    bool  showSelf = true;                            // draw the centre (player) marker at all

    // ---- View cone (FOV wedge from the centre, GTA radar style) ----
    bool  viewCone = false;
    float viewConeAngle  = 60.0f;                     // full cone angle, degrees
    float viewConeLength = 40.0f;                     // cone reach in map pixels
    Color viewConeColor  = Color::FromBytes(120, 200, 255, 70);

    // ---- Range rings (concentric distance circles) ----
    int   rangeRings = 0;                             // number of rings (0 = off)
    Color ringColor  = Color::FromBytes(150, 160, 180, 90);

    // ---- North indicator (a compass 'N' tick on the map edge) ----
    bool  showNorth = false;
    Color northColor = Color::FromBytes(235, 90, 90, 255);

    // ---- GTA-style custom world map ----
    std::string mapTexture;                          // image drawn under the blips ("" = none)
    float mapWorldSize = 100.0f;                      // world units the texture spans (square)
    Vec2  mapWorldCenter{0.0f, 0.0f};                 // world (east, north) at the texture's centre
    // A custom map reads best north-up (like GTA's pause map): the image stays put and
    // the player ARROW rotates to show facing, instead of spinning the whole map.
    bool  fullscreen = false;                         // big pause-map overlay (runtime toggle)
    char  fullscreenKey = 'm';                        // press to toggle the fullscreen map (0 = off)
    float fullscreenZoom = 3.0f;                       // worldPerPixel multiplier when fullscreen
    float fullscreenFrac = 0.82f;                      // fraction of the screen the big map fills

    // ---- Waypoints / markers (GTA POIs) ----
    /// A fixed world-space point of interest drawn on the map as a coloured diamond.
    /// `world` is (east, north) — matches the map plane (X/Z in 3D, X/Y in 2D).
    struct Marker { Vec2 world{0.0f, 0.0f}; Color color = Color::FromBytes(255, 230, 90, 255); float size = 6.0f; };
    std::vector<Marker> markers;

    // ---- Runtime zoom (GTA scroll-zoom) ----
    char  zoomInKey = 0, zoomOutKey = 0;               // nudge worldPerPixel (0 = off)
    float minZoom = 0.05f, maxZoom = 16.0f;            // worldPerPixel clamp
    float zoomStep = 1.15f;                            // multiply per key press

    /// True when a custom world-map image is set — the map then renders north-up.
    bool HasMap() const { return !mapTexture.empty(); }

    /// Build a world Vec3 from a map-plane (east, north) point for this map's mode.
    Vec3 PlaneToWorld(const Vec2& en) const {
        return useXZ ? Vec3{en.x, 0.0f, en.y} : Vec3{en.x, en.y, 0.0f};
    }

    void Update(float) override {
        if (fullscreenKey && Input::GetKeyDown(fullscreenKey)) fullscreen = !fullscreen;
        if (zoomInKey  && Input::GetKeyDown(zoomInKey))
            worldPerPixel = std::max(minZoom, worldPerPixel / (zoomStep > 1.001f ? zoomStep : 1.15f));
        if (zoomOutKey && Input::GetKeyDown(zoomOutKey))
            worldPerPixel = std::min(maxZoom, worldPerPixel * (zoomStep > 1.001f ? zoomStep : 1.15f));
    }

    /// Texture UV rectangle (0..1) covering the visible world window, given the centre
    /// world position, panel size (px) and effective worldPerPixel. North is up, so v
    /// grows downward (image space). Shared by the player + editor GTA-map draw.
    static void MapSrcUV(const Minimap& m, const Vec3& center, float mapW, float mapH, float wpp,
                         float& u0, float& v0, float& u1, float& v1) {
        float S = m.mapWorldSize > 1e-3f ? m.mapWorldSize : 1e-3f;
        float ceE = center.x;
        float ceN = m.useXZ ? center.z : center.y;
        float halfX = mapW * 0.5f * wpp, halfY = mapH * 0.5f * wpp;
        float originW = m.mapWorldCenter.x - S * 0.5f;   // texture's west edge (world east)
        float originN = m.mapWorldCenter.y + S * 0.5f;   // texture's north edge (world north)
        u0 = ((ceE - halfX) - originW) / S; u1 = ((ceE + halfX) - originW) / S;
        v0 = (originN - (ceN + halfY)) / S; v1 = (originN - (ceN - halfY)) / S;
    }

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
                            float& outX, float& outY, float wppOverride = 0.0f) {
        float wpp = (wppOverride > 1e-4f ? wppOverride
                                         : (m.worldPerPixel > 1e-4f ? m.worldPerPixel : 1e-4f));
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
    std::string icon;                                 // optional icon texture (overrides the shape)
};

} // namespace okay
