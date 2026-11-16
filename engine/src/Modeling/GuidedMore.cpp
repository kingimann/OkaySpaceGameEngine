// More guided templates: aircraft, boat, weapon, tower, lamp and tent. Each is a set of named parts (fuselage, wings, hull, sail,
// blade, grip, shaft, roof, shade, poles...) that can be given another design or removed, with the same Detail levels, start designs
// and rebuild rules as the others. Written from general parametric modelling (lofted tubes with superellipse sections, planforms and
// side profiles pushed across a thickness, prisms, ellipsoids) and general knowledge of how these objects are shaped; no code,
// models or assets from other projects. Every object stands on y = 0 and is centred on x = z = 0; vehicles face +z.
#include "GuidedKit.hpp"
#include <map>

namespace okay::modeling::guided {
namespace {
using namespace kit;

PolyMesh Cyl(float x, float y0, float y1, float z, float rx, float rz, int sides = 12) {   // an upright cylinder
    return shapes::Prism(FrameAA({x - rx, y0, z - rz}, {x + rx, y1, z + rz}), sides);
}
void FixCcw(std::vector<Vec2>& p) {
    double area = 0;
    for (size_t i = 0; i < p.size(); ++i) { const Vec2& a = p[i]; const Vec2& b = p[(i + 1) % p.size()]; area += (double)a.x * b.y - (double)b.x * a.y; }
    if (area < 0) std::reverse(p.begin(), p.end());
}
// A flat shape seen from above: points (x, z), pushed up from y0 to y1.
PolyMesh Plan(std::vector<Vec2> pts, float y0, float y1) {
    FixCcw(pts);
    return shapes::ExtrudeAxes({0, (y0 + y1) * 0.5f, 0}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}, pts, std::max(0.004f, y1 - y0));
}
// A shape seen from the side: points (z, y), pushed across x from x0 to x1.
PolyMesh Side(std::vector<Vec2> pts, float x0, float x1) {
    FixCcw(pts);
    return shapes::ExtrudeAxes({(x0 + x1) * 0.5f, 0, 0}, {0, 0, 1}, {0, 1, 0}, {1, 0, 0}, pts, std::max(0.004f, x1 - x0));
}
int Sd(int base, int mn = 6) { return shapes::Scale(base, mn); }
PolyMesh Tube(const std::vector<Station>& st, Vec3 refA, int sides, bool a, bool b, float expo = 2.0f) { return Loft(st, refA, sides, a, b, expo); }
float Lerp0(float a, float b, float t) { return a + (b - a) * t; }

// ======================================================================================================================
// Aircraft
// ======================================================================================================================
void GenerateAircraft(const Recipe& r, std::vector<GeneratedPart>& parts) {
    Out o{r, parts};
    const float L = Pf(r, "length"), span = Pf(r, "wingSpan"), R = Pf(r, "bodyRadius");
    const std::string fuseD = r.DesignFor("fuselage", "fuselage/0"), wingD = r.DesignFor("wings", "wing/0"), tailD = r.DesignFor("tail", "tail/0");
    const float gear = r.DesignFor("gear", "gear/0") == "none" ? 0.0f : 1.0f;
    const float cy = std::max(R + 0.2f, 0.55f * R + 0.8f) * (gear > 0 ? 1.0f : 0.8f);   // the fuselage axis height above the ground
    const int sides = Sd(14, 8);
    const float z0 = -L * 0.5f, z1 = L * 0.5f;
    // fuselage: a lofted tube, nose to tail; the design sets the shape of the sections and the taper
    o.Add("fuselage/0", "fuselage", FrameAA({-R, cy - R, z0}, {R, cy + R, z1}), [&](const std::string& d) {
        const float slim = d == "slim" ? 0.75f : d == "cargo" ? 1.2f : 1.0f;
        const float noseK = d == "slim" ? 0.7f : 0.45f;
        std::vector<Station> st = {
            {{0, cy - R * 0.05f, z1 - 0.01f}, R * 0.12f * slim, R * 0.12f * slim},
            {{0, cy - R * 0.04f, z1 - L * 0.04f}, R * noseK * slim, R * noseK * slim},
            {{0, cy, z1 - L * 0.14f}, R * 0.85f * slim, R * 0.85f * slim},
            {{0, cy, z1 - L * 0.3f}, R * slim, R * slim},
            {{0, cy, z0 + L * 0.35f}, R * slim, R * slim},
            {{0, cy + R * 0.12f, z0 + L * 0.14f}, R * 0.62f * slim, R * 0.6f * slim},
            {{0, cy + R * 0.3f, z0 + 0.02f}, R * 0.2f * slim, R * 0.18f * slim}};
        return Tube(st, {0, 1, 0}, sides, false, false, d == "cargo" ? 2.6f : 2.0f);
    });
    // canopy
    o.Add("canopy/0", "canopy", FrameAA({-R * 0.7f, cy + R * 0.4f, z1 - L * 0.32f}, {R * 0.7f, cy + R * 1.5f, z1 - L * 0.12f}), [&](const std::string& d) {
        if (d == "none") return PolyMesh();
        const float len = d == "long" ? L * 0.26f : L * 0.14f;
        const float zc = z1 - L * 0.22f;
        return Ell({0, cy + R * 0.82f, zc}, R * 0.58f, R * (d == "long" ? 0.5f : 0.62f), len * 0.5f, 5, 10);
    });
    // wings: a planform pushed through a thickness; swept, straight, delta or a pair for a biplane
    const float wy = cy + (wingD == "high" ? R * 0.65f : wingD == "low" ? -R * 0.55f : wingD == "biplane" ? -R * 0.35f : -R * 0.2f);
    const float chord = L * (wingD == "delta" ? 0.5f : 0.2f), wz = z1 - L * 0.42f;
    auto wing = [&](float y, float ch, float zc) {
        const float hs = span * 0.5f;
        std::vector<Vec2> p;
        if (wingD == "delta") p = {{-R * 0.5f, zc + ch * 0.5f}, {-hs, zc - ch * 0.5f}, {hs, zc - ch * 0.5f}, {R * 0.5f, zc + ch * 0.5f}};
        else if (wingD == "swept") p = {{-R * 0.9f, zc + ch * 0.55f}, {-hs, zc - ch * 0.5f}, {-hs, zc - ch * 0.75f}, {hs, zc - ch * 0.75f}, {hs, zc - ch * 0.5f}, {R * 0.9f, zc + ch * 0.55f}};
        else p = {{-hs, zc + ch * 0.5f}, {-hs, zc - ch * 0.5f}, {hs, zc - ch * 0.5f}, {hs, zc + ch * 0.5f}};
        return Plan(p, y - 0.05f - R * 0.02f, y + 0.05f + R * 0.02f);
    };
    o.Add("wing/0", "wings", FrameAA({-span * 0.5f, wy - 0.1f, wz - chord}, {span * 0.5f, wy + 0.1f, wz + chord}), [&](const std::string&) { return wing(wy, chord, wz); });
    if (wingD == "biplane") {
        const float top = cy + R * 1.35f;
        o.Add("wing/1", "wings", FrameAA({-span * 0.5f, top - 0.1f, wz - chord}, {span * 0.5f, top + 0.1f, wz + chord}), [&](const std::string&) { return wing(top, chord * 0.9f, wz + 0.01f); });
        o.Add("strut/0", "wings", FrameAA({-span * 0.4f, wy, wz - chord * 0.3f}, {span * 0.4f, top, wz + chord * 0.3f}), [&](const std::string&) {
            PolyMesh m;
            for (int s = -1; s <= 1; s += 2) for (int k = 0; k < 2; ++k)
                shapes::Append(m, Limb({s * span * (0.18f + 0.2f * (float)k), wy + 0.06f, wz + chord * 0.3f}, {s * span * (0.18f + 0.2f * (float)k) + 0.003f, top - 0.06f, wz + chord * 0.3f + 0.003f}, 0.03f, true, 6));
            return m;
        });
    }
    // tail: a fin and a tailplane
    const float tz = z0 + L * 0.1f;
    o.Add("tail/0", "tail", FrameAA({-span * 0.2f, cy, tz - L * 0.1f}, {span * 0.2f, cy + R * 2.8f, tz + L * 0.06f}), [&](const std::string& d) {
        PolyMesh m;
        const float fh = R * (d == "t_tail" ? 2.6f : 2.2f);
        const float x = d == "twin" ? R * 1.6f : 0.0f;
        for (int k = 0; k < (d == "twin" ? 2 : 1); ++k) {
            const float xx = d == "twin" ? (k ? x : -x) : 0.0f;
            shapes::Append(m, Side({{tz + L * 0.06f, cy + R * 0.2f}, {tz - L * 0.02f, cy + fh}, {tz - L * 0.09f, cy + fh}, {tz - L * 0.1f, cy + R * 0.2f}}, xx - 0.04f, xx + 0.04f));
        }
        const float py = d == "t_tail" ? cy + fh : cy + R * 0.25f;
        const float hs = span * 0.2f;
        shapes::Append(m, Plan({{-hs, tz - L * 0.095f}, {-hs * 0.9f, tz + L * 0.03f}, {hs * 0.9f, tz + L * 0.03f}, {hs, tz - L * 0.095f}}, py - 0.03f, py + 0.03f));
        return m;
    });
    // engines: a nose propeller, or pods under the wings
    o.Add("engine/0", "engine", FrameAA({-span * 0.4f, wy - R, wz - chord}, {span * 0.4f, wy + R * 0.4f, wz + chord}), [&](const std::string& d) {
        PolyMesh m;
        if (d == "none") return m;
        if (d == "propeller") {
            const float pz = z1 + 0.02f;
            shapes::Append(m, Ell({0, cy, pz - R * 0.1f}, R * 0.3f, R * 0.3f, R * 0.4f, 4, 10));
            for (int k = 0; k < 2; ++k) {
                const float a = 1.5708f * (float)k;
                shapes::Append(m, Limb({std::cos(a) * R * 0.2f, cy + std::sin(a) * R * 0.2f, pz + R * 0.2f}, {std::cos(a) * R * 1.9f, cy + std::sin(a) * R * 1.9f, pz + R * 0.22f}, 0.045f, false));
                shapes::Append(m, Limb({-std::cos(a) * R * 0.2f, cy - std::sin(a) * R * 0.2f, pz + R * 0.2f}, {-std::cos(a) * R * 1.9f, cy - std::sin(a) * R * 1.9f, pz + R * 0.22f}, 0.045f, false));
            }
            return m;
        }
        const int n = d == "quad" ? 2 : 1;
        for (int s = -1; s <= 1; s += 2)
            for (int k = 0; k < n; ++k) {
                const float x = s * span * (d == "quad" ? 0.16f + 0.17f * (float)k : 0.28f);
                const float y = wy - R * 0.42f;
                std::vector<Station> st = {{{x, y, wz + chord * 0.8f}, R * 0.25f, R * 0.25f}, {{x, y, wz + chord * 0.4f}, R * 0.34f, R * 0.34f}, {{x, y, wz - chord * 0.6f}, R * 0.3f, R * 0.3f}, {{x, y, wz - chord * 0.95f}, R * 0.18f, R * 0.18f}};
                shapes::Append(m, Tube(st, {0, 1, 0}, Sd(10, 6), true, true));
            }
        return m;
    });
    // landing gear: a nose wheel and two main wheels on short legs
    o.Add("gear/0", "gear", FrameAA({-R * 1.6f, 0, z0}, {R * 1.6f, cy, z1}), [&](const std::string& d) {
        PolyMesh m;
        if (d == "none") return m;
        const float wr = std::max(0.12f, R * 0.32f);
        const float pts[3][2] = {{0.0f, z1 - L * 0.22f}, {-R * 1.1f, wz - chord * 0.3f}, {R * 1.1f, wz - chord * 0.3f}};
        for (int k = 0; k < 3; ++k) {
            shapes::Append(m, Limb({pts[k][0], wr, pts[k][1]}, {pts[k][0] * 0.9f, cy - R * 0.6f, pts[k][1]}, 0.04f, true, 6));
            shapes::Append(m, Limb({pts[k][0] - wr * 0.5f, wr, pts[k][1] + 0.002f}, {pts[k][0] + wr * 0.5f, wr + 0.001f, pts[k][1] + 0.004f}, wr, true, Sd(12, 8)));
        }
        return m;
    });
}

Template MakeAircraft() {
    B b;
    b.t.id = "aircraft"; b.t.name = "Aircraft"; b.t.category = "Vehicles"; b.t.version = 1;
    b.t.description = "An aircraft: a fuselage, wings (straight, swept, delta or biplane), a tail, a canopy, engines (propeller or jet pods) and landing gear. Faces +Z.";
    b.P("length", "Length", 9.0, 3.0, 40.0, "Size", "Nose to tail.");
    b.P("wingSpan", "Wing span", 11.0, 3.0, 45.0, "Size", "Tip to tip.");
    b.P("bodyRadius", "Fuselage radius", 0.7, 0.25, 3.0, "Size", "Half the width of the body.");
    const Color kPaint{0.85f, 0.86f, 0.88f, 1}, kRed{0.75f, 0.15f, 0.15f, 1}, kGlass{0.45f, 0.62f, 0.78f, 1}, kDark{0.2f, 0.21f, 0.24f, 1}, kGreen{0.3f, 0.4f, 0.25f, 1}, kYellow{0.9f, 0.75f, 0.15f, 1};
    b.S("fuselage", "Fuselage", {{"tube", "Round body"}, {"slim", "Slim body"}, {"cargo", "Wide body"}}, kPaint, 0);
    b.S("canopy", "Canopy", {{"bubble", "Bubble"}, {"long", "Long"}, {"none", "None"}}, kGlass, 1);
    b.S("wings", "Wings", {{"straight", "Straight"}, {"swept", "Swept back"}, {"delta", "Delta"}, {"high", "Straight, high"}, {"low", "Straight, low"}, {"biplane", "Biplane"}}, kPaint, 2);
    b.S("tail", "Tail", {{"conventional", "Conventional"}, {"t_tail", "T-tail"}, {"twin", "Twin fins"}}, kPaint, 3);
    b.S("engine", "Engines", {{"propeller", "Nose propeller"}, {"jet", "Jet pods"}, {"quad", "Four jet pods"}, {"none", "None"}}, kDark, 4);
    b.S("gear", "Landing gear", {{"wheels", "Wheels"}, {"none", "None (in flight)"}}, kDark, 5);
    b.St("light_plane", "Light plane", "A small high-wing propeller plane.", {{"length", 8.0}, {"wingSpan", 11.0}, {"bodyRadius", 0.6}}, {{"fuselage", "tube"}, {"canopy", "bubble"}, {"wings", "high"}, {"tail", "conventional"}, {"engine", "propeller"}, {"gear", "wheels"}}, {{"wings", kRed}, {"tail", kRed}});
    b.St("biplane", "Biplane", "Two wings and a propeller.", {{"length", 7.0}, {"wingSpan", 9.0}, {"bodyRadius", 0.5}}, {{"fuselage", "tube"}, {"canopy", "none"}, {"wings", "biplane"}, {"tail", "conventional"}, {"engine", "propeller"}, {"gear", "wheels"}}, {{"fuselage", kYellow}, {"wings", kRed}, {"tail", kYellow}});
    b.St("jet_fighter", "Jet fighter", "A slim body with delta wings and twin fins.", {{"length", 14.0}, {"wingSpan", 9.0}, {"bodyRadius", 0.8}}, {{"fuselage", "slim"}, {"canopy", "long"}, {"wings", "delta"}, {"tail", "twin"}, {"engine", "none"}, {"gear", "wheels"}}, {{"fuselage", kGreen}, {"wings", kGreen}, {"tail", kGreen}});
    b.St("airliner", "Airliner", "A long body, swept wings and four jet pods.", {{"length", 36.0}, {"wingSpan", 36.0}, {"bodyRadius", 2.0}}, {{"fuselage", "tube"}, {"canopy", "none"}, {"wings", "swept"}, {"tail", "conventional"}, {"engine", "quad"}, {"gear", "wheels"}}, {{"tail", kRed}});
    b.St("cargo_plane", "Cargo plane", "A wide body, high straight wings and a T-tail.", {{"length", 30.0}, {"wingSpan", 34.0}, {"bodyRadius", 2.4}}, {{"fuselage", "cargo"}, {"canopy", "none"}, {"wings", "high"}, {"tail", "t_tail"}, {"engine", "jet"}, {"gear", "wheels"}}, {{"fuselage", kGreen}});
    b.St("glider", "Glider", "A slim body and very long wings, no engine.", {{"length", 8.0}, {"wingSpan", 18.0}, {"bodyRadius", 0.45}}, {{"fuselage", "slim"}, {"canopy", "long"}, {"wings", "low"}, {"tail", "t_tail"}, {"engine", "none"}, {"gear", "wheels"}}, {});
    b.t.generate = GenerateAircraft;
    return b.t;
}

// ======================================================================================================================
// Boat
// ======================================================================================================================
void GenerateBoat(const Recipe& r, std::vector<GeneratedPart>& parts) {
    Out o{r, parts};
    const float L = Pf(r, "length"), B = Pf(r, "beam"), H = Pf(r, "hullHeight");
    const std::string hullD = r.DesignFor("hull", "hull/0");
    const float z0 = -L * 0.5f, z1 = L * 0.5f;
    const float keel = hullD == "raft" ? 0.0f : 0.0f;
    (void)keel;
    // hull: a lofted shell from the stern to a pointed bow (a raft is a flat deck on logs)
    o.Add("hull/0", "hull", FrameAA({-B * 0.5f, 0, z0}, {B * 0.5f, H, z1}), [&](const std::string& d) {
        PolyMesh m;
        const float sk = d == "canoe" ? 0.55f : d == "speed" ? 0.85f : d == "ship" ? 1.0f : 0.9f;
        if (d == "raft") {
            const int n = 5;
            for (int k = 0; k < n; ++k) {
                const float x = -B * 0.5f + B * (float)(2 * k + 1) / (float)(2 * n);
                shapes::Append(m, shapes::Prism(AxisFrame({x, H * 0.5f, z0}, {x + 0.001f, H * 0.5f + 0.001f, z1}, B / (float)n * 0.95f, H * 0.95f), Sd(8, 6)));
            }
            return m;
        }
        const float bowPoint = d == "ship" ? 0.35f : d == "speed" ? 0.2f : 0.1f;
        std::vector<Station> st = {
            {{0, H * 0.5f, z1}, H * 0.04f, B * 0.02f},
            {{0, H * 0.5f, z1 - L * 0.08f}, H * 0.4f, B * 0.18f * sk},
            {{0, H * 0.5f, z1 - L * 0.25f}, H * 0.5f, B * 0.38f * sk},
            {{0, H * 0.5f, 0}, H * 0.5f, B * 0.5f * sk},
            {{0, H * 0.5f, z0 + L * 0.2f}, H * 0.5f, B * 0.46f * sk},
            {{0, H * 0.5f, z0 + 0.01f}, H * 0.46f, B * (0.3f + bowPoint * 0.3f) * sk}};
        return Tube(st, {0, 1, 0}, Sd(14, 8), true, false, d == "canoe" ? 2.2f : 3.0f);
    });
    // deck and cabin
    const float deckY = H * 0.95f;
    o.Add("cabin/0", "cabin", FrameAA({-B * 0.3f, deckY, z0 + L * 0.1f}, {B * 0.3f, deckY + B * 0.7f, z0 + L * 0.5f}), [&](const std::string& d) {
        if (d == "none") return PolyMesh();
        if (d == "bridge") {
            PolyMesh m = BoxAA({-B * 0.36f, deckY, z0 + L * 0.12f}, {B * 0.36f, deckY + B * 0.55f, z0 + L * 0.4f});
            shapes::Append(m, BoxAA({-B * 0.3f, deckY + B * 0.56f, z0 + L * 0.16f}, {B * 0.3f, deckY + B * 0.85f, z0 + L * 0.34f}));
            return m;
        }
        return Plan({{-B * 0.28f, z0 + L * 0.12f}, {B * 0.28f, z0 + L * 0.12f}, {B * 0.22f, z0 + L * 0.5f}, {-B * 0.22f, z0 + L * 0.5f}}, deckY, deckY + B * 0.55f);
    });
    // mast and sails
    const float mh = Pf(r, "mastHeight");
    o.Add("mast/0", "mast", FrameAA({-0.1f, deckY, -0.1f}, {0.1f, deckY + mh, 0.1f}), [&](const std::string& d) {
        if (d == "none") return PolyMesh();
        PolyMesh m = Cyl(0, deckY - 0.02f, deckY + mh, z1 * 0.25f, std::max(0.04f, B * 0.04f), std::max(0.04f, B * 0.04f), Sd(8, 6));
        if (d == "two") shapes::Append(m, Cyl(0, deckY - 0.02f, deckY + mh * 0.8f, z0 * 0.45f + 0.003f, std::max(0.035f, B * 0.035f), std::max(0.035f, B * 0.035f), Sd(8, 6)));
        return m;
    });
    o.Add("sail/0", "sail", FrameAA({-0.05f, deckY + 0.3f, z0}, {0.05f, deckY + mh, z1}), [&](const std::string& d) {
        PolyMesh m;
        if (d == "none") return m;
        const float zm = z1 * 0.25f;
        shapes::Append(m, Side({{zm - 0.04f, deckY + mh * 0.95f}, {zm - 0.04f, deckY + mh * 0.18f}, {zm - L * 0.34f, deckY + mh * 0.2f}}, -0.02f, 0.02f));   // the main sail
        shapes::Append(m, Side({{zm + 0.05f, deckY + mh * 0.9f}, {zm + L * 0.28f, deckY + mh * 0.18f}, {zm + 0.05f, deckY + mh * 0.18f}}, -0.015f, 0.015f));   // the jib
        if (d == "square") shapes::Append(m, Side({{zm - 0.02f, deckY + mh * 0.96f}, {zm - 0.02f, deckY + mh * 0.5f}, {zm + L * 0.12f, deckY + mh * 0.5f}, {zm + L * 0.12f, deckY + mh * 0.96f}}, -B * 0.4f, B * 0.4f));
        return m;
    });
    // motor, funnel, rudder
    o.Add("engine/0", "engine", FrameAA({-B * 0.15f, 0, z0 - 0.3f}, {B * 0.15f, H, z0 + 0.4f}), [&](const std::string& d) {
        PolyMesh m;
        if (d == "none") return m;
        if (d == "funnel") {
            m = Cyl(0, deckY + B * 0.5f, deckY + B * 1.2f, z0 + L * 0.3f, B * 0.14f, B * 0.14f, Sd(12, 6));
            shapes::Append(m, Cyl(0, deckY + B * 1.2f - 0.001f, deckY + B * 1.3f, z0 + L * 0.3f, B * 0.17f, B * 0.17f, Sd(12, 6)));
            return m;
        }
        shapes::Append(m, BoxAA({-B * 0.08f, deckY - H * 0.5f, z0 - 0.28f}, {B * 0.08f, deckY + H * 0.2f, z0 + 0.0f}));   // the outboard
        shapes::Append(m, Limb({0, deckY - H * 0.45f, z0 - 0.22f}, {0.002f, deckY - H * 1.0f, z0 - 0.18f}, 0.04f, true, 6));
        return m;
    });
    o.Add("seat/0", "seat", FrameAA({-B * 0.4f, H * 0.7f, z0}, {B * 0.4f, H * 1.1f, z1}), [&](const std::string& d) {
        PolyMesh m;
        if (d == "none") return m;
        const int n = d == "benches" ? 3 : 2;
        for (int k = 0; k < n; ++k) {
            const float z = z0 + L * (0.3f + 0.25f * (float)k);
            shapes::Append(m, BoxAA({-B * 0.36f + 0.01f * (float)k, deckY - 0.02f, z - 0.12f}, {B * 0.36f - 0.01f * (float)k, deckY + 0.06f, z + 0.12f}));
        }
        return m;
    });
}

Template MakeBoat() {
    B b;
    b.t.id = "boat"; b.t.name = "Boat"; b.t.category = "Vehicles"; b.t.version = 1;
    b.t.description = "A boat: a hull (canoe, rowboat, speedboat, ship or raft), a cabin or bridge, a mast with sails, a motor or funnel and benches. Faces +Z.";
    b.P("length", "Length", 5.0, 1.5, 60.0, "Size", "Bow to stern.");
    b.P("beam", "Beam", 1.8, 0.5, 15.0, "Size", "Width of the hull.");
    b.P("hullHeight", "Hull height", 0.6, 0.15, 6.0, "Size", "Keel to deck.");
    b.P("mastHeight", "Mast height", 4.5, 0.5, 40.0, "Size", "Height of the mast above the deck.");
    const Color kWood{0.55f, 0.38f, 0.22f, 1}, kWhite{0.92f, 0.92f, 0.9f, 1}, kRed{0.7f, 0.15f, 0.15f, 1}, kCloth{0.9f, 0.88f, 0.8f, 1}, kMetal{0.35f, 0.36f, 0.4f, 1}, kBlue{0.2f, 0.35f, 0.6f, 1};
    b.S("hull", "Hull", {{"rowboat", "Rowboat"}, {"canoe", "Canoe"}, {"speed", "Speedboat"}, {"ship", "Ship"}, {"raft", "Raft"}}, kWood, 0);
    b.S("cabin", "Cabin", {{"none", "None"}, {"cabin", "Cabin"}, {"bridge", "Bridge"}}, kWhite, 1);
    b.S("mast", "Mast", {{"none", "None"}, {"one", "One mast"}, {"two", "Two masts"}}, kWood, 2);
    b.S("sail", "Sails", {{"none", "None"}, {"fore_aft", "Fore and aft"}, {"square", "Square rigged"}}, kCloth, 3);
    b.S("engine", "Motor", {{"none", "None"}, {"outboard", "Outboard"}, {"funnel", "Funnel"}}, kMetal, 4);
    b.S("seat", "Seats", {{"none", "None"}, {"benches", "Benches"}, {"thwarts", "Thwarts"}}, kWood, 5);
    b.St("rowboat", "Rowboat", "A small wooden boat with benches.", {{"length", 4.0}, {"beam", 1.5}, {"hullHeight", 0.5}}, {{"hull", "rowboat"}, {"cabin", "none"}, {"mast", "none"}, {"sail", "none"}, {"engine", "none"}, {"seat", "benches"}}, {});
    b.St("canoe", "Canoe", "A long narrow boat.", {{"length", 4.5}, {"beam", 0.8}, {"hullHeight", 0.35}}, {{"hull", "canoe"}, {"cabin", "none"}, {"mast", "none"}, {"sail", "none"}, {"engine", "none"}, {"seat", "thwarts"}}, {{"hull", kRed}});
    b.St("speedboat", "Speedboat", "A fast hull with a small cabin and an outboard motor.", {{"length", 6.5}, {"beam", 2.2}, {"hullHeight", 0.7}}, {{"hull", "speed"}, {"cabin", "cabin"}, {"mast", "none"}, {"sail", "none"}, {"engine", "outboard"}, {"seat", "none"}}, {{"hull", kWhite}, {"cabin", kBlue}});
    b.St("sailboat", "Sailboat", "A hull, a mast and two sails.", {{"length", 8.0}, {"beam", 2.6}, {"hullHeight", 0.9}, {"mastHeight", 9.0}}, {{"hull", "rowboat"}, {"cabin", "cabin"}, {"mast", "one"}, {"sail", "fore_aft"}, {"engine", "none"}, {"seat", "none"}}, {{"hull", kWhite}});
    b.St("tall_ship", "Tall ship", "Two masts with square sails.", {{"length", 24.0}, {"beam", 6.5}, {"hullHeight", 2.4}, {"mastHeight", 22.0}}, {{"hull", "ship"}, {"cabin", "bridge"}, {"mast", "two"}, {"sail", "square"}, {"engine", "none"}, {"seat", "none"}}, {});
    b.St("steamer", "Steamer", "A ship with a bridge and a funnel.", {{"length", 28.0}, {"beam", 7.0}, {"hullHeight", 3.0}}, {{"hull", "ship"}, {"cabin", "bridge"}, {"mast", "none"}, {"sail", "none"}, {"engine", "funnel"}, {"seat", "none"}}, {{"hull", kRed}, {"cabin", kWhite}});
    b.St("raft", "Raft", "Logs lashed together with a small sail.", {{"length", 3.0}, {"beam", 2.0}, {"hullHeight", 0.25}, {"mastHeight", 2.5}}, {{"hull", "raft"}, {"cabin", "none"}, {"mast", "one"}, {"sail", "fore_aft"}, {"engine", "none"}, {"seat", "none"}}, {});
    b.t.generate = GenerateBoat;
    return b.t;
}

// ======================================================================================================================
// Weapon
// ======================================================================================================================
enum WKind { kLongsword, kDagger, kSpear, kBow, kMace, kPistol, kRifle, kShield };
void GenerateWeapon(const Recipe& r, std::vector<GeneratedPart>& parts) {
    Out o{r, parts};
    const int kind = (int)r.Param("kind");
    const float L = Pf(r, "length"), W = Pf(r, "width");
    const bool gun = kind == kPistol || kind == kRifle;
    // the main part: a blade, a spear head, a bow, a mace head, a gun body or a shield, by kind and design
    o.Add("main/0", "main", FrameAA({-W, 0, -W}, {W, L, W}), [&](const std::string& d) {
        PolyMesh m;
        const bool fancy = d == "ornate", curved = d == "curved";
        switch (kind) {
        case kLongsword: case kDagger: {
            const float y0 = L * (kind == kDagger ? 0.3f : 0.22f), bw = W * (kind == kDagger ? 0.6f : 0.5f) * (d == "broad" ? 1.5f : 1.0f);
            const float bend = curved ? W * 0.7f : 0.0f;
            std::vector<Station> st = {{{0, y0, 0}, 0.006f, bw}, {{bend * 0.4f, y0 + (L - y0) * 0.4f, 0}, 0.006f, bw * 0.95f}, {{bend, y0 + (L - y0) * 0.8f, 0}, 0.005f, bw * 0.7f}, {{bend * 1.3f, L - 0.012f, 0}, 0.003f, bw * 0.22f}};
            m = Tube(st, {0, 0, 1}, 4, false, true, 2.0f);
            if (fancy) shapes::Append(m, BoxAA({-0.003f, y0 + 0.02f, -0.016f * 0.5f}, {0.003f, y0 + (L - y0) * 0.5f, 0.016f * 0.5f}));   // a fuller
            break;
        }
        case kSpear: {
            const float hl = L * 0.16f;
            m = Cyl(0, L * 0.02f, L - hl, 0, 0.014f, 0.014f, Sd(8, 6));
            shapes::Append(m, Cone({0, L - hl - 0.001f, 0}, {0, L, 0}, d == "broad" ? 0.045f : 0.028f, Sd(6, 4)));
            if (fancy) shapes::Append(m, Cyl(0, L - hl - 0.06f, L - hl - 0.03f, 0, 0.026f, 0.026f, Sd(8, 6)));
            break;
        }
        case kBow: {
            const int n = Sd(10, 6);
            for (int k = 0; k < n; ++k) {
                const float t0 = (float)k / (float)n, t1 = (float)(k + 1) / (float)n;
                const float x0 = -std::sin(3.14159f * t0) * L * 0.18f, x1 = -std::sin(3.14159f * t1) * L * 0.18f;
                shapes::Append(m, Limb({x0, L * t0, 0}, {x1, L * t1, 0}, 0.018f * (1.0f - 0.35f * std::fabs(2.0f * t0 - 1.0f)), true, 6));
            }
            shapes::Append(m, Limb({0.004f, 0.0f, 0.0f}, {0.004f, L, 0.001f}, 0.004f, true, 4));   // the string
            break;
        }
        case kMace: {
            const float hy = L * 0.82f, hr = W * 0.9f;
            m = Cyl(0, L * 0.02f, hy, 0, 0.016f, 0.016f, Sd(8, 6));
            shapes::Append(m, Ell({0, hy + hr * 0.7f, 0}, hr, hr * 1.1f, hr, 5, 10));
            if (d != "plain") for (int k = 0; k < 8; ++k) {
                const float a = 6.2831853f * (float)k / 8.0f;
                shapes::Append(m, Cone({std::cos(a) * hr * 0.9f, hy + hr * 0.7f, std::sin(a) * hr * 0.9f}, {std::cos(a) * hr * 1.5f, hy + hr * 0.7f, std::sin(a) * hr * 1.5f}, 0.016f, 5));
            }
            break;
        }
        case kShield: {
            const float sw = W * 1.4f;
            const std::vector<Vec2> prof = d == "round" ? std::vector<Vec2>{{-sw * 0.5f, L * 0.1f}, {-sw, L * 0.3f}, {-sw, L * 0.7f}, {-sw * 0.5f, L * 0.95f}, {sw * 0.5f, L * 0.95f}, {sw, L * 0.7f}, {sw, L * 0.3f}, {sw * 0.5f, L * 0.1f}}
                                           : std::vector<Vec2>{{-sw, L * 0.95f}, {-sw, L * 0.35f}, {0, L * 0.02f}, {sw, L * 0.35f}, {sw, L * 0.95f}};
            std::vector<Vec2> p = prof;
            FixCcw(p);
            m = shapes::ExtrudeAxes({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, p, 0.03f);
            shapes::Append(m, Ell({0, L * 0.58f, 0.026f}, sw * 0.28f, sw * 0.28f, 0.03f, 4, 10));   // the boss
            break;
        }
        default: {   // a gun: the body lies along z
            const float bl = kind == kRifle ? L : L * 0.8f;
            m = BoxAA({-0.02f, 0.06f, -bl * 0.5f}, {0.02f, 0.11f, bl * 0.18f});                       // the receiver
            shapes::Append(m, Limb({0, 0.095f, bl * 0.18f - 0.002f}, {0, 0.095f, bl * 0.5f}, kind == kRifle ? 0.012f : 0.014f, true, Sd(10, 6)));   // the barrel
            if (kind == kRifle) shapes::Append(m, BoxAA({-0.016f, 0.0f, -bl * 0.5f - 0.0f}, {0.016f, 0.075f, -bl * 0.18f}));                          // the stock
            if (fancy) shapes::Append(m, BoxAA({-0.008f, 0.115f, -bl * 0.1f}, {0.008f, 0.14f, bl * 0.1f}));                                            // a sight rail
            break;
        }
        }
        return m;
    });
    o.Add("grip/0", "grip", FrameAA({-0.03f, 0, -0.03f}, {0.03f, L * 0.2f, 0.03f}), [&](const std::string& d) {
        PolyMesh m;
        if (d == "none") return m;
        if (kind == kLongsword || kind == kDagger) {
            const float gl = L * (kind == kDagger ? 0.2f : 0.18f);
            m = Cyl(0, 0.015f, gl, 0, 0.016f, 0.016f, Sd(8, 6));
            if (d == "wrapped") for (int k = 0; k < 4; ++k) shapes::Append(m, Cyl(0, 0.03f + gl * 0.2f * (float)k, 0.045f + gl * 0.2f * (float)k, 0, 0.0195f, 0.0195f, Sd(8, 6)));
        } else if (kind == kPistol) {
            m = BoxAA({-0.016f, -0.1f, -0.095f * (L / 0.3f)}, {0.016f, 0.07f, -0.05f * (L / 0.3f)});
            shapes::Append(m, BoxAA({-0.005f, 0.03f, -0.045f}, {0.005f, 0.065f, -0.03f}));
        } else if (kind == kRifle) {
            m = BoxAA({-0.014f, 0.01f, -L * 0.18f + 0.01f}, {0.014f, 0.07f, -L * 0.14f});
        } else if (kind == kMace || kind == kSpear) {
            m = Cyl(0, 0.04f, L * 0.2f, 0, 0.022f, 0.022f, Sd(8, 6));
        } else if (kind == kShield) {
            m = BoxAA({-0.05f, L * 0.45f, -0.07f}, {0.05f, L * 0.5f, -0.02f});
        }
        return m;
    });
    o.Add("guard/0", "guard", FrameAA({-W, 0, -0.05f}, {W, L * 0.3f, 0.05f}), [&](const std::string& d) {
        PolyMesh m;
        if (d == "none" || (kind != kLongsword && kind != kDagger)) return m;
        const float y = L * (kind == kDagger ? 0.2f : 0.18f);
        if (d == "disc") return Cyl(0, y, y + 0.012f, 0, W * 0.8f, W * 0.8f, Sd(14, 8));
        m = BoxAA({-W * 0.9f, y, -0.012f}, {W * 0.9f, y + 0.02f, 0.012f});
        for (int s = -1; s <= 1; s += 2) shapes::Append(m, Ell({s * W * 0.9f, y + 0.01f, 0}, 0.018f, 0.018f, 0.018f, 3, 8));
        return m;
    });
    o.Add("pommel/0", "pommel", FrameAA({-0.03f, -0.03f, -0.03f}, {0.03f, 0.04f, 0.03f}), [&](const std::string& d) {
        if (d == "none" || (kind != kLongsword && kind != kDagger && kind != kMace)) return PolyMesh();
        if (d == "cap") return Cyl(0, 0.0f, 0.025f, 0, 0.022f, 0.022f, Sd(10, 6));
        return Ell({0, 0.012f, 0}, 0.026f, 0.026f, 0.026f, 4, 8);
    });
    (void)gun;
}

Template MakeWeapon() {
    B b;
    b.t.id = "weapon"; b.t.name = "Weapon"; b.t.category = "Props"; b.t.version = 1;
    b.t.description = "A weapon or shield: sword, dagger, spear, bow, mace, pistol, rifle or shield, with a grip, a guard and a pommel. Blades, spears, bows and maces stand upright; guns lie along Z.";
    { ParamDef p; p.id = "kind"; p.label = "Kind"; p.kind = ParamDef::Kind::Choice; p.def = 0; p.min = 0; p.max = 7; p.group = "Size"; p.tip = "What it is."; p.choices = {"Sword", "Dagger", "Spear", "Bow", "Mace", "Pistol", "Rifle", "Shield"}; b.t.params.push_back(p); }
    b.P("length", "Length", 1.0, 0.15, 2.6, "Size", "Overall length (height for a shield).");
    b.P("width", "Width", 0.12, 0.04, 0.6, "Size", "Width of the guard, the head or the shield.");
    const Color kSteel{0.68f, 0.7f, 0.74f, 1}, kLeather{0.35f, 0.22f, 0.14f, 1}, kGold{0.8f, 0.65f, 0.2f, 1}, kWood{0.55f, 0.38f, 0.22f, 1}, kDark{0.18f, 0.18f, 0.2f, 1}, kRed{0.7f, 0.15f, 0.15f, 1};
    b.S("main", "Main part", {{"plain", "Plain"}, {"broad", "Broad"}, {"curved", "Curved"}, {"ornate", "Ornate"}, {"round", "Round (shield)"}}, kSteel, 0);
    b.S("grip", "Grip", {{"wrapped", "Wrapped"}, {"smooth", "Smooth"}, {"none", "None"}}, kLeather, 1);
    b.S("guard", "Guard", {{"cross", "Cross"}, {"disc", "Disc"}, {"none", "None"}}, kGold, 2);
    b.S("pommel", "Pommel", {{"ball", "Ball"}, {"cap", "Cap"}, {"none", "None"}}, kGold, 3);
    b.St("longsword", "Longsword", "A straight blade, a cross guard and a wrapped grip.", {{"kind", 0}, {"length", 1.1}, {"width", 0.12}}, {{"main", "plain"}, {"grip", "wrapped"}, {"guard", "cross"}, {"pommel", "ball"}}, {});
    b.St("saber", "Curved sabre", "A curved blade and a disc guard.", {{"kind", 0}, {"length", 1.0}, {"width", 0.1}}, {{"main", "curved"}, {"grip", "wrapped"}, {"guard", "disc"}, {"pommel", "cap"}}, {});
    b.St("dagger", "Dagger", "A short blade.", {{"kind", 1}, {"length", 0.38}, {"width", 0.07}}, {{"main", "plain"}, {"grip", "wrapped"}, {"guard", "cross"}, {"pommel", "ball"}}, {});
    b.St("spear", "Spear", "A long shaft with a leaf head.", {{"kind", 2}, {"length", 2.1}, {"width", 0.1}}, {{"main", "broad"}, {"grip", "none"}, {"guard", "none"}, {"pommel", "none"}}, {{"main", kWood}});
    b.St("bow", "Bow", "A curved wooden bow with a string.", {{"kind", 3}, {"length", 1.3}, {"width", 0.2}}, {{"main", "plain"}, {"grip", "smooth"}, {"guard", "none"}, {"pommel", "none"}}, {{"main", kWood}});
    b.St("mace", "Mace", "A haft and a spiked head.", {{"kind", 4}, {"length", 0.75}, {"width", 0.1}}, {{"main", "ornate"}, {"grip", "smooth"}, {"guard", "none"}, {"pommel", "ball"}}, {{"main", kDark}, {"grip", kWood}});
    b.St("pistol", "Pistol", "A short gun.", {{"kind", 5}, {"length", 0.3}, {"width", 0.06}}, {{"main", "plain"}, {"grip", "smooth"}, {"guard", "none"}, {"pommel", "none"}}, {{"main", kDark}, {"grip", kWood}});
    b.St("rifle", "Rifle", "A long gun with a stock.", {{"kind", 6}, {"length", 1.1}, {"width", 0.06}}, {{"main", "ornate"}, {"grip", "smooth"}, {"guard", "none"}, {"pommel", "none"}}, {{"main", kDark}, {"grip", kWood}});
    b.St("kite_shield", "Kite shield", "A pointed shield.", {{"kind", 7}, {"length", 1.0}, {"width", 0.3}}, {{"main", "plain"}, {"grip", "smooth"}, {"guard", "none"}, {"pommel", "none"}}, {{"main", kRed}, {"grip", kLeather}});
    b.St("round_shield", "Round shield", "A round shield with a boss.", {{"kind", 7}, {"length", 0.8}, {"width", 0.3}}, {{"main", "round"}, {"grip", "smooth"}, {"guard", "none"}, {"pommel", "none"}}, {{"main", kWood}, {"grip", kLeather}});
    b.t.generate = GenerateWeapon;
    return b.t;
}

// ======================================================================================================================
// Tower
// ======================================================================================================================
void GenerateTower(const Recipe& r, std::vector<GeneratedPart>& parts) {
    Out o{r, parts};
    const float H = Pf(r, "height"), R = Pf(r, "radius");
    const std::string shaftD = r.DesignFor("shaft", "shaft/0"), topD = r.DesignFor("top", "top/0");
    const bool square = shaftD == "square" || shaftD == "square_tapered";
    const bool tapered = shaftD == "tapered" || shaftD == "square_tapered" || shaftD == "lattice";
    const int sides = shaftD == "octagon" ? 8 : Sd(20, 8);
    const float bh = std::min(H * 0.08f, 1.0f);
    o.Add("base/0", "base", FrameAA({-R * 1.25f, 0, -R * 1.25f}, {R * 1.25f, bh, R * 1.25f}), [&](const std::string& d) {
        if (d == "none") return PolyMesh();
        if (square) return BoxAA({-R * 1.2f, 0, -R * 1.2f}, {R * 1.2f, bh, R * 1.2f});
        return Cyl(0, 0, bh, 0, R * 1.25f, R * 1.25f, sides);
    });
    const float sy0 = bh * 0.9f, sy1 = H * 0.86f;
    const float rt = tapered ? R * 0.62f : R;
    const bool legs = shaftD == "legs";
    o.Add("shaft/0", "shaft", FrameAA({-R, sy0, -R}, {R, sy1, R}), [&](const std::string&) {
        PolyMesh m;
        if (legs) {   // four braced legs (a water tower or a watch tower)
            const float lh = topD == "tank" ? H * 0.55f : sy1 + 0.02f;
            for (int sx = -1; sx <= 1; sx += 2)
                for (int sz = -1; sz <= 1; sz += 2) {
                    const float k = 0.98f;
                    shapes::Append(m, Limb({sx * R * 1.1f * k, 0, sz * R * 1.1f * k}, {sx * R * 0.85f, lh, sz * R * 0.85f}, std::max(0.07f, R * 0.07f), false));
                }
            for (int k = 1; k <= 2; ++k) {
                const float y = lh * (float)k / 3.0f, w = R * (1.05f - 0.09f * (float)k);
                for (int s = -1; s <= 1; s += 2) {
                    shapes::Append(m, Limb({-w, y, s * w + 0.002f}, {w, y + 0.001f, s * w}, 0.035f, false));
                    shapes::Append(m, Limb({s * w + 0.002f, y + 0.004f, -w}, {s * w, y + 0.005f, w}, 0.035f, false));
                }
            }
            return m;
        }
        if (square) {
            const float k = tapered ? 0.62f : 1.0f;
            std::vector<Station> st = {{{0, sy0, 0}, R, R}, {{0, sy1, 0}, R * k, R * k}};
            return Tube(st, {0, 0, 1}, 4, false, false, 8.0f);   // a very square section
        }
        std::vector<Station> st = {{{0, sy0, 0}, R, R}, {{0, Lerp0(sy0, sy1, 0.5f), 0}, (R + rt) * 0.5f, (R + rt) * 0.5f}, {{0, sy1, 0}, rt, rt}};
        return Tube(st, {0, 0, 1}, sides, false, false);
    });
    // a door and windows on the front
    o.Add("door/0", "door", FrameAA({-0.35f, bh, R * 0.9f}, {0.35f, bh + 1.8f, R * 1.3f}), [&](const std::string& d) {
        if (d == "none" || legs) return PolyMesh();
        const float zf = square ? R * 1.0f : R * 0.98f;
        PolyMesh m = BoxAA({-0.32f, bh, zf - 0.01f}, {0.32f, bh + std::min(1.7f, H * 0.18f), zf + 0.05f});
        shapes::Append(m, Ell({0, bh + std::min(1.7f, H * 0.18f), zf + 0.02f}, 0.32f, 0.2f, 0.035f, 3, 8));
        return m;
    });
    o.Add("windows/0", "windows", FrameAA({-R, H * 0.3f, -R}, {R, H * 0.8f, R * 1.2f}), [&](const std::string& d) {
        PolyMesh m;
        if (d == "none" || legs) return m;
        const int n = d == "many" ? 4 : 2;
        for (int k = 0; k < n; ++k) {
            const float y = sy0 + (sy1 - sy0) * (0.3f + 0.6f * (float)k / (float)std::max(1, n - 1)) * 0.95f;
            const float rr = tapered ? Lerp0(R, rt, (y - sy0) / (sy1 - sy0)) : R;
            const float a = 0.9f * (float)k;
            const Vec3 c{std::sin(a) * rr, y, std::cos(a) * rr};
            shapes::Append(m, BoxAA({c.x - 0.1f, y - 0.22f, c.z - 0.02f + (square ? 0.0f : 0.0f)}, {c.x + 0.1f, y + 0.22f, c.z + 0.06f}));
        }
        return m;
    });
    // the top: battlements, a conical or pyramid roof, a lantern room, a platform or a tank
    const float ty = sy1;
    o.Add("top/0", "top", FrameAA({-R * 1.4f, ty - 0.1f, -R * 1.4f}, {R * 1.4f, H, R * 1.4f}), [&](const std::string& d) {
        PolyMesh m;
        const float rr = rt * 1.12f;
        if (d == "battlements") {
            m = Cyl(0, ty - 0.02f, ty + H * 0.035f, 0, rr * 1.1f, rr * 1.1f, sides);
            const int n = 10;
            for (int k = 0; k < n; ++k) {
                const float a = 6.2831853f * (float)k / (float)n;
                const float x = std::cos(a) * rr * 1.02f, z = std::sin(a) * rr * 1.02f;
                shapes::Append(m, BoxAA({x - rr * 0.18f, ty + H * 0.035f - 0.001f, z - rr * 0.18f}, {x + rr * 0.18f, ty + H * 0.035f + H * 0.045f, z + rr * 0.18f}));
            }
        } else if (d == "cone") {
            m = Cyl(0, ty - 0.02f, ty + 0.12f, 0, rr * 1.15f, rr * 1.15f, sides);
            shapes::Append(m, Cone({0, ty + 0.11f, 0}, {0, H, 0}, rr * 1.25f, sides));
        } else if (d == "pyramid") {
            m = BoxAA({-rr * 1.15f, ty - 0.02f, -rr * 1.15f}, {rr * 1.15f, ty + 0.12f, rr * 1.15f});
            shapes::Append(m, Cone({0, ty + 0.11f, 0}, {0, H, 0}, rr * 1.4f, 4));
        } else if (d == "lantern") {   // a gallery, a glass room and a dome (a lighthouse)
            m = Cyl(0, ty - 0.02f, ty + 0.1f, 0, rr * 1.5f, rr * 1.5f, sides);
            shapes::Append(m, Cyl(0, ty + 0.099f, ty + (H - ty) * 0.65f, 0, rr * 0.8f, rr * 0.8f, sides));
            shapes::Append(m, Ell({0, ty + (H - ty) * 0.66f, 0}, rr * 1.0f, (H - ty) * 0.35f, rr * 1.0f, 5, Sd(14, 8)));
        } else if (d == "tank") {   // a water tank on top of the legs
            const float ly = H * 0.55f;
            m = Cyl(0, ly, H * 0.88f, 0, R * 1.35f, R * 1.35f, sides);
            shapes::Append(m, Cone({0, H * 0.88f - 0.001f, 0}, {0, H, 0}, R * 1.38f, sides));
        } else if (d == "dish") {   // a radio tower's antenna
            m = Cyl(0, ty - 0.02f, ty + 0.5f, 0, rt * 0.7f, rt * 0.7f, 10);
            shapes::Append(m, Ell({0, ty + 0.8f, 0.2f}, rt * 3.0f, rt * 3.0f, 0.07f, 4, 12));
            shapes::Append(m, Cyl(0, ty + 0.5f, H, 0, 0.04f, 0.04f, 6));
        } else if (d == "hut") {   // a roofed watch room
            m = BoxAA({-R * 1.5f, ty, -R * 1.5f}, {R * 1.5f, ty + R * 1.4f, R * 1.5f});
            shapes::Append(m, Cone({0, ty + R * 1.39f, 0}, {0, H, 0}, R * 2.1f, 4));
        } else {   // platform
            m = BoxAA({-R * 1.3f, ty - 0.02f, -R * 1.3f}, {R * 1.3f, ty + 0.12f, R * 1.3f});
        }
        return m;
    });
}

Template MakeTower() {
    B b;
    b.t.id = "tower"; b.t.name = "Tower"; b.t.category = "Architecture"; b.t.version = 1;
    b.t.description = "A tower: a base, a shaft (round, square, octagon, tapered or on legs), a door, windows and a top (battlements, cone, pyramid, lantern room, tank, dish or hut).";
    b.P("height", "Height", 14.0, 3.0, 80.0, "Size", "Overall height.");
    b.P("radius", "Radius", 2.0, 0.6, 8.0, "Size", "Half the width of the shaft at the bottom.");
    const Color kStone{0.62f, 0.62f, 0.6f, 1}, kWood{0.55f, 0.38f, 0.22f, 1}, kRoof{0.6f, 0.25f, 0.2f, 1}, kWhite{0.92f, 0.92f, 0.9f, 1}, kRed{0.75f, 0.15f, 0.15f, 1}, kDark{0.25f, 0.2f, 0.16f, 1}, kMetal{0.55f, 0.58f, 0.62f, 1}, kGlass{0.8f, 0.9f, 0.95f, 1};
    b.S("base", "Base", {{"plinth", "Plinth"}, {"none", "None"}}, kStone, 0);
    b.S("shaft", "Shaft", {{"round", "Round"}, {"tapered", "Round, tapered"}, {"square", "Square"}, {"square_tapered", "Square, tapered"}, {"octagon", "Octagon"}, {"legs", "On legs"}}, kStone, 1);
    b.S("door", "Door", {{"arched", "Arched"}, {"none", "None"}}, kDark, 2);
    b.S("windows", "Windows", {{"few", "Two"}, {"many", "Four"}, {"none", "None"}}, kDark, 3);
    b.S("top", "Top", {{"battlements", "Battlements"}, {"cone", "Conical roof"}, {"pyramid", "Pyramid roof"}, {"lantern", "Lantern room"}, {"tank", "Water tank"}, {"dish", "Dish"}, {"hut", "Watch hut"}, {"platform", "Platform"}}, kRoof, 4);
    b.St("castle_tower", "Castle tower", "A round stone tower with battlements.", {{"height", 14.0}, {"radius", 2.4}}, {{"base", "plinth"}, {"shaft", "round"}, {"door", "arched"}, {"windows", "few"}, {"top", "battlements"}}, {{"top", kStone}});
    b.St("round_keep", "Round tower", "A round tower with a conical roof.", {{"height", 16.0}, {"radius", 2.2}}, {{"base", "plinth"}, {"shaft", "round"}, {"door", "arched"}, {"windows", "many"}, {"top", "cone"}}, {});
    b.St("lighthouse", "Lighthouse", "A tapered white tower with a lantern room.", {{"height", 22.0}, {"radius", 2.6}}, {{"base", "plinth"}, {"shaft", "tapered"}, {"door", "arched"}, {"windows", "few"}, {"top", "lantern"}}, {{"shaft", kWhite}, {"top", kRed}});
    b.St("bell_tower", "Bell tower", "A square tower with a pyramid roof.", {{"height", 18.0}, {"radius", 2.0}}, {{"base", "plinth"}, {"shaft", "square"}, {"door", "arched"}, {"windows", "many"}, {"top", "pyramid"}}, {});
    b.St("water_tower", "Water tower", "A tank on four braced legs.", {{"height", 16.0}, {"radius", 2.6}}, {{"base", "none"}, {"shaft", "legs"}, {"door", "none"}, {"windows", "none"}, {"top", "tank"}}, {{"shaft", kMetal}, {"top", kMetal}});
    b.St("watch_tower", "Watch tower", "A wooden tower on legs with a roofed hut.", {{"height", 9.0}, {"radius", 1.6}}, {{"base", "none"}, {"shaft", "legs"}, {"door", "none"}, {"windows", "none"}, {"top", "hut"}}, {{"shaft", kWood}, {"top", kWood}});
    b.St("radio_mast", "Radio tower", "A tall tapered mast with a dish.", {{"height", 30.0}, {"radius", 1.2}}, {{"base", "plinth"}, {"shaft", "square_tapered"}, {"door", "none"}, {"windows", "none"}, {"top", "dish"}}, {{"shaft", kMetal}, {"top", kWhite}});
    b.St("octagon_tower", "Octagon tower", "An eight-sided tower with a flat platform.", {{"height", 12.0}, {"radius", 2.2}}, {{"base", "plinth"}, {"shaft", "octagon"}, {"door", "arched"}, {"windows", "many"}, {"top", "platform"}}, {});
    b.t.generate = GenerateTower;
    return b.t;
}

// ======================================================================================================================
// Lamp
// ======================================================================================================================
void GenerateLamp(const Recipe& r, std::vector<GeneratedPart>& parts) {
    Out o{r, parts};
    const float H = Pf(r, "height"), W = Pf(r, "shadeWidth");
    const std::string baseD = r.DesignFor("base", "base/0"), stemD = r.DesignFor("stem", "stem/0"), shadeD = r.DesignFor("shade", "shade/0");
    const bool hanging = baseD == "none" && stemD == "chain";
    const float by = baseD == "none" ? 0.0f : std::max(0.02f, H * 0.05f);
    const float br = std::max(0.07f, W * 0.34f);
    o.Add("base/0", "base", FrameAA({-br, 0, -br}, {br, by, br}), [&](const std::string& d) {
        if (d == "none") return PolyMesh();
        if (d == "square") return BoxAA({-br, 0, -br}, {br, by, br});
        PolyMesh m = Cyl(0, 0, by, 0, br, br, Sd(16, 8));
        if (d == "tripod") { m = PolyMesh(); for (int k = 0; k < 3; ++k) { const float a = 6.2831853f * (float)k / 3.0f; shapes::Append(m, Limb({std::cos(a) * br * 0.2f, by * 1.2f, std::sin(a) * br * 0.2f}, {std::cos(a) * br * 1.3f, 0, std::sin(a) * br * 1.3f}, 0.012f, true, 6)); } }
        return m;
    });
    const float sy0 = by - 0.003f, sy1 = H * (shadeD == "lantern" ? 0.7f : 0.74f);
    o.Add("stem/0", "stem", FrameAA({-0.03f, sy0, -0.03f}, {0.03f, sy1, 0.03f}), [&](const std::string& d) {
        PolyMesh m;
        if (d == "none") return m;
        const float sr = std::max(0.008f, H * 0.012f);
        if (d == "arm") {   // an angled desk-lamp arm
            m = Limb({0, sy0, 0}, {0, sy0 + (sy1 - sy0) * 0.55f, -0.02f}, sr, true, 8);
            shapes::Append(m, Limb({0, sy0 + (sy1 - sy0) * 0.55f - 0.01f, -0.02f}, {W * 0.35f, sy1, 0.02f}, sr, true, 8));
            shapes::Append(m, Ell({0, sy0 + (sy1 - sy0) * 0.55f, -0.02f}, sr * 1.8f, sr * 1.8f, sr * 1.8f, 3, 8));
            return m;
        }
        if (d == "chain") {
            const int n = 8;
            for (int k = 0; k < n; ++k) {
                const float y0 = H * (0.74f + 0.26f * (float)k / (float)n) + 0.003f * (float)k, y1 = H * (0.74f + 0.26f * (float)(k + 1) / (float)n);
                shapes::Append(m, Ell({0, (y0 + y1) * 0.5f, 0}, sr * 0.9f, (y1 - y0) * 0.6f, sr * (k % 2 ? 0.5f : 0.9f), 3, 6));
            }
            return m;
        }
        const float cr = d == "column" ? sr * 1.8f : sr;
        m = Cyl(0, sy0, sy1, 0, cr, cr, Sd(10, 6));
        if (d == "column") for (int k = 0; k < 3; ++k) shapes::Append(m, Ell({0, sy0 + (sy1 - sy0) * (0.25f + 0.25f * (float)k), 0}, cr * 1.7f, cr * 0.8f, cr * 1.7f, 3, 10));
        return m;
    });
    const float shy = sy1;
    o.Add("shade/0", "shade", FrameAA({-W * 0.5f, shy - W * 0.3f, -W * 0.5f}, {W * 0.5f, shy + W * 0.5f, W * 0.5f}), [&](const std::string& d) {
        PolyMesh m;
        if (d == "none") return m;
        const float xo = stemD == "arm" ? W * 0.35f : 0.0f;
        if (d == "cone") {
            std::vector<Station> st = {{{xo, shy - W * 0.28f, 0.02f * (stemD == "arm" ? 1 : 0)}, W * 0.5f, W * 0.5f}, {{xo, shy + W * 0.3f, 0.02f * (stemD == "arm" ? 1 : 0)}, W * 0.22f, W * 0.22f}};
            return Tube(st, {0, 0, 1}, Sd(16, 8), false, false);
        }
        if (d == "drum") return Cyl(xo, shy - W * 0.2f, shy + W * 0.25f, 0, W * 0.45f, W * 0.45f, Sd(16, 8));
        if (d == "globe") return Ell({xo, shy, 0}, W * 0.4f, W * 0.4f, W * 0.4f, 6, 14);
        if (d == "lantern") {   // a frame, a glass box and a cap
            m = BoxAA({-W * 0.3f, shy - W * 0.45f, -W * 0.3f}, {W * 0.3f, shy + W * 0.3f, W * 0.3f});
            shapes::Append(m, Cone({0, shy + W * 0.29f, 0}, {0, shy + W * 0.6f, 0}, W * 0.42f, 4));
            return m;
        }
        // candle arms: a ring with candles (a chandelier)
        const int n = 6;
        m = Cyl(0, shy - 0.03f, shy, 0, W * 0.45f, W * 0.45f, Sd(18, 8));
        for (int k = 0; k < n; ++k) {
            const float a = 6.2831853f * (float)k / (float)n;
            shapes::Append(m, Cyl(std::cos(a) * W * 0.42f, shy + 0.0f, shy + 0.12f, std::sin(a) * W * 0.42f, 0.018f, 0.018f, 8));
            shapes::Append(m, Cone({std::cos(a) * W * 0.42f, shy + 0.119f, std::sin(a) * W * 0.42f}, {std::cos(a) * W * 0.42f, shy + 0.17f, std::sin(a) * W * 0.42f}, 0.012f, 6));
        }
        return m;
    });
    o.Add("bulb/0", "bulb", FrameAA({-0.03f, shy - 0.06f, -0.03f}, {0.03f, shy + 0.04f, 0.03f}), [&](const std::string& d) {
        if (d == "none" || shadeD == "none" || shadeD == "candles") return PolyMesh();
        const float xo = stemD == "arm" ? W * 0.35f : 0.0f;
        return Ell({xo, shy - W * 0.08f, stemD == "arm" ? 0.02f : 0.0f}, 0.035f, 0.045f, 0.035f, 4, 8);
    });
    (void)hanging;
}

Template MakeLamp() {
    B b;
    b.t.id = "lamp"; b.t.name = "Lamp"; b.t.category = "Props"; b.t.version = 1;
    b.t.description = "A lamp: a base, a stem (rod, column, arm or chain), a shade (cone, drum, globe, lantern or candle ring) and a bulb. Table, floor, desk, hanging and hand lamps.";
    b.P("height", "Height", 0.5, 0.15, 3.0, "Size", "Overall height (a hanging lamp hangs from this height).");
    b.P("shadeWidth", "Shade width", 0.3, 0.08, 1.2, "Size", "Width of the shade.");
    const Color kBrass{0.75f, 0.6f, 0.25f, 1}, kWhite{0.95f, 0.92f, 0.8f, 1}, kDark{0.2f, 0.2f, 0.22f, 1}, kGlass{0.95f, 0.9f, 0.6f, 1}, kWood{0.5f, 0.35f, 0.2f, 1}, kRed{0.7f, 0.2f, 0.2f, 1};
    b.S("base", "Base", {{"round", "Round"}, {"square", "Square"}, {"tripod", "Tripod"}, {"none", "None"}}, kBrass, 0);
    b.S("stem", "Stem", {{"rod", "Rod"}, {"column", "Column"}, {"arm", "Desk arm"}, {"chain", "Chain"}, {"none", "None"}}, kBrass, 1);
    b.S("shade", "Shade", {{"cone", "Cone"}, {"drum", "Drum"}, {"globe", "Globe"}, {"lantern", "Lantern"}, {"candles", "Candle ring"}}, kWhite, 2);
    b.S("bulb", "Bulb", {{"bulb", "Bulb"}, {"none", "None"}}, kGlass, 3);
    b.St("table_lamp", "Table lamp", "A round base, a column and a cone shade.", {{"height", 0.5}, {"shadeWidth", 0.3}}, {{"base", "round"}, {"stem", "column"}, {"shade", "cone"}, {"bulb", "bulb"}}, {});
    b.St("floor_lamp", "Floor lamp", "A tall rod with a drum shade.", {{"height", 1.6}, {"shadeWidth", 0.4}}, {{"base", "round"}, {"stem", "rod"}, {"shade", "drum"}, {"bulb", "bulb"}}, {});
    b.St("desk_lamp", "Desk lamp", "An angled arm and a cone shade.", {{"height", 0.45}, {"shadeWidth", 0.18}}, {{"base", "square"}, {"stem", "arm"}, {"shade", "cone"}, {"bulb", "bulb"}}, {{"base", kDark}, {"stem", kDark}, {"shade", kRed}});
    b.St("globe_lamp", "Globe lamp", "A glass globe on a tripod.", {{"height", 1.1}, {"shadeWidth", 0.32}}, {{"base", "tripod"}, {"stem", "rod"}, {"shade", "globe"}, {"bulb", "bulb"}}, {{"shade", Color{0.95f, 0.95f, 0.9f, 1}}});
    b.St("hand_lantern", "Lantern", "A glass lantern on a short stem.", {{"height", 0.4}, {"shadeWidth", 0.22}}, {{"base", "round"}, {"stem", "rod"}, {"shade", "lantern"}, {"bulb", "bulb"}}, {{"shade", kDark}});
    b.St("chandelier", "Chandelier", "A candle ring hanging on a chain.", {{"height", 1.4}, {"shadeWidth", 0.9}}, {{"base", "none"}, {"stem", "chain"}, {"shade", "candles"}, {"bulb", "none"}}, {});
    b.t.generate = GenerateLamp;
    return b.t;
}

// ======================================================================================================================
// Tent and camp
// ======================================================================================================================
void GenerateTent(const Recipe& r, std::vector<GeneratedPart>& parts) {
    Out o{r, parts};
    const float W = Pf(r, "width"), D = Pf(r, "depth"), H = Pf(r, "height");
    const std::string bodyD = r.DesignFor("body", "body/0");
    o.Add("floor/0", "floor", FrameAA({-W * 0.5f, 0, -D * 0.5f}, {W * 0.5f, 0.04f, D * 0.5f}), [&](const std::string& d) {
        if (d == "none") return PolyMesh();
        if (bodyD == "tipi" || bodyD == "dome" || bodyD == "yurt") return Cyl(0, 0, 0.04f, 0, W * 0.5f, D * 0.5f, Sd(18, 8));
        return BoxAA({-W * 0.5f, 0, -D * 0.5f}, {W * 0.5f, 0.04f, D * 0.5f});
    });
    o.Add("body/0", "body", FrameAA({-W * 0.5f, 0, -D * 0.5f}, {W * 0.5f, H, D * 0.5f}), [&](const std::string& d) {
        PolyMesh m;
        const float w = W * 0.5f - 0.02f, dd = D * 0.5f - 0.02f;
        (void)dd;
        if (d == "a_frame") {   // two sloped sides closing in a prism along z
            m = shapes::ExtrudeAxes({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, std::vector<Vec2>{{-w, 0.04f}, {w, 0.04f}, {0.0f, H}}, D - 0.04f);
            return m;
        }
        if (d == "dome") {   // a half ellipsoid: rings from the ground up, rounded at the top
            std::vector<Station> st;
            const int n = Sd(7, 4);
            for (int k = 0; k < n; ++k) {
                const float a = 1.5708f * (float)k / (float)n;
                st.push_back({{0, 0.04f + (H - 0.04f) * std::sin(a), 0}, std::max(0.03f, dd * std::cos(a)), std::max(0.03f, w * std::cos(a))});
            }
            return Tube(st, {0, 0, 1}, Sd(18, 8), false, true);
        }
        if (d == "tipi") return Cone({0, 0.04f, 0}, {0, H * 1.12f, 0}, w, Sd(14, 6));
        if (d == "yurt") {
            m = Cyl(0, 0.04f, H * 0.55f, 0, w, dd, Sd(20, 8));
            shapes::Append(m, Cone({0, H * 0.55f - 0.01f, 0}, {0, H, 0}, w * 1.12f, Sd(20, 8)));
            return m;
        }
        // stall / awning: four posts and a sloped cloth roof
        m = Plan({{-w, -dd}, {-w, dd}, {w, dd * 0.4f}, {w, -dd * 0.4f}}, H * 0.88f, H * 0.93f);
        return m;
    });
    o.Add("poles/0", "poles", FrameAA({-W * 0.55f, 0, -D * 0.55f}, {W * 0.55f, H * 1.3f, D * 0.55f}), [&](const std::string& d) {
        PolyMesh m;
        if (d == "none") return m;
        if (bodyD == "tipi") {
            for (int k = 0; k < 6; ++k) { const float a = 6.2831853f * (float)k / 6.0f; shapes::Append(m, Limb({std::cos(a) * W * 0.22f, H * 0.55f, std::sin(a) * W * 0.22f}, {std::cos(a) * 0.01f + 0.004f * (float)k, H * 1.3f, std::sin(a) * 0.01f}, 0.02f, true, 5)); }
            return m;
        }
        if (bodyD == "stall") {
            for (int sx = -1; sx <= 1; sx += 2) for (int sz = -1; sz <= 1; sz += 2) shapes::Append(m, Limb({sx * (W * 0.5f - 0.05f), 0, sz * (D * 0.5f - 0.05f) * (sz > 0 ? 1.0f : 0.4f)}, {sx * (W * 0.5f - 0.05f), H * 0.9f, sz * (D * 0.5f - 0.05f) * (sz > 0 ? 1.0f : 0.4f)}, 0.035f, true, 6));
            return m;
        }
        // a ridge pole for the a-frame, spokes for the dome
        if (bodyD == "a_frame") return Limb({0, H * 1.0f, -D * 0.5f - 0.15f}, {0.003f, H * 1.0f + 0.002f, D * 0.5f + 0.15f}, 0.025f, true, 6);
        for (int k = 0; k < 2; ++k) { const float a = 1.5708f * (float)k; shapes::Append(m, Limb({std::cos(a) * W * 0.48f, 0.06f, std::sin(a) * D * 0.48f}, {-std::cos(a) * W * 0.48f + 0.002f, 0.062f, -std::sin(a) * D * 0.48f}, 0.015f, true, 5)); }
        return m;
    });
    o.Add("door/0", "door", FrameAA({-W * 0.2f, 0.04f, D * 0.4f}, {W * 0.2f, H * 0.7f, D * 0.6f}), [&](const std::string& d) {
        if (d == "none" || bodyD == "stall") return PolyMesh();
        // a darker flap on the front (+z)
        const float zf = bodyD == "dome" || bodyD == "yurt" ? D * 0.5f - 0.04f : bodyD == "tipi" ? W * 0.3f : D * 0.5f;
        return BoxAA({-W * 0.16f, 0.05f, zf - 0.06f}, {W * 0.16f, H * 0.62f, zf + 0.02f});
    });
    o.Add("fire/0", "fire", FrameAA({-0.5f, 0, D * 0.5f + 0.3f}, {0.5f, 0.5f, D * 0.5f + 1.3f}), [&](const std::string& d) {
        PolyMesh m;
        if (d == "none") return m;
        const float zc = D * 0.5f + 0.9f;
        for (int k = 0; k < 8; ++k) {
            const float a = 6.2831853f * (float)k / 8.0f;
            shapes::Append(m, Ell({std::cos(a) * 0.38f, 0.07f, zc + std::sin(a) * 0.38f}, 0.09f, 0.07f, 0.09f, 3, 6));
        }
        for (int k = 0; k < 3; ++k) {
            const float a = 2.1f * (float)k;
            shapes::Append(m, Limb({std::cos(a) * 0.25f, 0.1f + 0.02f * (float)k, zc + std::sin(a) * 0.25f}, {-std::cos(a) * 0.25f, 0.2f, zc - std::sin(a) * 0.25f}, 0.035f, true, 6));
        }
        if (d == "lit") shapes::Append(m, Cone({0, 0.15f, zc}, {0, 0.6f, zc}, 0.14f, 6));
        return m;
    });
}

Template MakeTent() {
    B b;
    b.t.id = "tent"; b.t.name = "Tent"; b.t.category = "Environment"; b.t.version = 1;
    b.t.description = "A tent or camp shelter: an A-frame, dome, tipi, yurt or market stall, with a floor, poles, a door flap and an optional campfire. The door and fire face +Z.";
    b.P("width", "Width", 2.4, 0.8, 12.0, "Size", "Left to right.");
    b.P("depth", "Depth", 2.2, 0.8, 12.0, "Size", "Front to back.");
    b.P("height", "Height", 1.5, 0.5, 8.0, "Size", "Overall height.");
    const Color kCanvas{0.8f, 0.72f, 0.5f, 1}, kGreen{0.3f, 0.45f, 0.3f, 1}, kWood{0.5f, 0.35f, 0.2f, 1}, kDark{0.2f, 0.17f, 0.14f, 1}, kStone{0.5f, 0.5f, 0.5f, 1}, kRed{0.75f, 0.2f, 0.2f, 1};
    b.S("floor", "Floor", {{"groundsheet", "Groundsheet"}, {"none", "None"}}, kDark, 0);
    b.S("body", "Shape", {{"a_frame", "A-frame"}, {"dome", "Dome"}, {"tipi", "Tipi"}, {"yurt", "Yurt"}, {"stall", "Market stall"}}, kCanvas, 1);
    b.S("poles", "Poles", {{"wood", "Wood"}, {"none", "None"}}, kWood, 2);
    b.S("door", "Door flap", {{"flap", "Flap"}, {"none", "None"}}, kDark, 3);
    b.S("fire", "Campfire", {{"none", "None"}, {"unlit", "Unlit"}, {"lit", "Lit"}}, kStone, 4);
    b.St("a_frame", "A-frame tent", "A simple two-sided tent.", {{"width", 2.2}, {"depth", 2.6}, {"height", 1.4}}, {{"floor", "groundsheet"}, {"body", "a_frame"}, {"poles", "wood"}, {"door", "flap"}, {"fire", "none"}}, {{"body", kGreen}});
    b.St("dome_tent", "Dome tent", "A round tent with crossing poles.", {{"width", 2.8}, {"depth", 2.8}, {"height", 1.4}}, {{"floor", "groundsheet"}, {"body", "dome"}, {"poles", "wood"}, {"door", "flap"}, {"fire", "none"}}, {{"body", kRed}});
    b.St("tipi", "Tipi", "A tall cone with poles sticking out of the top.", {{"width", 4.0}, {"depth", 4.0}, {"height", 4.2}}, {{"floor", "none"}, {"body", "tipi"}, {"poles", "wood"}, {"door", "flap"}, {"fire", "unlit"}}, {});
    b.St("yurt", "Yurt", "A round wall with a conical roof.", {{"width", 6.0}, {"depth", 6.0}, {"height", 3.4}}, {{"floor", "groundsheet"}, {"body", "yurt"}, {"poles", "none"}, {"door", "flap"}, {"fire", "none"}}, {});
    b.St("market_stall", "Market stall", "Four posts and a sloped cloth roof.", {{"width", 2.6}, {"depth", 2.0}, {"height", 2.4}}, {{"floor", "none"}, {"body", "stall"}, {"poles", "wood"}, {"door", "none"}, {"fire", "none"}}, {{"body", kRed}});
    b.St("camp", "Camp with a fire", "A dome tent and a lit campfire.", {{"width", 2.6}, {"depth", 2.6}, {"height", 1.4}}, {{"floor", "groundsheet"}, {"body", "dome"}, {"poles", "wood"}, {"door", "flap"}, {"fire", "lit"}}, {{"body", kGreen}, {"fire", kStone}});
    b.t.generate = GenerateTent;
    return b.t;
}

} // namespace

void RegisterMoreTemplates() {
    Registry& r = Registry::Instance();
    r.Register(MakeAircraft());
    r.Register(MakeBoat());
    r.Register(MakeWeapon());
    r.Register(MakeTower());
    r.Register(MakeLamp());
    r.Register(MakeTent());
}

} // namespace okay::modeling::guided
