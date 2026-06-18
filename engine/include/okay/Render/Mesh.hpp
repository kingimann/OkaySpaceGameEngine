#pragma once
#include "okay/Math/Vec3.hpp"
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

/// A simple indexed triangle mesh (positions + triangle indices). Enough to
/// describe 3D geometry; the editor renders these as wireframes and a future
/// GPU backend can upload them directly. `name` tags built-in primitives so they
/// can be serialized compactly.
struct Mesh {
    std::string       name;       // "Cube"/"Pyramid"/"Quad" for primitives
    std::vector<Vec3> vertices;
    std::vector<int>  triangles;  // 3 indices per triangle

    int TriangleCount() const { return static_cast<int>(triangles.size() / 3); }

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
        m.triangles = {
            0,1,2, 0,2,3,   // back
            4,6,5, 4,7,6,   // front
            4,5,1, 4,1,0,   // bottom
            3,2,6, 3,6,7,   // top
            4,0,3, 4,3,7,   // left
            1,5,6, 1,6,2,   // right
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
        m.triangles = {
            0,2,1, 0,3,2,        // base
            0,1,4, 1,2,4,        // sides
            2,3,4, 3,0,4,
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
            m.vertices.push_back({x, h, z});
            m.vertices.push_back({x, -h, z});
        }
        for (int s = 0; s < sectors; ++s) {             // side quads
            int t0 = s * 2, b0 = s * 2 + 1, t1 = (s + 1) * 2, b1 = (s + 1) * 2 + 1;
            m.triangles.insert(m.triangles.end(), {t0, b0, t1, t1, b0, b1});
        }
        int topC = (int)m.vertices.size(); m.vertices.push_back({0, h, 0});
        int botC = (int)m.vertices.size(); m.vertices.push_back({0, -h, 0});
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

    /// Append another mesh into this one (re-indexing its triangles) — build a
    /// compound model (e.g. a snowman) from primitive parts.
    void Combine(const Mesh& other) {
        int base = (int)vertices.size();
        name = "";
        vertices.insert(vertices.end(), other.vertices.begin(), other.vertices.end());
        for (int t : other.triangles) triangles.push_back(t + base);
    }
    static Mesh Combined(const Mesh& a, const Mesh& b) { Mesh m = a; m.Combine(b); return m; }

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
        for (std::size_t i = 0; i + 2 < triangles.size(); i += 3) {
            int a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
            int ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
            out.insert(out.end(), {a, ab, ca,  ab, b, bc,  ca, bc, c,  ab, bc, ca});
        }
        triangles = std::move(out);
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
};

} // namespace okay
