// Vehicle templates for guided modeling: car, truck, bus, motorcycle, trailer. Every vehicle is a set of named parts
// (body, cabin, glass, wheels, lights, bumpers, mirrors...) that can each be given another design, resized or moved by the
// settings. Written from general parametric modeling (boxes, side-view profiles pushed across the width, cylinders);
// no code, models or assets from other projects. Forward is +x, up is +y, the vehicle is centred on z = 0 and stands on y = 0.
// Parts are closed solids that never share a corner position, so the rebuild rules in Guided.hpp can tell them apart.
#include "okay/Modeling/Guided.hpp"
#include <algorithm>
#include <cmath>

namespace okay::modeling::guided {
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
// A cylinder along x (a round light, an exhaust, a tanker): from x0 to x1, centre (y, z), diameters dy, dz.
PolyMesh CylX(float x0, float x1, float y, float z, float dy, float dz, int sides = 14) {
    Frame f;
    f.origin = {x0, y, z};
    f.t = {0, 0, 1}; f.u = {1, 0, 0}; f.n = {0, 1, 0};   // t x u = n
    f.sx = dz; f.sy = x1 - x0; f.sz = dy;
    return shapes::Prism(f, sides);
}
// A cylinder along z (an axle, a wheel): from z0 to z1, centre (x, y), diameter d.
PolyMesh CylZ(float z0, float z1, float x, float y, float d, int sides = 16) {
    Frame f;
    f.origin = {x, y, z0};
    f.t = {0, 1, 0}; f.u = {0, 0, 1}; f.n = {1, 0, 0};
    f.sx = d; f.sy = z1 - z0; f.sz = d;
    return shapes::Prism(f, sides);
}
// A side-view profile (x, y points, counter-clockwise) pushed across the width from z0 to z1.
PolyMesh Side(const std::vector<Vec2>& prof, float z0, float z1) {
    return shapes::ExtrudeAxes({0, 0, (z0 + z1) * 0.5f}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, prof, z1 - z0);
}
// A thin slab from point a to point b (a window along a slope), `across` wide, `thick` thick, pushed `out` along `outward`.
PolyMesh Slab(Vec3 a, Vec3 b, float across, float thick, Vec3 outward) {
    const Vec3 d = b - a;
    const float len = d.Magnitude();
    if (len < 1e-3f) return PolyMesh();
    Frame f;
    f.u = d / len;
    f.t = {0, 0, 1};
    f.n = Vec3::Cross(f.t, f.u);
    if (Vec3::Dot(f.n, outward) < 0) outward = -outward;
    f.origin = a + outward.Normalized() * thick * 0.35f;
    f.sx = across; f.sy = len; f.sz = thick;
    return shapes::Box(f);
}
void Move(PolyMesh& m, Vec3 d) { for (Vec3& v : m.verts) v = v + d; }

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
    // Add a part whose design is looked up; `frame` is where a user part would be fitted.
    void Add(const std::string& id, const std::string& slot, const Frame& frame, const std::function<PolyMesh(const std::string&)>& build) {
        const std::string d = r.DesignFor(slot, id);
        if (d == "none") return;
        PolyMesh m = FillUser(r, d, frame, [&] { return build(d); });
        parts.push_back({id, slot, std::move(m)});
    }
};

// ---- Wheels ----------------------------------------------------------------------------------------------------
// A wheel centred at (x, y = radius) on the side z (the wheel spans z0..z1).
PolyMesh WheelMesh(const std::string& design, float x, float y, float radius, float z0, float z1) {
    PolyMesh m = CylZ(z0, z1, x, y, radius * 2, 18);   // the tyre
    const float zc = (z0 + z1) * 0.5f, w = z1 - z0;
    const bool left = zc < 0;
    const float zOut = left ? z0 : z1, sgn = left ? -1.0f : 1.0f;
    auto hubAt = [&](float rr, float depth) {   // a disc proud of the outer face
        const float a = zOut - sgn * depth * 0.4f, b = zOut + sgn * depth * 0.6f;
        return CylZ(std::min(a, b), std::max(a, b), x, y, rr * 2, 12);
    };
    if (design == "dish") {
        shapes::Append(m, hubAt(radius * 0.86f, 0.03f));
        shapes::Append(m, hubAt(radius * 0.3f, 0.08f));
    } else if (design == "mesh") {
        shapes::Append(m, hubAt(radius * 0.2f, 0.06f));
        for (int k = 0; k < 8; ++k) {
            const float a = 2 * kPi * (float)k / 8.0f + 0.2f;
            Frame f;
            f.t = {std::cos(a), std::sin(a), 0}; f.u = {-std::sin(a), std::cos(a), 0}; f.n = {0, 0, 1};
            f.origin = {x + std::cos(a) * radius * 0.2f, y + std::sin(a) * radius * 0.2f, zOut + sgn * 0.002f};
            f.origin = f.origin - f.u * (radius * 0.02f);
            f.sx = radius * 0.6f; f.sy = radius * 0.04f; f.sz = std::min(w * 0.25f, 0.03f);
            shapes::Append(m, shapes::Box(f));
        }
    } else if (design == "rim") {
        shapes::Append(m, hubAt(radius * 0.62f, 0.05f));
    } else if (design == "steel") {
        shapes::Append(m, hubAt(radius * 0.7f, 0.04f));
        shapes::Append(m, hubAt(radius * 0.22f, 0.09f));
    } else if (design == "spoke") {
        shapes::Append(m, hubAt(radius * 0.18f, 0.07f));
        for (int k = 0; k < 5; ++k) {
            const float a = 2 * kPi * (float)k / 5.0f;
            const Vec3 c{x + std::cos(a) * radius * 0.42f, y + std::sin(a) * radius * 0.42f, 0};
            Frame f;
            f.t = {std::cos(a), std::sin(a), 0}; f.u = {-std::sin(a), std::cos(a), 0}; f.n = {0, 0, 1};
            f.origin = {c.x, c.y, zOut + sgn * 0.003f};
            f.origin = f.origin - f.u * (radius * 0.04f);
            f.sx = radius * 0.62f; f.sy = radius * 0.08f; f.sz = std::min(w * 0.3f, 0.05f);
            shapes::Append(m, shapes::Box(f));
        }
    }
    if (shapes::Detail() >= 2) {   // tyre tread: blocks around the outside of the tyre (High and up)
        const int nt = shapes::Detail() == 2 ? 24 : 40;
        for (int k = 0; k < nt; ++k) {
            const float a = 2 * kPi * (float)k / (float)nt;
            Frame f;
            f.t = {-std::sin(a), std::cos(a), 0}; f.u = {std::cos(a), std::sin(a), 0}; f.n = {0, 0, -1};
            f.origin = {x + std::cos(a) * (radius - 0.006f), y + std::sin(a) * (radius - 0.006f), zc - w * 0.4f * 0.0f + 0.0f};
            f.origin.z = zc + w * 0.4f;
            f.sx = radius * 2 * kPi / (float)nt * 0.55f; f.sy = std::min(0.03f, radius * 0.1f); f.sz = w * 0.8f;
            shapes::Append(m, shapes::Box(f));
        }
    }
    if (shapes::Detail() >= 2 && design != "plain") {   // lug nuts on the hub (High and up)
        const int n = shapes::Detail() == 2 ? 5 : 8;
        for (int k = 0; k < n; ++k) {
            const float a = 2 * kPi * (float)k / (float)n + 0.1f;
            const float c0 = zOut + sgn * 0.045f, c1 = zOut + sgn * 0.075f;
            m.verts.reserve(m.verts.size() + 8);
            shapes::Append(m, CylZ(std::min(c0, c1), std::max(c0, c1), x + std::cos(a) * radius * 0.12f, y + std::sin(a) * radius * 0.12f, radius * 0.045f, 6));
        }
    }
    return m;
}

void Wheel(Out& o, const std::string& id, float x, float zCentre, float radius, float width) {
    x += 0.0071f * (float)std::atoi(id.c_str() + std::min<size_t>(6, id.size()));   // wheels of one vehicle never share a corner
    const float z0 = zCentre - width * 0.5f, z1 = zCentre + width * 0.5f;
    Frame f;
    f.origin = {x, radius, z0};
    f.t = {0, 1, 0}; f.u = {0, 0, 1}; f.n = {1, 0, 0};
    f.sx = radius * 2; f.sy = width; f.sz = radius * 2;
    f.origin = {x, radius, z0};
    o.Add(id, "wheel", f, [&](const std::string& d) { return WheelMesh(d, x, radius, radius, z0, z1); });
}

