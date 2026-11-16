// The organic human. Every Detail level (Low, Medium, High, Ultra) builds the person from smooth lofted tubes: a torso with
// chest, waist, hips and a trapezius slope; arms and legs with muscle and fat taper, elbows and knees that are real joints (a
// ball under a shared ring), a deltoid cap on each shoulder; a neck; a skull with jaw, brow and cheek; a face with nose, lips,
// eyes and ears; hands with a palm, four fingers (three bones each) and a thumb; feet with a heel, an arch, a ball and toes;
// hair styles; and clothes that are fitted shells following the same tubes. Detail changes how many rings and sides the tubes
// have (about 2 thousand triangles at Low, 8 thousand at Medium, 20 thousand and more at High, more again at Ultra), never the
// shape. Both the body-type and the build/muscle sliders move the table of ring sizes, so every start design is one body.
// Written from general modelling knowledge (lofting along a spine, superellipse sections, Catmull-Rom curves, the usual
// anatomical proportions: an adult is about 7.5 heads tall); no code, models or assets from other projects.
// The blocky character of style "Blocky" is the older generator in GuidedCharacters.cpp; it makes the same part ids and designs.
#include "GuidedKit.hpp"

namespace okay::modeling::guided {
namespace {
using namespace kit;

// ---- Detail and curves -------------------------------------------------------------------------------------------
float Mul() {   // how finely tubes are cut: Low about half, Medium 0.92, High 1.4, Ultra 1.9 of the base counts
    const float d = std::min(3.0f, std::max(0.0f, shapes::DetailF()));
    static const float m[4] = {0.5f, 0.92f, 1.4f, 1.9f};
    const int i = std::min(2, (int)d);
    return m[i] + (m[i + 1] - m[i]) * (d - (float)i);
}
int N(float base, int mn = 3) { return std::max(mn, (int)std::lround(base * Mul())); }
float Sm(float x) { x = std::min(1.0f, std::max(0.0f, x)); return x * x * (3.0f - 2.0f * x); }
float Lerp(float a, float b, float t) { return a + (b - a) * t; }
float Gauss(float x, float c, float w) { const float z = (x - c) / w; return std::exp(-z * z); }

Vec3 CRv(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, float t) {
    const float t2 = t * t, t3 = t2 * t;
    return (b * 2.0f + (c - a) * t + (a * 2.0f - b * 5.0f + c * 4.0f - d) * t2 + (b * 3.0f - a - c * 3.0f + d) * t3) * 0.5f;
}
float CRf(float a, float b, float c, float d, float t) {
    const float t2 = t * t, t3 = t2 * t;
    return (b * 2.0f + (c - a) * t + (a * 2.0f - b * 5.0f + c * 4.0f - d) * t2 + (b * 3.0f - a - c * 3.0f + d) * t3) * 0.5f;
}
// A smooth curve through the stations (Catmull-Rom on the centres and both radii), cut into `M` rings spaced evenly along the
// curve (a little closer where the radius changes fast). The first and last rings are the first and last stations.
std::vector<Station> Resample(const std::vector<Station>& s, int M) {
    const int n = (int)s.size();
    if (n < 2) return s;
    M = std::max(2, M);
    auto at = [&](int i) -> const Station& { return s[(size_t)std::min(n - 1, std::max(0, i))]; };
    std::vector<Station> dense;
    const int sub = 10;
    for (int i = 0; i + 1 < n; ++i)
        for (int k = 0; k < sub; ++k) {
            const float t = (float)k / (float)sub;
            Station o;
            o.c = CRv(at(i - 1).c, at(i).c, at(i + 1).c, at(i + 2).c, t);
            o.ra = std::max(0.001f, CRf(at(i - 1).ra, at(i).ra, at(i + 1).ra, at(i + 2).ra, t));
            o.rb = std::max(0.001f, CRf(at(i - 1).rb, at(i).rb, at(i + 1).rb, at(i + 2).rb, t));
            dense.push_back(o);
        }
    dense.push_back(s.back());
    std::vector<float> cum(dense.size(), 0.0f);
    for (size_t i = 1; i < dense.size(); ++i)
        cum[i] = cum[i - 1] + (dense[i].c - dense[i - 1].c).Magnitude() + 0.7f * (std::fabs(dense[i].ra - dense[i - 1].ra) + std::fabs(dense[i].rb - dense[i - 1].rb));
    std::vector<Station> out;
    size_t j = 0;
    for (int i = 0; i < M; ++i) {
        const float target = cum.back() * (float)i / (float)(M - 1);
        while (j + 2 < dense.size() && cum[j + 1] < target) ++j;
        const float span = std::max(1e-9f, cum[j + 1] - cum[j]);
        const float t = std::min(1.0f, std::max(0.0f, (target - cum[j]) / span));
        Station o;
        o.c = dense[j].c + (dense[j + 1].c - dense[j].c) * t;
        o.ra = Lerp(dense[j].ra, dense[j + 1].ra, t);
        o.rb = Lerp(dense[j].rb, dense[j + 1].rb, t);
        out.push_back(o);
    }
    return out;
}
PolyMesh SmoothLoft(const std::vector<Station>& st, Vec3 refA, int sides, int rings, bool roundStart, bool roundEnd, float expo = 2.0f) {
    return Loft(Resample(st, rings), refA, sides, roundStart, roundEnd, expo);
}

// An ellipsoid (own latitude-longitude mesh): the ring and sector counts are the Medium counts and follow the Detail level here, so the
// small parts (eyes, lips, joints) do not grow faster than the tubes.
PolyMesh Elp(Vec3 c, float rx, float ry, float rz, int rings = 6, int sectors = 10) {
    rings = N((float)rings, 3); sectors = N((float)sectors, 6);
    PolyMesh m;
    m.verts.push_back({c.x, c.y + ry, c.z});
    for (int r = 1; r < rings; ++r) {
        const float pol = kPi * (float)r / (float)rings;
        for (int k = 0; k < sectors; ++k) {
            const float az = 2.0f * kPi * (float)k / (float)sectors;
            m.verts.push_back({c.x + rx * std::sin(pol) * std::sin(az), c.y + ry * std::cos(pol), c.z + rz * std::sin(pol) * std::cos(az)});
        }
    }
    m.verts.push_back({c.x, c.y - ry, c.z});
    const int bottom = (int)m.verts.size() - 1;
    auto ring = [&](int r, int k) { return 1 + (r - 1) * sectors + (k % sectors); };
    for (int k = 0; k < sectors; ++k) { PolyFace t; t.verts = {0, ring(1, k), ring(1, k + 1)}; m.faces.push_back(t); }
    for (int r = 1; r + 1 < rings; ++r)
        for (int k = 0; k < sectors; ++k) { PolyFace q; q.verts = {ring(r, k), ring(r + 1, k), ring(r + 1, k + 1), ring(r, k + 1)}; m.faces.push_back(q); }
    for (int k = 0; k < sectors; ++k) { PolyFace t; t.verts = {bottom, ring(rings - 1, k + 1), ring(rings - 1, k)}; m.faces.push_back(t); }
    shapes::OrientOutward(m, c);
    return m;
}

// Push the surface near `c` along `dir` (a muscle, a bust, a brow ridge): only points whose position across `dir` is within R
// of c move, and only those on the same side, so the far side of a thin body is never touched.
void Bump(PolyMesh& m, Vec3 c, Vec3 dir, float R, float amt) {
    dir = dir.Normalized();
    for (Vec3& v : m.verts) {
        const Vec3 q = v - c;
        const float along = Vec3::Dot(q, dir);
        const Vec3 pl = q - dir * along;
        const float dd = pl.Magnitude();
        if (dd >= R || along < -R * 0.7f || along > R * 0.9f) continue;
        v = v + dir * (amt * Sm(1.0f - dd / R));
    }
}

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
void Move(PolyMesh& m, Vec3 d) { for (Vec3& v : m.verts) v = v + d; }

Frame BoundsFrame(const PolyMesh& m) {
    Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
    for (const Vec3& v : m.verts) { lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)}; hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)}; }
    if (m.verts.empty()) { lo = {0, 0, 0}; hi = {0.01f, 0.01f, 0.01f}; }
    return FrameAA(lo, hi);
}
// Add a part: built first so a user part can be fitted to its real size.
void AddPart(Out& o, const std::string& id, const std::string& slot, const std::function<PolyMesh(const std::string&)>& build) {
    const std::string dg = o.r.DesignFor(slot, id);
    if (dg == "none" || dg.empty()) return;
    PolyMesh m = build(dg);
    if (m.faces.empty()) return;
    if (dg.rfind("user:", 0) == 0) { const Frame f = BoundsFrame(m); m = FillUser(o.r, dg, f, [&] { return m; }); }
    o.parts.push_back({id, slot, std::move(m)});
}

// ---- The body's measurements -------------------------------------------------------------------------------------
struct Dim {
    float H, sw, td, hs, th, build, muscle, gk, hipY, shoulderY, hb, ankleY, tl, neckL, W, Dd, ua, fa, hl;
    int bt, style;
    float armA, bend, stance, limbK;
    Vec3 S[2], E[2], Wr[2], Hp[2], K[2], A[2], d1[2], d2[2], l1[2];   // [0] = -x (the figure's right), [1] = +x (its left)
    std::string torsoD, headD;
};
const Vec3 kFwd{0, 0, 1};

