#pragma once
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Render/Color.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <functional>
#include <fstream>
#include <map>
#include <set>
#include <tuple>
#include <sstream>
#include <string>
#include <unordered_map>
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
    float autoSmoothAngle = 0.0f; // >0: normals came from ComputeAutoSmoothNormals(angle)
                                  // (serialized so the shading survives save/load)

    int TriangleCount() const { return static_cast<int>(triangles.size() / 3); }
    bool HasFaceColors() const { return (int)triColors.size() == TriangleCount() && !triColors.empty(); }
    bool HasNormals() const { return normals.size() == vertices.size() && !vertices.empty(); }

    /// Compute per-vertex smooth normals for Gouraud shading. Vertices sharing a
    /// position (even across separately-built parts) are grouped so the normal is
    /// averaged across the seam — this both rounds curved surfaces and hides the
    /// joins between assembled body parts. Area-weighted (uses un-normalized face
    /// normals) for a natural result.
    void ComputeSmoothNormals() {
        autoSmoothAngle = 0.0f;               // fully smooth supersedes auto-smooth
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

    /// Angle-based shading (Blender's Auto Smooth): smooth normals across edges
    /// where adjacent faces meet at less than `angleDeg`, hard splits where they
    /// meet sharper — a cylinder gets a smooth barrel with crisp cap rims in one
    /// click, no manual smooth/flat juggling. Rebuilds the vertex list (corners in
    /// different smoothing groups get their own vertex copies); keeps UVs (split
    /// per seam) and per-face colors. Remembered in `autoSmoothAngle` so the scene
    /// serializer can restore the same shading on load.
    void ComputeAutoSmoothNormals(float angleDeg = 30.0f) {
        const int nt = (int)triangles.size() / 3;
        if (nt == 0 || vertices.empty()) return;
        const float cosT = std::cos(angleDeg * 0.01745329252f);
        const bool hasUV = uvs.size() == vertices.size();
        // Area-weighted face normals (+ unit copies for the angle tests). Zero-area
        // triangles carry no orientation: they join whatever group is available
        // without contributing to (or being tested against) its normal.
        std::vector<Vec3> fn(nt), fu(nt);
        std::vector<char> fdeg(nt, 0);
        for (int t = 0; t < nt; ++t) {
            const Vec3& a = vertices[triangles[t * 3]];
            fn[t] = Vec3::Cross(vertices[triangles[t * 3 + 1]] - a,
                                vertices[triangles[t * 3 + 2]] - a);
            float m = fn[t].Magnitude();
            if (m > 1e-12f) fu[t] = fn[t] * (1.0f / m);
            else { fu[t] = Vec3{0, 1, 0}; fdeg[t] = 1; }
        }
        // Corners around each coincident-position vertex.
        std::vector<int> rep = CoincidentReps();
        std::map<int, std::vector<std::pair<int, int>>> corners;   // rep -> (tri, slot)
        for (int t = 0; t < nt; ++t)
            for (int k = 0; k < 3; ++k)
                corners[rep[triangles[t * 3 + k]]].push_back({t, k});
        Mesh out;
        out.triangles.assign(triangles.size(), 0);
        for (auto& cv : corners) {
            // Greedy smoothing groups at this vertex: a corner joins the first group
            // whose average normal is within the angle of its face's normal.
            std::vector<Vec3> gSum;
            std::vector<std::vector<std::pair<int, int>>> gMembers;
            for (auto& tk : cv.second) {
                int t = tk.first, gi = -1;
                float sign = 1.0f;
                if (fdeg[t]) {
                    // No orientation of its own: ride along with the first group.
                    if (gSum.empty()) { gSum.push_back({0, 0, 0}); gMembers.emplace_back(); }
                    gMembers[0].push_back(tk);
                    continue;
                }
                for (int g = 0; g < (int)gSum.size(); ++g) {
                    Vec3 gn = gSum[g]; float m = gn.Magnitude();
                    if (m < 1e-12f) continue;
                    // Sign-tolerant: several built-in generators (and hand edits)
                    // carry mixed winding, so a flipped-but-parallel neighbor still
                    // belongs to the group — accumulate it sign-aligned instead of
                    // letting it cancel the average to zero.
                    float d = Vec3::Dot(gn * (1.0f / m), fu[t]);
                    if (d >= cosT)       { gi = g; sign =  1.0f; break; }
                    else if (-d >= cosT) { gi = g; sign = -1.0f; break; }
                }
                if (gi < 0) { gSum.push_back({0, 0, 0}); gMembers.emplace_back(); gi = (int)gSum.size() - 1; }
                gSum[gi] += fn[t] * sign;
                gMembers[gi].push_back(tk);
            }
            for (int g = 0; g < (int)gSum.size(); ++g) {
                Vec3 n = gSum[g]; float m = n.Magnitude();
                n = m > 1e-12f ? n * (1.0f / m) : Vec3{0, 1, 0};
                // One output vertex per (group, uv) so texture seams keep their UVs.
                std::map<std::pair<long, long>, int> byUV;
                for (auto& tk : gMembers[g]) {
                    int src = triangles[tk.first * 3 + tk.second];
                    std::pair<long, long> uk{0, 0};
                    if (hasUV) uk = {std::lround(uvs[src].x * 4096.0f),
                                     std::lround(uvs[src].y * 4096.0f)};
                    auto it = byUV.find(uk);
                    int vi;
                    if (it == byUV.end()) {
                        vi = (int)out.vertices.size();
                        out.vertices.push_back(vertices[src]);
                        out.normals.push_back(n);
                        if (hasUV) out.uvs.push_back(uvs[src]);
                        byUV[uk] = vi;
                    } else vi = it->second;
                    out.triangles[tk.first * 3 + tk.second] = vi;
                }
            }
        }
        vertices = std::move(out.vertices);
        normals  = std::move(out.normals);
        triangles = std::move(out.triangles);
        if (hasUV) uvs = std::move(out.uvs); else uvs.clear();
        autoSmoothAngle = angleDeg;   // triColors are per-face and index-stable: keep
        name = "";
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

    /// A rounded box (superellipsoid): flat faces with softly rounded edges and
    /// corners — the go-to shape for UI-ish props, buttons, dice, soft crates.
    /// `roundness` 0.05 = nearly a sharp cube .. 1.0 = a full sphere.
    static Mesh RoundedBox(float size = 1.0f, float roundness = 0.3f, int rings = 14, int sectors = 20) {
        Mesh m; m.name = "RoundedBox";
        const float kPi = 3.14159265358979323846f;
        float e = roundness < 0.05f ? 0.05f : (roundness > 1.0f ? 1.0f : roundness);
        float h = size * 0.5f;
        auto sp = [](float v, float p) { float s = v < 0 ? -1.0f : 1.0f; return s * std::pow(std::fabs(v), p); };
        for (int r = 0; r <= rings; ++r) {
            float phi = kPi * (float)r / rings;                 // 0..pi (top to bottom)
            float cphi = std::cos(phi), sphi = std::sin(phi);
            for (int s = 0; s <= sectors; ++s) {
                float th = 2.0f * kPi * (float)s / sectors;
                float cth = std::cos(th), sth = std::sin(th);
                m.vertices.push_back({h * sp(sphi, e) * sp(cth, e),
                                      h * sp(cphi, e),
                                      h * sp(sphi, e) * sp(sth, e)});
                m.uvs.push_back({(float)s / sectors, 1.0f - (float)r / rings});
            }
        }
        int stride = sectors + 1;
        for (int r = 0; r < rings; ++r)
            for (int s = 0; s < sectors; ++s) {
                int a = r * stride + s, b = a + stride;
                m.triangles.insert(m.triangles.end(), {a, a + 1, b, a + 1, b + 1, b});
            }
        m.ComputeSmoothNormals();
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

    /// A (p,q) torus knot: a circular tube swept along the knot curve
    ///   r = cos(q·t) + 2,  (x,z) = r·(cos,sin)(p·t),  y = -sin(q·t)
    /// The cross-section frame is built by parallel transport of an initial normal
    /// (no per-sample curvature needed), which keeps the tube from flipping. A
    /// Blender staple; nice for stress-testing shading and wireframes.
    static Mesh TorusKnot(int p = 2, int q = 3, float scale = 0.32f, float tube = 0.10f,
                          int rings = 160, int sides = 12) {
        Mesh m;
        m.name = "TorusKnot";
        if (p < 1) p = 1; if (q < 1) q = 1; if (rings < 8) rings = 8; if (sides < 3) sides = 3;
        const float kPi = 3.14159265358979323846f;
        auto curve = [&](float t) {
            float r = std::cos(q * t) + 2.0f;
            return Vec3{ r * std::cos(p * t) * scale, -std::sin(q * t) * scale, r * std::sin(p * t) * scale };
        };
        std::vector<Vec3> C(rings), T(rings), N(rings), B(rings);
        for (int i = 0; i < rings; ++i) {
            float t = 2.0f * kPi * (float)i / rings, dt = 1e-3f;
            C[i] = curve(t);
            Vec3 tg = curve(t + dt) - curve(t - dt);
            float mg = tg.Magnitude(); T[i] = mg > 1e-6f ? tg * (1.0f / mg) : Vec3{0, 0, 1};
        }
        // Initial frame, then parallel-transport the normal around the loop.
        Vec3 up = std::fabs(T[0].y) < 0.99f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
        N[0] = Vec3::Cross(up, T[0]);
        { float mg = N[0].Magnitude(); N[0] = mg > 1e-6f ? N[0] * (1.0f / mg) : Vec3{1, 0, 0}; }
        B[0] = Vec3::Cross(T[0], N[0]);
        for (int i = 1; i < rings; ++i) {
            Vec3 v = Vec3::Cross(T[i - 1], T[i]);
            float s = v.Magnitude(), c = Vec3::Dot(T[i - 1], T[i]);
            Vec3 n = N[i - 1];
            if (s > 1e-6f) {                       // rotate n from T[i-1] onto T[i] (Rodrigues)
                Vec3 axis = v * (1.0f / s);
                float ang = std::atan2(s, c), ca = std::cos(ang), sa = std::sin(ang);
                n = n * ca + Vec3::Cross(axis, n) * sa + axis * (Vec3::Dot(axis, n) * (1.0f - ca));
            }
            n = n - T[i] * Vec3::Dot(T[i], n);     // re-orthogonalise against the new tangent
            float mg = n.Magnitude(); N[i] = mg > 1e-6f ? n * (1.0f / mg) : N[i - 1];
            B[i] = Vec3::Cross(T[i], N[i]);
        }
        for (int i = 0; i < rings; ++i)
            for (int s = 0; s < sides; ++s) {
                float a = 2.0f * kPi * (float)s / sides;
                m.vertices.push_back(C[i] + N[i] * (std::cos(a) * tube) + B[i] * (std::sin(a) * tube));
            }
        for (int i = 0; i < rings; ++i)
            for (int s = 0; s < sides; ++s) {
                int i1 = (i + 1) % rings, s1 = (s + 1) % sides;
                int a = i * sides + s, b = i1 * sides + s, c = i1 * sides + s1, d = i * sides + s1;
                m.triangles.insert(m.triangles.end(), {a, b, d, d, b, c});
            }
        m.ComputeSmoothNormals();
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

    /// Append an axis-aligned box spanning [mn, mx] (8 verts, 12 tris, outward
    /// winding) to an existing mesh — a building block for compound primitives.
    static void AppendBox(Mesh& m, const Vec3& mn, const Vec3& mx) {
        int b = (int)m.vertices.size();
        m.vertices.push_back({mn.x, mn.y, mn.z}); m.vertices.push_back({mx.x, mn.y, mn.z});
        m.vertices.push_back({mx.x, mx.y, mn.z}); m.vertices.push_back({mn.x, mx.y, mn.z});
        m.vertices.push_back({mn.x, mn.y, mx.z}); m.vertices.push_back({mx.x, mn.y, mx.z});
        m.vertices.push_back({mx.x, mx.y, mx.z}); m.vertices.push_back({mn.x, mx.y, mx.z});
        const int f[36] = {0,2,1, 0,3,2,  4,5,6, 4,6,7,  0,1,5, 0,5,4,
                           3,7,6, 3,6,2,  1,2,6, 1,6,5,  0,4,7, 0,7,3};
        for (int i = 0; i < 36; ++i) m.triangles.push_back(b + f[i]);
    }

    /// A dome: the top half of a sphere with a flat base cap on the XZ plane.
    static Mesh Hemisphere(float radius = 0.5f, int rings = 6, int sectors = 16) {
        Mesh m; m.name = "Hemisphere";
        if (rings < 1) rings = 1; if (sectors < 3) sectors = 3;
        const float kPi = 3.14159265358979323846f;
        const int stride = sectors + 1;
        for (int r = 0; r <= rings; ++r) {
            float phi = (kPi * 0.5f) * (float)r / rings;       // 0 = top .. pi/2 = equator
            float y = radius * std::cos(phi), rr = radius * std::sin(phi);
            for (int s = 0; s <= sectors; ++s) {
                float th = 2.0f * kPi * (float)s / sectors;
                m.vertices.push_back({rr * std::cos(th), y, rr * std::sin(th)});
                m.uvs.push_back({(float)s / sectors, 1.0f - (float)r / rings});
            }
        }
        for (int r = 0; r < rings; ++r)
            for (int s = 0; s < sectors; ++s) {
                int a = r * stride + s, b = a + stride;
                m.triangles.insert(m.triangles.end(), {a, a + 1, b, a + 1, b + 1, b});
            }
        int base = (int)m.vertices.size(); m.vertices.push_back({0, 0, 0}); m.uvs.push_back({0.5f, 0.5f});
        int ring0 = rings * stride;                            // the equator ring
        for (int s = 0; s < sectors; ++s)
            m.triangles.insert(m.triangles.end(), {base, ring0 + s, ring0 + s + 1});
        return m;
    }

    /// A staircase of `steps` boxes climbing +Y and marching +Z (great for level
    /// blockout). Sits with its base at y=0, front at z=0.
    static Mesh Stairs(int steps = 5, float width = 1.0f, float totalHeight = 1.0f, float totalDepth = 1.0f) {
        Mesh m; m.name = "Stairs";
        if (steps < 1) steps = 1;
        float sh = totalHeight / steps, sd = totalDepth / steps, hw = width * 0.5f;
        for (int i = 0; i < steps; ++i)
            AppendBox(m, {-hw, 0.0f, i * sd}, {hw, (i + 1) * sh, totalDepth});
        return m;
    }

    /// A cog/gear: a short cylinder with `teeth` square teeth around the rim,
    /// extruded `thickness` along Y. Spin it with a script for machinery.
    static Mesh Gear(int teeth = 10, float radius = 0.5f, float toothDepth = 0.12f, float thickness = 0.25f) {
        Mesh m; m.name = "Gear";
        if (teeth < 3) teeth = 3;
        const float kPi = 3.14159265358979323846f;
        const int seg = teeth * 4;                             // 4 rim segments per tooth
        float h = thickness * 0.5f;
        for (int s = 0; s < seg; ++s) {
            float th = 2.0f * kPi * (float)s / seg;
            int phase = s % 4;
            float rr = (phase == 1 || phase == 2) ? radius + toothDepth : radius;   // crest vs valley
            float x = rr * std::cos(th), z = rr * std::sin(th);
            m.vertices.push_back({x, h, z}); m.vertices.push_back({x, -h, z});
        }
        for (int s = 0; s < seg; ++s) {
            int t0 = s * 2, b0 = s * 2 + 1, t1 = ((s + 1) % seg) * 2, b1 = ((s + 1) % seg) * 2 + 1;
            m.triangles.insert(m.triangles.end(), {t0, t1, b0, t1, b1, b0});   // outward side quads
        }
        int topC = (int)m.vertices.size(); m.vertices.push_back({0, h, 0});
        int botC = (int)m.vertices.size(); m.vertices.push_back({0, -h, 0});
        for (int s = 0; s < seg; ++s) {
            m.triangles.insert(m.triangles.end(), {topC, ((s + 1) % seg) * 2, s * 2});       // top cap (up)
            m.triangles.insert(m.triangles.end(), {botC, s * 2 + 1, ((s + 1) % seg) * 2 + 1}); // bottom cap (down)
        }
        return m;
    }

    /// An n-sided prism (flat-capped): a regular polygon extruded along Y. `sides`
    /// picks the shape — 3 = triangular prism, 6 = hexagonal column, etc. Distinct
    /// from Cylinder (which is high-poly/round) — this stays crisply faceted.
    static Mesh Prism(int sides = 6, float radius = 0.5f, float height = 1.0f) {
        Mesh m; m.name = "Prism";
        if (sides < 3) sides = 3;
        const float kPi = 3.14159265358979323846f;
        float h = height * 0.5f;
        for (int s = 0; s < sides; ++s) {
            float th = 2.0f * kPi * ((float)s + 0.5f) / sides;   // flat face toward +Z
            float x = radius * std::cos(th), z = radius * std::sin(th);
            m.vertices.push_back({x, h, z}); m.vertices.push_back({x, -h, z});
        }
        for (int s = 0; s < sides; ++s) {
            int t0 = s * 2, b0 = s * 2 + 1, t1 = ((s + 1) % sides) * 2, b1 = ((s + 1) % sides) * 2 + 1;
            m.triangles.insert(m.triangles.end(), {t0, t1, b0, t1, b1, b0});
        }
        int topC = (int)m.vertices.size(); m.vertices.push_back({0, h, 0});
        int botC = (int)m.vertices.size(); m.vertices.push_back({0, -h, 0});
        for (int s = 0; s < sides; ++s) {
            m.triangles.insert(m.triangles.end(), {topC, ((s + 1) % sides) * 2, s * 2});
            m.triangles.insert(m.triangles.end(), {botC, s * 2 + 1, ((s + 1) % sides) * 2 + 1});
        }
        return m;
    }

    /// A flat filled circle on the XZ plane (Blender's Circle, filled) — a round
    /// disc facing +Y. Handy for tabletops, coins, platforms, decals.
    static Mesh Disc(float radius = 0.5f, int sectors = 24) {
        Mesh m; m.name = "Disc";
        if (sectors < 3) sectors = 3;
        const float kPi = 3.14159265358979323846f;
        m.vertices.push_back({0, 0, 0}); m.uvs.push_back({0.5f, 0.5f});   // center
        for (int s = 0; s <= sectors; ++s) {
            float th = 2.0f * kPi * (float)s / sectors;
            float cx = std::cos(th), cz = std::sin(th);
            m.vertices.push_back({radius * cx, 0, radius * cz});
            m.uvs.push_back({0.5f + 0.5f * cx, 0.5f + 0.5f * cz});
        }
        for (int s = 0; s < sectors; ++s)
            m.triangles.insert(m.triangles.end(), {0, s + 2, s + 1});    // faces +Y
        return m;
    }

    /// An octahedron — a faceted gem/diamond (6 vertices, 8 triangular faces).
    static Mesh Octahedron(float radius = 0.5f) {
        Mesh m; m.name = "Octahedron";
        float r = radius;
        m.vertices = {{r,0,0}, {-r,0,0}, {0,r,0}, {0,-r,0}, {0,0,r}, {0,0,-r}};  // px nx py ny pz nz
        m.triangles = {4,0,2, 4,2,1, 4,1,3, 4,3,0,     // top fan around +Z
                       5,2,0, 5,1,2, 5,3,1, 5,0,3};    // bottom fan around -Z
        return m;
    }

    /// A regular tetrahedron — the simplest solid (4 vertices, 4 triangular faces).
    static Mesh Tetrahedron(float radius = 0.5f) {
        Mesh m; m.name = "Tetrahedron";
        float a = radius;
        m.vertices = {{a, a, a}, {a, -a, -a}, {-a, a, -a}, {-a, -a, a}};
        m.triangles = {0, 1, 2, 0, 3, 1, 0, 2, 3, 1, 3, 2};   // outward-facing
        return m;
    }

    /// A bipyramid — a faceted crystal/gem: an n-gon equator with an apex above and
    /// below. `sides` picks the cut (6 = a classic hex crystal).
    static Mesh Bipyramid(int sides = 6, float radius = 0.5f, float height = 1.0f) {
        Mesh m; m.name = "Bipyramid";
        if (sides < 3) sides = 3;
        const float kPi = 3.14159265358979323846f;
        float h = height * 0.5f;
        for (int s = 0; s < sides; ++s) {
            float th = 2.0f * kPi * (float)s / sides;
            m.vertices.push_back({radius * std::cos(th), 0, radius * std::sin(th)});
        }
        int top = (int)m.vertices.size(); m.vertices.push_back({0, h, 0});
        int bot = (int)m.vertices.size(); m.vertices.push_back({0, -h, 0});
        for (int s = 0; s < sides; ++s) {
            int n = (s + 1) % sides;
            m.triangles.insert(m.triangles.end(), {top, n, s});   // upper faces
            m.triangles.insert(m.triangles.end(), {bot, s, n});   // lower faces
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
    // Deterministic per-(variant,key) value in [lo,hi] for procedural prop
    // variation. variant 0 always returns the midpoint, so the "base" model is
    // unchanged and backward-compatible; any other variant perturbs it.
    static float PropRand(int variant, int key, float lo, float hi) {
        if (variant == 0) return (lo + hi) * 0.5f;
        unsigned x = (unsigned)(variant * 374761393 + key * 668265263);
        x = (x ^ (x >> 13)) * 1274126177u;
        float t = (float)((x ^ (x >> 16)) & 0xffffu) / 65535.0f;
        return lo + (hi - lo) * t;
    }

    static Mesh Tree(int variant = 0) {
        Mesh m;
        Color bark  = Color::FromBytes(96, 68, 44);
        Color leafA = Color::FromBytes(56, 118, 52);
        Color leafB = Color::FromBytes(76, 138, 60);
        Color leafC = Color::FromBytes(46, 102, 48);
        auto R = [&](int k, float lo, float hi) { return PropRand(variant, k, lo, hi); };
        float lean = R(1, -0.10f, 0.14f), leanZ = R(2, -0.08f, 0.08f);
        float ht   = R(3, 0.92f, 1.18f);                // trunk-height multiplier
        // Curved, tapered trunk (lean + height vary per variant) + two branches.
        m.Add(SweepPath({{0,0,0}, {0.02f,0.35f*ht,0.01f}, {0.06f,0.70f*ht,0.03f},
                         {0.12f+lean,1.05f*ht,0.05f+leanZ}}, 0.14f, 8, true, 0.07f), {0,0,0}, {1,1,1}, &bark);
        m.Add(SweepPath({{0.06f,0.80f*ht,0.02f}, {0.35f,1.00f*ht,0.15f}, {0.55f,1.25f*ht,0.28f}},
                        0.05f, 6, true, 0.02f), {0,0,0}, {1,1,1}, &bark);
        m.Add(SweepPath({{0.03f,0.65f*ht,0.00f}, {-0.30f,0.90f*ht,-0.12f}, {-0.50f,1.10f*ht,-0.25f}},
                        0.05f, 6, true, 0.02f), {0,0,0}, {1,1,1}, &bark);
        // Canopy: jittered blobs, each nudged in position/size by the variant.
        int js = variant * 100 + 1;
        auto blob = [&](Vec3 at, float s, const Color& c, int k) {
            at.x += R(k*3+10, -0.12f, 0.12f); at.y += R(k*3+11, -0.06f, 0.10f) + (ht-1.0f)*0.5f;
            at.z += R(k*3+12, -0.12f, 0.12f); s *= R(k+20, 0.86f, 1.14f);
            Mesh b = Icosphere(0.5f, 1);
            b.JitterVertices({}, 0.055f, js + k);
            m.Add(b, at, {s, s * 0.88f, s}, &c);
        };
        blob({0.10f, 1.55f, 0.05f}, 1.45f, leafA, 1);
        blob({0.55f, 1.30f, 0.30f}, 0.90f, leafB, 2);
        blob({-0.50f, 1.16f, -0.28f}, 0.85f, leafC, 3);
        blob({-0.05f, 1.82f, -0.10f}, 0.95f, leafB, 4);
        blob({0.32f, 1.60f, -0.34f}, 0.78f, leafC, 5);
        m.MottleFaceColors(0.09f, 7 + variant);
        m.name = "Tree";
        m.ComputeSmoothNormals();
        return m;
    }
    /// A conifer / pine: tapered trunk under overlapping shaded skirts. ~2.4 tall.
    static Mesh Pine(int variant = 0) {
        Mesh m;
        Color bark  = Color::FromBytes(88, 60, 40);
        Color leafA = Color::FromBytes(38, 96, 58);
        Color leafB = Color::FromBytes(52, 116, 66);
        auto R = [&](int k, float lo, float hi) { return PropRand(variant, k, lo, hi); };
        float ht = R(1, 0.90f, 1.20f), wid = R(2, 0.88f, 1.14f);
        m.Add(SweepPath({{0,0,0}, {0.01f,0.45f*ht,0.0f}, {0.02f,0.9f*ht,0.0f}},
                        0.11f, 8, true, 0.05f), {0,0,0}, {1,1,1}, &bark);
        float y = 0.95f * ht;
        m.Add(Cone(0.5f, 1.0f, 12), {0.00f, y,        0.0f}, {1.25f*wid, 0.85f*ht, 1.25f*wid}, &leafA);
        m.Add(Cone(0.5f, 1.0f, 12), {0.01f, y+0.40f*ht, 0.0f}, {1.00f*wid, 0.85f*ht, 1.00f*wid}, &leafB);
        m.Add(Cone(0.5f, 1.0f, 12), {0.00f, y+0.80f*ht, 0.0f}, {0.75f*wid, 0.85f*ht, 0.75f*wid}, &leafA);
        m.Add(Cone(0.5f, 1.0f, 10), {-0.01f, y+1.17f*ht, 0.0f}, {0.48f*wid, 0.80f*ht, 0.48f*wid}, &leafB);
        m.MottleFaceColors(0.08f, 3 + variant);
        m.name = "Pine";
        m.ComputeSmoothNormals();
        return m;
    }
    /// A boulder: faceted low-poly lump, mottled stone, flat-shaded. ~0.8 tall.
    static Mesh Rock(int variant = 0) {
        Mesh m = Icosphere(0.5f, 2);
        auto R = [&](int k, float lo, float hi) { return PropRand(variant, k, lo, hi); };
        // Lump it: a smooth pseudo-random field (phase-shifted per variant) plus
        // per-vertex jitter, squashed by a varying amount.
        float px = R(1, 0.0f, 6.28f), pz = R(2, 0.0f, 6.28f), squash = R(3, 0.54f, 0.72f);
        for (Vec3& v : m.vertices) {
            float n = std::sin(v.x * 7.3f + 1.1f + px) * std::cos(v.z * 6.1f + 2.7f + pz)
                    + std::sin(v.y * 5.7f + 0.5f) * 0.5f;
            float s = 1.0f + 0.22f * n;
            v = {v.x * s, v.y * s * squash, v.z * s};
        }
        m.JitterVertices({}, 0.035f, 11 + variant);
        m.Decimate(0.14f);                              // collapse to chunky facets
        Vec3 lo, hi; m.Bounds(lo, hi);
        for (Vec3& v : m.vertices) v.y -= lo.y;         // rest on the ground
        m.triColors.assign(m.TriangleCount(), Color::FromBytes(124, 118, 110));
        m.MottleFaceColors(0.14f, 5 + variant);         // mottled granite shades
        m.name = "Rock";
        m.normals.clear();                              // flat facets, not a soft blob
        return m;
    }
    /// A small shrub: a two-tone cluster of jittered leafy blobs, ~0.7 tall.
    static Mesh Bush(int variant = 0) {
        Mesh m;
        Color leafA = Color::FromBytes(64, 124, 56);
        Color leafB = Color::FromBytes(84, 142, 64);
        auto R = [&](int k, float lo, float hi) { return PropRand(variant, k, lo, hi); };
        int js = variant * 100 + 1;
        auto blob = [&](Vec3 at, Vec3 s, const Color& c, int k) {
            at.x += R(k*3+10, -0.10f, 0.10f); at.z += R(k*3+12, -0.10f, 0.10f);
            float sm = R(k+20, 0.85f, 1.18f); s = {s.x*sm, s.y*sm, s.z*sm};
            Mesh b = Icosphere(0.5f, 1);
            b.JitterVertices({}, 0.05f, js + k);
            m.Add(b, at, s, &c);
        };
        blob({0.00f, 0.28f, 0.00f},  {0.92f, 0.78f, 0.92f}, leafA, 1);
        blob({-0.30f, 0.20f, 0.12f}, {0.62f, 0.52f, 0.62f}, leafB, 2);
        blob({0.28f, 0.22f, -0.14f}, {0.64f, 0.56f, 0.64f}, leafB, 3);
        blob({0.10f, 0.30f, 0.24f},  {0.50f, 0.44f, 0.50f}, leafA, 4);
        blob({-0.12f, 0.32f, -0.22f},{0.46f, 0.42f, 0.46f}, leafA, 5);
        m.MottleFaceColors(0.09f, 9 + variant);
        m.name = "Bush";
        m.ComputeSmoothNormals();
        return m;
    }

    // ---- Prop library (compound models built from the primitives + modeling ops).
    // Every prop bakes per-face colors, rests on y=0, and keeps its name so it
    // serializes compactly and regenerates on load.

    /// A leaning palm: curved trunk, drooping fronds, coconuts. ~2.6 tall.
    static Mesh PalmTree(int variant = 0) {
        Mesh m;
        Color bark = Color::FromBytes(126, 96, 62);
        Color leaf = Color::FromBytes(62, 138, 66);
        Color coco = Color::FromBytes(92, 70, 46);
        auto R = [&](int k, float lo, float hi) { return PropRand(variant, k, lo, hi); };
        float lean = R(1, 0.36f, 0.66f), ht = R(2, 0.88f, 1.16f);
        int fronds = 6 + (int)(R(3, 0.0f, 2.99f));          // 6..8 fronds
        m.Add(SweepPath({{0,0,0}, {0.10f,0.7f*ht,0.0f}, {0.28f,1.5f*ht,0.0f}, {lean,2.2f*ht,0.0f}},
                        0.12f, 8, true, 0.06f), {0,0,0}, {1,1,1}, &bark);
        Vec3 top{lean, 2.25f * ht, 0.0f};
        for (int i = 0; i < fronds; ++i) {                  // drooping fronds, fanned around
            float a = (float)i * (360.0f / fronds) + R(4, 0.0f, 40.0f);
            Mesh frond = Sphere(0.5f, 5, 8);
            frond.JitterVertices({}, 0.03f, variant * 20 + i + 1);
            m.AddPosed(frond, {top.x + 0.55f, top.y + 0.02f, top.z},
                       {1.35f, 0.05f, 0.28f}, {0.0f, a, -16.0f + R(5, -6.0f, 6.0f)}, top, &leaf);
        }
        m.Add(Sphere(0.5f, 6, 8), {top.x - 0.10f, top.y - 0.10f, top.z + 0.08f}, {0.18f, 0.18f, 0.18f}, &coco);
        m.Add(Sphere(0.5f, 6, 8), {top.x + 0.08f, top.y - 0.12f, top.z - 0.09f}, {0.16f, 0.16f, 0.16f}, &coco);
        m.MottleFaceColors(0.08f, 4 + variant);
        m.name = "PalmTree";
        m.ComputeSmoothNormals();
        return m;
    }
    /// A bare, weathered dead tree: gnarled trunk + reaching branches. ~2 tall.
    static Mesh DeadTree(int variant = 0) {
        Mesh m;
        Color wood = Color::FromBytes(92, 82, 70);
        auto R = [&](int k, float lo, float hi) { return PropRand(variant, k, lo, hi); };
        float ht = R(1, 0.86f, 1.18f), tw = R(2, -0.12f, 0.12f);
        m.Add(SweepPath({{0,0,0}, {0.05f,0.5f*ht,0.03f}, {-0.03f,1.0f*ht,-0.02f}, {0.08f,1.6f*ht,0.04f}},
                        0.13f, 7, true, 0.04f), {0,0,0}, {1,1,1}, &wood);
        m.Add(SweepPath({{0.02f,0.9f*ht,0.0f}, {0.35f+tw,1.25f*ht,0.15f}, {0.60f+tw,1.65f*ht,0.30f}},
                        0.05f, 6, true, 0.015f), {0,0,0}, {1,1,1}, &wood);
        m.Add(SweepPath({{-0.02f,1.15f*ht,0.0f}, {-0.30f-tw,1.45f*ht,-0.12f}, {-0.55f-tw,1.85f*ht,-0.20f}},
                        0.045f, 6, true, 0.015f), {0,0,0}, {1,1,1}, &wood);
        m.Add(SweepPath({{0.05f,1.45f*ht,0.02f}, {0.18f,1.80f*ht,-0.15f}, {0.25f,2.05f*ht,-0.28f}},
                        0.035f, 5, true, 0.012f), {0,0,0}, {1,1,1}, &wood);
        m.MottleFaceColors(0.10f, 6 + variant);
        m.name = "DeadTree";
        m.ComputeSmoothNormals();
        return m;
    }
    /// A toadstool: pale stem, red spotted cap. ~0.55 tall.
    static Mesh Mushroom(int variant = 0) {
        Mesh m;
        Color stem = Color::FromBytes(228, 218, 196);
        Color cap  = Color::FromBytes(188, 52, 44);
        auto R = [&](int k, float lo, float hi) { return PropRand(variant, k, lo, hi); };
        float sh = R(1, 0.80f, 1.30f), cw = R(2, 0.80f, 1.20f);   // stem height, cap width
        m.Add(SweepPath({{0,0,0}, {0.01f,0.18f*sh,0.0f}, {0.0f,0.34f*sh,0.0f}},
                        0.09f, 10, true, 0.06f), {0,0,0}, {1,1,1}, &stem);
        int capStart = m.TriangleCount();
        m.Add(Hemisphere(0.5f, 6, 14), {0.0f, 0.32f*sh, 0.0f}, {0.62f*cw, 0.42f, 0.62f*cw}, &cap);
        for (int f = capStart; f < m.TriangleCount(); ++f) {     // white spots (seed-shifted)
            unsigned x = (unsigned)((f + variant * 7) * 2654435761u);
            if (((x >> 7) & 7) == 0) m.triColors[f] = Color::FromBytes(240, 234, 222);
        }
        m.MottleFaceColors(0.05f, 2 + variant);
        m.name = "Mushroom";
        m.ComputeSmoothNormals();
        return m;
    }
    /// A saguaro cactus: ribbed body + two elbow arms. ~1.6 tall.
    static Mesh Cactus(int variant = 0) {
        Mesh m;
        Color green = Color::FromBytes(74, 128, 58);
        auto R = [&](int k, float lo, float hi) { return PropRand(variant, k, lo, hi); };
        float ht = R(1, 0.82f, 1.22f), la = R(2, 0.85f, 1.10f), ra = R(3, 0.82f, 1.12f);
        m.Add(SweepPath({{0,0,0}, {0,0.6f*ht,0}, {0,1.2f*ht,0}}, 0.18f, 10, true, 0.14f),
              {0,0,0}, {1,1,1}, &green);
        m.Add(Sphere(0.5f, 6, 10), {0.0f, 1.22f*ht, 0.0f}, {0.29f, 0.29f, 0.29f}, &green);
        m.Add(SweepPath({{0.12f,0.55f*ht,0.0f}, {0.38f,0.60f*ht,0.0f}, {0.42f,0.95f*ht*la,0.0f}},
                        0.10f, 8, true, 0.08f), {0,0,0}, {1,1,1}, &green);
        m.Add(SweepPath({{-0.12f,0.75f*ht,0.0f}, {-0.36f,0.80f*ht,0.0f}, {-0.40f,1.10f*ht*ra,0.0f}},
                        0.09f, 8, true, 0.075f), {0,0,0}, {1,1,1}, &green);
        m.MottleFaceColors(0.07f, 8 + variant);
        m.name = "Cactus";
        m.ComputeSmoothNormals();
        return m;
    }
    /// A wooden barrel with iron bands. ~0.9 tall.
    static Mesh Barrel() {
        Mesh m;
        Color wood = Color::FromBytes(140, 100, 62);
        Color iron = Color::FromBytes(70, 72, 78);
        m.Add(Lathe({{0.001f,0.0f}, {0.30f,0.02f}, {0.36f,0.25f}, {0.38f,0.45f},
                     {0.36f,0.65f}, {0.30f,0.88f}, {0.001f,0.90f}}, 14),
              {0,0,0}, {1,1,1}, &wood);
        m.Add(Tube(0.5f, 0.46f, 1.0f, 14), {0.0f, 0.18f, 0.0f}, {0.76f, 0.05f, 0.76f}, &iron);
        m.Add(Tube(0.5f, 0.46f, 1.0f, 14), {0.0f, 0.72f, 0.0f}, {0.76f, 0.05f, 0.76f}, &iron);
        m.MottleFaceColors(0.08f, 3);
        m.name = "Barrel";
        m.ComputeSmoothNormals();
        return m;
    }
    /// A slatted shipping crate with corner braces. 0.8 cube.
    static Mesh Crate() {
        Mesh m;
        Color plank = Color::FromBytes(160, 122, 76);
        Color brace = Color::FromBytes(112, 82, 50);
        m.Add(Cube(1.0f), {0.0f, 0.40f, 0.0f}, {0.76f, 0.76f, 0.76f}, &plank);
        const float h = 0.40f, e = 0.40f, t = 0.05f;     // centre height, half-extent, beam thickness
        for (int sx = -1; sx <= 1; sx += 2)              // 4 vertical corner beams
            for (int sz = -1; sz <= 1; sz += 2)
                m.Add(Cube(1.0f), {e * sx, h, e * sz}, {t, 0.82f, t}, &brace);
        for (int sy = -1; sy <= 1; sy += 2)              // top + bottom frames
            for (int sz = -1; sz <= 1; sz += 2)
                m.Add(Cube(1.0f), {0.0f, h + e * sy, e * sz}, {0.82f, t, t}, &brace);
        for (int sy = -1; sy <= 1; sy += 2)
            for (int sx = -1; sx <= 1; sx += 2)
                m.Add(Cube(1.0f), {e * sx, h + e * sy, 0.0f}, {t, t, 0.82f}, &brace);
        m.MottleFaceColors(0.09f, 12);
        m.name = "Crate";
        m.normals.clear();                               // crisp flat planks
        return m;
    }
    /// A two-rail wooden fence segment, ~2 wide, ~1 tall. Array X to extend.
    static Mesh Fence() {
        Mesh m;
        Color wood = Color::FromBytes(134, 100, 64);
        for (int i = -1; i <= 1; ++i)                    // 3 posts
            m.Add(Cube(1.0f), {(float)i * 0.9f, 0.5f, 0.0f}, {0.10f, 1.0f, 0.10f}, &wood);
        m.Add(Cube(1.0f), {0.0f, 0.78f, 0.0f}, {2.0f, 0.08f, 0.06f}, &wood);   // rails
        m.Add(Cube(1.0f), {0.0f, 0.42f, 0.0f}, {2.0f, 0.08f, 0.06f}, &wood);
        m.MottleFaceColors(0.10f, 4);
        m.name = "Fence";
        m.normals.clear();
        return m;
    }
    /// A stone well: mottled ring, posts, and a little pitched roof. ~1.5 tall.
    static Mesh Well() {
        Mesh m;
        Color stone = Color::FromBytes(136, 132, 126);
        Color wood  = Color::FromBytes(110, 82, 54);
        Color roofC = Color::FromBytes(150, 66, 48);
        m.Add(Tube(0.5f, 0.38f, 1.0f, 14), {0.0f, 0.25f, 0.0f}, {1.2f, 0.5f, 1.2f}, &stone);
        m.Add(Cube(1.0f), {-0.52f, 0.85f, 0.0f}, {0.09f, 1.2f, 0.09f}, &wood);   // posts
        m.Add(Cube(1.0f), {0.52f, 0.85f, 0.0f}, {0.09f, 1.2f, 0.09f}, &wood);
        m.Add(Cube(1.0f), {0.0f, 1.02f, 0.0f}, {1.1f, 0.05f, 0.05f}, &wood);     // axle bar
        m.Add(Pyramid(1.0f), {0.0f, 1.62f, 0.0f}, {1.5f, 0.55f, 1.5f}, &roofC);  // roof
        m.MottleFaceColors(0.11f, 15);
        m.name = "Well";
        m.normals.clear();
        return m;
    }
    /// A street lamp: curved post with a warm glowing head. ~2.6 tall.
    static Mesh StreetLamp() {
        Mesh m;
        Color iron = Color::FromBytes(52, 54, 60);
        Color glow = Color::FromBytes(255, 226, 150);
        m.Add(Cylinder(0.5f, 1.0f, 10), {0.0f, 0.05f, 0.0f}, {0.34f, 0.1f, 0.34f}, &iron); // base
        m.Add(SweepPath({{0,0.05f,0}, {0,1.2f,0}, {0,2.2f,0}, {0.12f,2.45f,0}, {0.35f,2.55f,0}},
                        0.05f, 8, true, 0.035f), {0,0,0}, {1,1,1}, &iron);
        m.Add(Cube(1.0f), {0.38f, 2.38f, 0.0f}, {0.16f, 0.22f, 0.16f}, &glow);   // lamp head
        m.Add(Pyramid(1.0f), {0.38f, 2.52f, 0.0f}, {0.24f, 0.10f, 0.24f}, &iron);
        m.name = "StreetLamp";
        m.ComputeSmoothNormals();
        return m;
    }
    /// A crystal cluster: tilted shards in two tones, flat-shaded. ~0.9 tall.
    static Mesh Crystal(int variant = 0) {
        Mesh m;
        Color a = Color::FromBytes(120, 190, 230);
        Color b = Color::FromBytes(150, 130, 224);
        auto R = [&](int k, float lo, float hi) { return PropRand(variant, k, lo, hi); };
        float h0 = R(1, 0.78f, 1.05f);                       // main-shard height
        m.AddPosed(Bipyramid(6, 0.5f, 1.0f), {0.0f, 0.42f*h0, 0.0f}, {0.34f, 0.9f*h0, 0.34f},
                   {4.0f, R(2, 0.0f, 60.0f), -6.0f}, {0.0f, 0.0f, 0.0f}, &a);
        m.AddPosed(Bipyramid(6, 0.5f, 1.0f), {0.24f, 0.28f, 0.10f}, {0.22f, 0.6f*R(3,0.8f,1.2f), 0.22f},
                   {8.0f, R(4, 0.0f, 90.0f), 22.0f}, {0.24f, 0.0f, 0.10f}, &b);
        m.AddPosed(Bipyramid(6, 0.5f, 1.0f), {-0.22f, 0.24f, -0.06f}, {0.18f, 0.5f*R(5,0.8f,1.2f), 0.18f},
                   {-10.0f, R(6, 30.0f, 120.0f), -24.0f}, {-0.22f, 0.0f, -0.06f}, &a);
        m.AddPosed(Bipyramid(6, 0.5f, 1.0f), {0.05f, 0.20f, -0.24f}, {0.15f, 0.4f, 0.15f},
                   {-18.0f, R(7, 90.0f, 200.0f), 10.0f}, {0.05f, 0.0f, -0.24f}, &b);
        m.MottleFaceColors(0.10f, 21 + variant);
        m.name = "Crystal";
        m.normals.clear();                               // gem facets
        return m;
    }
    /// A cottage: walls, hipped roof, door, windows, chimney. ~2.2 tall, 3 wide.
    static Mesh House() {
        Mesh m;
        Color wall = Color::FromBytes(214, 202, 178);
        Color roofC = Color::FromBytes(148, 74, 56);
        Color door = Color::FromBytes(96, 66, 40);
        Color win  = Color::FromBytes(150, 196, 220);
        Color chim = Color::FromBytes(120, 110, 104);
        m.Add(Cube(1.0f), {0.0f, 0.65f, 0.0f}, {3.0f, 1.3f, 2.2f}, &wall);
        m.Add(Pyramid(1.0f), {0.0f, 1.72f, 0.0f}, {3.4f, 0.85f, 2.6f}, &roofC);
        m.Add(Cube(1.0f), {0.0f, 0.42f, 1.11f}, {0.55f, 0.85f, 0.06f}, &door);
        m.Add(Cube(1.0f), {-0.95f, 0.72f, 1.11f}, {0.5f, 0.45f, 0.05f}, &win);
        m.Add(Cube(1.0f), {0.95f, 0.72f, 1.11f}, {0.5f, 0.45f, 0.05f}, &win);
        m.Add(Cube(1.0f), {0.9f, 2.0f, -0.4f}, {0.28f, 0.7f, 0.28f}, &chim);
        m.MottleFaceColors(0.06f, 18);
        m.name = "House";
        m.normals.clear();
        return m;
    }
    /// A stone watchtower with a conical roof. ~3.4 tall.
    static Mesh Tower() {
        Mesh m;
        Color stone = Color::FromBytes(140, 136, 128);
        Color roofC = Color::FromBytes(90, 100, 140);
        Color door  = Color::FromBytes(88, 62, 40);
        m.Add(Cylinder(0.5f, 1.0f, 14), {0.0f, 1.25f, 0.0f}, {1.5f, 2.5f, 1.5f}, &stone);
        m.Add(Cylinder(0.5f, 1.0f, 14), {0.0f, 2.60f, 0.0f}, {1.7f, 0.25f, 1.7f}, &stone); // parapet lip
        m.Add(Cone(0.5f, 1.0f, 14), {0.0f, 3.10f, 0.0f}, {1.75f, 0.9f, 1.75f}, &roofC);
        m.Add(Cube(1.0f), {0.0f, 0.5f, 0.74f}, {0.5f, 1.0f, 0.1f}, &door);
        m.MottleFaceColors(0.10f, 23);
        m.name = "Tower";
        m.normals.clear();
        return m;
    }
    /// A windmill: tapered stone body, conical cap, four angled sail blades. ~3.4 tall.
    static Mesh Windmill() {
        Mesh m;
        Color stone = Color::FromBytes(206, 198, 180);
        Color roofC = Color::FromBytes(120, 74, 54);
        Color sail  = Color::FromBytes(150, 120, 84);
        m.Add(Lathe({{0.9f,0.0f}, {0.85f,0.6f}, {0.72f,1.4f}, {0.6f,2.2f}, {0.55f,2.5f}, {0.001f,2.5f}}, 16),
              {0,0,0}, {1,1,1}, &stone);
        m.Add(Cone(0.5f, 1.0f, 16), {0.0f, 2.75f, 0.0f}, {1.4f, 0.7f, 1.4f}, &roofC);
        Vec3 hub{0.0f, 2.35f, 0.62f};
        for (int i = 0; i < 4; ++i)                        // four sails around Z
            m.AddPosed(Cube(1.0f), {hub.x, hub.y + 0.9f, hub.z}, {0.16f, 1.8f, 0.05f},
                       {0.0f, 0.0f, (float)i * 90.0f}, hub, &sail);
        m.MottleFaceColors(0.07f, 31);
        m.name = "Windmill";
        m.normals.clear();
        return m;
    }
    /// An arched plank footbridge with side rails. ~3 long, ~1 tall.
    static Mesh Bridge() {
        Mesh m;
        Color wood = Color::FromBytes(140, 104, 66);
        // Deck: planks following a shallow arc across X.
        const int planks = 11;
        auto deckY = [](float t) { return 0.12f + 0.5f * std::sin(t * 3.14159f); }; // ends on ground, arch up
        for (int i = 0; i < planks; ++i) {
            float t = (float)i / (planks - 1);
            m.Add(Cube(1.0f), {-1.4f + 2.8f * t, deckY(t), 0.0f}, {0.26f, 0.06f, 1.2f}, &wood);
        }
        for (int s = -1; s <= 1; s += 2)                   // two side rails + posts
            for (int i = 0; i < planks; i += 2) {
                float t = (float)i / (planks - 1);
                m.Add(Cube(1.0f), {-1.4f + 2.8f * t, deckY(t) + 0.28f, s * 0.55f}, {0.06f, 0.5f, 0.06f}, &wood);
            }
        for (int s = -1; s <= 1; s += 2)                   // rail caps (swept arc)
            m.Add(SweepPath({{-1.4f,deckY(0)+0.45f,s*0.55f}, {0.0f,deckY(0.5f)+0.45f,s*0.55f},
                             {1.4f,deckY(1)+0.45f,s*0.55f}}, 0.05f, 6, true), {0,0,0}, {1,1,1}, &wood);
        m.MottleFaceColors(0.08f, 33);
        m.name = "Bridge";
        m.normals.clear();
        return m;
    }
    /// A natural stone arch: two tapered legs joined by a swept span. ~2.4 tall.
    static Mesh RockArch() {
        Mesh m;
        Color stone = Color::FromBytes(150, 130, 104);
        m.Add(SweepPath({{-1.0f,0.0f,0.0f}, {-1.05f,0.9f,0.0f}, {-0.7f,1.7f,0.0f},
                         {-0.2f,2.15f,0.0f}, {0.4f,2.3f,0.0f}, {1.0f,2.0f,0.0f}, {1.15f,1.1f,0.0f},
                         {1.0f,0.2f,0.0f}, {0.98f,0.0f,0.0f}}, 0.42f, 10, true, 0.30f),
              {0,0,0}, {1,1,1}, &stone);
        m.JitterVertices({}, 0.05f, 41);                   // rough it up
        m.triColors.assign(m.TriangleCount(), stone);
        m.MottleFaceColors(0.13f, 42);
        m.name = "RockArch";
        m.normals.clear();
        return m;
    }
    /// A campfire: ring of stones, crossed logs, an emissive flame. ~0.6 tall.
    static Mesh Campfire() {
        Mesh m;
        Color stone = Color::FromBytes(120, 116, 110);
        Color log   = Color::FromBytes(96, 66, 42);
        Color flame = Color::FromBytes(255, 150, 40);      // emissive-friendly warm
        const float kPi = 3.14159265f;
        for (int i = 0; i < 8; ++i) {                      // stone ring
            float a = 2.0f * kPi * i / 8.0f;
            m.Add(Icosphere(0.5f, 1), {std::cos(a) * 0.5f, 0.09f, std::sin(a) * 0.5f},
                  {0.22f, 0.18f, 0.22f}, &stone);
        }
        m.AddPosed(Cylinder(0.5f, 1.0f, 6), {0.0f, 0.14f, 0.0f}, {0.10f, 0.7f, 0.10f},
                   {0.0f, 0.0f, 90.0f}, {0,0,0}, &log);     // crossed logs
        m.AddPosed(Cylinder(0.5f, 1.0f, 6), {0.0f, 0.20f, 0.0f}, {0.10f, 0.7f, 0.10f},
                   {90.0f, 0.0f, 0.0f}, {0,0,0}, &log);
        m.Add(Cone(0.5f, 1.0f, 8), {0.0f, 0.28f, 0.0f}, {0.34f, 0.6f, 0.34f}, &flame);
        m.name = "Campfire";
        m.ComputeSmoothNormals();
        return m;
    }
    /// A hanging lantern: metal cage, warm glass core, top ring. ~0.6 tall.
    static Mesh Lantern() {
        Mesh m;
        Color iron = Color::FromBytes(60, 60, 66);
        Color glow = Color::FromBytes(255, 224, 150);
        m.Add(Tube(0.5f, 0.42f, 1.0f, 8), {0.0f, 0.28f, 0.0f}, {0.5f, 0.5f, 0.5f}, &iron);
        m.Add(Cube(1.0f), {0.0f, 0.28f, 0.0f}, {0.20f, 0.32f, 0.20f}, &glow);   // glass core
        m.Add(Cone(0.5f, 1.0f, 8), {0.0f, 0.52f, 0.0f}, {0.34f, 0.18f, 0.34f}, &iron);
        m.Add(Torus(0.09f, 0.03f, 8, 6), {0.0f, 0.62f, 0.0f}, {1.0f, 1.0f, 1.0f}, &iron);
        m.name = "Lantern";
        m.ComputeSmoothNormals();
        return m;
    }
    /// A treasure chest: wooden box, curved lid, iron bands + lock. ~0.7 tall.
    static Mesh Chest() {
        Mesh m;
        Color wood = Color::FromBytes(120, 82, 48);
        Color iron = Color::FromBytes(78, 74, 66);
        Color gold = Color::FromBytes(214, 176, 72);
        m.Add(Cube(1.0f), {0.0f, 0.25f, 0.0f}, {1.0f, 0.5f, 0.66f}, &wood);      // body
        m.Add(SweepPath({{-0.5f,0.5f,0.0f}, {0.0f,0.72f,0.0f}, {0.5f,0.5f,0.0f}}, // domed lid
                        0.33f, 8, true), {0,0,0}, {1.0f, 1.0f, 1.0f}, &wood);
        for (int s = -1; s <= 1; s += 2)                    // iron bands
            m.Add(Cube(1.0f), {s * 0.35f, 0.30f, 0.0f}, {0.07f, 0.62f, 0.70f}, &iron);
        m.Add(Cube(1.0f), {0.0f, 0.44f, 0.34f}, {0.14f, 0.16f, 0.05f}, &gold);   // lock
        m.MottleFaceColors(0.06f, 51);
        m.name = "Chest";
        m.normals.clear();
        return m;
    }
    /// A signpost: post with an angled board. ~1.5 tall.
    static Mesh Signpost() {
        Mesh m;
        Color wood  = Color::FromBytes(128, 92, 56);
        Color board = Color::FromBytes(168, 132, 84);
        m.Add(Cylinder(0.5f, 1.0f, 8), {0.0f, 0.75f, 0.0f}, {0.12f, 1.5f, 0.12f}, &wood);
        m.AddPosed(Cube(1.0f), {0.22f, 1.15f, 0.0f}, {0.7f, 0.34f, 0.06f},
                   {0.0f, 0.0f, -6.0f}, {0.0f, 1.15f, 0.0f}, &board);
        m.MottleFaceColors(0.07f, 55);
        m.name = "Signpost";
        m.normals.clear();
        return m;
    }
    /// A wooden hand-cart: bed, side rails, two wheels. ~1 long.
    static Mesh Cart() {
        Mesh m;
        Color wood = Color::FromBytes(134, 96, 58);
        Color iron = Color::FromBytes(60, 58, 60);
        m.Add(Cube(1.0f), {0.0f, 0.42f, 0.0f}, {1.1f, 0.12f, 0.66f}, &wood);     // bed
        for (int s = -1; s <= 1; s += 2)
            m.Add(Cube(1.0f), {0.0f, 0.58f, s * 0.32f}, {1.1f, 0.22f, 0.05f}, &wood);
        m.Add(Cube(1.0f), {-0.55f, 0.58f, 0.0f}, {0.05f, 0.22f, 0.66f}, &wood);
        for (int s = -1; s <= 1; s += 2)                    // wheels
            m.AddPosed(Torus(0.26f, 0.07f, 12, 8), {0.30f, 0.26f, s * 0.38f}, {1.0f, 1.0f, 1.0f},
                       {90.0f, 0.0f, 0.0f}, {0.30f, 0.26f, s * 0.38f}, &iron);
        m.MottleFaceColors(0.07f, 57);
        m.name = "Cart";
        m.normals.clear();
        return m;
    }
    /// A camping tent: ridged canvas with a dark door flap. ~1 tall.
    static Mesh Tent() {
        Mesh m;
        Color canvas = Color::FromBytes(168, 96, 64);
        Color dark   = Color::FromBytes(60, 40, 32);
        // Triangular prism lying along Z (ridge tent) via Prism(3) rotated.
        m.AddPosed(Prism(3, 0.5f, 1.0f), {0.0f, 0.0f, 0.0f}, {1.7f, 1.7f, 1.9f},
                   {0.0f, 0.0f, 0.0f}, {0,0,0}, &canvas);
        Vec3 lo, hi; m.Bounds(lo, hi);
        for (Vec3& v : m.vertices) v.y -= lo.y;             // ground it
        m.Add(Cube(1.0f), {0.0f, 0.35f, 0.92f}, {0.34f, 0.7f, 0.05f}, &dark);   // door
        m.MottleFaceColors(0.06f, 61);
        m.name = "Tent";
        m.normals.clear();
        return m;
    }
    /// A park bench: slatted seat + back on two legs. ~0.8 tall, ~1.6 wide.
    static Mesh Bench() {
        Mesh m;
        Color wood = Color::FromBytes(146, 108, 68);
        Color iron = Color::FromBytes(54, 56, 60);
        for (int i = 0; i < 3; ++i)                         // seat slats
            m.Add(Cube(1.0f), {0.0f, 0.42f, -0.18f + i * 0.16f}, {1.6f, 0.05f, 0.13f}, &wood);
        for (int i = 0; i < 3; ++i)                         // back slats
            m.AddPosed(Cube(1.0f), {0.0f, 0.68f + i * 0.16f, -0.28f}, {1.6f, 0.05f, 0.13f},
                       {-24.0f, 0.0f, 0.0f}, {0.0f, 0.42f, -0.28f}, &wood);
        for (int s = -1; s <= 1; s += 2) {                  // end legs
            m.Add(Cube(1.0f), {s * 0.7f, 0.2f, 0.0f}, {0.08f, 0.4f, 0.5f}, &iron);
            m.Add(Cube(1.0f), {s * 0.7f, 0.62f, -0.28f}, {0.08f, 0.5f, 0.06f}, &iron);
        }
        m.MottleFaceColors(0.06f, 63);
        m.name = "Bench";
        m.normals.clear();
        return m;
    }

    /// True if this named prop supports procedural variants (FromNameSeeded).
    static bool NameHasVariants(const std::string& n) {
        return n == "Tree" || n == "Pine" || n == "Rock" || n == "Bush"
            || n == "PalmTree" || n == "DeadTree" || n == "Mushroom"
            || n == "Cactus" || n == "Crystal";
    }

    /// Like FromName, but for the nature props that support it, `variant` picks a
    /// procedurally-varied version (0 = the canonical base model). Used so a
    /// scattered forest isn't hundreds of identical clones; the variant is stored
    /// per object and regenerates the exact same shape on load.
    static Mesh FromNameSeeded(const std::string& n, int variant) {
        if (variant != 0) {
            if (n == "Tree") return Tree(variant);
            if (n == "Pine") return Pine(variant);
            if (n == "Rock") return Rock(variant);
            if (n == "Bush") return Bush(variant);
            if (n == "PalmTree") return PalmTree(variant);
            if (n == "DeadTree") return DeadTree(variant);
            if (n == "Mushroom") return Mushroom(variant);
            if (n == "Cactus")   return Cactus(variant);
            if (n == "Crystal")  return Crystal(variant);
        }
        return FromName(n);
    }

    /// Recreate a primitive mesh from its name.
    static Mesh FromName(const std::string& n) {
        if (n == "Tree")      return Tree();
        if (n == "Pine")      return Pine();
        if (n == "Rock")      return Rock();
        if (n == "Bush")      return Bush();
        if (n == "PalmTree")  return PalmTree();
        if (n == "DeadTree")  return DeadTree();
        if (n == "Mushroom")  return Mushroom();
        if (n == "Cactus")    return Cactus();
        if (n == "Barrel")    return Barrel();
        if (n == "Crate")     return Crate();
        if (n == "Fence")     return Fence();
        if (n == "Well")      return Well();
        if (n == "StreetLamp") return StreetLamp();
        if (n == "Crystal")   return Crystal();
        if (n == "House")     return House();
        if (n == "Tower")     return Tower();
        if (n == "Windmill")  return Windmill();
        if (n == "Bridge")    return Bridge();
        if (n == "RockArch")  return RockArch();
        if (n == "Campfire")  return Campfire();
        if (n == "Lantern")   return Lantern();
        if (n == "Chest")     return Chest();
        if (n == "Signpost")  return Signpost();
        if (n == "Cart")      return Cart();
        if (n == "Tent")      return Tent();
        if (n == "Bench")     return Bench();
        if (n == "Pyramid")   return Pyramid();
        if (n == "Quad")      return Quad();
        if (n == "Plane")     return Plane();
        if (n == "Sphere")    return Sphere();
        if (n == "Cylinder")  return Cylinder();
        if (n == "Cone")      return Cone();
        if (n == "Torus")     return Torus();
        if (n == "TorusKnot") return TorusKnot();
        if (n == "Capsule")   return Capsule();
        if (n == "Icosphere") return Icosphere();
        if (n == "Grid")      return Grid();
        if (n == "Wedge")     return Wedge();
        if (n == "Tube")      return Tube();
        if (n == "Hemisphere") return Hemisphere();
        if (n == "Stairs")    return Stairs();
        if (n == "Gear")      return Gear();
        if (n == "Prism")     return Prism();
        if (n == "Octahedron") return Octahedron();
        if (n == "Disc")      return Disc();
        if (n == "Tetrahedron") return Tetrahedron();
        if (n == "Bipyramid") return Bipyramid();
        if (n == "RoundedBox") return RoundedBox();
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

    /// Normalize an imported mesh's size in place. Exporters disagree on units —
    /// Meshy AI / FBX / CAD tools often emit centimeters or millimeters, making a
    /// character 180 "meters" tall. If the largest dimension is implausibly big
    /// (> 20 units) or microscopic (< 0.02), scale the VERTICES so it lands at
    /// ~2 units. Deterministic per file, so re-loading a scene can't compound it.
    static void NormalizeImportScale(Mesh& m) {
        if (m.vertices.empty()) return;
        Vec3 lo, hi; m.Bounds(lo, hi);
        Vec3 sz = hi - lo;
        float d = sz.x > sz.y ? (sz.x > sz.z ? sz.x : sz.z) : (sz.y > sz.z ? sz.y : sz.z);
        if (d <= 0.0f || (d <= 20.0f && d >= 0.02f)) return;
        float s = 2.0f / d;
        for (Vec3& v : m.vertices) v = v * s;
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
        // Read the whole file once and parse it with a pointer walk. The previous
        // getline + istringstream version spent 10-100x longer in stream tokenizing
        // (a multi-second freeze on real exported models).
        std::string data;
        {
            std::FILE* fp = std::fopen(path.c_str(), "rb");
            if (!fp) { if (ok) *ok = false; return m; }
            std::fseek(fp, 0, SEEK_END); long sz = std::ftell(fp); std::fseek(fp, 0, SEEK_SET);
            if (sz > 0) {
                data.resize((std::size_t)sz);
                if (std::fread(&data[0], 1, (std::size_t)sz, fp) != (std::size_t)sz) {
                    std::fclose(fp); if (ok) *ok = false; return m;
                }
            }
            std::fclose(fp);
        }
        std::string dir;
        { std::size_t s = path.find_last_of("/\\"); if (s != std::string::npos) dir = path.substr(0, s + 1); }
        std::vector<Vec3> pos, nrm; std::vector<Vec2> uv;
        struct Corner { int p, t, n; };                        // 0-based pos / uv / normal (-1 = none)
        std::vector<std::vector<Corner>> faces;
        std::string mtllib;
        const char* p = data.c_str();
        const char* end = p + data.size();
        // Bounded number readers: skip spaces/tabs only — NEVER a newline, so a short
        // line can't consume the next line's numbers (strtof alone would).
        auto readF = [&](const char*& c) -> float {
            while (c < end && (*c == ' ' || *c == '\t' || *c == '\r')) ++c;
            if (c >= end || *c == '\n') return 0.0f;
            char* q = nullptr; float v = std::strtof(c, &q); c = q; return v;
        };
        while (p < end) {
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) ++p;
            if (p < end) {
                if (p[0] == 'v' && p + 1 < end && (p[1] == ' ' || p[1] == '\t')) {
                    p += 2; Vec3 v; v.x = readF(p); v.y = readF(p); v.z = readF(p); pos.push_back(v);
                } else if (p[0] == 'v' && p + 2 < end && p[1] == 't' && (p[2] == ' ' || p[2] == '\t')) {
                    p += 3; Vec2 t; t.x = readF(p); t.y = readF(p); uv.push_back(t);
                } else if (p[0] == 'v' && p + 2 < end && p[1] == 'n' && (p[2] == ' ' || p[2] == '\t')) {
                    p += 3; Vec3 n; n.x = readF(p); n.y = readF(p); n.z = readF(p); nrm.push_back(n);
                } else if (p[0] == 'f' && p + 1 < end && (p[1] == ' ' || p[1] == '\t')) {
                    p += 2;
                    std::vector<Corner> face; face.reserve(4);
                    for (;;) {
                        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) ++p;
                        if (p >= end || *p == '\n') break;
                        char* q = nullptr;
                        long vi = std::strtol(p, &q, 10); if (q == p) break; p = q;
                        long ti = 0, ni = 0;
                        if (p < end && *p == '/') {                 // v/vt, v//vn or v/vt/vn
                            ++p;
                            if (p < end && *p == '/') { ++p; ni = std::strtol(p, &q, 10); p = q; }
                            else {
                                ti = std::strtol(p, &q, 10); p = q;
                                if (p < end && *p == '/') { ++p; ni = std::strtol(p, &q, 10); p = q; }
                            }
                        }
                        if (vi < 0) vi = (long)pos.size() + vi + 1;
                        if (ti < 0) ti = (long)uv.size() + ti + 1;
                        if (ni < 0) ni = (long)nrm.size() + ni + 1;
                        face.push_back({(int)vi - 1, (int)ti - 1, (int)ni - 1});
                    }
                    if (face.size() >= 3) faces.push_back(std::move(face));
                } else if (end - p >= 7 && std::strncmp(p, "mtllib", 6) == 0 &&
                           (p[6] == ' ' || p[6] == '\t')) {
                    p += 7;
                    while (p < end && (*p == ' ' || *p == '\t')) ++p;
                    const char* s = p;
                    while (p < end && *p != '\n' && *p != '\r') ++p;
                    mtllib.assign(s, p);
                }
            }
            while (p < end && *p != '\n') ++p;                    // to end of line
            if (p < end) ++p;
        }
        auto tri = [&](int a, int b, int cc) { m.triangles.insert(m.triangles.end(), {a, b, cc}); };
        if (uv.empty() && nrm.empty()) {               // simplest case: 1:1 with v lines
            m.vertices = pos;
            for (auto& fc : faces)
                for (std::size_t i = 2; i < fc.size(); ++i) tri(fc[0].p, fc[i - 1].p, fc[i].p);
        } else {                                       // de-index per (pos,uv,normal) so attributes map right
            const bool haveUV = !uv.empty(), haveN = !nrm.empty();
            // Hash map keyed on the packed corner: the ordered std::map<tuple> this
            // replaces was the second big import cost (log-n rebalancing per corner).
            struct CKey { int p, t, n; bool operator==(const CKey& o) const { return p == o.p && t == o.t && n == o.n; } };
            struct CKeyHash {
                std::size_t operator()(const CKey& k) const {
                    std::size_t h = 1469598103934665603ull;
                    auto mix = [&](long v) { h ^= (std::size_t)(v + 1); h *= 1099511628211ull; };
                    mix(k.p); mix(k.t); mix(k.n); return h;
                }
            };
            std::unordered_map<CKey, int, CKeyHash> remap;
            remap.reserve(pos.size() * 2 + 16);
            auto corner = [&](const Corner& c) {
                CKey key{c.p, c.t, c.n};
                auto it = remap.find(key); if (it != remap.end()) return it->second;
                int ni = (int)m.vertices.size();
                m.vertices.push_back((c.p >= 0 && c.p < (int)pos.size()) ? pos[c.p] : Vec3{0, 0, 0});
                if (haveUV) m.uvs.push_back((c.t >= 0 && c.t < (int)uv.size()) ? uv[c.t] : Vec2{0, 0});
                if (haveN)  m.normals.push_back((c.n >= 0 && c.n < (int)nrm.size()) ? nrm[c.n] : Vec3{0, 1, 0});
                remap[key] = ni; return ni;
            };
            std::vector<int> idx;
            for (auto& fc : faces) {
                idx.clear(); for (auto& c : fc) idx.push_back(corner(c));
                for (std::size_t i = 2; i < idx.size(); ++i) tri(idx[0], idx[i - 1], idx[i]);
            }
        }
        NormalizeImportScale(m);   // cm/mm exports (Meshy AI etc.) land at a usable size
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
        const bool hadUV = uvs.size() == vertices.size() && !uvs.empty();
        if (hadUV)
            for (const Vec2& t : uvs) f << "vt " << t.x << " " << t.y << "\n";
        for (std::size_t i = 0; i + 2 < triangles.size(); i += 3) {
            int a = triangles[i] + 1, b = triangles[i + 1] + 1, c = triangles[i + 2] + 1;
            if (hadUV)   // per-vertex UVs share the position index
                f << "f " << a << "/" << a << " " << b << "/" << b << " " << c << "/" << c << "\n";
            else
                f << "f " << a << " " << b << " " << c << "\n";
        }
        return static_cast<bool>(f);
    }

    // ---- UV projection (quick unwraps for texturing edited meshes) -----------
    // All of these fill the per-vertex `uvs` array and mark the mesh custom
    // (clear `name`) so the projected UVs are what gets saved, not a primitive's
    // regenerated defaults. `tile` = how many texture repeats across the bounds.

    /// Planar projection along one axis (0=X 1=Y 2=Z): the other two coordinates
    /// map straight to (u, v). Ideal for floors/walls/flat props.
    void ProjectUVPlanar(int axis, float tile = 1.0f) {
        if (vertices.empty() || axis < 0 || axis > 2) return;
        Vec3 lo, hi; Bounds(lo, hi);
        Vec3 size = hi - lo;
        auto span = [&](int a) { float s = (&size.x)[a]; return s > 1e-6f ? s : 1.0f; };
        int ua = (axis == 0) ? 2 : 0, va = (axis == 1) ? 2 : 1;
        uvs.resize(vertices.size());
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            const Vec3& p = vertices[i];
            uvs[i] = {((&p.x)[ua] - (&lo.x)[ua]) / span(ua) * tile,
                      ((&p.x)[va] - (&lo.x)[va]) / span(va) * tile};
        }
        name = "";
    }

    /// Box (tri-planar) projection: each vertex projects along its dominant
    /// normal axis, so every side of a blocky shape gets sensible UVs. The go-to
    /// unwrap for buildings and hand-edited geometry.
    void ProjectUVBox(float tile = 1.0f) {
        if (vertices.empty()) return;
        Vec3 lo, hi; Bounds(lo, hi);
        Vec3 size = hi - lo;
        auto span = [&](int a) { float s = (&size.x)[a]; return s > 1e-6f ? s : 1.0f; };
        std::vector<Vec3> vn = Normals();
        uvs.resize(vertices.size());
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            const Vec3& nrm = vn[i]; const Vec3& p = vertices[i];
            float ax = std::fabs(nrm.x), ay = std::fabs(nrm.y), az = std::fabs(nrm.z);
            int axis = (ax >= ay && ax >= az) ? 0 : (ay >= az ? 1 : 2);
            int ua = (axis == 0) ? 2 : 0, va = (axis == 1) ? 2 : 1;
            uvs[i] = {((&p.x)[ua] - (&lo.x)[ua]) / span(ua) * tile,
                      ((&p.x)[va] - (&lo.x)[va]) / span(va) * tile};
        }
        name = "";
    }

    /// Cylindrical projection around Y: u wraps around the side, v runs bottom
    /// to top. For columns, cans, tree trunks, lathe/screw results.
    void ProjectUVCylinder(float tile = 1.0f) {
        if (vertices.empty()) return;
        Vec3 lo, hi; Bounds(lo, hi);
        Vec3 c = (lo + hi) * 0.5f;
        float h = (hi.y - lo.y) > 1e-6f ? (hi.y - lo.y) : 1.0f;
        const float kPi = 3.14159265358979323846f;
        uvs.resize(vertices.size());
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            const Vec3& p = vertices[i];
            float u = (std::atan2(p.z - c.z, p.x - c.x) / (2.0f * kPi) + 0.5f) * tile;
            uvs[i] = {u, (p.y - lo.y) / h * tile};
        }
        name = "";
    }

    /// Spherical projection from the bounds centre — planets, rocks, heads.
    void ProjectUVSphere(float tile = 1.0f) {
        if (vertices.empty()) return;
        Vec3 lo, hi; Bounds(lo, hi);
        Vec3 c = (lo + hi) * 0.5f;
        const float kPi = 3.14159265358979323846f;
        uvs.resize(vertices.size());
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            Vec3 d = vertices[i] - c;
            float m = d.Magnitude();
            if (m < 1e-8f) { uvs[i] = {0.5f, 0.5f}; continue; }
            d = d * (1.0f / m);
            uvs[i] = {(std::atan2(d.z, d.x) / (2.0f * kPi) + 0.5f) * tile,
                      (std::asin(d.y < -1.0f ? -1.0f : (d.y > 1.0f ? 1.0f : d.y)) / kPi + 0.5f) * tile};
        }
        name = "";
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
        // Preserve the parallel per-vertex UVs so welding a textured mesh keeps its
        // texture coordinates (previously the stale uvs array was silently dropped,
        // un-texturing the mesh). The kept vertex's UV wins at a merged seam.
        const bool hadUV = uvs.size() == vertices.size();
        std::vector<Vec2> uniqueUV;
        float e2 = epsilon * epsilon;
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            int found = -1;
            for (std::size_t j = 0; j < unique.size(); ++j)
                if ((unique[j] - vertices[i]).SqrMagnitude() <= e2) { found = (int)j; break; }
            if (found < 0) {
                found = (int)unique.size(); unique.push_back(vertices[i]);
                if (hadUV) uniqueUV.push_back(uvs[i]);
            }
            remap[i] = found;
        }
        int removed = (int)vertices.size() - (int)unique.size();
        // Rebuild triangles, dropping any face that collapsed, and keep the parallel
        // per-triangle face colors aligned to the surviving faces.
        const bool hadFC = HasFaceColors();
        std::vector<int>   tris;
        std::vector<Color> fc;
        for (std::size_t i = 0; i + 2 < triangles.size(); i += 3) {
            int a = remap[triangles[i]], b = remap[triangles[i + 1]], c = remap[triangles[i + 2]];
            if (a != b && b != c && a != c) {
                tris.insert(tris.end(), {a, b, c}); // skip degenerate
                if (hadFC) fc.push_back(triColors[i / 3]);
            }
        }
        vertices = std::move(unique);
        triangles = std::move(tris);
        if (hadUV) uvs = std::move(uniqueUV); else uvs.clear();
        if (hadFC) triColors = std::move(fc); else triColors.clear();
        if (removed > 0) name = "";
        RefreshNormals();   // rebuild smooth normals at the welded resolution (or stay flat)
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

    /// Move a vertex set with a proportional (soft-selection) falloff: every
    /// vertex within `radius` of the selection also moves, scaled by a smoothstep
    /// of its distance to the nearest selected vertex — Blender's proportional
    /// editing. radius <= 0 behaves exactly like MoveVertices.
    void MoveVerticesSoft(const std::vector<int>& verts, const Vec3& delta, float radius) {
        if (radius <= 1e-6f) { MoveVertices(verts, delta); return; }
        std::vector<Vec3> anchors;
        anchors.reserve(verts.size());
        for (int v : verts)
            if (v >= 0 && v < (int)vertices.size()) anchors.push_back(vertices[v]);
        if (anchors.empty()) return;
        for (Vec3& p : vertices) {
            float d2min = 1e30f;
            for (const Vec3& a : anchors) {
                Vec3 d = p - a;
                float d2 = d.x*d.x + d.y*d.y + d.z*d.z;
                if (d2 < d2min) d2min = d2;
            }
            float dist = std::sqrt(d2min);
            if (dist >= radius) continue;
            float t = 1.0f - dist / radius;
            p += delta * (t * t * (3.0f - 2.0f * t));   // smoothstep falloff
        }
        name = "";
        RefreshNormals();
    }

    /// Laplacian-relax ONLY the listed vertices (the rest stay put) — evens out
    /// a lumpy patch without melting the whole mesh like Smooth() would.
    void SmoothVertices(const std::vector<int>& verts, float amount = 0.5f) {
        if (verts.empty() || amount == 0.0f || vertices.empty()) return;
        std::vector<Vec3> sum(vertices.size(), Vec3{0, 0, 0});
        std::vector<int>  cnt(vertices.size(), 0);
        auto link = [&](int a, int b) { sum[a] += vertices[b]; ++cnt[a]; };
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            int i0 = triangles[t], i1 = triangles[t + 1], i2 = triangles[t + 2];
            link(i0, i1); link(i1, i0); link(i1, i2); link(i2, i1); link(i2, i0); link(i0, i2);
        }
        std::vector<Vec3> moved = vertices;
        for (int v : verts)
            if (v >= 0 && v < (int)vertices.size() && cnt[v] > 0)
                moved[v] = vertices[v] * (1.0f - amount) + sum[v] * (amount / cnt[v]);
        vertices = std::move(moved);
        name = "";
        RefreshNormals();
    }

    /// Move the listed vertices along their own normals by `dist` — Blender's
    /// Shrink/Fatten (Alt+S): inflate or carve a selected patch without dragging
    /// an axis. Uses smooth per-vertex normals so the offset follows curvature.
    void ShrinkFattenVertices(const std::vector<int>& verts, float dist) {
        if (verts.empty() || std::fabs(dist) < 1e-8f || vertices.empty()) return;
        std::vector<Vec3> n = Normals();
        for (int v : verts)
            if (v >= 0 && v < (int)vertices.size()) vertices[v] += n[v] * dist;
        name = "";
        RefreshNormals();
    }

    /// Reshape the listed vertices toward a sphere around their centroid by
    /// `amount` in [0,1] (0 = unchanged, 1 = perfectly spherical) — Blender's
    /// To Sphere (Shift+Alt+S). The sphere radius is the selection's mean distance
    /// from its centre, so the patch rounds without ballooning.
    void SphereizeVertices(const std::vector<int>& verts, float amount) {
        if (verts.empty() || amount <= 0.0f || vertices.empty()) return;
        float t = amount > 1.0f ? 1.0f : amount;
        Vec3 c{0, 0, 0}; int n = 0;
        for (int v : verts) if (v >= 0 && v < (int)vertices.size()) { c += vertices[v]; ++n; }
        if (n == 0) return;
        c = c * (1.0f / n);
        float r = 0.0f;
        for (int v : verts) if (v >= 0 && v < (int)vertices.size()) r += (vertices[v] - c).Magnitude();
        r /= (float)n;
        for (int v : verts) {
            if (v < 0 || v >= (int)vertices.size()) continue;
            Vec3 d = vertices[v] - c;
            float m = d.Magnitude();
            if (m < 1e-6f) continue;
            Vec3 onSphere = c + d * (r / m);
            vertices[v] = vertices[v] + (onSphere - vertices[v]) * t;
        }
        name = "";
        RefreshNormals();
    }

    /// Snap the listed vertices to their shared average coordinate on one axis
    /// (0=X 1=Y 2=Z) — Blender's "flatten" (S+axis+0): level a rim, square a wall.
    void FlattenVertices(const std::vector<int>& verts, int axis) {
        if (verts.empty() || axis < 0 || axis > 2) return;
        float avg = 0.0f; int n = 0;
        for (int v : verts)
            if (v >= 0 && v < (int)vertices.size()) { avg += (&vertices[v].x)[axis]; ++n; }
        if (n == 0) return;
        avg /= (float)n;
        for (int v : verts)
            if (v >= 0 && v < (int)vertices.size()) (&vertices[v].x)[axis] = avg;
        name = "";
        RefreshNormals();
    }

    /// Split the listed triangles off into a NEW mesh (returned) and delete them
    /// from this one — Blender's "separate selection". The new mesh gets compacted
    /// vertices with their UVs and face colors carried over.
    Mesh SeparateFaces(const std::vector<int>& faces) {
        Mesh out;
        if (faces.empty()) return out;
        const bool hadUV = uvs.size() == vertices.size() && !uvs.empty();
        const bool hadColors = HasFaceColors();
        std::map<int, int> remap;
        for (int f : faces) {
            int i = f * 3;
            if (i < 0 || i + 2 >= (int)triangles.size()) continue;
            for (int k = 0; k < 3; ++k) {
                int v = triangles[i + k];
                auto it = remap.find(v);
                int nv;
                if (it == remap.end()) {
                    nv = (int)out.vertices.size(); remap[v] = nv;
                    out.vertices.push_back(vertices[v]);
                    if (hadUV) out.uvs.push_back(uvs[v]);
                } else nv = it->second;
                out.triangles.push_back(nv);
            }
            if (hadColors && f < (int)triColors.size()) out.triColors.push_back(triColors[f]);
        }
        out.RefreshNormals();
        DeleteFaces(faces);
        return out;
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

    /// Poke: fan each selected triangle from a new centre vertex (1 -> 3 tris).
    /// `height` raises the centre along the face normal — 0 keeps it flat (adds an
    /// editable centre point); positive spikes it, negative dimples it. Blender's
    /// Poke Faces. Face colors of the poked triangle carry to its three children.
    void PokeFaces(const std::vector<int>& faces, float height = 0.0f) {
        if (faces.empty()) return;
        std::set<int> sel(faces.begin(), faces.end());
        const bool hadColors = HasFaceColors();
        std::vector<int> out; out.reserve(triangles.size() * 2);
        std::vector<Color> outC; if (hadColors) outC.reserve(triColors.size() * 2);
        for (int f = 0, n = TriangleCount(); f < n; ++f) {
            int i = f * 3, a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
            Color fc = (hadColors && f < (int)triColors.size()) ? triColors[f] : Color{1,1,1,1};
            if (!sel.count(f)) { out.insert(out.end(), {a, b, c}); if (hadColors) outC.push_back(fc); continue; }
            Vec3 ctr = (vertices[a] + vertices[b] + vertices[c]) * (1.0f / 3.0f);
            if (height != 0.0f) ctr += FaceNormal(f) * height;
            int m = (int)vertices.size(); vertices.push_back(ctr);
            out.insert(out.end(), {a, b, m,  b, c, m,  c, a, m});
            if (hadColors) { outC.push_back(fc); outC.push_back(fc); outC.push_back(fc); }
        }
        triangles = std::move(out);
        if (hadColors) triColors = std::move(outC);
        name = "";
        RefreshNormals();
    }

    /// Extrude each selected face on its OWN normal as a separate protrusion
    /// (studs, greebles, spikes) — Blender's "Extrude Individual Faces". Unlike
    /// ExtrudeFaces (one connected cap along the averaged normal), each face gets
    /// its own detached cap + side walls. Vertices are duplicated per face.
    void ExtrudeFacesIndividual(const std::vector<int>& faces, float dist) {
        if (faces.empty() || std::fabs(dist) < 1e-8f) return;
        std::set<int> sel(faces.begin(), faces.end());
        const bool hadColors = HasFaceColors();
        std::vector<int> out; out.reserve(triangles.size() * 3);
        std::vector<Color> outC; if (hadColors) outC.reserve(triColors.size() * 3);
        for (int f = 0, n = TriangleCount(); f < n; ++f) {
            int i = f * 3, a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
            Color fc = (hadColors && f < (int)triColors.size()) ? triColors[f] : Color{1,1,1,1};
            if (!sel.count(f)) { out.insert(out.end(), {a, b, c}); if (hadColors) outC.push_back(fc); continue; }
            Vec3 nrm = FaceNormal(f) * dist;
            int a2 = (int)vertices.size(); vertices.push_back(vertices[a] + nrm);
            int b2 = (int)vertices.size(); vertices.push_back(vertices[b] + nrm);
            int c2 = (int)vertices.size(); vertices.push_back(vertices[c] + nrm);
            out.insert(out.end(), {a2, b2, c2});                     // cap
            out.insert(out.end(), {a, b, b2,  a, b2, a2});           // side walls (outward)
            out.insert(out.end(), {b, c, c2,  b, c2, b2});
            out.insert(out.end(), {c, a, a2,  c, a2, c2});
            if (hadColors) for (int k = 0; k < 7; ++k) outC.push_back(fc);
        }
        triangles = std::move(out);
        if (hadColors) triColors = std::move(outC);
        name = "";
        RefreshNormals();
    }

    /// Inset each selected face independently (a smaller face inside + a border
    /// ring), per face rather than as one shared region — Blender's "Inset
    /// Individual". Pairs with ExtrudeFacesIndividual for panel/greeble looks.
    void InsetFacesIndividual(const std::vector<int>& faces, float amount) {
        if (faces.empty()) return;
        float t = amount < 0.0f ? 0.0f : (amount > 1.0f ? 1.0f : amount);
        std::set<int> sel(faces.begin(), faces.end());
        const bool hadColors = HasFaceColors();
        std::vector<int> out; out.reserve(triangles.size() * 3);
        std::vector<Color> outC; if (hadColors) outC.reserve(triColors.size() * 3);
        for (int f = 0, n = TriangleCount(); f < n; ++f) {
            int i = f * 3, a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
            Color fc = (hadColors && f < (int)triColors.size()) ? triColors[f] : Color{1,1,1,1};
            if (!sel.count(f)) { out.insert(out.end(), {a, b, c}); if (hadColors) outC.push_back(fc); continue; }
            Vec3 ctr = (vertices[a] + vertices[b] + vertices[c]) * (1.0f / 3.0f);
            int a2 = (int)vertices.size(); vertices.push_back(vertices[a] + (ctr - vertices[a]) * t);
            int b2 = (int)vertices.size(); vertices.push_back(vertices[b] + (ctr - vertices[b]) * t);
            int c2 = (int)vertices.size(); vertices.push_back(vertices[c] + (ctr - vertices[c]) * t);
            out.insert(out.end(), {a2, b2, c2});                     // inner face
            out.insert(out.end(), {a, b, b2,  a, b2, a2});           // border ring
            out.insert(out.end(), {b, c, c2,  b, c2, b2});
            out.insert(out.end(), {c, a, a2,  c, a2, c2});
            if (hadColors) for (int k = 0; k < 7; ++k) outC.push_back(fc);
        }
        triangles = std::move(out);
        if (hadColors) triColors = std::move(outC);
        name = "";
        RefreshNormals();
    }

    /// Delete the listed triangles (erasing their 3 indices each). Sorted
    /// descending so earlier erases don't shift later indices. Orphan vertices are
    /// left in place (harmless).
    void DeleteFaces(std::vector<int> faces) {
        const bool hadColors = HasFaceColors();
        std::sort(faces.begin(), faces.end(), std::greater<int>());
        for (int f : faces) {
            int i = f * 3;
            if (i < 0 || i + 2 >= (int)triangles.size()) continue;
            triangles.erase(triangles.begin() + i, triangles.begin() + i + 3);
            if (hadColors && f < (int)triColors.size())   // keep per-face colors aligned
                triColors.erase(triColors.begin() + f);
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

    /// Make every face's winding coherent with its neighbors and point each closed
    /// shell OUTWARD (Blender's "Recalculate Normals Outside"). Welds, bridges,
    /// booleans, mirrored halves and imports routinely leave a few faces inside-out
    /// — they shade dark and leak shadows. This walks each connected shell flipping
    /// inconsistent triangles (two coherent neighbors traverse their shared edge in
    /// OPPOSITE directions), then flips whole shells whose signed volume is negative.
    /// Returns how many triangles were flipped. Coincident-vertex aware, so seams
    /// from Separate/Weld/hard-shading still count as connected.
    int OrientFacesOutward() {
        const int nt = (int)triangles.size() / 3;
        if (nt == 0) return 0;
        std::vector<int> rep = CoincidentReps();
        // Zero-area triangles (collapsed pole fans etc.) have no orientation of
        // their own and corrupt the winding walk — leave them out entirely.
        std::vector<char> degen(nt, 0);
        for (int t = 0; t < nt; ++t) {
            const Vec3& a = vertices[triangles[t * 3]];
            Vec3 n = Vec3::Cross(vertices[triangles[t * 3 + 1]] - a,
                                 vertices[triangles[t * 3 + 2]] - a);
            if (n.SqrMagnitude() < 1e-16f) degen[t] = 1;
        }
        // Directed edge -> owning tri list (a tri owns edges a->b, b->c, c->a).
        std::map<std::pair<int, int>, std::vector<int>> edgeTris;   // undirected key
        for (int t = 0; t < nt; ++t) {
            if (degen[t]) continue;
            for (int k = 0; k < 3; ++k) {
                int a = rep[triangles[t * 3 + k]], b = rep[triangles[t * 3 + (k + 1) % 3]];
                if (a == b) continue;
                edgeTris[{a < b ? a : b, a < b ? b : a}].push_back(t);
            }
        }
        // Directed edge of tri t at slot k AFTER any flip recorded in `flip`.
        auto dirEdge = [&](int t, int k, bool flipped, int& a, int& b) {
            int i0 = rep[triangles[t * 3 + k]], i1 = rep[triangles[t * 3 + (k + 1) % 3]];
            if (flipped) { a = i1; b = i0; } else { a = i0; b = i1; }
        };
        std::vector<char> flip(nt, 0), seen(nt, 0);
        std::vector<int> shell; shell.reserve(64);
        int flipped = 0;
        for (int seed = 0; seed < nt; ++seed) {
            if (seen[seed] || degen[seed]) continue;
            shell.clear();
            std::vector<int> stack{seed};
            seen[seed] = 1;
            while (!stack.empty()) {                    // BFS: propagate coherent winding
                int t = stack.back(); stack.pop_back();
                shell.push_back(t);
                for (int k = 0; k < 3; ++k) {
                    int a, b; dirEdge(t, k, flip[t] != 0, a, b);
                    if (a == b) continue;
                    auto it = edgeTris.find({a < b ? a : b, a < b ? b : a});
                    if (it == edgeTris.end()) continue;
                    for (int n : it->second) {
                        if (n == t || seen[n]) continue;
                        // Find the shared edge's direction in the neighbor: coherent
                        // neighbors traverse it OPPOSITE to (a -> b).
                        for (int j = 0; j < 3; ++j) {
                            int na, nb; dirEdge(n, j, false, na, nb);
                            if (na == a && nb == b) { flip[n] = 1; break; }   // same dir = wrong
                            if (na == b && nb == a) { flip[n] = 0; break; }
                        }
                        seen[n] = 1;
                        stack.push_back(n);
                    }
                }
            }
            // Outward test: the shell's signed volume (divergence theorem). Negative
            // = the coherent winding faces inward, so flip the whole shell.
            double vol = 0.0;
            for (int t : shell) {
                Vec3 va = vertices[triangles[t * 3]], vb = vertices[triangles[t * 3 + 1]],
                     vc = vertices[triangles[t * 3 + 2]];
                if (flip[t]) std::swap(vb, vc);
                vol += (double)Vec3::Dot(va, Vec3::Cross(vb, vc));
            }
            if (vol < 0.0) for (int t : shell) flip[t] = !flip[t];
        }
        for (int t = 0; t < nt; ++t)
            if (flip[t]) { std::swap(triangles[t * 3 + 1], triangles[t * 3 + 2]); ++flipped; }
        if (flipped) { name = ""; if (HasNormals()) ComputeSmoothNormals(); }
        return flipped;
    }


    /// A sculpting brush in LOCAL mesh space. Vertices within `radius` of `center`
    /// are displaced with a smoothstep falloff `w`. mode: 0 = GRAB (push along
    /// `dir`), 1 = INFLATE (push along each vertex's own normal), 2 = SMOOTH (pull
    /// toward the local average of nearby vertices), 3 = FLATTEN (press the region
    /// onto its own average plane), 4 = PINCH (draw vertices toward the brush
    /// centre, sharpening creases). SMOOTH/FLATTEN read a snapshot so the
    /// relaxation doesn't feed back within one call.
    void SculptBrush(const Vec3& center, const Vec3& dir, float radius, float strength, int mode) {
        if (radius <= 1e-6f || vertices.empty()) return;
        std::vector<Vec3> vn;
        if (mode == 1 || mode == 3) vn = Normals();    // vertex normals for INFLATE/FLATTEN
        std::vector<Vec3> snapshot = vertices;         // SMOOTH reads positions pre-edit
        float inv = 1.0f / radius;
        // FLATTEN presses toward the best-fit plane of the brush region: its
        // centroid with the averaged vertex normal.
        Vec3 planeC{0, 0, 0}, planeN{0, 1, 0};
        if (mode == 3) {
            Vec3 nsum{0, 0, 0}; int cnt = 0;
            for (std::size_t i = 0; i < snapshot.size(); ++i)
                if ((snapshot[i] - center).Magnitude() < radius) {
                    planeC += snapshot[i]; nsum += vn[i]; ++cnt;
                }
            if (cnt == 0) return;
            planeC = planeC * (1.0f / cnt);
            float m = nsum.Magnitude();
            if (m > 1e-6f) planeN = nsum * (1.0f / m);
        }
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
            } else if (mode == 3) {                     // FLATTEN: press onto the plane
                float off = Vec3::Dot(snapshot[i] - planeC, planeN);
                float k = strength * w; if (k > 1.0f) k = 1.0f;
                vertices[i] += planeN * (-off * k);
            } else if (mode == 4) {                     // PINCH: gather toward centre
                float k = strength * w; if (k > 1.0f) k = 1.0f;
                vertices[i] += (center - snapshot[i]) * k;
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

    /// Sweep a circular cross-section along a 3D polyline — pipes, rails, cables,
    /// tree branches. Frames are parallel-transported so the tube doesn't twist at
    /// bends. `caps` closes the ends with fans. `radiusEnd` >= 0 tapers the tube
    /// linearly from `radius` at the start to `radiusEnd` at the end (trunks, tails).
    static Mesh SweepPath(const std::vector<Vec3>& path, float radius = 0.25f,
                          int segments = 12, bool caps = true, float radiusEnd = -1.0f) {
        Mesh m;
        const int n = (int)path.size();
        if (n < 2 || segments < 3 || radius <= 1e-6f) return m;
        const float kPi = 3.14159265358979323846f;
        std::vector<Vec3> T(n);                        // tangents (central differences)
        for (int i = 0; i < n; ++i) {
            Vec3 d = path[std::min(i + 1, n - 1)] - path[std::max(i - 1, 0)];
            float md = d.Magnitude();
            T[i] = md > 1e-8f ? d * (1.0f / md) : Vec3{0, 1, 0};
        }
        Vec3 up = std::fabs(T[0].y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
        Vec3 N = Vec3::Cross(up, T[0]);
        { float mn = N.Magnitude(); N = mn > 1e-8f ? N * (1.0f / mn) : Vec3{0, 0, 1}; }
        for (int i = 0; i < n; ++i) {
            if (i > 0) {                                // transport the frame
                N = N - T[i] * Vec3::Dot(N, T[i]);
                float mn = N.Magnitude();
                if (mn > 1e-8f) N = N * (1.0f / mn);
                else {                                  // degenerate (sharp reversal)
                    Vec3 u2 = std::fabs(T[i].y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
                    N = Vec3::Cross(u2, T[i]).Normalized();
                }
            }
            Vec3 B = Vec3::Cross(T[i], N);
            float r = radius;
            if (radiusEnd >= 0.0f && n > 1)
                r = radius + (radiusEnd - radius) * ((float)i / (float)(n - 1));
            if (r < 1e-4f) r = 1e-4f;
            for (int s = 0; s < segments; ++s) {
                float th = 2.0f * kPi * (float)s / segments;
                m.vertices.push_back(path[i] + (N * std::cos(th) + B * std::sin(th)) * r);
            }
        }
        for (int i = 0; i + 1 < n; ++i)
            for (int s = 0; s < segments; ++s) {
                int s1 = (s + 1) % segments;
                int a = i * segments + s,        b = i * segments + s1;
                int c = (i + 1) * segments + s,  d = (i + 1) * segments + s1;
                m.triangles.insert(m.triangles.end(), {a, b, c,  b, d, c});
            }
        if (caps) {
            int c0 = (int)m.vertices.size(); m.vertices.push_back(path[0]);
            int c1 = (int)m.vertices.size(); m.vertices.push_back(path[n - 1]);
            for (int s = 0; s < segments; ++s) {
                int s1 = (s + 1) % segments;
                m.triangles.insert(m.triangles.end(), {c0, s1, s});                     // start cap (faces -T)
                int base = (n - 1) * segments;
                m.triangles.insert(m.triangles.end(), {c1, base + s, base + s1});       // end cap (faces +T)
            }
        }
        m.ComputeSmoothNormals();
        return m;
    }

    /// Cap every hole: boundary edges (edges used by only one triangle) are chained
    /// into loops and each loop is fan-filled from its centroid. Coincident vertices
    /// are treated as one so unwelded seams don't read as boundaries. Returns the
    /// number of holes closed.
    int FillHoles() {
        if (triangles.size() < 3 || vertices.empty()) return 0;
        // Representative index per coincident-vertex group.
        std::map<std::tuple<int, int, int>, int> grid;
        std::vector<int> rep(vertices.size());
        auto key = [](const Vec3& v) {
            return std::make_tuple((int)std::lround(v.x * 4096.0f),
                                   (int)std::lround(v.y * 4096.0f),
                                   (int)std::lround(v.z * 4096.0f));
        };
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            auto k = key(vertices[i]);
            auto it = grid.find(k);
            if (it == grid.end()) { grid[k] = (int)i; rep[i] = (int)i; }
            else rep[i] = it->second;
        }
        std::set<std::pair<int, int>> edges;            // directed, on representatives
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            int a = rep[triangles[t]], b = rep[triangles[t + 1]], c = rep[triangles[t + 2]];
            if (a == b || b == c || a == c) continue;
            edges.insert({a, b}); edges.insert({b, c}); edges.insert({c, a});
        }
        // Boundary edges: no reverse partner. A hole rim chains head-to-tail.
        std::map<int, int> next;
        for (const auto& e : edges)
            if (!edges.count({e.second, e.first}) && !next.count(e.first))
                next[e.first] = e.second;
        const bool hadUV = uvs.size() == vertices.size() && !uvs.empty();
        int holes = 0;
        std::set<int> used;
        for (const auto& start : next) {
            if (used.count(start.first)) continue;
            std::vector<int> loop;                      // walk the rim
            int v = start.first;
            while (next.count(v) && !used.count(v)) {
                used.insert(v); loop.push_back(v);
                v = next[v];
            }
            if (loop.size() < 3 || v != start.first) continue;   // open chain — not a loop
            Vec3 c{0, 0, 0};
            for (int i : loop) c += vertices[i];
            c = c * (1.0f / (float)loop.size());
            int ci = (int)vertices.size();
            vertices.push_back(c);
            if (hadUV) uvs.push_back({0.5f, 0.5f});
            // Rim edges wind with the surrounding faces; (b, a, centre) faces outward.
            for (std::size_t i = 0; i < loop.size(); ++i) {
                int a = loop[i], b = loop[(i + 1) % loop.size()];
                triangles.insert(triangles.end(), {b, a, ci});
            }
            ++holes;
        }
        if (holes > 0) {
            triColors.clear();                           // face records changed
            name = "";
            RefreshNormals();
        }
        return holes;
    }

    /// Bridge: if the mesh has EXACTLY two open boundary loops (e.g. two facing
    /// faces deleted), connect them with a wall of triangles — tunnels, necks,
    /// tube joins. Loops of different sizes are paired proportionally. Returns
    /// true when a bridge was built.
    bool BridgeLoops() {
        if (triangles.size() < 3 || vertices.empty()) return false;
        // Boundary loops, coincident-vertex aware (same approach as FillHoles).
        std::map<std::tuple<int, int, int>, int> grid;
        std::vector<int> rep(vertices.size());
        auto key = [](const Vec3& v) {
            return std::make_tuple((int)std::lround(v.x * 4096.0f),
                                   (int)std::lround(v.y * 4096.0f),
                                   (int)std::lround(v.z * 4096.0f));
        };
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            auto k = key(vertices[i]);
            auto it = grid.find(k);
            if (it == grid.end()) { grid[k] = (int)i; rep[i] = (int)i; }
            else rep[i] = it->second;
        }
        std::set<std::pair<int, int>> edges;
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            int a = rep[triangles[t]], b = rep[triangles[t + 1]], c = rep[triangles[t + 2]];
            if (a == b || b == c || a == c) continue;
            edges.insert({a, b}); edges.insert({b, c}); edges.insert({c, a});
        }
        std::map<int, int> next;
        for (const auto& e : edges)
            if (!edges.count({e.second, e.first}) && !next.count(e.first))
                next[e.first] = e.second;
        std::vector<std::vector<int>> loops;
        std::set<int> used;
        for (const auto& start : next) {
            if (used.count(start.first)) continue;
            std::vector<int> loop;
            int v = start.first;
            while (next.count(v) && !used.count(v)) { used.insert(v); loop.push_back(v); v = next[v]; }
            if (loop.size() >= 3 && v == start.first) loops.push_back(std::move(loop));
        }
        if (loops.size() != 2) return false;
        std::vector<int>& A = loops[0];
        std::vector<int>  B = loops[1];
        // Two rims of one surface wind opposite each other when facing; walking A
        // forward pairs with walking B REVERSED. Then rotate B so its start is the
        // vertex nearest A's start.
        std::reverse(B.begin(), B.end());
        {
            float best = 1e30f; std::size_t bi = 0;
            for (std::size_t i = 0; i < B.size(); ++i) {
                float d = (vertices[B[i]] - vertices[A[0]]).SqrMagnitude();
                if (d < best) { best = d; bi = i; }
            }
            std::rotate(B.begin(), B.begin() + bi, B.end());
        }
        const int nA = (int)A.size(), nB = (int)B.size();
        const int steps = std::max(nA, nB);
        auto ai = [&](int i) { return A[((long long)i * nA / steps) % nA]; };
        auto bi = [&](int i) { return B[((long long)i * nB / steps) % nB]; };
        const bool hadUV = uvs.size() == vertices.size() && !uvs.empty();
        (void)hadUV;                     // bridge reuses existing vertices only
        for (int i = 0; i < steps; ++i) {
            int a0 = ai(i), a1 = ai(i + 1);
            int b0 = bi(i), b1 = bi(i + 1);
            // The surface already contains the rim's directed edges, so the wall
            // must supply their REVERSES: (a1->a0) and, on the reversed-B rim,
            // (b0->b1). Quad (a1, a0, b0, b1) split into two triangles does both.
            if (a0 != a1) triangles.insert(triangles.end(), {a1, a0, b0});
            if (b0 != b1) triangles.insert(triangles.end(), {a1, b0, b1});
        }
        triColors.clear();
        name = "";
        RefreshNormals();
        return true;
    }

    /// Collapse each listed edge to its midpoint (both endpoints merge into one
    /// vertex) — remove edge loops, simplify dense areas. Degenerate triangles
    /// are dropped by the weld pass.
    void CollapseEdges(const std::vector<std::pair<int, int>>& es) {
        if (es.empty()) return;
        for (const auto& e : es) {
            if (e.first < 0 || e.first >= (int)vertices.size() ||
                e.second < 0 || e.second >= (int)vertices.size()) continue;
            Vec3 mid = (vertices[e.first] + vertices[e.second]) * 0.5f;
            vertices[e.first] = mid;
            vertices[e.second] = mid;
        }
        WeldVertices();                  // merges the pairs + drops collapsed tris
        name = "";
        RefreshNormals();
    }

    /// Split each listed edge at its midpoint: a new vertex is inserted and every
    /// triangle sharing that edge is cut in two — add local detail exactly where
    /// you need it without subdividing whole faces.
    void SplitEdges(const std::vector<std::pair<int, int>>& es) {
        if (es.empty()) return;
        const bool hadUV = uvs.size() == vertices.size() && !uvs.empty();
        for (const auto& e : es) {
            int a = e.first, b = e.second;
            if (a < 0 || a >= (int)vertices.size() || b < 0 || b >= (int)vertices.size() || a == b)
                continue;
            int mid = (int)vertices.size();
            vertices.push_back((vertices[a] + vertices[b]) * 0.5f);
            if (hadUV) uvs.push_back({(uvs[a].x + uvs[b].x) * 0.5f, (uvs[a].y + uvs[b].y) * 0.5f});
            std::vector<int> out;
            out.reserve(triangles.size() + 6);
            for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
                int v[3] = {triangles[t], triangles[t + 1], triangles[t + 2]};
                int hit = -1;                            // which cyclic edge is (a,b)?
                for (int k = 0; k < 3; ++k) {
                    int p = v[k], q = v[(k + 1) % 3];
                    if ((p == a && q == b) || (p == b && q == a)) { hit = k; break; }
                }
                if (hit < 0) { out.insert(out.end(), {v[0], v[1], v[2]}); continue; }
                int p = v[hit], q = v[(hit + 1) % 3], r = v[(hit + 2) % 3];
                out.insert(out.end(), {p, mid, r,  mid, q, r});   // same orientation
            }
            triangles = std::move(out);
        }
        triColors.clear();
        name = "";
        RefreshNormals();
    }

    /// Append a detached copy of the listed triangles (new vertices, UVs carried)
    /// and return the indices of the NEW faces — duplicate-and-drag workflows.
    std::vector<int> DuplicateFaces(const std::vector<int>& faces) {
        std::vector<int> newFaces;
        if (faces.empty()) return newFaces;
        const bool hadUV = uvs.size() == vertices.size() && !uvs.empty();
        const bool hadColors = HasFaceColors();
        std::map<int, int> remap;
        for (int f : faces) {
            int i = f * 3;
            if (i < 0 || i + 2 >= (int)triangles.size()) continue;
            newFaces.push_back((int)(triangles.size() / 3));
            for (int k = 0; k < 3; ++k) {
                int v = triangles[i + k];
                auto it = remap.find(v);
                int nv;
                if (it == remap.end()) {
                    nv = (int)vertices.size(); remap[v] = nv;
                    vertices.push_back(vertices[v]);
                    if (hadUV) uvs.push_back(uvs[v]);
                } else nv = it->second;
                triangles.push_back(nv);
            }
            if (hadColors && f < (int)triColors.size()) triColors.push_back(triColors[f]);
        }
        name = "";
        RefreshNormals();
        return newFaces;
    }

    // ---- Selection helpers (index sets in/out; the editor owns the selection) ----

    /// Representative index per coincident-vertex group, so selection ops flow
    /// across unwelded seams the same way Smooth() does.
    std::vector<int> CoincidentReps() const {
        std::map<std::tuple<int, int, int>, int> grid;
        std::vector<int> rep(vertices.size());
        auto key = [](const Vec3& v) {
            return std::make_tuple((int)std::lround(v.x * 4096.0f),
                                   (int)std::lround(v.y * 4096.0f),
                                   (int)std::lround(v.z * 4096.0f));
        };
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            auto k = key(vertices[i]);
            auto it = grid.find(k);
            if (it == grid.end()) { grid[k] = (int)i; rep[i] = (int)i; }
            else rep[i] = it->second;
        }
        return rep;
    }

    /// One step of selection GROW: every vertex sharing an edge with the selection
    /// joins it (coincident-vertex aware).
    std::vector<int> GrownVertexSelection(const std::vector<int>& sel) const {
        if (sel.empty() || vertices.empty()) return sel;
        std::vector<int> rep = CoincidentReps();
        std::set<int> inSel;                              // rep space
        for (int v : sel) if (v >= 0 && v < (int)vertices.size()) inSel.insert(rep[v]);
        std::set<int> grown = inSel;
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            int r[3] = {rep[triangles[t]], rep[triangles[t + 1]], rep[triangles[t + 2]]};
            for (int k = 0; k < 3; ++k)
                for (int j = 0; j < 3; ++j)
                    if (j != k && inSel.count(r[k])) grown.insert(r[j]);
        }
        std::vector<int> out;
        for (std::size_t i = 0; i < vertices.size(); ++i)
            if (grown.count(rep[i])) out.push_back((int)i);
        return out;
    }

    /// One step of selection SHRINK: only vertices whose every edge-neighbour is
    /// also selected stay — peels the selection's rim off.
    std::vector<int> ShrunkVertexSelection(const std::vector<int>& sel) const {
        if (sel.empty() || vertices.empty()) return sel;
        std::vector<int> rep = CoincidentReps();
        std::set<int> inSel;
        for (int v : sel) if (v >= 0 && v < (int)vertices.size()) inSel.insert(rep[v]);
        std::set<int> rim;                                // selected verts touching outside
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            int r[3] = {rep[triangles[t]], rep[triangles[t + 1]], rep[triangles[t + 2]]};
            for (int k = 0; k < 3; ++k)
                for (int j = 0; j < 3; ++j)
                    if (j != k && inSel.count(r[k]) && !inSel.count(r[j])) rim.insert(r[k]);
        }
        std::vector<int> out;
        for (std::size_t i = 0; i < vertices.size(); ++i)
            if (inSel.count(rep[i]) && !rim.count(rep[i])) out.push_back((int)i);
        return out;
    }

    /// Everything connected to `seed` through shared edges (coincident-vertex
    /// aware) — Select Linked: grab one island of a combined mesh.
    std::vector<int> LinkedVertices(int seed) const {
        std::vector<int> out;
        if (seed < 0 || seed >= (int)vertices.size()) return out;
        std::vector<int> rep = CoincidentReps();
        std::map<int, std::vector<int>> adj;              // rep-space adjacency
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            int r[3] = {rep[triangles[t]], rep[triangles[t + 1]], rep[triangles[t + 2]]};
            for (int k = 0; k < 3; ++k)
                for (int j = 0; j < 3; ++j)
                    if (j != k) adj[r[k]].push_back(r[j]);
        }
        std::set<int> seen;
        std::vector<int> stack = {rep[seed]};
        seen.insert(rep[seed]);
        while (!stack.empty()) {
            int v = stack.back(); stack.pop_back();
            auto it = adj.find(v);
            if (it == adj.end()) continue;
            for (int n : it->second)
                if (!seen.count(n)) { seen.insert(n); stack.push_back(n); }
        }
        for (std::size_t i = 0; i < vertices.size(); ++i)
            if (seen.count(rep[i])) out.push_back((int)i);
        return out;
    }

    /// Extend one edge into its LOOP: at each endpoint keep stepping onto the
    /// straightest continuation (quad-aware via VisibleEdges) until the loop
    /// closes or turns sharper than `minDot`. Returns vertex-index pairs
    /// including the seed edge — Blender's Alt+Click edge loop.
    std::vector<std::pair<int, int>> EdgeLoop(int va, int vb, float minDot = 0.5f) const {
        std::vector<std::pair<int, int>> out;
        if (va < 0 || vb < 0 || va >= (int)vertices.size() || vb >= (int)vertices.size())
            return out;
        std::vector<int> rep = CoincidentReps();
        std::map<int, std::vector<int>> adj;              // quad-wireframe adjacency
        for (const auto& e : VisibleEdges()) {
            int a = rep[e.first], b = rep[e.second];
            if (a == b) continue;
            adj[a].push_back(b);
            adj[b].push_back(a);
        }
        auto dir = [&](int a, int b) { return (vertices[b] - vertices[a]).Normalized(); };
        std::set<std::pair<int, int>> seen;
        auto addEdge = [&](int a, int b) {
            auto k = a < b ? std::make_pair(a, b) : std::make_pair(b, a);
            if (seen.count(k)) return false;
            seen.insert(k);
            out.push_back({a, b});
            return true;
        };
        int A = rep[va], B = rep[vb];
        if (!addEdge(A, B)) return out;
        for (int pass = 0; pass < 2; ++pass) {            // walk both directions
            int prev = pass == 0 ? A : B;
            int cur  = pass == 0 ? B : A;
            for (int guard = 0; guard < 4096; ++guard) {
                Vec3 d = dir(prev, cur);
                float best = minDot; int nxt = -1;
                auto it = adj.find(cur);
                if (it == adj.end()) break;
                for (int n : it->second) {
                    if (n == prev) continue;
                    float dot = Vec3::Dot(d, dir(cur, n));
                    if (dot > best) { best = dot; nxt = n; }
                }
                if (nxt < 0) break;
                if (!addEdge(cur, nxt)) break;            // loop closed
                prev = cur; cur = nxt;
            }
        }
        return out;
    }

    // ---- Face painting (per-face vertex colors, saved via the meshcolors record) ----

    /// Make sure the per-face color array exists (one entry per triangle),
    /// seeding it with `base` — call before painting.
    void EnsureFaceColors(const Color& base = {1, 1, 1, 1}) {
        if (!HasFaceColors()) triColors.assign(TriangleCount(), base);
    }

    /// Paint brush: blend the faces whose centroid lies within `radius` of
    /// `center` toward color `c`, with a smoothstep falloff scaled by `blend`.
    /// Painting keeps the primitive name — a painted cube is still a Cube.
    void PaintFaces(const Vec3& center, float radius, const Color& c, float blend = 1.0f) {
        if (radius <= 1e-6f || triangles.empty() || blend <= 0.0f) return;
        EnsureFaceColors();
        for (int f = 0; f < TriangleCount(); ++f) {
            float d = (FaceCenter(f) - center).Magnitude();
            if (d >= radius) continue;
            float w = 1.0f - d / radius;
            w = w * w * (3.0f - 2.0f * w) * blend;
            if (w > 1.0f) w = 1.0f;
            Color& o = triColors[f];
            o.r += (c.r - o.r) * w;
            o.g += (c.g - o.g) * w;
            o.b += (c.b - o.b) * w;
            o.a += (c.a - o.a) * w;
        }
    }

    /// Vary each face color's brightness by up to ±`amt` (deterministic per seed)
    /// — breaks up flat prop colors so wood/stone/foliage reads as a material
    /// instead of plastic. Keeps the primitive name (used by the prop builders).
    void MottleFaceColors(float amt, int seed = 0) {
        if (!HasFaceColors() || amt <= 0.0f) return;
        for (int f = 0; f < (int)triColors.size(); ++f) {
            unsigned x = (unsigned)(f * 73856093 ^ seed * 19349663);
            x = (x ^ (x >> 13)) * 1274126177u;
            float k = 1.0f + amt * (((float)((x ^ (x >> 16)) & 0xffffu) / 65535.0f) * 2.0f - 1.0f);
            Color& c = triColors[f];
            c.r = std::fmin(c.r * k, 1.0f);
            c.g = std::fmin(c.g * k, 1.0f);
            c.b = std::fmin(c.b * k, 1.0f);
        }
    }

    /// Set the listed faces to exactly `c` (bucket fill on a selection).
    void FillFacesColor(const std::vector<int>& faces, const Color& c) {
        if (faces.empty()) return;
        EnsureFaceColors();
        for (int f : faces)
            if (f >= 0 && f < (int)triColors.size()) triColors[f] = c;
    }

    /// Extrude the listed edges: each gets a wall quad pushed `dist` along its
    /// adjacent face's normal — pull walls up from a floor plate's rim, extend a
    /// ribbon strip. Returns the NEW outer edges so the caller can keep extruding
    /// or move them. Winding is chosen from how the edge appears in its face, so
    /// boundary-edge walls face outward.
    std::vector<std::pair<int, int>> ExtrudeEdges(const std::vector<std::pair<int, int>>& es,
                                                  float dist) {
        std::vector<std::pair<int, int>> newEdges;
        if (es.empty() || std::fabs(dist) < 1e-8f) return newEdges;
        const bool hadUV = uvs.size() == vertices.size() && !uvs.empty();
        for (const auto& e : es) {
            int a = e.first, b = e.second;
            if (a < 0 || a >= (int)vertices.size() || b < 0 || b >= (int)vertices.size() || a == b)
                continue;
            // Find a triangle owning this edge to get its direction + normal.
            int p = -1, q = -1; Vec3 n{0, 1, 0}; bool found = false;
            for (std::size_t t = 0; t + 2 < triangles.size() && !found; t += 3) {
                int v[3] = {triangles[t], triangles[t + 1], triangles[t + 2]};
                for (int k = 0; k < 3; ++k) {
                    int i0 = v[k], i1 = v[(k + 1) % 3];
                    if ((i0 == a && i1 == b) || (i0 == b && i1 == a)) {
                        p = i0; q = i1; n = FaceNormal((int)(t / 3)); found = true; break;
                    }
                }
            }
            if (!found) continue;
            int p2 = (int)vertices.size();
            vertices.push_back(vertices[p] + n * dist);
            if (hadUV) uvs.push_back(uvs[p]);
            int q2 = (int)vertices.size();
            vertices.push_back(vertices[q] + n * dist);
            if (hadUV) uvs.push_back(uvs[q]);
            // The face already has directed edge p->q; the wall supplies q->p.
            triangles.insert(triangles.end(), {q, p, p2,  q, p2, q2});
            newEdges.push_back({p2, q2});
        }
        if (!newEdges.empty()) {
            triColors.clear();
            name = "";
            RefreshNormals();
        }
        return newEdges;
    }

    /// Quantize vertices to a grid of `step` (empty `verts` = whole mesh) — clean
    /// up hand-moved points, make modular kit pieces line up exactly.
    void SnapToGrid(float step, const std::vector<int>& verts = {}) {
        if (step <= 1e-6f || vertices.empty()) return;
        auto snap = [&](int v) {
            vertices[v] = {std::lround(vertices[v].x / step) * step,
                           std::lround(vertices[v].y / step) * step,
                           std::lround(vertices[v].z / step) * step};
        };
        if (verts.empty())
            for (int i = 0; i < (int)vertices.size(); ++i) snap(i);
        else
            for (int v : verts)
                if (v >= 0 && v < (int)vertices.size()) snap(v);
        name = "";
        RefreshNormals();
    }

    /// Split every triangle that straddles the plane `axis == coord` (0=X 1=Y
    /// 2=Z), inserting vertices on the crossing edges and keeping BOTH sides — an
    /// edge loop across the mesh. The surface stays watertight and its volume is
    /// unchanged (this only adds cuts). Face colors carry to the split pieces.
    void SplitByPlane(int axis, float coord, float eps = 1e-5f) {
        if (axis < 0 || axis > 2 || triangles.size() < 3) return;
        const bool hadColors = HasFaceColors();
        std::vector<int> out; out.reserve(triangles.size() * 2);
        std::vector<Color> outC; if (hadColors) outC.reserve(triColors.size() * 2);
        auto sd = [&](int v) { return (&vertices[v].x)[axis] - coord; };
        auto lerpV = [&](int a, int b) {
            float sa = sd(a), sb = sd(b), t = sa / (sa - sb);
            int idx = (int)vertices.size();
            vertices.push_back(vertices[a] + (vertices[b] - vertices[a]) * t);
            if (uvs.size() == vertices.size() - 1)
                uvs.clear();   // (keep it simple: slicing drops per-vertex UVs)
            return idx;
        };
        for (int f = 0, n = TriangleCount(); f < n; ++f) {
            int i = f * 3, a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
            Color fc = (hadColors && f < (int)triColors.size()) ? triColors[f] : Color{1,1,1,1};
            float s[3] = {sd(a), sd(b), sd(c)};
            float mn = std::fmin(s[0], std::fmin(s[1], s[2]));
            float mx = std::fmax(s[0], std::fmax(s[1], s[2]));
            if (!(mn < -eps && mx > eps)) {                     // no genuine crossing
                out.insert(out.end(), {a, b, c}); if (hadColors) outC.push_back(fc); continue;
            }
            int v[3] = {a, b, c};
            int lone = (s[0] * s[1] >= 0.0f) ? 2 : ((s[0] * s[2] >= 0.0f) ? 1 : 0);
            int L = v[lone], M = v[(lone + 1) % 3], N = v[(lone + 2) % 3];
            int iLM = lerpV(L, M), iNL = lerpV(N, L);
            out.insert(out.end(), {L, iLM, iNL});               // lone-side triangle
            out.insert(out.end(), {M, N, iNL,  M, iNL, iLM});   // other-side quad
            if (hadColors) { outC.push_back(fc); outC.push_back(fc); outC.push_back(fc); }
        }
        triangles = std::move(out);
        if (hadColors) triColors = std::move(outC);
        name = "";
        RefreshNormals();
    }

    /// Loop Cut: insert `count` evenly-spaced edge loops across the mesh
    /// perpendicular to `axis` (0=X 1=Y 2=Z) — Blender's Ctrl+R. Robust on any
    /// topology (it slices, not quad-walks): great for adding editable divisions
    /// to a wall/box/extruded shape. Welds the new cut vertices into clean loops.
    void LoopCut(int axis, int count) {
        if (axis < 0 || axis > 2 || count < 1 || vertices.empty()) return;
        Vec3 lo, hi; Bounds(lo, hi);
        float a0 = (&lo.x)[axis], a1 = (&hi.x)[axis];
        if (a1 - a0 < 1e-5f) return;
        for (int k = 1; k <= count; ++k)
            SplitByPlane(axis, a0 + (a1 - a0) * (float)k / (float)(count + 1));
        WeldVertices();                                         // merge coincident cut verts into loops
        name = "";
        RefreshNormals();
    }

    /// Chamfer the listed vertices: each selected corner is cut back by `amount`
    /// along every edge that meets it and the opening is capped with a flat facet —
    /// Blender's vertex bevel. Great for knocking the sharp corners off boxes.
    void BevelVertices(const std::vector<int>& sel, float amount) {
        if (sel.empty() || amount <= 1e-6f || triangles.empty()) return;
        std::set<int> S;
        for (int v : sel) if (v >= 0 && v < (int)vertices.size()) S.insert(v);
        if (S.empty()) return;
        const bool hadUV = uvs.size() == vertices.size() && !uvs.empty();
        std::vector<Vec3> vn = Normals();                // cap orientation
        std::map<std::pair<int, int>, int> splitIdx;     // (corner, neighbour) -> new vert
        std::map<int, std::vector<int>> ringOf;          // corner -> its new ring verts
        auto splitPoint = [&](int v, int o) {
            auto k = std::make_pair(v, o);
            auto it = splitIdx.find(k);
            if (it != splitIdx.end()) return it->second;
            Vec3 d = vertices[o] - vertices[v];
            float len = d.Magnitude();
            float t = len > 1e-8f ? std::min(amount, len * 0.45f) / len : 0.0f;
            int idx = (int)vertices.size();
            vertices.push_back(vertices[v] + d * t);
            if (hadUV) uvs.push_back(uvs[v]);
            splitIdx[k] = idx;
            ringOf[v].push_back(idx);
            return idx;
        };
        std::vector<int> out;
        out.reserve(triangles.size() * 2);
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            int a = triangles[t], b = triangles[t + 1], c = triangles[t + 2];
            int poly[6]; int pn = 0;
            // Walk the perimeter a->b->c; a chamfered corner contributes its two
            // edge split points in walk order (arrive-edge first, leave-edge second).
            auto corner = [&](int v, int prev, int nxt) {
                if (!S.count(v)) { poly[pn++] = v; return; }
                poly[pn++] = splitPoint(v, prev);
                poly[pn++] = splitPoint(v, nxt);
            };
            corner(a, c, b); corner(b, a, c); corner(c, b, a);
            for (int i = 1; i + 1 < pn; ++i)
                out.insert(out.end(), {poly[0], poly[i], poly[i + 1]});
        }
        triangles = std::move(out);
        // Cap each chamfered corner with a facet over its ring, ordered by angle
        // around the old vertex normal so the fan faces outward.
        for (auto& kv : ringOf) {
            std::vector<int>& ring = kv.second;
            if (ring.size() < 3) continue;
            Vec3 nrm = vn[kv.first];
            if (nrm.Magnitude() < 1e-6f) nrm = {0, 1, 0};
            Vec3 u = Vec3::Cross(nrm, std::fabs(nrm.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0});
            { float mu = u.Magnitude(); u = mu > 1e-8f ? u * (1.0f / mu) : Vec3{1, 0, 0}; }
            Vec3 w = Vec3::Cross(nrm, u);
            Vec3 c{0, 0, 0};
            for (int i : ring) c += vertices[i];
            c = c * (1.0f / (float)ring.size());
            std::sort(ring.begin(), ring.end(), [&](int i, int j) {
                Vec3 di = vertices[i] - c, dj = vertices[j] - c;
                return std::atan2(Vec3::Dot(di, w), Vec3::Dot(di, u))
                     < std::atan2(Vec3::Dot(dj, w), Vec3::Dot(dj, u));
            });
            for (std::size_t i = 1; i + 1 < ring.size(); ++i)
                triangles.insert(triangles.end(), {ring[0], (int)ring[i], (int)ring[i + 1]});
        }
        triColors.clear();                               // face records changed shape
        name = "";
        RefreshNormals();
    }

    /// Displace vertices by a deterministic random offset up to `amount` — quick
    /// organic roughness (rocks, rubble, hand-made look). Empty `verts` = whole mesh.
    void JitterVertices(const std::vector<int>& verts, float amount, int seed = 0) {
        if (amount == 0.0f || vertices.empty()) return;
        auto h = [&](int i, int c) {
            unsigned x = (unsigned)(i * 73856093 ^ c * 19349663 ^ seed * 83492791);
            x = (x ^ (x >> 13)) * 1274126177u;
            return ((float)((x ^ (x >> 16)) & 0xffffu) / 65535.0f) * 2.0f - 1.0f;
        };
        auto apply = [&](int v) {
            vertices[v] += Vec3{h(v, 1), h(v, 2), h(v, 3)} * amount;
        };
        if (verts.empty())
            for (int i = 0; i < (int)vertices.size(); ++i) apply(i);
        else
            for (int v : verts)
                if (v >= 0 && v < (int)vertices.size()) apply(v);
        name = "";
        RefreshNormals();
    }

    /// Shear: slide the mesh along `axis` proportionally to the coordinate on
    /// `along` (about the centre) — slanted walls, italic text blocks, leaning towers.
    void Shear(int axis, int along, float amount) {
        if (axis < 0 || axis > 2 || along < 0 || along > 2 || axis == along
            || amount == 0.0f || vertices.empty()) return;
        Vec3 lo, hi; Bounds(lo, hi);
        float c = ((&lo.x)[along] + (&hi.x)[along]) * 0.5f;
        for (Vec3& v : vertices)
            (&v.x)[axis] += ((&v.x)[along] - c) * amount;
        name = "";
        RefreshNormals();
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
