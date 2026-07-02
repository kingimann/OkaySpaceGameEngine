#pragma once
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Render/Color.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <fstream>
#include <map>
#include <set>
#include <tuple>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace okay {

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
    std::vector<Vec3> normals;    // optional per-vertex normals (parallel to vertices);
                                  // when present the renderer smooth (Gouraud) shades.

    int TriangleCount() const { return static_cast<int>(triangles.size() / 3); }
    bool HasFaceColors() const { return (int)triColors.size() == TriangleCount() && !triColors.empty(); }
    bool HasNormals() const { return normals.size() == vertices.size() && !vertices.empty(); }

    /// Compute per-vertex smooth normals for Gouraud shading. Vertices sharing a
    /// position (even across separately-built parts) are grouped so the normal is
    /// averaged across the seam — this both rounds curved surfaces and hides the
    /// joins between assembled body parts. Area-weighted (uses un-normalized face
    /// normals) for a natural result.
    void ComputeSmoothNormals() {
        normals.assign(vertices.size(), Vec3{0, 0, 0});
        std::map<std::tuple<int, int, int>, int> rep;
        std::vector<int> grp(vertices.size());
        auto key = [](const Vec3& p) {
            return std::make_tuple((int)std::lround(p.x * 2048.0f),
                                   (int)std::lround(p.y * 2048.0f),
                                   (int)std::lround(p.z * 2048.0f));
        };
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            auto k = key(vertices[i]);
            auto it = rep.find(k);
            grp[i] = (it == rep.end()) ? (rep[k] = (int)i) : it->second;
        }
        for (std::size_t i = 0; i + 2 < triangles.size(); i += 3) {
            int a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
            Vec3 fn = Vec3::Cross(vertices[b] - vertices[a], vertices[c] - vertices[a]);
            normals[grp[a]] += fn; normals[grp[b]] += fn; normals[grp[c]] += fn;
        }
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            Vec3 n = normals[grp[i]];
            float m = n.Magnitude();
            normals[i] = m > 1e-8f ? n * (1.0f / m) : Vec3{0, 1, 0};
        }
    }

    /// Keep normals valid after an edit WITHOUT changing the mesh's shading mode:
    /// if it was smooth-shaded (had per-vertex normals) recompute them at the new
    /// resolution; if it had none, leave it flat (the renderer face-shades). This
    /// fixes lighting after edits that add/move vertices (Subdivide, deformers) —
    /// otherwise a stale, wrong-sized normals array corrupts the shading.
    void RefreshNormals() {
        if (normals.empty()) return;                 // flat-shaded: stay flat (face normals)
        ComputeSmoothNormals();                       // was smooth: rebuild at the new resolution
    }

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
        m.ComputeSmoothNormals();   // round the surface (no facets)
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
        m.ComputeSmoothNormals();   // smooth torus surface
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
        m.ComputeSmoothNormals();                 // round the surface (no facets)
        return m;
    }

    /// A broadleaf tree: a tapered trunk under a rounded leafy canopy. Sits on
    /// the ground (base at y=0), ~2 units tall. Per-face colored + smooth-shaded
    /// so it drops onto terrain and looks right immediately.
    static Mesh Tree() {
        Mesh m; m.name = "Tree";
        Color bark = Color::FromBytes(102, 72, 46);
        Color leaf = Color::FromBytes(60, 122, 56);
        m.Add(Cylinder(0.5f, 1.0f, 8), {0.0f, 0.45f, 0.0f}, {0.16f, 0.9f, 0.16f}, &bark);  // trunk
        m.Add(Sphere(0.5f, 8, 10), {0.0f, 1.30f, 0.0f},  {1.05f, 1.10f, 1.05f}, &leaf);    // canopy
        m.Add(Sphere(0.5f, 7, 9),  {-0.35f, 1.05f, 0.18f}, {0.7f, 0.7f, 0.7f}, &leaf);
        m.Add(Sphere(0.5f, 7, 9),  {0.34f, 1.12f, -0.2f}, {0.66f, 0.66f, 0.66f}, &leaf);
        m.ComputeSmoothNormals();
        return m;
    }
    /// A conifer / pine: a trunk under stacked cones. Base at y=0, ~2.3 tall.
    static Mesh Pine() {
        Mesh m; m.name = "Pine";
        Color bark = Color::FromBytes(96, 66, 42);
        Color leaf = Color::FromBytes(42, 104, 64);
        m.Add(Cylinder(0.5f, 1.0f, 8), {0.0f, 0.35f, 0.0f}, {0.13f, 0.7f, 0.13f}, &bark);  // trunk
        m.Add(Cone(0.5f, 1.0f, 10), {0.0f, 1.05f, 0.0f}, {1.10f, 1.0f, 1.10f}, &leaf);
        m.Add(Cone(0.5f, 1.0f, 10), {0.0f, 1.55f, 0.0f}, {0.85f, 0.9f, 0.85f}, &leaf);
        m.Add(Cone(0.5f, 1.0f, 10), {0.0f, 2.00f, 0.0f}, {0.55f, 0.8f, 0.55f}, &leaf);
        m.ComputeSmoothNormals();
        return m;
    }
    /// A boulder: a low, irregularly-lumped dome. Base near y=0, ~0.8 tall.
    static Mesh Rock() {
        Mesh m = Icosphere(0.5f, 2);
        Color stone = Color::FromBytes(124, 118, 110);
        // Lump it: push each vertex by a smooth pseudo-random amount, then squash.
        for (Vec3& v : m.vertices) {
            float n = std::sin(v.x * 7.3f + 1.1f) * std::cos(v.z * 6.1f + 2.7f)
                    + std::sin(v.y * 5.7f + 0.5f) * 0.5f;
            float s = 1.0f + 0.22f * n;
            v = {v.x * s, v.y * s * 0.62f, v.z * s};   // squashed boulder
        }
        Vec3 lo, hi; m.Bounds(lo, hi);
        for (Vec3& v : m.vertices) v.y -= lo.y;        // rest on the ground
        m.triColors.assign(m.TriangleCount(), stone);
        m.name = "Rock";
        m.ComputeSmoothNormals();
        return m;
    }
    /// A small shrub: a cluster of leafy spheres at ground level, ~0.7 tall.
    static Mesh Bush() {
        Mesh m; m.name = "Bush";
        Color leaf = Color::FromBytes(70, 128, 62);
        m.Add(Sphere(0.5f, 6, 8), {0.0f, 0.28f, 0.0f},   {0.9f, 0.8f, 0.9f}, &leaf);
        m.Add(Sphere(0.5f, 6, 8), {-0.28f, 0.22f, 0.1f}, {0.6f, 0.55f, 0.6f}, &leaf);
        m.Add(Sphere(0.5f, 6, 8), {0.26f, 0.24f, -0.12f},{0.62f, 0.6f, 0.62f}, &leaf);
        m.ComputeSmoothNormals();
        return m;
    }

    /// Recreate a primitive mesh from its name.
    static Mesh FromName(const std::string& n) {
        if (n == "Tree")      return Tree();
        if (n == "Pine")      return Pine();
        if (n == "Rock")      return Rock();
        if (n == "Bush")      return Bush();
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
    /// Edges worth drawing in a wireframe: every real edge EXCEPT the internal
    /// triangulation diagonals of flat quads. Two coplanar triangles that form a
    /// convex quad are treated as one face and their shared (diagonal) edge is
    /// hidden — so a cube reads as 6 quads and a subdivided face as a clean grid,
    /// instead of a mess of triangles. Boundary edges and creases (angled faces,
    /// e.g. a sphere) are always kept. Returned as pairs of vertex indices.
    /// `angleDeg` is the crease threshold; larger keeps fewer diagonals.
    std::vector<std::pair<int, int>> VisibleEdges(float angleDeg = 12.0f) const {
        std::vector<std::pair<int, int>> out;
        const int nTris = TriangleCount();
        if (nTris == 0) return out;
        // Weld positions to canonical ids so shared edges match across duplicates.
        std::map<std::tuple<int, int, int>, int> rep;
        std::vector<int> cid(vertices.size());
        std::vector<int> repVert;                            // canonical id -> a vertex index
        auto key = [](const Vec3& v) {
            return std::make_tuple((int)std::lround(v.x * 1024.0f),
                                   (int)std::lround(v.y * 1024.0f),
                                   (int)std::lround(v.z * 1024.0f));
        };
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            auto it = rep.find(key(vertices[i]));
            if (it == rep.end()) { int id = (int)repVert.size(); rep[key(vertices[i])] = id; repVert.push_back((int)i); cid[i] = id; }
            else cid[i] = it->second;
        }
        // Per-triangle normal + the canonical ids of its corners.
        struct Tri { int c[3]; Vec3 n; };
        std::vector<Tri> tri(nTris);
        for (int f = 0; f < nTris; ++f) {
            int a = triangles[f * 3], b = triangles[f * 3 + 1], c = triangles[f * 3 + 2];
            tri[f].c[0] = cid[a]; tri[f].c[1] = cid[b]; tri[f].c[2] = cid[c];
            Vec3 fn = Vec3::Cross(vertices[b] - vertices[a], vertices[c] - vertices[a]);
            float m = fn.Magnitude(); tri[f].n = m > 1e-8f ? fn * (1.0f / m) : Vec3{0, 0, 0};
        }
        // Edge -> the (up to two) triangles sharing it.
        std::map<std::pair<int, int>, std::vector<int>> edgeTris;
        auto ek = [](int a, int b) { return std::make_pair(std::min(a, b), std::max(a, b)); };
        for (int f = 0; f < nTris; ++f)
            for (int e = 0; e < 3; ++e)
                edgeTris[ek(tri[f].c[e], tri[f].c[(e + 1) % 3])].push_back(f);
        // Candidate diagonals: internal, coplanar, convex-quad edges — sorted by
        // descending length so a quad's true diagonal (longer) is hidden before its
        // grid-edge legs (shorter), and each triangle is consumed at most once.
        const float cosT = std::cos(angleDeg * 3.14159265358979323846f / 180.0f);
        auto pos = [&](int c) { return vertices[repVert[c]]; };
        struct Cand { std::pair<int, int> e; int t0, t1; float len; };
        std::vector<Cand> cands;
        for (auto& kv : edgeTris) {
            if (kv.second.size() != 2) continue;
            int t0 = kv.second[0], t1 = kv.second[1];
            float d = tri[t0].n.x * tri[t1].n.x + tri[t0].n.y * tri[t1].n.y + tri[t0].n.z * tri[t1].n.z;
            if (d < cosT) continue;                          // a crease — keep this edge
            float len = (pos(kv.first.first) - pos(kv.first.second)).Magnitude();
            cands.push_back({kv.first, t0, t1, len});
        }
        std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.len > b.len; });
        std::vector<char> used(nTris, 0);
        std::set<std::pair<int, int>> hidden;
        for (const Cand& c : cands)
            if (!used[c.t0] && !used[c.t1]) { used[c.t0] = used[c.t1] = 1; hidden.insert(c.e); }
        for (auto& kv : edgeTris)
            if (hidden.find(kv.first) == hidden.end())
                out.push_back({repVert[kv.first.first], repVert[kv.first.second]});
        return out;
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
    /// Load a Wavefront .OBJ. Parses positions + texture coords (UVs), splitting
    /// vertices per unique position/UV pair so textures map correctly. If
    /// `outTexture` is given, the referenced .mtl is read and its diffuse map
    /// (map_Kd) is returned (resolved next to the .obj) so an imported, textured
    /// model (e.g. a MakeHuman/Mixamo export) renders with its skin/clothing.
    static Mesh LoadOBJ(const std::string& path, bool* ok = nullptr, std::string* outTexture = nullptr) {
        Mesh m;
        std::ifstream f(path);
        if (!f) { if (ok) *ok = false; return m; }
        std::string dir;
        { std::size_t s = path.find_last_of("/\\"); if (s != std::string::npos) dir = path.substr(0, s + 1); }
        std::vector<Vec3> pos, nrm; std::vector<Vec2> uv;
        struct Corner { int p, t, n; };                        // 0-based pos / uv / normal (-1 = none)
        std::vector<std::vector<Corner>> faces;
        std::string mtllib, line;
        while (std::getline(f, line)) {
            std::istringstream ss(line); std::string tag; ss >> tag;
            if (tag == "v") { Vec3 v; ss >> v.x >> v.y >> v.z; pos.push_back(v); }
            else if (tag == "vt") { Vec2 t; ss >> t.x >> t.y; uv.push_back(t); }
            else if (tag == "vn") { Vec3 n; ss >> n.x >> n.y >> n.z; nrm.push_back(n); }
            else if (tag == "mtllib") { ss >> mtllib; }
            else if (tag == "f") {
                std::vector<Corner> face; std::string tok;
                while (ss >> tok) {
                    // Accept v, v/vt, v//vn and v/vt/vn (the OBJ corner forms).
                    int vi = 0, ti = 0, ni = 0;
                    if (std::sscanf(tok.c_str(), "%d/%d/%d", &vi, &ti, &ni) == 3) {}
                    else if (std::sscanf(tok.c_str(), "%d//%d", &vi, &ni) == 2) ti = 0;
                    else if (std::sscanf(tok.c_str(), "%d/%d", &vi, &ti) == 2) ni = 0;
                    else { std::sscanf(tok.c_str(), "%d", &vi); ti = ni = 0; }
                    if (vi < 0) vi = (int)pos.size() + vi + 1;
                    if (ti < 0) ti = (int)uv.size() + ti + 1;
                    if (ni < 0) ni = (int)nrm.size() + ni + 1;
                    face.push_back({vi - 1, ti - 1, ni - 1});
                }
                if (face.size() >= 3) faces.push_back(std::move(face));
            }
        }
        auto tri = [&](int a, int b, int cc) { m.triangles.insert(m.triangles.end(), {a, b, cc}); };
        if (uv.empty() && nrm.empty()) {               // simplest case: 1:1 with v lines
            m.vertices = pos;
            for (auto& fc : faces)
                for (std::size_t i = 2; i < fc.size(); ++i) tri(fc[0].p, fc[i - 1].p, fc[i].p);
        } else {                                       // de-index per (pos,uv,normal) so attributes map right
            const bool haveUV = !uv.empty(), haveN = !nrm.empty();
            std::map<std::tuple<int, int, int>, int> remap;
            auto corner = [&](const Corner& c) {
                auto key = std::make_tuple(c.p, c.t, c.n);
                auto it = remap.find(key); if (it != remap.end()) return it->second;
                int ni = (int)m.vertices.size();
                m.vertices.push_back((c.p >= 0 && c.p < (int)pos.size()) ? pos[c.p] : Vec3{0, 0, 0});
                if (haveUV) m.uvs.push_back((c.t >= 0 && c.t < (int)uv.size()) ? uv[c.t] : Vec2{0, 0});
                if (haveN)  m.normals.push_back((c.n >= 0 && c.n < (int)nrm.size()) ? nrm[c.n] : Vec3{0, 1, 0});
                remap[key] = ni; return ni;
            };
            for (auto& fc : faces) {
                std::vector<int> idx; for (auto& c : fc) idx.push_back(corner(c));
                for (std::size_t i = 2; i < idx.size(); ++i) tri(idx[0], idx[i - 1], idx[i]);
            }
        }
        if (outTexture && !mtllib.empty()) {                    // read .mtl for map_Kd
            std::ifstream mf(dir + mtllib); std::string ml;
            while (mf && std::getline(mf, ml)) {
                std::istringstream ms(ml); std::string mt; ms >> mt;
                if (mt == "map_Kd") { std::string tex; std::getline(ms, tex);
                    std::size_t a = tex.find_first_not_of(" \t");
                    if (a != std::string::npos) { tex = tex.substr(a);
                        *outTexture = (tex.find_first_of("/\\") == std::string::npos) ? dir + tex : tex; }
                    break;
                }
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
        RefreshNormals();
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
        RefreshNormals();
    }
    static Mesh Combined(const Mesh& a, const Mesh& b) { Mesh m = a; m.Combine(b); return m; }

    /// Mirror the mesh across an axis plane through the origin (0 = X / yz-plane,
    /// 1 = Y, 2 = Z), appending a flipped copy so the model becomes symmetric —
    /// model one half, mirror the rest. When `weld` is on, vertices on the mirror
    /// plane are merged so there's no seam down the middle.
    void Mirror(int axis = 0, bool weld = true) {
        if (axis < 0 || axis > 2 || vertices.empty()) return;
        const bool faceCols = HasFaceColors();
        const bool hadNormals = HasNormals();
        Mesh copy = *this;
        for (Vec3& v : copy.vertices) (&v.x)[axis] = -(&v.x)[axis];   // reflect
        copy.FlipWinding();                                          // keep faces outward
        int base = (int)vertices.size();
        vertices.insert(vertices.end(), copy.vertices.begin(), copy.vertices.end());
        for (int t : copy.triangles) triangles.push_back(t + base);
        if (faceCols && copy.HasFaceColors())
            triColors.insert(triColors.end(), copy.triColors.begin(), copy.triColors.end());
        normals.clear();
        name = "";
        if (weld) WeldVertices();
        if (hadNormals) ComputeSmoothNormals();      // preserve smooth shading through the mirror
    }
    Mesh Mirrored(int axis = 0, bool weld = true) const { Mesh m = *this; m.Mirror(axis, weld); return m; }

    /// Push every vertex toward a sphere centred on the mesh, by `amount` in [0,1]
    /// (0 = unchanged, 1 = fully spherical) — round off a cube into a ball, or just
    /// soften a blocky shape. The sphere radius is the average vertex distance.
    void Spherify(float amount = 1.0f) {
        if (vertices.empty()) return;
        Vec3 lo, hi; Bounds(lo, hi);
        Vec3 c{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
        float rsum = 0.0f;
        for (const Vec3& v : vertices) {
            Vec3 d{v.x - c.x, v.y - c.y, v.z - c.z};
            rsum += std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        }
        float r = rsum / (float)vertices.size();
        for (Vec3& v : vertices) {
            Vec3 d{v.x - c.x, v.y - c.y, v.z - c.z};
            float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            if (len < 1e-6f) continue;
            Vec3 onSphere{c.x + d.x / len * r, c.y + d.y / len * r, c.z + d.z / len * r};
            v.x += (onSphere.x - v.x) * amount;
            v.y += (onSphere.y - v.y) * amount;
            v.z += (onSphere.z - v.z) * amount;
        }
        name = "";
        RefreshNormals();                            // rounding a shape re-smooths its lighting
    }

    /// Twist the mesh around an axis: each vertex rotates about that axis by
    /// `degreesPerUnit` × (its distance along the axis from the mesh centre) — the
    /// classic "wring the model" deformer. axis: 0 = X, 1 = Y, 2 = Z.
    void Twist(float degreesPerUnit, int axis = 1) {
        if (vertices.empty() || axis < 0 || axis > 2) return;
        Vec3 lo, hi; Bounds(lo, hi);
        float mid = ((&lo.x)[axis] + (&hi.x)[axis]) * 0.5f;
        const float deg2rad = 3.14159265358979323846f / 180.0f;
        int a = (axis + 1) % 3, b = (axis + 2) % 3;   // the two perpendicular axes
        for (Vec3& v : vertices) {
            float along = (&v.x)[axis] - mid;
            float ang = degreesPerUnit * along * deg2rad;
            float ca = std::cos(ang), sa = std::sin(ang);
            float pa = (&v.x)[a], pb = (&v.x)[b];
            (&v.x)[a] = pa * ca - pb * sa;
            (&v.x)[b] = pa * sa + pb * ca;
        }
        name = "";
        RefreshNormals();
    }

    /// Taper along an axis: scale the two perpendicular axes from full size at the
    /// bottom (min of `axis`) to `endScale` at the top — turn a cylinder into a cone
    /// or a box into a frustum/wedge. axis: 0 = X, 1 = Y, 2 = Z.
    void Taper(int axis = 1, float endScale = 0.5f) {
        if (vertices.empty() || axis < 0 || axis > 2) return;
        Vec3 lo, hi; Bounds(lo, hi);
        float a0 = (&lo.x)[axis], a1 = (&hi.x)[axis], range = a1 - a0;
        if (range < 1e-6f) return;
        Vec3 c{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
        int b = (axis + 1) % 3, d = (axis + 2) % 3;
        for (Vec3& v : vertices) {
            float t = ((&v.x)[axis] - a0) / range;       // 0 at bottom .. 1 at top
            float s = 1.0f + (endScale - 1.0f) * t;
            (&v.x)[b] = (&c.x)[b] + ((&v.x)[b] - (&c.x)[b]) * s;
            (&v.x)[d] = (&c.x)[d] + ((&v.x)[d] - (&c.x)[d]) * s;
        }
        name = "";
        RefreshNormals();
    }

    /// Bend the mesh into an arc: sweep it around a circle so a straight bar curves by
    /// `degrees` total over its length along `axis` (0 = X, 1 = Y, 2 = Z). The bend
    /// happens in the plane of `axis` and the next axis. 0 leaves it straight.
    void Bend(float degrees, int axis = 0) {
        if (vertices.empty() || axis < 0 || axis > 2 || std::fabs(degrees) < 1e-4f) return;
        Vec3 lo, hi; Bounds(lo, hi);
        float a0 = (&lo.x)[axis], a1 = (&hi.x)[axis], len = a1 - a0;
        if (len < 1e-6f) return;
        const float kPi = 3.14159265358979323846f;
        float total = degrees * kPi / 180.0f;            // total bend angle
        float radius = len / total;                       // arc radius so the ends meet the angle
        float mid = (a0 + a1) * 0.5f;
        int up = (axis + 1) % 3;                          // the axis we curl toward
        for (Vec3& v : vertices) {
            float s = (&v.x)[axis] - mid;                 // distance along the bar from centre
            float h = (&v.x)[up];                         // offset from the neutral fibre
            float ang = s / radius;
            float r = radius + h;                         // outer fibres travel a larger arc
            (&v.x)[axis] = std::sin(ang) * r;
            (&v.x)[up]   = radius - std::cos(ang) * r;
        }
        normals.clear();
        name = "";
    }

    /// Give a thin surface real thickness: duplicate it, push the copy inward along
    /// the (smooth) vertex normals by `thickness`, and flip it so the shell reads
    /// solid from both sides — turn a plane/curved sheet into a slab/bowl wall.
    void Solidify(float thickness = 0.1f) {
        if (vertices.empty() || triangles.empty()) return;
        Mesh src = *this;
        if (!src.HasNormals()) src.ComputeSmoothNormals();
        Mesh inner = src;
        for (std::size_t i = 0; i < inner.vertices.size(); ++i) {
            const Vec3& n = src.normals[i];
            inner.vertices[i].x -= n.x * thickness;
            inner.vertices[i].y -= n.y * thickness;
            inner.vertices[i].z -= n.z * thickness;
        }
        inner.FlipWinding();
        std::vector<Color> innerCols = HasFaceColors() ? triColors : std::vector<Color>{};   // copy before growing
        int base = (int)vertices.size();
        vertices.insert(vertices.end(), inner.vertices.begin(), inner.vertices.end());
        for (int t : inner.triangles) triangles.push_back(t + base);
        if (!innerCols.empty()) triColors.insert(triColors.end(), innerCols.begin(), innerCols.end());
        normals.clear();
        name = "";
    }

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
        RefreshNormals();                            // keep lighting valid at the new resolution
    }

    /// Push every vertex onto a sphere of the given radius about the origin —
    /// pair with Subdivide() to turn a cube/icosphere into a smooth ball.
    void ProjectToSphere(float radius = 0.5f) {
        for (Vec3& v : vertices) {
            float m = v.Magnitude();
            if (m > 1e-6f) v = v * (radius / m);
        }
        name = "";
        RefreshNormals();
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
        m.ComputeSmoothNormals();   // smooth capsule surface
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

    /// Concatenate another mesh wholesale (vertices, rebased triangles, face
    /// colors) — used to merge pre-built parts into one mesh.
    void Append(const Mesh& src) {
        int base = (int)vertices.size();
        vertices.insert(vertices.end(), src.vertices.begin(), src.vertices.end());
        for (int t : src.triangles) triangles.push_back(t + base);
        triColors.insert(triColors.end(), src.triColors.begin(), src.triColors.end());
    }

    /// Laplacian relaxation: move each vertex toward (positive `amount`) or away
    /// from (negative) the average of its edge-connected neighbours. Coincident
    /// vertices are treated as one so seams between welded parts/faces round too.
    void Smooth(float amount = 0.5f) {
        if (amount == 0.0f || vertices.empty()) return;
        const std::size_t n = vertices.size();
        // Group coincident vertices (cube faces, joined parts) so smoothing
        // propagates across seams instead of leaving hard creases.
        std::map<std::tuple<int, int, int>, int> grid;
        std::vector<int> rep(n);
        auto key = [](const Vec3& v) {
            return std::make_tuple((int)std::lround(v.x * 2048.0f),
                                   (int)std::lround(v.y * 2048.0f),
                                   (int)std::lround(v.z * 2048.0f));
        };
        for (std::size_t i = 0; i < n; ++i) {
            auto k = key(vertices[i]);
            auto it = grid.find(k);
            if (it == grid.end()) { grid[k] = (int)i; rep[i] = (int)i; }
            else rep[i] = it->second;
        }
        std::vector<Vec3> sum(n, Vec3{0, 0, 0});
        std::vector<int>  cnt(n, 0);
        auto link = [&](int a, int b) { int ra = rep[a], rb = rep[b];
            sum[ra] = sum[ra] + vertices[rb]; ++cnt[ra]; };
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            int i0 = triangles[t], i1 = triangles[t + 1], i2 = triangles[t + 2];
            link(i0, i1); link(i1, i0); link(i1, i2); link(i2, i1); link(i2, i0); link(i0, i2);
        }
        std::vector<Vec3> moved(n);
        for (std::size_t i = 0; i < n; ++i) {
            int r = rep[i];
            moved[i] = (cnt[r] > 0)
                ? vertices[i] * (1.0f - amount) + sum[r] * (amount / cnt[r])
                : vertices[i];
        }
        vertices = std::move(moved);
        RefreshNormals();
    }

    /// Take low-poly geometry up to smooth high-poly: `iterations` rounds of
    /// Subdivide() + Taubin smoothing (a shrink pass then an inflate pass) so the
    /// result rounds out WITHOUT collapsing/shrinking like plain Laplacian.
    void SubdivideSmooth(int iterations = 1, float amount = 0.5f) {
        for (int i = 0; i < iterations && !triangles.empty(); ++i) {
            Subdivide();
            Smooth(amount);                       // shrink toward neighbours
            Smooth(-(amount * 1.05f + 0.02f));    // inflate back (Taubin) — keeps volume
        }
    }

    // ---- Interactive editing (Blender-like mesh ops) -------------------
    // These operate on the triangle-indexed mesh, mark it custom (clear `name`)
    // so edited geometry is serialized verbatim rather than regenerated from a
    // primitive name, and recompute normals where it matters.

    /// Normalized geometric normal of triangle `f` (cross of its two edges).
    /// Returns the zero vector for a degenerate (collinear) triangle.
    Vec3 FaceNormal(int f) const {
        int i = f * 3;
        if (i < 0 || i + 2 >= (int)triangles.size()) return Vec3{0, 0, 0};
        int a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
        Vec3 n = Vec3::Cross(vertices[b] - vertices[a], vertices[c] - vertices[a]);
        float m = n.Magnitude();
        return m > 1e-8f ? n * (1.0f / m) : Vec3{0, 0, 0};
    }

    /// Centroid (average of the three corners) of triangle `f`.
    Vec3 FaceCenter(int f) const {
        int i = f * 3;
        if (i < 0 || i + 2 >= (int)triangles.size()) return Vec3{0, 0, 0};
        return (vertices[triangles[i]] + vertices[triangles[i + 1]] + vertices[triangles[i + 2]])
               * (1.0f / 3.0f);
    }

    /// Add `delta` to each listed vertex position (the move/translate gizmo).
    /// The caller resolves welded selections into the index list it passes.
    void MoveVertices(const std::vector<int>& verts, const Vec3& delta) {
        for (int v : verts)
            if (v >= 0 && v < (int)vertices.size()) vertices[v] += delta;
        name = "";
        RefreshNormals();
    }

    /// Region-extrude the selected set of triangles along their averaged normal:
    /// the selected faces are detached and pushed out by `dist`, and the boundary
    /// of the region is bridged with side walls so the cap stays connected. The
    /// passed `faces` indices stay valid (those slots are re-pointed, not removed),
    /// so the caller's selection still refers to the extruded cap.
    void ExtrudeFaces(const std::vector<int>& faces, float dist) {
        if (faces.empty()) return;
        // Averaged (area-weighted) normal of the selected faces.
        Vec3 N{0, 0, 0};
        for (int f : faces) {
            int i = f * 3;
            if (i < 0 || i + 2 >= (int)triangles.size()) continue;
            int a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
            N += Vec3::Cross(vertices[b] - vertices[a], vertices[c] - vertices[a]); // area-weighted
        }
        float nm = N.Magnitude();
        N = nm > 1e-8f ? N * (1.0f / nm) : Vec3{0, 0, 0};
        // Boundary edges: undirected edges used by exactly ONE selected triangle.
        std::map<std::pair<int, int>, int> edgeCount;
        auto ekey = [](int a, int b) { return std::make_pair(std::min(a, b), std::max(a, b)); };
        for (int f : faces) {
            int i = f * 3;
            if (i < 0 || i + 2 >= (int)triangles.size()) continue;
            int a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
            auto k1 = ekey(a, b); ++edgeCount[{k1.first, k1.second}];
            auto k2 = ekey(b, c); ++edgeCount[{k2.first, k2.second}];
            auto k3 = ekey(c, a); ++edgeCount[{k3.first, k3.second}];
        }
        // Duplicate every vertex used by the selected region, pushed out by N*dist.
        std::map<int, int> newIndex;
        for (int f : faces) {
            int i = f * 3;
            if (i < 0 || i + 2 >= (int)triangles.size()) continue;
            for (int k = 0; k < 3; ++k) {
                int v = triangles[i + k];
                if (newIndex.find(v) == newIndex.end()) {
                    newIndex[v] = (int)vertices.size();
                    vertices.push_back(vertices[v] + N * dist);
                }
            }
        }
        // Re-point the selected triangles to the duplicated (moved-out) vertices,
        // and bridge each boundary edge — oriented as it appears in its triangle so
        // the side-wall winding stays outward-consistent.
        for (int f : faces) {
            int i = f * 3;
            if (i < 0 || i + 2 >= (int)triangles.size()) continue;
            int v0 = triangles[i], v1 = triangles[i + 1], v2 = triangles[i + 2];
            int e[3][2] = {{v0, v1}, {v1, v2}, {v2, v0}};
            for (auto& pr : e) {
                auto k = ekey(pr[0], pr[1]);
                if (edgeCount[{k.first, k.second}] == 1) {       // boundary edge a->b
                    int a = pr[0], b = pr[1];
                    int na = newIndex[a], nb = newIndex[b];
                    triangles.insert(triangles.end(), {a, b, nb}); // side wall (two tris)
                    triangles.insert(triangles.end(), {a, nb, na});
                }
            }
            triangles[i] = newIndex[v0]; triangles[i + 1] = newIndex[v1]; triangles[i + 2] = newIndex[v2];
        }
        name = "";
        RefreshNormals();     // keep the mesh's shading mode (flat stays flat → hard extrude edges)
    }

    /// Inset the selected region: duplicate its vertices, pulled toward the region
    /// centroid by `amount` (a [0,1] fraction of each vertex's distance to centre),
    /// re-point the selected faces to the inner ring, and bridge the boundary so a
    /// smaller inner cap sits inside the original outline.
    void InsetFaces(const std::vector<int>& faces, float amount) {
        if (faces.empty()) return;
        float t = amount < 0.0f ? 0.0f : (amount > 1.0f ? 1.0f : amount);
        // Boundary edges (used by exactly one selected triangle).
        std::map<std::pair<int, int>, int> edgeCount;
        auto ekey = [](int a, int b) { return std::make_pair(std::min(a, b), std::max(a, b)); };
        for (int f : faces) {
            int i = f * 3;
            if (i < 0 || i + 2 >= (int)triangles.size()) continue;
            int a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
            auto k1 = ekey(a, b); ++edgeCount[{k1.first, k1.second}];
            auto k2 = ekey(b, c); ++edgeCount[{k2.first, k2.second}];
            auto k3 = ekey(c, a); ++edgeCount[{k3.first, k3.second}];
        }
        // Centroid of all vertices used by the selected faces.
        std::map<int, int> newIndex;
        Vec3 C{0, 0, 0}; int count = 0;
        for (int f : faces) {
            int i = f * 3;
            if (i < 0 || i + 2 >= (int)triangles.size()) continue;
            for (int k = 0; k < 3; ++k) {
                int v = triangles[i + k];
                if (newIndex.find(v) == newIndex.end()) { newIndex[v] = -1; C += vertices[v]; ++count; }
            }
        }
        if (count > 0) C = C * (1.0f / count);
        // Duplicate each region vertex, lerped toward the centroid by `t`.
        for (auto& kv : newIndex) {
            int v = kv.first;
            kv.second = (int)vertices.size();
            vertices.push_back(vertices[v] + (C - vertices[v]) * t);
        }
        // Re-point selected faces to the inner ring and bridge boundary edges.
        for (int f : faces) {
            int i = f * 3;
            if (i < 0 || i + 2 >= (int)triangles.size()) continue;
            int v0 = triangles[i], v1 = triangles[i + 1], v2 = triangles[i + 2];
            int e[3][2] = {{v0, v1}, {v1, v2}, {v2, v0}};
            for (auto& pr : e) {
                auto k = ekey(pr[0], pr[1]);
                if (edgeCount[{k.first, k.second}] == 1) {
                    int a = pr[0], b = pr[1];
                    int na = newIndex[a], nb = newIndex[b];
                    triangles.insert(triangles.end(), {a, b, nb});
                    triangles.insert(triangles.end(), {a, nb, na});
                }
            }
            triangles[i] = newIndex[v0]; triangles[i + 1] = newIndex[v1]; triangles[i + 2] = newIndex[v2];
        }
        name = "";
        RefreshNormals();     // keep the mesh's shading mode (flat stays flat → hard extrude edges)
    }

    /// 1->4 midpoint subdivision of ONLY the listed triangles. Midpoints are
    /// shared between the selected faces (via an edge->midpoint map) so the region
    /// stays welded; unselected triangles are left untouched.
    void SubdivideFaces(const std::vector<int>& faces) {
        if (faces.empty()) return;
        std::map<int, bool> sel;
        for (int f : faces) sel[f] = true;
        std::map<std::pair<int, int>, int> mids;       // edge -> new midpoint index
        auto midpoint = [&](int a, int b) {
            auto key = std::minmax(a, b);
            auto it = mids.find({key.first, key.second});
            if (it != mids.end()) return it->second;
            int idx = (int)vertices.size();
            vertices.push_back((vertices[a] + vertices[b]) * 0.5f);
            mids[{key.first, key.second}] = idx;
            return idx;
        };
        std::vector<int> out;
        for (int face = 0, n = TriangleCount(); face < n; ++face) {
            int i = face * 3;
            int a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
            if (sel.find(face) == sel.end()) {          // untouched
                out.insert(out.end(), {a, b, c});
            } else {                                    // split into 4
                int ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
                out.insert(out.end(), {a, ab, ca,  ab, b, bc,  ca, bc, c,  ab, bc, ca});
            }
        }
        triangles = std::move(out);
        name = "";
        RefreshNormals();
    }

    /// Delete the listed triangles (erasing their 3 indices each). Sorted
    /// descending so earlier erases don't shift later indices. Orphan vertices are
    /// left in place (harmless).
    void DeleteFaces(std::vector<int> faces) {
        std::sort(faces.begin(), faces.end(), std::greater<int>());
        for (int f : faces) {
            int i = f * 3;
            if (i < 0 || i + 2 >= (int)triangles.size()) continue;
            triangles.erase(triangles.begin() + i, triangles.begin() + i + 3);
        }
        name = "";
    }

    /// Reverse the winding of every triangle (swap the 2nd and 3rd index) so all
    /// faces point the other way, then recompute normals.
    void FlipNormals() {
        for (std::size_t i = 0; i + 2 < triangles.size(); i += 3)
            std::swap(triangles[i + 1], triangles[i + 2]);
        name = "";
        ComputeSmoothNormals();
    }

    /// A sculpting brush in LOCAL mesh space. Vertices within `radius` of `center`
    /// are displaced with a smoothstep falloff `w`. mode: 0 = GRAB (push along
    /// `dir`), 1 = INFLATE (push along each vertex's own normal), 2 = SMOOTH (pull
    /// toward the local average of nearby vertices). SMOOTH reads a snapshot so the
    /// relaxation doesn't feed back within one call.
    void SculptBrush(const Vec3& center, const Vec3& dir, float radius, float strength, int mode) {
        if (radius <= 1e-6f || vertices.empty()) return;
        std::vector<Vec3> vn;
        if (mode == 1) vn = Normals();                 // per-vertex normals for INFLATE
        std::vector<Vec3> snapshot = vertices;         // SMOOTH reads positions pre-edit
        float inv = 1.0f / radius;
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            float d = (snapshot[i] - center).Magnitude();
            if (d >= radius) continue;
            float w = 1.0f - d * inv;                   // linear falloff in [0,1]
            if (w < 0.0f) w = 0.0f;
            w = w * w * (3.0f - 2.0f * w);              // smoothstep for a nicer curve
            if (mode == 1) {                            // INFLATE: along vertex normal
                vertices[i] += vn[i] * (strength * w);
            } else if (mode == 2) {                     // SMOOTH: toward local average
                Vec3 avg{0, 0, 0}; int cnt = 0;
                for (std::size_t j = 0; j < snapshot.size(); ++j)
                    if ((snapshot[j] - center).Magnitude() < radius) { avg += snapshot[j]; ++cnt; }
                if (cnt > 0) { avg = avg * (1.0f / cnt);
                    vertices[i] += (avg - snapshot[i]) * (strength * w); }
            } else {                                    // GRAB: along `dir`
                vertices[i] += dir * (strength * w);
            }
        }
        name = "";
        ComputeSmoothNormals();
    }

    // ---- Modifiers (Blender-style) ------------------------------------------

    /// Array modifier: keep the original and append `count-1` more copies, each
    /// shifted a further `offset` along. Great for fences, columns, stairs, gears.
    void Array(int count, const Vec3& offset) {
        if (count <= 1 || vertices.empty()) return;
        Mesh base = *this;
        for (int i = 1; i < count; ++i)
            Add(base, {offset.x * (float)i, offset.y * (float)i, offset.z * (float)i});
        name = "";
        ComputeSmoothNormals();
    }

    /// Decimate modifier (vertex clustering): snap vertices to a grid of `cellSize`
    /// and collapse each cell to one averaged vertex, dropping faces that degenerate.
    /// A fast, robust way to cut triangle count on dense/imported meshes. Returns the
    /// new triangle count.
    int Decimate(float cellSize) {
        if (cellSize <= 1e-6f || vertices.empty()) return TriangleCount();
        std::map<std::tuple<int, int, int>, int> cell;
        std::vector<Vec3> sum; std::vector<int> cnt; std::vector<int> remap(vertices.size());
        auto key = [&](const Vec3& v) {
            return std::make_tuple((int)std::floor(v.x / cellSize),
                                   (int)std::floor(v.y / cellSize),
                                   (int)std::floor(v.z / cellSize));
        };
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            auto k = key(vertices[i]);
            auto it = cell.find(k);
            int id;
            if (it == cell.end()) { id = (int)sum.size(); cell[k] = id; sum.push_back(vertices[i]); cnt.push_back(1); }
            else { id = it->second; sum[id] += vertices[i]; ++cnt[id]; }
            remap[i] = id;
        }
        std::vector<Vec3> nv(sum.size());
        for (std::size_t i = 0; i < sum.size(); ++i) nv[i] = sum[i] * (1.0f / (float)cnt[i]);
        std::vector<int> nt; nt.reserve(triangles.size());
        std::vector<Color> nc; const bool hadColors = HasFaceColors();
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            int a = remap[triangles[t]], b = remap[triangles[t + 1]], c = remap[triangles[t + 2]];
            if (a == b || b == c || a == c) continue;            // collapsed → drop
            nt.push_back(a); nt.push_back(b); nt.push_back(c);
            if (hadColors) nc.push_back(triColors[t / 3]);
        }
        vertices = std::move(nv); triangles = std::move(nt);
        triColors = std::move(nc); uvs.clear();
        name = "";
        ComputeSmoothNormals();
        return TriangleCount();
    }

    /// True if `p` is inside this CLOSED mesh — parity of how many faces a ray from
    /// `p` crosses (odd = inside). The basis for the boolean/remesh ops. The ray is
    /// slightly skewed off the axes so it never threads an exact shared edge/vertex
    /// (which would double-count and flip the answer).
    bool PointInside(const Vec3& p) const {
        const Vec3 d{1.0f, 0.0009124f, 0.0007215f};              // near +X, off every edge
        auto dot = [](const Vec3& u, const Vec3& w) { return u.x * w.x + u.y * w.y + u.z * w.z; };
        int crossings = 0;
        for (std::size_t i = 0; i + 2 < triangles.size(); i += 3) {
            const Vec3& a = vertices[triangles[i]];
            const Vec3& b = vertices[triangles[i + 1]];
            const Vec3& c = vertices[triangles[i + 2]];
            Vec3 e1 = b - a, e2 = c - a;
            Vec3 h = Vec3::Cross(d, e2);
            float det = dot(e1, h);
            if (det > -1e-9f && det < 1e-9f) continue;           // ray parallel to face
            float invDet = 1.0f / det;
            Vec3 s = p - a;
            float u = dot(s, h) * invDet;
            if (u < 0.0f || u > 1.0f) continue;
            Vec3 q = Vec3::Cross(s, e1);
            float v = dot(d, q) * invDet;
            if (v < 0.0f || u + v > 1.0f) continue;
            float t = dot(e2, q) * invDet;
            if (t > 1e-7f) ++crossings;                          // crossing ahead along the ray
        }
        return (crossings & 1) != 0;
    }

    enum class BoolOp { Union, Difference, Intersect };

    /// Remesh modifier ("Blocks" mode): sample the solid on a voxel grid and emit a
    /// watertight blocky surface between solid and empty cells. Follow with Smooth()
    /// or SubdivideSmooth() to round it. `voxel` is the cell size; the grid is capped
    /// at `maxDim` cells per axis so a tiny voxel on a big mesh can't explode.
    static Mesh VoxelRemesh(const Mesh& src, float voxel, int maxDim = 64) {
        if (src.vertices.empty() || voxel <= 1e-5f) return Mesh{};
        Vec3 lo, hi; src.Bounds(lo, hi);
        lo = lo - Vec3{voxel, voxel, voxel};
        hi = hi + Vec3{voxel, voxel, voxel};
        int nx, ny, nz; float v;
        GridDims(lo, hi, voxel, maxDim, nx, ny, nz, v);
        std::vector<unsigned char> occ((std::size_t)nx * ny * nz, 0);
        SampleInto(src, occ, nx, ny, nz, lo, v);
        return EmitOccupancy(occ, nx, ny, nz, lo, v);
    }
    /// Replace this mesh with its voxel remesh in place.
    void Remesh(float voxel, int maxDim = 64) { *this = VoxelRemesh(*this, voxel, maxDim); }

    /// Boolean modifier: combine two closed meshes with union (A∪B), difference
    /// (A−B), or intersect (A∩B), via a shared voxel grid. Robust on any closed
    /// input (no fragile coplanar-triangle clipping); the result is blocky at the
    /// voxel scale, so pick `voxel` small for crisp seams and Smooth() to taste.
    static Mesh Boolean(const Mesh& a, const Mesh& b, BoolOp op, float voxel, int maxDim = 96) {
        if (a.vertices.empty() || b.vertices.empty() || voxel <= 1e-5f) return Mesh{};
        Vec3 alo, ahi, blo, bhi; a.Bounds(alo, ahi); b.Bounds(blo, bhi);
        // Union of both AABBs (covers every cell either solid could occupy), padded.
        Vec3 lo{std::min(alo.x, blo.x), std::min(alo.y, blo.y), std::min(alo.z, blo.z)};
        Vec3 hi{std::max(ahi.x, bhi.x), std::max(ahi.y, bhi.y), std::max(ahi.z, bhi.z)};
        lo = lo - Vec3{voxel, voxel, voxel};
        hi = hi + Vec3{voxel, voxel, voxel};
        int nx, ny, nz; float v;
        GridDims(lo, hi, voxel, maxDim, nx, ny, nz, v);
        const std::size_t n = (std::size_t)nx * ny * nz;
        std::vector<unsigned char> oa(n, 0), ob(n, 0);
        SampleInto(a, oa, nx, ny, nz, lo, v);
        SampleInto(b, ob, nx, ny, nz, lo, v);
        std::vector<unsigned char> occ(n, 0);
        for (std::size_t i = 0; i < n; ++i) {
            bool ia = oa[i] != 0, ib = ob[i] != 0;
            occ[i] = (op == BoolOp::Union ? (ia || ib)
                    : op == BoolOp::Intersect ? (ia && ib)
                    : (ia && !ib)) ? 1 : 0;                      // Difference
        }
        return EmitOccupancy(occ, nx, ny, nz, lo, v);
    }

    /// Convex Hull: the tight convex shell around a set of points (incremental
    /// algorithm). Great for turning a scan/blockout into a clean collider or a
    /// crystal/rock silhouette. Returns an empty mesh if the points are degenerate
    /// (fewer than 4, or all coplanar).
    static Mesh ConvexHull(const std::vector<Vec3>& ptsIn) {
        Mesh out;
        // De-duplicate coincident points.
        std::vector<Vec3> p;
        p.reserve(ptsIn.size());
        for (const Vec3& q : ptsIn) {
            bool dup = false;
            for (const Vec3& e : p) if ((e - q).Magnitude() < 1e-6f) { dup = true; break; }
            if (!dup) p.push_back(q);
        }
        if (p.size() < 4) return out;
        auto dot = [](const Vec3& u, const Vec3& w) { return u.x * w.x + u.y * w.y + u.z * w.z; };

        // Seed tetrahedron: spread-out, non-degenerate 4 points.
        int i0 = 0, i1 = 1;
        float best = -1.0f;
        for (std::size_t i = 0; i < p.size(); ++i)
            for (std::size_t j = i + 1; j < p.size(); ++j) {
                float d = (p[i] - p[j]).Magnitude();
                if (d > best) { best = d; i0 = (int)i; i1 = (int)j; }
            }
        int i2 = -1; best = -1.0f;
        for (std::size_t i = 0; i < p.size(); ++i) {
            Vec3 c = Vec3::Cross(p[i1] - p[i0], p[i] - p[i0]);
            float d = c.Magnitude();
            if (d > best) { best = d; i2 = (int)i; }
        }
        if (i2 < 0 || best < 1e-9f) return out;                 // all collinear
        Vec3 nrm = Vec3::Cross(p[i1] - p[i0], p[i2] - p[i0]);
        int i3 = -1; best = 1e-9f;
        for (std::size_t i = 0; i < p.size(); ++i) {
            float d = std::fabs(dot(nrm, p[i] - p[i0]));
            if (d > best) { best = d; i3 = (int)i; }
        }
        if (i3 < 0) return out;                                  // all coplanar

        Vec3 interior = (p[i0] + p[i1] + p[i2] + p[i3]) * 0.25f; // always inside the hull
        struct Face { int a, b, c; };
        std::vector<Face> faces;
        auto faceNormal = [&](const Face& f) { return Vec3::Cross(p[f.b] - p[f.a], p[f.c] - p[f.a]); };
        auto addFace = [&](int a, int b, int c) {
            Face f{a, b, c};
            // Wind so the normal points away from the interior point.
            if (dot(faceNormal(f), p[a] - interior) < 0.0f) std::swap(f.b, f.c);
            faces.push_back(f);
        };
        addFace(i0, i1, i2); addFace(i0, i1, i3); addFace(i0, i2, i3); addFace(i1, i2, i3);

        for (int q = 0; q < (int)p.size(); ++q) {
            if (q == i0 || q == i1 || q == i2 || q == i3) continue;
            // Faces the new point can "see" (it's outside their plane).
            std::vector<char> vis(faces.size(), 0);
            bool any = false;
            for (std::size_t f = 0; f < faces.size(); ++f) {
                Vec3 n = faceNormal(faces[f]);
                if (dot(n, p[q] - p[faces[f].a]) > 1e-9f) { vis[f] = 1; any = true; }
            }
            if (!any) continue;                                 // inside the current hull
            // Horizon = directed edges of visible faces whose reverse isn't also visible.
            std::map<std::pair<int, int>, int> dir;             // directed edge -> count among visible
            auto add = [&](int a, int b) { dir[{a, b}]++; };
            for (std::size_t f = 0; f < faces.size(); ++f)
                if (vis[f]) { add(faces[f].a, faces[f].b); add(faces[f].b, faces[f].c); add(faces[f].c, faces[f].a); }
            std::vector<std::pair<int, int>> horizon;
            for (auto& kv : dir)
                if (dir.find({kv.first.second, kv.first.first}) == dir.end())
                    horizon.push_back(kv.first);
            // Drop visible faces, add a fan from the horizon to q.
            std::vector<Face> keep;
            for (std::size_t f = 0; f < faces.size(); ++f) if (!vis[f]) keep.push_back(faces[f]);
            faces.swap(keep);
            for (auto& e : horizon) addFace(e.first, e.second, q);
        }

        // Emit unique vertices used by the hull faces.
        std::map<int, int> remap;
        for (const Face& f : faces)
            for (int idx : {f.a, f.b, f.c})
                if (remap.find(idx) == remap.end()) { remap[idx] = (int)out.vertices.size(); out.vertices.push_back(p[idx]); }
        for (const Face& f : faces) {
            out.triangles.push_back(remap[f.a]); out.triangles.push_back(remap[f.b]); out.triangles.push_back(remap[f.c]);
        }
        out.ComputeSmoothNormals();
        return out;
    }
    /// Replace this mesh with the convex hull of its own vertices.
    void MakeConvexHull() { *this = ConvexHull(vertices); }

    /// Bisect: cut the mesh with a plane (point `planeP`, normal `planeN`) and keep
    /// the half on the normal's side (flip `keepPositive` for the other). With `cap`,
    /// the exposed cross-section is filled so the result stays a closed solid. This is
    /// Blender's Bisect tool — slice a model cleanly in half, lop off a top, etc.
    void Bisect(const Vec3& planeP, const Vec3& planeN, bool cap = true, bool keepPositive = true) {
        Vec3 N = planeN; float nl = N.Magnitude();
        if (nl < 1e-9f) return;
        N = N * (1.0f / nl);
        const float sign = keepPositive ? 1.0f : -1.0f;
        auto sd = [&](const Vec3& v) { return sign * ((v.x - planeP.x) * N.x + (v.y - planeP.y) * N.y + (v.z - planeP.z) * N.z); };
        std::vector<Vec3> nv; std::vector<int> nt;
        std::vector<Vec3> cut;                                   // intersection points (for the cap)
        auto emit = [&](const Vec3& a, const Vec3& b, const Vec3& c) {
            int base = (int)nv.size(); nv.push_back(a); nv.push_back(b); nv.push_back(c);
            nt.push_back(base); nt.push_back(base + 1); nt.push_back(base + 2);
        };
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            Vec3 v[3] = {vertices[triangles[t]], vertices[triangles[t + 1]], vertices[triangles[t + 2]]};
            float d[3] = {sd(v[0]), sd(v[1]), sd(v[2])};
            // Clip the triangle to the kept half-space (Sutherland–Hodgman).
            std::vector<Vec3> poly;
            for (int i = 0; i < 3; ++i) {
                int j = (i + 1) % 3;
                if (d[i] >= 0.0f) poly.push_back(v[i]);
                if ((d[i] >= 0.0f) != (d[j] >= 0.0f)) {          // edge crosses the plane
                    float tt = d[i] / (d[i] - d[j]);
                    Vec3 x = v[i] + (v[j] - v[i]) * tt;
                    poly.push_back(x);
                    cut.push_back(x);
                }
            }
            for (std::size_t k = 1; k + 1 < poly.size(); ++k) emit(poly[0], poly[k], poly[k + 1]);
        }
        // Cap the cross-section: fan-triangulate the intersection ring in the plane.
        if (cap && cut.size() >= 3) {
            Vec3 ctr{0, 0, 0}; for (const Vec3& c : cut) ctr += c; ctr = ctr * (1.0f / (float)cut.size());
            // Plane basis for angular sort.
            Vec3 up = std::fabs(N.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
            Vec3 e0 = Vec3::Cross(up, N); e0 = e0 * (1.0f / std::max(1e-6f, e0.Magnitude()));
            Vec3 e1 = Vec3::Cross(N, e0);
            auto ang = [&](const Vec3& c) { Vec3 dd = c - ctr; return std::atan2(dd.x * e1.x + dd.y * e1.y + dd.z * e1.z,
                                                                                 dd.x * e0.x + dd.y * e0.y + dd.z * e0.z); };
            std::sort(cut.begin(), cut.end(), [&](const Vec3& a, const Vec3& b) { return ang(a) < ang(b); });
            for (std::size_t k = 0; k < cut.size(); ++k) {
                const Vec3& a = cut[k]; const Vec3& b = cut[(k + 1) % cut.size()];
                // Wind the cap so its normal faces out of the kept half (-N side is the new surface).
                Vec3 fn = Vec3::Cross(a - ctr, b - ctr);
                int base = (int)nv.size();
                if ((fn.x * N.x + fn.y * N.y + fn.z * N.z) * sign > 0.0f) { nv.push_back(ctr); nv.push_back(b); nv.push_back(a); }
                else                                                       { nv.push_back(ctr); nv.push_back(a); nv.push_back(b); }
                nt.push_back(base); nt.push_back(base + 1); nt.push_back(base + 2);
            }
        }
        vertices = std::move(nv); triangles = std::move(nt);
        triColors.clear(); uvs.clear();
        name = "";
        WeldVertices();
        ComputeSmoothNormals();
    }

    /// Shrink/Fatten (Blender's Alt-S): push every vertex along its surface normal by
    /// `dist` — positive inflates, negative deflates. Normals are oriented outward
    /// (away from the mesh centre) so it inflates regardless of triangle winding.
    void ShrinkFatten(float dist) {
        if (vertices.empty()) return;
        ComputeSmoothNormals();
        Vec3 ctr{0, 0, 0};
        for (const Vec3& v : vertices) ctr += v;
        ctr = ctr * (1.0f / (float)vertices.size());
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            Vec3 n = normals[i];
            Vec3 out = vertices[i] - ctr;
            if (n.x * out.x + n.y * out.y + n.z * out.z < 0.0f) n = n * -1.0f;  // face outward
            vertices[i] += n * dist;
        }
        name = "";
        ComputeSmoothNormals();
    }

    /// Wireframe modifier: replace every edge with a solid beam of square cross-
    /// section (`thickness` across), turning the mesh into its wire lattice — handy
    /// for cages, scaffolds, and stylised low-poly props.
    void Wireframe(float thickness) {
        if (triangles.empty() || thickness <= 1e-6f) return;
        Mesh w = *this; w.WeldVertices();
        std::set<std::pair<int, int>> edges;
        for (std::size_t t = 0; t + 2 < w.triangles.size(); t += 3) {
            int a = w.triangles[t], b = w.triangles[t + 1], c = w.triangles[t + 2];
            auto add = [&](int i, int j) { edges.insert({std::min(i, j), std::max(i, j)}); };
            add(a, b); add(b, c); add(c, a);
        }
        Mesh out;
        float r = thickness * 0.5f;
        for (const auto& e : edges) out.AddBeam(w.vertices[e.first], w.vertices[e.second], r);
        *this = std::move(out);
        name = "";
        ComputeSmoothNormals();
    }

    /// Displace modifier: perturb every vertex along its outward normal by value
    /// noise sampled at its position — instant organic bumpiness (rocks, terrain,
    /// bark). `frequency` sets the noise scale, `seed` picks the pattern; both are
    /// deterministic. Subdivide first for finer detail.
    void Displace(float amount, float frequency = 1.0f, int seed = 0) {
        if (vertices.empty()) return;
        ComputeSmoothNormals();
        Vec3 ctr{0, 0, 0};
        for (const Vec3& v : vertices) ctr += v;
        ctr = ctr * (1.0f / (float)vertices.size());
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            Vec3 n = normals[i], out = vertices[i] - ctr;
            if (n.x * out.x + n.y * out.y + n.z * out.z < 0.0f) n = n * -1.0f;
            float d = ValueNoise(vertices[i] * frequency, seed);
            vertices[i] += n * (d * amount);
        }
        name = "";
        ComputeSmoothNormals();
    }

    /// Cast modifier (to a cylinder): pull each vertex's radius (perpendicular to
    /// `axis`: 0=X,1=Y,2=Z) toward `radius`, by `amount` (0 = none, 1 = full). Rounds
    /// a boxy shape into a barrel/tube. Cast-to-sphere already exists as Spherify().
    void CastToCylinder(float radius, int axis = 1, float amount = 1.0f) {
        if (vertices.empty()) return;
        for (Vec3& v : vertices) {
            float a, b;
            if (axis == 0)      { a = v.y; b = v.z; }
            else if (axis == 2) { a = v.x; b = v.y; }
            else                { a = v.x; b = v.z; }
            float r = std::sqrt(a * a + b * b);
            if (r < 1e-6f) continue;
            float f = 1.0f + (radius / r - 1.0f) * amount;   // lerp(1, radius/r, amount)
            a *= f; b *= f;
            if (axis == 0)      { v.y = a; v.z = b; }
            else if (axis == 2) { v.x = a; v.y = b; }
            else                { v.x = a; v.z = b; }
        }
        name = "";
        ComputeSmoothNormals();
    }

    /// Stretch/squash along one axis (0=X,1=Y,2=Z) about the mesh centre. factor > 1
    /// stretches, < 1 squashes, < 0 mirrors. A cheap Simple-Deform stretch.
    void Stretch(int axis, float factor) {
        if (vertices.empty() || axis < 0 || axis > 2) return;
        float c = 0.0f;
        for (const Vec3& v : vertices) c += (&v.x)[axis];
        c /= (float)vertices.size();
        for (Vec3& v : vertices) (&v.x)[axis] = c + ((&v.x)[axis] - c) * factor;
        name = "";
        ComputeSmoothNormals();
    }

    /// Screw modifier: revolve a profile (x = radius, y = height) around Y, sweeping
    /// `turns` full turns over `segments` steps and rising `pitch` per turn — a helix
    /// (springs, screw threads, spiral ramps). With turns = 1 and pitch = 0 it's a
    /// closed lathe.
    static Mesh Screw(const std::vector<Vec2>& profile, float turns = 1.0f, float pitch = 0.0f,
                      int segments = 32) {
        Mesh m;
        const int rows = (int)profile.size();
        if (rows < 2 || segments < 2 || turns <= 0.0f) return m;
        const float kPi = 3.14159265358979323846f;
        const bool closed = (turns >= 0.999f && turns <= 1.001f && std::fabs(pitch) < 1e-6f);
        const int rings = closed ? segments : segments + 1;     // open sweeps need a final ring
        for (int s = 0; s < rings; ++s) {
            float frac = (float)s / segments;                    // 0..turns across the sweep
            float th = 2.0f * kPi * turns * frac;
            float rise = pitch * turns * frac;
            float c = std::cos(th), sn = std::sin(th);
            for (const Vec2& p : profile)
                m.vertices.push_back({p.x * c, p.y + rise, p.x * sn});
        }
        for (int s = 0; s + 1 < rings || (closed && s < segments); ++s) {
            int s1 = closed ? (s + 1) % segments : s + 1;
            if (!closed && s1 >= rings) break;
            for (int r = 0; r + 1 < rows; ++r) {
                int a = s * rows + r,  b = s * rows + r + 1;
                int c = s1 * rows + r, d = s1 * rows + r + 1;
                m.triangles.insert(m.triangles.end(), {a, c, b, b, c, d});
            }
        }
        m.ComputeSmoothNormals();
        return m;
    }

