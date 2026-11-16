// The organic human: from Medium up the human is built from lofted elliptical sections along a spine (a shaped torso, tapered
// limbs with a calf swell and a wrist, a neck that joins the skull, a shaped skull with jaw and brow ridge) with separate swappable
// face parts, hands with individual fingers and a thumb, feet with an arch and toes, and clothes that follow the body as fitted
// shells. The blocky character (Low) is the older generator in GuidedCharacters.cpp; both make the same part ids and designs, so
// swaps and recipes work at every level. Written from general modeling (lofting, ellipsoids, anatomical proportions: an adult is
// about 7.5 heads tall); no code, models or assets from other projects.
#include "GuidedKit.hpp"

namespace okay::modeling::guided {
namespace {
using namespace kit;

struct Dim {
    float H, sw, td, hs, th, hipY, shoulderY, hb, ankleY, tl;
    int bt;                    // body type 0 neutral, 1 male, 2 female
    float armA, bend, stance, armF;
    std::string torsoD, headD;
    float limbK;               // 1 round, 0.7 piston, 1.3 thick
    int sides;                 // ring points
    int limbSides;
};
const Vec3 kFwd{0, 0, 1};

Dim MakeDim(const Recipe& r) {
    Dim d;
    d.H = Pf(r, "height"); d.sw = Pf(r, "shoulderWidth"); d.td = Pf(r, "torsoDepth"); d.hs = Pf(r, "headSize");
    d.th = Pf(r, "limbThickness");
    const float legF = Pf(r, "legLength"), neck = Pf(r, "neckLength");
    d.armF = Pf(r, "armLength");
    d.hipY = d.H * legF;
    d.shoulderY = std::max(d.hipY + 0.25f, d.H - neck - d.hs);
    d.hb = d.shoulderY + neck;
    d.ankleY = d.H * 0.035f;
    d.tl = d.shoulderY - d.hipY;
    d.bt = (int)r.Param("bodyType");
    d.armA = Pf(r, "armAngle") * kPi / 180.0f; d.bend = Pf(r, "elbowBend") * kPi / 180.0f; d.stance = Pf(r, "stance") * kPi / 180.0f;
    d.torsoD = r.DesignFor("torso", "torso/0"); d.headD = r.DesignFor("head", "head/0");
    d.limbK = 1.0f;
    d.sides = shapes::Scale(16, 6);
    d.limbSides = shapes::Scale(12, 6);
    if ((int)r.Param("style", 0) == 1) { d.sides = std::min(d.sides, 7); d.limbSides = std::min(d.limbSides, 5); }   // stylised low poly: visible facets
    return d;
}

// The torso as stations from the hips up, offset outward by `off` (clothes).
std::vector<Station> TorsoStations(const Dim& d, float off, float bottomLift = 0.0f, float topCut = 0.0f) {
    static const float hipF[3] = {1.0f, 0.92f, 1.1f}, waistF[3] = {0.9f, 0.98f, 0.8f}, chestF[3] = {1.0f, 1.06f, 0.94f}, shF[3] = {1.0f, 1.06f, 0.9f}, bustZ[3] = {1.0f, 1.0f, 1.12f};
    float wf = waistF[d.bt], sf = shF[d.bt], rzK = 1.0f;
    if (d.torsoD == "tapered") { wf *= 0.88f; sf *= 1.04f; }
    else if (d.torsoD == "barrel") { wf *= 1.15f; rzK = 1.12f; }
    else if (d.torsoD == "box") { wf = std::max(wf, 1.0f) * 1.04f; }
    const float tl = d.tl;
    std::vector<Station> s;
    s.push_back({{0, d.hipY - 0.07f + bottomLift, 0}, d.td * 0.50f * rzK + off, d.sw * 0.40f * hipF[d.bt] + off});
    s.push_back({{0, d.hipY + 0.38f * tl, 0}, d.td * 0.44f * rzK + off, d.sw * 0.34f * wf + off});
    s.push_back({{0, d.hipY + 0.72f * tl, 0.005f}, d.td * 0.50f * bustZ[d.bt] * rzK + off, d.sw * 0.42f * chestF[d.bt] + off});
    s.push_back({{0, d.shoulderY - 0.03f, 0}, d.td * 0.40f + off, d.sw * 0.43f * sf + off});
    if (topCut <= 0) s.push_back({{0, d.shoulderY + 0.03f, 0}, d.td * 0.30f + off, d.sw * 0.17f + off});
    return s;
}

// Stations along a limb from a to b: (t, radius multiplier) pairs; `fwdK` scales the radius front-to-back.
std::vector<Station> LimbStations(Vec3 a, Vec3 b, float base, const std::vector<std::pair<float, float>>& prof, float off, float tmax = 1.0f, float fwdK = 1.0f) {
    std::vector<Station> s;
    for (const auto& p : prof) {
        if (p.first > tmax + 1e-4f) { const float t = tmax; const float r = base * p.second + off; s.push_back({a + (b - a) * t, r * fwdK, r}); break; }
        const float r = base * p.second + off;
        s.push_back({a + (b - a) * p.first, r * fwdK, r});
        if (p.first >= tmax - 1e-4f) break;
    }
    return s;
}
const std::vector<std::pair<float, float>> kUpperArm = {{0.0f, 1.25f}, {0.25f, 1.2f}, {0.6f, 0.98f}, {1.0f, 0.84f}};
const std::vector<std::pair<float, float>> kForearm = {{0.0f, 0.80f}, {0.25f, 0.9f}, {0.7f, 0.62f}, {1.0f, 0.5f}};
const std::vector<std::pair<float, float>> kThigh = {{0.0f, 1.0f}, {0.3f, 0.98f}, {0.7f, 0.78f}, {1.0f, 0.64f}};
const std::vector<std::pair<float, float>> kCalf = {{0.0f, 0.60f}, {0.28f, 0.72f}, {0.65f, 0.5f}, {1.0f, 0.36f}};

PolyMesh Rotate(PolyMesh m, Vec3 pivot, Vec3 from, Vec3 to) {   // turn a mesh so `from` points along `to`
    const Vec3 f = from.Normalized(), t = to.Normalized();
    const Vec3 ax = Vec3::Cross(f, t);
    const float s = ax.Magnitude(), c = Vec3::Dot(f, t);
    if (s < 1e-5f) { if (c < 0) for (Vec3& v : m.verts) { v.x = 2 * pivot.x - v.x; v.y = 2 * pivot.y - v.y; } return m; }
    const Vec3 k = ax / s;
    for (Vec3& v : m.verts) {
        const Vec3 p = v - pivot;
        v = pivot + p * c + Vec3::Cross(k, p) * s + k * (Vec3::Dot(k, p) * (1 - c));
    }
    return m;
}

// ---- Head and face -------------------------------------------------------------------------------------------------
struct HeadShape { Vec3 c; float rx, ry, rz, jaw; };
HeadShape ShapeOfHead(const Dim& d) {
    HeadShape h;
    h.c = {0, d.hb + d.hs * 0.5f, 0};
    h.rx = d.hs * 0.34f; h.ry = d.hs * 0.5f; h.rz = d.hs * 0.47f; h.jaw = 0.25f;
    if (d.headD == "oval") { h.rx *= 0.9f; h.ry *= 1.0f; h.jaw = 0.32f; }
    else if (d.headD == "box") { h.rx *= 1.12f; h.jaw = 0.06f; }
    else if (d.headD == "pointed") { h.jaw = 0.42f; }
    else if (d.headD == "heart") { h.rx *= 1.06f; h.jaw = 0.5f; }
    else if (d.headD == "long") { h.rx *= 0.9f; h.ry *= 1.14f; h.jaw = 0.3f; }
    else if (d.headD == "wide") { h.rx *= 1.2f; h.rz *= 0.96f; h.jaw = 0.12f; }
    return h;
}
float Taper(const HeadShape& h, float y) {
    const float yn = (y - h.c.y) / h.ry;
    return yn < 0.1f ? std::max(0.35f, 1.0f - h.jaw * (0.1f - yn)) : 1.0f;
}
float SkullZ(const HeadShape& h, float x, float y) {   // front surface of the skull at (x, y)
    const float tp = Taper(h, y);
    const float u = x / (h.rx * tp), v = (y - h.c.y) / h.ry;
    return h.rz * tp * std::sqrt(std::max(0.0f, 1.0f - u * u - v * v));
}
PolyMesh Skull(const Dim& d, const HeadShape& h) {
    PolyMesh m = Ell(h.c, h.rx, h.ry, h.rz, shapes::Scale(8, 4), shapes::Scale(14, 6));
    for (Vec3& v : m.verts) {
        const float tp = Taper(h, v.y);
        v.x = h.c.x + (v.x - h.c.x) * tp;
        v.z = h.c.z + (v.z - h.c.z) * tp;
        if (v.z < h.c.z) v.z = h.c.z + (v.z - h.c.z) * 0.96f;            // a flatter back
        const float yn = (v.y - h.c.y) / h.ry;
        if (yn > 0.15f && yn < 0.45f && v.z > h.c.z + h.rz * 0.5f) v.z += d.hs * 0.012f * (1.0f - std::fabs(yn - 0.3f) / 0.15f);   // brow ridge
        if (yn < -0.5f && v.z > h.c.z) v.z += d.hs * 0.02f * (-yn - 0.5f) * (d.headD == "pointed" ? 3.0f : 1.0f);               // chin forward
    }
    return m;
}

struct FaceSet { std::string eyes, brows, nose, mouth, ears; };

PolyMesh EyesMesh(const Dim& d, const HeadShape& h, const std::string& design) {
    PolyMesh m;
    const float hs = d.hs, ey = h.c.y + 0.06f * hs, ex = 0.16f * hs;
    const float er = (design == "big" ? 0.075f : design == "narrow" ? 0.04f : 0.05f) * hs;
    for (int s = -1; s <= 1; s += 2) {
        const float z = SkullZ(h, s * ex, ey) - 0.012f * hs;
        shapes::Append(m, Ell({s * ex, ey, z}, er, er * (design == "narrow" ? 0.55f : 0.9f), er * 0.7f, 4, 8));
        if (shapes::Detail() >= 2)   // eyelids: a slightly larger half shell over the top of the eye
            shapes::Append(m, Ell({s * ex, ey + er * 0.35f, z - er * 0.12f}, er * 1.22f, er * 0.5f, er * 0.82f, 3, 8));
    }
    return m;
}
PolyMesh BrowsMesh(const Dim& d, const HeadShape& h, const std::string& design) {
    PolyMesh m;
    const float hs = d.hs, by = h.c.y + 0.16f * hs;
    for (int s = -1; s <= 1; s += 2) {
        const float x = s * 0.16f * hs, z = SkullZ(h, x, by) - 0.004f * hs;
        const float w = (design == "thick" ? 0.1f : 0.085f) * hs, t = (design == "thick" ? 0.028f : 0.016f) * hs;
        if (design == "angular") {
            shapes::Append(m, Ell({x - s * w * 0.4f, by - 0.005f * hs, z}, w * 0.6f, t, t * 1.2f, 3, 6));
            shapes::Append(m, Ell({x + s * w * 0.5f, by + 0.012f * hs, z}, w * 0.6f, t, t * 1.2f, 3, 6));
        } else shapes::Append(m, Ell({x, by, z}, w, t, t * 1.3f, 3, 8));
    }
    return m;
}
PolyMesh NoseMesh(const Dim& d, const HeadShape& h, const std::string& design) {
    PolyMesh m;
    const float hs = d.hs, ny = h.c.y - 0.1f * hs, z = SkullZ(h, 0, ny);
    float rx = 0.05f, ry = 0.09f, rz = 0.06f;
    if (design == "button") { rx = 0.04f; ry = 0.05f; rz = 0.04f; }
    else if (design == "long") { ry = 0.12f; rz = 0.07f; }
    else if (design == "flat") { rx = 0.065f; ry = 0.06f; rz = 0.04f; }
    shapes::Append(m, Ell({0.0006f * hs, ny, z + rz * hs * 0.5f}, rx * hs, ry * hs, rz * hs));
    shapes::Append(m, Ell({0.0011f * hs, ny + ry * hs * 0.5f, z - 0.005f * hs}, rx * hs * 0.5f, ry * hs * 0.7f, rz * hs * 0.55f, 3, 6));   // bridge
    if (shapes::Detail() >= 2)
        for (int s = -1; s <= 1; s += 2) shapes::Append(m, Ell({s * rx * hs * 0.7f, ny - ry * hs * 0.75f, z + rz * hs * 0.1f}, rx * hs * 0.45f, ry * hs * 0.22f, rz * hs * 0.45f, 3, 6));   // nostrils
    return m;
}
PolyMesh MouthMesh(const Dim& d, const HeadShape& h, const std::string& design) {
    PolyMesh m;
    const float hs = d.hs, my = h.c.y - 0.29f * hs, z = SkullZ(h, 0, my);
    const float w = design == "small" ? 0.07f : 0.1f;
    shapes::Append(m, Ell({0.0007f * hs, my + 0.012f * hs, z - 0.004f * hs}, w * hs, 0.02f * hs, 0.03f * hs, 3, 10));   // upper lip
    shapes::Append(m, Ell({0.0013f * hs, my - 0.018f * hs, z - 0.002f * hs}, w * 0.85f * hs, 0.024f * hs, 0.032f * hs, 3, 10));   // lower lip
    if (design == "smile") for (int s = -1; s <= 1; s += 2) shapes::Append(m, Ell({s * (w + 0.015f) * hs, my + 0.012f * hs, z - 0.02f * hs}, 0.016f * hs, 0.012f * hs, 0.02f * hs, 3, 6));
    return m;
}
PolyMesh EarsMesh(const Dim& d, const HeadShape& h, const std::string& design) {
    PolyMesh m;
    const float hs = d.hs;
    const float sz = design == "small" ? 0.8f : design == "big" ? 1.25f : 1.0f;
    for (int s = -1; s <= 1; s += 2) {
        shapes::Append(m, Ell({s * (h.rx * 0.985f), h.c.y - 0.02f * hs, -0.02f * hs}, 0.03f * hs * sz, 0.12f * hs * sz, 0.075f * hs * sz, 4, 8));
        if (shapes::Detail() >= 2) shapes::Append(m, Ell({s * (h.rx * 1.02f), h.c.y + 0.0f, -0.035f * hs}, 0.012f * hs * sz, 0.08f * hs * sz, 0.045f * hs * sz, 3, 6));   // rim
    }
    return m;
}

// ---- Hands and feet ----------------------------------------------------------------------------------------------
// A hand pointing down (-y) at the origin, scaled to length hl: a palm and, by design, fingers and a thumb.
PolyMesh HandLocal(const std::string& design, float hl, float side) {
    PolyMesh m;
    const float pw = hl * 0.42f, pl = hl * 0.5f, pt = hl * 0.16f;
    const int fin = design != "mitten" && design != "block" && design != "fist";
    m = Ell({0, -pl * 0.55f, 0}, pw, pl * 0.55f, pt, 4, 8);   // palm
    if (design == "fist") { shapes::Append(m, Ell({0, -pl * 0.9f, pt * 0.7f}, pw * 0.95f, pl * 0.45f, pt * 1.5f, 4, 8)); shapes::Append(m, Ell({side * pw * 0.95f, -pl * 0.55f, pt}, pt * 1.2f, pl * 0.35f, pt, 3, 6)); return m; }
    if (design == "block") { m = BoxAA({-pw, -hl, -pt}, {pw, 0.01f, pt}); return m; }
    if (design == "mitten" || !fin) {
        shapes::Append(m, Ell({0, -pl - (hl - pl) * 0.45f, 0}, pw * 0.9f, (hl - pl) * 0.5f, pt * 0.85f, 4, 8));
        shapes::Append(m, Ell({side * pw * 0.95f, -pl * 0.45f, pt * 0.8f}, pt * 1.0f, pl * 0.4f, pt * 0.8f, 3, 6));
        return m;
    }
    const bool curled = design == "relaxed";
    const float fl[4] = {0.9f, 1.0f, 0.95f, 0.75f};
    for (int k = 0; k < 4; ++k) {
        const float x = -pw * 0.78f + pw * 1.56f * (float)k / 3.0f;
        const float len = (hl - pl) * fl[k];
        const int segs = shapes::DetailF() >= 2.5f ? 3 : shapes::DetailF() >= 1.5f ? 2 : 1;
        for (int s = 0; s < segs; ++s) {
            const float t0 = (float)s / (float)segs, t1 = (float)(s + 1) / (float)segs;
            const float cv = curled ? 0.35f * (float)s : 0.0f;
            const Vec3 a{x, -pl - len * t0 * 0.95f, pt * cv * 0.4f}, b{x, -pl - len * t1 * 0.95f + cv * 0.0f, pt * 0.5f * (cv + 0.35f * (float)(s + 1) * (curled ? 1 : 0))};
            const float r = pw * 0.17f * (1.0f - 0.2f * t0);
            shapes::Append(m, Limb(a, b, r * (1.0f - 0.02f * (float)s), true, 6));
        }
    }
    // thumb: open (out from the side) or curled across the palm
    const float tl = (hl - pl) * 0.7f;
    const Vec3 t0{side * pw * 0.85f, -pl * 0.35f, pt * 0.5f};
    shapes::Append(m, Limb(t0, t0 + Vec3{side * pw * (curled ? 0.2f : 0.55f), -tl * 0.5f, pt * (curled ? 1.3f : 0.3f)}, pw * 0.2f, true, 6));
    shapes::Append(m, Limb(t0 + Vec3{side * pw * (curled ? 0.2f : 0.55f), -tl * 0.5f - 0.002f, pt * (curled ? 1.3f : 0.3f)}, t0 + Vec3{side * pw * (curled ? 0.35f : 0.8f), -tl, pt * (curled ? 2.0f : 0.5f)}, pw * 0.16f, true, 6));
    return m;
}

PolyMesh FootLocal(const std::string& design, float len) {   // heel at z = -0.25 len, toes at +0.75 len; ground at y = 0
    PolyMesh m;
    const float w = len * 0.17f;
    std::vector<Station> st = {
        {{0, len * 0.17f, -len * 0.22f}, len * 0.15f, w * 0.85f},     // heel
        {{0, len * 0.19f, len * 0.1f}, len * 0.12f, w * 0.9f},        // arch (the underside is lifted here)
        {{0, len * 0.13f, len * 0.4f}, len * 0.1f, w * 1.1f},         // ball of the foot
        {{0, len * 0.08f, len * 0.62f}, len * 0.07f, w * 0.95f}};     // toes
    // station axes: Loft uses refA = up so A is up, B is sideways; the tube runs along +z
    m = Loft(st, {0, 1, 0}, shapes::Scale(10, 6), true, true);
    if (shapes::Detail() >= 2 && design == "block")
        for (int k = 0; k < 5; ++k) shapes::Append(m, Ell({-w * 0.8f + w * 1.6f * (float)k / 4.0f, len * 0.045f, len * (0.7f - 0.012f * (float)std::abs(k - 1))}, w * 0.2f, len * 0.04f, len * 0.07f, 3, 6));   // toes
    return m;
}

// ---- The generator -----------------------------------------------------------------------------------------------------
} // namespace

void GenerateHumanOrganic(const Recipe& r, std::vector<GeneratedPart>& parts, const std::function<void(const Recipe&, std::vector<GeneratedPart>&)>& boxy) {
    // Accessories (hair, hat, glasses, backpack, cape, shoulder pads) come from the blocky generator: they sit on the same dimensions.
    {
        std::vector<GeneratedPart> all;
        boxy(r, all);
        static const char* kReplaced[] = {"head/", "face/", "neck/", "torso/", "pelvis/", "arm/", "hand/", "leg/", "foot/", "shirt/", "pants/", "belt/"};
        for (GeneratedPart& p : all) {
            bool replaced = false;
            for (const char* pre : kReplaced) if (p.id.rfind(pre, 0) == 0) replaced = true;
            if (!replaced) parts.push_back(std::move(p));
        }
    }
    Out o{r, parts};
    const Dim d = MakeDim(r);
    const HeadShape hd = ShapeOfHead(d);
    const float hs = d.hs;
    auto limbK = [&](const std::string& design) { return design == "thick" ? 1.3f : design == "piston" ? 0.7f : 1.0f; };
    auto limbSides = [&](const std::string& design) { return design == "boxy" ? std::max(4, d.limbSides / 3) : d.limbSides; };

    // torso and hips
    o.Add("torso/0", "torso", FrameAA({-d.sw * 0.5f, d.hipY - 0.07f, -d.td}, {d.sw * 0.5f, d.shoulderY + 0.04f, d.td}), [&](const std::string&) {
        return Loft(TorsoStations(d, 0.0f), kFwd, d.sides, false, true);
    });
    o.Add("pelvis/0", "pelvis", FrameAA({-d.sw * 0.4f, d.hipY - 0.15f, -d.td * 0.5f}, {d.sw * 0.4f, d.hipY, d.td * 0.5f}), [&](const std::string&) {
        const float hipF = d.bt == 1 ? 0.92f : d.bt == 2 ? 1.1f : 1.0f;
        return Ell({0, d.hipY - 0.1f, 0}, d.sw * 0.385f * hipF, 0.085f, d.td * 0.46f, 5, d.sides / 2);
    });
    // neck
    o.Add("neck/0", "neck", FrameAA({-hs * 0.3f, d.shoulderY, -hs * 0.3f}, {hs * 0.3f, d.hb + 0.04f, hs * 0.3f}), [&](const std::string& dg) {
        const float nr = hs * (dg == "thin" ? 0.19f : 0.24f);
        return Loft({{{0, d.shoulderY - 0.02f, 0}, nr * 1.15f, nr * 1.15f}, {{0, d.shoulderY + (d.hb - d.shoulderY) * 0.5f, 0.008f}, nr * 0.95f, nr}, {{0, d.hb + 0.05f * hs, 0.012f}, nr * 0.9f, nr * 0.95f}}, kFwd, d.limbSides, false, false);
    });
    // head and face
    o.Add("head/0", "head", FrameAA({-hd.rx, d.hb, -hd.rz}, {hd.rx, d.hb + hs, hd.rz}), [&](const std::string&) { return Skull(d, hd); });
    {
        const Frame ff = FrameAA({-hs * 0.3f, hd.c.y - hs * 0.35f, hd.rz * 0.5f}, {hs * 0.3f, hd.c.y + hs * 0.25f, hd.rz + 0.03f * hs});
        const std::string fd = r.DesignFor("face", "face/0");
        o.Add("face/0", "face", ff, [&](const std::string& dg) {
            PolyMesh m = EyesMesh(d, hd, dg == "big" ? "big" : "round");
            shapes::Append(m, NoseMesh(d, hd, "straight"));
            shapes::Append(m, MouthMesh(d, hd, "smile"));
            return m;
        });
        (void)fd;
        const bool combined = r.DesignFor("face", "face/0") != "none" && !r.DesignFor("face", "face/0").empty();   // the old combined face replaces the separate parts
        if (!combined) o.Add("eyes/0", "eyes", ff, [&](const std::string& dg) { return EyesMesh(d, hd, dg); });
        if (!combined) o.Add("brows/0", "brows", ff, [&](const std::string& dg) { return BrowsMesh(d, hd, dg); });
        if (!combined) o.Add("nose/0", "nose", ff, [&](const std::string& dg) { return NoseMesh(d, hd, dg); });
        if (!combined) o.Add("mouth/0", "mouth", ff, [&](const std::string& dg) { return MouthMesh(d, hd, dg); });
        o.Add("ears/0", "ears", FrameAA({-hd.rx * 1.1f, hd.c.y - hs * 0.15f, -hs * 0.1f}, {hd.rx * 1.1f, hd.c.y + hs * 0.12f, hs * 0.06f}), [&](const std::string& dg) { return EarsMesh(d, hd, dg); });
    }
    // arms and hands
    for (int side = 0; side < 2; ++side) {
        const float s = side ? 1.0f : -1.0f;
        const std::string sd = std::to_string(side);
        const float ua = d.H * d.armF * 0.46f, fa = d.H * d.armF * 0.40f, hl = d.H * d.armF * 0.14f;
        const Vec3 S{s * d.sw * 0.5f, d.shoulderY - 0.04f, 0};
        const Vec3 d1{s * std::sin(d.armA), -std::cos(d.armA), 0};
        const Vec3 E = S + d1 * ua;
        Vec3 d2{d1.x, d1.y * std::cos(d.bend), -d1.y * std::sin(d.bend)};
        d2 = d2.Normalized();
        const Vec3 W = E + d2 * fa;
        auto armFrame = [&](Vec3 a, Vec3 b) { return FrameAA({std::min(a.x, b.x) - d.th * 2, std::min(a.y, b.y), std::min(a.z, b.z) - d.th * 2}, {std::max(a.x, b.x) + d.th * 2, std::max(a.y, b.y) + d.th, std::max(a.z, b.z) + d.th * 2}); };
        o.Add("arm/" + sd + "/upper", "arm", armFrame(S, E), [&](const std::string& dg) { return Loft(LimbStations(S, E, d.th * limbK(dg), kUpperArm, 0, 1.0f, 0.95f), kFwd, limbSides(dg), true, false); });
        o.Add("arm/" + sd + "/fore", "arm", armFrame(E, W), [&](const std::string& dg) { return Loft(LimbStations(E, W, d.th * limbK(dg), kForearm, 0, 1.0f, 0.95f), kFwd, limbSides(dg), false, true); });
        o.Add("hand/" + sd, "hand", FrameAA({W.x - hl * 0.5f, W.y - hl, W.z - hl * 0.3f}, {W.x + hl * 0.5f, W.y + 0.02f, W.z + hl * 0.3f}), [&](const std::string& dg) {
            PolyMesh m = HandLocal(dg, hl, s);
            return Rotate(m, {0, 0, 0}, {0, -1, 0}, d2);
        });
        // the hand is built at the origin and moved to the wrist (see below)
    }
    // legs and feet
    for (int side = 0; side < 2; ++side) {
        const float s = side ? 1.0f : -1.0f;
        const std::string sd = std::to_string(side);
        const Vec3 Hp{s * d.sw * 0.22f, d.hipY - 0.04f, 0};
        const float L = std::max(0.3f, (Hp.y - d.ankleY) / std::cos(d.stance));
        const Vec3 d1{s * std::sin(d.stance), -std::cos(d.stance), 0};
        const Vec3 K = Hp + d1 * (L * 0.5f), A = Hp + d1 * L;
        const float R = d.th * 1.45f;
        auto legFrame = [&](Vec3 a, Vec3 b) { return FrameAA({std::min(a.x, b.x) - R, std::min(a.y, b.y), std::min(a.z, b.z) - R}, {std::max(a.x, b.x) + R, std::max(a.y, b.y) + 0.01f, std::max(a.z, b.z) + R}); };
        o.Add("leg/" + sd + "/upper", "leg", legFrame(Hp, K), [&](const std::string& dg) { return Loft(LimbStations(Hp, K, R * limbK(dg), kThigh, 0, 1.0f, 1.0f), kFwd, limbSides(dg), false, false); });
        o.Add("leg/" + sd + "/lower", "leg", legFrame(K, A), [&](const std::string& dg) { return Loft(LimbStations(K, A, R * limbK(dg), kCalf, 0, 1.0f, 0.95f), kFwd, limbSides(dg), false, true); });
        const float fl = d.H * 0.15f;
        o.Add("foot/" + sd, "foot", FrameAA({A.x - fl * 0.2f, 0, A.z - fl * 0.3f}, {A.x + fl * 0.2f, A.y + 0.05f, A.z + fl * 0.8f}), [&](const std::string& dg) {
            PolyMesh m = FootLocal(dg, fl * (dg == "boot" || dg == "sneaker" ? 1.12f : dg == "round" ? 1.1f : 1.0f));
            if (dg == "boot") {
                std::vector<Station> sh = {{{0, fl * 0.2f, -fl * 0.1f}, d.th * 1.1f + 0.012f, d.th * 1.1f + 0.012f}, {{0, A.y + 0.2f, -fl * 0.12f}, d.th * 0.95f + 0.012f, d.th * 0.95f + 0.012f}};
                shapes::Append(m, Loft(sh, {0, 0, 1}, 10, false, false));
            }
            for (Vec3& v : m.verts) { v.x += A.x; v.z += A.z + fl * 0.08f; }
            return m;
        });
        // the shin ends at the ankle; the foot is placed under it
    }
    // clothes: fitted shells offset from the skin
    {
        const float off = 0.014f;
        o.Add("shirt/0", "shirt", FrameAA({-d.sw * 0.55f, d.hipY - 0.2f, -d.td}, {d.sw * 0.55f, d.shoulderY + 0.05f, d.td}), [&](const std::string& dg) {
            const float lower = dg == "jacket" ? 0.16f : 0.0f;
            const float lowerK = dg == "hoodie" ? 0.14f : lower;
            std::vector<Station> ts = TorsoStations(d, off, -lowerK, dg == "vest" ? 1.0f : 0.0f);
            PolyMesh m = Loft(ts, kFwd, d.sides, false, true);
            const bool longSleeve = dg == "jacket" || dg == "longsleeve" || dg == "hoodie";
            if (dg == "tshirt" || longSleeve) for (int side = 0; side < 2; ++side) {
                const float s = side ? 1.0f : -1.0f;
                const float ua = d.H * d.armF * 0.46f, fa = d.H * d.armF * 0.40f;
                const Vec3 S{s * d.sw * 0.5f, d.shoulderY - 0.04f, 0}, d1{s * std::sin(d.armA), -std::cos(d.armA), 0};
                const Vec3 E = S + d1 * ua;
                shapes::Append(m, Loft(LimbStations(S, E, d.th, kUpperArm, 0.014f, longSleeve ? 1.0f : 0.5f, 0.95f), kFwd, d.limbSides, true, false));
                if (dg == "longsleeve" || dg == "hoodie") {   // the forearm sleeve too, ending at the wrist
                    Vec3 d2{d1.x, d1.y * std::cos(d.bend), -d1.y * std::sin(d.bend)};
                    d2 = d2.Normalized();
                    const Vec3 W = E + d2 * fa;
                    shapes::Append(m, Loft(LimbStations(E + d1 * 0.004f, W, d.th * 0.85f, kForearm, 0.016f, 0.97f, 0.95f), kFwd, d.limbSides, false, true));
                }
            }
            if (dg == "hoodie") shapes::Append(m, Ell({0, d.shoulderY + 0.03f, -d.td * 0.5f}, d.sw * 0.2f, 0.1f, d.td * 0.34f, 4, 10));   // the hood resting on the back
            return m;
        });
        o.Add("pants/0", "pants", FrameAA({-d.sw * 0.5f, 0, -d.td}, {d.sw * 0.5f, d.hipY + 0.03f, d.td}), [&](const std::string& dg) {
            PolyMesh m;
            if (dg == "skirt") {
                return Loft({{{0, d.hipY - 0.45f, 0}, d.td * 0.9f, d.sw * 0.62f}, {{0, d.hipY - 0.2f, 0}, d.td * 0.68f, d.sw * 0.5f}, {{0, d.hipY + 0.04f, 0}, d.td * 0.5f + off, d.sw * 0.34f + off}}, kFwd, d.sides, false, false);
            }
            if (dg == "longskirt") {   // down to the calves
                const float yb = std::max(d.ankleY + 0.12f, d.hipY - 0.8f);
                return Loft({{{0, yb + 0.0007f, 0}, d.td * 0.951f, d.sw * 0.703f}, {{0, d.hipY - 0.4011f, 0}, d.td * 0.723f, d.sw * 0.552f}, {{0, d.hipY - 0.1213f, 0}, d.td * 0.562f, d.sw * 0.423f}, {{0, d.hipY + 0.04f, 0}, d.td * 0.5f + off, d.sw * 0.34f + off}}, kFwd, d.sides, false, false);
            }
            std::vector<Station> waist = {{{0, d.hipY - 0.16f, 0}, d.td * 0.5f + off + 0.002f, d.sw * 0.4f + off + 0.002f}, {{0, d.hipY + 0.02f, 0}, d.td * 0.47f + off, d.sw * 0.36f + off}};
            m = Loft(waist, kFwd, d.sides, false, false);
            for (int side = 0; side < 2; ++side) {
                const float s = side ? 1.0f : -1.0f;
                const Vec3 Hp{s * d.sw * 0.22f, d.hipY - 0.04f, 0};
                const float L = std::max(0.3f, (Hp.y - d.ankleY) / std::cos(d.stance));
                const Vec3 d1{s * std::sin(d.stance), -std::cos(d.stance), 0};
                const Vec3 K = Hp + d1 * (L * 0.5f), A = Hp + d1 * L;
                const float R = d.th * 1.45f * (dg == "jeans" ? 1.1f : dg == "cargo" ? 1.16f : dg == "leggings" ? 0.94f : 1.0f);
                shapes::Append(m, Loft(LimbStations(Hp - d1 * 0.02f, K, R, kThigh, 0.016f, dg == "shorts" ? 0.7f : 1.0f, 1.0f), kFwd, d.limbSides, false, dg == "shorts"));
                if (dg == "trousers" || dg == "greaves" || dg == "jeans" || dg == "cargo" || dg == "leggings") shapes::Append(m, Loft(LimbStations(K, A, R, kCalf, 0.016f, 0.97f, 0.95f), kFwd, d.limbSides, false, true));
                if (dg == "cargo") {   // a pocket on the outside of each thigh
                    const Vec3 c = Hp + d1 * (L * 0.3f);
                    shapes::Append(m, BoxAA({c.x + s * (R * 1.12f) - 0.02f, c.y - 0.07f, c.z - 0.06f}, {c.x + s * (R * 1.12f) + 0.03f, c.y + 0.07f, c.z + 0.06f}));
                }
            }
            return m;
        });
        o.Add("belt/0", "belt", FrameAA({-d.sw * 0.45f, d.hipY - 0.01f, -d.td * 0.6f}, {d.sw * 0.45f, d.hipY + 0.08f, d.td * 0.7f}), [&](const std::string&) {
            const float wy = d.hipY + 0.38f * d.tl * 0.0f + 0.03f;
            const float k = 1.0f;
            PolyMesh m = Loft({{{0, wy - 0.03f, 0}, d.td * 0.47f * k + 0.016f, d.sw * 0.37f * k + 0.016f}, {{0, wy + 0.035f, 0}, d.td * 0.46f * k + 0.016f, d.sw * 0.36f * k + 0.016f}}, kFwd, d.sides, false, false);
            shapes::Append(m, BoxAA({-0.035f, wy - 0.035f, d.td * 0.47f + 0.008f}, {0.035f, wy + 0.04f, d.td * 0.47f + 0.032f}));
            return m;
        });
    }
    o.Add("scarf/0", "scarf", FrameAA({-hs * 0.4f, d.shoulderY - 0.1f, -hs * 0.4f}, {hs * 0.4f, d.hb + 0.04f, hs * 0.4f}), [&](const std::string& dg) {
        const float nr = hs * 0.24f * 1.45f;
        PolyMesh m = Loft({{{0, d.shoulderY - 0.01f, 0}, nr * 1.2f, nr * 1.2f}, {{0, d.shoulderY + (d.hb - d.shoulderY) * 0.55f, 0}, nr * 1.05f, nr * 1.08f}, {{0, d.hb - 0.005f, 0}, nr, nr}}, kFwd, d.limbSides, false, false);
        if (dg == "long") shapes::Append(m, Loft({{{0.04f, d.shoulderY - 0.02f, nr * 1.05f}, 0.012f, 0.05f}, {{0.045f, d.shoulderY - 0.2f * d.tl, nr * 1.1f + 0.01f}, 0.012f, 0.055f}, {{0.05f, d.shoulderY - 0.45f * d.tl, nr * 1.12f + 0.012f}, 0.012f, 0.05f}}, kFwd, 8, false, false));
        return m;
    });
    // Hands and feet were built at the origin: move each to its joint.
    for (GeneratedPart& p : parts) {
        if (p.id.rfind("hand/", 0) == 0) {
            const int side = p.id.back() - '0';
            const float s = side ? 1.0f : -1.0f;
            const float ua = d.H * d.armF * 0.46f, fa = d.H * d.armF * 0.40f;
            const Vec3 S{s * d.sw * 0.5f, d.shoulderY - 0.04f, 0}, d1{s * std::sin(d.armA), -std::cos(d.armA), 0};
            const Vec3 E = S + d1 * ua;
            Vec3 d2{d1.x, d1.y * std::cos(d.bend), -d1.y * std::sin(d.bend)};
            const Vec3 W = E + d2.Normalized() * fa;
            for (Vec3& v : p.mesh.verts) v = v + W;
        }
    }
}


// ---- The skeleton -----------------------------------------------------------------------------------------------------------
bool HumanoidSkeleton(const Recipe& r, std::vector<RigJoint>& out) {
    out.clear();
    if (r.templateId != "human") return false;
    const Dim d = MakeDim(r);
    out.resize(15);
    auto set = [&](int i, const char* name, int parent, Vec3 pos, Vec3 tail) { out[i].name = name; out[i].parent = parent; out[i].pos = pos; out[i].tail = tail; };
    const Vec3 hips{0, d.hipY, 0}, spine{0, d.hipY + 0.5f * d.tl, 0}, headJ{0, d.shoulderY + (d.hb - d.shoulderY) * 0.5f, 0};
    set(0, "pelvis", -1, hips, spine);
    set(1, "spine_01", 0, spine, {0, d.shoulderY, 0});
    set(2, "head", 1, headJ, {0, d.hb + d.hs, 0});
    static const char* armN[2][3] = {{"upperarm_r", "lowerarm_r", "hand_r"}, {"upperarm_l", "lowerarm_l", "hand_l"}};
    static const char* legN[2][3] = {{"thigh_r", "calf_r", "foot_r"}, {"thigh_l", "calf_l", "foot_l"}};
    for (int side = 0; side < 2; ++side) {   // side 1 = +X = the figure's left (it faces +Z)
        const float s = side ? 1.0f : -1.0f;
        const float ua = d.H * d.armF * 0.46f, fa = d.H * d.armF * 0.40f, hl = d.H * d.armF * 0.14f;
        const Vec3 S{s * d.sw * 0.5f, d.shoulderY - 0.04f, 0};
        const Vec3 d1{s * std::sin(d.armA), -std::cos(d.armA), 0};
        const Vec3 E = S + d1 * ua;
        Vec3 d2{d1.x, d1.y * std::cos(d.bend), -d1.y * std::sin(d.bend)};
        d2 = d2.Normalized();
        const Vec3 W = E + d2 * fa;
        const int a0 = 3 + side * 0 + (side ? 0 : 3);   // left arm bones are 3..5, right 6..8
        set(a0, armN[side][0], 1, S, E);
        set(a0 + 1, armN[side][1], a0, E, W);
        set(a0 + 2, armN[side][2], a0 + 1, W, W + d2 * hl);
        const Vec3 Hp{s * d.sw * 0.22f, d.hipY - 0.04f, 0};
        const float L = std::max(0.3f, (Hp.y - d.ankleY) / std::cos(d.stance));
        const Vec3 l1{s * std::sin(d.stance), -std::cos(d.stance), 0};
        const Vec3 K = Hp + l1 * (L * 0.5f), A = Hp + l1 * L;
        const int l0 = side ? 9 : 12;
        set(l0, legN[side][0], 0, Hp, K);
        set(l0 + 1, legN[side][1], l0, K, A);
        set(l0 + 2, legN[side][2], l0 + 1, A, A + Vec3{0, 0, d.H * 0.07f});
    }
    return true;
}

void SkinVertices(const std::vector<Vec3>& verts, const std::vector<RigJoint>& joints,
                  std::vector<std::array<int, 4>>& idx, std::vector<std::array<float, 4>>& wt) {
    idx.assign(verts.size(), {0, 0, 0, 0});
    wt.assign(verts.size(), {0, 0, 0, 0});
    const int n = (int)joints.size();
    std::vector<float> dist((size_t)n);
    for (size_t v = 0; v < verts.size(); ++v) {
        const Vec3 p = verts[v];
        for (int j = 0; j < n; ++j) {
            const Vec3 a = joints[j].pos, ab = joints[j].tail - a;
            const float l2 = Vec3::Dot(ab, ab);
            float t = l2 > 1e-9f ? Vec3::Dot(p - a, ab) / l2 : 0.0f;
            t = std::min(1.0f, std::max(0.0f, t));
            dist[j] = (p - (a + ab * t)).Magnitude();
        }
        int best[3] = {-1, -1, -1};
        for (int k = 0; k < 3; ++k) {
            float bd = 1e30f;
            for (int j = 0; j < n; ++j) {
                if (j == best[0] || j == best[1]) continue;
                if (dist[j] < bd) { bd = dist[j]; best[k] = j; }
            }
        }
        float sum = 0;
        for (int k = 0; k < 3; ++k) {
            if (best[k] < 0) continue;
            const float w = 1.0f / std::pow(dist[best[k]] + 0.02f, 3.0f);
            // a bone far behind the nearest one adds nothing: drop it below a fifth of the best weight
            idx[v][k] = best[k]; wt[v][k] = w; sum += w;
        }
        const float top = wt[v][0];
        sum = 0;
        for (int k = 0; k < 3; ++k) { if (wt[v][k] < top * 0.2f) wt[v][k] = 0; sum += wt[v][k]; }
        for (int k = 0; k < 4; ++k) wt[v][k] = sum > 0 ? wt[v][k] / sum : (k == 0 ? 1.0f : 0.0f);
    }
}

} // namespace okay::modeling::guided