// ---- Lights, bumpers, mirrors --------------------------------------------------------------------------------
void Headlight(Out& o, const std::string& id, float x, float y, float z, float size) {
    Frame f = FrameAA({x - 0.03f, y - size * 0.5f, z - size * 0.5f}, {x + 0.03f, y + size * 0.5f, z + size * 0.5f});
    o.Add(id, "headlight", f, [&](const std::string& d) {
        if (d == "square") return BoxAA({x - 0.03f, y - size * 0.4f, z - size * 0.55f}, {x + 0.05f, y + size * 0.4f, z + size * 0.55f});
        if (d == "slim") return BoxAA({x - 0.03f, y - size * 0.18f, z - size * 1.1f}, {x + 0.04f, y + size * 0.18f, z + size * 1.1f});
        if (d == "twin") { PolyMesh m = CylX(x - 0.03f, x + 0.04f, y, z - size * 0.4f, size * 0.6f, size * 0.6f, 12); shapes::Append(m, CylX(x - 0.03f, x + 0.04f, y, z + size * 0.4f, size * 0.6f, size * 0.6f, 12)); return m; }
        return CylX(x - 0.03f, x + 0.04f, y, z, size, size, 14);
    });
}
void Taillight(Out& o, const std::string& id, float x, float y, float z, float size) {
    Frame f = FrameAA({x - 0.04f, y - size * 0.5f, z - size * 0.5f}, {x + 0.03f, y + size * 0.5f, z + size * 0.5f});
    o.Add(id, "taillight", f, [&](const std::string& d) {
        if (d == "strip") return BoxAA({x - 0.04f, y - size * 0.15f, z - size * 1.3f}, {x + 0.03f, y + size * 0.15f, z + size * 1.3f});
        if (d == "round") return CylX(x - 0.04f, x + 0.03f, y, z, size, size, 14);
        return BoxAA({x - 0.04f, y - size * 0.4f, z - size * 0.6f}, {x + 0.03f, y + size * 0.4f, z + size * 0.6f});
    });
}
void Bumper(Out& o, const std::string& id, bool front, float xFace, float y0, float width, float h) {
    const float s = front ? 1.0f : -1.0f;
    const float xa = xFace - s * 0.2f, xb = xFace + s * 0.13f;   // reaches back into the (smoothed) nose so it is joined
    Frame f = FrameAA({std::min(xa, xb), y0, -width / 2}, {std::max(xa, xb), y0 + h, width / 2});
    o.Add(id, "bumper", f, [&](const std::string& d) {
        if (d == "chrome") return CylZ(-width / 2 - 0.02f, width / 2 + 0.02f, xFace + s * 0.02f, y0 + h * 0.5f, h * 0.55f, 10);
        if (d == "sport") { PolyMesh m = BoxAA({std::min(xa, xb), y0, -width / 2}, {std::max(xa, xb) - s * 0.03f, y0 + h * 0.7f, width / 2}); shapes::Append(m, BoxAA({std::min(xa, xb + s * 0.1f), y0 - h * 0.15f, -width / 2 * 0.8f}, {std::max(xa, xb + s * 0.1f), y0 + h * 0.2f, width / 2 * 0.8f})); return m; }
        if (d == "heavy") return BoxAA({std::min(xa, xb + s * 0.07f), y0 - h * 0.2f, -width / 2 - 0.03f}, {std::max(xa, xb + s * 0.07f), y0 + h * 1.4f, width / 2 + 0.03f});
        return BoxAA({std::min(xa, xb), y0, -width / 2}, {std::max(xa, xb), y0 + h, width / 2});
    });
}
void Mirror(Out& o, const std::string& id, float x, float y, float zOuter, int side, float zIn = 1e9f) {   // zIn: where the arm starts (the cabin side)
    const float s = (float)side;
    if (zIn > 1e8f) zIn = zOuter - s * 0.02f;
    Frame f = FrameAA({x - 0.07f, y, std::min(zOuter, zOuter + s * 0.2f)}, {x + 0.07f, y + 0.14f, std::max(zOuter, zOuter + s * 0.2f)});
    o.Add(id, "mirror", f, [&](const std::string& d) {
        PolyMesh m = BoxAA({x - 0.02f, y - 0.01f, std::min(zIn, zOuter + s * 0.1f)}, {x + 0.02f, y + 0.03f, std::max(zIn, zOuter + s * 0.1f)});   // arm
        const float z0 = zOuter + s * 0.08f, z1 = zOuter + s * 0.2f;
        if (d == "round") {
            Frame b = FrameAA({x - 0.08f, y, std::min(z0, z1)}, {x + 0.08f, y + 0.16f, std::max(z0, z1)});
            shapes::Append(m, shapes::Ball(b, 4, 8));
        } else shapes::Append(m, BoxAA({x - 0.07f, y, std::min(z0, z1)}, {x + 0.07f, y + 0.14f, std::max(z0, z1)}));
        return m;
    });
}
void Grille(Out& o, const std::string& id, float x, float y0, float y1, float width) {
    Frame f = FrameAA({x - 0.02f, y0, -width / 2}, {x + 0.05f, y1, width / 2});
    o.Add(id, "grille", f, [&](const std::string& d) {
        if (d == "slats") {
            PolyMesh m;
            const int n = 4;
            for (int k = 0; k < n; ++k) {
                const float yy = y0 + (y1 - y0) * ((float)k + 0.3f) / (float)n;
                shapes::Append(m, BoxAA({x - 0.02f, yy, -width / 2}, {x + 0.05f, yy + (y1 - y0) / (float)n * 0.4f, width / 2}));
            }
            return m;
        }
        return BoxAA({x - 0.02f, y0, -width / 2}, {x + 0.04f, y1, width / 2});
    });
}

// ---- Car -------------------------------------------------------------------------------------------------------
// A car body style: the side silhouette as control points, as fractions along the length (0 tail, 1 nose) and of the height above
// the ground clearance. Each style has its own hood height, belt line, trunk deck, cabin position and roof shape.
struct CarStyle { float nose, belt, deck, cabRear, cabFront, roofRear, roofFront; bool bed; };
CarStyle StyleOf(const std::string& d) {
    //                   nose  belt  deck  cabR  cabF  roofR roofF
    if (d == "hatchback") return {0.34f, 0.50f, 0.50f, 0.06f, 0.72f, 0.12f, 0.62f, false};
    if (d == "coupe")     return {0.25f, 0.40f, 0.42f, 0.22f, 0.64f, 0.40f, 0.52f, false};
    if (d == "suv")       return {0.42f, 0.56f, 0.56f, 0.04f, 0.76f, 0.06f, 0.66f, false};
    if (d == "pickup")    return {0.40f, 0.56f, 0.34f, 0.44f, 0.76f, 0.46f, 0.66f, true};
    if (d == "sports")    return {0.22f, 0.34f, 0.38f, 0.30f, 0.66f, 0.40f, 0.50f, false};
    if (d == "van")       return {0.48f, 0.62f, 0.62f, 0.02f, 0.86f, 0.03f, 0.80f, false};
    return {0.30f, 0.46f, 0.44f, 0.17f, 0.70f, 0.30f, 0.56f, false};   // sedan
}
using Poly = std::vector<Vec2>;
// Corner cutting (Chaikin): each pass replaces every corner by two points a quarter in, so a polyline becomes a smooth spline.
Poly Chaikin(const Poly& p, int iters, bool closed) {
    Poly q = p;
    for (int it = 0; it < iters; ++it) {
        Poly n;
        const int m = (int)q.size();
        if (!closed) n.push_back(q.front());
        for (int i = 0; i < (closed ? m : m - 1); ++i) {
            const Vec2 a = q[i], b = q[(i + 1) % m];
            if (!closed && i == 0) { n.push_back({a.x * 0.5f + b.x * 0.5f, a.y * 0.5f + b.y * 0.5f}); continue; }
            n.push_back({a.x * 0.75f + b.x * 0.25f, a.y * 0.75f + b.y * 0.25f});
            if (!closed && i == m - 2) { n.push_back(b); continue; }
            n.push_back({a.x * 0.25f + b.x * 0.75f, a.y * 0.25f + b.y * 0.75f});
        }
        q = n;
    }
    return q;
}