Dim MakeDim(const Recipe& r) {
    Dim d;
    d.H = Pf(r, "height"); d.sw = Pf(r, "shoulderWidth"); d.td = Pf(r, "torsoDepth"); d.hs = Pf(r, "headSize");
    d.th = Pf(r, "limbThickness");
    d.build = (float)r.Param("build", 0.5); d.muscle = (float)r.Param("muscle", 0.3);
    d.gk = (d.th / 0.045f) * (0.84f + 0.32f * d.build) * (1.0f + 0.08f * d.muscle);   // arm and leg girth: the slider, the build and the muscle
    const float neck = Pf(r, "neckLength");
    d.bt = (int)r.Param("bodyType"); d.style = (int)r.Param("style", 0);
    d.hipY = d.H * Pf(r, "legLength");
    d.shoulderY = std::max(d.hipY + 0.25f, d.H - neck - d.hs);
    d.hb = d.shoulderY + neck;
    d.ankleY = d.H * 0.035f;
    d.tl = d.shoulderY - d.hipY;
    d.neckL = neck;
    const float widthK = d.bt == 1 ? 1.04f : d.bt == 2 ? 0.95f : 1.0f;
    d.W = d.sw * 0.5f * widthK;
    d.Dd = d.td * 0.5f;
    const float armTotal = d.H * Pf(r, "armLength");
    d.ua = armTotal * 0.42f; d.fa = armTotal * 0.34f; d.hl = armTotal * 0.24f;
    d.armA = Pf(r, "armAngle") * kPi / 180.0f; d.bend = Pf(r, "elbowBend") * kPi / 180.0f; d.stance = Pf(r, "stance") * kPi / 180.0f;
    d.torsoD = r.DesignFor("torso", "torso/0"); d.headD = r.DesignFor("head", "head/0");
    d.limbK = 1.0f;
    const std::string legDesign = r.DesignFor("leg", "leg/0/upper");
    const float legK = legDesign == "thick" ? 1.25f : legDesign == "piston" ? 0.78f : 1.0f;
    for (int side = 0; side < 2; ++side) {
        const float s = side ? 1.0f : -1.0f;
        d.S[side] = {s * d.W * 1.0f, d.shoulderY - 0.045f, 0};
        d.d1[side] = {s * std::sin(d.armA), -std::cos(d.armA), 0};
        d.E[side] = d.S[side] + d.d1[side] * d.ua;
        Vec3 d2{d.d1[side].x, d.d1[side].y * std::cos(d.bend), -d.d1[side].y * std::sin(d.bend)};
        d.d2[side] = d2.Normalized();
        d.Wr[side] = d.E[side] + d.d2[side] * d.fa;
        d.Hp[side] = {s * std::max(d.W * 0.46f, 0.092f * d.gk * legK * 1.12f + 0.004f), d.hipY + 0.03f, 0};   // the thighs never touch at the middle
        const float L = std::max(0.3f, (d.Hp[side].y - d.ankleY) / std::cos(d.stance));
        d.l1[side] = {s * std::sin(d.stance), -std::cos(d.stance), 0};
        d.K[side] = d.Hp[side] + d.l1[side] * (L * 0.47f);
        d.A[side] = d.Hp[side] + d.l1[side] * L;
    }
    return d;
}
int Sides(const Dim& d, float base, int mn = 6) {
    int n = N(base, mn);
    if (d.style == 1) n = std::min(n, 8);   // stylised low poly: visible facets
    return n;
}
int Rings(const Dim& d, float base, int mn = 3) {
    int n = N(base, mn);
    if (d.style == 1) n = std::min(n, 5);
    return n;
}

// ---- The torso -----------------------------------------------------------------------------------------------------
struct TRow { float y, w, dp, cz; };   // y in torso lengths above the hip, half width in W units, half depth in Dd units, centre z (m)
const TRow kTorso[] = {
    {-0.125f, 0.50f, 0.60f, 0.0f}, {-0.07f, 0.78f, 0.90f, -0.002f}, {0.0f, 0.88f, 1.0f, 0.0f}, {0.15f, 0.86f, 0.92f, 0.0f},
    {0.33f, 0.72f, 0.80f, 0.002f}, {0.5f, 0.80f, 0.88f, 0.004f}, {0.68f, 0.86f, 0.98f, 0.006f}, {0.82f, 0.93f, 0.98f, 0.006f},
    {0.93f, 0.95f, 0.86f, 0.002f}, {1.0f, 0.90f, 0.72f, -0.004f}, {1.05f, 0.58f, 0.55f, -0.006f}, {1.10f, 0.30f, 0.40f, -0.006f}};

TRow TorsoRow(const Dim& d, const TRow& b) {
    TRow r = b;
    const float hipW = Gauss(b.y, 0.0f, 0.18f), waistW = Gauss(b.y, 0.33f, 0.18f), chestW = Gauss(b.y, 0.8f, 0.2f), shW = Gauss(b.y, 0.95f, 0.15f);
    const float mus = d.muscle, bd = d.build - 0.5f;
    float w = 1.0f, dp = 1.0f;
    if (d.bt == 1) { w += -0.06f * hipW + 0.05f * chestW; }
    if (d.bt == 2) { w += 0.08f * hipW - 0.10f * waistW - 0.03f * chestW; dp += 0.04f * chestW; }
    w += 0.07f * mus * chestW - 0.06f * mus * waistW + 0.05f * mus * shW;
    w += bd * 0.28f * (0.5f + 0.5f * waistW);
    dp += bd * 0.5f * (0.4f + 0.6f * waistW);
    if (d.torsoD == "tapered") { w *= 1.0f - 0.10f * waistW + 0.04f * shW; }
    else if (d.torsoD == "barrel") { w *= 1.0f + 0.12f * waistW; dp *= 1.12f; }
    else if (d.torsoD == "box") { w *= 1.0f + 0.05f * waistW; }
    r.w = b.w * w; r.dp = b.dp * dp;
    return r;
}
// The torso ring table, between two heights (in torso lengths), every ring grown by `off` (a shirt).
std::vector<Station> TorsoStations(const Dim& d, float off, float y0, float y1) {
    std::vector<TRow> rows;
    const int n = (int)(sizeof(kTorso) / sizeof(kTorso[0]));
    auto interp = [&](float y) {
        for (int i = 0; i + 1 < n; ++i)
            if (y >= kTorso[i].y && y <= kTorso[i + 1].y) {
                const float t = (y - kTorso[i].y) / (kTorso[i + 1].y - kTorso[i].y);
                TRow r{y, Lerp(kTorso[i].w, kTorso[i + 1].w, t), Lerp(kTorso[i].dp, kTorso[i + 1].dp, t), Lerp(kTorso[i].cz, kTorso[i + 1].cz, t)};
                return r;
            }
        return kTorso[y < 0 ? 0 : n - 1];
    };
    rows.push_back(interp(y0));
    for (int i = 0; i < n; ++i) if (kTorso[i].y > y0 + 0.02f && kTorso[i].y < y1 - 0.02f) rows.push_back(kTorso[i]);
    rows.push_back(interp(y1));
    std::vector<Station> st;
    for (const TRow& b : rows) {
        const TRow r = TorsoRow(d, b);
        st.push_back({{0, d.hipY + r.y * d.tl, r.cz}, std::max(0.01f, d.Dd * r.dp + off), std::max(0.01f, d.W * r.w + off)});
    }
    return st;
}
float TorsoHalfDepthAt(const Dim& d, float yAbs) {   // the skin's front-to-back half size at a height (for placing details)
    const float y = std::max(-0.125f, std::min(1.1f, (yAbs - d.hipY) / d.tl));
    const std::vector<Station> st = TorsoStations(d, 0.0f, y - 0.001f, y + 0.001f);
    return st.empty() ? d.Dd : st[0].ra;
}
void SculptTorso(PolyMesh& m, const Dim& d, float off) {
    const float bd = d.build - 0.5f;
    auto front = [&](float yRel) { return TorsoHalfDepthAt(d, d.hipY + yRel * d.tl) + off; };
    if (d.bt == 2) {   // bust
        const float y = d.hipY + 0.72f * d.tl;
        for (int s = -1; s <= 1; s += 2) Bump(m, {s * d.W * 0.40f, y, front(0.72f)}, Vec3{s * 0.15f, -0.08f, 1.0f}, d.W * 0.42f, 0.032f * (0.8f + 0.5f * d.build));
    } else {            // chest muscle
        const float y = d.hipY + 0.76f * d.tl;
        for (int s = -1; s <= 1; s += 2) Bump(m, {s * d.W * 0.42f, y, front(0.76f)}, Vec3{s * 0.1f, -0.05f, 1.0f}, d.W * 0.44f, 0.008f + 0.022f * d.muscle + (d.bt == 1 ? 0.006f : 0.0f));
    }
    if (d.muscle > 0.55f) {   // abdominal blocks
        for (int row = 0; row < 3; ++row)
            for (int s = -1; s <= 1; s += 2) Bump(m, {s * d.W * 0.16f, d.hipY + (0.22f + 0.13f * (float)row) * d.tl, front(0.22f + 0.13f * (float)row)}, kFwd, d.W * 0.2f, 0.006f * (d.muscle - 0.4f));
    }
    if (bd > 0.0f || d.bt == 1) Bump(m, {0, d.hipY + 0.28f * d.tl, front(0.28f)}, kFwd, d.W * 0.6f, std::max(0.0f, bd) * 0.14f + (d.bt == 1 ? 0.006f : 0.0f));   // belly
    for (int s = -1; s <= 1; s += 2) Bump(m, {s * d.W * 0.40f, d.hipY - 0.01f, -front(-0.02f)}, Vec3{s * 0.1f, -0.1f, -1.0f}, d.W * 0.46f, 0.018f + (d.bt == 2 ? 0.010f : 0.0f) + bd * 0.03f);   // buttocks
    for (int s = -1; s <= 1; s += 2) Bump(m, {s * d.W * 0.78f, d.hipY + 0.9f * d.tl, 0}, Vec3{s * 1.0f, 0.4f, 0}, d.W * 0.36f, 0.006f + 0.012f * d.muscle);   // the trapezius where it meets the shoulder
}
PolyMesh TorsoShell(const Dim& d, float off, float y0, float y1) {
    PolyMesh m = SmoothLoft(TorsoStations(d, off, y0, y1), kFwd, Sides(d, 16, 8), Rings(d, 12, 5), false, false, 2.3f);
    SculptTorso(m, d, off);
    return m;
}

