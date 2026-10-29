#pragma once
// Lines and trails drawn as camera-facing (or flat) ribbons: LineRenderer for
// lasers, ropes, aiming arcs and paths (points set in the Inspector, from
// scripts every frame, or taken from a Spline); TrailRenderer for the ribbon a
// moving object leaves behind (swords, projectiles, vehicles). Both build their
// triangles on the CPU each frame and are drawn alpha-blended over the 3D image,
// the same way particles are, so they look alike on every renderer.
#include "okay/Scene/Component.hpp"
#include "okay/Components/ParticleSystem.hpp"   // ParticleCurve / ParticleGradient
#include "okay/Math/Mat4.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Render/Color.hpp"
#include <deque>
#include <string>
#include <vector>

namespace okay {

class GameObject;
class Scene;

/// View: the ribbon turns to face the camera. Flat: it lies in the plane
/// perpendicular to the object's up axis (markings on the ground).
enum class LineAlignment : int { View = 0, Flat = 1 };
/// Stretch: the texture runs once along the whole length. Tile: it repeats every
/// `tileLength` units.
enum class LineTextureMode : int { Stretch = 0, Tile = 1 };

/// One point of a ribbon to build: where, how wide, which colour, texture u.
struct RibbonPoint {
    Vec3  position{0, 0, 0};
    float width = 0.1f;
    Color color = Color::White;
    float u = 0.0f;
};
struct RibbonVertex {
    Vec3  position{0, 0, 0};
    Color color = Color::White;
    Vec2  uv{0, 0};
};
/// World-space triangles of a ribbon (3 indices each).
struct Ribbon {
    std::vector<RibbonVertex> vertices;
    std::vector<int> indices;
    int TriangleCount() const { return (int)indices.size() / 3; }
};
struct RibbonOptions {
    LineAlignment alignment = LineAlignment::View;
    Vec3  eye{0, 0, 0};             ///< camera position (View)
    Vec3  flatNormal{0, 1, 0};      ///< the ribbon's facing (Flat)
    bool  loop = false;             ///< join the last point back to the first
    float loopEndU = 1.0f;          ///< u where the loop closes (the total, for tiling)
    int   cornerVertices = 0;       ///< > 0: round each bend with this many extra steps
    int   capVertices = 0;          ///< > 0: round both ends of an open line
};
/// Build the ribbon through `pts`. Counts, with n points (duplicates dropped):
/// open, square ends: 2n vertices, 2(n - 1) triangles; a loop: 2(n + 1) vertices
/// (the first pair again, carrying loopEndU), 2n triangles. Each rounded bend
/// (cornerVertices = k > 0) adds k + 1 vertices and k + 1 triangles; each rounded
/// end (capVertices = c > 0) adds c + 1 vertices and c + 1 triangles.
void BuildRibbon(const std::vector<RibbonPoint>& pts, const RibbonOptions& opt, Ribbon& out);

class LineRenderer : public Behaviour {
public:
    std::vector<Vec3> positions;      ///< the points, in world space or the object's (see useWorldSpace)
    bool  useWorldSpace = true;
    bool  loop = false;
    float width = 0.1f;
    ParticleCurve widthCurve;         ///< width multiplier along the line (0 = start, 1 = end); none = constant
    Color color = Color::White;
    ParticleGradient colorGradient;   ///< colour along the line; none = `color`
    int   cornerVertices = 0;
    int   capVertices = 0;
    LineAlignment alignment = LineAlignment::View;
    LineTextureMode textureMode = LineTextureMode::Stretch;
    float tileLength = 1.0f;          ///< Tile: units per repeat of the texture
    std::string texture;              ///< optional image (PNG/JPG), tinted by the colour
    std::string spline;               ///< draw this object's Spline instead of `positions`
    int   splineSamples = 64;         ///< points taken along the Spline