void GenerateCar(const Recipe& r, std::vector<GeneratedPart>& parts) {
    Out o{r, parts};
    const float L = Pf(r, "length"), W = Pf(r, "width"), H = Pf(r, "height"), gc = Pf(r, "groundClear");
    const float wr = std::min(Pf(r, "wheelRadius"), L * 0.2f), ww = Pf(r, "wheelWidth");
    const float wheelbase = std::min(Pf(r, "wheelbase"), L * 0.8f), shift = Pf(r, "axleShift") * L;
    const std::string bodyD = r.DesignFor("body", "body/0");
    const bool userBody = bodyD.rfind("user:", 0) == 0;
    const CarStyle st = StyleOf(userBody ? "sedan" : bodyD);
    const float cabinScale = Pf(r, "cabinScale"), cabinShift = Pf(r, "cabinShift");
    const float Wb = W * 0.9f, Wc = Wb * 0.86f, x0 = -L / 2;
    const int smooth = std::min(3, (int)std::lround(shapes::DetailF()));   // corner-cutting passes: 0 blocky, 1 rounded, 2 smooth, 3 finest
    auto X = [&](float f) { return x0 + f * L; };
    const float span = H - gc;
    const float beltY = std::max(gc + st.belt * span, gc + wr * 2.25f + 0.06f);   // high enough that the wheel arch stays inside the body
    auto Y = [&](float f) { return std::max(gc + f * span, gc + 0.05f); };
    const float noseY = std::min(Y(st.nose), beltY), deckY = std::min(Y(st.deck), beltY);
    const float roof = std::max(H, beltY + 0.3f);   // a cabin always has some height, even with tall wheels or a low roof setting
    // cabin key x (shifted and scaled about its middle)
    const float cmid = 0.5f * (st.cabRear + st.cabFront);
    auto cx = [&](float f) { return X(std::min(std::max(cmid + (f - cmid) * cabinScale + cabinShift, 0.02f), 0.97f)); };
    const float bx0 = cx(st.cabRear), bx1 = cx(st.cabFront), tx0 = cx(st.roofRear), tx1 = cx(st.roofFront);
    const float mid = 0.5f * (bx0 + bx1);
    // axles (the arches and the wheels are centred on them)
    const float arch = wr * 1.16f;
    float xf = wheelbase / 2 + shift, xrr = -wheelbase / 2 + shift;
    {   // keep the two arches apart and inside the body, whatever the sliders say
        if (xf - xrr < 2 * arch + 0.1f) { const float m = 0.5f * (xf + xrr); xf = m + arch + 0.05f; xrr = m - arch - 0.05f; }
        if (xf + arch > L / 2 - 0.05f) { const float d = xf + arch - (L / 2 - 0.05f); xf -= d; xrr -= d; }
        if (xrr - arch < x0 + 0.05f) { const float d = (x0 + 0.05f) - (xrr - arch); xf += d; xrr += d; }
    }

    // lower body: tail to nose over the top, then back along the bottom with a round arch cut at each axle
    {
        Poly top;   // from the tail bottom up and over to the nose bottom
        top.push_back({x0, gc});
        top.push_back({x0, deckY});
        if (st.bed) { top.push_back({bx0, deckY}); top.push_back({bx0, beltY}); }
        else { top.push_back({std::min(bx0, X(0.12f) + 0.0f), deckY}); top.push_back({bx0, beltY}); }
        top.push_back({bx1, beltY});
        top.push_back({L / 2, noseY});
        top.push_back({L / 2, gc});
        Poly sm = top;
        if (smooth > 0) {
            // smooth only the top outline, keep the bottom corners square so the body still sits flat
            Poly mid3(top.begin() + 1, top.end() - 1);
            mid3 = Chaikin(mid3, smooth, false);
            sm.clear(); sm.push_back(top.front()); for (const Vec2& v : mid3) sm.push_back(v); sm.push_back(top.back());
        }
        Poly bottom;   // nose to tail, with arches
        std::vector<float> ax = {xf, xrr};
        std::sort(ax.begin(), ax.end(), std::greater<float>());
        for (float a : ax) {
            bottom.push_back({a + arch, gc});
            bottom.push_back({a + arch, wr});
            const int seg = shapes::Scale(8, 4);
            for (int k = 1; k < seg; ++k) { const float t = kPi * (float)k / (float)seg; bottom.push_back({a + std::cos(t) * arch, wr + std::sin(t) * arch}); }
            bottom.push_back({a - arch, wr});
            bottom.push_back({a - arch, gc});
        }
        Poly full = sm;
        for (const Vec2& v : bottom) full.push_back(v);
        std::reverse(full.begin(), full.end());   // counter-clockwise: along the bottom first
        Frame fr = userBody ? FrameAA({x0, gc, -Wb / 2}, {L / 2, H, Wb / 2}) : FrameAA({x0, gc, -Wb / 2}, {L / 2, beltY, Wb / 2});
        o.Add("body/0", "body", fr, [&](const std::string&) { return Side(full, -Wb / 2, Wb / 2); });
    }
    if (!userBody) {
        // cabin: the greenhouse, a rounded roof line when smoothed
        Poly cab = {{bx0, beltY - 0.02f}, {bx1, beltY - 0.02f}, {tx1, roof}, {tx0, roof}};
        if (smooth > 0) cab = Chaikin(cab, smooth, true);
        Frame fc = FrameAA({bx0, beltY, -Wc / 2}, {bx1, roof, Wc / 2});
        o.Add("cabin/0", "cabin", fc, [&](const std::string&) {
            PolyMesh m = Side(cab, -Wc / 2, Wc / 2);
            if (shapes::Detail() >= 2) {   // door seams and handles on both sides (High and up)
                const float xm = (bx0 + bx1) * 0.5f;
                for (int s = -1; s <= 1; s += 2) {
                    const float zo = s * Wc / 2;
                    const float za = std::min(zo - s * 0.002f, zo + s * 0.006f), zb = std::max(zo - s * 0.002f, zo + s * 0.006f);
                    shapes::Append(m, BoxAA({xm - 0.004f, beltY + 0.02f, za}, {xm + 0.004f, beltY + (roof - beltY) * 0.85f, zb}));
                    shapes::Append(m, BoxAA({bx0 + (bx1 - bx0) * 0.2f, beltY + 0.06f, std::min(zo - s * 0.002f, zo + s * 0.02f)}, {bx0 + (bx1 - bx0) * 0.2f + 0.1f, beltY + 0.09f, std::max(zo - s * 0.002f, zo + s * 0.02f)}));
                    shapes::Append(m, BoxAA({xm + 0.15f, beltY + 0.06f, std::min(zo - s * 0.002f, zo + s * 0.02f)}, {xm + 0.25f, beltY + 0.09f, std::max(zo - s * 0.002f, zo + s * 0.02f)}));
                }
            }
            return m;
        });
        // side glass: the cabin side shrunk inwards, leaving the pillars
        const float yb = beltY + 0.07f, yt = roof - 0.08f;
        auto xAt = [&](float y, bool front) {
            const float t = (y - (beltY - 0.02f)) / std::max(roof - (beltY - 0.02f), 1e-3f);
            return front ? bx1 + (tx1 - bx1) * t : bx0 + (tx0 - bx0) * t;
        };
        auto inset = [&](float y, bool front) { const float xa = xAt(y, false), xb = xAt(y, true); const float ins = std::min(0.12f, (xb - xa) * 0.2f); return front ? xb - ins : xa + ins; };
        Poly gl = {{inset(yb, false), yb}, {inset(yb, true), yb}, {inset(yt, true), yt}, {inset(yt, false), yt}};
        if (smooth > 0) gl = Chaikin(gl, std::min(smooth, 2), true);
        for (int side = 0; side < 2; ++side) {
            const float zs = side ? Wc / 2 : -Wc / 2, sg = side ? 1.0f : -1.0f;
            const float za = zs - sg * 0.012f, zb = zs + sg * 0.01f;
            Frame f2 = FrameAA({bx0, yb, std::min(za, zb)}, {bx1, yt, std::max(za, zb)});
            o.Add("glass/side/" + std::to_string(side), "glass", f2, [&](const std::string&) { return Side(gl, std::min(za, zb), std::max(za, zb)); });
        }
        const Vec3 centre{mid, (beltY + roof) * 0.5f, 0};
        auto outwardOf = [&](Vec3 a, Vec3 b) {
            Vec3 d = b - a; d = d / d.Magnitude();
            Vec3 n{-d.y, d.x, 0};
            const Vec3 m = (a + b) * 0.5f;
            return Vec3::Dot(n, m - centre) >= 0 ? n : -n;
        };
        {   // windscreen
            const Vec3 a{bx1 - 0.02f, beltY + 0.04f, 0}, b{tx1 - 0.02f, roof - 0.05f, 0};
            Frame f3 = FrameAA({std::min(tx1, bx1) - 0.1f, beltY, -Wc * 0.44f}, {std::max(tx1, bx1) + 0.1f, roof, Wc * 0.44f});
            o.Add("glass/front", "glass", f3, [&](const std::string&) { return Slab(a, b, Wc * 0.88f, 0.02f, outwardOf(a, b)); });
        }
        {   // rear glass
            const Vec3 a{bx0 + 0.02f, beltY + 0.04f, 0}, b{tx0 + 0.02f, roof - 0.05f, 0};
            Frame f3 = FrameAA({std::min(tx0, bx0) - 0.1f, beltY, -Wc * 0.44f}, {std::max(tx0, bx0) + 0.1f, roof, Wc * 0.44f});
            o.Add("glass/rear", "glass", f3, [&](const std::string&) { return Slab(a, b, Wc * 0.88f, 0.02f, outwardOf(a, b)); });
        }
        for (int side = 0; side < 2; ++side)
            Mirror(o, "mirror/" + std::to_string(side), bx1 - 0.15f, beltY + 0.08f, (side ? 1.0f : -1.0f) * Wb / 2, side ? 1 : -1, (side ? 1.0f : -1.0f) * (Wc / 2 - 0.03f));
    }
    if (st.bed && !userBody) {   // pickup bed: floor and three walls behind the cabin
        const float bxe = bx0 - 0.1f, xe = x0 + 0.02f, wt = 0.06f, topW = beltY + 0.26f, floorY = deckY - 0.02f;
        PolyMesh bedm;
        shapes::Append(bedm, BoxAA({xe, floorY, -Wb / 2 + 0.05f}, {bxe, floorY + 0.08f, Wb / 2 - 0.05f}));
        shapes::Append(bedm, BoxAA({xe, floorY + 0.07f, -Wb / 2 + 0.01f}, {bxe, topW, -Wb / 2 + 0.01f + wt}));
        shapes::Append(bedm, BoxAA({xe, floorY + 0.07f, Wb / 2 - 0.01f - wt}, {bxe, topW, Wb / 2 - 0.01f}));
        shapes::Append(bedm, BoxAA({xe, floorY + 0.07f, -Wb / 2 + 0.08f}, {xe + wt, topW, Wb / 2 - 0.08f}));
        Frame fb = FrameAA({xe, floorY, -Wb / 2}, {bxe, topW, Wb / 2});
        o.Add("bed/0", "bed", fb, [&](const std::string&) { return bedm; });
    }
    // axles join each pair of wheels through the body, so no wheel floats
    for (int a = 0; a < 2; ++a) {
        const float ax = (a ? xrr : xf) + 0.003f * (float)a;
        const float tr2 = (W - ww) * 0.5f;
        Frame fa = FrameAA({ax - 0.04f, wr - 0.04f, -tr2}, {ax + 0.04f, wr + 0.04f, tr2});
        o.Add("axle/" + std::to_string(a), "axle", fa, [&](const std::string&) { return BoxAA({ax - 0.04f, wr - 0.04f, -tr2}, {ax + 0.04f, wr + 0.04f, tr2}); });
    }
    // wheels, centred in the arches
    const float track = W - ww;
    int wi = 0;
    for (float x : {xf, xrr})
        for (int side = 0; side < 2; ++side) Wheel(o, "wheel/" + std::to_string(wi++), x, (side ? 1.0f : -1.0f) * track / 2, wr, ww);
    // lights, bumpers, grille on the nose and tail
    const float ly = gc + (noseY - gc) * 0.62f + 0.05f;
    Headlight(o, "headlight/0", L / 2, ly, -Wb * 0.33f, Pf(r, "lightSize"));
    Headlight(o, "headlight/1", L / 2, ly, Wb * 0.33f, Pf(r, "lightSize"));
    Taillight(o, "taillight/0", x0, deckY - 0.1f, -Wb * 0.36f, Pf(r, "lightSize") * 0.8f);
    Taillight(o, "taillight/1", x0, deckY - 0.1f, Wb * 0.36f, Pf(r, "lightSize") * 0.8f);
    Bumper(o, "bumper/0", true, L / 2, gc + 0.02f, Wb, 0.22f);
    Bumper(o, "bumper/1", false, x0, gc + 0.02f, Wb, 0.22f);
    Grille(o, "grille/0", L / 2, std::max(gc + 0.12f, ly - 0.1f), noseY - 0.02f, Wb * 0.34f);
    // spoiler on the tail, roof fitting, tail pipes
    {
        const float sx0 = x0 - 0.02f, sx1 = x0 + 0.3f, sy = deckY;
        Frame f = FrameAA({sx0, sy, -Wb * 0.4f}, {sx1, sy + 0.3f, Wb * 0.4f});
        o.Add("spoiler/0", "spoiler", f, [&](const std::string& d) {
            if (d == "wing") {
                PolyMesh m = BoxAA({sx0, sy + 0.26f, -Wb * 0.42f}, {sx1, sy + 0.32f, Wb * 0.42f});
                for (int s = 0; s < 2; ++s) shapes::Append(m, BoxAA({sx0 + 0.1f, sy - 0.02f, (s ? 0.3f : -0.34f) * Wb * 0.5f}, {sx0 + 0.18f, sy + 0.27f, (s ? 0.34f : -0.3f) * Wb * 0.5f}));
                return m;
            }
            return BoxAA({sx0, sy - 0.01f, -Wb * 0.4f}, {sx1, sy + 0.07f, Wb * 0.4f});
        });
        const float rx = mid, rw = std::min(Wc * 0.4f, 0.5f);
        Frame fr = FrameAA({rx - 0.2f, H - 0.01f, -rw}, {rx + 0.2f, H + 0.14f, rw});
        o.Add("roof/0", "roof", fr, [&](const std::string& d) {
            if (d == "sign") return BoxAA({rx - 0.2f, H - 0.01f, -0.16f}, {rx + 0.2f, H + 0.14f, 0.16f});
            if (d == "rack") {
                PolyMesh m;
                for (int s = 0; s < 2; ++s) shapes::Append(m, BoxAA({rx - 0.5f, H - 0.01f, (s ? 0.3f : -0.34f) * Wc}, {rx + 0.5f, H + 0.06f, (s ? 0.34f : -0.3f) * Wc}));
                for (int k = 0; k < 3; ++k) shapes::Append(m, BoxAA({rx - 0.45f + 0.45f * (float)k, H + 0.05f, -0.34f * Wc}, {rx - 0.4f + 0.45f * (float)k, H + 0.1f, 0.34f * Wc}));
                return m;
            }
            return BoxAA({rx - 0.2f, H - 0.01f, -rw}, {rx + 0.2f, H + 0.14f, rw});   // light bar
        });
        Frame fx = FrameAA({x0 - 0.1f, gc + 0.03f, Wb * 0.24f}, {x0 + 0.3f, gc + 0.13f, Wb * 0.32f});
        o.Add("exhaust/0", "exhaust", fx, [&](const std::string& d) {
            PolyMesh m = CylX(x0 - 0.1f, x0 + 0.3f, gc + 0.08f, Wb * 0.28f, 0.1f, 0.1f, 10);
            if (d == "dual") shapes::Append(m, CylX(x0 - 0.1f, x0 + 0.3f, gc + 0.08f, -Wb * 0.28f, 0.1f, 0.1f, 10));
            return m;
        });
    }
}