// ---- Limbs ---------------------------------------------------------------------------------------------------------
struct LP { float t, ra, rb, cz; };   // along the limb 0..1, radius front-to-back and side to side (m), centre shift forward (m)
const std::vector<LP> kUpperArm = {{0.0f, 0.052f, 0.050f, 0}, {0.2f, 0.053f, 0.051f, 0.001f}, {0.45f, 0.051f, 0.046f, 0.003f}, {0.75f, 0.042f, 0.039f, 0.001f}, {1.0f, 0.036f, 0.034f, 0}};
const std::vector<LP> kForearm = {{0.0f, 0.036f, 0.037f, 0}, {0.15f, 0.040f, 0.042f, 0}, {0.5f, 0.033f, 0.036f, 0}, {0.8f, 0.026f, 0.030f, 0}, {1.0f, 0.022f, 0.027f, 0}};
const std::vector<LP> kThigh = {{0.0f, 0.088f, 0.088f, 0}, {0.18f, 0.092f, 0.090f, 0}, {0.5f, 0.082f, 0.078f, 0.002f}, {0.8f, 0.064f, 0.060f, 0}, {1.0f, 0.054f, 0.052f, 0}};
const std::vector<LP> kCalf = {{0.0f, 0.054f, 0.053f, 0}, {0.22f, 0.058f, 0.056f, -0.007f}, {0.4f, 0.057f, 0.054f, -0.008f}, {0.75f, 0.040f, 0.040f, -0.002f}, {1.0f, 0.032f, 0.031f, 0}};

// A tube from a to b: the profile's radii times `scale` plus `off`, cut at `tmax`. `startShift` moves the first ring a little along
// the limb so two tubes that meet at a joint do not share ring corners.
PolyMesh LimbTube(Vec3 a, Vec3 b, const std::vector<LP>& prof, float scale, float off, float tmax, int sides, int rings, bool roundStart, bool roundEnd, float before = 0.0f, float after = 0.0f) {
    std::vector<Station> st;
    auto lp = [&](float t) {
        for (size_t i = 0; i + 1 < prof.size(); ++i)
            if (t >= prof[i].t - 1e-6f && t <= prof[i + 1].t + 1e-6f) {
                const float u = (t - prof[i].t) / (prof[i + 1].t - prof[i].t);
                return LP{t, Lerp(prof[i].ra, prof[i + 1].ra, u), Lerp(prof[i].rb, prof[i + 1].rb, u), Lerp(prof[i].cz, prof[i + 1].cz, u)};
            }
        return prof.back();
    };
    auto push = [&](const LP& p) { st.push_back({a + (b - a) * p.t + Vec3{0, 0, p.cz * scale}, p.ra * scale + off, p.rb * scale + off}); };
    if (before > 0.0f) { LP p = prof.front(); p.t = -before; push(p); }   // a short run-in so the joint's two tubes overlap
    bool done = false;
    for (const LP& p : prof) {
        if (p.t > tmax + 1e-4f) break;
        push(p);
        if (p.t >= tmax - 1e-4f) { done = true; break; }
    }
    if (!done) push(lp(tmax));
    if (after > 0.0f && tmax >= 0.999f) { LP p = prof.back(); p.t = 1.0f + after; push(p); }
    if (st.size() < 2) return PolyMesh();
    return SmoothLoft(st, kFwd, sides, std::max(3, (int)std::lround((float)rings * std::max(0.35f, tmax))), roundStart, roundEnd);
}
PolyMesh Ball(Vec3 c, float r, float ky = 1.0f) { return Elp(c, r, r * ky, r, 5, 10); }

PolyMesh UpperArm(const Dim& d, int side, float off, float tmax, bool deltoid) {
    const float sc = d.gk * d.limbK;
    PolyMesh m = LimbTube(d.S[side], d.E[side], kUpperArm, sc, off, tmax, Sides(d, 12, 6), Rings(d, 8, 4), false, false, 0.0f, 0.05f);
    const float s = side ? 1.0f : -1.0f;
    if (deltoid) {   // the deltoid cap over the shoulder joint, bigger with muscle
        const float k = sc * (1.0f + 0.2f * d.muscle);
        shapes::Append(m, Elp(d.S[side] + Vec3{s * 0.002f, -0.016f, 0.0f}, 0.048f * k + off, 0.056f * k + off, 0.048f * k + off, 5, 11));
    }
    if (tmax >= 0.999f) shapes::Append(m, Ball(d.E[side], 0.034f * sc + off));
    return m;
}
PolyMesh ForeArm(const Dim& d, int side, float off, float tmax) {
    const float sc = d.gk * d.limbK;
    return LimbTube(d.E[side], d.Wr[side], kForearm, sc, off, tmax, Sides(d, 12, 6), Rings(d, 8, 4), false, false, 0.05f);
}
PolyMesh Thigh(const Dim& d, int side, float off, float tmax) {
    const float sc = d.gk * d.limbK * 1.12f;
    PolyMesh m = LimbTube(d.Hp[side], d.K[side], kThigh, sc, off, tmax, Sides(d, 16, 7), Rings(d, 9, 4), false, false, 0.0f, 0.05f);
    if (tmax >= 0.999f) shapes::Append(m, Ball(d.K[side], 0.052f * sc + off));
    return m;
}
PolyMesh Calf(const Dim& d, int side, float off, float tmax) {
    const float sc = d.gk * d.limbK * 1.12f;
    return LimbTube(d.K[side], d.A[side], kCalf, sc, off, tmax, Sides(d, 14, 6), Rings(d, 9, 4), false, false, 0.05f);
}