    int  PositionCount() const { return (int)positions.size(); }
    void SetPositionCount(int n) { positions.resize((std::size_t)(n < 0 ? 0 : n)); }
    void SetPosition(int index, const Vec3& p);
    Vec3 GetPosition(int index) const;
    void SetPositions(const std::vector<Vec3>& pts) { positions = pts; }
    /// The points this frame draws, in world space (the spline's, when one is set).
    std::vector<Vec3> WorldPoints() const;
    /// What Build turns into a ribbon: the points (world space) and the options, seen from `eye`.
    void RibbonInput(const Vec3& eye, std::vector<RibbonPoint>& pts, RibbonOptions& opt) const;
    /// The ribbon seen from `eye` (world space).
    void Build(const Vec3& eye, Ribbon& out) const;
};

class TrailRenderer : public Behaviour {
public:
    float time = 1.0f;                ///< seconds a point lives
    float minVertexDistance = 0.1f;   ///< a new point once the object moved this far
    float width = 0.5f;
    ParticleCurve widthCurve;         ///< width multiplier over a point's life (0 = new, 1 = about to vanish)
    Color color = Color::White;
    ParticleGradient colorGradient;   ///< colour over a point's life (fading alpha)
    bool  emitting = true;
    LineAlignment alignment = LineAlignment::View;
    LineTextureMode textureMode = LineTextureMode::Stretch;
    float tileLength = 1.0f;
    std::string texture;
    /// A jump longer than this in one frame (a teleport, a respawn) starts a new
    /// trail instead of drawing a streak across the scene. 0 = never.
    float teleportDistance = 5.0f;

    TrailRenderer() {
        colorGradient.SetTwo(Color{1, 1, 1, 1}, Color{1, 1, 1, 0});
        widthCurve.SetLinear(1.0f, 0.0f);
    }

    void Update(float dt) override;
    /// Drop every point (the trail starts again from where the object is).
    void Clear();
    int  PointCount() const { return (int)m_points.size(); }
    /// The stored points, oldest first.
    std::vector<Vec3> Points() const;
    /// What Build turns into a ribbon: the stored points plus the object's current
    /// position (world space) and the options, seen from `eye`.
    void RibbonInput(const Vec3& eye, std::vector<RibbonPoint>& pts, RibbonOptions& opt) const;
    /// The ribbon seen from `eye`: the stored points plus the object's current position.
    void Build(const Vec3& eye, Ribbon& out) const;

private:
    struct Point { Vec3 position; float born; };
    std::deque<Point> m_points;
    float m_now = 0.0f;
    bool  m_hasLast = false;
    Vec3  m_last{0, 0, 0};
};

/// A screen-space vertex of a line or trail (pixels).
struct LineDrawVertex { float x, y, u, v; Color color; };
/// One renderer's triangles for a view, three vertices each.
struct LineDraw {
    std::string texture;
    float depth = 0.0f;               ///< distance from the camera (sorting)
    std::vector<LineDrawVertex> tris;
};
/// True when the scene is nearer than a point at clip-w `clipW` at normalized
/// screen coordinates (nx, ny): the renderers pass ParticleOccluded.
using LineOcclusionFn = bool (*)(float nx, float ny, float clipW);
/// Every active LineRenderer and TrailRenderer of `scene` as seen through `vp`
/// from `eye`, in the view's pixel rect, near-clipped, back to front. `ignore`
/// hides that object's subtree (first person), `cullingMask` the camera's layers;
/// vertices `occluded` reports behind solid geometry fade out (like particles),
/// and with it long segments get a vertex every 12 pixels so they hide just where
/// something is in front.
void CollectLineDraws(const Scene& scene, const Mat4& vp, const Vec3& eye, float viewX, float viewY,
                      float viewW, float viewH, std::vector<LineDraw>& out, const GameObject* ignore = nullptr,
                      int cullingMask = ~0, LineOcclusionFn occluded = nullptr);
/// The `vp` for a 2D view drawn as screen = view centre + (world - camPos) * pixelsPerUnit
/// (y up): every depth lies on the screen, so nothing is near-clipped.
Mat4 LineView2D(const Vec3& camPos, float pixelsPerUnit, float viewW, float viewH);

} // namespace okay