// ---- Truck -------------------------------------------------------------------------------------------------------
void GenerateTruck(const Recipe& r, std::vector<GeneratedPart>& parts) {
    Out o{r, parts};
    const float L = Pf(r, "length"), W = Pf(r, "width"), cabH = Pf(r, "cabHeight"), clear = Pf(r, "chassisHeight");
    const float wr = Pf(r, "wheelRadius"), ww = Pf(r, "wheelWidth");
    const int axles = std::max(1, (int)r.Param("rearAxles"));
    const float cabLen = Pf(r, "cabLength"), cargoH = Pf(r, "cargoHeight");
    const std::string cab = r.DesignFor("cab", "cab/0"), cargo = r.DesignFor("cargo", "cargo/0");
    const float x0 = -L / 2, xc = L / 2 - cabLen;   // cab occupies xc..L/2
    const float Wb = W * 0.92f;
    // chassis rails and deck
    Frame fc = FrameAA({x0, clear - 0.25f, -Wb / 2 * 0.85f}, {L / 2 - 0.1f, clear, Wb / 2 * 0.85f});
    o.Add("chassis/0", "chassis", fc, [&](const std::string&) { return BoxAA({x0, clear - 0.25f, -Wb / 2 * 0.85f}, {L / 2 - 0.1f, clear, Wb / 2 * 0.85f}); });
    // cab
    const float cy0 = clear + 0.02f;
    Frame fcab = FrameAA({xc, cy0, -Wb / 2}, {L / 2, cy0 + cabH, Wb / 2});
    o.Add("cab/0", "cab", fcab, [&](const std::string&) {
        if (cab == "conventional") {
            PolyMesh m = BoxAA({xc, cy0, -Wb / 2}, {xc + cabLen * 0.55f, cy0 + cabH, Wb / 2});               // cab box
            shapes::Append(m, BoxAA({xc + cabLen * 0.55f - 0.01f, cy0 + 0.05f, -Wb / 2 + 0.1f}, {L / 2, cy0 + cabH * 0.62f, Wb / 2 - 0.1f}));   // hood
            return m;
        }
        return Side({{xc, cy0}, {L / 2, cy0}, {L / 2, cy0 + cabH * 0.8f}, {L / 2 - 0.3f, cy0 + cabH}, {xc, cy0 + cabH}}, -Wb / 2, Wb / 2);   // cab over engine
    });
    const float cabBoxFront = cab == "conventional" ? xc + cabLen * 0.55f : L / 2 - 0.3f;
    // glass
    for (int side = 0; side < 2; ++side) {
        const float zs = side ? Wb / 2 : -Wb / 2, sg = side ? 1.0f : -1.0f;
        const float za = zs - sg * 0.012f, zb = zs + sg * 0.012f;
        const float gx0 = xc + 0.25f, gx1 = cabBoxFront - 0.12f;
        Frame f = FrameAA({gx0, cy0 + cabH * 0.5f, std::min(za, zb)}, {gx1, cy0 + cabH * 0.88f, std::max(za, zb)});
        o.Add("glass/side/" + std::to_string(side), "glass", f, [&](const std::string&) { return BoxAA({gx0, cy0 + cabH * 0.5f, std::min(za, zb)}, {gx1, cy0 + cabH * 0.88f, std::max(za, zb)}); });
    }
    {
        const float gx = cabBoxFront;
        Frame f = FrameAA({gx - 0.012f, cy0 + cabH * 0.5f, -Wb * 0.4f}, {gx + 0.012f, cy0 + cabH * 0.9f, Wb * 0.4f});
        if (cab == "conventional")
            o.Add("glass/front", "glass", f, [&](const std::string&) { return BoxAA({gx - 0.012f, cy0 + cabH * 0.5f, -Wb * 0.4f}, {gx + 0.012f, cy0 + cabH * 0.9f, Wb * 0.4f}); });
        else
            o.Add("glass/front", "glass", f, [&](const std::string&) { return BoxAA({L / 2 - 0.012f, cy0 + cabH * 0.42f, -Wb * 0.4f}, {L / 2 + 0.012f, cy0 + cabH * 0.78f, Wb * 0.4f}); });
    }
    // cargo
    const float cx0 = x0 + 0.05f, cx1 = xc - 0.12f, cy = clear + 0.02f;
    Frame fcg = FrameAA({cx0, cy, -Wb / 2}, {cx1, cy + cargoH, Wb / 2});
    o.Add("cargo/0", "cargo", fcg, [&](const std::string& d) {
        if (d == "flatbed") {
            PolyMesh m = BoxAA({cx0, cy, -Wb / 2}, {cx1, cy + 0.12f, Wb / 2});
            for (int side = 0; side < 2; ++side) {
                const float z = side ? Wb / 2 - 0.03f : -Wb / 2 + 0.03f;
                shapes::Append(m, BoxAA({cx0 + 0.02f, cy + 0.1f, z - 0.03f}, {cx1 - 0.02f, cy + 0.45f, z + 0.03f}));
            }
            return m;
        }
        if (d == "tanker") return CylX(cx0, cx1, cy + cargoH * 0.5f, 0, cargoH, Wb, 20);
        if (d == "tipper") {   // an open bin tilted up at the front about its rear bottom edge
            const float h = std::min(cargoH, 1.6f), wt = 0.08f;
            PolyMesh m = BoxAA({cx0, cy, -Wb / 2}, {cx1, cy + wt, Wb / 2});
            shapes::Append(m, BoxAA({cx0, cy + wt - 0.01f, -Wb / 2}, {cx1, cy + h, -Wb / 2 + wt}));
            shapes::Append(m, BoxAA({cx0, cy + wt - 0.01f, Wb / 2 - wt}, {cx1, cy + h, Wb / 2}));
            shapes::Append(m, BoxAA({cx0, cy + wt - 0.01f, -Wb / 2 + wt - 0.004f}, {cx0 + wt, cy + h, Wb / 2 - wt + 0.004f}));
            shapes::Append(m, BoxAA({cx1 - wt, cy + wt - 0.01f, -Wb / 2 + wt - 0.004f}, {cx1, cy + h + 0.3f, Wb / 2 - wt + 0.004f}));
            const float ang = 14.0f * kPi / 180.0f, c = std::cos(ang), s = std::sin(ang);
            for (Vec3& v : m.verts) { const float dx = v.x - cx0, dy = v.y - cy; v.x = cx0 + dx * c - dy * s; v.y = cy + 0.05f + dx * s + dy * c; }
            return m;
        }
        return BoxAA({cx0, cy, -Wb / 2}, {cx1, cy + cargoH, Wb / 2});
    });
    // wheels: a front axle under the cab, then the rear axles
    const float track = W - ww;
    std::vector<float> axleX = {xc + cabLen * 0.35f};
    const float spacing = std::min(wr * 2 + 0.35f, (xc - x0 - 1.4f) / (float)axles);
    for (int a = 0; a < axles; ++a) axleX.push_back(x0 + 1.0f + (float)a * std::max(spacing, 0.1f));
    int wi = 0;
    for (float x : axleX)
        for (int side = 0; side < 2; ++side) Wheel(o, "wheel/" + std::to_string(wi++), x, (side ? 1.0f : -1.0f) * track / 2, wr, ww);
    const float ly = cy0 + cabH * 0.2f;
    Headlight(o, "headlight/0", L / 2, ly, -Wb * 0.36f, Pf(r, "lightSize"));
    Headlight(o, "headlight/1", L / 2, ly, Wb * 0.36f, Pf(r, "lightSize"));
    Taillight(o, "taillight/0", x0, clear - 0.1f, -Wb * 0.38f, Pf(r, "lightSize") * 0.8f);
    Taillight(o, "taillight/1", x0, clear - 0.1f, Wb * 0.38f, Pf(r, "lightSize") * 0.8f);
    Bumper(o, "bumper/0", true, L / 2, cy0 + 0.017f, Wb, 0.3f);
    Bumper(o, "bumper/1", false, x0, clear - 0.35f, Wb * 0.9f, 0.25f);
    Grille(o, "grille/0", L / 2, cy0 + 0.15f, cy0 + cabH * 0.55f, Wb * 0.5f);
    for (int side = 0; side < 2; ++side) Mirror(o, "mirror/" + std::to_string(side), cabBoxFront - 0.25f, cy0 + cabH * 0.55f, (side ? 1.0f : -1.0f) * Wb / 2, side ? 1 : -1);
    // exhaust stack on the outside of the cab
    Frame fe = FrameAA({xc + 0.2f, cy0, Wb / 2 + 0.02f}, {xc + 0.32f, cy0 + cabH * 1.05f, Wb / 2 + 0.14f});
    o.Add("exhaust/0", "exhaust", fe, [&](const std::string& d) {
        Frame p; p.origin = {xc + 0.26f, cy0, Wb / 2 + 0.08f}; p.t = {1, 0, 0}; p.u = {0, 1, 0}; p.n = {0, 0, 1}; p.sx = p.sz = 0.12f; p.sy = cabH * 1.05f;
        PolyMesh m = shapes::Prism(p, 10);
        if (d == "twin") { p.origin = {xc + 0.26f, cy0, -Wb / 2 - 0.08f}; shapes::Append(m, shapes::Prism(p, 10)); }
        return m;
    });
}