// ---- Head and face -------------------------------------------------------------------------------------------------
struct HeadShape {
    float s;                       // size relative to a 0.233 m head
    std::vector<Station> fine;     // the skull, chin to crown
    float yb;                      // the chin's height
    std::string design;
};
HeadShape ShapeOfHead(const Dim& d) {
    HeadShape h;
    h.s = d.hs / 0.233f; h.yb = d.hb; h.design = d.headD;
    struct R { float y, cz, ra, rb; };
    std::vector<R> rows = {{0.0f, 0.050f, 0.020f, 0.028f}, {0.012f, 0.046f, 0.034f, 0.040f}, {0.030f, 0.038f, 0.050f, 0.050f}, {0.055f, 0.024f, 0.071f, 0.059f}, {0.082f, 0.013f, 0.085f, 0.065f},
                           {0.112f, 0.007f, 0.091f, 0.071f}, {0.150f, 0.0f, 0.094f, 0.075f}, {0.176f, -0.003f, 0.092f, 0.073f}, {0.199f, -0.004f, 0.080f, 0.063f}, {0.2175f, -0.004f, 0.062f, 0.049f}, {0.2285f, -0.003f, 0.038f, 0.030f}, {0.2328f, -0.003f, 0.016f, 0.013f}};
    float wK = 1.0f, jawW = 1.0f, hK = 1.0f, chinZ = 1.0f, jawD = 1.0f;
    const std::string& g = h.design;
    if (g == "oval") { wK = 0.92f; jawW = 0.92f; }
    else if (g == "box") { jawW = 1.18f; chinZ = 0.9f; jawD = 1.1f; }
    else if (g == "pointed") { jawW = 0.78f; chinZ = 1.25f; }
    else if (g == "heart") { jawW = 0.72f; wK = 1.04f; chinZ = 1.1f; }
    else if (g == "long") { wK = 0.92f; hK = 1.12f; }
    else if (g == "wide") { wK = 1.16f; jawW = 1.05f; }
    if (d.bt == 1) { jawW *= 1.06f; } else if (d.bt == 2) { jawW *= 0.94f; }   // women: a slightly smaller jaw; men: a squarer one
    for (R& r : rows) {
        const float low = Sm(1.0f - r.y / 0.11f);   // how much of the lower face this ring is
        r.rb *= wK * (1.0f + (jawW - 1.0f) * low);
        r.ra *= 1.0f + (jawD - 1.0f) * low;
        r.cz *= 1.0f + (chinZ - 1.0f) * low;
        r.y *= hK;
    }
    std::vector<Station> st;
    for (const R& r : rows) st.push_back({{0, h.yb + r.y * h.s, r.cz * h.s}, r.ra * h.s, r.rb * h.s});
    h.fine = Resample(st, 64);
    return h;
}
float FrontZ(const HeadShape& h, float x, float y) {   // the skull's front surface at (x, y)
    const std::vector<Station>& f = h.fine;
    if (y <= f.front().c.y) return f.front().c.z;
    for (size_t i = 0; i + 1 < f.size(); ++i)
        if (y <= f[i + 1].c.y) {
            const float t = (y - f[i].c.y) / std::max(1e-6f, f[i + 1].c.y - f[i].c.y);
            const float cz = Lerp(f[i].c.z, f[i + 1].c.z, t), ra = Lerp(f[i].ra, f[i + 1].ra, t), rb = Lerp(f[i].rb, f[i + 1].rb, t);
            const float u = std::min(0.999f, std::fabs(x) / rb);
            return cz + ra * std::sqrt(1.0f - u * u);
        }
    return f.back().c.z;
}
float HalfWidthAt(const HeadShape& h, float y) {
    const std::vector<Station>& f = h.fine;
    for (size_t i = 0; i + 1 < f.size(); ++i)
        if (y <= f[i + 1].c.y) { const float t = (y - f[i].c.y) / std::max(1e-6f, f[i + 1].c.y - f[i].c.y); return Lerp(f[i].rb, f[i + 1].rb, std::min(1.0f, std::max(0.0f, t))); }
    return f.back().rb;
}
PolyMesh Skull(const Dim& d, const HeadShape& h) {
    (void)d;
    PolyMesh m = Loft(Resample(h.fine, std::max(10, N(24, 10))), kFwd, std::max(10, N(24, 10)), false, true);
    const float s = h.s, yb = h.yb;
    for (int sg = -1; sg <= 1; sg += 2) {
        Bump(m, {sg * 0.032f * s, yb + 0.140f * s, FrontZ(h, sg * 0.032f * s, yb + 0.140f * s)}, kFwd, 0.045f * s, 0.006f * s);                                       // brow ridge
        Bump(m, {sg * 0.052f * s, yb + 0.095f * s, FrontZ(h, sg * 0.052f * s, yb + 0.095f * s)}, Vec3{sg * 0.5f, 0.0f, 0.8f}, 0.045f * s, 0.005f * s);               // cheekbone
        Bump(m, {sg * 0.035f * s, yb + 0.050f * s, FrontZ(h, sg * 0.035f * s, yb + 0.050f * s)}, Vec3{sg * 0.4f, 0.0f, 0.9f}, 0.035f * s, 0.004f * s);               // cheek
        Bump(m, {sg * 0.032f * s, yb + 0.118f * s, FrontZ(h, sg * 0.032f * s, yb + 0.118f * s)}, kFwd, 0.022f * s, -0.004f * s);                                     // the eye socket
    }
    Bump(m, {0, yb + 0.018f * s, FrontZ(h, 0, yb + 0.018f * s)}, kFwd, 0.03f * s, 0.004f * s);                                                                    // the chin
    return m;
}
PolyMesh Neck(const Dim& d, const HeadShape& h, const std::string& design) {
    const float nr = (design == "thin" ? 0.043f : 0.050f) * std::max(0.5f, d.hs / 0.233f) * (0.92f + 0.16f * d.build) * (d.bt == 2 ? 0.93f : d.bt == 1 ? 1.05f : 1.0f);
    const float y0 = d.shoulderY - 0.035f, y1 = h.yb + 0.06f * h.s;
    std::vector<Station> st = {{{0, y0, 0.0f}, nr * 1.22f, nr * 1.3f}, {{0, Lerp(y0, y1, 0.35f), 0.002f}, nr * 1.02f, nr * 1.08f}, {{0, Lerp(y0, y1, 0.75f), -0.004f}, nr * 0.95f, nr * 1.0f}, {{0, y1, -0.012f}, nr * 0.98f, nr * 1.0f}};
    return SmoothLoft(st, kFwd, Sides(d, 14, 6), Rings(d, 7, 3), false, false);
}
// Two eyes as two parts: the white ball and the iris (a darker disc on its front), so each takes its own colour.
// (Lids would be skin coloured, which a part of the eyes cannot be; they are left out.)
PolyMesh EyeBalls(const HeadShape& h, const std::string& design, bool iris) {
    PolyMesh m;
    const float s = h.s, ey = h.yb + 0.118f * s, ex = (design == "big" ? 0.034f : 0.032f) * s;
    const float er = (design == "big" ? 0.0145f : design == "narrow" ? 0.0098f : 0.0118f) * s;
    for (int sg = -1; sg <= 1; sg += 2) {
        const float z = FrontZ(h, sg * ex, ey) - er * 0.45f;
        const float ry = er * (design == "narrow" ? 0.65f : 0.92f);
        if (!iris) {
            shapes::Append(m, Elp({sg * ex, ey, z}, er, ry, er * 0.9f, 4, 8));
        } else {
            shapes::Append(m, Elp({sg * ex * 0.97f, ey, z + er * 0.80f}, er * 0.55f, std::min(er * 0.55f, ry * 0.92f), er * 0.34f, 3, 8));
        }
    }
    return m;
}
PolyMesh EyesMesh(const Dim& d, const HeadShape& h, const std::string& design) { (void)d; return EyeBalls(h, design, true); }
PolyMesh EyeWhitesMesh(const Dim& d, const HeadShape& h, const std::string& design) { (void)d; return EyeBalls(h, design, false); }
PolyMesh BrowsMesh(const Dim& d, const HeadShape& h, const std::string& design) {
    PolyMesh m;
    const float s = h.s;
    (void)d;
    for (int sg = -1; sg <= 1; sg += 2) {
        const float w = (design == "thick" ? 0.026f : 0.023f) * s, t = (design == "thick" ? 0.0075f : 0.0045f) * s;
        for (int k = 0; k < 2; ++k) {   // an inner and an outer half, the outer a little higher (the arch)
            const float x = sg * (0.024f + 0.021f * (float)k) * s, y = h.yb + (0.140f + (design == "angular" ? 0.008f * (float)k : 0.004f * (float)k)) * s;
            shapes::Append(m, Elp({x, y, FrontZ(h, x, y) - 0.001f * s}, w * 0.6f, t, t * 1.6f, 3, 6));
        }
    }
    return m;
}
PolyMesh NoseMesh(const Dim& d, const HeadShape& h, const std::string& design) {
    PolyMesh m;
    const float s = h.s;
    (void)d;
    float sz = 1.0f, len = 1.0f, wide = 1.0f, proj = 1.0f;
    if (design == "button") { sz = 0.8f; len = 0.8f; proj = 0.9f; }
    else if (design == "long") { len = 1.25f; proj = 1.2f; }
    else if (design == "flat") { wide = 1.25f; proj = 0.7f; }
    const float yb = h.yb + 0.085f * s;   // the base of the nose
    const float zf = FrontZ(h, 0, yb);
    const float tipZ = zf + 0.019f * s * proj * sz;
    const int rings = std::max(3, N(4, 3)), sec = std::max(6, N(8, 6));
    shapes::Append(m, Elp({0, yb + 0.012f * s * len, tipZ - 0.006f * s}, 0.0105f * s * wide * sz, 0.0105f * s * sz, 0.0115f * s * sz, rings, sec));   // the tip
    for (int sg = -1; sg <= 1; sg += 2) shapes::Append(m, Elp({sg * 0.0115f * s * wide * sz, yb + 0.004f * s, zf + 0.0075f * s * proj}, 0.0062f * s * sz, 0.0065f * s * sz, 0.0075f * s, 3, 6));   // the wings
    const float y0 = yb + 0.040f * s * len;   // the bridge: from between the eyes to the tip, a tapered tube
    std::vector<Station> st = {{{0, y0, FrontZ(h, 0, y0) + 0.0005f * s}, 0.004f * s, 0.006f * s}, {{0, Lerp(y0, yb + 0.012f * s * len, 0.5f), Lerp(FrontZ(h, 0, y0), tipZ - 0.006f * s, 0.5f) + 0.002f * s}, 0.0068f * s * proj, 0.0075f * s * wide * sz},
                               {{0, yb + 0.012f * s * len, tipZ - 0.007f * s}, 0.0085f * s * proj, 0.0085f * s * wide * sz}};
    shapes::Append(m, Loft(st, kFwd, std::max(5, N(6, 5)), false, false));
    return m;
}
PolyMesh MouthMesh(const Dim& d, const HeadShape& h, const std::string& design) {
    PolyMesh m;
    const float s = h.s, my = h.yb + 0.056f * s;
    (void)d;
    const float w = (design == "small" ? 0.017f : 0.022f) * s;
    const int rings = std::max(3, N(4, 3)), sec = std::max(6, N(10, 6));
    shapes::Append(m, Elp({0, my + 0.005f * s, FrontZ(h, 0, my + 0.005f * s) - 0.002f * s}, w, 0.0050f * s, 0.0070f * s, rings, sec));          // the upper lip
    shapes::Append(m, Elp({0, my - 0.0055f * s, FrontZ(h, 0, my - 0.0055f * s) - 0.0015f * s}, w * 0.88f, 0.0065f * s, 0.0078f * s, rings, sec));   // the lower lip
    if (design == "smile") for (int sg = -1; sg <= 1; sg += 2) shapes::Append(m, Elp({sg * (w + 0.0035f * s), my + 0.0085f * s, FrontZ(h, sg * w, my) - 0.002f * s}, 0.0045f * s, 0.0045f * s, 0.006f * s, 3, 6));
    return m;
}
PolyMesh EarsMesh(const Dim& d, const HeadShape& h, const std::string& design) {
    PolyMesh m;
    const float s = h.s, k = design == "small" ? 0.8f : design == "big" ? 1.25f : 1.0f;
    (void)d;
    const float y = h.yb + 0.108f * s;
    for (int sg = -1; sg <= 1; sg += 2) {
        const float x = sg * (HalfWidthAt(h, y) + 0.001f * s);
        shapes::Append(m, Elp({x, y, -0.012f * s}, 0.0075f * s * k, 0.030f * s * k, 0.0185f * s * k, 4, 8));
        if (shapes::Detail() >= 2) {
            shapes::Append(m, Elp({x + sg * 0.004f * s * k, y + 0.002f * s, -0.016f * s}, 0.0045f * s * k, 0.024f * s * k, 0.012f * s * k, 3, 6));    // the rim
            shapes::Append(m, Elp({x + sg * 0.0015f * s, y - 0.026f * s * k, -0.008f * s}, 0.0055f * s * k, 0.0065f * s * k, 0.008f * s * k, 3, 6));  // the lobe
        }
    }
    return m;
}

