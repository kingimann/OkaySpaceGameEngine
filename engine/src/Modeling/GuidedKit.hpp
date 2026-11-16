#pragma once
// Small helpers shared by the guided template files (internal, not part of the public engine headers): axis-aligned boxes,
// oriented limbs and cones, ellipsoids, a seeded random source, the part collector and the template builder.
#include "okay/Modeling/Guided.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace okay::modeling::guided::kit {

constexpr float kPi = 3.14159265f;
inline float Pf(const Recipe& r, const char* id) { return (float)r.Param(id); }

inline Frame FrameAA(Vec3 lo, Vec3 hi) {
    Frame f;
    f.origin = {(lo.x + hi.x) * 0.5f, lo.y, (lo.z + hi.z) * 0.5f};
    f.t = {1, 0, 0}; f.u = {0, 1, 0}; f.n = {0, 0, 1};
    f.sx = std::max(hi.x - lo.x, 1e-3f); f.sy = std::max(hi.y - lo.y, 1e-3f); f.sz = std::max(hi.z - lo.z, 1e-3f);
    return f;
}
inline PolyMesh BoxAA(Vec3 lo, Vec3 hi) { return shapes::Box(FrameAA(lo, hi)); }
inline PolyMesh Ell(Vec3 c, float rx, float ry, float rz, int rings = 6, int sectors = 10) {
    return shapes::Ball(FrameAA({c.x - rx, c.y - ry, c.z - rz}, {c.x + rx, c.y + ry, c.z + rz}), rings, sectors);
}
inline Frame AxisFrame(Vec3 a, Vec3 b, float w, float h) {   // box/prism frame from a to b (u along the axis)
    const Vec3 d = b - a;
    const float len = d.Magnitude();
    Frame f;
    f.u = len > 1e-5f ? d / len : Vec3{0, 1, 0};
    const Vec3 ref = std::fabs(f.u.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    f.t = Vec3::Cross(ref, f.u).Normalized();
    f.n = Vec3::Cross(f.t, f.u);
    f.origin = a;
    f.sx = w; f.sz = h; f.sy = std::max(len, 1e-3f);
    return f;
}
inline PolyMesh Limb(Vec3 a, Vec3 b, float r, bool round = true, int sides = 8) {
    if ((b - a).Magnitude() < 1e-4f) return PolyMesh();
    const Frame f = AxisFrame(a, b, r * 2, r * 2);
    return round ? shapes::Prism(f, sides) : shapes::Box(f);
}
inline PolyMesh Cone(Vec3 base, Vec3 tip, float r, int sides = 8) {
    if ((tip - base).Magnitude() < 1e-4f) return PolyMesh();
    return shapes::Pyramid(AxisFrame(base, tip, r * 2, r * 2), sides);
}
inline PolyMesh FillUser(const Recipe& r, const std::string& design, const Frame& f, const std::function<PolyMesh()>& builtin) {
    if (design.rfind("user:", 0) == 0) {
        auto it = r.userParts.find(std::string(design, 5));
        if (it != r.userParts.end()) return FitUserPart(it->second, f);
        return shapes::Box(f);
    }
    return builtin();
}
/// Collects parts: a part whose design is "none" (or whose slot the template lacks) is skipped; a user part is fitted to `frame`.
struct Out {
    const Recipe& r;
    std::vector<GeneratedPart>& parts;
    void Add(const std::string& id, const std::string& slot, const Frame& frame, const std::function<PolyMesh(const std::string&)>& build) {
        const std::string d = r.DesignFor(slot, id);
        if (d == "none" || d.empty()) return;
        PolyMesh m = FillUser(r, d, frame, [&] { return build(d); });
        if (!m.faces.empty()) parts.push_back({id, slot, std::move(m)});
    }
};
/// One ring of a lofted tube: its centre and two radii (along the ring's A and B axes).
struct Station { Vec3 c; float ra, rb; };
/// A closed tube through `st` (at least two rings): each ring is an ellipse of `sides` points; `refA` (for example forward) fixes the
/// A axis of every ring. The ends are rounded (a pole point just beyond the ring) or flat.
inline PolyMesh Loft(const std::vector<Station>& st, Vec3 refA, int sides, bool roundStart, bool roundEnd) {
    PolyMesh m;
    const int n = (int)st.size();
    if (n < 2 || sides < 3) return m;
    std::vector<Vec3> T(n), A(n), Bv(n);
    for (int i = 0; i < n; ++i) {
        const Vec3 d = (st[std::min(i + 1, n - 1)].c - st[std::max(i - 1, 0)].c);
        T[i] = d.Magnitude() > 1e-6f ? d.Normalized() : Vec3{0, 1, 0};
        Vec3 a = refA - T[i] * Vec3::Dot(refA, T[i]);
        if (a.Magnitude() < 1e-4f) { const Vec3 alt = std::fabs(T[i].x) < 0.9f ? Vec3{1, 0, 0} : Vec3{0, 0, 1}; a = alt - T[i] * Vec3::Dot(alt, T[i]); }
        A[i] = a.Normalized();
        Bv[i] = Vec3::Cross(T[i], A[i]);
    }
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < sides; ++k) {
            const float a = 2 * kPi * (float)k / (float)sides;
            m.verts.push_back(st[i].c + A[i] * (st[i].ra * std::cos(a)) + Bv[i] * (st[i].rb * std::sin(a)));
        }
    auto V = [&](int i, int k) { return i * sides + (k % sides); };
    for (int i = 0; i + 1 < n; ++i)
        for (int k = 0; k < sides; ++k) { PolyFace f; f.verts = {V(i, k), V(i, k + 1), V(i + 1, k + 1), V(i + 1, k)}; m.faces.push_back(f); }
    auto cap = [&](int i, bool round, bool start) {
        if (round) {
            const int pole = (int)m.verts.size();
            m.verts.push_back(st[i].c + T[i] * ((start ? -1.0f : 1.0f) * std::max(st[i].ra, st[i].rb) * 0.7f));
            for (int k = 0; k < sides; ++k) { PolyFace f; f.verts = start ? std::vector<int>{V(i, k + 1), V(i, k), pole} : std::vector<int>{V(i, k), V(i, k + 1), pole}; m.faces.push_back(f); }
        } else {
            PolyFace f;
            for (int k = 0; k < sides; ++k) f.verts.push_back(start ? V(i, sides - 1 - k) : V(i, k));
            m.faces.push_back(f);
        }
    };
    cap(0, roundStart, true);
    cap(n - 1, roundEnd, false);
    return m;
}

