// Mesh shape generators (primitives, props, extrusions).
// Bodies of okay::Mesh members moved out of engine/include/okay/Render/Mesh.hpp (modularization plan M1); the
// declarations (with their default arguments) stay in the struct. The code is unchanged.
#include "okay/Render/Mesh.hpp"

namespace okay {

    Mesh Mesh::Quad(float size) {
        float h = size * 0.5f;
        Mesh m;
        m.name = "Quad";
        m.vertices = {{-h, -h, 0}, {h, -h, 0}, {h, h, 0}, {-h, h, 0}};
        m.triangles = {0, 1, 2, 0, 2, 3};
        return m;
    }

    Mesh Mesh::Cube(float size) {
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

    Mesh Mesh::Wedge(float size) {
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

    Mesh Mesh::Pyramid(float size) {
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

    Mesh Mesh::Grid(float width, float depth, int cols, int rows) {
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

    Mesh Mesh::Plane(float size) {
        float h = size * 0.5f;
        Mesh m;
        m.name = "Plane";
        m.vertices = {{-h, 0, -h}, {h, 0, -h}, {h, 0, h}, {-h, 0, h}};
        m.triangles = {0, 2, 1, 0, 3, 2};
        return m;
    }

    Mesh Mesh::Sphere(float radius, int rings, int sectors) {
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
        m.FlipWinding();   // the rows above wind inward: turn every face to point outward
        m.ComputeSmoothNormals();   // round the surface (no facets)
        return m;
    }

    Mesh Mesh::RoundedBox(float size, float roundness, int rings, int sectors) {
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

    Mesh Mesh::Cylinder(float radius, float height, int sectors) {
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
        m.FlipWinding();   // the rows above wind inward: turn every face to point outward
        return m;
    }

    Mesh Mesh::Cone(float radius, float height, int sectors) {
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
        m.FlipWinding();   // the rows above wind inward: turn every face to point outward
        return m;
    }

    Mesh Mesh::Torus(float radius, float tube, int rings, int sides) {
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
        m.FlipWinding();   // the rows above wind inward: turn every face to point outward
        m.ComputeSmoothNormals();   // smooth torus surface
        return m;
    }

    Mesh Mesh::TorusKnot(int p, int q, float scale, float tube, int rings, int sides) {
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
        m.FlipWinding();   // the rows above wind inward: turn every face to point outward
        m.ComputeSmoothNormals();
        return m;
    }

    Mesh Mesh::Tube(float outer, float inner, float height, int sectors) {
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
        m.FlipWinding();   // the rows above wind inward: turn every face to point outward
        return m;
    }

    void Mesh::AppendBox(Mesh& m, const Vec3& mn, const Vec3& mx) {
        int b = (int)m.vertices.size();
        m.vertices.push_back({mn.x, mn.y, mn.z}); m.vertices.push_back({mx.x, mn.y, mn.z});
        m.vertices.push_back({mx.x, mx.y, mn.z}); m.vertices.push_back({mn.x, mx.y, mn.z});
        m.vertices.push_back({mn.x, mn.y, mx.z}); m.vertices.push_back({mx.x, mn.y, mx.z});
        m.vertices.push_back({mx.x, mx.y, mx.z}); m.vertices.push_back({mn.x, mx.y, mx.z});
        const int f[36] = {0,2,1, 0,3,2,  4,5,6, 4,6,7,  0,1,5, 0,5,4,
                           3,7,6, 3,6,2,  1,2,6, 1,6,5,  0,4,7, 0,7,3};
        for (int i = 0; i < 36; ++i) m.triangles.push_back(b + f[i]);
    }

    Mesh Mesh::Hemisphere(float radius, int rings, int sectors) {
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

    Mesh Mesh::Stairs(int steps, float width, float totalHeight, float totalDepth) {
        Mesh m; m.name = "Stairs";
        if (steps < 1) steps = 1;
        float sh = totalHeight / steps, sd = totalDepth / steps, hw = width * 0.5f;
        for (int i = 0; i < steps; ++i)
            AppendBox(m, {-hw, 0.0f, i * sd}, {hw, (i + 1) * sh, totalDepth});
        return m;
    }

    Mesh Mesh::Gear(int teeth, float radius, float toothDepth, float thickness) {
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

    Mesh Mesh::Prism(int sides, float radius, float height) {
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

    Mesh Mesh::Disc(float radius, int sectors) {
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

    Mesh Mesh::Octahedron(float radius) {
        Mesh m; m.name = "Octahedron";
        float r = radius;
        m.vertices = {{r,0,0}, {-r,0,0}, {0,r,0}, {0,-r,0}, {0,0,r}, {0,0,-r}};  // px nx py ny pz nz
        m.triangles = {4,0,2, 4,2,1, 4,1,3, 4,3,0,     // top fan around +Z
                       5,2,0, 5,1,2, 5,3,1, 5,0,3};    // bottom fan around -Z
        return m;
    }

    Mesh Mesh::Tetrahedron(float radius) {
        Mesh m; m.name = "Tetrahedron";
        float a = radius;
        m.vertices = {{a, a, a}, {a, -a, -a}, {-a, a, -a}, {-a, -a, a}};
        m.triangles = {0, 1, 2, 0, 3, 1, 0, 2, 3, 1, 3, 2};   // outward-facing
        return m;
    }

    Mesh Mesh::Bipyramid(int sides, float radius, float height) {
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

    Mesh Mesh::Icosphere(float radius, int subdivisions) {
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

    float Mesh::PropRand(int variant, int key, float lo, float hi) {
        if (variant == 0) return (lo + hi) * 0.5f;
        unsigned x = (unsigned)variant * 374761393u + (unsigned)key * 668265263u;   // unsigned: no signed overflow, same bits
        x = (x ^ (x >> 13)) * 1274126177u;
        float t = (float)((x ^ (x >> 16)) & 0xffffu) / 65535.0f;
        return lo + (hi - lo) * t;
    }

    Mesh Mesh::Tree(int variant) {
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

    Mesh Mesh::Pine(int variant) {
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

    Mesh Mesh::Rock(int variant) {
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

    Mesh Mesh::Bush(int variant) {
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

    Mesh Mesh::PalmTree(int variant) {
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

    Mesh Mesh::DeadTree(int variant) {
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

    Mesh Mesh::Mushroom(int variant) {
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

    Mesh Mesh::Cactus(int variant) {
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

    Mesh Mesh::Barrel() {
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

    Mesh Mesh::Crate() {
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

    Mesh Mesh::Fence() {
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

    Mesh Mesh::Well() {
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

    Mesh Mesh::StreetLamp() {
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

    Mesh Mesh::Crystal(int variant) {
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

    Mesh Mesh::House() {
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

    Mesh Mesh::Tower() {
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

    Mesh Mesh::Windmill() {
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

    Mesh Mesh::Bridge() {
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

    Mesh Mesh::RockArch() {
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

    Mesh Mesh::Campfire() {
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

    Mesh Mesh::Lantern() {
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

    Mesh Mesh::Chest() {
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

    Mesh Mesh::Signpost() {
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

    Mesh Mesh::Cart() {
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

    Mesh Mesh::Tent() {
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

    Mesh Mesh::Bench() {
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

    bool Mesh::NameHasVariants(const std::string& n) {
        return n == "Tree" || n == "Pine" || n == "Rock" || n == "Bush"
            || n == "PalmTree" || n == "DeadTree" || n == "Mushroom"
            || n == "Cactus" || n == "Crystal";
    }

    Mesh Mesh::FromNameSeeded(const std::string& n, int variant) {
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

    Mesh Mesh::FromName(const std::string& n) {
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

    Mesh Mesh::Extrude(const std::vector<Vec2>& outline, float depth) {
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

    Mesh Mesh::Lathe(const std::vector<Vec2>& profile, int segments) {
        Mesh m;
        const int rows = (int)profile.size();
        if (rows < 2 || segments < 3) return m;
        // Signed area of the profile closed along the axis: positive when it runs
        // up the outside (bottom to top), negative when it runs down.
        float area = 0.0f;
        for (int r = 0; r + 1 < rows; ++r)
            area += profile[r].x * profile[r + 1].y - profile[r + 1].x * profile[r].y;
        area += profile[rows - 1].x * profile[rows - 1].y - profile[0].x * profile[0].y;   // the axis edges
        const bool up = area >= 0.0f;
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
                if (up) m.triangles.insert(m.triangles.end(), {a, b, c, b, d, c});
                else    m.triangles.insert(m.triangles.end(), {a, c, b, b, c, d});
            }
        }
        return m;
    }

    Mesh Mesh::Capsule(float radius, float height, int sectors, int rings) {
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
        m.FlipWinding();   // the rows above wind inward: turn every face to point outward
        m.ComputeSmoothNormals();   // smooth capsule surface
        return m;
    }

    Mesh Mesh::Screw(const std::vector<Vec2>& profile, float turns, float pitch, int segments) {
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

    Mesh Mesh::SweepPath(const std::vector<Vec3>& path, float radius, int segments, bool caps, float radiusEnd) {
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

} // namespace okay