// ---- Hair ----------------------------------------------------------------------------------------------------------
// A cap over the top of the skull, thickness `t` (head units), `boost` extra on top. Above the hairline (y = 0.165) it covers the
// whole skull; below it the ring shrinks to the back and the sides (the front edge pulls back to the ear), down to `yLow`.
PolyMesh HairCap(const HeadShape& h, float yLow, float t, float boost, int sides, int rings) {
    std::vector<Station> st;
    const float yBot = h.yb + yLow * h.s, yHair = h.yb + 0.19f * h.s;
    for (const Station& f : h.fine) {
        if (f.c.y < yBot - 1e-5f) continue;
        const float top = Sm((f.c.y - h.yb) / (0.2328f * h.s));
        const float ra = f.ra + t * h.s + boost * h.s * top, rb = f.rb + t * h.s + boost * h.s * top * 0.8f;
        const float below = Sm((yHair - f.c.y) / (0.05f * h.s));   // 0 at the hairline, 1 well below it
        float front = f.c.z + ra, back = f.c.z - ra;
        front = Lerp(front, f.c.z + 0.2f * f.ra, below);
        st.push_back({{0, f.c.y, (front + back) * 0.5f}, (front - back) * 0.5f, rb});
    }
    if (st.size() < 3) return PolyMesh();
    {   // round the crown off: two smaller rings above the last one, so the cap is a dome and not a point
        const Station last = st.back();
        st.push_back({last.c + Vec3{0, 0.007f * h.s, 0}, last.ra * 0.7f, last.rb * 0.7f});
        st.push_back({last.c + Vec3{0, 0.012f * h.s, 0}, last.ra * 0.35f, last.rb * 0.35f});
    }
    return Loft(Resample(st, rings), kFwd, sides, false, false);
}
PolyMesh HairMesh(const Dim& d, const HeadShape& h, const std::string& design) {
    PolyMesh m;
    const float s = h.s, yb = h.yb;
    const int sides = std::max(10, N(20, 10)), rings = std::max(6, N(12, 6));
    const float hy = yb + 0.233f * s;   // the crown
    if (design == "buzz") return HairCap(h, 0.12f, 0.004f, 0.0f, sides, rings);
    if (design == "afro") return Elp({0, yb + 0.15f * s, -0.012f * s}, 0.135f * s, 0.12f * s, 0.135f * s, 8, 16);
    const bool thin = design == "mohawk";
    m = HairCap(h, thin ? 0.19f : design == "spiky" ? 0.165f : 0.085f, thin ? 0.004f : 0.011f, design == "curly" ? 0.004f : 0.012f, sides, rings);
    if (design == "sidepart")   // a swept fringe over one brow
        shapes::Append(m, Elp({0.026f * s, yb + 0.178f * s, FrontZ(h, 0.026f * s, yb + 0.178f * s) - 0.004f * s}, 0.052f * s, 0.022f * s, 0.030f * s, 4, 10));
    if (design == "bob") {
        std::vector<Station> st;
        for (const Station& f : h.fine) {
            if (f.c.y > yb + 0.165f * s || f.c.y < yb + 0.02f * s) continue;
            st.push_back({{0, f.c.y, f.c.z - 0.02f * s}, f.ra + 0.016f * s * (1.0f + 0.5f * Sm(1.0f - (f.c.y - yb) / (0.14f * s))), f.rb + 0.022f * s});
        }
        if (st.size() >= 2) shapes::Append(m, Loft(Resample(st, std::max(4, N(7, 4))), kFwd, sides, true, false));
    }
    if (design == "long") {   // a curtain down the back to the shoulder blades and a lock beside each cheek
        const float yT = yb + 0.15f * s, yL = d.shoulderY - 0.13f;
        std::vector<Station> st;
        const int n = 6;
        for (int i = 0; i < n; ++i) {
            const float u = (float)i / (float)(n - 1);
            const float y = Lerp(yT, yL, u);
            const float zc = Lerp(-0.08f * s, -TorsoHalfDepthAt(d, y) * 0.9f - 0.012f, Sm(u * 1.4f));
            st.push_back({{0, y, zc}, Lerp(0.030f, 0.026f, u) * s, Lerp(0.085f, 0.09f, u) * s});
        }
        shapes::Append(m, SmoothLoft(st, kFwd, std::max(8, N(14, 8)), std::max(5, N(9, 5)), false, true));
        for (int sg = -1; sg <= 1; sg += 2) {
            const float x = sg * (HalfWidthAt(h, yb + 0.1f * s) + 0.004f * s);
            std::vector<Station> sl = {{{x, yb + 0.15f * s, -0.01f * s}, 0.032f * s, 0.011f * s}, {{x, yb + 0.07f * s, -0.01f * s}, 0.03f * s, 0.011f * s}, {{x, yb - 0.02f * s, -0.012f * s}, 0.026f * s, 0.010f * s}};
            shapes::Append(m, SmoothLoft(sl, kFwd, 8, std::max(4, N(6, 4)), false, true));
        }
    }
    if (design == "ponytail") {
        std::vector<Station> st = {{{0, yb + 0.175f * s, -0.07f * s}, 0.03f * s, 0.03f * s}, {{0, yb + 0.15f * s, -0.12f * s}, 0.034f * s, 0.032f * s}, {{0, yb + 0.07f * s, -0.15f * s}, 0.03f * s, 0.028f * s}, {{0, yb - 0.04f * s, -0.145f * s}, 0.022f * s, 0.02f * s}, {{0, yb - 0.09f * s, -0.14f * s}, 0.012f * s, 0.012f * s}};
        shapes::Append(m, SmoothLoft(st, kFwd, std::max(8, N(10, 8)), std::max(6, N(10, 6)), false, true));
    }
    if (design == "bun") shapes::Append(m, Elp({0, yb + 0.205f * s, -0.062f * s}, 0.048f * s, 0.045f * s, 0.048f * s, 6, 12));
    if (design == "braid")
        for (int i = 0; i < 7; ++i) {
            const float u = (float)i / 6.0f;
            shapes::Append(m, Elp({(i % 2 ? 0.006f : -0.006f) * s, yb + (0.15f - 0.2f * u) * s, -(0.1f + 0.045f * u) * s}, 0.021f * s * (1.0f - 0.3f * u), 0.022f * s, 0.021f * s, 3, 8));
        }
    if (design == "spiky") {
        Rnd rnd(7);
        const int n = std::max(9, N(14, 9));
        for (int i = 0; i < n; ++i) {
            const float a = 6.2831853f * (float)i / (float)n, rr = i % 2 ? 0.7f : 0.45f;
            const Vec3 base{std::cos(a) * 0.07f * s * rr, hy - 0.022f * s, std::sin(a) * 0.08f * s * rr - 0.004f * s};
            shapes::Append(m, Cone(base - Vec3{0, 0.01f * s, 0}, base + Vec3{std::cos(a) * 0.025f * s, (0.055f + 0.015f * rnd.F()) * s, std::sin(a) * 0.025f * s}, 0.022f * s, 6));
        }
    }
    if (design == "mohawk") {
        const int n = std::max(6, N(8, 6));
        for (int i = 0; i < n; ++i) {
            const float u = (float)i / (float)(n - 1), z = Lerp(0.07f, -0.085f, u) * s;
            const float y = yb + (0.215f - 0.07f * u * u) * s + 0.004f * s;
            shapes::Append(m, Cone({0, y - 0.015f * s, z}, {0, y + (0.05f + 0.02f * std::sin(3.14159f * u)) * s, z - 0.01f * s}, 0.021f * s, 6));
        }
    }
    if (design == "curly") {
        Rnd rnd(7);
        const int n = std::max(14, N(26, 14));
        for (int i = 0; i < n; ++i) {
            const float a = 6.2831853f * rnd.F(), e = 0.15f + 1.2f * rnd.F();
            const Vec3 c{std::cos(a) * std::cos(e) * 0.088f * s, yb + 0.12f * s + std::sin(e) * 0.12f * s, std::sin(a) * std::cos(e) * 0.1f * s - 0.005f * s};
            shapes::Append(m, Elp(c, 0.026f * s, 0.026f * s, 0.026f * s, 3, 7));
        }
    }
    return m;
}


// ---- Hats and glasses (fitted to this skull) -------------------------------------------------------------------------------
PolyMesh HatMesh(const Dim& d, const HeadShape& h, const std::string& design) {
    (void)d;
    const float s = h.s, yb = h.yb;
    const int sides = std::max(10, N(20, 10)), rings = std::max(6, N(12, 6));
    PolyMesh m;
    if (design == "tophat") {
        std::vector<Station> st = {{{0, yb + 0.185f * s, -0.004f * s}, 0.098f * s, 0.082f * s}, {{0, yb + 0.25f * s, -0.004f * s}, 0.096f * s, 0.080f * s}, {{0, yb + 0.35f * s, -0.004f * s}, 0.094f * s, 0.078f * s}};
        m = Loft(Resample(st, 4), kFwd, sides, false, false);
        shapes::Append(m, Elp({0, yb + 0.185f * s, -0.004f * s}, 0.128f * s, 0.006f * s, 0.140f * s, 3, 16));
        return m;
    }
    if (design == "helmet") {
        m = HairCap(h, 0.06f, 0.026f, 0.014f, sides, rings);
        shapes::Append(m, Elp({0, yb + 0.07f * s, -0.01f * s}, 0.094f * s, 0.008f * s, 0.112f * s, 3, 16));   // the rim
        return m;
    }
    if (design == "beanie") {
        m = HairCap(h, 0.11f, 0.02f, 0.02f, sides, rings);
        shapes::Append(m, Elp({0, yb + 0.285f * s, -0.004f * s}, 0.026f * s, 0.026f * s, 0.026f * s, 4, 8));   // a bobble
        return m;
    }
    m = HairCap(h, 0.12f, 0.02f, 0.016f, sides, rings);   // cap
    shapes::Append(m, Elp({0, yb + 0.155f * s, FrontZ(h, 0, yb + 0.155f * s) + 0.034f * s}, 0.07f * s, 0.005f * s, 0.052f * s, 3, 12));   // the peak
    return m;
}
PolyMesh GlassesMesh(const Dim& d, const HeadShape& h, const std::string& design) {
    (void)d;
    const float s = h.s, ey = h.yb + 0.118f * s;
    PolyMesh m;
    auto zAt = [&](float x) { return FrontZ(h, x, ey) + 0.0125f * s; };
    if (design == "visor") {   // one band across both eyes
        std::vector<Station> st;
        for (int i = 0; i <= 6; ++i) { const float x = (-0.075f + 0.025f * (float)i) * s; st.push_back({{x, ey, FrontZ(h, x, ey) + 0.012f * s}, 0.018f * s, 0.010f * s}); }
        shapes::Append(m, Loft(Resample(st, std::max(5, N(8, 5))), {0, 1, 0}, std::max(6, N(8, 6)), true, true));
        return m;
    }
    const bool square = design == "square";
    for (int sg = -1; sg <= 1; sg += 2) {
        const float cx = sg * 0.033f * s;
        const int beads = std::max(10, N(14, 10));
        for (int k = 0; k < beads; ++k) {   // the frame: a ring of small beads around the eye
            const float a = 2.0f * kPi * (float)k / (float)beads;
            float ux = std::cos(a), uy = std::sin(a);
            if (square) { const float m2 = std::max(std::fabs(ux), std::fabs(uy)); ux /= m2; uy /= m2; }
            const float x = cx + ux * 0.0185f * s, y = ey + uy * 0.0155f * s;
            shapes::Append(m, Elp({x, y, FrontZ(h, x, y) + 0.0125f * s}, 0.0032f * s, 0.0032f * s, 0.0032f * s, 3, 6));
        }
        // the arm back to the ear
        std::vector<Station> t = {{{sg * 0.0525f * s, ey, FrontZ(h, sg * 0.052f * s, ey) + 0.01f * s}, 0.003f * s, 0.003f * s}, {{sg * 0.076f * s, ey, 0.0f}, 0.003f * s, 0.003f * s}};
        shapes::Append(m, Loft(t, {0, 1, 0}, 5, true, true));
    }
    shapes::Append(m, Loft({{{-0.012f * s, ey + 0.005f * s, zAt(0)}, 0.003f * s, 0.003f * s}, {{0.012f * s, ey + 0.005f * s, zAt(0)}, 0.003f * s, 0.003f * s}}, {0, 1, 0}, 5, true, true));   // the bridge
    return m;
}

