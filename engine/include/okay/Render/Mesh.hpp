#pragma once
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Render/Color.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace okay {

/// Proportion knobs for the procedural humanoid (Mesh::Humanoid). Defaults of 1
/// reproduce the baseline figure; the editor's Character component exposes these
/// as sliders. Multipliers are applied to a fixed base layout.
struct HumanoidParams {
    float height       = 1.0f;   // overall vertical scale
    float build        = 1.0f;   // limb/torso thickness
    float headSize     = 1.0f;
    float shoulderWidth= 1.0f;   // arm spacing + torso top width
    float hipWidth     = 1.0f;   // leg spacing + hip width
    float armLength    = 1.0f;
    float legLength    = 1.0f;
    float neckLength   = 1.0f;
    float handSize     = 1.0f;
    float footSize     = 1.0f;
    float armSpread    = 10.0f;  // degrees arms angle out from the body (A/T-pose)
    float legSpread    = 3.0f;   // degrees legs angle out (stance width)
    float torsoLength  = 1.0f;   // lengthens the torso (raises the upper body)
    float bodyDepth    = 1.0f;   // front-to-back thickness of torso + hips
    int   hairStyle    = 1;      // 0 cap, 1 short, 2 long, 3 spiky, 4 ponytail, 5 mohawk
    float eyeSpacing   = 1.0f;   // multiplier on the gap between the eyes
    float mouthWidth   = 1.0f;   // multiplier on mouth width (a wider "smile")
    float browAngle    = 0.0f;   // degrees; +tilts brows inward/down (angry), -up (worried)
    bool  ears         = true;   // add ears to the sides of the head
    float armSwing     = 0.0f;   // degrees: fore/aft arm swing (animation; antisymmetric)
    float legSwing     = 0.0f;   // degrees: fore/aft leg swing (animation; antisymmetric)
};

/// Per-region colors for the procedural humanoid (Mesh::Humanoid). When passed,
/// parts are colored individually and optional hair + face details are added.
struct HumanoidColors {
    Color skin  = Color::FromBytes(214, 178, 150); // head, neck, hands, nose
    Color shirt = Color::FromBytes(70, 110, 170);  // torso, arms
    Color pants = Color::FromBytes(50, 55, 70);    // hips, legs
    Color shoes = Color::FromBytes(35, 35, 40);    // feet
    Color hair  = Color::FromBytes(60, 40, 30);    // hair cap, brows, mouth
    Color eye   = Color::FromBytes(40, 40, 50);    // eyes
    Color hat   = Color::FromBytes(150, 40, 40);   // hat
    Color glasses = Color::FromBytes(20, 20, 25);  // glasses frame
    bool  hasHair = true;
    bool  hasFace = true;
    bool  hasHat = false;
    bool  hasGlasses = false;
};

/// A simple indexed triangle mesh (positions + triangle indices). Enough to
/// describe 3D geometry; the editor renders these as wireframes and a future
/// GPU backend can upload them directly. `name` tags built-in primitives so they
/// can be serialized compactly.
struct Mesh {
    std::string       name;       // "Cube"/"Pyramid"/"Quad" for primitives
    std::vector<Vec3> vertices;
    std::vector<int>  triangles;  // 3 indices per triangle
    std::vector<Vec2> uvs;        // optional per-vertex UVs (parallel to vertices)
    std::vector<Color> triColors; // optional per-triangle colors (parallel to faces);
                                  // used by the renderer only when fully populated.

    int TriangleCount() const { return static_cast<int>(triangles.size() / 3); }
    bool HasFaceColors() const { return (int)triColors.size() == TriangleCount() && !triColors.empty(); }

    // ---- Primitive generators -----------------------------------------
    static Mesh Quad(float size = 1.0f) {
        float h = size * 0.5f;
        Mesh m;
        m.name = "Quad";
        m.vertices = {{-h, -h, 0}, {h, -h, 0}, {h, h, 0}, {-h, h, 0}};
        m.triangles = {0, 1, 2, 0, 2, 3};
        return m;
    }

    static Mesh Cube(float size = 1.0f) {
        float h = size * 0.5f;
        Mesh m;
        m.name = "Cube";
        m.vertices = {
            {-h, -h, -h}, { h, -h, -h}, { h, h, -h}, {-h, h, -h}, // back
            {-h, -h,  h}, { h, -h,  h}, { h, h,  h}, {-h, h,  h}, // front
        };
        // Counter-clockwise winding (when viewed from outside) so each face's
        // normal = cross(e1, e2) points OUTWARD — required for backface culling
        // and lighting to treat the camera-facing side as the front.
        m.triangles = {
            0,2,1, 0,3,2,   // back   (-z)
            4,5,6, 4,6,7,   // front  (+z)
            4,1,5, 4,0,1,   // bottom (-y)
            3,6,2, 3,7,6,   // top    (+y)
            4,3,0, 4,7,3,   // left   (-x)
            1,6,5, 1,2,6,   // right  (+x)
        };
        return m;
    }

    /// A wedge / ramp: a right-triangular prism (a cube sliced corner-to-corner)
    /// rising along +Z toward +Y. Great for ramps, rooftops, and level blockouts.
    static Mesh Wedge(float size = 1.0f) {
        float h = size * 0.5f;
        Mesh m;
        m.name = "Wedge";
        m.vertices = {
            {-h, -h, -h}, { h, -h, -h}, { h, -h, h}, {-h, -h, h}, // base (y = -h)
            {-h,  h,  h}, { h,  h, h},                            // top edge (y = +h, z = +h)
        };
        m.triangles = {                  // CCW / outward-facing winding
            0,1,2, 0,2,3,        // base (y = -h)
            3,5,4, 3,2,5,        // vertical front face (z = +h)
            0,5,1, 0,4,5,        // sloped face (back-bottom up to front-top)
            1,5,2,               // right side triangle (x = +h)
            0,3,4,               // left side triangle (x = -h)
        };
        return m;
    }

    static Mesh Pyramid(float size = 1.0f) {
        float h = size * 0.5f;
        Mesh m;
        m.name = "Pyramid";
        m.vertices = {
            {-h, -h, -h}, { h, -h, -h}, { h, -h, h}, {-h, -h, h}, // base
            { 0,  h,  0},                                         // apex
        };
        m.triangles = {                  // CCW / outward-facing winding
            0,1,2, 0,2,3,        // base (-y)
            0,4,1, 1,4,2,        // sides
            2,4,3, 3,4,0,
        };
        return m;
    }

    /// A subdivided horizontal grid in the XZ plane: `cols`×`rows` cells over
    /// `width`×`depth`, centered at the origin. The dense vertex grid is the
    /// base for terrain/water (displace the Y of each vertex afterwards).
    static Mesh Grid(float width = 10.0f, float depth = 10.0f, int cols = 10, int rows = 10) {
        Mesh m;
        m.name = "Grid";
        if (cols < 1) cols = 1;
        if (rows < 1) rows = 1;
        for (int z = 0; z <= rows; ++z)
            for (int x = 0; x <= cols; ++x)
                m.vertices.push_back({-width * 0.5f + width * (float)x / cols, 0.0f,
                                      -depth * 0.5f + depth * (float)z / rows});
        int stride = cols + 1;
        for (int z = 0; z < rows; ++z)
            for (int x = 0; x < cols; ++x) {
                int a = z * stride + x, b = a + 1, c = a + stride, d = c + 1;
                m.triangles.insert(m.triangles.end(), {a, c, b, b, c, d});
            }
        return m;
    }

    /// A flat horizontal ground plane in the XZ plane (good for 3D floors).
    static Mesh Plane(float size = 10.0f) {
        float h = size * 0.5f;
        Mesh m;
        m.name = "Plane";
        m.vertices = {{-h, 0, -h}, {h, 0, -h}, {h, 0, h}, {-h, 0, h}};
        m.triangles = {0, 2, 1, 0, 3, 2};
        return m;
    }

    /// A UV sphere of the given radius.
    static Mesh Sphere(float radius = 0.5f, int rings = 8, int sectors = 12) {
        Mesh m;
        m.name = "Sphere";
        const float kPi = 3.14159265358979323846f;
        for (int r = 0; r <= rings; ++r) {
            float phi = kPi * (float)r / rings;            // 0..pi (top to bottom)
            for (int s = 0; s <= sectors; ++s) {
                float theta = 2.0f * kPi * (float)s / sectors;
                m.vertices.push_back({radius * std::sin(phi) * std::cos(theta),
                                      radius * std::cos(phi),
                                      radius * std::sin(phi) * std::sin(theta)});
                m.uvs.push_back({(float)s / sectors, 1.0f - (float)r / rings});
            }
        }
        int stride = sectors + 1;
        for (int r = 0; r < rings; ++r)
            for (int s = 0; s < sectors; ++s) {
                int a = r * stride + s, b = a + stride;
                m.triangles.insert(m.triangles.end(), {a, b, a + 1, a + 1, b, b + 1});
            }
        return m;
    }

    /// A capped cylinder along Y.
    static Mesh Cylinder(float radius = 0.5f, float height = 1.0f, int sectors = 12) {
        Mesh m;
        m.name = "Cylinder";
        const float kPi = 3.14159265358979323846f;
        float h = height * 0.5f;
        for (int s = 0; s <= sectors; ++s) {            // interleaved top/bottom ring
            float th = 2.0f * kPi * (float)s / sectors;
            float x = radius * std::cos(th), z = radius * std::sin(th);
            float u = (float)s / sectors;
            m.vertices.push_back({x, h, z});  m.uvs.push_back({u, 1.0f});
            m.vertices.push_back({x, -h, z}); m.uvs.push_back({u, 0.0f});
        }
        for (int s = 0; s < sectors; ++s) {             // side quads
            int t0 = s * 2, b0 = s * 2 + 1, t1 = (s + 1) * 2, b1 = (s + 1) * 2 + 1;
            m.triangles.insert(m.triangles.end(), {t0, b0, t1, t1, b0, b1});
        }
        int topC = (int)m.vertices.size(); m.vertices.push_back({0, h, 0});  m.uvs.push_back({0.5f, 0.5f});
        int botC = (int)m.vertices.size(); m.vertices.push_back({0, -h, 0}); m.uvs.push_back({0.5f, 0.5f});
        for (int s = 0; s < sectors; ++s) {             // caps
            m.triangles.insert(m.triangles.end(), {topC, s * 2, (s + 1) * 2});
            m.triangles.insert(m.triangles.end(), {botC, (s + 1) * 2 + 1, s * 2 + 1});
        }
        return m;
    }

    /// A cone with a circular base on the XZ plane and apex on +Y.
    static Mesh Cone(float radius = 0.5f, float height = 1.0f, int sectors = 16) {
        Mesh m;
        m.name = "Cone";
        const float kPi = 3.14159265358979323846f;
        float h = height * 0.5f;
        for (int s = 0; s < sectors; ++s) {            // base ring
            float th = 2.0f * kPi * (float)s / sectors;
            m.vertices.push_back({radius * std::cos(th), -h, radius * std::sin(th)});
        }
        int apex = (int)m.vertices.size(); m.vertices.push_back({0, h, 0});
        int base = (int)m.vertices.size(); m.vertices.push_back({0, -h, 0});
        for (int s = 0; s < sectors; ++s) {
            int n = (s + 1) % sectors;
            m.triangles.insert(m.triangles.end(), {s, n, apex});   // side
            m.triangles.insert(m.triangles.end(), {base, n, s});   // base cap
        }
        return m;
    }

    /// A torus (donut) around the Y axis. `radius` is the ring radius, `tube`
    /// the tube thickness.
    static Mesh Torus(float radius = 0.5f, float tube = 0.2f, int rings = 16, int sides = 10) {
        Mesh m;
        m.name = "Torus";
        const float kPi = 3.14159265358979323846f;
        for (int r = 0; r < rings; ++r) {
            float u = 2.0f * kPi * (float)r / rings;
            for (int s = 0; s < sides; ++s) {
                float v = 2.0f * kPi * (float)s / sides;
                float cx = (radius + tube * std::cos(v));
                m.vertices.push_back({cx * std::cos(u), tube * std::sin(v), cx * std::sin(u)});
            }
        }
        for (int r = 0; r < rings; ++r)
            for (int s = 0; s < sides; ++s) {
                int r1 = (r + 1) % rings, s1 = (s + 1) % sides;
                int a = r * sides + s, b = r1 * sides + s;
                int c = r1 * sides + s1, d = r * sides + s1;
                m.triangles.insert(m.triangles.end(), {a, b, d, d, b, c});
            }
        return m;
    }

    /// A hollow tube / pipe / ring along Y: an outer and inner wall joined by top
    /// and bottom annulus caps. `inner` < `outer`. Good for rings, portals, pipes.
    static Mesh Tube(float outer = 0.5f, float inner = 0.3f, float height = 1.0f, int sectors = 24) {
        Mesh m;
        m.name = "Tube";
        if (inner < 0.0f) inner = 0.0f;
        if (inner > outer) inner = outer;
        const float kPi = 3.14159265358979323846f;
        float h = height * 0.5f;
        for (int s = 0; s < sectors; ++s) {
            float th = 2.0f * kPi * (float)s / sectors;
            float c = std::cos(th), sn = std::sin(th);
            m.vertices.push_back({outer * c,  h, outer * sn});  // base+0 outer-top
            m.vertices.push_back({outer * c, -h, outer * sn});  // base+1 outer-bot
            m.vertices.push_back({inner * c,  h, inner * sn});  // base+2 inner-top
            m.vertices.push_back({inner * c, -h, inner * sn});  // base+3 inner-bot
        }
        for (int s = 0; s < sectors; ++s) {
            int b = s * 4, b2 = ((s + 1) % sectors) * 4;
            int ot = b, ob = b + 1, it = b + 2, ib = b + 3;
            int ot2 = b2, ob2 = b2 + 1, it2 = b2 + 2, ib2 = b2 + 3;
            m.triangles.insert(m.triangles.end(), {ot, ob, ob2,  ot, ob2, ot2});   // outer wall
            m.triangles.insert(m.triangles.end(), {it, it2, ib2,  it, ib2, ib});   // inner wall
            m.triangles.insert(m.triangles.end(), {ot, ot2, it2,  ot, it2, it});   // top ring
            m.triangles.insert(m.triangles.end(), {ob, ib, ib2,  ob, ib2, ob2});   // bottom ring
        }
        return m;
    }

    /// A geodesic sphere: an icosahedron subdivided `subdivisions` times and
    /// projected to the radius. Triangles are near-uniform (no pinching at the
    /// poles like the UV Sphere), so it shades and tessellates evenly.
    static Mesh Icosphere(float radius = 0.5f, int subdivisions = 2) {
        const float t = 1.61803398875f;          // golden ratio
        Mesh m;
        m.name = "Icosphere";
        m.vertices = {
            {-1,  t,  0}, { 1,  t,  0}, {-1, -t,  0}, { 1, -t,  0},
            { 0, -1,  t}, { 0,  1,  t}, { 0, -1, -t}, { 0,  1, -t},
            { t,  0, -1}, { t,  0,  1}, {-t,  0, -1}, {-t,  0,  1},
        };
        m.triangles = {
            0,11,5, 0,5,1, 0,1,7, 0,7,10, 0,10,11,
            1,5,9, 5,11,4, 11,10,2, 10,7,6, 7,1,8,
            3,9,4, 3,4,2, 3,2,6, 3,6,8, 3,8,9,
            4,9,5, 2,4,11, 6,2,10, 8,6,7, 9,8,1,
        };
        for (int i = 0; i < subdivisions; ++i) m.Subdivide();
        m.ProjectToSphere(radius);
        m.name = "Icosphere";                     // Subdivide clears the name
        return m;
    }

    /// Recreate a primitive mesh from its name.
    static Mesh FromName(const std::string& n) {
        if (n == "Pyramid")   return Pyramid();
        if (n == "Quad")      return Quad();
        if (n == "Plane")     return Plane();
        if (n == "Sphere")    return Sphere();
        if (n == "Cylinder")  return Cylinder();
        if (n == "Cone")      return Cone();
        if (n == "Torus")     return Torus();
        if (n == "Capsule")   return Capsule();
        if (n == "Icosphere") return Icosphere();
        if (n == "Grid")      return Grid();
        if (n == "Wedge")     return Wedge();
        if (n == "Tube")      return Tube();
        if (n == "Human" || n == "Humanoid") return Humanoid();
        return Cube();
    }

    /// Axis-aligned bounding box of the vertices. `outMin`/`outMax` receive the
    /// corners; safe on an empty mesh (both set to origin). Useful for placing,
    /// centering, framing the camera, or simple culling.
    void Bounds(Vec3& outMin, Vec3& outMax) const {
        if (vertices.empty()) { outMin = outMax = Vec3{0, 0, 0}; return; }
        outMin = outMax = vertices[0];
        for (const Vec3& v : vertices) {
            outMin = {std::fmin(outMin.x, v.x), std::fmin(outMin.y, v.y), std::fmin(outMin.z, v.z)};
            outMax = {std::fmax(outMax.x, v.x), std::fmax(outMax.y, v.y), std::fmax(outMax.z, v.z)};
        }
    }
    /// Center of the bounding box.
    Vec3 Center() const {
        Vec3 lo, hi; Bounds(lo, hi);
        return {(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
    }
    /// Size (extent) of the bounding box.
    Vec3 Size() const {
        Vec3 lo, hi; Bounds(lo, hi);
        return {hi.x - lo.x, hi.y - lo.y, hi.z - lo.z};
    }

    /// Translate so the bounding-box center sits at the origin — fixes imported
    /// OBJs modeled off to one side so they rotate/scale about their middle.
    void RecenterToOrigin() {
        Vec3 c = Center();
        for (Vec3& v : vertices) v -= c;
    }

    /// Move the pivot to the base: centered in X/Z with the lowest point at
    /// Y = 0, so the model rests on the ground when placed at y = 0.
    void GroundPivot() {
        if (vertices.empty()) return;
        Vec3 lo, hi; Bounds(lo, hi);
        Vec3 c{(lo.x + hi.x) * 0.5f, lo.y, (lo.z + hi.z) * 0.5f};
        for (Vec3& v : vertices) v -= c;
    }

    /// Uniformly scale (about the origin) so the largest bounding-box dimension
    /// equals `maxExtent` — normalize wildly-sized imported models to a usable
    /// scale. No-op on a flat/empty mesh.
    void ScaleToFit(float maxExtent = 1.0f) {
        Vec3 s = Size();
        float biggest = std::fmax(s.x, std::fmax(s.y, s.z));
        if (biggest <= 1e-6f) return;
        float k = maxExtent / biggest;
        for (Vec3& v : vertices) v = v * k;
    }

    // ---- Modeling: import/export and mesh operations -------------------

    /// Load a Wavefront .OBJ (v positions + f faces; polygons are fan-triangulated,
    /// v/vt/vn and negative indices handled). Returns an empty mesh on failure;
    /// `ok` (if given) reports success.
    static Mesh LoadOBJ(const std::string& path, bool* ok = nullptr) {
        Mesh m;
        std::ifstream f(path);
        if (!f) { if (ok) *ok = false; return m; }
        std::string line;
        while (std::getline(f, line)) {
            std::istringstream ss(line);
            std::string tag; ss >> tag;
            if (tag == "v") {
                Vec3 v; ss >> v.x >> v.y >> v.z; m.vertices.push_back(v);
            } else if (tag == "f") {
                std::vector<int> idx; std::string tok;
                while (ss >> tok) {
                    int vi = std::atoi(tok.c_str());      // stops at '/'
                    if (vi < 0) vi = (int)m.vertices.size() + vi + 1; // relative
                    idx.push_back(vi - 1);                 // 1-based -> 0-based
                }
                for (std::size_t i = 2; i < idx.size(); ++i)
                    m.triangles.insert(m.triangles.end(), {idx[0], idx[i - 1], idx[i]});
            }
        }
        if (ok) *ok = true;
        return m;
    }

    /// Write this mesh to a Wavefront .OBJ. Returns false on I/O error.
    bool SaveOBJ(const std::string& path) const {
        std::ofstream f(path);
        if (!f) return false;
        f << "# OkaySpace mesh\n";
        for (const Vec3& v : vertices) f << "v " << v.x << " " << v.y << " " << v.z << "\n";
        for (std::size_t i = 0; i + 2 < triangles.size(); i += 3)
            f << "f " << triangles[i] + 1 << " " << triangles[i + 1] + 1 << " "
              << triangles[i + 2] + 1 << "\n";
        return static_cast<bool>(f);
    }

    /// A copy with each vertex scaled (per-axis) then offset — the basic modeling
    /// transform for placing/sizing a part before combining.
    Mesh Transformed(const Vec3& scale, const Vec3& offset) const {
        Mesh m = *this; m.name = "";
        for (Vec3& v : m.vertices)
            v = {v.x * scale.x + offset.x, v.y * scale.y + offset.y, v.z * scale.z + offset.z};
        return m;
    }

    /// Bake an Euler rotation (degrees) into the vertices — orient a part (a cone
    /// roof, an arm) before Combine. Returns a rotated copy; RotateVerts does it
    /// in place.
    void RotateVerts(const Vec3& eulerDegrees) {
        Quat q = Quat::Euler(eulerDegrees);
        for (Vec3& v : vertices) v = q * v;
        name = "";
    }
    Mesh Rotated(const Vec3& eulerDegrees) const { Mesh m = *this; m.RotateVerts(eulerDegrees); return m; }

    /// Reverse triangle winding so faces point the other way — fix an
    /// inside-out imported mesh, or make an inward-facing skybox shell.
    void FlipWinding() {
        for (std::size_t i = 0; i + 2 < triangles.size(); i += 3)
            std::swap(triangles[i + 1], triangles[i + 2]);
    }

    /// Append another mesh into this one (re-indexing its triangles) — build a
    /// compound model (e.g. a snowman) from primitive parts.
    void Combine(const Mesh& other) {
        int base = (int)vertices.size();
        name = "";
        vertices.insert(vertices.end(), other.vertices.begin(), other.vertices.end());
        for (int t : other.triangles) triangles.push_back(t + base);
    }
    static Mesh Combined(const Mesh& a, const Mesh& b) { Mesh m = a; m.Combine(b); return m; }

    /// Extrude a 2D outline (XY, convex, counter-clockwise) into a 3D prism of
    /// the given depth along Z — custom signs, logos, blocky props. Builds a
    /// front and back cap (fan-triangulated) plus side walls.
    static Mesh Extrude(const std::vector<Vec2>& outline, float depth = 1.0f) {
        Mesh m;
        const int n = (int)outline.size();
        if (n < 3) return m;
        float hz = depth * 0.5f;
        for (const Vec2& p : outline) m.vertices.push_back({p.x, p.y, -hz}); // back ring  [0..n)
        for (const Vec2& p : outline) m.vertices.push_back({p.x, p.y,  hz}); // front ring [n..2n)
        for (int i = 1; i + 1 < n; ++i) {
            m.triangles.insert(m.triangles.end(), {0, i + 1, i});             // back cap (-Z)
            m.triangles.insert(m.triangles.end(), {n, n + i, n + i + 1});     // front cap (+Z)
        }
        for (int i = 0; i < n; ++i) {                                        // side walls
            int j = (i + 1) % n;
            m.triangles.insert(m.triangles.end(), {i, j, n + j});
            m.triangles.insert(m.triangles.end(), {i, n + j, n + i});
        }
        return m;
    }

    /// Revolve a 2D profile around the Y axis to make a surface of revolution —
    /// vases, bottles, goblets, columns. Each profile point is (radius, height);
    /// `segments` sets the angular resolution.
    static Mesh Lathe(const std::vector<Vec2>& profile, int segments = 16) {
        Mesh m;
        const int rows = (int)profile.size();
        if (rows < 2 || segments < 3) return m;
        const float kPi = 3.14159265358979323846f;
        for (int s = 0; s < segments; ++s) {
            float th = 2.0f * kPi * (float)s / segments;
            float c = std::cos(th), sn = std::sin(th);
            for (const Vec2& p : profile)
                m.vertices.push_back({p.x * c, p.y, p.x * sn});  // x = radius, y = height
        }
        for (int s = 0; s < segments; ++s) {
            int s1 = (s + 1) % segments;
            for (int r = 0; r + 1 < rows; ++r) {
                int a = s * rows + r,  b = s * rows + r + 1;
                int c = s1 * rows + r, d = s1 * rows + r + 1;
                m.triangles.insert(m.triangles.end(), {a, c, b, b, c, d});
            }
        }
        return m;
    }

    /// Merge vertices closer than `epsilon` into one and re-index triangles,
    /// then drop any triangle that collapsed to a line/point. Cleans up imported
    /// OBJs and Combine()d meshes so they're watertight. Returns vertices removed.
    int WeldVertices(float epsilon = 1e-5f) {
        std::vector<Vec3> unique;
        std::vector<int>  remap(vertices.size());
        float e2 = epsilon * epsilon;
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            int found = -1;
            for (std::size_t j = 0; j < unique.size(); ++j)
                if ((unique[j] - vertices[i]).SqrMagnitude() <= e2) { found = (int)j; break; }
            if (found < 0) { found = (int)unique.size(); unique.push_back(vertices[i]); }
            remap[i] = found;
        }
        int removed = (int)vertices.size() - (int)unique.size();
        std::vector<int> tris;
        for (std::size_t i = 0; i + 2 < triangles.size(); i += 3) {
            int a = remap[triangles[i]], b = remap[triangles[i + 1]], c = remap[triangles[i + 2]];
            if (a != b && b != c && a != c) tris.insert(tris.end(), {a, b, c}); // skip degenerate
        }
        vertices = std::move(unique);
        triangles = std::move(tris);
        if (removed > 0) name = "";
        return removed;
    }

    /// Subdivide every triangle into 4 by splitting each edge at its midpoint —
    /// adds detail (and, after re-projecting, smooths primitives). Midpoints are
    /// shared between adjacent triangles so the mesh stays welded.
    void Subdivide() {
        std::vector<int> out;
        std::map<std::pair<int, int>, int> mids;     // edge -> new midpoint index
        auto midpoint = [&](int a, int b) {
            auto key = std::minmax(a, b);
            auto it = mids.find({key.first, key.second});
            if (it != mids.end()) return it->second;
            Vec3 m = (vertices[a] + vertices[b]) * 0.5f;
            int idx = (int)vertices.size();
            vertices.push_back(m);
            mids[{key.first, key.second}] = idx;
            return idx;
        };
        const bool faceCols = HasFaceColors();
        std::vector<Color> outCols;
        for (std::size_t i = 0, face = 0; i + 2 < triangles.size(); i += 3, ++face) {
            int a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
            int ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
            out.insert(out.end(), {a, ab, ca,  ab, b, bc,  ca, bc, c,  ab, bc, ca});
            if (faceCols) { Color fc = triColors[face]; outCols.insert(outCols.end(), {fc, fc, fc, fc}); }
        }
        triangles = std::move(out);
        if (faceCols) triColors = std::move(outCols);
        name = "";
    }

    /// Push every vertex onto a sphere of the given radius about the origin —
    /// pair with Subdivide() to turn a cube/icosphere into a smooth ball.
    void ProjectToSphere(float radius = 0.5f) {
        for (Vec3& v : vertices) {
            float m = v.Magnitude();
            if (m > 1e-6f) v = v * (radius / m);
        }
        name = "";
    }

    /// Per-vertex normals, area-weighted from the adjacent faces (for lighting /
    /// export). Size matches `vertices`; degenerate meshes give zero vectors.
    std::vector<Vec3> Normals() const {
        std::vector<Vec3> n(vertices.size(), Vec3{0, 0, 0});
        for (std::size_t i = 0; i + 2 < triangles.size(); i += 3) {
            int a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
            Vec3 face = Vec3::Cross(vertices[b] - vertices[a], vertices[c] - vertices[a]);
            n[a] += face; n[b] += face; n[c] += face;   // area-weighted accumulation
        }
        for (Vec3& v : n) if (v.SqrMagnitude() > 1e-12f) v = v.Normalized();
        return n;
    }

    /// A capsule (cylinder body with hemispherical caps) along Y.
    static Mesh Capsule(float radius = 0.5f, float height = 1.0f, int sectors = 12, int rings = 6) {
        Mesh m;
        m.name = "Capsule";
        const float kPi = 3.14159265358979323846f;
        float cyl = std::fmax(0.0f, height - 2.0f * radius) * 0.5f; // half cylinder length
        // Build latitude rings: top hemisphere, then bottom hemisphere, offset in Y.
        int half = rings;
        for (int r = 0; r <= half; ++r) {                 // top cap (0..pi/2)
            float phi = (kPi * 0.5f) * (float)r / half;
            float y = cyl + radius * std::cos(phi), rr = radius * std::sin(phi);
            for (int s = 0; s <= sectors; ++s) {
                float th = 2.0f * kPi * (float)s / sectors;
                m.vertices.push_back({rr * std::cos(th), y, rr * std::sin(th)});
            }
        }
        for (int r = 0; r <= half; ++r) {                 // bottom cap (pi/2..pi)
            float phi = (kPi * 0.5f) * (1.0f + (float)r / half);
            float y = -cyl + radius * std::cos(phi), rr = radius * std::sin(phi);
            for (int s = 0; s <= sectors; ++s) {
                float th = 2.0f * kPi * (float)s / sectors;
                m.vertices.push_back({rr * std::cos(th), y, rr * std::sin(th)});
            }
        }
        int stride = sectors + 1, totalRings = 2 * (half + 1);
        for (int r = 0; r < totalRings - 1; ++r)
            for (int s = 0; s < sectors; ++s) {
                int a = r * stride + s, b = a + stride;
                m.triangles.insert(m.triangles.end(), {a, b, a + 1, a + 1, b, b + 1});
            }
        return m;
    }

    /// Append another mesh's geometry, scaled (per-axis) then translated.
    /// Triangle indices are rebased; this is how compound meshes are built. If
    /// `col` is given, every appended face takes that color (per-part coloring).
    void Add(const Mesh& src, Vec3 offset, Vec3 scale = {1, 1, 1}, const Color* col = nullptr) {
        int base = (int)vertices.size();
        for (const Vec3& v : src.vertices)
            vertices.push_back({v.x * scale.x + offset.x,
                                v.y * scale.y + offset.y,
                                v.z * scale.z + offset.z});
        for (int t : src.triangles) triangles.push_back(t + base);
        if (col) for (int i = 0, n = (int)src.triangles.size() / 3; i < n; ++i) triColors.push_back(*col);
    }

    /// As Add(), but rotate (Euler degrees) about `pivot` after scaling — used to
    /// pose limbs (e.g. spread arms into an A-pose).
    void AddPosed(const Mesh& src, Vec3 offset, Vec3 scale, Vec3 eulerDeg, Vec3 pivot,
                  const Color* col = nullptr) {
        int base = (int)vertices.size();
        Quat q = Quat::Euler(eulerDeg);
        for (const Vec3& v : src.vertices) {
            Vec3 s{v.x * scale.x + offset.x - pivot.x,
                   v.y * scale.y + offset.y - pivot.y,
                   v.z * scale.z + offset.z - pivot.z};
            Vec3 r = q * s;
            vertices.push_back({r.x + pivot.x, r.y + pivot.y, r.z + pivot.z});
        }
        for (int t : src.triangles) triangles.push_back(t + base);
        if (col) for (int i = 0, n = (int)src.triangles.size() / 3; i < n; ++i) triColors.push_back(*col);
    }

    /// A low-poly humanoid blockout assembled from primitive parts (head, neck,
    /// torso, hips, arms, hands, legs, feet), shaped by `p`. Stands on ~Y=0.
    /// Subdivide()/SubdivideSmooth() it to raise it from low-poly to high-poly.
    /// When `c` is given, parts are colored per-region (skin/shirt/pants/shoes),
    /// a hair cap and a simple face (eyes/brows/nose/mouth) are added optionally.
    static Mesh Humanoid(const HumanoidParams& p, const HumanoidColors* c = nullptr) {
        Mesh m; m.name = "Human";
        const float H = p.height, B = p.build, hd = p.headSize;
        const float sw = 0.46f * p.shoulderWidth;   // arm half-spacing
        const float hw = 0.20f * p.hipWidth;        // leg half-spacing
        const float aL = p.armLength, lL = p.legLength;
        const float bd = p.bodyDepth;
        const Color* skin  = c ? &c->skin  : nullptr;
        const Color* shirt = c ? &c->shirt : nullptr;
        const Color* pants = c ? &c->pants : nullptr;
        const Color* shoes = c ? &c->shoes : nullptr;
        // Torso length grows the torso upward (bottom stays at the hips); the upper
        // body (neck, head, arms) rides up by the same amount so it stays attached.
        const float up = 0.78f * H * (p.torsoLength - 1.0f);
        const float headY = 1.78f * H + up;
        m.Add(Sphere(0.5f, 6, 8), {0.0f, headY, 0.0f}, {0.40f * hd, 0.46f * hd, 0.40f * hd}, skin); // head
        if (c && c->hasHair) {                       // hair, by style
            const Color* h = &c->hair;
            m.Add(Sphere(0.5f, 5, 8), {0.0f, headY + 0.10f * hd, 0.0f},      // base cap
                  {0.44f * hd, 0.34f * hd, 0.44f * hd}, h);
            switch (p.hairStyle) {
                case 2:  // long: a panel down the back to the shoulders
                    m.Add(Cube(1.0f), {0.0f, headY - 0.22f * hd, -0.16f * hd},
                          {0.40f * hd, 0.55f * hd, 0.16f * hd}, h);
                    break;
                case 3:  // spiky: small pyramids on top
                    for (int sx = -1; sx <= 1; ++sx) for (int sz = -1; sz <= 1; ++sz)
                        m.Add(Pyramid(1.0f), {sx * 0.13f * hd, headY + 0.26f * hd, sz * 0.13f * hd},
                              {0.10f * hd, 0.16f * hd, 0.10f * hd}, h);
                    break;
                case 4:  // ponytail: a capsule trailing behind
                    m.Add(Capsule(0.5f, 1.0f, 6, 3), {0.0f, headY - 0.05f * hd, -0.26f * hd},
                          {0.12f * hd, 0.34f * hd, 0.12f * hd}, h);
                    break;
                case 5:  // mohawk: a tall thin strip along the top
                    m.Add(Cube(1.0f), {0.0f, headY + 0.24f * hd, 0.0f},
                          {0.08f * hd, 0.22f * hd, 0.46f * hd}, h);
                    break;
                default: break;                       // 0/1: just the cap
            }
        }
        if (c && c->hasHat) {                        // brimmed hat on top of the head
            m.Add(Cylinder(0.5f, 1.0f, 10), {0.0f, headY + 0.20f * hd, 0.0f},
                  {0.64f * hd, 0.04f * hd, 0.64f * hd}, &c->hat);      // brim
            m.Add(Cylinder(0.5f, 1.0f, 10), {0.0f, headY + 0.34f * hd, 0.0f},
                  {0.42f * hd, 0.24f * hd, 0.42f * hd}, &c->hat);      // crown
        }
        if (c && p.ears) {                           // ears on the head sides
            for (int s = -1; s <= 1; s += 2)
                m.Add(Sphere(0.5f, 4, 5), {s * 0.21f * hd, headY + 0.01f * hd, 0.0f},
                      {0.05f * hd, 0.09f * hd, 0.07f * hd}, skin);
        }
        if (c && c->hasFace) {                       // eyes, pupils, brows, nose, mouth on +Z
            const float fz = 0.19f * hd, ex = 0.09f * hd * p.eyeSpacing;
            Color pupil{c->eye.r * 0.25f, c->eye.g * 0.25f, c->eye.b * 0.25f, 1.0f};
            for (int s = -1; s <= 1; s += 2) {
                m.Add(Sphere(0.5f, 4, 5), {s * ex, headY + 0.04f * hd, fz},
                      {0.06f * hd, 0.07f * hd, 0.05f * hd}, &c->eye);                       // eye white/iris
                m.Add(Sphere(0.5f, 3, 4), {s * ex, headY + 0.04f * hd, fz + 0.03f * hd},
                      {0.028f * hd, 0.032f * hd, 0.02f * hd}, &pupil);                       // pupil
                // Brow, tilted by browAngle (inner end down for "angry").
                m.AddPosed(Cube(1.0f), {s * ex, headY + 0.12f * hd, fz},
                           {0.09f * hd, 0.025f * hd, 0.04f * hd},
                           {0.0f, 0.0f, (float)s * p.browAngle}, {s * ex, headY + 0.12f * hd, fz}, &c->hair);
            }
            m.Add(Cube(1.0f), {0.0f, headY - 0.01f * hd, fz + 0.02f * hd},
                  {0.05f * hd, 0.08f * hd, 0.06f * hd}, &c->skin);                          // nose
            m.Add(Cube(1.0f), {0.0f, headY - 0.13f * hd, fz},
                  {0.12f * hd * p.mouthWidth, 0.025f * hd, 0.04f * hd}, &c->hair);          // mouth
            if (c->hasGlasses)                        // a sunglasses bar across the eyes
                m.Add(Cube(1.0f), {0.0f, headY + 0.04f * hd, fz + 0.03f * hd},
                      {(0.18f * p.eyeSpacing + 0.14f) * hd, 0.06f * hd, 0.03f * hd}, &c->glasses);
        }
        m.Add(Cylinder(0.5f, 1.0f, 6), {0.0f, 1.52f * H + up, 0.0f}, {0.16f, 0.20f * p.neckLength, 0.16f}, skin); // neck
        m.Add(Cube(1.0f), {0.0f, (0.71f * H) + 0.39f * H * p.torsoLength, 0.0f},
              {0.62f * p.shoulderWidth * B, 0.78f * H * p.torsoLength, 0.34f * B * bd}, shirt);         // torso
        m.Add(Cube(1.0f), {0.0f, 0.66f * H, 0.0f}, {0.56f * p.hipWidth * B, 0.24f * H, 0.34f * B * bd}, pants); // hips
        for (int s = -1; s <= 1; s += 2) {                                    // arms + hands
            Vec3 shoulder{s * sw, 1.50f * H + up, 0.0f};       // pivot at the shoulder
            Vec3 armRot{(float)s * p.armSwing, 0.0f, (float)s * -p.armSpread};  // out + fore/aft
            m.AddPosed(Capsule(0.5f, 1.0f, 6, 3), {s * sw, 1.18f * H + up, 0.0f},
                       {0.22f * B, 0.64f * aL * H, 0.22f * B}, armRot, shoulder, shirt);
            m.AddPosed(Sphere(0.5f, 5, 6), {s * sw, (1.18f - 0.54f * aL) * H + up, 0.0f},
                       {0.17f * B * p.handSize, 0.17f * B * p.handSize, 0.17f * B * p.handSize},
                       armRot, shoulder, skin);
        }
        for (int s = -1; s <= 1; s += 2) {                                    // legs + feet
            Vec3 hip{s * hw, 0.60f * H, 0.0f};
            Vec3 legRot{(float)s * p.legSwing, 0.0f, (float)s * -p.legSpread};
            m.AddPosed(Capsule(0.5f, 1.0f, 6, 3), {s * hw, 0.12f * H, 0.0f},
                       {0.26f * B, 0.96f * lL * H, 0.26f * B}, legRot, hip, pants);
            m.AddPosed(Cube(1.0f), {s * hw, (0.12f - 0.58f * lL) * H, 0.08f},
                       {0.26f * B * p.footSize, 0.12f * p.footSize, 0.52f * p.footSize}, legRot, hip, shoes);
        }
        return m;
    }
    static Mesh Humanoid() { return Humanoid(HumanoidParams{}); }

    /// Laplacian smoothing: relax each vertex toward the average of its
    /// edge-connected neighbours by `amount` (0..1). Rounds off a faceted mesh;
    /// pairs with Subdivide() to take low-poly geometry up to smooth high-poly.
    void Smooth(float amount = 0.5f) {
        if (amount <= 0.0f || vertices.empty()) return;
        std::vector<Vec3> sum(vertices.size(), Vec3{0, 0, 0});
        std::vector<int>  cnt(vertices.size(), 0);
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            int idx[3] = {triangles[t], triangles[t + 1], triangles[t + 2]};
            for (int e = 0; e < 3; ++e) {
                int v = idx[e], w = idx[(e + 1) % 3];
                sum[v] = sum[v] + vertices[w]; ++cnt[v];
                sum[w] = sum[w] + vertices[v]; ++cnt[w];
            }
        }
        for (std::size_t i = 0; i < vertices.size(); ++i)
            if (cnt[i] > 0)
                vertices[i] = vertices[i] * (1.0f - amount) + sum[i] * (amount / cnt[i]);
    }

    /// Convenience: `iterations` rounds of Subdivide()+Smooth() — the low-poly to
    /// high-poly pipeline (e.g. on a Humanoid()). Each round quadruples triangles.
    void SubdivideSmooth(int iterations = 1, float amount = 0.5f) {
        for (int i = 0; i < iterations && !triangles.empty(); ++i) { Subdivide(); Smooth(amount); }
    }
};

} // namespace okay