private:
    /// Deterministic hash of an integer lattice point → [-1,1] (for value noise).
    static float Hash3(int x, int y, int z, int seed) {
        unsigned h = (unsigned)(x * 374761393 + y * 668265263 + z * 2147483647 + seed * 971);
        h = (h ^ (h >> 13)) * 1274126177u;
        return ((float)((h ^ (h >> 16)) & 0xffffu) / 65535.0f) * 2.0f - 1.0f;
    }
    /// Trilinear-interpolated value noise at `p` (smoothstep fade), range ~[-1,1].
    static float ValueNoise(const Vec3& p, int seed) {
        int xi = (int)std::floor(p.x), yi = (int)std::floor(p.y), zi = (int)std::floor(p.z);
        float fx = p.x - xi, fy = p.y - yi, fz = p.z - zi;
        auto sm = [](float t) { return t * t * (3.0f - 2.0f * t); };
        fx = sm(fx); fy = sm(fy); fz = sm(fz);
        auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
        float c00 = lerp(Hash3(xi, yi, zi, seed),       Hash3(xi + 1, yi, zi, seed), fx);
        float c10 = lerp(Hash3(xi, yi + 1, zi, seed),   Hash3(xi + 1, yi + 1, zi, seed), fx);
        float c01 = lerp(Hash3(xi, yi, zi + 1, seed),   Hash3(xi + 1, yi, zi + 1, seed), fx);
        float c11 = lerp(Hash3(xi, yi + 1, zi + 1, seed), Hash3(xi + 1, yi + 1, zi + 1, seed), fx);
        return lerp(lerp(c00, c10, fy), lerp(c01, c11, fy), fz);
    }
    /// Append a square-section beam between two points (used by Wireframe()).
    void AddBeam(const Vec3& p0, const Vec3& p1, float r) {
        Vec3 d = p1 - p0; float len = d.Magnitude();
        if (len < 1e-6f) return;
        d = d * (1.0f / len);
        Vec3 up = std::fabs(d.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
        Vec3 u = Vec3::Cross(d, up); u = u * (1.0f / std::max(1e-6f, u.Magnitude()));
        Vec3 w = Vec3::Cross(d, u);
        auto corner = [&](const Vec3& base, float su, float sw) { return base + u * (su * r) + w * (sw * r); };
        int b = (int)vertices.size();
        // 0..3 around p0, 4..7 around p1 (same order).
        vertices.push_back(corner(p0, +1, +1)); vertices.push_back(corner(p0, +1, -1));
        vertices.push_back(corner(p0, -1, -1)); vertices.push_back(corner(p0, -1, +1));
        vertices.push_back(corner(p1, +1, +1)); vertices.push_back(corner(p1, +1, -1));
        vertices.push_back(corner(p1, -1, -1)); vertices.push_back(corner(p1, -1, +1));
        auto quad = [&](int a, int b2, int c, int dd) {
            triangles.insert(triangles.end(), {b + a, b + b2, b + c, b + a, b + c, b + dd});
        };
        quad(0, 1, 5, 4); quad(1, 2, 6, 5); quad(2, 3, 7, 6); quad(3, 0, 4, 7);  // sides
        quad(3, 2, 1, 0); quad(4, 5, 6, 7);                                       // caps
    }

    /// Choose a grid that covers [lo,hi] at ~`voxel`, capped to `maxDim` per axis
    /// (enlarging the effective cell size `outV` if needed so it always fits).
    static void GridDims(const Vec3& lo, const Vec3& hi, float voxel, int maxDim,
                         int& nx, int& ny, int& nz, float& outV) {
        outV = voxel;
        auto dim = [&](float span) {
            int d = (int)std::ceil(span / outV);
            return d < 1 ? 1 : d;
        };
        nx = dim(hi.x - lo.x); ny = dim(hi.y - lo.y); nz = dim(hi.z - lo.z);
        int m = std::max(nx, std::max(ny, nz));
        if (maxDim > 0 && m > maxDim) {
            outV *= (float)m / (float)maxDim;                   // grow cells to fit the cap
            nx = dim(hi.x - lo.x); ny = dim(hi.y - lo.y); nz = dim(hi.z - lo.z);
        }
    }
    /// Mark every grid cell whose centre is inside `src` as solid.
    static void SampleInto(const Mesh& src, std::vector<unsigned char>& occ,
                           int nx, int ny, int nz, const Vec3& origin, float v) {
        for (int z = 0; z < nz; ++z)
            for (int y = 0; y < ny; ++y)
                for (int x = 0; x < nx; ++x) {
                    Vec3 c{origin.x + (x + 0.5f) * v, origin.y + (y + 0.5f) * v, origin.z + (z + 0.5f) * v};
                    if (src.PointInside(c)) occ[(std::size_t)(z * ny + y) * nx + x] = 1;
                }
    }
    /// Build a surface from a solid/empty grid: a quad on every face between a solid
    /// cell and an empty neighbour (or the grid edge), wound to face outward.
    static Mesh EmitOccupancy(const std::vector<unsigned char>& occ,
                              int nx, int ny, int nz, const Vec3& origin, float v) {
        Mesh out;
        auto solid = [&](int x, int y, int z) {
            if (x < 0 || y < 0 || z < 0 || x >= nx || y >= ny || z >= nz) return false;
            return occ[(std::size_t)(z * ny + y) * nx + x] != 0;
        };
        auto face = [&](Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 outward) {
            Vec3 nrm = Vec3::Cross(b - a, c - a);
            float dot = nrm.x * outward.x + nrm.y * outward.y + nrm.z * outward.z;
            int base = (int)out.vertices.size();
            if (dot >= 0.0f) { out.vertices.push_back(a); out.vertices.push_back(b);
                               out.vertices.push_back(c); out.vertices.push_back(d); }
            else             { out.vertices.push_back(a); out.vertices.push_back(d);
                               out.vertices.push_back(c); out.vertices.push_back(b); }
            out.triangles.insert(out.triangles.end(),
                                 {base, base + 1, base + 2, base, base + 2, base + 3});
        };
        for (int z = 0; z < nz; ++z)
            for (int y = 0; y < ny; ++y)
                for (int x = 0; x < nx; ++x) {
                    if (!solid(x, y, z)) continue;
                    float x0 = origin.x + x * v, y0 = origin.y + y * v, z0 = origin.z + z * v;
                    float x1 = x0 + v, y1 = y0 + v, z1 = z0 + v;
                    if (!solid(x + 1, y, z)) face({x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}, {1, 0, 0});
                    if (!solid(x - 1, y, z)) face({x0, y0, z0}, {x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1}, {-1, 0, 0});
                    if (!solid(x, y + 1, z)) face({x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}, {0, 1, 0});
                    if (!solid(x, y - 1, z)) face({x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, {0, -1, 0});
                    if (!solid(x, y, z + 1)) face({x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, {0, 0, 1});
                    if (!solid(x, y, z - 1)) face({x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0}, {0, 0, -1});
                }
        out.WeldVertices();
        out.ComputeSmoothNormals();
        return out;
    }

public:
};

} // namespace okay