// ---- Hands and feet ----------------------------------------------------------------------------------------------
// A hand at the wrist (the origin) pointing down (-y), thumb forward (+z), palm facing the body (inward: -x for the left hand,
// side = +1). `hl` is the hand's length; 0.185 m is the reference.
PolyMesh HandLocal(const std::string& design, float hl, float side) {
    PolyMesh m;
    const float u = hl / 0.185f;
    const float in = -side;   // the palm's direction (toward the body)
    const int sec = std::max(6, N(8, 6));
    if (design == "block") { m = BoxAA({-0.02f * u, -hl, -0.043f * u}, {0.02f * u, 0.0f, 0.043f * u}); shapes::Append(m, Ball({0, 0, 0}, 0.026f * u)); return m; }
    // the palm: thin across the palm, wide at the knuckles
    std::vector<Station> palm = {{{0, -0.002f * u, 0}, 0.016f * u, 0.029f * u}, {{0, -0.034f * u, 0.002f * u}, 0.0165f * u, 0.037f * u}, {{0, -0.072f * u, 0.002f * u}, 0.015f * u, 0.0425f * u}, {{0, -0.100f * u, 0.002f * u}, 0.0125f * u, 0.0425f * u}};
    m = Loft(Resample(palm, std::max(3, N(4, 3))), {1, 0, 0}, sec * 2, false, true, 2.6f);
    shapes::Append(m, Ball({0, 0, 0}, 0.0265f * u));   // the wrist
    const float flen[4] = {0.074f, 0.082f, 0.076f, 0.060f}, frad[4] = {0.0094f, 0.0100f, 0.0092f, 0.0080f}, fz[4] = {0.0325f, 0.0108f, -0.0108f, -0.0325f};
    if (design == "mitten") {
        std::vector<Station> blk = {{{0, -0.095f * u, 0.002f * u}, 0.0125f * u, 0.0415f * u}, {{0, -0.130f * u, 0.002f * u}, 0.0115f * u, 0.039f * u}, {{0, -0.162f * u, 0.002f * u}, 0.0095f * u, 0.030f * u}, {{0, -0.178f * u, 0.002f * u}, 0.006f * u, 0.014f * u}};
        shapes::Append(m, Loft(Resample(blk, std::max(3, N(4, 3))), {1, 0, 0}, sec * 2, false, true, 2.6f));
    } else if (design == "fist") {
        for (int k = 0; k < 4; ++k) {   // each finger folded into the palm: a knuckle and a curled segment
            shapes::Append(m, Elp({in * 0.012f * u, (-0.108f - 0.004f * (float)k) * u, fz[k] * u}, 0.012f * u, 0.015f * u, 0.0105f * u, 3, 7));
            shapes::Append(m, Elp({in * 0.026f * u, (-0.085f - 0.004f * (float)k) * u, fz[k] * u}, 0.011f * u, 0.022f * u, 0.0098f * u, 3, 7));
        }
    } else {
        const int segs = shapes::DetailF() >= 1.5f ? 3 : shapes::DetailF() >= 0.75f ? 2 : 1;
        for (int k = 0; k < 4; ++k) {
            const float L = flen[k] * u, r0 = frad[k] * u;
            const float curl = 0.30f + 0.04f * (float)k;   // fingers curl a little toward the palm, the more so toward the tip
            std::vector<Station> st;
            Vec3 p{in * 0.001f * u, -0.094f * u, fz[k] * u};
            float ang = 0.0f;
            const float seg[3] = {0.46f, 0.30f, 0.24f};
            st.push_back({p, r0, r0 * 1.05f});
            for (int j = 0; j < 3; ++j) {
                ang += curl * (0.5f + (float)j * 0.55f);
                const float l = L * seg[j];
                p = p + Vec3{in * std::sin(ang) * l, -std::cos(ang) * l, 0};
                const float taper = 1.0f - 0.22f * (float)(j + 1) / 3.0f;
                st.push_back({p, r0 * taper * 0.98f, r0 * taper * 1.04f});
            }
            const Vec3 dir = (st.back().c - st[st.size() - 2].c).Normalized();
            st.push_back({st.back().c + dir * (r0 * 0.5f), r0 * 0.62f, r0 * 0.66f});   // a rounded tip
            shapes::Append(m, Loft(segs >= 3 ? Resample(st, 9) : segs == 2 ? Resample(st, 6) : Resample(st, 3), {1, 0, 0}, std::max(6, sec), false, true));
        }
    }
    // the thumb: out from the heel of the palm, forward, two joints
    std::vector<Station> tt = {{{in * 0.012f * u, -0.022f * u, 0.030f * u}, 0.0125f * u, 0.0135f * u}, {{in * 0.016f * u, -0.048f * u, 0.046f * u}, 0.0112f * u, 0.0118f * u},
                                {{in * 0.020f * u, -0.070f * u, 0.052f * u}, 0.0098f * u, 0.0104f * u}, {{in * 0.024f * u, -0.086f * u, 0.0535f * u}, 0.0075f * u, 0.0085f * u}};
    shapes::Append(m, Loft(Resample(tt, std::max(3, N(5, 3))), {1, 0, 0}, std::max(6, sec), false, true));
    return m;
}

PolyMesh FootLocal(const std::string& design, float len, bool shoe) {   // the ankle is at x = z = 0; y is the height above the ground
    const float u = len / 0.26f;
    struct R { float z, cy, ra, rb; };
    const std::vector<R> rows = {{-0.066f, 0.042f, 0.036f, 0.026f}, {-0.040f, 0.052f, 0.050f, 0.036f}, {0.005f, 0.059f, 0.053f, 0.042f}, {0.058f, 0.046f, 0.040f, 0.046f}, {0.108f, 0.030f, 0.030f, 0.050f}, {0.158f, 0.021f, 0.021f, 0.048f}, {0.196f, 0.017f, 0.0165f, 0.040f}, {0.2145f, 0.0145f, 0.011f, 0.026f}};
    const float sk = shoe ? 1.07f : 1.0f;
    std::vector<Station> st;
    for (const R& r : rows) st.push_back({{0, r.cy * u * (shoe ? 1.04f : 1.0f), r.z * u * (shoe ? 1.03f : 1.0f) * 0.9f}, r.ra * u * sk, r.rb * u * sk});
    PolyMesh m = Loft(Resample(st, std::max(5, N(10, 5))), {0, 1, 0}, std::max(8, N(14, 8)), true, true);
    if (design == "round") for (Vec3& v : m.verts) if (v.z > 0.05f * u) v.x *= 1.12f;   // a rounder, wider toe box
    if (design == "sneaker") shapes::Append(m, Elp({0, 0.032f * u, 0.15f * u}, 0.040f * u, 0.022f * u, 0.032f * u, 3, 8));   // a toe cap
    if (shoe) {   // a sole: a flat slab the shape of the foot's outline under the shoe
        std::vector<Station> sl;
        for (const R& r : rows) sl.push_back({{0, 0.010f * u, r.z * u * 1.03f * 0.9f}, 0.010f * u, r.rb * u * sk * 1.03f});
        shapes::Append(m, Loft(Resample(sl, std::max(4, N(8, 4))), {0, 1, 0}, std::max(8, N(14, 8)), true, true));
    } else if (shapes::Detail() >= 1) {   // toes
        for (int k = 0; k < 5; ++k) {
            const float x = (0.034f - 0.0145f * (float)k) * u, z = (0.192f - 0.004f * (float)(k > 2 ? k - 2 : 0) + (k == 0 ? 0.012f : 0.0f) - (k == 4 ? 0.018f : 0.0f)) * u;
            shapes::Append(m, Elp({x, (0.013f + (k == 0 ? 0.004f : 0.0f)) * u, z + 0.008f * u}, (k == 0 ? 0.0105f : 0.0085f) * u, 0.0105f * u, (k == 0 ? 0.017f : 0.012f) * u, 3, 7));
        }
    }
    if (design == "boot") {   // the shaft up the shin
        std::vector<Station> sh = {{{0, 0.06f * u, -0.004f * u}, 0.058f * u, 0.052f * u}, {{0, 0.13f * u, -0.012f * u}, 0.056f * u, 0.052f * u}, {{0, 0.20f * u, -0.016f * u}, 0.056f * u, 0.054f * u}};
        shapes::Append(m, Loft(Resample(sh, 4), kFwd, std::max(8, N(12, 8)), false, false));
    }
    return m;
}

// ---- The generator -----------------------------------------------------------------------------------------------------
} // namespace

