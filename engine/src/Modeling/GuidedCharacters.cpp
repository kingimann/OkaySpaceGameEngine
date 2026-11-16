// Character templates for guided modeling: human, robot and creature. A character is a set of named parts (head, hair, face,
// torso, neck, arms, hands, legs, feet, clothing, accessories) that can each be given another design; proportion settings
// change the body. Written from general parametric modeling (boxes, ellipsoids, oriented limbs between joints); no code,
// models or assets from other projects. The character stands on y = 0, faces +z and is centred on x = 0. Parts are closed
// solids that never share a corner position, so the rebuild rules in Guided.hpp can tell them apart.
#include "okay/Modeling/Guided.hpp"
#include <algorithm>
#include <cmath>

namespace okay::modeling::guided {
void GenerateHumanOrganic(const Recipe& r, std::vector<GeneratedPart>& parts, const std::function<void(const Recipe&, std::vector<GeneratedPart>&)>& boxy);   // GuidedHuman.cpp
namespace {

constexpr float kPi = 3.14159265f;
float Pf(const Recipe& r, const char* id) { return (float)r.Param(id); }

PolyMesh BoxAA(Vec3 lo, Vec3 hi) {
    Frame f;
    f.origin = {(lo.x + hi.x) * 0.5f, lo.y, (lo.z + hi.z) * 0.5f};
    f.t = {1, 0, 0}; f.u = {0, 1, 0}; f.n = {0, 0, 1};
    f.sx = std::max(hi.x - lo.x, 1e-3f); f.sy = std::max(hi.y - lo.y, 1e-3f); f.sz = std::max(hi.z - lo.z, 1e-3f);
    return shapes::Box(f);
}
Frame FrameAA(Vec3 lo, Vec3 hi) {
    Frame f;
    f.origin = {(lo.x + hi.x) * 0.5f, lo.y, (lo.z + hi.z) * 0.5f};
    f.t = {1, 0, 0}; f.u = {0, 1, 0}; f.n = {0, 0, 1};
    f.sx = std::max(hi.x - lo.x, 1e-3f); f.sy = std::max(hi.y - lo.y, 1e-3f); f.sz = std::max(hi.z - lo.z, 1e-3f);
    return f;
}
// An ellipsoid with the given centre and radii.
PolyMesh Ell(Vec3 c, float rx, float ry, float rz, int rings = 6, int sectors = 10) {
    return shapes::Ball(FrameAA({c.x - rx, c.y - ry, c.z - rz}, {c.x + rx, c.y + ry, c.z + rz}), rings, sectors);
}
// A limb from a to b: a prism of radius r, or a box of width 2r.
PolyMesh Limb(Vec3 a, Vec3 b, float r, bool round) {
    const Vec3 d = b - a;
    const float len = d.Magnitude();
    if (len < 1e-4f) return PolyMesh();
    Frame f;
    f.u = d / len;
    const Vec3 ref = std::fabs(f.u.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    f.t = Vec3::Cross(ref, f.u).Normalized();
    f.n = Vec3::Cross(f.t, f.u);
    f.origin = a;
    f.sx = f.sz = r * 2; f.sy = len;
    return round ? shapes::Prism(f, 10) : shapes::Box(f);
}
PolyMesh Cone(Vec3 base, Vec3 tip, float r, int sides = 6) {
    const Vec3 d = tip - base;
    const float len = d.Magnitude();
    if (len < 1e-4f) return PolyMesh();
    Frame f;
    f.u = d / len;
    const Vec3 ref = std::fabs(f.u.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    f.t = Vec3::Cross(ref, f.u).Normalized();
    f.n = Vec3::Cross(f.t, f.u);
    f.origin = base;
    f.sx = f.sz = r * 2; f.sy = len;
    return shapes::Pyramid(f, sides);
}
PolyMesh FillUser(const Recipe& r, const std::string& design, const Frame& f, const std::function<PolyMesh()>& builtin) {
    if (design.rfind("user:", 0) == 0) {
        auto it = r.userParts.find(std::string(design, 5));
        if (it != r.userParts.end()) return FitUserPart(it->second, f);
        return shapes::Box(f);
    }
    return builtin();
}
struct Out {
    const Recipe& r;
    std::vector<GeneratedPart>& parts;
    void Add(const std::string& id, const std::string& slot, const Frame& frame, const std::function<PolyMesh(const std::string&)>& build) {
        const std::string d = r.DesignFor(slot, id);
        if (d == "none" || d.empty()) return;   // a slot this template does not have builds nothing
        PolyMesh m = FillUser(r, d, frame, [&] { return build(d); });
        parts.push_back({id, slot, std::move(m)});
    }
};

// ---- Biped (human and robot) ---------------------------------------------------------------------------------
void GenerateBiped(const Recipe& r, std::vector<GeneratedPart>& parts) {
    Out o{r, parts};
    const float H = Pf(r, "height"), sw = Pf(r, "shoulderWidth"), td = Pf(r, "torsoDepth"), hs = Pf(r, "headSize"), legF = Pf(r, "legLength");
    const float th = Pf(r, "limbThickness"), neck = Pf(r, "neckLength"), armF = Pf(r, "armLength");
    const float armA = Pf(r, "armAngle") * kPi / 180.0f, bend = Pf(r, "elbowBend") * kPi / 180.0f, stance = Pf(r, "stance") * kPi / 180.0f;
    const float hipY = H * legF;
    const float shoulderY = std::max(hipY + 0.25f, H - neck - hs);
    const float tw = sw * 0.9f, torsoLen = shoulderY - hipY;
    const float hb = shoulderY + neck;   // bottom of the head
    const float ankleY = H * 0.035f;

    // torso and pelvis
    Frame ft = FrameAA({-tw / 2, hipY, -td / 2}, {tw / 2, shoulderY, td / 2});
    o.Add("torso/0", "torso", ft, [&](const std::string& d) {
        if (d == "tapered") return shapes::ExtrudeAxes({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {{-tw * 0.34f, hipY}, {tw * 0.34f, hipY}, {tw / 2, shoulderY}, {-tw / 2, shoulderY}}, td);
        if (d == "barrel") { Frame f = ft; f.sx = tw; f.sz = td; return shapes::Prism(f, 12); }
        if (shapes::Detail() >= 2 && d != "power" && r.templateId == "robot") {   // panel bolts on the chest (High and up)
            PolyMesh m = d == "tapered" ? shapes::ExtrudeAxes({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {{-tw * 0.34f, hipY}, {tw * 0.34f, hipY}, {tw / 2, shoulderY}, {-tw / 2, shoulderY}}, td)
                : d == "barrel" ? [&] { Frame g = ft; g.sx = tw; g.sz = td; return shapes::Prism(g, 12); }() : BoxAA({-tw / 2, hipY, -td / 2}, {tw / 2, shoulderY, td / 2});
            const int nb = shapes::Detail() >= 3 ? 8 : 4;
            for (int k = 0; k < nb; ++k) {
                const float bx = (k % 2 ? 1.0f : -1.0f) * tw * 0.3f, by = hipY + torsoLen * (0.2f + 0.6f * (float)(k / 2) / std::max(1.0f, (float)(nb / 2 - 1)));
                Frame bf; bf.origin = {bx, by, td / 2 - 0.005f}; bf.t = {1, 0, 0}; bf.u = {0, 0, 1}; bf.n = {0, -1, 0}; bf.sx = 0.03f; bf.sy = 0.02f; bf.sz = 0.03f;
                shapes::Append(m, shapes::Prism(bf, 8));
            }
            return m;
        }
        if (d == "power") { PolyMesh m = BoxAA({-tw / 2, hipY, -td / 2}, {tw / 2, shoulderY, td / 2}); shapes::Append(m, BoxAA({-tw * 0.16f, hipY + torsoLen * 0.45f, td / 2 - 0.01f}, {tw * 0.16f, hipY + torsoLen * 0.75f, td / 2 + 0.04f})); return m; }
        return BoxAA({-tw / 2, hipY, -td / 2}, {tw / 2, shoulderY, td / 2});
    });
    Frame fp = FrameAA({-tw * 0.46f, hipY - 0.1f, -td * 0.46f}, {tw * 0.46f, hipY + 0.03f, td * 0.46f});
    o.Add("pelvis/0", "pelvis", fp, [&](const std::string&) { return BoxAA({-tw * 0.46f, hipY - 0.1f, -td * 0.46f}, {tw * 0.46f, hipY + 0.03f, td * 0.46f}); });
    // neck
    {
        Frame f = FrameAA({-hs * 0.2f, shoulderY - 0.02f, -hs * 0.2f}, {hs * 0.2f, hb + 0.03f, hs * 0.2f});
        o.Add("neck/0", "neck", f, [&](const std::string& d) {
            if (d == "piston") { PolyMesh m = BoxAA({-hs * 0.14f, shoulderY - 0.02f, -hs * 0.14f}, {hs * 0.14f, hb + 0.03f, hs * 0.14f}); for (int k = 0; k < 2; ++k) shapes::Append(m, BoxAA({-hs * 0.22f, shoulderY + 0.02f + neck * 0.4f * (float)k, -hs * 0.22f}, {hs * 0.22f, shoulderY + 0.04f + neck * 0.4f * (float)k, hs * 0.22f})); return m; }
            if (d == "thin") { Frame g = f; g.sx = g.sz = hs * 0.25f; return shapes::Prism(g, 8); }
            return shapes::Prism(f, 10);
        });
    }
    // head, face, hair
    const Vec3 hc{0, hb + hs * 0.5f, 0};
    Frame fh = FrameAA({-hs / 2, hb, -hs / 2}, {hs / 2, hb + hs, hs / 2});
    o.Add("head/0", "head", fh, [&](const std::string& d) {
        if (d == "box" || d == "cube") {
            PolyMesh m = BoxAA({-hs / 2, hb, -hs / 2}, {hs / 2, hb + hs, hs / 2});
            if (d == "cube") shapes::Append(m, BoxAA({-hs * 0.4f, hb + hs * 0.5f, hs / 2 - 0.005f}, {hs * 0.4f, hb + hs * 0.72f, hs / 2 + 0.025f}));   // visor strip
            return m;
        }
        if (d == "oval") return Ell(hc, hs * 0.42f, hs * 0.55f, hs * 0.45f);
        if (d == "pointed") { PolyMesh m = Ell({0, hb + hs * 0.4f, 0}, hs * 0.45f, hs * 0.4f, hs * 0.45f); shapes::Append(m, Cone({0, hb + hs * 0.72f, 0}, {0, hb + hs * 1.12f, 0}, hs * 0.3f, 6)); return m; }
        if (d == "cyclops") { PolyMesh m = BoxAA({-hs / 2, hb, -hs / 2}, {hs / 2, hb + hs, hs / 2}); Frame e; e.origin = {0, hb + hs * 0.5f, hs / 2 - 0.01f}; e.t = {1, 0, 0}; e.u = {0, 0, 1}; e.n = {0, -1, 0}; e.sx = hs * 0.34f; e.sy = 0.05f; e.sz = hs * 0.34f; shapes::Append(m, shapes::Prism(e, 12)); return m; }
        return Ell(hc, hs * 0.5f, hs * 0.5f, hs * 0.5f);   // round, dome
    });
    {
        const float ey = hb + hs * 0.56f, ex = hs * 0.2f, zf = hs * 0.5f;
        Frame f = FrameAA({-hs * 0.3f, ey - 0.03f, zf * 0.7f}, {hs * 0.3f, ey + 0.04f, zf + 0.03f});
        o.Add("face/0", "face", f, [&](const std::string& d) {
            PolyMesh m;
            if (d == "big") { for (int s = -1; s <= 1; s += 2) shapes::Append(m, BoxAA({s * ex - hs * 0.08f, ey - hs * 0.1f, zf * 0.78f}, {s * ex + hs * 0.08f, ey + hs * 0.1f, zf + 0.02f})); return m; }
            if (d == "slit") return BoxAA({-hs * 0.32f, ey - 0.02f, zf * 0.8f}, {hs * 0.32f, ey + 0.02f, zf + 0.025f});
            if (d == "grille") { for (int k = 0; k < 4; ++k) shapes::Append(m, BoxAA({-hs * 0.22f, ey - hs * 0.28f + 0.03f * (float)k, zf * 0.8f}, {hs * 0.22f, ey - hs * 0.28f + 0.03f * (float)k + 0.012f, zf + 0.02f})); return m; }
            for (int s = -1; s <= 1; s += 2) shapes::Append(m, BoxAA({s * ex - 0.02f, ey - 0.02f, zf * 0.8f}, {s * ex + 0.02f, ey + 0.02f, zf + 0.02f}));
            if (d == "simple") shapes::Append(m, BoxAA({-0.015f, ey - hs * 0.2f, zf * 0.85f}, {0.015f, ey - hs * 0.04f, zf + 0.035f}));   // nose
            return m;
        });
    }
    {
        const float top = hb + hs;
        Frame f = FrameAA({-hs * 0.55f, hb + hs * 0.5f, -hs * 0.6f}, {hs * 0.55f, top + hs * 0.35f, hs * 0.55f});
        o.Add("hair/0", "hair", f, [&](const std::string& d) {
            PolyMesh m;
            auto cap = [&] { return Ell({0, top - hs * 0.2f, -hs * 0.02f}, hs * 0.54f, hs * 0.31f, hs * 0.54f); };
            if (d == "mohawk") return BoxAA({-0.04f, top - hs * 0.1f, -hs * 0.45f}, {0.04f, top + hs * 0.38f, hs * 0.35f});
            m = cap();
            if (d == "long") shapes::Append(m, BoxAA({-hs * 0.5f, shoulderY - 0.1f, -hs * 0.6f}, {hs * 0.5f, top - hs * 0.3f, -hs * 0.33f}));
            if (d == "bun") shapes::Append(m, Ell({0, top + hs * 0.12f, -hs * 0.36f}, hs * 0.2f, hs * 0.2f, hs * 0.2f, 4, 8));
            if (d == "spiky") for (int k = 0; k < 5; ++k) { const float a = 2 * kPi * (float)k / 5.0f; shapes::Append(m, Cone({std::cos(a) * hs * 0.3f, top - hs * 0.12f, std::sin(a) * hs * 0.3f}, {std::cos(a) * hs * 0.38f, top + hs * 0.3f, std::sin(a) * hs * 0.38f}, hs * 0.1f, 4)); }
            return m;
        });
    }
    // arms and hands
    for (int side = 0; side < 2; ++side) {
        const float s = side ? 1.0f : -1.0f;
        const std::string sd = std::to_string(side);
        const float ua = H * armF * 0.46f, fa = H * armF * 0.40f, hl = H * armF * 0.14f;
        const Vec3 S{s * sw * 0.5f, shoulderY - 0.04f, 0};
        const Vec3 d1{s * std::sin(armA), -std::cos(armA), 0};
        const Vec3 E = S + d1 * ua;
        Vec3 d2{d1.x, d1.y * std::cos(bend), -d1.y * std::sin(bend)};
        d2 = d2.Normalized();
        const Vec3 W = E + d2 * fa;
        auto limbOf = [&](const std::string& id, const std::string& slot, Vec3 a, Vec3 b, float rad) {
            const Vec3 lo{std::min(a.x, b.x) - rad, std::min(a.y, b.y), std::min(a.z, b.z) - rad}, hi{std::max(a.x, b.x) + rad, std::max(a.y, b.y), std::max(a.z, b.z) + rad};
            o.Add(id, slot, FrameAA(lo, hi), [&](const std::string& d) {
                const float k = d == "thick" ? 1.3f : d == "piston" ? 0.7f : 1.0f;
                return Limb(a, b, rad * k, d != "boxy");
            });
        };
        limbOf("arm/" + sd + "/upper", "arm", S, E, th);
        limbOf("arm/" + sd + "/fore", "arm", E, W, th * 0.85f);
        const std::string hid = "hand/" + sd;
        Frame fhd = FrameAA({W.x - 0.05f, W.y - hl, W.z - 0.03f}, {W.x + 0.05f, W.y + 0.01f, W.z + 0.03f});
        o.Add(hid, "hand", fhd, [&](const std::string& d) {
            const float x = W.x, y = W.y, z = W.z;
            if (d == "fist") return Ell({x, y - hl * 0.4f, z}, hl * 0.45f, hl * 0.45f, hl * 0.4f, 4, 8);
            if (d == "mitten") { PolyMesh m = BoxAA({x - 0.04f, y - hl, z - 0.025f}, {x + 0.04f, y + 0.01f, z + 0.025f}); shapes::Append(m, BoxAA({x + s * 0.035f, y - hl * 0.6f, z + 0.02f}, {x + s * 0.075f, y - hl * 0.2f, z + 0.05f})); return m; }
            if (d == "claw") { PolyMesh m; for (int k = -1; k <= 1; ++k) shapes::Append(m, Cone({x + k * 0.03f, y - hl * 0.3f, z}, {x + k * 0.04f, y - hl * 1.1f, z}, 0.016f, 4)); shapes::Append(m, BoxAA({x - 0.045f, y - hl * 0.35f, z - 0.02f}, {x + 0.045f, y + 0.01f, z + 0.02f})); return m; }
            if (d == "pincer") { PolyMesh m = BoxAA({x - 0.03f, y - hl * 0.4f, z - 0.025f}, {x + 0.03f, y + 0.01f, z + 0.025f}); shapes::Append(m, BoxAA({x - 0.035f, y - hl, z - 0.02f}, {x - 0.012f, y - hl * 0.38f, z + 0.02f})); shapes::Append(m, BoxAA({x + 0.012f, y - hl, z - 0.02f}, {x + 0.035f, y - hl * 0.38f, z + 0.02f})); return m; }
            if (d == "fingers") {
                PolyMesh m = BoxAA({x - 0.04f, y - hl * 0.5f, z - 0.02f}, {x + 0.04f, y + 0.01f, z + 0.02f});
                for (int k = 0; k < 4; ++k) shapes::Append(m, BoxAA({x - 0.037f + 0.0205f * (float)k, y - hl, z - 0.012f}, {x - 0.02f + 0.0205f * (float)k, y - hl * 0.48f, z + 0.012f}));
                shapes::Append(m, BoxAA({x + s * 0.04f, y - hl * 0.7f, z + 0.005f}, {x + s * 0.07f, y - hl * 0.3f, z + 0.03f}));
                return m;
            }
            return BoxAA({x - 0.04f, y - hl, z - 0.025f}, {x + 0.04f, y + 0.01f, z + 0.025f});   // block
        });
        // shoulder pads (robots and costumes)
        Frame fsp = FrameAA({S.x - 0.09f, S.y - 0.05f, -0.09f}, {S.x + 0.09f, S.y + 0.12f, 0.09f});
        o.Add("shoulder/" + sd, "shoulder", fsp, [&](const std::string& d) {
            if (d == "spike") return Cone({S.x, S.y + 0.02f, 0}, {S.x + s * 0.04f, S.y + 0.22f, 0}, 0.07f, 6);
            if (d == "plate") return BoxAA({S.x - 0.09f, S.y + 0.01f, -0.09f}, {S.x + 0.09f, S.y + 0.07f, 0.09f});
            return Ell({S.x, S.y + 0.03f, 0}, 0.1f, 0.08f, 0.1f, 4, 8);
        });
    }
    // legs and feet
    for (int side = 0; side < 2; ++side) {
        const float s = side ? 1.0f : -1.0f;
        const std::string sd = std::to_string(side);
        const Vec3 Hp{s * sw * 0.22f, hipY - 0.04f, 0};
        const float L = std::max(0.3f, (Hp.y - ankleY) / std::cos(stance));
        const Vec3 d1{s * std::sin(stance), -std::cos(stance), 0};
        const Vec3 K = Hp + d1 * (L * 0.5f), A = Hp + d1 * L;
        auto limbOf = [&](const std::string& id, Vec3 a, Vec3 b, float rad) {
            const Vec3 lo{std::min(a.x, b.x) - rad, std::min(a.y, b.y), std::min(a.z, b.z) - rad}, hi{std::max(a.x, b.x) + rad, std::max(a.y, b.y), std::max(a.z, b.z) + rad};
            o.Add(id, "leg", FrameAA(lo, hi), [&](const std::string& d) { const float k = d == "thick" ? 1.3f : d == "piston" ? 0.7f : 1.0f; return Limb(a, b, rad * k, d != "boxy"); });
        };
        limbOf("leg/" + sd + "/upper", Hp, K, th * 1.25f);
        limbOf("leg/" + sd + "/lower", K, A, th * 0.95f);
        Frame ff = FrameAA({A.x - 0.06f, 0, A.z - 0.07f}, {A.x + 0.06f, A.y + 0.04f, A.z + 0.2f});
        o.Add("foot/" + sd, "foot", ff, [&](const std::string& d) {
            const float x = A.x, y = std::max(A.y, 0.03f);
            if (d == "boot") return BoxAA({x - 0.06f, 0, -0.06f}, {x + 0.06f, y + 0.2f, 0.2f});
            if (d == "round") return Ell({x, 0.06f, 0.06f}, 0.06f, 0.06f, 0.14f, 4, 8);
            if (d == "sneaker") { PolyMesh m = BoxAA({x - 0.055f, 0.03f, -0.06f}, {x + 0.055f, y + 0.04f, 0.19f}); shapes::Append(m, BoxAA({x - 0.065f, 0, -0.07f}, {x + 0.065f, 0.035f, 0.21f})); return m; }
            if (d == "tread") { PolyMesh m = BoxAA({x - 0.07f, 0.04f, -0.12f}, {x + 0.07f, y + 0.04f, 0.26f}); Frame w; w.origin = {x, 0.04f, -0.12f}; w.t = {0, 1, 0}; w.u = {0, 0, 1}; w.n = {1, 0, 0}; w.sx = 0.08f; w.sy = 0.38f; w.sz = 0.08f; shapes::Append(m, shapes::Prism(w, 8)); return m; }
            if (d == "hover") { Frame w; w.origin = {x, 0, 0}; w.t = {1, 0, 0}; w.u = {0, 1, 0}; w.n = {0, 0, 1}; w.sx = w.sz = 0.26f; w.sy = y + 0.06f; return shapes::Prism(w, 14); }
            return BoxAA({x - 0.055f, 0, -0.06f}, {x + 0.055f, y + 0.04f, 0.2f});   // block
        });
    }
    // clothing
    {
        Frame f = FrameAA({-tw / 2 - 0.02f, hipY, -td / 2 - 0.02f}, {tw / 2 + 0.02f, shoulderY + 0.01f, td / 2 + 0.02f});
        o.Add("shirt/0", "shirt", f, [&](const std::string& d) {
            PolyMesh m;
            const float bot = d == "jacket" ? hipY - 0.15f : hipY + 0.02f;
            const float top = d == "vest" || d == "plates" ? shoulderY - 0.02f : shoulderY + 0.01f;
            shapes::Append(m, BoxAA({-tw / 2 - 0.02f, bot, -td / 2 - 0.02f}, {tw / 2 + 0.02f, top, td / 2 + 0.02f}));
            if (d == "plates") shapes::Append(m, BoxAA({-tw * 0.3f, hipY + torsoLen * 0.3f, td / 2 + 0.01f}, {tw * 0.3f, hipY + torsoLen * 0.85f, td / 2 + 0.05f}));
            if (d == "tshirt" || d == "jacket") for (int side = 0; side < 2; ++side) {
                const float s = side ? 1.0f : -1.0f;
                const float ua = H * armF * 0.46f;
                const Vec3 S{s * sw * 0.5f, shoulderY - 0.04f, 0}, dd{s * std::sin(armA), -std::cos(armA), 0};
                shapes::Append(m, Limb(S - dd * 0.02f, S + dd * (d == "jacket" ? ua * 0.97f : ua * 0.45f), th + 0.025f, true));
            }
            return m;
        });
        Frame fpn = FrameAA({-tw / 2, hipY - 0.45f, -td / 2}, {tw / 2, hipY + 0.03f, td / 2});
        o.Add("pants/0", "pants", fpn, [&](const std::string& d) {
            PolyMesh m;
            if (d == "skirt") { Frame sk; sk.origin = {0, hipY - 0.42f, 0}; sk.t = {1, 0, 0}; sk.u = {0, 1, 0}; sk.n = {0, 0, 1}; sk.sx = tw * 1.3f; sk.sz = td * 1.7f; sk.sy = 0.47f; return shapes::Prism(sk, 14); }
            shapes::Append(m, BoxAA({-tw * 0.46f - 0.02f, hipY - 0.13f, -td * 0.46f - 0.02f}, {tw * 0.46f + 0.02f, hipY + 0.04f, td * 0.46f + 0.02f}));
            for (int side = 0; side < 2; ++side) {
                const float s = side ? 1.0f : -1.0f;
                const Vec3 Hp{s * sw * 0.22f, hipY - 0.04f, 0};
                const float L = std::max(0.3f, (Hp.y - ankleY) / std::cos(stance));
                const Vec3 d1{s * std::sin(stance), -std::cos(stance), 0};
                const float len = d == "shorts" ? L * 0.35f : L * 0.5f;
                shapes::Append(m, Limb(Hp - d1 * 0.03f, Hp + d1 * len, th * 1.25f + 0.025f, true));
                if (d == "trousers" || d == "greaves") shapes::Append(m, Limb(Hp + d1 * (L * 0.5f), Hp + d1 * (L * 0.96f), th * 0.95f + 0.025f, true));
            }
            return m;
        });
    }
    // accessories
    {
        const float top = hb + hs;
        Frame f = FrameAA({-hs * 0.6f, top - hs * 0.3f, -hs * 0.6f}, {hs * 0.6f, top + hs * 0.6f, hs * 0.75f});
        o.Add("hat/0", "hat", f, [&](const std::string& d) {
            if (d == "tophat") { PolyMesh m; Frame c; c.origin = {0, top + hs * 0.03f, 0}; c.t = {1, 0, 0}; c.u = {0, 1, 0}; c.n = {0, 0, 1}; c.sx = c.sz = hs * 0.78f; c.sy = hs * 0.6f; m = shapes::Prism(c, 12); Frame b = c; b.origin = {0, top - hs * 0.02f, 0}; b.sx = b.sz = hs * 1.25f; b.sy = hs * 0.08f; shapes::Append(m, shapes::Prism(b, 14)); return m; }
            if (d == "helmet") return Ell({0, top - hs * 0.12f, 0}, hs * 0.62f, hs * 0.5f, hs * 0.62f);
            if (d == "beanie") { PolyMesh m = Ell({0, top - hs * 0.1f, 0}, hs * 0.57f, hs * 0.34f, hs * 0.57f); shapes::Append(m, Ell({0, top + hs * 0.28f, 0}, hs * 0.1f, hs * 0.1f, hs * 0.1f, 4, 8)); return m; }
            PolyMesh m = Ell({0, top - hs * 0.1f, 0}, hs * 0.58f, hs * 0.36f, hs * 0.58f);   // cap
            shapes::Append(m, BoxAA({-hs * 0.45f, top - hs * 0.2f, hs * 0.3f}, {hs * 0.45f, top - hs * 0.15f, hs * 0.75f}));
            return m;
        });
        const float ey = hb + hs * 0.56f, zf = hs * 0.5f;
        Frame fg = FrameAA({-hs * 0.4f, ey - hs * 0.15f, zf}, {hs * 0.4f, ey + hs * 0.15f, zf + 0.05f});
        o.Add("glasses/0", "glasses", fg, [&](const std::string& d) {
            PolyMesh m;
            if (d == "visor") return BoxAA({-hs * 0.42f, ey - hs * 0.1f, zf + 0.0f}, {hs * 0.42f, ey + hs * 0.12f, zf + 0.045f});
            for (int s = -1; s <= 1; s += 2) {
                if (d == "square") shapes::Append(m, BoxAA({s * hs * 0.2f - hs * 0.13f, ey - hs * 0.1f, zf + 0.0f}, {s * hs * 0.2f + hs * 0.13f, ey + hs * 0.1f, zf + 0.03f}));
                else { Frame c; c.origin = {s * hs * 0.2f, ey, zf}; c.t = {1, 0, 0}; c.u = {0, 0, 1}; c.n = {0, -1, 0}; c.sx = hs * 0.27f; c.sz = hs * 0.27f; c.sy = 0.03f; shapes::Append(m, shapes::Prism(c, 12)); }
            }
            shapes::Append(m, BoxAA({-hs * 0.08f, ey - 0.008f, zf + 0.005f}, {hs * 0.08f, ey + 0.008f, zf + 0.022f}));
            return m;
        });
    }
    {
        Frame f = FrameAA({-tw * 0.4f, hipY + 0.05f, -td / 2 - 0.25f}, {tw * 0.4f, shoulderY, -td / 2 + 0.02f});
        o.Add("backpack/0", "backpack", f, [&](const std::string& d) {
            const float depth = d == "large" || d == "jet" ? 0.24f : 0.16f, hh = d == "large" ? torsoLen * 0.95f : torsoLen * 0.7f;
            PolyMesh m = BoxAA({-tw * 0.36f, shoulderY - hh, -td / 2 - depth}, {tw * 0.36f, shoulderY - 0.03f, -td / 2 + 0.015f});
            if (d == "jet") for (int s = -1; s <= 1; s += 2) { Frame c; c.origin = {s * tw * 0.2f, shoulderY - hh - 0.12f, -td / 2 - depth * 0.5f}; c.t = {1, 0, 0}; c.u = {0, 1, 0}; c.n = {0, 0, 1}; c.sx = c.sz = 0.1f; c.sy = 0.15f; shapes::Append(m, shapes::Prism(c, 8)); }
            for (int s = -1; s <= 1; s += 2) shapes::Append(m, BoxAA({s * tw * 0.3f - 0.025f, shoulderY - hh * 0.9f, -td / 2 - 0.02f}, {s * tw * 0.3f + 0.025f, shoulderY + 0.012f, td / 2 + 0.028f}));
            return m;
        });
        Frame fb = FrameAA({-tw / 2 - 0.012f, hipY + 0.02f, -td / 2 - 0.012f}, {tw / 2 + 0.012f, hipY + 0.07f, td / 2 + 0.012f});
        o.Add("belt/0", "belt", fb, [&](const std::string&) {
            PolyMesh m = BoxAA({-tw / 2 - 0.012f, hipY + 0.02f, -td / 2 - 0.012f}, {tw / 2 + 0.012f, hipY + 0.07f, td / 2 + 0.012f});
            shapes::Append(m, BoxAA({-0.035f, hipY + 0.015f, td / 2 + 0.005f}, {0.035f, hipY + 0.075f, td / 2 + 0.03f}));
            return m;
        });
        Frame fc = FrameAA({-tw / 2, hipY - 0.1f, -td / 2 - 0.1f}, {tw / 2, shoulderY + 0.02f, -td / 2 - 0.05f});
        o.Add("cape/0", "cape", fc, [&](const std::string& d) {
            const float bot = d == "long" ? std::max(0.2f, hipY * 0.45f) : hipY + 0.05f;
            return BoxAA({-tw * 0.55f, bot, -td / 2 - 0.1f}, {tw * 0.55f, shoulderY + 0.02f, -td / 2 - 0.065f});
        });
        Frame fa = FrameAA({-0.03f, hb + hs, -0.03f}, {0.03f, hb + hs + 0.3f, 0.03f});
        o.Add("antenna/0", "antenna", fa, [&](const std::string& d) {
            const float top = hb + hs;
            if (d == "dish") { PolyMesh m = BoxAA({-0.015f, top - 0.02f, -0.015f}, {0.015f, top + 0.12f, 0.015f}); Frame c; c.origin = {0, top + 0.12f, 0}; c.t = {1, 0, 0}; c.u = {0, 1, 0}; c.n = {0, 0, 1}; c.sx = c.sz = 0.16f; c.sy = 0.03f; shapes::Append(m, shapes::Prism(c, 12)); return m; }
            if (d == "twin") { PolyMesh m = BoxAA({-hs * 0.3f - 0.012f, top - 0.02f, -0.012f}, {-hs * 0.3f + 0.012f, top + 0.22f, 0.012f}); shapes::Append(m, BoxAA({hs * 0.3f - 0.012f, top - 0.02f, -0.012f}, {hs * 0.3f + 0.012f, top + 0.22f, 0.012f})); return m; }
            return BoxAA({-0.012f, top - 0.02f, -0.012f}, {0.012f, top + 0.26f, 0.012f});
        });
    }
}

// ---- Creature -------------------------------------------------------------------------------------------------
// Plans: 0 quadruped, 1 bird, 2 dragon, 3 insect (six legs), 4 spider (eight legs). Faces +z.
void GenerateCreature(const Recipe& r, std::vector<GeneratedPart>& parts) {
    Out o{r, parts};
    const int plan = (int)r.Param("plan");
    const float BL = Pf(r, "bodyLength"), BH = Pf(r, "bodyHeight"), LL = Pf(r, "legLength"), LT = Pf(r, "legThickness");
    const float HS = Pf(r, "headSize"), TL = Pf(r, "tailLength"), WS = Pf(r, "wingSpan"), NL = Pf(r, "neckLength");
    const float by = LL + BH * 0.5f;   // body centre height
    // body
    {
        Frame f = FrameAA({-BH * 0.5f, by - BH * 0.5f, -BL * 0.5f}, {BH * 0.5f, by + BH * 0.5f, BL * 0.5f});
        o.Add("body/0", "body", f, [&](const std::string& d) {
            if (d == "box") return BoxAA({-BH * 0.45f, by - BH * 0.5f, -BL * 0.5f}, {BH * 0.45f, by + BH * 0.5f, BL * 0.5f});
            if (d == "long") return Ell({0, by, 0}, BH * 0.4f, BH * 0.45f, BL * 0.62f);
            return Ell({0, by, 0}, BH * 0.5f, BH * 0.5f, BL * 0.5f);
        });
    }
    const float frontZ = BL * 0.5f;
    // head and neck
    const Vec3 neckBase{0, by + BH * 0.25f, frontZ - BH * 0.15f};
    const Vec3 neckTop = plan == 1 ? neckBase + Vec3{0, NL * 0.7f, NL * 0.4f} : plan == 2 ? neckBase + Vec3{0, NL * 0.6f, NL * 0.7f} : neckBase + Vec3{0, NL * 0.35f, NL * 0.5f};
    if (plan <= 2 && NL > 0.05f) {
        Frame f = FrameAA({-HS * 0.3f, neckBase.y, neckBase.z}, {HS * 0.3f, neckTop.y, neckTop.z});
        o.Add("neck/0", "neck", f, [&](const std::string&) { return Limb(neckBase, neckTop, HS * 0.28f, true); });
    }
    const Vec3 hc = plan == 3 || plan == 4 ? Vec3{0, by + BH * 0.1f, frontZ + HS * 0.35f} : neckTop + Vec3{0, HS * 0.15f, HS * 0.2f};
    {
        Frame f = FrameAA({hc.x - HS * 0.5f, hc.y - HS * 0.5f, hc.z - HS * 0.5f}, {hc.x + HS * 0.5f, hc.y + HS * 0.5f, hc.z + HS * 0.8f});
        o.Add("head/0", "head", f, [&](const std::string& d) {
            PolyMesh m = Ell(hc, HS * 0.5f, HS * 0.45f, HS * 0.5f);
            if (d == "snout" || d == "horse" || d == "dragon") {
                const float sl = d == "horse" ? HS * 0.7f : d == "dragon" ? HS * 0.6f : HS * 0.4f;
                shapes::Append(m, BoxAA({hc.x - HS * 0.22f, hc.y - HS * 0.25f, hc.z + HS * 0.3f}, {hc.x + HS * 0.22f, hc.y + HS * 0.12f, hc.z + HS * 0.3f + sl}));
            } else if (d == "beak") shapes::Append(m, Cone({hc.x, hc.y - HS * 0.05f, hc.z + HS * 0.3f}, {hc.x, hc.y - HS * 0.1f, hc.z + HS * 0.95f}, HS * 0.2f, 4));
            else if (d == "mandible") { for (int s = -1; s <= 1; s += 2) shapes::Append(m, Cone({hc.x + s * HS * 0.2f, hc.y - HS * 0.25f, hc.z + HS * 0.3f}, {hc.x + s * HS * 0.06f, hc.y - HS * 0.35f, hc.z + HS * 0.8f}, HS * 0.09f, 4)); }
            return m;
        });
        const float ey = hc.y + HS * 0.12f;
        Frame fe = FrameAA({hc.x - HS * 0.35f, ey - 0.02f, hc.z + HS * 0.2f}, {hc.x + HS * 0.35f, ey + 0.04f, hc.z + HS * 0.55f});
        o.Add("eyes/0", "face", fe, [&](const std::string& d) {
            PolyMesh m;
            const float es = d == "big" ? HS * 0.12f : HS * 0.06f;
            for (int s = -1; s <= 1; s += 2) shapes::Append(m, Ell({hc.x + s * HS * 0.3f, ey, hc.z + HS * 0.3f}, es, es, es, 4, 6));
            return m;
        });
        // ears and horns
        for (int side = 0; side < 2; ++side) {
            const float s = side ? 1.0f : -1.0f;
            const std::string sd = std::to_string(side);
            Frame f2 = FrameAA({hc.x + s * HS * 0.5f - 0.1f, hc.y + HS * 0.3f, hc.z - 0.1f}, {hc.x + s * HS * 0.5f + 0.1f, hc.y + HS * 0.9f, hc.z + 0.1f});
            o.Add("ear/" + sd, "ears", f2, [&](const std::string& d) {
                const Vec3 b{hc.x + s * HS * 0.3f, hc.y + HS * 0.35f, hc.z - HS * 0.05f};
                if (d == "floppy") return BoxAA({b.x + s * 0.0f - 0.025f + s * 0.04f, b.y - HS * 0.4f, b.z - HS * 0.12f}, {b.x + s * 0.0f + 0.025f + s * 0.04f, b.y + HS * 0.1f, b.z + HS * 0.12f});
                if (d == "round") return Ell({b.x + s * 0.05f, b.y + HS * 0.18f, b.z}, HS * 0.14f, HS * 0.17f, HS * 0.05f, 4, 8);
                return Cone(b, b + Vec3{s * HS * 0.12f, HS * 0.55f, 0}, HS * 0.16f, 4);
            });
            Frame f3 = FrameAA({hc.x + s * HS * 0.3f - 0.06f, hc.y + HS * 0.35f, hc.z - 0.1f}, {hc.x + s * HS * 0.3f + 0.06f, hc.y + HS * 0.95f, hc.z + 0.1f});
            o.Add("horn/" + sd, "horns", f3, [&](const std::string& d) {
                const Vec3 b{hc.x + s * HS * 0.25f, hc.y + HS * 0.32f, hc.z - HS * 0.05f};
                if (d == "antlers") { PolyMesh m = Limb(b, b + Vec3{s * HS * 0.2f, HS * 0.7f, -HS * 0.1f}, 0.02f, true); shapes::Append(m, Limb(b + Vec3{s * HS * 0.2f, HS * 0.5f, -HS * 0.08f}, b + Vec3{s * HS * 0.55f, HS * 0.8f, -HS * 0.1f}, 0.015f, true)); shapes::Append(m, Limb(b + Vec3{s * HS * 0.12f, HS * 0.3f, -HS * 0.05f}, b + Vec3{s * HS * 0.45f, HS * 0.45f, 0}, 0.014f, true)); return m; }
                if (d == "curved") { PolyMesh m = Cone(b, b + Vec3{s * HS * 0.2f, HS * 0.45f, -HS * 0.15f}, HS * 0.1f, 6); shapes::Append(m, Cone(b + Vec3{s * HS * 0.2f, HS * 0.43f, -HS * 0.14f}, b + Vec3{s * HS * 0.1f, HS * 0.85f, -HS * 0.45f}, HS * 0.06f, 6)); return m; }
                return Cone(b, b + Vec3{s * HS * 0.1f, HS * 0.5f, -HS * 0.05f}, HS * 0.1f, 6);
            });
        }
    }
    // tail
    {
        const Vec3 b{0, by + (plan == 1 ? 0.0f : BH * 0.1f), -BL * 0.5f + BH * 0.2f};
        Frame f = FrameAA({-0.1f, b.y - TL * 0.5f, b.z - TL}, {0.1f, b.y + TL * 0.4f, b.z + 0.05f});
        o.Add("tail/0", "tail", f, [&](const std::string& d) {
            if (d == "fan") return BoxAA({-TL * 0.35f, b.y - 0.02f, b.z - TL * 0.9f}, {TL * 0.35f, b.y + 0.03f, b.z + 0.02f});
            if (d == "bushy") return Ell(b + Vec3{0, TL * 0.1f, -TL * 0.45f}, TL * 0.2f, TL * 0.2f, TL * 0.5f, 5, 8);
            if (d == "long") {
                PolyMesh m;
                Vec3 p = b;
                for (int k = 0; k < 4; ++k) { const Vec3 q = p + Vec3{0, -TL * 0.04f * (float)(k < 2 ? -1 : 1), -TL * 0.25f}; shapes::Append(m, Limb(p, q, LT * (1.1f - 0.2f * (float)k), true)); p = q; }
                return m;
            }
            return Limb(b, b + Vec3{0, TL * 0.2f, -TL * 0.5f}, LT * 0.9f, true);
        });
    }
    // legs
    const int nLegs = plan == 1 ? 2 : plan == 3 ? 6 : plan == 4 ? 8 : 4;
    for (int i = 0; i < nLegs; ++i) {
        const float s = (i % 2) ? 1.0f : -1.0f;
        const int row = i / 2, rows = nLegs / 2;
        const float z = rows == 1 ? -BL * 0.05f : BL * (0.35f - 0.7f * (float)row / (float)(rows - 1));
        const Vec3 hip{s * BH * (plan >= 3 ? 0.35f : 0.38f) + s * 0.0007f * (float)i, by - BH * 0.3f, z};
        const std::string id = "leg/" + std::to_string(i);
        const bool spread = plan >= 3;
        const Vec3 knee = spread ? Vec3{hip.x + s * LL * 0.7f, hip.y + LL * 0.25f, hip.z + (row - (rows - 1) * 0.5f) * 0.1f} : Vec3{hip.x + s * LL * 0.08f, hip.y - LL * 0.5f, hip.z - 0.02f * (float)(plan == 1 ? -1 : 1)};
        const Vec3 foot = spread ? Vec3{knee.x + s * LL * 0.3f + s * 0.0011f * (float)i, 0.01f, knee.z} : Vec3{knee.x + s * 0.0011f * (float)i, 0.01f, knee.z + 0.03f};
        const Vec3 lo{std::min(hip.x, foot.x) - LT, 0, std::min(hip.z, foot.z) - LT}, hi{std::max(hip.x, foot.x) + LT, std::max(hip.y, knee.y), std::max(hip.z, foot.z) + LT};
        o.Add(id, "legs", FrameAA(lo, hi), [&](const std::string& d) {
            const float k = d == "thick" ? 1.4f : d == "thin" ? 0.7f : 1.0f;
            const bool rnd = d != "boxy";
            PolyMesh m = Limb(hip, knee, LT * k, rnd);
            shapes::Append(m, Limb(knee, foot, LT * k * 0.8f, rnd));
            if (plan <= 2 && plan != 1) shapes::Append(m, BoxAA({foot.x - LT * 0.9f, 0, foot.z - LT * 0.6f}, {foot.x + LT * 0.9f, LT * 0.8f, foot.z + LT * 1.6f}));   // paw
            if (plan == 1) for (int c = -1; c <= 1; ++c) shapes::Append(m, Cone({foot.x + c * 0.02f, 0.03f, foot.z}, {foot.x + c * 0.04f, 0.0f + 0.005f, foot.z + 0.1f}, 0.012f, 4));
            return m;
        });
    }
    // wings
    for (int side = 0; side < 2; ++side) {
        const float s = side ? 1.0f : -1.0f;
        const std::string sd = std::to_string(side);
        const Vec3 root{s * BH * 0.3f, by + BH * 0.35f, BL * 0.05f};
        Frame f = FrameAA({std::min(root.x, root.x + s * WS * 0.5f) - 0.05f, root.y - 0.1f, root.z - WS * 0.3f}, {std::max(root.x, root.x + s * WS * 0.5f) + 0.05f, root.y + WS * 0.25f, root.z + WS * 0.2f});
        o.Add("wing/" + sd, "wings", f, [&](const std::string& d) {
            const float L2 = WS * 0.5f;
            if (d == "bat") {
                PolyMesh m = Limb(root, root + Vec3{s * L2, WS * 0.2f, -WS * 0.05f}, 0.025f, true);
                std::vector<Vec2> prof = {{0, WS * 0.1f}, {0, -WS * 0.3f}, {L2 * 0.95f, -WS * 0.05f}};   // (x, z), counter-clockwise for the right wing
                if (s < 0) { for (Vec2& p : prof) p.x = -p.x; std::reverse(prof.begin(), prof.end()); }
                shapes::Append(m, shapes::ExtrudeAxes({root.x, root.y - 0.03f, root.z}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}, prof, 0.012f));
                return m;
            }
            if (d == "insect") return BoxAA({std::min(root.x, root.x + s * L2 * 0.9f), root.y + 0.01f, root.z - WS * 0.2f}, {std::max(root.x, root.x + s * L2 * 0.9f), root.y + 0.03f, root.z + WS * 0.12f});
            // bird: a long feathered plank with a thick root
            PolyMesh m = BoxAA({std::min(root.x, root.x + s * L2), root.y - 0.02f, root.z - WS * 0.14f}, {std::max(root.x, root.x + s * L2), root.y + 0.04f, root.z + WS * 0.1f});
            shapes::Append(m, BoxAA({std::min(root.x + s * L2 * 0.5f, root.x + s * L2 * 1.1f), root.y - 0.015f, root.z - WS * 0.26f}, {std::max(root.x + s * L2 * 0.5f, root.x + s * L2 * 1.1f), root.y + 0.03f, root.z - WS * 0.12f}));
            return m;
        });
    }
    // antennae (insects)
    if (plan == 3 || plan == 4) for (int side = 0; side < 2; ++side) {
        const float s = side ? 1.0f : -1.0f;
        const Vec3 b = hc + Vec3{s * HS * 0.25f, HS * 0.35f, HS * 0.3f};
        Frame f = FrameAA({b.x - 0.1f, b.y, b.z}, {b.x + 0.1f, b.y + 0.3f, b.z + 0.3f});
        o.Add("antenna/" + std::to_string(side), "antennae", f, [&](const std::string&) { return Limb(b, b + Vec3{s * HS * 0.3f, HS * 0.7f, HS * 0.5f}, 0.012f, false); });
    }
    // abdomen (insect, spider): a second body segment behind
    if (plan == 3 || plan == 4) {
        const float ab = BH * (plan == 4 ? 0.8f : 0.6f);
        o.Add("abdomen/0", "abdomen", FrameAA({-ab, by - ab * 0.8f, -BL * 0.5f - ab * 1.6f}, {ab, by + ab * 0.8f, -BL * 0.5f + ab * 0.2f}), [&](const std::string& d) {
            if (d == "segmented") { PolyMesh m; for (int k = 0; k < 3; ++k) shapes::Append(m, Ell({0, by, -BL * 0.5f - ab * (0.3f + 0.6f * (float)k)}, ab * (0.75f - 0.1f * (float)k), ab * (0.7f - 0.1f * (float)k), ab * 0.35f, 4, 8)); return m; }
            return Ell({0, by, -BL * 0.5f - ab * 0.7f}, ab, ab * 0.8f, ab * 0.9f);
        });
    }
}


// The human: blocky below Medium (the deliberate low-poly end), organic from Medium up. Same part ids and designs at every level.
void GenerateHuman(const Recipe& r, std::vector<GeneratedPart>& parts) {
    if (shapes::DetailF() >= 0.999f) { GenerateHumanOrganic(r, parts, GenerateBiped); return; }
    Recipe b = r;   // Low: the separate face parts are drawn as the old combined face
    if (b.DesignFor("face", "face/0") == "none" && b.DesignFor("eyes", "eyes/0") != "none" && !b.DesignFor("eyes", "eyes/0").empty()) b.slotDesign["face"] = b.DesignFor("eyes", "eyes/0") == "big" ? "big" : "simple";
    GenerateBiped(b, parts);
}

// ---- Template definitions ---------------------------------------------------------------------------------------
struct B {
    Template t;
    using K = ParamDef::Kind;
    void P(const char* id, const char* label, double def, double mn, double mx, const char* group, const char* tip, K k = K::Float) {
        ParamDef p; p.id = id; p.label = label; p.kind = k; p.def = def; p.min = mn; p.max = mx; p.group = group; p.tip = tip;
        t.params.push_back(p);
    }
    void Plan(std::vector<std::string> ch, int def) {
        ParamDef p; p.id = "plan"; p.label = "Body plan"; p.kind = K::Choice; p.def = def; p.min = 0; p.max = (double)ch.size() - 1; p.group = "Body"; p.tip = "The layout of the creature."; p.choices = std::move(ch);
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
const Color kSkin{0.87f, 0.68f, 0.55f, 1}, kSkinD{0.55f, 0.38f, 0.28f, 1}, kBrown{0.32f, 0.2f, 0.12f, 1}, kBlond{0.85f, 0.72f, 0.35f, 1}, kBlack{0.1f, 0.1f, 0.11f, 1},
            kWhite{0.92f, 0.92f, 0.9f, 1}, kRed{0.75f, 0.15f, 0.15f, 1}, kBlue{0.2f, 0.35f, 0.7f, 1}, kGreen{0.25f, 0.5f, 0.3f, 1}, kGrey{0.5f, 0.52f, 0.55f, 1},
            kSteel{0.62f, 0.65f, 0.7f, 1}, kYellow{0.9f, 0.75f, 0.15f, 1}, kLeather{0.45f, 0.3f, 0.18f, 1}, kOrange{0.9f, 0.5f, 0.15f, 1}, kTeal{0.15f, 0.6f, 0.6f, 1};

void BipedParams(B& b, double H, double sw, double td, double hs, double legF, double th, double armF, double neck) {
    b.P("height", "Height", H, 0.5, 3.0, "Proportions", "Overall height.");
    b.P("shoulderWidth", "Shoulder width", sw, 0.2, 1.0, "Proportions", "Width across the shoulders.");
    b.P("torsoDepth", "Chest depth", td, 0.1, 0.6, "Proportions", "Front-to-back thickness of the torso.");
    b.P("headSize", "Head size", hs, 0.1, 0.6, "Proportions", "Size of the head.");
    b.P("legLength", "Leg length", legF, 0.3, 0.6, "Proportions", "Legs as a fraction of the height.");
    b.P("armLength", "Arm length", armF, 0.25, 0.5, "Proportions", "Arms (with hands) as a fraction of the height.");
    b.P("limbThickness", "Limb thickness", th, 0.02, 0.16, "Proportions", "Radius of the arms; legs are thicker.");
    b.P("neckLength", "Neck length", neck, 0.0, 0.25, "Proportions", "Length of the neck.");
    b.P("armAngle", "Arm angle (degrees)", 10, 0, 90, "Pose", "How far the arms stand out from the body.");
    b.P("elbowBend", "Elbow bend (degrees)", 15, 0, 90, "Pose", "How far the forearms bend forward.");
    b.P("stance", "Leg spread (degrees)", 5, 0, 30, "Pose", "How far apart the legs stand.");
}
std::vector<DesignDef> HandDesigns() { return {{"block", "Block"}, {"mitten", "Mitten"}, {"fist", "Fist"}, {"fingers", "Fingers"}, {"claw", "Claw"}, {"pincer", "Pincer"}}; }
std::vector<DesignDef> LimbDesigns() { return {{"round", "Round"}, {"boxy", "Boxy"}, {"thick", "Thick"}, {"piston", "Thin piston"}}; }
std::vector<DesignDef> FootDesigns() { return {{"block", "Block"}, {"boot", "Boot"}, {"sneaker", "Sneaker"}, {"round", "Round shoe"}, {"tread", "Tread"}, {"hover", "Hover disc"}}; }

Template MakeHuman() {
    B b;
    b.t.id = "human"; b.t.name = "Human"; b.t.category = "Characters"; b.t.version = 1;
    b.t.description = "A person: head, face, hair, torso, arms, hands, legs, feet, clothes and accessories, each a part you can change, with body proportion sliders.";
    BipedParams(b, 1.75, 0.46, 0.24, 0.23, 0.47, 0.045, 0.40, 0.06);
    { ParamDef p; p.id = "bodyType"; p.label = "Body type"; p.kind = ParamDef::Kind::Choice; p.def = 0; p.min = 0; p.max = 2; p.group = "Proportions"; p.tip = "Neutral, male or female build (shoulders, waist, hips, chest)."; p.choices = {"Neutral", "Male", "Female"}; b.t.params.push_back(p); }
    b.S("head", "Head", {{"round", "Round"}, {"oval", "Oval"}, {"box", "Square"}, {"pointed", "Pointed"}}, kSkin, 0);
    b.S("face", "Face", {{"simple", "Eyes and nose"}, {"big", "Big eyes"}, {"none", "None"}}, kBlack, 1);
    b.S("eyes", "Eyes", {{"none", "None"}, {"round", "Round"}, {"big", "Big"}, {"narrow", "Narrow"}}, kBlack, 11);
    b.S("brows", "Eyebrows", {{"none", "None"}, {"soft", "Soft"}, {"thick", "Thick"}, {"angular", "Angular"}}, kBrown, 12);
    b.S("nose", "Nose", {{"none", "None"}, {"straight", "Straight"}, {"button", "Button"}, {"long", "Long"}, {"flat", "Flat"}}, kSkin, 13);
    b.S("mouth", "Mouth", {{"none", "None"}, {"smile", "Smile"}, {"neutral", "Neutral"}, {"small", "Small"}}, Color{0.7f, 0.3f, 0.3f, 1}, 14);
    b.S("ears", "Ears", {{"none", "None"}, {"normal", "Normal"}, {"small", "Small"}, {"big", "Big"}}, kSkin, 15);
    b.S("hair", "Hair", {{"none", "Bald"}, {"short", "Short"}, {"long", "Long"}, {"mohawk", "Mohawk"}, {"bun", "Bun"}, {"spiky", "Spiky"}}, kBrown, 2);
    b.S("neck", "Neck", {{"round", "Round"}, {"thin", "Thin"}}, kSkin, 0);
    b.S("torso", "Torso", {{"box", "Box"}, {"tapered", "Tapered"}, {"barrel", "Barrel"}}, kSkin, 0);
    b.S("pelvis", "Hips", {{"box", "Hips"}}, kSkin, 0);
    b.S("arm", "Arms", LimbDesigns(), kSkin, 0);
    b.S("hand", "Hands", {{"block", "Block"}, {"mitten", "Mitten"}, {"fist", "Fist"}, {"fingers", "Fingers"}}, kSkin, 0);
    b.S("leg", "Legs", LimbDesigns(), kSkin, 0);
    b.S("foot", "Feet", {{"block", "Bare foot"}, {"boot", "Boot"}, {"sneaker", "Sneaker"}, {"round", "Round shoe"}}, kSkin, 0);
    b.S("shirt", "Top", {{"none", "None"}, {"tshirt", "T-shirt"}, {"jacket", "Jacket"}, {"vest", "Vest"}}, kBlue, 3);
    b.S("pants", "Bottoms", {{"none", "None"}, {"shorts", "Shorts"}, {"trousers", "Trousers"}, {"skirt", "Skirt"}}, kGrey, 4);
    b.S("hat", "Hat", {{"none", "None"}, {"cap", "Cap"}, {"tophat", "Top hat"}, {"helmet", "Helmet"}, {"beanie", "Beanie"}}, kRed, 5);
    b.S("glasses", "Glasses", {{"none", "None"}, {"round", "Round"}, {"square", "Square"}, {"visor", "Visor"}}, kBlack, 6);
    b.S("backpack", "Backpack", {{"none", "None"}, {"small", "Small"}, {"large", "Large"}}, kLeather, 7);
    b.S("belt", "Belt", {{"none", "None"}, {"belt", "Belt"}}, kLeather, 8);
    b.S("cape", "Cape", {{"none", "None"}, {"short", "Short cape"}, {"long", "Long cape"}}, kRed, 9);
    b.S("shoulder", "Shoulder pads", {{"none", "None"}, {"round", "Round"}, {"plate", "Plate"}, {"spike", "Spike"}}, kSteel, 10);
    const std::map<std::string, Color> clothes = {{"shirt", kBlue}, {"pants", kGrey}, {"hair", kBrown}};
    b.St("average", "Average adult", "A medium build in a T-shirt and trousers.", {}, {{"head", "round"}, {"hair", "short"}, {"torso", "box"}, {"hand", "block"}, {"foot", "sneaker"}, {"shirt", "tshirt"}, {"pants", "trousers"}}, clothes);
    b.St("athletic", "Athletic", "Broad shoulders, thick limbs.", {{"shoulderWidth", 0.56}, {"limbThickness", 0.06}, {"torsoDepth", 0.27}, {"height", 1.85}},
         {{"head", "box"}, {"hair", "spiky"}, {"torso", "tapered"}, {"arm", "thick"}, {"leg", "thick"}, {"hand", "fist"}, {"foot", "sneaker"}, {"shirt", "tshirt"}, {"pants", "shorts"}}, {{"shirt", kRed}, {"pants", kBlack}, {"hair", kBlack}});
    b.St("heavy", "Heavy build", "A wide barrel chest and a round face.", {{"shoulderWidth", 0.55}, {"torsoDepth", 0.36}, {"limbThickness", 0.062}, {"height", 1.7}},
         {{"head", "round"}, {"hair", "none"}, {"torso", "barrel"}, {"hand", "block"}, {"foot", "boot"}, {"shirt", "vest"}, {"pants", "trousers"}, {"belt", "belt"}}, {{"shirt", kGreen}, {"pants", kBrown}});
    b.St("child", "Child", "Small, with a big head and short legs.", {{"height", 1.1}, {"headSize", 0.22}, {"legLength", 0.42}, {"shoulderWidth", 0.3}, {"torsoDepth", 0.18}, {"limbThickness", 0.032}, {"armLength", 0.38}},
         {{"head", "round"}, {"face", "big"}, {"hair", "bun"}, {"torso", "barrel"}, {"hand", "mitten"}, {"foot", "round"}, {"shirt", "tshirt"}, {"pants", "shorts"}}, {{"shirt", kYellow}, {"pants", kBlue}, {"hair", kBlond}});
    b.St("tall_slim", "Tall and slim", "A long, thin figure with a pointed face.", {{"height", 1.95}, {"shoulderWidth", 0.4}, {"torsoDepth", 0.19}, {"limbThickness", 0.034}, {"legLength", 0.5}, {"neckLength", 0.1}},
         {{"head", "oval"}, {"hair", "long"}, {"torso", "tapered"}, {"arm", "piston"}, {"leg", "piston"}, {"hand", "fingers"}, {"foot", "boot"}, {"shirt", "jacket"}, {"pants", "trousers"}}, {{"shirt", kBlack}, {"pants", kBlack}, {"hair", kBrown}});
    b.St("adventurer", "Adventurer", "A hat, a backpack, a belt and boots.", {}, {{"head", "round"}, {"hair", "short"}, {"hand", "block"}, {"foot", "boot"}, {"shirt", "jacket"}, {"pants", "trousers"}, {"hat", "cap"}, {"backpack", "large"}, {"belt", "belt"}, {"glasses", "round"}},
         {{"shirt", kLeather}, {"pants", kBrown}, {"hat", kGreen}, {"backpack", kOrange}, {"hair", kBrown}});
    b.St("hero", "Caped hero", "A visor, a long cape and shoulder plates.", {{"shoulderWidth", 0.52}, {"limbThickness", 0.052}, {"height", 1.85}},
         {{"head", "box"}, {"hair", "none"}, {"torso", "tapered"}, {"arm", "thick"}, {"hand", "fist"}, {"foot", "boot"}, {"shirt", "vest"}, {"pants", "trousers"}, {"glasses", "visor"}, {"cape", "long"}, {"belt", "belt"}, {"shoulder", "plate"}},
         {{"shirt", kBlue}, {"pants", kBlue}, {"cape", kRed}, {"glasses", kBlack}, {"shoulder", kYellow}});
    b.St("casual", "Casual", "A beanie, glasses and a hoodie look.", {}, {{"head", "oval"}, {"hair", "long"}, {"hand", "block"}, {"foot", "sneaker"}, {"shirt", "jacket"}, {"pants", "trousers"}, {"hat", "beanie"}, {"glasses", "square"}},
         {{"shirt", kTeal}, {"pants", kGrey}, {"hat", kOrange}, {"hair", kBlack}});
    b.St("dress", "Person in a dress", "A skirt, a bun and round glasses.", {{"shoulderWidth", 0.4}, {"torsoDepth", 0.2}, {"limbThickness", 0.038}}, {{"head", "round"}, {"hair", "bun"}, {"torso", "tapered"}, {"hand", "block"}, {"foot", "round"}, {"shirt", "tshirt"}, {"pants", "skirt"}, {"glasses", "round"}, {"belt", "belt"}},
         {{"shirt", kRed}, {"pants", kRed}, {"hair", kBrown}});
    // Every start gets the separate face parts; the older combined face stays for recipes saved before them.
    for (StartDesign& st : b.t.starts) {
        const bool big = st.slotDesign.count("face") && st.slotDesign["face"] == "big";
        st.slotDesign["face"] = "none";
        st.slotDesign["eyes"] = big ? "big" : "round";
        st.slotDesign["brows"] = st.id == "athletic" || st.id == "hero" ? "thick" : "soft";
        st.slotDesign["nose"] = st.id == "child" ? "button" : "straight";
        st.slotDesign["mouth"] = "smile";
        st.slotDesign["ears"] = "normal";
        st.colors["eyes"] = kBlack; st.colors["mouth"] = Color{0.7f, 0.3f, 0.3f, 1}; st.colors["brows"] = st.colors.count("hair") ? st.colors["hair"] : kBrown;
        st.params["bodyType"] = st.id == "athletic" || st.id == "hero" || st.id == "heavy" ? 1 : st.id == "dress" || st.id == "casual" ? 2 : 0;
    }
    b.t.generate = GenerateHuman;
    return b.t;
}

Template MakeRobot() {
    B b;
    b.t.id = "robot"; b.t.name = "Robot"; b.t.category = "Characters"; b.t.version = 1;
    b.t.description = "A robot: head, face panel, torso, arms, hands, legs, feet, antenna, shoulder pads and a backpack, each a part you can change.";
    BipedParams(b, 1.8, 0.6, 0.34, 0.3, 0.44, 0.06, 0.4, 0.08);
    b.S("head", "Head", {{"cube", "Cube with visor"}, {"dome", "Dome"}, {"cyclops", "Cyclops"}, {"box", "Plain box"}}, kSteel, 0);
    b.S("face", "Face panel", {{"eyes", "Two eyes"}, {"slit", "Visor slit"}, {"grille", "Speaker grille"}, {"none", "None"}}, kYellow, 1);
    b.S("neck", "Neck", {{"piston", "Piston"}, {"thin", "Thin"}, {"round", "Round"}}, kGrey, 0);
    b.S("torso", "Torso", {{"box", "Box"}, {"barrel", "Barrel"}, {"power", "Power core"}, {"tapered", "Tapered"}}, kSteel, 0);
    b.S("pelvis", "Hips", {{"box", "Hips"}}, kGrey, 0);
    b.S("arm", "Arms", {{"piston", "Thin piston"}, {"boxy", "Boxy"}, {"thick", "Thick"}, {"round", "Round"}}, kGrey, 0);
    b.S("hand", "Hands", {{"claw", "Claw"}, {"pincer", "Pincer"}, {"block", "Block"}, {"fist", "Fist"}}, kSteel, 0);
    b.S("leg", "Legs", {{"piston", "Thin piston"}, {"boxy", "Boxy"}, {"thick", "Thick"}, {"round", "Round"}}, kGrey, 0);
    b.S("foot", "Feet", {{"block", "Block"}, {"tread", "Tread"}, {"hover", "Hover disc"}, {"round", "Round"}}, kSteel, 0);
    b.S("shirt", "Armour", {{"none", "None"}, {"plates", "Chest plate"}, {"vest", "Vest plate"}}, kOrange, 3);
    b.S("pants", "Leg armour", {{"none", "None"}, {"greaves", "Greaves"}, {"shorts", "Thigh guards"}}, kOrange, 4);
    b.S("antenna", "Antenna", {{"none", "None"}, {"rod", "Rod"}, {"dish", "Dish"}, {"twin", "Twin rods"}}, kGrey, 5);
    b.S("shoulder", "Shoulder pads", {{"none", "None"}, {"round", "Round"}, {"plate", "Plate"}, {"spike", "Spike"}}, kOrange, 6);
    b.S("backpack", "Backpack", {{"none", "None"}, {"small", "Pack"}, {"jet", "Jet pack"}}, kGrey, 7);
    b.S("belt", "Belt", {{"none", "None"}, {"belt", "Belt"}}, kGrey, 8);
    b.St("standard", "Standard robot", "A cube head with a visor and claws.", {}, {{"head", "cube"}, {"face", "eyes"}, {"neck", "piston"}, {"torso", "power"}, {"arm", "boxy"}, {"hand", "claw"}, {"leg", "boxy"}, {"foot", "block"}, {"antenna", "rod"}}, {});
    b.St("heavy_bot", "Heavy robot", "Wide, thick and armoured, on treads.", {{"shoulderWidth", 0.8}, {"torsoDepth", 0.45}, {"limbThickness", 0.085}, {"height", 2.1}, {"headSize", 0.34}, {"legLength", 0.36}},
         {{"head", "box"}, {"face", "slit"}, {"neck", "thin"}, {"torso", "box"}, {"arm", "thick"}, {"hand", "pincer"}, {"leg", "thick"}, {"foot", "tread"}, {"shirt", "plates"}, {"shoulder", "plate"}}, {{"torso", kGreen}, {"shirt", kYellow}});
    b.St("scout", "Scout drone", "Small, light and hovering.", {{"height", 1.2}, {"shoulderWidth", 0.4}, {"torsoDepth", 0.22}, {"headSize", 0.24}, {"limbThickness", 0.035}}, {{"head", "dome"}, {"face", "slit"}, {"neck", "round"}, {"torso", "barrel"}, {"arm", "piston"}, {"hand", "pincer"}, {"leg", "piston"}, {"foot", "hover"}, {"antenna", "twin"}, {"backpack", "jet"}},
         {{"torso", kWhite}, {"head", kWhite}});
    b.St("cyclops", "Cyclops bot", "One big eye, a power core and spiked shoulders.", {{"height", 1.9}}, {{"head", "cyclops"}, {"face", "none"}, {"neck", "piston"}, {"torso", "power"}, {"arm", "thick"}, {"hand", "fist"}, {"leg", "round"}, {"foot", "block"}, {"shoulder", "spike"}}, {{"torso", kSteel}});
    b.St("android", "Android", "A slim, nearly human machine.", {{"height", 1.78}, {"shoulderWidth", 0.46}, {"torsoDepth", 0.24}, {"headSize", 0.23}, {"limbThickness", 0.04}},
         {{"head", "dome"}, {"face", "eyes"}, {"neck", "thin"}, {"torso", "tapered"}, {"arm", "round"}, {"hand", "block"}, {"leg", "round"}, {"foot", "block"}, {"belt", "belt"}}, {{"head", kWhite}, {"torso", kWhite}, {"arm", kWhite}, {"leg", kWhite}, {"face", kTeal}});
    b.St("rover", "Rover bot", "A box body on treads with a dish antenna.", {{"height", 1.5}, {"legLength", 0.3}, {"shoulderWidth", 0.7}}, {{"head", "box"}, {"face", "grille"}, {"neck", "thin"}, {"torso", "box"}, {"arm", "piston"}, {"hand", "claw"}, {"leg", "boxy"}, {"foot", "tread"}, {"antenna", "dish"}, {"backpack", "small"}}, {{"torso", kOrange}});
    b.t.generate = GenerateBiped;
    return b.t;
}

Template MakeCreature() {
    B b;
    b.t.id = "creature"; b.t.name = "Creature"; b.t.category = "Characters"; b.t.version = 1;
    b.t.description = "An animal or monster: a quadruped, a bird, a dragon, an insect or a spider, with head, ears, horns, tail, wings and legs you can change.";
    b.Plan({"Quadruped", "Bird", "Dragon", "Insect (6 legs)", "Spider (8 legs)"}, 0);
    b.P("bodyLength", "Body length", 1.0, 0.3, 4, "Body", "Length of the body.");
    b.P("bodyHeight", "Body height", 0.5, 0.15, 1.5, "Body", "Thickness of the body.");
    b.P("legLength", "Leg length", 0.5, 0.1, 1.6, "Body", "Length of the legs.");
    b.P("legThickness", "Leg thickness", 0.06, 0.015, 0.2, "Body", "Thickness of the legs.");
    b.P("headSize", "Head size", 0.3, 0.1, 1.0, "Body", "Size of the head.");
    b.P("neckLength", "Neck length", 0.3, 0.0, 1.5, "Body", "Length of the neck (not for insects).");
    b.P("tailLength", "Tail length", 0.6, 0.1, 3, "Body", "Length of the tail.");
    b.P("wingSpan", "Wing span", 1.2, 0.3, 4, "Body", "Span of the wings (when it has them).");
    b.S("body", "Body", {{"oval", "Oval"}, {"long", "Long"}, {"box", "Boxy"}}, kBrown, 0);
    b.S("head", "Head", {{"round", "Round"}, {"snout", "Snout"}, {"horse", "Long muzzle"}, {"beak", "Beak"}, {"dragon", "Dragon snout"}, {"mandible", "Mandibles"}}, kBrown, 0);
    b.S("neck", "Neck", {{"round", "Neck"}}, kBrown, 0);
    b.S("eyes", "Eyes", {{"small", "Small"}, {"big", "Big"}, {"none", "None"}}, kBlack, 1);
    b.S("ears", "Ears", {{"none", "None"}, {"pointed", "Pointed"}, {"floppy", "Floppy"}, {"round", "Round"}}, kBrown, 0);
    b.S("horns", "Horns", {{"none", "None"}, {"short", "Short"}, {"curved", "Curved"}, {"antlers", "Antlers"}}, kWhite, 2);
    b.S("tail", "Tail", {{"none", "None"}, {"short", "Short"}, {"long", "Long"}, {"bushy", "Bushy"}, {"fan", "Fan"}}, kBrown, 0);
    b.S("wings", "Wings", {{"none", "None"}, {"bird", "Feathered"}, {"bat", "Bat"}, {"insect", "Insect"}}, kBrown, 3);
    b.S("legs", "Legs", {{"round", "Round"}, {"thick", "Thick"}, {"thin", "Thin"}, {"boxy", "Boxy"}}, kBrown, 0);
    b.S("antennae", "Antennae", {{"feelers", "Feelers"}, {"none", "None"}}, kBlack, 4);
    b.S("abdomen", "Abdomen", {{"round", "Round"}, {"segmented", "Segmented"}}, kBrown, 5);
    const Color kFur{0.55f, 0.4f, 0.25f, 1}, kGold{0.7f, 0.55f, 0.2f, 1}, kDragonG{0.2f, 0.5f, 0.3f, 1}, kBeetle{0.15f, 0.3f, 0.2f, 1}, kSpiderC{0.2f, 0.15f, 0.15f, 1};
    b.St("dog", "Dog", "Floppy ears, a snout and a long tail.", {{"plan", 0}, {"bodyLength", 0.9}, {"bodyHeight", 0.38}, {"legLength", 0.4}, {"headSize", 0.26}, {"tailLength", 0.5}},
         {{"body", "long"}, {"head", "snout"}, {"ears", "floppy"}, {"tail", "long"}, {"legs", "round"}, {"eyes", "small"}}, {{"body", kFur}, {"head", kFur}, {"ears", kBrown}, {"neck", kFur}, {"tail", kFur}, {"legs", kFur}});
    b.St("horse", "Horse", "A tall body, a long neck and a long muzzle.", {{"plan", 0}, {"bodyLength", 1.8}, {"bodyHeight", 0.6}, {"legLength", 1.0}, {"legThickness", 0.07}, {"headSize", 0.36}, {"neckLength", 0.8}, {"tailLength", 0.8}},
         {{"body", "oval"}, {"head", "horse"}, {"ears", "pointed"}, {"tail", "bushy"}, {"legs", "thin"}, {"eyes", "small"}}, {{"body", kBrown}, {"head", kBrown}, {"neck", kBrown}, {"legs", kBrown}, {"tail", kBlack}});
    b.St("bear", "Bear", "A heavy body on thick legs.", {{"plan", 0}, {"bodyLength", 1.3}, {"bodyHeight", 0.8}, {"legLength", 0.5}, {"legThickness", 0.14}, {"headSize", 0.4}, {"tailLength", 0.15}},
         {{"body", "oval"}, {"head", "snout"}, {"ears", "round"}, {"tail", "short"}, {"legs", "thick"}, {"eyes", "small"}}, {{"body", kBrown}, {"head", kBrown}, {"ears", kBrown}, {"legs", kBrown}});
    b.St("stag", "Stag", "A slim body with antlers.", {{"plan", 0}, {"bodyLength", 1.4}, {"bodyHeight", 0.5}, {"legLength", 0.9}, {"legThickness", 0.045}, {"headSize", 0.28}, {"neckLength", 0.6}, {"tailLength", 0.15}},
         {{"body", "oval"}, {"head", "horse"}, {"ears", "pointed"}, {"horns", "antlers"}, {"tail", "short"}, {"legs", "thin"}}, {{"body", kGold}, {"head", kGold}, {"neck", kGold}, {"legs", kGold}, {"horns", kWhite}});
    b.St("eagle", "Eagle", "Two legs, a beak and big feathered wings.", {{"plan", 1}, {"bodyLength", 0.7}, {"bodyHeight", 0.35}, {"legLength", 0.3}, {"legThickness", 0.03}, {"headSize", 0.2}, {"neckLength", 0.15}, {"tailLength", 0.4}, {"wingSpan", 2.0}},
         {{"body", "oval"}, {"head", "beak"}, {"tail", "fan"}, {"wings", "bird"}, {"legs", "thin"}, {"eyes", "small"}}, {{"body", kBrown}, {"head", kWhite}, {"neck", kWhite}, {"wings", kBrown}, {"tail", kBrown}, {"legs", kYellow}});
    b.St("duck", "Duck", "A small round bird with short wings.", {{"plan", 1}, {"bodyLength", 0.5}, {"bodyHeight", 0.35}, {"legLength", 0.15}, {"legThickness", 0.025}, {"headSize", 0.18}, {"neckLength", 0.12}, {"tailLength", 0.15}, {"wingSpan", 0.7}},
         {{"body", "oval"}, {"head", "beak"}, {"tail", "short"}, {"wings", "bird"}, {"legs", "thin"}, {"eyes", "big"}}, {{"body", kWhite}, {"head", kGreen}, {"neck", kGreen}, {"wings", kWhite}, {"legs", kOrange}});
    b.St("dragon", "Dragon", "A long body, horns, bat wings and a long tail.", {{"plan", 2}, {"bodyLength", 2.4}, {"bodyHeight", 0.9}, {"legLength", 0.7}, {"legThickness", 0.13}, {"headSize", 0.5}, {"neckLength", 0.9}, {"tailLength", 2.2}, {"wingSpan", 3.2}},
         {{"body", "long"}, {"head", "dragon"}, {"horns", "curved"}, {"tail", "long"}, {"wings", "bat"}, {"legs", "thick"}, {"eyes", "big"}}, {{"body", kDragonG}, {"head", kDragonG}, {"neck", kDragonG}, {"tail", kDragonG}, {"wings", kRed}, {"legs", kDragonG}, {"horns", kWhite}});
    b.St("wyvern", "Wyvern", "A slimmer two-winged dragon with short horns.", {{"plan", 2}, {"bodyLength", 1.8}, {"bodyHeight", 0.6}, {"legLength", 0.5}, {"legThickness", 0.08}, {"headSize", 0.4}, {"neckLength", 0.7}, {"tailLength", 1.8}, {"wingSpan", 2.8}},
         {{"body", "long"}, {"head", "dragon"}, {"horns", "short"}, {"tail", "long"}, {"wings", "bat"}, {"legs", "thin"}}, {{"body", kGrey}, {"head", kGrey}, {"neck", kGrey}, {"tail", kGrey}, {"wings", kBlue}, {"legs", kGrey}});
    b.St("beetle", "Beetle", "A rounded shell and six legs.", {{"plan", 3}, {"bodyLength", 0.5}, {"bodyHeight", 0.3}, {"legLength", 0.25}, {"legThickness", 0.02}, {"headSize", 0.16}, {"tailLength", 0.1}, {"wingSpan", 0.5}},
         {{"body", "oval"}, {"head", "mandible"}, {"wings", "insect"}, {"legs", "thin"}, {"antennae", "feelers"}, {"abdomen", "round"}, {"tail", "none"}}, {{"body", kBeetle}, {"head", kBeetle}, {"abdomen", kBeetle}, {"legs", kBlack}, {"wings", kBeetle}});
    b.St("ant", "Ant", "Three body segments and long thin legs.", {{"plan", 3}, {"bodyLength", 0.4}, {"bodyHeight", 0.16}, {"legLength", 0.35}, {"legThickness", 0.012}, {"headSize", 0.14}, {"wingSpan", 0.3}},
         {{"body", "long"}, {"head", "mandible"}, {"wings", "none"}, {"legs", "thin"}, {"antennae", "feelers"}, {"abdomen", "segmented"}, {"tail", "none"}}, {{"body", kSpiderC}, {"head", kSpiderC}, {"abdomen", kSpiderC}, {"legs", kSpiderC}});
    b.St("spider", "Spider", "Eight legs and a big round abdomen.", {{"plan", 4}, {"bodyLength", 0.4}, {"bodyHeight", 0.25}, {"legLength", 0.4}, {"legThickness", 0.015}, {"headSize", 0.16}},
         {{"body", "oval"}, {"head", "round"}, {"wings", "none"}, {"legs", "thin"}, {"antennae", "none"}, {"abdomen", "round"}, {"tail", "none"}, {"eyes", "big"}}, {{"body", kSpiderC}, {"head", kSpiderC}, {"abdomen", kBrown}, {"legs", kSpiderC}});
    b.t.generate = GenerateCreature;
    return b.t;
}

} // namespace

void RegisterCharacterTemplates() {
    Registry& r = Registry::Instance();
    r.Register(MakeHuman());
    r.Register(MakeRobot());
    r.Register(MakeCreature());
}

} // namespace okay::modeling::guided
