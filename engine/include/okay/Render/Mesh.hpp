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
#include <tuple>
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
    float build        = 0.86f;  // limb/torso thickness (lean, low body-fat base)
    float headSize     = 0.66f;  // realistic ~7.5-head proportions (small adult head)
    float shoulderWidth= 0.98f;  // arm spacing + torso top width
    float hipWidth     = 0.8f;   // leg spacing + hip width (narrow hips)
    float armLength    = 1.1f;   // arms reach toward mid-thigh
    float legLength    = 1.16f;  // long lean legs (taller, adult build)
    float neckLength   = 1.0f;
    float handSize     = 0.95f;
    float footSize     = 1.0f;
    float armSpread    = 34.0f;  // degrees arms angle out (A-pose; clear arm/torso separation)
    float legSpread    = 6.0f;   // degrees legs angle out (stance width)
    float torsoLength  = 1.0f;   // lengthens the torso (raises the upper body)
    float bodyDepth    = 0.88f;  // front-to-back thickness of torso + hips (lean)
    int   hairStyle    = 1;      // 0 cap, 1 short, 2 long, 3 spiky, 4 ponytail, 5 mohawk
    float eyeSpacing   = 1.0f;   // multiplier on the gap between the eyes
    float mouthWidth   = 1.0f;   // multiplier on mouth width (a wider "smile")
    float browAngle    = 0.0f;   // degrees; +tilts brows inward/down (angry), -up (worried)
    bool  ears         = true;   // add ears to the sides of the head
    float armSwing     = 0.0f;   // degrees: fore/aft arm swing (animation; antisymmetric)
    float legSwing     = 0.0f;   // degrees: fore/aft leg swing (animation; antisymmetric)
    Vec3  rightArmRot  = {0, 0, 0}; // extra euler on the RIGHT arm only (wave)
    float eyeSize      = 1.0f;   // multiplier on eye + pupil size
    float noseSize     = 1.0f;   // multiplier on nose size
    float armThickness = 0.9f;   // arm girth (independent of build)
    float legThickness = 0.98f;  // leg girth (independent of build)
    float waist        = 0.8f;   // hip/midsection width (lean, defined waist)
    float belly        = 0.0f;   // belly size (0 = none)
    float armGap       = 0.0f;   // lateral spacing of arms from the body (+out, -in)
    float legGap       = 0.0f;   // lateral spacing of legs (stance width; +apart, -together)
    float gender       = 0.5f;   // 0 = female, 1 = male (shoulders/hips/waist/bust)
    float muscle       = 0.5f;   // 0 = slim, 1 = muscular (limb + torso girth)

    /// Constrain every proportion to a believable human range so the character
    /// always reads as a person — no matter how the sliders are dragged. Applied
    /// when the mesh is built (the underlying values are left untouched).
    void ClampHuman() {
        auto cl = [](float& v, float lo, float hi) { v = v < lo ? lo : (v > hi ? hi : v); };
        cl(height, 0.82f, 1.35f);     cl(build, 0.85f, 1.22f);
        cl(headSize, 0.55f, 0.95f);   cl(neckLength, 0.8f, 1.18f);
        cl(shoulderWidth, 0.95f, 1.28f); cl(hipWidth, 0.8f, 1.12f);
        cl(armLength, 0.9f, 1.15f);   cl(legLength, 0.92f, 1.22f);
        cl(armThickness, 0.82f, 1.18f);cl(legThickness, 0.85f, 1.18f);
        cl(torsoLength, 0.9f, 1.12f); cl(bodyDepth, 0.82f, 1.08f);
        cl(handSize, 0.8f, 1.18f);    cl(footSize, 0.82f, 1.25f);
        cl(waist, 0.8f, 1.15f);       cl(belly, 0.0f, 0.85f);
        cl(armSpread, 0.0f, 50.0f);   cl(legSpread, 0.0f, 14.0f);
        cl(armGap, -0.05f, 0.10f);    cl(legGap, -0.05f, 0.14f);
        cl(eyeSpacing, 0.75f, 1.3f);  cl(eyeSize, 0.75f, 1.35f);
        cl(mouthWidth, 0.65f, 1.4f);  cl(noseSize, 0.7f, 1.5f);
        cl(browAngle, -25.0f, 25.0f); cl(gender, 0.0f, 1.0f); cl(muscle, 0.0f, 1.0f);
        // Realism coupling: hips never read much wider than the shoulders, and the
        // waist can't be wider than the chest (keeps a natural human taper).
        if (hipWidth > shoulderWidth + 0.10f) hipWidth = shoulderWidth + 0.10f;
        float maxWaist = shoulderWidth * 1.15f;
        if (waist > maxWaist) waist = maxWaist;
    }
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
    bool  beard = false;         // chin/jaw beard (hair color)
    bool  mustache = false;      // mustache under the nose (hair color)
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
        std::vector<Vec3> pos; std::vector<Vec2> uv;
        std::vector<std::vector<std::pair<int, int>>> faces;   // per-corner (posIdx, uvIdx), 0-based
        std::string mtllib, line;
        while (std::getline(f, line)) {
            std::istringstream ss(line); std::string tag; ss >> tag;
            if (tag == "v") { Vec3 v; ss >> v.x >> v.y >> v.z; pos.push_back(v); }
            else if (tag == "vt") { Vec2 t; ss >> t.x >> t.y; uv.push_back(t); }
            else if (tag == "mtllib") { ss >> mtllib; }
            else if (tag == "f") {
                std::vector<std::pair<int, int>> face; std::string tok;
                while (ss >> tok) {
                    int vi = 0, ti = 0; std::sscanf(tok.c_str(), "%d/%d", &vi, &ti);
                    if (vi < 0) vi = (int)pos.size() + vi + 1;
                    if (ti < 0) ti = (int)uv.size() + ti + 1;
                    face.push_back({vi - 1, ti - 1});
                }
                if (face.size() >= 3) faces.push_back(std::move(face));
            }
        }
        auto tri = [&](int a, int b, int cc) { m.triangles.insert(m.triangles.end(), {a, b, cc}); };
        if (uv.empty()) {                              // no texcoords: 1:1 with v lines
            m.vertices = pos;
            for (auto& fc : faces)
                for (std::size_t i = 2; i < fc.size(); ++i) tri(fc[0].first, fc[i - 1].first, fc[i].first);
        } else {                                       // de-index per (pos,uv) so UVs map right
            std::map<std::pair<int, int>, int> remap;
            auto corner = [&](std::pair<int, int> c) {
                auto it = remap.find(c); if (it != remap.end()) return it->second;
                int ni = (int)m.vertices.size();
                m.vertices.push_back((c.first >= 0 && c.first < (int)pos.size()) ? pos[c.first] : Vec3{0, 0, 0});
                m.uvs.push_back((c.second >= 0 && c.second < (int)uv.size()) ? uv[c.second] : Vec2{0, 0});
                remap[c] = ni; return ni;
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

    /// Concatenate another mesh wholesale (vertices, rebased triangles, face
    /// colors) — used to merge pre-built parts into one mesh.
    void Append(const Mesh& src) {
        int base = (int)vertices.size();
        vertices.insert(vertices.end(), src.vertices.begin(), src.vertices.end());
        for (int t : src.triangles) triangles.push_back(t + base);
        triColors.insert(triColors.end(), src.triColors.begin(), src.triColors.end());
    }

    /// A low-poly humanoid blockout assembled from primitive parts (head, neck,
    /// torso, hips, arms, hands, legs, feet), shaped by `p`. Stands on ~Y=0.
    /// Subdivide()/SubdivideSmooth() it to raise it from low-poly to high-poly.
    /// When `c` is given, parts are colored per-region (skin/shirt/pants/shoes),
    /// a hair cap and a simple face (eyes/brows/nose/mouth) are added optionally.
    /// Build the merged single-mesh humanoid (see BuildHumanoidParts for the
    /// per-part layout). Defined out-of-line below the struct.
    static Mesh Humanoid(const HumanoidParams& p, const HumanoidColors* c = nullptr);
    static Mesh Humanoid();

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
};

/// One named body part of a humanoid (its own positioned, colored mesh).
struct HumanoidPart { std::string name; Mesh mesh; };

/// Build the humanoid as a list of named parts (Head, Neck, Torso, Hips, Arm.L,
/// Arm.R, Leg.L, Leg.R). Each part is a self-contained, character-local mesh —
/// merge them for one object, or spawn one GameObject per part to rig/animate.
inline std::vector<HumanoidPart> BuildHumanoidParts(const HumanoidParams& p,
                                                    const HumanoidColors* c = nullptr) {
    std::vector<HumanoidPart> parts;
    parts.reserve(16);
    auto add = [&](const char* nm) -> Mesh& { parts.push_back({nm, Mesh{}}); return parts.back().mesh; };
    const float H = p.height, B = p.build, hd = p.headSize;
    const float sw = 0.24f * p.shoulderWidth, hw = 0.20f * p.hipWidth;
    const float aL = p.armLength, lL = p.legLength, bd = p.bodyDepth;
    const Color* skin  = c ? &c->skin  : nullptr;
    const Color* shirt = c ? &c->shirt : nullptr;
    const Color* pants = c ? &c->pants : nullptr;
    const Color* shoes = c ? &c->shoes : nullptr;
    const float up = 0.78f * H * (p.torsoLength - 1.0f);
    const float headY = 1.78f * H + up;

    Mesh& head = add("Head");
    // Skull shaped like a human head: a tall rounded cranium, cheekbones at the
    // widest point, tapering down to a narrower jaw and a small chin (an egg, not
    // a ball).
    head.Add(Mesh::Sphere(0.5f, 12, 14), {0.0f, headY + 0.05f * hd, -0.01f * hd},
             {0.33f * hd, 0.40f * hd, 0.355f * hd}, skin);                              // cranium
    head.Add(Mesh::Sphere(0.5f, 10, 12), {0.0f, headY - 0.04f * hd, 0.07f * hd},
             {0.305f * hd, 0.28f * hd, 0.31f * hd}, skin);                              // brow / cheekbones (widest)
    head.Add(Mesh::Sphere(0.5f, 9, 11), {0.0f, headY - 0.17f * hd, 0.05f * hd},
             {0.225f * hd, 0.22f * hd, 0.26f * hd}, skin);                              // jaw (narrower)
    head.Add(Mesh::Sphere(0.5f, 7, 8), {0.0f, headY - 0.255f * hd, 0.10f * hd},
             {0.135f * hd, 0.13f * hd, 0.155f * hd}, skin);                             // chin
    if (c && c->hasHair) {
        const Color* h = &c->hair;
        head.Add(Mesh::Sphere(0.5f, 5, 8), {0.0f, headY + 0.10f * hd, 0.0f}, {0.44f * hd, 0.34f * hd, 0.44f * hd}, h);
        switch (p.hairStyle) {
            case 2: head.Add(Mesh::Cube(1.0f), {0.0f, headY - 0.22f * hd, -0.16f * hd}, {0.40f * hd, 0.55f * hd, 0.16f * hd}, h); break;
            case 3: for (int sx = -1; sx <= 1; ++sx) for (int sz = -1; sz <= 1; ++sz)
                        head.Add(Mesh::Pyramid(1.0f), {sx * 0.13f * hd, headY + 0.26f * hd, sz * 0.13f * hd}, {0.10f * hd, 0.16f * hd, 0.10f * hd}, h); break;
            case 4: head.Add(Mesh::Capsule(0.5f, 1.0f, 6, 3), {0.0f, headY - 0.05f * hd, -0.26f * hd}, {0.12f * hd, 0.34f * hd, 0.12f * hd}, h); break;
            case 5: head.Add(Mesh::Cube(1.0f), {0.0f, headY + 0.24f * hd, 0.0f}, {0.08f * hd, 0.22f * hd, 0.46f * hd}, h); break;
            case 6: head.Add(Mesh::Sphere(0.5f, 6, 8), {0.0f, headY + 0.30f * hd, -0.08f * hd}, {0.26f * hd, 0.26f * hd, 0.26f * hd}, h); break;
            case 7: head.Add(Mesh::Sphere(0.5f, 7, 9), {0.0f, headY + 0.16f * hd, 0.0f}, {0.62f * hd, 0.56f * hd, 0.62f * hd}, h); break;
            default: break;
        }
    }
    if (c && c->hasHat) {
        head.Add(Mesh::Cylinder(0.5f, 1.0f, 10), {0.0f, headY + 0.20f * hd, 0.0f}, {0.64f * hd, 0.04f * hd, 0.64f * hd}, &c->hat);
        head.Add(Mesh::Cylinder(0.5f, 1.0f, 10), {0.0f, headY + 0.34f * hd, 0.0f}, {0.42f * hd, 0.24f * hd, 0.42f * hd}, &c->hat);
    }
    if (c && p.ears)
        for (int s = -1; s <= 1; s += 2)
            head.Add(Mesh::Sphere(0.5f, 4, 5), {s * 0.21f * hd, headY + 0.01f * hd, 0.0f}, {0.05f * hd, 0.09f * hd, 0.07f * hd}, skin);
    if (c && c->hasFace) {
        const float fz = 0.19f * hd, ex = 0.09f * hd * p.eyeSpacing, es = p.eyeSize;
        Color pupil{c->eye.r * 0.25f, c->eye.g * 0.25f, c->eye.b * 0.25f, 1.0f};
        for (int s = -1; s <= 1; s += 2) {
            head.Add(Mesh::Sphere(0.5f, 4, 5), {s * ex, headY + 0.04f * hd, fz}, {0.06f * hd * es, 0.07f * hd * es, 0.05f * hd}, &c->eye);
            head.Add(Mesh::Sphere(0.5f, 3, 4), {s * ex, headY + 0.04f * hd, fz + 0.03f * hd}, {0.028f * hd * es, 0.032f * hd * es, 0.02f * hd}, &pupil);
            head.AddPosed(Mesh::Cube(1.0f), {s * ex, headY + 0.12f * hd, fz}, {0.09f * hd, 0.025f * hd, 0.04f * hd}, {0.0f, 0.0f, (float)s * p.browAngle}, {s * ex, headY + 0.12f * hd, fz}, &c->hair);
        }
        head.Add(Mesh::Cube(1.0f), {0.0f, headY - 0.01f * hd, fz + 0.02f * hd}, {0.05f * hd * p.noseSize, 0.08f * hd * p.noseSize, 0.06f * hd * p.noseSize}, &c->skin);
        head.Add(Mesh::Cube(1.0f), {0.0f, headY - 0.13f * hd, fz}, {0.12f * hd * p.mouthWidth, 0.025f * hd, 0.04f * hd}, &c->hair);
        if (c->mustache) head.Add(Mesh::Cube(1.0f), {0.0f, headY - 0.08f * hd, fz}, {0.13f * hd, 0.03f * hd, 0.05f * hd}, &c->hair);
        if (c->beard)    head.Add(Mesh::Sphere(0.5f, 5, 6), {0.0f, headY - 0.19f * hd, fz - 0.04f * hd}, {0.30f * hd, 0.20f * hd, 0.20f * hd}, &c->hair);
        if (c->hasGlasses) head.Add(Mesh::Cube(1.0f), {0.0f, headY + 0.04f * hd, fz + 0.03f * hd}, {(0.18f * p.eyeSpacing + 0.14f) * hd, 0.06f * hd, 0.03f * hd}, &c->glasses);
    }

    add("Neck").Add(Mesh::Cylinder(0.5f, 1.0f, 10), {0.0f, 1.50f * H + up, 0.0f}, {0.105f, 0.22f * p.neckLength, 0.105f}, skin);

    Mesh& torso = add("Torso");
    {   // Anatomical V-taper (matches the seamless body's proportions): a tall
        // chest/ribcage narrowing to a defined waist. Sphere scale = 2x the
        // seamless ellipsoid radii (a unit sphere has radius 0.5).
        const float sW = p.shoulderWidth;
        torso.Add(Mesh::Sphere(0.5f, 10, 12), {0.0f, 1.40f * H + up, 0.0f},
                  {0.30f * sW * B, 0.23f * H, 0.27f * B * bd}, shirt);          // clavicle/traps
        torso.Add(Mesh::Sphere(0.5f, 10, 12), {0.0f, 1.29f * H + up, 0.01f * bd},
                  {0.44f * sW * B, 0.30f * H, 0.32f * B * bd}, shirt);          // chest
        torso.Add(Mesh::Sphere(0.5f, 10, 12), {0.0f, 1.12f * H + up * 0.7f, 0.0f},
                  {0.376f * sW * B, 0.33f * H, 0.30f * B * bd}, shirt);         // ribcage
        torso.Add(Mesh::Sphere(0.5f, 9, 11), {0.0f, 0.98f * H + up * 0.35f, 0.0f},
                  {0.30f * B * p.waist, 0.30f * H, 0.27f * B * bd}, shirt);     // waist (narrow)
    }
    if (p.belly > 0.05f)
        torso.Add(Mesh::Sphere(0.5f, 6, 8), {0.0f, 0.92f * H, 0.18f * bd}, {0.42f * B * p.belly, 0.36f * H * p.belly, 0.32f * bd * p.belly}, shirt);

    add("Hips").Add(Mesh::Sphere(0.5f, 9, 12), {0.0f, 0.68f * H, 0.0f}, {0.41f * p.hipWidth * B, 0.30f * H, 0.33f * B * bd}, pants);

    for (int s = -1; s <= 1; s += 2) {
        Mesh& arm = add(s < 0 ? "Arm.L" : "Arm.R");
        float aw = sw + p.armGap;                       // shoulder x incl. lateral spacing
        float shoulderY = 1.46f * H + up;
        Vec3 shoulder{s * aw, shoulderY, 0.0f};
        Vec3 armRot{(float)s * p.armSwing, 0.0f, (float)s * p.armSpread};   // + spreads outward
        if (s == 1) { armRot.x += p.rightArmRot.x; armRot.y += p.rightArmRot.y; armRot.z += p.rightArmRot.z; }
        float at = 0.155f * B * p.armThickness;
        float armLen = 0.82f * aL * H;
        float armCY = shoulderY - armLen * 0.5f;       // arm hangs from the shoulder
        float wristY = shoulderY - armLen;             // bottom of the arm
        // Small shoulder joint that just fills the arm/torso seam.
        arm.Add(Mesh::Sphere(0.5f, 6, 7), {s * aw * 0.85f, shoulderY, 0.0f}, {at * 1.2f, at * 1.15f, at * 1.2f}, shirt);
        arm.AddPosed(Mesh::Capsule(0.5f, 1.0f, 9, 3), {s * aw, armCY, 0.0f}, {at, armLen, at}, armRot, shoulder, shirt);
        // Relaxed hand: a rounded palm with the fingers softly closed into a
        // single mitt (the old splayed cubes read as a claw) plus a thumb.
        float hsz = 0.115f * B * p.handSize;
        Vec3 hp{s * aw, wristY + hsz * 0.15f, 0.0f};   // hand at the wrist (overlaps the arm)
        arm.AddPosed(Mesh::Sphere(0.5f, 6, 7), hp, {hsz * 0.82f, hsz * 0.78f, hsz * 0.52f}, armRot, shoulder, skin);  // palm
        arm.AddPosed(Mesh::Sphere(0.5f, 6, 7), {hp.x, hp.y - hsz * 0.66f, hp.z + hsz * 0.04f},
                     {hsz * 0.74f, hsz * 0.62f, hsz * 0.48f}, armRot, shoulder, skin);    // fingers (relaxed/closed)
        arm.AddPosed(Mesh::Sphere(0.5f, 5, 6), {hp.x + s * hsz * 0.42f, hp.y - hsz * 0.18f, hp.z + hsz * 0.06f},
                     {hsz * 0.34f, hsz * 0.5f, hsz * 0.34f}, armRot, shoulder, skin);     // thumb
    }
    for (int s = -1; s <= 1; s += 2) {
        Mesh& leg = add(s < 0 ? "Leg.L" : "Leg.R");
        float lw = hw + p.legGap;                       // hip x incl. lateral spacing
        Vec3 hip{s * lw, 0.60f * H, 0.0f};
        Vec3 legRot{(float)s * p.legSwing, 0.0f, (float)s * p.legSpread};   // + spreads outward
        float lt = 0.20f * B * p.legThickness;
        leg.Add(Mesh::Sphere(0.5f, 6, 7), {s * lw, 0.54f * H, 0.0f}, {lt * 1.05f, lt * 1.05f, lt * 1.05f}, pants);
        leg.AddPosed(Mesh::Capsule(0.5f, 1.0f, 9, 3), {s * lw, 0.06f * H, 0.0f}, {lt, 1.15f * lL * H, lt}, legRot, hip, pants);
        float fsz = p.footSize, fy = (0.06f - 0.57f * lL) * H;
        leg.AddPosed(Mesh::Sphere(0.5f, 5, 6), {s * lw, fy, 0.0f}, {lt * 0.9f, lt * 0.9f, lt * 0.9f}, legRot, hip, skin);     // ankle
        leg.AddPosed(Mesh::Cube(1.0f), {s * lw, fy - 0.06f * H, 0.16f * fsz}, {0.22f * B * fsz, 0.11f * fsz, 0.58f * fsz}, legRot, hip, shoes); // sole
        leg.AddPosed(Mesh::Sphere(0.5f, 5, 7), {s * lw, fy - 0.055f * H, 0.42f * fsz}, {0.22f * B * fsz, 0.13f * fsz, 0.22f * fsz}, legRot, hip, shoes); // rounded toe
    }
    return parts;
}

inline Mesh Mesh::Humanoid(const HumanoidParams& p, const HumanoidColors* c) {
    Mesh m; m.name = "Human";
    for (const HumanoidPart& pt : BuildHumanoidParts(p, c)) m.Append(pt.mesh);
    return m;
}
inline Mesh Mesh::Humanoid() { return Humanoid(HumanoidParams{}); }

// ---- Seamless body via signed-distance field + Surface Nets ----------------

/// Naive Surface Nets: extract a smooth watertight mesh from an SDF (`sdf` < 0
/// inside) over [lo,hi] at `res` cells/axis. Each face is colored by `colorAt`
/// at its centroid. Used to fuse overlapping body blobs into one seamless skin.
inline Mesh SurfaceNets(const std::function<float(const Vec3&)>& sdf,
                        const std::function<Color(const Vec3&)>& colorAt,
                        const Vec3& lo, const Vec3& hi, int res) {
    Mesh out;
    if (res < 2) res = 2; if (res > 96) res = 96;
    const int nx = res, ny = res, nz = res, sx = nx + 1, sy = ny + 1, sz = nz + 1;
    Vec3 cs{(hi.x - lo.x) / nx, (hi.y - lo.y) / ny, (hi.z - lo.z) / nz};
    auto pos = [&](int i, int j, int k) { return Vec3{lo.x + i * cs.x, lo.y + j * cs.y, lo.z + k * cs.z}; };
    auto SI = [&](int i, int j, int k) { return ((std::size_t)k * sy + j) * sx + i; };
    std::vector<float> d((std::size_t)sx * sy * sz);
    for (int k = 0; k < sz; ++k) for (int j = 0; j < sy; ++j) for (int i = 0; i < sx; ++i)
        d[SI(i, j, k)] = sdf(pos(i, j, k));
    auto CI = [&](int i, int j, int k) { return ((std::size_t)k * ny + j) * nx + i; };
    std::vector<int> cellV((std::size_t)nx * ny * nz, -1);
    static const int co[8][3] = {{0,0,0},{1,0,0},{0,1,0},{1,1,0},{0,0,1},{1,0,1},{0,1,1},{1,1,1}};
    static const int ed[12][2] = {{0,1},{0,2},{0,4},{1,3},{1,5},{2,3},{2,6},{3,7},{4,5},{4,6},{5,7},{6,7}};
    for (int k = 0; k < nz; ++k) for (int j = 0; j < ny; ++j) for (int i = 0; i < nx; ++i) {
        float cd[8]; Vec3 cp[8]; bool neg = false, posb = false;
        for (int c = 0; c < 8; ++c) {
            int ii = i + co[c][0], jj = j + co[c][1], kk = k + co[c][2];
            cd[c] = d[SI(ii, jj, kk)]; cp[c] = pos(ii, jj, kk);
            if (cd[c] < 0) neg = true; else posb = true;
        }
        if (!(neg && posb)) continue;
        Vec3 sum{0, 0, 0}; int cnt = 0;
        for (int e = 0; e < 12; ++e) {
            int a = ed[e][0], b = ed[e][1];
            if ((cd[a] < 0) != (cd[b] < 0)) {
                float t = cd[a] / (cd[a] - cd[b]);
                sum = sum + cp[a] * (1.0f - t) + cp[b] * t; ++cnt;
            }
        }
        cellV[CI(i, j, k)] = (int)out.vertices.size();
        out.vertices.push_back(cnt ? sum * (1.0f / cnt) : (cp[0] + cp[7]) * 0.5f);
    }
    auto quad = [&](int a, int b, int c2, int e, bool flip) {
        if (a < 0 || b < 0 || c2 < 0 || e < 0) return;
        if (!flip) out.triangles.insert(out.triangles.end(), {a, b, c2, a, c2, e});
        else       out.triangles.insert(out.triangles.end(), {a, c2, b, a, e, c2});
    };
    for (int k = 0; k < sz; ++k) for (int j = 0; j < sy; ++j) for (int i = 0; i < sx; ++i) {
        float dc = d[SI(i, j, k)];
        if (i < nx && j > 0 && k > 0 && (dc < 0) != (d[SI(i + 1, j, k)] < 0))
            quad(cellV[CI(i, j - 1, k - 1)], cellV[CI(i, j, k - 1)], cellV[CI(i, j, k)], cellV[CI(i, j - 1, k)], dc < 0);
        if (j < ny && i > 0 && k > 0 && (dc < 0) != (d[SI(i, j + 1, k)] < 0))
            quad(cellV[CI(i - 1, j, k - 1)], cellV[CI(i, j, k - 1)], cellV[CI(i, j, k)], cellV[CI(i - 1, j, k)], !(dc < 0));
        if (k < nz && i > 0 && j > 0 && (dc < 0) != (d[SI(i, j, k + 1)] < 0))
            quad(cellV[CI(i - 1, j - 1, k)], cellV[CI(i, j - 1, k)], cellV[CI(i, j, k)], cellV[CI(i - 1, j, k)], dc < 0);
    }
    out.triColors.reserve(out.triangles.size() / 3);
    for (std::size_t t = 0; t + 2 < out.triangles.size(); t += 3) {
        Vec3 ctr = (out.vertices[out.triangles[t]] + out.vertices[out.triangles[t + 1]] +
                    out.vertices[out.triangles[t + 2]]) * (1.0f / 3.0f);
        out.triColors.push_back(colorAt(ctr));
    }
    return out;
}

/// Seamless humanoid: the body masses are blended into ONE continuous skin via
/// smooth-union SDFs + Surface Nets, then the detailed head is appended on top.
inline Mesh BuildSmoothHumanoid(const HumanoidParams& p, const HumanoidColors* c, int res) {
    struct Blob { int kind; Vec3 a, b, rad; float r; Color col; };  // 0 sphere,1 capsule,2 ellipsoid
    std::vector<Blob> bl;
    Color skin  = c ? c->skin  : Color::White;
    Color shirt = c ? c->shirt : Color::White;
    Color pants = c ? c->pants : Color::White;
    Color shoes = c ? c->shoes : Color::White;
    const float H = p.height, B = p.build, bd = p.bodyDepth;
    const float sw = 0.24f * p.shoulderWidth, hw = 0.20f * p.hipWidth;
    const float aL = p.armLength, lL = p.legLength;
    const float up = 0.78f * H * (p.torsoLength - 1.0f);
    const float sW = p.shoulderWidth, hW = p.hipWidth;
    // ---- Neck: a slim tapered column blending head to shoulders. ----
    bl.push_back({1, {0, 1.40f * H + up, 0}, {0, 1.61f * H + up, 0}, {}, 0.072f * p.neckLength + 0.034f, skin});
    // ---- Torso: anatomical V-taper — broad shoulders/clavicle, pecs/ribcage,
    //      a narrow waist, then the pelvis. Each mass overlaps its neighbour so
    //      the smooth-union makes one continuous trunk. ----
    bl.push_back({2, {0, 1.40f * H + up, 0}, {}, {0.150f * sW * B, 0.115f * H, 0.135f * B * bd}, 0, shirt}); // base of neck (narrow; shoulders slope out via traps)
    bl.push_back({2, {0, 1.29f * H + up, 0.01f * bd}, {}, {0.220f * sW * B, 0.150f * H, 0.160f * B * bd}, 0, shirt}); // upper chest (pecs)
    bl.push_back({2, {0, 1.12f * H + up * 0.7f, 0}, {}, {0.188f * sW * B, 0.165f * H, 0.150f * B * bd}, 0, shirt}); // lower ribcage
    bl.push_back({2, {0, 1.00f * H + up * 0.45f, 0}, {}, {0.160f * sW * B, 0.120f * H, 0.140f * B * bd}, 0, shirt}); // upper abdomen
    bl.push_back({2, {0, 0.88f * H + up * 0.25f, 0}, {}, {0.150f * B * p.waist, 0.140f * H, 0.132f * B * bd}, 0, shirt}); // waist (narrowest)
    if (p.belly > 0.05f)
        bl.push_back({2, {0, 0.95f * H, 0.14f * bd}, {}, {0.21f * B * p.belly, 0.18f * H * p.belly, 0.16f * bd * p.belly}, 0, shirt}); // belly
    bl.push_back({2, {0, 0.68f * H, 0}, {}, {0.205f * hW * B, 0.135f * H, 0.165f * B * bd}, 0, pants}); // pelvis
    struct HandRec { Vec3 wrist; Quat fq; int s; };
    std::vector<HandRec> hands;   // fingers are appended at full detail after meshing
    for (int s = -1; s <= 1; s += 2) {
        // ---- Arm: deltoid cap, tapering bicep -> forearm, then a hand with a
        //      flattened palm + thumb (no more featureless stump). ----
        float aw = sw + p.armGap;
        float shoulderY = 1.46f * H + up, at = 0.140f * B * p.armThickness, armLen = 0.82f * aL * H;
        Vec3 sh{s * aw, shoulderY, 0};
        Quat q = Quat::Euler({(float)s * p.armSwing, 0, (float)s * p.armSpread});
        Vec3 elbow = sh + q * Vec3{0, -armLen * 0.5f, 0};
        // Relaxed pose: the forearm bends slightly forward and inward at the elbow
        // so the arm rests naturally instead of hanging like a stiff straight rod.
        Quat fq = q * Quat::Euler({16.0f, 0.0f, (float)s * -7.0f});
        Vec3 foreDir = fq * Vec3{0, -1, 0};
        Vec3 wrist = elbow + foreDir * (armLen * 0.5f);
        hands.push_back({wrist, fq, s});
        // Trapezius: a muscle sloping from the neck base DOWN to the shoulder, so
        // the shoulder line slopes naturally instead of forming a flat square bar.
        bl.push_back({1, {0, 1.52f * H + up, -0.01f * bd}, {s * aw * 0.80f, shoulderY + 0.01f * H, 0}, {}, 0.075f, shirt});
        bl.push_back({1, {s * 0.14f * sW * B, shoulderY, 0}, sh, {}, at * 1.05f, shirt}); // shoulder bridge (closes armpit)
        bl.push_back({1, sh, Vec3{s * aw, shoulderY - 0.06f * H, 0}, {}, at * 1.05f, shirt}); // deltoid (rounded, drops to upper arm)
        // Pectoral: a soft chest plate on the front, one per side, for definition.
        bl.push_back({2, {s * 0.10f * sW * B, 1.27f * H + up, 0.10f * B * bd}, {},
                      {0.110f * sW * B, 0.095f * H, 0.085f * B * bd}, 0, shirt});
        bl.push_back({3, sh, elbow, {at * 0.80f, 0, 0}, at * 1.10f, shirt});       // upper arm (taper to elbow)
        bl.push_back({0, elbow, {}, {}, at * 0.84f, shirt});                       // elbow
        bl.push_back({3, elbow, wrist, {at * 0.50f, 0, 0}, at * 0.90f, skin});     // forearm (taper to thin wrist)
        Vec3 hdir = foreDir;
        float hs = 0.098f * B * p.handSize;
        bl.push_back({2, wrist + hdir * (hs * 1.0f), {}, {hs * 0.90f, hs * 1.15f, hs * 0.48f}, 0, skin}); // palm
        bl.push_back({0, wrist + hdir * (hs * 0.6f) + Vec3{s * hs * 0.5f, 0, hs * 0.2f}, {}, {}, hs * 0.40f, skin}); // thumb
        // ---- Leg: glute, tapering thigh -> knee -> calf, ankle, forward foot. ----
        float lt = 0.195f * B * p.legThickness, lw = hw + p.legGap;
        Vec3 hip{s * lw, 0.60f * H, 0};
        Quat ql = Quat::Euler({(float)s * p.legSwing, 0, (float)s * p.legSpread});
        float legLen = 0.6f * H + 0.55f * lL * H;
        Vec3 knee = hip + ql * Vec3{0, -legLen * 0.5f, 0};
        Vec3 ankle = hip + ql * Vec3{0, -legLen, 0};
        bl.push_back({1, {s * 0.08f * hW * B, 0.64f * H, 0}, hip, {}, lt * 0.92f, pants}); // hip bridge
        bl.push_back({0, hip, {}, {}, lt * 0.95f, pants});                         // glute / hip
        bl.push_back({3, hip, knee, {lt * 0.78f, 0, 0}, lt * 1.05f, pants});       // thigh (taper to knee)
        bl.push_back({0, knee, {}, {}, lt * 0.80f, pants});                        // knee
        bl.push_back({3, knee, ankle, {lt * 0.48f, 0, 0}, lt * 0.92f, pants});     // calf -> thin ankle
        // Calf muscle: a bulge on the upper-back of the shin for leg definition.
        bl.push_back({2, knee + ql * Vec3{0, -legLen * 0.30f, 0} + Vec3{0, 0, -0.05f * bd}, {},
                      {lt * 0.78f, lt * 1.30f, lt * 0.95f}, 0, pants});
        bl.push_back({0, ankle, {}, {}, lt * 0.55f, pants});                       // ankle (slim; trouser cuff)
        // Foot: a flat, elongated shoe shape (heel under the ankle to a tapered
        // toe) instead of a bulbous blob.
        bl.push_back({2, ankle + Vec3{0, -0.075f * H, 0.085f}, {}, {0.070f * B, 0.045f, 0.205f}, 0, shoes}); // sole (long, flat)
        bl.push_back({2, ankle + Vec3{0, -0.080f * H, 0.215f}, {}, {0.060f * B, 0.038f, 0.080f}, 0, shoes}); // toe cap
        bl.push_back({0, ankle + Vec3{0, -0.045f * H, -0.05f}, {}, {}, lt * 0.48f, shoes}); // heel
    }
    auto segd = [](const Vec3& pt, const Vec3& a, const Vec3& b) {
        Vec3 ab = b - a, ap = pt - a; float dd = Vec3::Dot(ab, ab);
        float t = dd > 1e-6f ? Vec3::Dot(ap, ab) / dd : 0.0f;
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        return (pt - (a + ab * t)).Magnitude();
    };
    auto one = [&](const Blob& b2, const Vec3& pt) -> float {
        if (b2.kind == 0) return (pt - b2.a).Magnitude() - b2.r;
        if (b2.kind == 1) return segd(pt, b2.a, b2.b) - b2.r;
        if (b2.kind == 3) {   // tapered capsule (round cone): radius r at a -> rad.x at b
            float r1 = b2.r, r2 = b2.rad.x;
            Vec3 ba = b2.b - b2.a, pa = pt - b2.a;
            float l2 = Vec3::Dot(ba, ba); if (l2 < 1e-9f) return pa.Magnitude() - r1;
            float rr = r1 - r2, a2 = l2 - rr * rr, il2 = 1.0f / l2;
            float y = Vec3::Dot(pa, ba), z = y - l2;
            Vec3 xv = pa * l2 - ba * y; float x2 = Vec3::Dot(xv, xv);
            float y2 = y * y * l2, z2 = z * z * l2;
            float k = (rr < 0 ? -1.0f : 1.0f) * rr * rr * x2;
            if ((z < 0 ? -1.0f : 1.0f) * a2 * z2 > k) return std::sqrt(x2 + z2) * il2 - r2;
            if ((y < 0 ? -1.0f : 1.0f) * a2 * y2 < k) return std::sqrt(x2 + y2) * il2 - r1;
            return (std::sqrt(x2 * a2 * il2) + y * rr) * il2 - r1;
        }
        Vec3 e{(pt.x - b2.a.x) / b2.rad.x, (pt.y - b2.a.y) / b2.rad.y, (pt.z - b2.a.z) / b2.rad.z};
        float mn = std::min(b2.rad.x, std::min(b2.rad.y, b2.rad.z));
        return (e.Magnitude() - 1.0f) * mn;
    };
    const float kk = 0.10f;   // smooth-union radius: larger = masses fuse more
    auto field = [&](const Vec3& pt) -> float {
        float dr = 1e9f;
        for (const Blob& b2 : bl) {
            float dd = one(b2, pt);
            float h = 0.5f + 0.5f * (dr - dd) / kk; h = h < 0 ? 0 : (h > 1 ? 1 : h);
            dr = (dr * (1 - h) + dd * h) - kk * h * (1 - h);   // smooth-min: blend toward the nearer surface
        }
        return dr;
    };
    auto colorAt = [&](const Vec3& pt) -> Color {
        float best = 1e9f; Color cc = skin;
        for (const Blob& b2 : bl) { float dd = one(b2, pt); if (dd < best) { best = dd; cc = b2.col; } }
        return cc;
    };
    Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
    auto expand = [&](const Vec3& q, float r) {
        lo = {std::min(lo.x, q.x - r), std::min(lo.y, q.y - r), std::min(lo.z, q.z - r)};
        hi = {std::max(hi.x, q.x + r), std::max(hi.y, q.y + r), std::max(hi.z, q.z + r)};
    };
    for (const Blob& b2 : bl) {
        float r = b2.kind == 2 ? std::max(b2.rad.x, std::max(b2.rad.y, b2.rad.z)) : b2.r;
        expand(b2.a, r);
        if (b2.kind == 1) expand(b2.b, r);
        if (b2.kind == 3) expand(b2.b, b2.rad.x);   // tapered capsule: radius at b
    }
    Vec3 m2{0.25f, 0.25f, 0.25f}; lo = lo - m2; hi = hi + m2;
    Mesh body = SurfaceNets(field, colorAt, lo, hi, res);
    // One gentle Taubin pass (relax, then anti-shrink) to take the voxel facets
    // off the silhouette without inflating the body into a balloon.
    body.Smooth(0.32f); body.Smooth(-0.34f);
    for (const HumanoidPart& pt : BuildHumanoidParts(p, c)) if (pt.name == "Head") body.Append(pt.mesh);
    // Fingers: too thin to resolve in the SDF grid, so each digit is appended at
    // full detail as a slim capsule oriented along the hand — four separated
    // fingers (with a knuckle bend) + an opposed thumb, so the hand reads as a
    // real hand instead of a featureless stump.
    {
        Color skinC = skin;
        float hs = 0.098f * B * p.handSize;
        // Append a capsule from `a` along unit `dir` for `len`, radius `rad`,
        // oriented by rotation `rot` (its local Y axis runs along the digit).
        auto digit = [&](const Vec3& a, const Quat& rot, const Vec3& dir, float len, float rad) {
            Mesh cap = Mesh::Capsule(0.5f, 1.0f, 6, 2);
            Vec3 center = a + dir * (len * 0.5f);
            std::size_t base = body.vertices.size();
            for (Vec3 v : cap.vertices) {
                v = Vec3{v.x * rad, v.y * len, v.z * rad};
                body.vertices.push_back(rot * v + center);
            }
            for (int t : cap.triangles) body.triangles.push_back((int)base + t);
            for (int f = 0, n = cap.TriangleCount(); f < n; ++f) body.triColors.push_back(skinC);
        };
        for (const HandRec& hd : hands) {
            Vec3 dn = hd.fq * Vec3{0, -1, 0};   // along the fingers
            Vec3 sd = hd.fq * Vec3{1, 0, 0};    // across the knuckles
            // Curl the fingers slightly forward (toward the palm front) for a
            // natural relaxed hand rather than rigid spikes.
            Quat curl = hd.fq * Quat::Euler({22.0f, 0.0f, 0.0f});
            Vec3 cdn = curl * Vec3{0, -1, 0};
            Vec3 knuckle = hd.wrist + dn * (hs * 1.7f);
            for (int f = 0; f < 4; ++f) {       // four fingers, middle longest
                float off = (f - 1.5f) * hs * 0.42f;
                float len = hs * (1.45f - 0.14f * std::fabs(f - 1.5f));
                Vec3 root = knuckle + sd * off;
                digit(root, hd.fq, dn, len * 0.55f, hs * 0.34f);             // proximal (straight)
                Vec3 mid = root + dn * (len * 0.55f);
                digit(mid, curl, cdn, len * 0.52f, hs * 0.29f);             // distal (curled)
            }
            // Thumb: opposed, lower on the hand, angled across toward the body.
            Vec3 tdir = (dn - sd * ((float)hd.s * 1.1f)).Normalized();
            Quat trot = hd.fq * Quat::AngleAxis((float)hd.s * 55.0f, Vec3{0, 0, 1});
            Vec3 troot = hd.wrist + dn * (hs * 0.7f) - sd * ((float)hd.s * hs * 0.5f);
            digit(troot, trot, tdir, hs * 0.95f, hs * 0.40f);
        }
    }
    body.name = "Human";
    return body;
}

// ---- Low-poly humanoid: a hand-authored-style polygonal mesh built from
// cross-section loops (a torso loft + tapered limb tubes + a simple head),
// FLAT-shaded so it reads like a clean low-poly game character (the look of a
// modelled mesh, not a smooth blob). Double-sided, so winding is forgiving. ----
inline Mesh BuildLowPolyHumanoid(const HumanoidParams& p, const HumanoidColors* c) {
    Mesh m; m.name = "Human";
    Color skin  = c ? c->skin  : Color::White;
    Color shirt = c ? c->shirt : Color::White;
    Color pants = c ? c->pants : Color::White;
    Color shoes = c ? c->shoes : Color::White;
    Color hair  = c ? c->hair  : Color::FromBytes(60, 40, 30);
    const float H = p.height, B = p.build, bd = p.bodyDepth;
    const float up = 0.78f * H * (p.torsoLength - 1.0f);
    const float sW = p.shoulderWidth, hW = p.hipWidth, kPi = 3.14159265f;

    auto ring = [&](Vec3 ctr, Vec3 ax, Vec3 az, float rx, float rz, int K) {
        int base = (int)m.vertices.size();
        for (int k = 0; k < K; ++k) {
            float a = 2.0f * kPi * k / K;
            m.vertices.push_back(ctr + ax * (rx * std::cos(a)) + az * (rz * std::sin(a)));
        }
        return base;
    };
    auto bridge = [&](int b0, int b1, int K, Color col) {
        for (int k = 0; k < K; ++k) {
            int n = (k + 1) % K;
            m.triangles.insert(m.triangles.end(), {b0 + k, b1 + k, b0 + n, b0 + n, b1 + k, b1 + n});
            m.triColors.push_back(col); m.triColors.push_back(col);
        }
    };
    auto capFan = [&](int b, int K, Vec3 apex, Color col, bool up) {
        int ci = (int)m.vertices.size(); m.vertices.push_back(apex);
        for (int k = 0; k < K; ++k) {
            int n = (k + 1) % K;
            if (up) m.triangles.insert(m.triangles.end(), {b + k, b + n, ci});
            else    m.triangles.insert(m.triangles.end(), {b + n, b + k, ci});
            m.triColors.push_back(col);
        }
    };
    // A straight tapered tube from a->b (K-gon), flat low-poly limb segment.
    auto seg = [&](Vec3 a, Vec3 b, float rA, float rB, int K, Color col) {
        Vec3 d = (b - a).Normalized();
        Vec3 ax = (std::fabs(d.y) < 0.9f) ? Vec3::Cross(d, {0, 1, 0}).Normalized()
                                          : Vec3::Cross(d, {1, 0, 0}).Normalized();
        Vec3 az = Vec3::Cross(d, ax).Normalized();
        int b0 = ring(a, ax, az, rA, rA, K), b1 = ring(b, ax, az, rB, rB, K);
        bridge(b0, b1, K, col);
        return std::make_pair(b0, b1);
    };

    // ---- Torso: a vertical loft of elliptical rings (V-taper). ----
    const Vec3 AX{1, 0, 0}, AZ{0, 0, 1};
    struct R { float y, rx, rz; Color col; };
    R rings[] = {
        {0.56f * H,        0.150f * hW * B, 0.120f * bd * B, pants},  // pelvis bottom
        {0.70f * H,        0.150f * hW * B, 0.125f * bd * B, pants},  // hips
        {0.84f * H,        0.120f * B * p.waist, 0.105f * bd * B, shirt}, // waist (narrow)
        {1.00f * H + up*0.3f, 0.150f * sW * B, 0.120f * bd * B, shirt}, // abdomen
        {1.16f * H + up*0.6f, 0.188f * sW * B, 0.140f * bd * B, shirt}, // chest
        {1.33f * H + up,   0.205f * sW * B, 0.140f * bd * B, shirt},  // upper chest
        {1.43f * H + up,   0.150f * sW * B, 0.120f * bd * B, shirt},  // shoulders -> neck
        {1.52f * H + up,   0.066f, 0.066f, skin},                    // neck
    };
    const int KT = 14, NR = (int)(sizeof(rings) / sizeof(rings[0]));
    int rb[16];
    for (int i = 0; i < NR; ++i) rb[i] = ring({0, rings[i].y, 0}, AX, AZ, rings[i].rx, rings[i].rz, KT);
    for (int i = 0; i + 1 < NR; ++i) bridge(rb[i], rb[i + 1], KT, rings[i + 1].col);
    capFan(rb[0], KT, {0, rings[0].y - 0.03f * H, 0}, pants, false);

    // ---- Head: a low-poly skull (flattened icosphere) + hair cap. ----
    float headY = 1.66f * H + up, hd = p.headSize;
    m.Add(Mesh::Icosphere(0.5f, 2), {0.0f, headY, 0.02f * hd}, {0.30f * hd, 0.40f * hd, 0.34f * hd}, &skin);
    m.Add(Mesh::Icosphere(0.5f, 1), {0.0f, headY - 0.16f * hd, 0.05f * hd}, {0.22f * hd, 0.22f * hd, 0.26f * hd}, &skin); // jaw/chin
    if (!c || c->hasHair)
        m.Add(Mesh::Icosphere(0.5f, 1), {0.0f, headY + 0.12f * hd, -0.02f * hd}, {0.33f * hd, 0.30f * hd, 0.36f * hd}, &hair);
    if (!c || c->hasFace) {                                  // simple face
        Color eyeC = c ? c->eye : Color::FromBytes(30, 30, 40);
        for (int s = -1; s <= 1; s += 2)
            m.Add(Mesh::Icosphere(0.5f, 0), {s * 0.10f * hd, headY + 0.02f * hd, 0.30f * hd}, {0.05f * hd, 0.06f * hd, 0.04f * hd}, &eyeC);
        m.Add(Mesh::Cube(1.0f), {0.0f, headY - 0.04f * hd, 0.32f * hd}, {0.05f * hd, 0.08f * hd, 0.06f * hd}, &skin); // nose
    }

    // ---- Arms: A-pose tapered tubes (upper arm + forearm) + a block hand. ----
    const float aL = p.armLength, lL = p.legLength;
    for (int s = -1; s <= 1; s += 2) {
        float aw = 0.20f * sW * B, shoulderY = 1.40f * H + up;
        float at = 0.062f * B * p.armThickness, armLen = 0.82f * aL * H;
        Vec3 sh{s * aw, shoulderY, 0};
        Quat q = Quat::Euler({(float)s * p.armSwing, 0, (float)s * p.armSpread});
        Vec3 elbow = sh + q * Vec3{0, -armLen * 0.5f, 0};
        Quat fq = q * Quat::Euler({14.0f, 0, (float)s * -6.0f});
        Vec3 dn = fq * Vec3{0, -1, 0};
        Vec3 wrist = elbow + dn * (armLen * 0.5f);
        m.Add(Mesh::Icosphere(0.5f, 1), {sh.x, sh.y - 0.01f * H, 0}, {at * 2.0f, at * 1.9f, at * 2.0f}, &shirt); // shoulder
        seg(sh, elbow, at * 1.05f, at * 0.85f, 8, shirt);
        seg(elbow, wrist, at * 0.85f, at * 0.62f, 8, skin);
        Vec3 hc = wrist + dn * (at * 1.6f);
        m.Add(Mesh::Icosphere(0.5f, 1), hc, {at * 1.3f, at * 1.9f, at * 0.9f}, &skin); // hand block
    }

    // ---- Legs: tapered tubes (thigh + calf) + a block foot. ----
    for (int s = -1; s <= 1; s += 2) {
        float lw = 0.105f * hW * B, lt = 0.095f * B * p.legThickness;
        float legLen = 0.60f * H + 0.52f * lL * H;
        Vec3 hip{s * lw, 0.60f * H, 0};
        Quat ql = Quat::Euler({(float)s * p.legSwing, 0, (float)s * (p.legSpread * 0.4f)});
        Vec3 knee = hip + ql * Vec3{0, -legLen * 0.5f, 0};
        Vec3 ankle = hip + ql * Vec3{0, -legLen, 0};
        m.Add(Mesh::Icosphere(0.5f, 1), {hip.x, hip.y, 0}, {lt * 1.7f, lt * 1.7f, lt * 1.7f}, &pants); // hip
        seg(hip, knee, lt * 1.15f, lt * 0.82f, 8, pants);
        seg(knee, ankle, lt * 0.86f, lt * 0.55f, 8, pants);
        float fy = ankle.y - 0.04f * H, fsz = p.footSize;
        m.Add(Mesh::Cube(1.0f), {ankle.x, fy, 0.10f * fsz}, {0.085f * B * fsz, 0.07f * fsz, 0.26f * fsz}, &shoes); // foot
    }
    return m;   // flat-shaded (no smooth normals) -> low-poly facets
}

} // namespace okay