void GenerateHumanOrganic(const Recipe& r, std::vector<GeneratedPart>& parts, const std::function<void(const Recipe&, std::vector<GeneratedPart>&)>& boxy) {
    // Accessories (hat, glasses, backpack, cape, shoulder pads) come from the blocky generator: they sit on the same dimensions.
    {
        std::vector<GeneratedPart> all;
        boxy(r, all);
        static const char* kReplaced[] = {"head/", "face/", "neck/", "torso/", "pelvis/", "arm/", "hand/", "leg/", "foot/", "shirt/", "pants/", "belt/", "hair/", "eyes/", "brows/", "nose/", "mouth/", "ears/", "scarf/", "eyewhite/", "hat/", "glasses/"};
        for (GeneratedPart& p : all) {
            bool replaced = false;
            for (const char* pre : kReplaced) if (p.id.rfind(pre, 0) == 0) replaced = true;
            if (!replaced) parts.push_back(std::move(p));
        }
    }
    Out o{r, parts};
    const Dim d = MakeDim(r);
    const HeadShape hd = ShapeOfHead(d);
    auto limbK = [](const std::string& design) { return design == "thick" ? 1.25f : design == "piston" ? 0.78f : 1.0f; };
    const float armK = limbK(r.DesignFor("arm", "arm/0/upper")), legK = limbK(r.DesignFor("leg", "leg/0/upper"));   // clothes follow the limb design

    // torso, pelvis, neck
    AddPart(o, "torso/0", "torso", [&](const std::string&) { return TorsoShell(d, 0.0f, -0.125f, 1.10f); });
    AddPart(o, "pelvis/0", "pelvis", [&](const std::string&) {
        const float hipF = d.bt == 1 ? 0.94f : d.bt == 2 ? 1.1f : 1.0f;
        return Elp({0, d.hipY + 0.0f, -0.006f}, d.W * 0.84f * hipF, 0.04f, d.Dd * 0.9f, 5, 16);
    });
    AddPart(o, "neck/0", "neck", [&](const std::string& dg) { return Neck(d, hd, dg); });
    // head and face
    AddPart(o, "head/0", "head", [&](const std::string&) { return Skull(d, hd); });
    {
        const std::string fd = r.DesignFor("face", "face/0");
        AddPart(o, "face/0", "face", [&](const std::string& dg) {
            PolyMesh m = EyeWhitesMesh(d, hd, dg == "big" ? "big" : "round");
            shapes::Append(m, EyesMesh(d, hd, dg == "big" ? "big" : "round"));
            shapes::Append(m, NoseMesh(d, hd, "straight"));
            shapes::Append(m, MouthMesh(d, hd, "smile"));
            return m;
        });
        const bool combined = fd != "none" && !fd.empty();   // the old combined face replaces the separate parts
        if (!combined) {
            AddPart(o, "eyes/0", "eyes", [&](const std::string& dg) { return EyesMesh(d, hd, dg); });
            AddPart(o, "eyewhite/0", "eyewhite", [&](const std::string&) { return EyeWhitesMesh(d, hd, r.DesignFor("eyes", "eyes/0")); });
            AddPart(o, "brows/0", "brows", [&](const std::string& dg) { return BrowsMesh(d, hd, dg); });
            AddPart(o, "nose/0", "nose", [&](const std::string& dg) { return NoseMesh(d, hd, dg); });
            AddPart(o, "mouth/0", "mouth", [&](const std::string& dg) { return MouthMesh(d, hd, dg); });
        }
        AddPart(o, "ears/0", "ears", [&](const std::string& dg) { return EarsMesh(d, hd, dg); });
        AddPart(o, "hair/0", "hair", [&](const std::string& dg) { return HairMesh(d, hd, dg); });
        AddPart(o, "hat/0", "hat", [&](const std::string& dg) { return HatMesh(d, hd, dg); });
        AddPart(o, "glasses/0", "glasses", [&](const std::string& dg) { return GlassesMesh(d, hd, dg); });
    }
    // arms and hands
    for (int side = 0; side < 2; ++side) {
        const std::string sd = std::to_string(side);
        AddPart(o, "arm/" + sd + "/upper", "arm", [&](const std::string& dg) { Dim e = d; e.limbK = limbK(dg); return UpperArm(e, side, 0.0f, 1.0f, true); });
        AddPart(o, "arm/" + sd + "/fore", "arm", [&](const std::string& dg) { Dim e = d; e.limbK = limbK(dg); return ForeArm(e, side, 0.0f, 1.0f); });
        AddPart(o, "hand/" + sd, "hand", [&](const std::string& dg) {
            PolyMesh m = HandLocal(dg, d.hl, side ? 1.0f : -1.0f);
            m = Rotate(m, {0, 0, 0}, {0, -1, 0}, d.d2[side]);
            Move(m, d.Wr[side]);
            return m;
        });
    }
    // legs and feet
    for (int side = 0; side < 2; ++side) {
        const std::string sd = std::to_string(side);
        AddPart(o, "leg/" + sd + "/upper", "leg", [&](const std::string& dg) { Dim e = d; e.limbK = limbK(dg); return Thigh(e, side, 0.0f, 1.0f); });
        AddPart(o, "leg/" + sd + "/lower", "leg", [&](const std::string& dg) { Dim e = d; e.limbK = limbK(dg); return Calf(e, side, 0.0f, 1.0f); });
        AddPart(o, "foot/" + sd, "foot", [&](const std::string& dg) {
            const float fl = d.H * 0.148f;
            const bool shoe = dg == "boot" || dg == "sneaker" || dg == "round";
            PolyMesh m = FootLocal(dg, fl, shoe);
            const float s = side ? 1.0f : -1.0f, ang = 0.10f * s;   // the toes turn out a few degrees
            for (Vec3& v : m.verts) { const float x = v.x * std::cos(ang) + v.z * std::sin(ang), z = -v.x * std::sin(ang) + v.z * std::cos(ang); v.x = x; v.z = z; }
            shapes::Append(m, Ball({0, d.ankleY, 0}, 0.034f * d.gk * (shoe ? 1.2f : 1.0f)));
            Move(m, {d.A[side].x, 0, d.A[side].z});
            return m;
        });
    }
    // clothes: fitted shells offset from the skin
    const float slack = std::max(0.0f, 1.0f - Mul()) * 0.032f;   // coarse rings sag between their corners: at Low the clothes stand further off the skin
    const float off = 0.016f + slack;
    const float offP = 0.008f + slack * 0.35f;                    // trousers sit under a shirt that hangs over them
    AddPart(o, "shirt/0", "shirt", [&](const std::string& dg) {
        const bool vest = dg == "vest" || dg == "tank";
        const float hem = dg == "jacket" ? -0.11f : dg == "hoodie" ? -0.1f : dg == "longsleeve" ? -0.05f : dg == "tank" ? -0.04f : -0.03f;
        PolyMesh m = TorsoShell(d, off, hem, 1.10f);
        const bool longSleeve = dg == "jacket" || dg == "longsleeve" || dg == "hoodie";
        if (!vest) for (int side = 0; side < 2; ++side) {
            Dim e = d; e.limbK = armK;
            shapes::Append(m, UpperArm(e, side, off + 0.002f, longSleeve ? 1.0f : 0.5f, true));
            if (longSleeve) shapes::Append(m, ForeArm(e, side, off, 0.97f));
        }
        if (dg == "hoodie") shapes::Append(m, Elp({0, d.shoulderY + 0.03f, -d.Dd * 0.55f}, d.W * 0.45f, 0.075f, d.Dd * 0.4f, 4, 12));
        return m;
    });
    AddPart(o, "pants/0", "pants", [&](const std::string& dg) {
        if (dg == "skirt" || dg == "longskirt") {
            const float yb = dg == "skirt" ? d.hipY - 0.42f : std::max(d.ankleY + 0.12f, d.hipY - 0.78f);
            std::vector<Station> st = {{{0, yb, 0}, d.Dd * 1.55f, d.W * 1.55f}, {{0, Lerp(yb, d.hipY, 0.5f), 0}, d.Dd * 1.2f, d.W * 1.32f}, {{0, d.hipY - 0.04f, 0}, d.Dd * 1.02f + off, d.W * 0.98f + off}, {{0, d.hipY + 0.2f * d.tl, 0}, d.Dd * 0.82f + off, d.W * 0.74f + off}};
            if (dg == "longskirt") { st[0].ra *= 0.9f; st[0].rb *= 0.9f; }
            return SmoothLoft(st, kFwd, Sides(d, 18, 8), Rings(d, 10, 4), false, false, 2.2f);
        }
        PolyMesh m = TorsoShell(d, offP, -0.125f, 0.12f);
        const float k = legK * (dg == "jeans" ? 1.1f : dg == "cargo" ? 1.16f : dg == "leggings" ? 0.97f : 1.0f);
        const float legOff = offP + 0.002f + (dg == "jeans" ? 0.006f : dg == "cargo" ? 0.012f : dg == "leggings" ? 0.0f : 0.004f);
        const float tmax = dg == "shorts" ? 0.78f : 1.0f;
        for (int side = 0; side < 2; ++side) {
            Dim e = d; e.limbK = k;
            shapes::Append(m, Thigh(e, side, legOff, tmax));
            if (dg == "trousers" || dg == "greaves" || dg == "jeans" || dg == "cargo" || dg == "leggings") shapes::Append(m, Calf(e, side, legOff - 0.001f, 0.96f));
            if (dg == "cargo") {   // a pocket on the outside of each thigh
                const float s = side ? 1.0f : -1.0f;
                const Vec3 c = d.Hp[side] + d.l1[side] * 0.12f;
                shapes::Append(m, BoxAA({c.x + s * 0.09f - 0.02f, c.y - 0.08f, c.z - 0.06f}, {c.x + s * 0.09f + 0.03f, c.y + 0.07f, c.z + 0.06f}));
            }
        }
        return m;
    });
    AddPart(o, "belt/0", "belt", [&](const std::string&) {
        const float yb = d.hipY + 0.19f * d.tl;
        const std::vector<Station> a = TorsoStations(d, 0.0f, 0.19f - 0.05f, 0.19f + 0.06f);
        std::vector<Station> st;
        for (const Station& s2 : a) st.push_back({s2.c, s2.ra + 0.018f, s2.rb + 0.018f});
        PolyMesh m = Loft(Resample(st, 3), kFwd, Sides(d, 16, 7), false, false, 2.3f);
        const float zf = (st.empty() ? d.Dd : st[st.size() / 2].ra) + 0.003f;
        shapes::Append(m, BoxAA({-0.03f, yb - 0.03f, zf - 0.012f}, {0.03f, yb + 0.035f, zf + 0.012f}));
        return m;
    });
    AddPart(o, "scarf/0", "scarf", [&](const std::string& dg) {
        const float nr = 0.054f * std::max(0.5f, hd.s) * 1.55f;
        PolyMesh m = SmoothLoft({{{0, d.shoulderY - 0.02f, 0}, nr * 1.25f, nr * 1.4f}, {{0, Lerp(d.shoulderY, d.hb, 0.5f), 0}, nr * 1.05f, nr * 1.1f}, {{0, d.hb + 0.005f, -0.006f}, nr, nr}}, kFwd, Sides(d, 14, 6), Rings(d, 5, 3), false, false);
        if (dg == "long") shapes::Append(m, SmoothLoft({{{0.05f, d.shoulderY - 0.03f, nr * 1.1f}, 0.012f, 0.05f}, {{0.055f, d.shoulderY - 0.2f * d.tl, nr * 1.15f + 0.01f}, 0.012f, 0.055f}, {{0.06f, d.shoulderY - 0.45f * d.tl, nr * 1.2f + 0.02f}, 0.012f, 0.05f}}, kFwd, 8, Rings(d, 6, 3), false, true));
        return m;
    });
    // Fine detail (High and Ultra) on the clothes: a row of buttons down the front, a placket, the hem, and two pockets. A part of its own, removable.
    if (shapes::DetailF() >= 1.99f && r.DesignFor("shirt", "shirt/0") != "none" && !r.DesignFor("shirt", "shirt/0").empty()) {
        const std::string sh = r.DesignFor("shirt", "shirt/0");
        const float yb = d.hipY + 0.16f * d.tl, yt = d.shoulderY - 0.1f;
        auto zAt = [&](float y) { return TorsoHalfDepthAt(d, y) + 0.012f; };
        AddPart(o, "button/0", "fitting", [&](const std::string& dg) {
            if (dg == "none") return PolyMesh();
            PolyMesh m;
            const int n = sh == "vest" || sh == "jacket" || sh == "hoodie" ? 5 : 3;
            for (int k = 0; k < n; ++k) {
                const float y = yb + (yt - yb) * (float)k / (float)std::max(1, n - 1);
                Frame f; f.origin = {0, y, zAt(y) - 0.006f}; f.t = {1, 0, 0}; f.u = {0, 0, 1}; f.n = {0, -1, 0}; f.sx = 0.022f; f.sy = 0.016f; f.sz = 0.022f;
                shapes::Append(m, shapes::Prism(f, 8));
            }
            if (sh != "tshirt" && sh != "tank") shapes::Append(m, BoxAA({-0.004f, yb - 0.02f, zAt(yb) - 0.012f}, {0.004f, yt + 0.02f, zAt(yt) - 0.002f}));   // the placket
            return m;
        });
        if (sh == "jacket" || sh == "hoodie")
            AddPart(o, "pocket/0", "fitting", [&](const std::string& dg) {
                if (dg == "none") return PolyMesh();
                PolyMesh m;
                const float yp = d.hipY - 0.02f, zp = zAt(yp);
                for (int sg = -1; sg <= 1; sg += 2) shapes::Append(m, BoxAA({sg * d.W * 0.5f - 0.045f, yp - 0.035f, zp - 0.012f}, {sg * d.W * 0.5f + 0.045f, yp + 0.04f, zp + 0.002f}));   // two pocket flaps
                return m;
            });
    }
}