// ---- Bus -------------------------------------------------------------------------------------------------------
void GenerateBus(const Recipe& r, std::vector<GeneratedPart>& parts) {
    Out o{r, parts};
    const float L = Pf(r, "length"), W = Pf(r, "width"), H = Pf(r, "height"), gc = Pf(r, "groundClear");
    const float wr = Pf(r, "wheelRadius"), ww = Pf(r, "wheelWidth");
    const int nWin = std::max(1, (int)r.Param("windows"));
    const std::string body = r.DesignFor("body", "body/0");
    const float x0 = -L / 2, x1 = L / 2;
    const float Wb = W * 0.94f;
    Frame fb = FrameAA({x0, gc, -Wb / 2}, {x1, H, Wb / 2});
    o.Add("body/0", "body", fb, [&](const std::string& d) {
        if (d == "coach") return Side({{x0, gc}, {x1, gc}, {x1, H * 0.78f}, {x1 - 0.5f, H}, {x0 + 0.3f, H}, {x0, H * 0.9f}}, -Wb / 2, Wb / 2);
        if (d == "school") return Side({{x0, gc}, {x1, gc}, {x1, H * 0.52f}, {x1 - 0.9f, H * 0.52f}, {x1 - 0.9f, H}, {x0, H}}, -Wb / 2, Wb / 2);
        if (d == "minibus") return Side({{x0, gc}, {x1, gc}, {x1, H * 0.55f}, {x1 - 0.7f, H}, {x0, H}}, -Wb / 2, Wb / 2);
        return Side({{x0, gc}, {x1, gc}, {x1, H - 0.05f}, {x1 - 0.1f, H}, {x0 + 0.1f, H}, {x0, H - 0.05f}}, -Wb / 2, Wb / 2);   // city
    });
    // window panes along each side
    const float wy0 = gc + (H - gc) * 0.45f, wy1 = H - 0.25f;
    const float span = L - 3.0f, pane = span / (float)nWin;
    for (int side = 0; side < 2; ++side)
        for (int k = 0; k < nWin; ++k) {
            const float zs = side ? Wb / 2 : -Wb / 2, sg = side ? 1.0f : -1.0f;
            const float za = zs - sg * 0.012f, zb = zs + sg * 0.012f;
            const float px0 = x0 + 1.0f + pane * (float)k + 0.06f, px1 = x0 + 1.0f + pane * (float)(k + 1) - 0.06f;
            const std::string id = "window/" + std::to_string(side) + "/" + std::to_string(k);
            Frame f = FrameAA({px0, wy0, std::min(za, zb)}, {px1, wy1, std::max(za, zb)});
            o.Add(id, "window", f, [&](const std::string&) { return BoxAA({px0, wy0, std::min(za, zb)}, {px1, wy1, std::max(za, zb)}); });
        }
    {
        const float wx = body == "school" ? x1 - 0.9f : x1 - 0.05f;
        Frame f = FrameAA({wx - 0.012f, wy0, -Wb * 0.42f}, {wx + 0.012f, wy1 + 0.1f, Wb * 0.42f});
        o.Add("windscreen/0", "window", f, [&](const std::string&) { return BoxAA({wx - 0.012f, wy0, -Wb * 0.42f}, {wx + 0.012f, wy1 + 0.1f, Wb * 0.42f}); });
    }
    // passenger door on the right, behind the front axle
    {
        const float dx0 = x1 - 3.2f, dx1 = dx0 + 1.1f;
        Frame f = FrameAA({dx0, gc + 0.15f, Wb / 2 - 0.01f}, {dx1, wy1, Wb / 2 + 0.03f});
        o.Add("door/0", "door", f, [&](const std::string& d) {
            PolyMesh m = BoxAA({dx0, gc + 0.15f, Wb / 2 - 0.01f}, {dx1, wy1, Wb / 2 + 0.03f});
            if (d == "folding") shapes::Append(m, BoxAA({(dx0 + dx1) / 2 - 0.02f, gc + 0.15f, Wb / 2 + 0.02f}, {(dx0 + dx1) / 2 + 0.02f, wy1, Wb / 2 + 0.05f}));
            return m;
        });
    }
    const float track = W - ww;
    const int rearAxles = std::max(1, (int)r.Param("rearAxles"));
    std::vector<float> ax = {x1 - 1.6f};
    for (int a = 0; a < rearAxles; ++a) ax.push_back(x0 + 1.6f + (float)a * (wr * 2 + 0.3f));
    int wi = 0;
    for (float x : ax)
        for (int side = 0; side < 2; ++side) Wheel(o, "wheel/" + std::to_string(wi++), x, (side ? 1.0f : -1.0f) * track / 2, wr, ww);
    const float ly = gc + (H - gc) * 0.22f;
    Headlight(o, "headlight/0", x1, ly, -Wb * 0.38f, Pf(r, "lightSize"));
    Headlight(o, "headlight/1", x1, ly, Wb * 0.38f, Pf(r, "lightSize"));
    Taillight(o, "taillight/0", x0, ly + 0.2f, -Wb * 0.4f, Pf(r, "lightSize") * 0.8f);
    Taillight(o, "taillight/1", x0, ly + 0.2f, Wb * 0.4f, Pf(r, "lightSize") * 0.8f);
    Bumper(o, "bumper/0", true, x1, gc + 0.02f, Wb, 0.25f);
    Bumper(o, "bumper/1", false, x0, gc + 0.02f, Wb, 0.25f);
    for (int side = 0; side < 2; ++side) Mirror(o, "mirror/" + std::to_string(side), x1 - 0.2f, wy0 + 0.2f, (side ? 1.0f : -1.0f) * Wb / 2, side ? 1 : -1);
    Frame fr = FrameAA({-0.8f, H, -0.6f}, {0.8f, H + 0.25f, 0.6f});
    o.Add("roof/0", "roof", fr, [&](const std::string&) { return BoxAA({-0.8f, H - 0.01f, -0.6f}, {0.8f, H + 0.25f, 0.6f}); });
}

// ---- Motorcycle ------------------------------------------------------------------------------------------------
void GenerateBike(const Recipe& r, std::vector<GeneratedPart>& parts) {
    Out o{r, parts};
    const float wb = Pf(r, "wheelbase"), fr = Pf(r, "frontWheelRadius"), rr = Pf(r, "rearWheelRadius"), ww = Pf(r, "wheelWidth");
    const float seatH = Pf(r, "seatHeight"), tankLen = Pf(r, "tankLength");
    const float xf = wb / 2, xr = -wb / 2;
    // wheels (a front and a rear; their radii differ)
    Wheel(o, "wheel/0", xf, 0, fr, ww);
    Wheel(o, "wheel/1", xr, 0, rr, ww);
    // fork and mudguards
    {
        Frame f = FrameAA({xf - 0.35f, fr, -0.1f}, {xf, seatH + 0.25f, 0.1f});
        o.Add("fork/0", "fork", f, [&](const std::string&) {
            PolyMesh m;
            for (int s = 0; s < 2; ++s) {
                const float z = s ? 0.09f : -0.09f;
                shapes::Append(m, Side({{xf - 0.02f, fr}, {xf + 0.02f, fr}, {xf - 0.17f, seatH + 0.28f}, {xf - 0.22f, seatH + 0.28f}}, z - 0.015f, z + 0.015f));
            }
            return m;
        });
    }
    // frame / engine block
    const float ey = std::max(fr, rr) * 0.35f;
    Frame fe = FrameAA({-0.3f, ey, -0.17f}, {0.3f, ey + 0.38f, 0.17f});
    o.Add("engine/0", "engine", fe, [&](const std::string& d) {
        if (d == "twin") { PolyMesh m = BoxAA({-0.3f, ey, -0.17f}, {0.3f, ey + 0.3f, 0.17f}); shapes::Append(m, BoxAA({-0.25f, ey + 0.31f, -0.16f}, {-0.02f, ey + 0.5f, 0.16f})); shapes::Append(m, BoxAA({0.02f, ey + 0.31f, -0.16f}, {0.25f, ey + 0.5f, 0.16f})); return m; }
        return BoxAA({-0.3f, ey, -0.17f}, {0.3f, ey + 0.38f, 0.17f});
    });
    Frame ff = FrameAA({xr, seatH - 0.2f, -0.05f}, {xf - 0.1f, seatH - 0.1f, 0.05f});
    o.Add("frame/0", "frame", ff, [&](const std::string&) {
        PolyMesh m = BoxAA({xr + 0.05f, seatH - 0.22f, -0.04f}, {xf - 0.25f, seatH - 0.12f, 0.04f});
        shapes::Append(m, Side({{xr, rr}, {xr + 0.07f, rr}, {xr + 0.45f, std::max(seatH - 0.2f, rr + 0.1f)}, {xr + 0.35f, std::max(seatH - 0.2f, rr + 0.1f)}}, -0.04f, 0.04f));
        return m;
    });
    // tank, seat, handlebar
    const float tx0 = 0.05f, tx1 = tx0 + tankLen;
    Frame ft = FrameAA({tx0, seatH - 0.05f, -0.17f}, {tx1, seatH + 0.22f, 0.17f});
    o.Add("tank/0", "tank", ft, [&](const std::string& d) {
        if (d == "round") return shapes::Ball(ft, 5, 10);
        return BoxAA({tx0, seatH - 0.05f, -0.17f}, {tx1, seatH + 0.22f, 0.17f});
    });
    Frame fs = FrameAA({xr + 0.2f, seatH - 0.1f, -0.15f}, {tx0 - 0.02f, seatH + 0.08f, 0.15f});
    o.Add("seat/0", "seat", fs, [&](const std::string& d) {
        if (d == "stepped") { PolyMesh m = BoxAA({xr + 0.2f, seatH - 0.1f, -0.15f}, {xr + 0.55f, seatH + 0.12f, 0.15f}); shapes::Append(m, BoxAA({xr + 0.5f, seatH - 0.1f, -0.15f}, {tx0 - 0.02f, seatH + 0.04f, 0.15f})); return m; }
        return BoxAA({xr + 0.2f, seatH - 0.1f, -0.15f}, {tx0 - 0.02f, seatH + 0.08f, 0.15f});
    });
    const float hx = xf - 0.3f, hy = seatH + 0.3f;
    Frame fh = FrameAA({hx - 0.05f, hy, -0.4f}, {hx + 0.05f, hy + 0.2f, 0.4f});
    o.Add("handlebar/0", "handlebar", fh, [&](const std::string& d) {
        PolyMesh m = CylZ(-0.38f, 0.38f, hx, hy, 0.03f, 8);
        if (d == "cruiser") {
            for (int s = 0; s < 2; ++s) shapes::Append(m, BoxAA({hx - 0.02f, hy, s ? 0.34f : -0.38f}, {hx + 0.02f, hy + 0.2f, s ? 0.38f : -0.34f}));
        }
        return m;
    });
    Headlight(o, "headlight/0", xf - 0.15f, hy - 0.05f, 0, Pf(r, "lightSize"));
    Taillight(o, "taillight/0", xr + 0.2f, seatH - 0.02f, 0, 0.1f);
    // exhaust
    Frame fx = FrameAA({xr - 0.1f, ey, 0.17f}, {0.2f, ey + 0.1f, 0.3f});
    o.Add("exhaust/0", "exhaust", fx, [&](const std::string& d) {
        PolyMesh m = CylX(xr - 0.1f, 0.2f, ey + 0.05f, 0.24f, 0.1f, 0.1f, 10);
        if (d == "dual") shapes::Append(m, CylX(xr - 0.1f, 0.2f, ey + 0.05f, -0.24f, 0.1f, 0.1f, 10));
        return m;
    });
    // mudguards
    for (int s = 0; s < 2; ++s) {
        const float R = s ? rr : fr, X = s ? xr : xf;
        Frame fm = FrameAA({X - R * 0.9f, R * 2 + 0.03f, -ww * 0.6f}, {X + R * 0.9f, R * 2 + 0.07f, ww * 0.6f});
        o.Add("fender/" + std::to_string(s), "fender", fm, [&](const std::string&) { return BoxAA({X - R * 0.9f, R * 2 + 0.03f, -ww * 0.6f}, {X + R * 0.9f, R * 2 + 0.07f, ww * 0.6f}); });
    }
}