/// A deterministic random source (same seed, same numbers, on every machine).
struct Rnd {
    uint32_t s;
    explicit Rnd(uint32_t seed) : s(seed * 2654435761u + 12345u) {}
    float F() { s = s * 1664525u + 1013904223u; return (float)((s >> 8) & 0xffff) / 65535.0f; }
    float R(float lo, float hi) { return lo + (hi - lo) * F(); }
};

struct B {
    Template t;
    using K = ParamDef::Kind;
    void P(const char* id, const char* label, double def, double mn, double mx, const char* group, const char* tip, K k = K::Float) {
        ParamDef p; p.id = id; p.label = label; p.kind = k; p.def = def; p.min = mn; p.max = mx; p.group = group; p.tip = tip;
        t.params.push_back(p);
    }
    void S(const char* id, const char* name, std::vector<DesignDef> d, Color c, int mat) {
        SlotDef s; s.id = id; s.name = name; s.designs = std::move(d); s.defaultColor = c; s.defaultMaterial = mat;
        t.slots.push_back(s);
    }
    void St(const char* id, const char* name, const char* desc, std::map<std::string, double> p, std::map<std::string, std::string> sd, std::map<std::string, Color> c) {
        StartDesign s; s.id = id; s.name = name; s.description = desc; s.params = std::move(p); s.slotDesign = std::move(sd); s.colors = std::move(c);
        t.starts.push_back(s);
    }
};

} // namespace okay::modeling::guided::kit