// ---- The skeleton -----------------------------------------------------------------------------------------------------------
// Twenty-two bones: the fifteen humanoid bones plus the spine chain, neck, collar bones and toes. The names are Mixamo's
// ("mixamorig:LeftUpLeg") by default, so a clip that targets those node names plays on a Guided human; RigNaming::Engine uses the
// engine's mannequin names (pelvis, spine_01, upperarm_l, thigh_l, calf_l ...). Both map through Animation/HumanoidBones.hpp.
bool HumanoidSkeleton(const Recipe& r, std::vector<RigJoint>& out, RigNaming naming) {
    out.clear();
    if (r.templateId != "human") return false;
    const Dim d = MakeDim(r);
    const bool mx = naming == RigNaming::Mixamo;
    auto nm = [&](const char* mixamo, const char* engine) { return std::string(mx ? "mixamorig:" : "") + (mx ? mixamo : engine); };
    auto add = [&](const std::string& name, int parent, Vec3 pos, Vec3 tail, float radius, int group = 0) { RigJoint j; j.name = name; j.parent = parent; j.pos = pos; j.tail = tail; j.radius = radius; j.group = group; out.push_back(j); return (int)out.size() - 1; };
    const float g = d.gk;
    const Vec3 hips{0, d.hipY, 0};
    const Vec3 sp0{0, d.hipY + 0.22f * d.tl, 0}, sp1{0, d.hipY + 0.48f * d.tl, 0}, sp2{0, d.hipY + 0.74f * d.tl, 0}, neck{0, d.shoulderY, 0}, headJ{0, d.hb + 0.045f * d.hs / 0.233f, 0};
    const int iHips = add(nm("Hips", "pelvis"), -1, hips, sp0, d.W);
    const int iSp0 = add(nm("Spine", "spine_01"), iHips, sp0, sp1, d.W * 0.8f);
    const int iSp1 = add(nm("Spine1", "spine_02"), iSp0, sp1, sp2, d.W * 0.85f);
    const int iSp2 = add(nm("Spine2", "spine_03"), iSp1, sp2, neck, d.W * 0.85f);
    const int iNeck = add(nm("Neck", "neck_01"), iSp2, neck, headJ, 0.055f);
    add(nm("Head", "head"), iNeck, headJ, {0, d.hb + d.hs, 0}, d.hs * 0.38f);
    for (int side = 1; side >= 0; --side) {   // the engine's left is +x (the figure faces +z)
        const float s = side ? 1.0f : -1.0f;
        const char* lr = side ? "Left" : "Right";
        const char* e = side ? "_l" : "_r";
        auto nn = [&](const char* mixamoBase, const char* engineBase) { return mx ? std::string("mixamorig:") + lr + mixamoBase : std::string(engineBase) + e; };
        const int cl = add(nn("Shoulder", "clavicle"), iSp2, {s * 0.02f, d.shoulderY - 0.02f, 0}, d.S[side], 0.05f, 1);
        const int ua = add(nn("Arm", "upperarm"), cl, d.S[side], d.E[side], 0.052f * g, 1);
        const int la = add(nn("ForeArm", "lowerarm"), ua, d.E[side], d.Wr[side], 0.038f * g, 1);
        add(nn("Hand", "hand"), la, d.Wr[side], d.Wr[side] + d.d2[side] * d.hl, 0.04f, 1);
        const int th = add(nn("UpLeg", "thigh"), iHips, d.Hp[side], d.K[side], 0.09f * g, 2);
        const int ca = add(nn("Leg", "calf"), th, d.K[side], d.A[side], 0.055f * g, 2);
        const float u = d.H * 0.148f / 0.26f;
        const Vec3 ball = d.A[side] + Vec3{0, -d.ankleY + 0.03f, 0.11f * u};
        const int ft = add(nn("Foot", "foot"), ca, d.A[side], ball, 0.05f, 2);
        add(nn("ToeBase", "ball"), ft, ball, d.A[side] + Vec3{0, -d.ankleY + 0.02f, 0.20f * u}, 0.03f, 2);
    }
    return true;
}

unsigned char RigGroupsOfSlot(const std::string& slot) {
    if (slot == "arm" || slot == "hand") return 2;
    if (slot == "leg" || slot == "foot" || slot == "pants") return 1 | 4;
    if (slot == "shirt") return 1 | 2;
    return 1;
}

void SkinVertices(const std::vector<Vec3>& verts, const std::vector<RigJoint>& joints,
                  std::vector<std::array<int, 4>>& idx, std::vector<std::array<float, 4>>& wt, const std::vector<unsigned char>* groups) {
    idx.assign(verts.size(), {0, 0, 0, 0});
    wt.assign(verts.size(), {0, 0, 0, 0});
    const int n = (int)joints.size();
    std::vector<float> dist((size_t)n);
    float neckY = 1e9f;
    for (const RigJoint& j : joints) if (j.group == 0 && j.parent >= 0 && j.pos.y > 0.5f && joints[(size_t)j.parent].group == 0 && j.name.find("eck") != std::string::npos) neckY = j.pos.y;
    for (size_t v = 0; v < verts.size(); ++v) {
        const Vec3 p = verts[v];
        for (int j = 0; j < n; ++j) {
            const Vec3 a = joints[j].pos, ab = joints[j].tail - a;
            const float l2 = Vec3::Dot(ab, ab);
            float t = l2 > 1e-9f ? Vec3::Dot(p - a, ab) / l2 : 0.0f;
            t = std::min(1.0f, std::max(0.0f, t));
            dist[j] = (p - (a + ab * t)).Magnitude() / (joints[j].radius + 0.015f);   // in units of the body's thickness around the bone
        }
        const unsigned mask = groups && v < groups->size() && (*groups)[v] ? (unsigned)(*groups)[v] : 7u;
        for (int j = 0; j < n; ++j) if (!(mask & (1u << joints[j].group))) dist[j] += 100.0f;   // a bone of a group this part may not follow
        // An arm hanging at the side must not move the chest: inside the trunk's outline the arm bones count as far away, smoothly
        // (no hard edge), so a vertex there takes its weights from the spine instead.
        float armK = 1.0f;
        if ((mask & 2u) && (mask & 1u) && p.y > joints[0].pos.y && p.y < neckY - 0.03f) armK = Sm((std::fabs(p.x) - joints[0].radius * 0.9f) / (joints[0].radius * 0.35f));
        for (int j = 0; j < n; ++j) if (joints[j].group == 1) dist[j] += (1.0f - armK) * 3.0f;
        int best[4] = {-1, -1, -1, -1};
        for (int k = 0; k < 4; ++k) {
            float bd = 1e30f;
            for (int j = 0; j < n; ++j) {
                if (j == best[0] || j == best[1] || j == best[2]) continue;
                if (dist[j] < bd) { bd = dist[j]; best[k] = j; }
            }
        }
        // Weights fall off with the distance to each bone's segment in units of the body's thickness there (squared, softened so a
        // vertex on a joint takes both bones about equally); a bone far behind the nearest one is dropped. A smooth falloff keeps
        // neighbouring vertices on nearly the same blend, so no triangle is pulled apart by two bones that disagree.
        float sum = 0;
        for (int k = 0; k < 4; ++k) {
            if (best[k] < 0) continue;
            const float w = 1.0f / std::pow(dist[best[k]] + 0.5f, 3.0f);
            idx[v][k] = best[k]; wt[v][k] = w; sum += w;
        }
        const float top = wt[v][0];
        sum = 0;
        for (int k = 0; k < 4; ++k) { wt[v][k] = std::max(0.0f, wt[v][k] - top * 0.12f); sum += wt[v][k]; }   // a soft cut: a weak bone fades out, it does not switch off
        for (int k = 0; k < 4; ++k) wt[v][k] = sum > 0 ? wt[v][k] / sum : (k == 0 ? 1.0f : 0.0f);
    }
}

} // namespace okay::modeling::guided