// ---- Trailer ---------------------------------------------------------------------------------------------------
void GenerateTrailer(const Recipe& r, std::vector<GeneratedPart>& parts) {
    Out o{r, parts};
    const float L = Pf(r, "length"), W = Pf(r, "width"), deckH = Pf(r, "deckHeight"), cargoH = Pf(r, "cargoHeight");
    const float wr = Pf(r, "wheelRadius"), ww = Pf(r, "wheelWidth");
    const int axles = std::max(1, (int)r.Param("axles"));
    const float x0 = -L / 2, x1 = L / 2;
    Frame fd = FrameAA({x0, deckH - 0.15f, -W / 2}, {x1, deckH, W / 2});
    o.Add("deck/0", "deck", fd, [&](const std::string&) { return BoxAA({x0, deckH - 0.15f, -W / 2}, {x1, deckH, W / 2}); });
    const float cy = deckH + 0.02f;
    Frame fc = FrameAA({x0 + 0.05f, cy, -W / 2 + 0.05f}, {x1 - 0.05f, cy + cargoH, W / 2 - 0.05f});
    o.Add("cargo/0", "cargo", fc, [&](const std::string& d) {
        const float a0 = x0 + 0.05f, a1 = x1 - 0.05f, z = W / 2 - 0.05f;
        if (d == "open") {
            PolyMesh m;
            shapes::Append(m, BoxAA({a0, cy, -z}, {a1, cy + cargoH, -z + 0.05f}));
            shapes::Append(m, BoxAA({a0, cy, z - 0.05f}, {a1, cy + cargoH, z}));
            shapes::Append(m, BoxAA({a0, cy, -z + 0.04f}, {a0 + 0.05f, cy + cargoH, z - 0.04f}));
            shapes::Append(m, BoxAA({a1 - 0.05f, cy, -z + 0.04f}, {a1, cy + cargoH, z - 0.04f}));
            return m;
        }
        if (d == "flatbed") return BoxAA({a0, cy, -z}, {a1, cy + 0.08f, z});
        if (d == "tanker") return CylX(a0, a1, cy + cargoH * 0.5f, 0, cargoH, 2 * z, 20);
        if (d == "caravan") {   // a rounded-roof living trailer with a window each side
            PolyMesh m = Side({{a0, cy}, {a1, cy}, {a1, cy + cargoH * 0.7f}, {a1 - 0.5f, cy + cargoH}, {a0 + 0.3f, cy + cargoH}, {a0, cy + cargoH * 0.85f}}, -z, z);
            for (int s = 0; s < 2; ++s) {
                const float zz = s ? z : -z, sg = s ? 1.0f : -1.0f;
                shapes::Append(m, BoxAA({(a0 + a1) * 0.5f - 0.5f, cy + cargoH * 0.45f, std::min(zz - sg * 0.012f, zz + sg * 0.012f)}, {(a0 + a1) * 0.5f + 0.5f, cy + cargoH * 0.8f, std::max(zz - sg * 0.012f, zz + sg * 0.012f)}));
            }
            return m;
        }
        return BoxAA({a0, cy, -z}, {a1, cy + cargoH, z});   // box
    });
    const float track = W - ww + 0.1f;
    int wi = 0;
    for (int a = 0; a < axles; ++a) {
        const float sp = std::min(wr * 2 + 0.2f, L * 0.6f / (float)axles);
        const float x = -L * 0.12f + ((float)a - (float)(axles - 1) * 0.5f) * sp;
        for (int side = 0; side < 2; ++side) Wheel(o, "wheel/" + std::to_string(wi++), x, (side ? 1.0f : -1.0f) * track / 2, wr, ww);
    }
    // hitch tongue: a flat A shape in front
    Frame fh = FrameAA({x1, deckH - 0.12f, -W * 0.3f}, {x1 + Pf(r, "tongueLength"), deckH - 0.03f, W * 0.3f});
    o.Add("hitch/0", "hitch", fh, [&](const std::string& d) {
        const float tl = Pf(r, "tongueLength");
        if (d == "bar") { PolyMesh m = BoxAA({x1 - 0.1f, deckH - 0.12f, -0.05f}, {x1 + tl, deckH - 0.03f, 0.05f}); shapes::Append(m, CylZ(-0.06f, 0.06f, x1 + tl, deckH, 0.12f, 8)); return m; }
        return Side({{x1 - 0.1f, deckH - 0.12f}, {x1 + tl, deckH - 0.12f}, {x1 + tl, deckH - 0.03f}, {x1 - 0.1f, deckH - 0.03f}}, -W * 0.3f, W * 0.3f);   // wide plate
    });
    Taillight(o, "taillight/0", x0, deckH - 0.05f, -W * 0.4f, 0.12f);
    Taillight(o, "taillight/1", x0, deckH - 0.05f, W * 0.4f, 0.12f);
    // landing legs
    for (int s = 0; s < 2; ++s) {
        const float z = (s ? 1.0f : -1.0f) * W * 0.28f;
        Frame fl = FrameAA({x1 - 0.6f, 0, z - 0.04f}, {x1 - 0.52f, deckH - 0.15f, z + 0.04f});
        o.Add("leg/" + std::to_string(s), "leg", fl, [&](const std::string&) { return BoxAA({x1 - 0.6f, 0.02f, z - 0.04f}, {x1 - 0.52f, deckH - 0.14f, z + 0.04f}); });
    }
}

// ---- Template definitions ----------------------------------------------------------------------------------------
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
const Color kRed{0.75f, 0.12f, 0.12f, 1}, kBlue{0.18f, 0.32f, 0.62f, 1}, kWhite{0.92f, 0.92f, 0.9f, 1}, kBlack{0.1f, 0.1f, 0.11f, 1},
            kGlassC{0.55f, 0.75f, 0.82f, 1}, kChrome{0.78f, 0.8f, 0.82f, 1}, kYellow{0.9f, 0.72f, 0.12f, 1}, kGrey{0.5f, 0.52f, 0.55f, 1},
            kLight{0.97f, 0.95f, 0.75f, 1}, kTail{0.8f, 0.05f, 0.05f, 1}, kGreen{0.2f, 0.45f, 0.28f, 1};
std::vector<DesignDef> WheelDesigns() { return {{"plain", "Plain tyre"}, {"rim", "Tyre and rim"}, {"steel", "Steel wheel"}, {"spoke", "Five spoke"}, {"mesh", "Eight spoke mesh"}, {"dish", "Deep dish"}}; }
std::vector<DesignDef> HeadDesigns() { return {{"round", "Round"}, {"square", "Square"}, {"slim", "Slim bar"}, {"twin", "Twin lamps"}}; }
std::vector<DesignDef> TailDesigns() { return {{"box", "Box"}, {"strip", "Strip"}, {"round", "Round"}}; }
std::vector<DesignDef> BumperDesigns() { return {{"plain", "Plain"}, {"chrome", "Chrome bar"}, {"heavy", "Heavy"}, {"sport", "Sport lip"}}; }
std::vector<DesignDef> MirrorDesigns() { return {{"box", "Box"}, {"round", "Round"}, {"none", "None"}}; }
std::vector<DesignDef> GrilleDesigns() { return {{"slats", "Slats"}, {"plate", "Plate"}, {"none", "None"}}; }
void CommonSlots(B& b, bool grille = true, bool mirrors = true) {
    b.S("wheel", "Wheels", WheelDesigns(), kBlack, 1);
    b.S("headlight", "Headlights", HeadDesigns(), kLight, 2);
    b.S("taillight", "Tail lights", TailDesigns(), kTail, 3);
    b.S("bumper", "Bumpers", BumperDesigns(), kChrome, 4);
    if (mirrors) b.S("mirror", "Mirrors", MirrorDesigns(), kBlack, 5);
    if (grille) b.S("grille", "Grille", GrilleDesigns(), kBlack, 6);
}
void WheelParams(B& b, double radius, double width) {
    b.P("wheelRadius", "Wheel radius", radius, 0.12, 0.8, "Wheels", "Size of the wheels (the vehicle stands on them).");
    b.P("wheelWidth", "Wheel width", width, 0.06, 0.5, "Wheels", "How wide each tyre is.");
}

Template MakeCar() {
    B b;
    b.t.id = "car"; b.t.name = "Car"; b.t.category = "Vehicles"; b.t.version = 1;
    b.t.description = "A car: body, cabin, glass, four wheels, lights, bumpers, mirrors and grille, each a part you can change.";
    b.P("length", "Length", 4.4, 2.4, 7, "Body", "Overall length.");
    b.P("width", "Width", 1.8, 1.2, 2.6, "Body", "Overall width, wheels included.");
    b.P("height", "Height", 1.45, 1.0, 2.4, "Body", "Overall height.");
    b.P("groundClear", "Ground clearance", 0.18, 0.08, 0.6, "Body", "Gap under the body.");
    b.P("cabinScale", "Cabin size", 1.0, 0.7, 1.3, "Body", "Stretch or shrink the cabin.");
    b.P("cabinShift", "Cabin position", 0.0, -0.15, 0.15, "Body", "Move the cabin forward or back (fraction of length).");
    b.P("wheelbase", "Wheelbase", 2.7, 1.2, 5, "Wheels", "Distance between the axles.");
    b.P("axleShift", "Axles forward / back", 0.0, -0.15, 0.15, "Wheels", "Move both axles along the body (fraction of length).");
    WheelParams(b, 0.33, 0.22);
    b.P("lightSize", "Light size", 0.2, 0.08, 0.5, "Lights", "Size of the headlights (tail lights follow).");
    b.S("body", "Body style", {{"sedan", "Sedan"}, {"hatchback", "Hatchback"}, {"coupe", "Coupe"}, {"suv", "SUV"}, {"pickup", "Pickup"}, {"sports", "Sports car"}, {"van", "Van"}}, kRed, 0);
    b.S("cabin", "Cabin", {{"standard", "Standard"}}, kRed, 0);
    b.S("glass", "Glass", {{"clear", "Glass"}}, kGlassC, 7);
    b.S("bed", "Pickup bed", {{"open", "Open bed"}}, kGrey, 8);
    b.S("axle", "Axles", {{"bar", "Axle bar"}, {"none", "None"}}, kBlack, 12);
    b.S("spoiler", "Spoiler", {{"none", "None"}, {"lip", "Boot lip"}, {"wing", "Wing"}}, kBlack, 9);
    b.S("roof", "Roof fitting", {{"none", "None"}, {"lightbar", "Light bar"}, {"sign", "Taxi sign"}, {"rack", "Roof rack"}}, kGrey, 10);
    b.S("exhaust", "Exhaust", {{"none", "None"}, {"single", "Single pipe"}, {"dual", "Dual pipes"}}, kChrome, 11);
    CommonSlots(b);
    b.St("sedan", "Sedan", "A four-door saloon.", {}, {{"body", "sedan"}, {"wheel", "rim"}, {"headlight", "round"}, {"bumper", "plain"}, {"mirror", "box"}, {"grille", "plate"}}, {{"body", kRed}, {"cabin", kRed}});
    b.St("hatchback", "Hatchback", "A short car with a steep tail.", {{"length", 3.9}, {"height", 1.5}, {"wheelbase", 2.5}}, {{"body", "hatchback"}, {"wheel", "steel"}, {"headlight", "square"}, {"taillight", "strip"}, {"mirror", "round"}, {"grille", "slats"}},
         {{"body", kBlue}, {"cabin", kBlue}});
    b.St("coupe", "Coupe", "Low and long with a small cabin.", {{"height", 1.25}, {"length", 4.5}, {"cabinScale", 0.9}, {"groundClear", 0.12}},
         {{"body", "coupe"}, {"wheel", "spoke"}, {"headlight", "slim"}, {"taillight", "strip"}, {"bumper", "chrome"}, {"grille", "slats"}}, {{"body", kYellow}, {"cabin", kYellow}});
    b.St("suv", "SUV", "Tall with big wheels and a long roof.", {{"length", 4.7}, {"height", 1.8}, {"groundClear", 0.28}, {"wheelRadius", 0.4}, {"wheelWidth", 0.28}, {"wheelbase", 2.85}},
         {{"body", "suv"}, {"wheel", "steel"}, {"headlight", "square"}, {"bumper", "heavy"}, {"grille", "plate"}}, {{"body", kGreen}, {"cabin", kGreen}});
    b.St("pickup", "Pickup", "A cab and an open bed.", {{"length", 5.2}, {"height", 1.7}, {"groundClear", 0.26}, {"wheelRadius", 0.38}, {"wheelbase", 3.2}},
         {{"body", "pickup"}, {"wheel", "rim"}, {"headlight", "square"}, {"bumper", "heavy"}, {"grille", "plate"}}, {{"body", kWhite}, {"cabin", kWhite}, {"bed", kGrey}});
    b.St("sports", "Sports car", "Low, wide, with a wing and twin pipes.", {{"length", 4.5}, {"height", 1.2}, {"width", 1.95}, {"groundClear", 0.1}, {"wheelRadius", 0.34}, {"wheelWidth", 0.28}, {"cabinScale", 0.85}, {"wheelbase", 2.65}},
         {{"body", "sports"}, {"wheel", "mesh"}, {"headlight", "slim"}, {"taillight", "strip"}, {"bumper", "sport"}, {"mirror", "round"}, {"grille", "slats"}, {"spoiler", "wing"}, {"exhaust", "dual"}}, {{"body", kYellow}, {"cabin", kYellow}, {"spoiler", kBlack}});
    b.St("van", "Van", "A boxy delivery van.", {{"length", 5.0}, {"height", 2.0}, {"groundClear", 0.22}, {"wheelbase", 3.0}},
         {{"body", "van"}, {"wheel", "steel"}, {"headlight", "square"}, {"bumper", "plain"}, {"mirror", "box"}, {"grille", "plate"}}, {{"body", kWhite}, {"cabin", kWhite}});
    b.St("taxi", "Taxi", "A yellow sedan with a roof sign.", {}, {{"body", "sedan"}, {"wheel", "steel"}, {"headlight", "round"}, {"bumper", "plain"}, {"mirror", "box"}, {"grille", "slats"}, {"roof", "sign"}}, {{"body", kYellow}, {"cabin", kYellow}, {"roof", kWhite}});
    b.St("police", "Patrol car", "A white sedan with a light bar and a heavy front bumper.", {}, {{"body", "sedan"}, {"wheel", "dish"}, {"headlight", "twin"}, {"bumper", "heavy"}, {"mirror", "box"}, {"grille", "slats"}, {"roof", "lightbar"}}, {{"body", kWhite}, {"cabin", kWhite}, {"roof", kBlue}});
    b.t.constrain = [](Recipe& r) {   // compatibility: no wing on a utility body, no taxi sign on a pickup
        const std::string body = r.slotDesign.count("body") ? r.slotDesign["body"] : "sedan";
        if (r.slotDesign["spoiler"] == "wing" && (body == "pickup" || body == "suv" || body == "van")) r.slotDesign["spoiler"] = "lip";
        if (r.slotDesign["roof"] == "sign" && body == "pickup") r.slotDesign["roof"] = "rack";
    };
    b.t.generate = GenerateCar;
    return b.t;
}

Template MakeTruck() {
    B b;
    b.t.id = "truck"; b.t.name = "Truck"; b.t.category = "Vehicles"; b.t.version = 1;
    b.t.description = "A truck: chassis, cab, glass, cargo, any number of rear axles, lights, bumpers, mirrors and an exhaust.";
    b.P("length", "Length", 9, 5, 18, "Body", "Overall length.");
    b.P("width", "Width", 2.5, 1.8, 3.2, "Body", "Overall width.");
    b.P("cabHeight", "Cab height", 2.4, 1.6, 3.6, "Body", "Height of the cab.");
    b.P("cabLength", "Cab length", 2.4, 1.4, 4, "Body", "Length of the cab (and hood).");
    b.P("chassisHeight", "Chassis height", 1.0, 0.6, 1.6, "Body", "Height of the frame above the ground.");
    b.P("cargoHeight", "Cargo height", 2.4, 0.6, 3.6, "Cargo", "Height of the load area.");
    b.P("rearAxles", "Rear axles", 2, 1, 4, "Wheels", "How many rear axles.", B::K::Int);
    WheelParams(b, 0.5, 0.32);
    b.P("lightSize", "Light size", 0.25, 0.08, 0.6, "Lights", "Size of the headlights.");
    b.S("cab", "Cab", {{"cabover", "Cab over engine"}, {"conventional", "Conventional (with hood)"}}, kBlue, 0);
    b.S("cargo", "Cargo", {{"box", "Closed box"}, {"flatbed", "Flatbed"}, {"tanker", "Tanker"}, {"tipper", "Tipper (tilted)"}}, kWhite, 9);
    b.S("chassis", "Chassis", {{"frame", "Frame"}}, kBlack, 10);
    b.S("glass", "Glass", {{"clear", "Glass"}}, kGlassC, 7);
    b.S("exhaust", "Exhaust", {{"stack", "Single stack"}, {"twin", "Twin stacks"}, {"none", "None"}}, kChrome, 11);
    CommonSlots(b);
    b.St("box_truck", "Box truck", "A cab-over truck with a closed box.", {}, {{"cab", "cabover"}, {"cargo", "box"}, {"wheel", "steel"}, {"headlight", "square"}, {"bumper", "plain"}, {"grille", "plate"}}, {{"cab", kBlue}});
    b.St("semi_cab", "Conventional truck", "A hooded cab with a flatbed.", {{"cabLength", 3.4}, {"length", 10}}, {{"cab", "conventional"}, {"cargo", "flatbed"}, {"wheel", "rim"}, {"headlight", "round"}, {"bumper", "chrome"}, {"grille", "slats"}, {"exhaust", "twin"}}, {{"cab", kRed}, {"cargo", kGrey}});
    b.St("tanker", "Tanker", "A cylinder tank on three axles.", {{"rearAxles", 3}, {"length", 11}, {"cargoHeight", 2.2}}, {{"cab", "conventional"}, {"cargo", "tanker"}, {"wheel", "steel"}, {"headlight", "round"}, {"bumper", "heavy"}, {"grille", "slats"}}, {{"cab", kWhite}, {"cargo", kChrome}});
    b.St("light_truck", "Light truck", "A small delivery truck.", {{"length", 6.2}, {"width", 2.1}, {"cabHeight", 1.9}, {"cabLength", 1.8}, {"cargoHeight", 1.8}, {"rearAxles", 1}, {"wheelRadius", 0.38}, {"chassisHeight", 0.8}},
         {{"cab", "cabover"}, {"cargo", "box"}, {"wheel", "rim"}, {"headlight", "square"}, {"bumper", "plain"}, {"grille", "plate"}, {"exhaust", "none"}}, {{"cab", kYellow}, {"cargo", kWhite}});
    b.St("tipper", "Tipper truck", "An open bin tilted up at the front.", {{"length", 8}, {"cargoHeight", 1.6}, {"rearAxles", 2}},
         {{"cab", "conventional"}, {"cargo", "tipper"}, {"wheel", "dish"}, {"headlight", "twin"}, {"bumper", "heavy"}, {"grille", "slats"}, {"exhaust", "stack"}}, {{"cab", kYellow}, {"cargo", kGrey}});
    b.St("long_haul", "Long-haul truck", "A hooded cab with a long closed box on three axles.", {{"length", 13}, {"cabLength", 3.2}, {"rearAxles", 3}, {"cargoHeight", 2.8}},
         {{"cab", "conventional"}, {"cargo", "box"}, {"wheel", "mesh"}, {"headlight", "round"}, {"bumper", "chrome"}, {"grille", "slats"}, {"exhaust", "twin"}}, {{"cab", kGreen}, {"cargo", kWhite}});
    b.t.generate = GenerateTruck;
    return b.t;
}

Template MakeBus() {
    B b;
    b.t.id = "bus"; b.t.name = "Bus"; b.t.category = "Vehicles"; b.t.version = 1;
    b.t.description = "A bus: body, a row of windows, a windscreen, a door, wheels, lights, bumpers, mirrors and a roof box.";
    b.P("length", "Length", 11, 5, 20, "Body", "Overall length.");
    b.P("width", "Width", 2.5, 1.8, 3.2, "Body", "Overall width.");
    b.P("height", "Height", 3.1, 2.2, 4.2, "Body", "Overall height.");
    b.P("groundClear", "Ground clearance", 0.3, 0.15, 0.8, "Body", "Gap under the body.");
    b.P("windows", "Windows per side", 5, 1, 10, "Body", "How many side windows.", B::K::Int);
    b.P("rearAxles", "Rear axles", 1, 1, 3, "Wheels", "How many rear axles.", B::K::Int);
    WheelParams(b, 0.5, 0.3);
    b.P("lightSize", "Light size", 0.22, 0.08, 0.5, "Lights", "Size of the headlights.");
    b.S("body", "Body style", {{"city", "City bus"}, {"coach", "Coach"}, {"minibus", "Minibus"}, {"school", "School bus (with nose)"}}, kYellow, 0);
    b.S("window", "Windows", {{"clear", "Glass"}, {"none", "None"}}, kGlassC, 7);
    b.S("door", "Door", {{"sliding", "Sliding"}, {"folding", "Folding"}, {"none", "None"}}, kGrey, 12);
    b.S("roof", "Roof box", {{"vent", "Air-conditioning box"}, {"none", "None"}}, kWhite, 13);
    CommonSlots(b, false, true);
    b.St("city", "City bus", "A boxy low-floor bus.", {}, {{"body", "city"}, {"window", "clear"}, {"door", "sliding"}, {"wheel", "steel"}, {"headlight", "square"}, {"bumper", "plain"}, {"mirror", "box"}}, {{"body", kYellow}});
    b.St("coach", "Coach", "Tall with a raised roof and big windows.", {{"length", 12.5}, {"height", 3.6}, {"groundClear", 0.9}, {"windows", 6}, {"rearAxles", 2}},
         {{"body", "coach"}, {"window", "clear"}, {"door", "folding"}, {"wheel", "rim"}, {"headlight", "slim"}, {"bumper", "chrome"}, {"mirror", "round"}}, {{"body", kBlue}});
    b.St("minibus", "Minibus", "A short van-like bus.", {{"length", 6.5}, {"width", 2.1}, {"height", 2.5}, {"windows", 3}, {"wheelRadius", 0.38}},
         {{"body", "minibus"}, {"window", "clear"}, {"door", "sliding"}, {"wheel", "rim"}, {"headlight", "round"}, {"bumper", "plain"}, {"mirror", "box"}, {"roof", "none"}}, {{"body", kWhite}});
    b.St("school", "School bus", "A long nose and yellow paint.", {{"length", 10}, {"height", 2.9}, {"windows", 6}, {"groundClear", 0.35}},
         {{"body", "school"}, {"window", "clear"}, {"door", "folding"}, {"wheel", "steel"}, {"headlight", "round"}, {"bumper", "heavy"}, {"mirror", "round"}}, {{"body", kYellow}});
    b.St("shuttle", "Shuttle bus", "A short city bus.", {{"length", 7.5}, {"windows", 3}, {"wheelRadius", 0.42}},
         {{"body", "city"}, {"window", "clear"}, {"door", "sliding"}, {"wheel", "rim"}, {"headlight", "twin"}, {"bumper", "plain"}, {"mirror", "box"}, {"roof", "none"}}, {{"body", kWhite}});
    b.St("long_city", "Long city bus", "Eight windows on two rear axles.", {{"length", 15}, {"windows", 8}, {"rearAxles", 2}},
         {{"body", "city"}, {"window", "clear"}, {"door", "sliding"}, {"wheel", "dish"}, {"headlight", "slim"}, {"bumper", "chrome"}, {"mirror", "box"}}, {{"body", kGreen}});
    b.t.generate = GenerateBus;
    return b.t;
}

Template MakeBike() {
    B b;
    b.t.id = "motorcycle"; b.t.name = "Motorcycle"; b.t.category = "Vehicles"; b.t.version = 1;
    b.t.description = "A motorcycle: two wheels (different sizes), fork, frame, engine, tank, seat, handlebar, headlight, exhaust and mudguards.";
    b.P("wheelbase", "Wheelbase", 1.5, 0.9, 2.4, "Body", "Distance between the wheels.");
    b.P("seatHeight", "Seat height", 0.8, 0.5, 1.2, "Body", "Height of the seat.");
    b.P("tankLength", "Tank length", 0.5, 0.25, 0.9, "Body", "Length of the fuel tank.");
    b.P("frontWheelRadius", "Front wheel radius", 0.32, 0.18, 0.5, "Wheels", "Radius of the front wheel.");
    b.P("rearWheelRadius", "Rear wheel radius", 0.34, 0.18, 0.5, "Wheels", "Radius of the rear wheel.");
    b.P("wheelWidth", "Wheel width", 0.14, 0.06, 0.35, "Wheels", "Tyre width.");
    b.P("lightSize", "Headlight size", 0.2, 0.1, 0.4, "Lights", "Size of the headlight.");
    b.S("wheel", "Wheels", WheelDesigns(), kBlack, 1);
    b.S("frame", "Frame", {{"tube", "Tube frame"}}, kGrey, 0);
    b.S("fork", "Fork", {{"telescopic", "Telescopic"}}, kChrome, 2);
    b.S("engine", "Engine", {{"single", "Single"}, {"twin", "Twin"}}, kGrey, 3);
    b.S("tank", "Tank", {{"box", "Box"}, {"round", "Rounded"}}, kRed, 4);
    b.S("seat", "Seat", {{"flat", "Flat"}, {"stepped", "Stepped"}}, kBlack, 5);
    b.S("handlebar", "Handlebar", {{"straight", "Straight"}, {"cruiser", "Cruiser"}}, kChrome, 6);
    b.S("headlight", "Headlight", HeadDesigns(), kLight, 7);
    b.S("taillight", "Tail light", TailDesigns(), kTail, 8);
    b.S("exhaust", "Exhaust", {{"single", "Single"}, {"dual", "Dual"}, {"none", "None"}}, kChrome, 9);
    b.S("fender", "Mudguards", {{"short", "Short"}, {"none", "None"}}, kRed, 10);
    b.St("standard", "Standard", "A middleweight road bike.", {}, {{"wheel", "rim"}, {"engine", "single"}, {"tank", "box"}, {"seat", "flat"}, {"handlebar", "straight"}, {"headlight", "round"}, {"exhaust", "single"}}, {{"tank", kRed}});
    b.St("cruiser", "Cruiser", "Long and low with a rounded tank and wide bars.", {{"wheelbase", 1.8}, {"seatHeight", 0.7}, {"tankLength", 0.55}, {"wheelWidth", 0.17}},
         {{"wheel", "spoke"}, {"engine", "twin"}, {"tank", "round"}, {"seat", "stepped"}, {"handlebar", "cruiser"}, {"headlight", "round"}, {"exhaust", "dual"}}, {{"tank", kBlack}});
    b.St("sport", "Sport", "Short and tall at the tail, with a slim light.", {{"wheelbase", 1.35}, {"seatHeight", 0.85}, {"rearWheelRadius", 0.32}, {"tankLength", 0.45}},
         {{"wheel", "rim"}, {"engine", "twin"}, {"tank", "box"}, {"seat", "stepped"}, {"handlebar", "straight"}, {"headlight", "slim"}, {"taillight", "strip"}, {"exhaust", "dual"}}, {{"tank", kBlue}});
    b.St("scooter", "Scooter", "Small wheels and a low seat.", {{"wheelbase", 1.2}, {"seatHeight", 0.7}, {"frontWheelRadius", 0.2}, {"rearWheelRadius", 0.2}, {"wheelWidth", 0.1}, {"tankLength", 0.3}},
         {{"wheel", "steel"}, {"engine", "single"}, {"tank", "round"}, {"seat", "flat"}, {"handlebar", "straight"}, {"headlight", "square"}, {"exhaust", "none"}}, {{"tank", kWhite}});
    b.St("dirt", "Dirt bike", "Tall, light, with a long fork and spoked wheels.", {{"wheelbase", 1.45f}, {"seatHeight", 0.95f}, {"frontWheelRadius", 0.36f}, {"rearWheelRadius", 0.34f}, {"wheelWidth", 0.09f}, {"tankLength", 0.32f}},
         {{"wheel", "mesh"}, {"engine", "single"}, {"tank", "box"}, {"seat", "flat"}, {"handlebar", "straight"}, {"headlight", "square"}, {"exhaust", "single"}, {"fender", "none"}}, {{"tank", kYellow}});
    b.St("touring", "Touring bike", "A big round tank, a deep seat, wide bars and dual pipes.", {{"wheelbase", 1.7f}, {"seatHeight", 0.82f}, {"tankLength", 0.65f}, {"wheelWidth", 0.19f}},
         {{"wheel", "steel"}, {"engine", "twin"}, {"tank", "round"}, {"seat", "stepped"}, {"handlebar", "cruiser"}, {"headlight", "twin"}, {"exhaust", "dual"}}, {{"tank", kGreen}});
    b.t.generate = GenerateBike;
    return b.t;
}

Template MakeTrailer() {
    B b;
    b.t.id = "trailer"; b.t.name = "Trailer"; b.t.category = "Vehicles"; b.t.version = 1;
    b.t.description = "A trailer: deck, cargo, wheels on one to three axles, hitch, tail lights and landing legs.";
    b.P("length", "Length", 6, 2, 14, "Body", "Length of the deck.");
    b.P("width", "Width", 2.3, 1.2, 3, "Body", "Width of the deck.");
    b.P("deckHeight", "Deck height", 0.9, 0.4, 1.6, "Body", "Height of the deck above the ground.");
    b.P("cargoHeight", "Cargo height", 2.2, 0.4, 3.6, "Cargo", "Height of the load area.");
    b.P("tongueLength", "Hitch length", 1.2, 0.3, 3, "Body", "Length of the hitch in front.");
    b.P("axles", "Axles", 1, 1, 3, "Wheels", "How many axles.", B::K::Int);
    WheelParams(b, 0.4, 0.25);
    b.S("deck", "Deck", {{"plank", "Deck"}}, kGrey, 0);
    b.S("cargo", "Cargo", {{"box", "Closed box"}, {"open", "Open sides"}, {"flatbed", "Flatbed"}, {"tanker", "Tanker"}, {"caravan", "Caravan"}}, kWhite, 1);
    b.S("hitch", "Hitch", {{"plate", "Wide plate"}, {"bar", "Bar and ball"}}, kBlack, 2);
    b.S("leg", "Landing legs", {{"post", "Posts"}, {"none", "None"}}, kGrey, 3);
    b.S("wheel", "Wheels", WheelDesigns(), kBlack, 4);
    b.S("taillight", "Tail lights", TailDesigns(), kTail, 5);
    b.St("box", "Box trailer", "A closed box on one axle.", {}, {{"cargo", "box"}, {"hitch", "plate"}, {"wheel", "steel"}}, {{"cargo", kWhite}});
    b.St("open", "Open trailer", "Low sides, two axles.", {{"axles", 2}, {"cargoHeight", 0.9}, {"length", 6.5}}, {{"cargo", "open"}, {"hitch", "bar"}, {"wheel", "rim"}}, {{"cargo", kGreen}});
    b.St("flatbed", "Flatbed", "A bare deck for loads.", {{"axles", 2}, {"length", 8}}, {{"cargo", "flatbed"}, {"hitch", "plate"}, {"wheel", "steel"}}, {{"cargo", kGrey}});
    b.St("tanker", "Tanker trailer", "A cylinder on three axles.", {{"axles", 3}, {"length", 9}, {"cargoHeight", 2.1}, {"deckHeight", 1.0}}, {{"cargo", "tanker"}, {"hitch", "plate"}, {"wheel", "steel"}}, {{"cargo", kChrome}});
    b.St("caravan", "Caravan", "A rounded living trailer with windows.", {{"length", 5.5}, {"cargoHeight", 2.0}, {"axles", 1}}, {{"cargo", "caravan"}, {"hitch", "bar"}, {"wheel", "rim"}}, {{"cargo", kWhite}});
    b.St("utility", "Utility trailer", "A small low open trailer.", {{"length", 3}, {"width", 1.6}, {"cargoHeight", 0.5}, {"wheelRadius", 0.25}, {"deckHeight", 0.6}, {"tongueLength", 1.0}}, {{"cargo", "open"}, {"hitch", "bar"}, {"wheel", "plain"}}, {{"cargo", kGrey}});
    b.t.generate = GenerateTrailer;
    return b.t;
}

} // namespace

void RegisterVehicleTemplates() {
    Registry& r = Registry::Instance();
    r.Register(MakeCar());
    r.Register(MakeTruck());
    r.Register(MakeBus());
    r.Register(MakeBike());
    r.Register(MakeTrailer());
}

} // namespace okay::modeling::guided
