// The fence template for guided modeling. Written from general parametric modeling: posts at even spacing along a path,
// rails and infill per bay, designs that fill a box. Every design is made by this file's own code.
#include "okay/Modeling/Guided.hpp"
#include <algorithm>
#include <cmath>

namespace okay::modeling::guided {
namespace {

constexpr float kPi = 3.14159265f;
const Vec3 kUp{0, 1, 0};

float Pf(const Recipe& r, const char* id) { return (float)r.Param(id); }

// One straight run of the fence between two posts.
struct Bay {
    Vec3 start, end;      // ground points at the post centres
    Vec3 h;               // horizontal unit direction
    Vec3 n;               // front normal (h x up)
    float lh = 0;         // horizontal length
    float rise = 0;       // slope: height gained per metre of horizontal run
    bool gate = false;
    float GroundY(float x) const { return start.y + rise * x; }
};

// A box in the bay's own sheared frame: x is the horizontal distance along the bay, y the height above the bay's
// ground line, so a rail or a gate on a slope follows the ground while posts and pickets stay upright.
Frame BayFrame(const Bay& b, float x0, float y0, float w, float hh, float thick, float nOff) {
    Frame f;
    const float xc = x0 + w * 0.5f;
    f.origin = b.start + b.h * xc + kUp * (b.rise * xc + y0) + b.n * nOff;
    f.t = b.h + kUp * b.rise;
    f.u = kUp;
    f.n = b.n;
    f.sx = w; f.sy = std::max(hh, 0.005f); f.sz = std::max(thick, 0.002f);
    return f;
}

PolyMesh Fill(const Recipe& r, const std::string& slot, const std::string& partId, const Frame& f, const std::string& design,
              const std::function<PolyMesh(const std::string&)>& builtin) {
    if (design.rfind("user:", 0) == 0) {
        auto it = r.userParts.find(design.substr(5));
        if (it != r.userParts.end()) return FitUserPart(it->second, f);
        return shapes::Box(f);   // the part is missing from the file: a plain box, never nothing
    }
    (void)slot; (void)partId;
    return builtin(design);
}

// ---- Designs ------------------------------------------------------------------------------------------------

PolyMesh PostDesign(const std::string& d, const Frame& f) {
    if (d == "round") return shapes::Prism(f, 12);
    if (d == "octagon") return shapes::Prism(f, 8);
    return shapes::Box(f);
}

PolyMesh CapDesign(const std::string& d, const Frame& f) {
    if (d == "none") return PolyMesh();
    if (d == "pyramid") return shapes::Pyramid(f, 4);
    if (d == "ball") return shapes::Ball(f, 4, 8);
    Frame s = f; s.sy = f.sy * 0.35f;   // flat slab
    return shapes::Box(s);
}

// A rail along the bay. `round` rails are prisms whose axis runs along the bay.
PolyMesh RailDesign(const std::string& d, const Bay& b, float level, float railH, float railD, float e) {
    // Rails stop inside the posts (e from each post centre) so the rail of one bay never touches the next bay's rail.
    if (d == "round") {
        Frame f;
        const Vec3 along = (b.h + kUp * b.rise);
        const Vec3 alongN = along.Normalized();
        const float len = (b.lh - 2 * e) * along.Magnitude();
        f.origin = b.start + b.h * e + kUp * (b.rise * e + level);
        f.t = kUp; f.u = alongN; f.n = Vec3::Cross(kUp, alongN);   // (t, u, n) right-handed: up x along
        f.sx = railH; f.sy = len; f.sz = railH;
        if (Vec3::Dot(Vec3::Cross(f.t, f.u), f.n) < 0) f.n = -f.n;
        return shapes::Prism(f, 8);
    }
    return shapes::Box(BayFrame(b, e, level - railH * 0.5f, b.lh - 2 * e, railH, railD, 0));
}

std::vector<Vec2> PicketProfile(const std::string& d, float w, float h) {
    const float hd = std::max(h, 0.01f);
    if (d == "pointed") {
        const float ph = std::min(0.5f * w, 0.4f * hd) / hd;
        return {{0, 0}, {1, 0}, {1, 1 - ph}, {0.5f, 1}, {0, 1 - ph}};
    }
    if (d == "rounded") {
        const float ph = std::min(0.5f * w, 0.4f * hd) / hd;
        std::vector<Vec2> p = {{0, 0}, {1, 0}};
        const int steps = shapes::Scale(6, 2);
        for (int k = 0; k <= steps; ++k) {
            const float a = kPi * (float)k / (float)steps;
            p.push_back({0.5f + 0.5f * std::cos(a), (1 - ph) + ph * std::sin(a)});
        }
        return p;
    }
    if (d == "dogear") {
        const float cx = 0.5f;
        const float cy = std::min(0.5f * w, 0.3f * hd) / hd;
        return {{0, 0}, {1, 0}, {1, 1 - cy}, {1 - cx * 0.0f - 0.25f, 1}, {0.25f, 1}, {0, 1 - cy}};
    }
    if (d == "spear") {
        const float s = std::min(0.9f * w, 0.2f * hd) / hd;   // spear head height
        return {{0.35f, 0}, {0.65f, 0}, {0.65f, 1 - s}, {1, 1 - s}, {0.5f, 1}, {0, 1 - s}, {0.35f, 1 - s}};
    }
    return {{0, 0}, {1, 0}, {1, 1}, {0, 1}};   // flat
}

// Chain link: two families of thin diagonal wires over the panel.
PolyMesh ChainLink(const Bay& b, float x0, float y0, float W, float H, float wire, float cell, float thick, float nOff) {
    PolyMesh m;
    cell = std::max(cell, 0.03f);
    wire = std::max(wire, 0.004f);
    const Vec3 along = b.h + kUp * b.rise;
    const float k = along.Magnitude();
    const Vec3 T = along / k;
    const Vec3 U = Vec3::Cross(b.n, T);
    const float Wt = W * k, Ht = H / k;   // the panel in true in-plane units (an upright panel on a slope is a parallelogram; this keeps wires inside it)
    const Vec3 base = b.start + b.h * x0 + kUp * (b.rise * x0 + y0) + b.n * nOff;
    const float s2 = std::sqrt(2.0f);
    for (int fam = 0; fam < 2; ++fam) {
        const float off = fam ? thick * 0.9f : 0.0f;
        const float lo = fam ? 0.0f : -Wt, hi = fam ? Wt + Ht : Ht;
        for (float c = lo + cell * 0.5f; c < hi; c += cell) {
            float xa, xb;
            if (fam == 0) { xa = std::max(0.0f, -c); xb = std::min(Wt, Ht - c); }
            else          { xa = std::max(0.0f, c - Ht); xb = std::min(Wt, c); }
            if (xb - xa < 0.02f) continue;
            const float xm = 0.5f * (xa + xb);
            const float ym = fam == 0 ? xm + c : c - xm;
            const float len = (xb - xa) * s2;
            Frame f;
            const float ang = fam == 0 ? kPi / 4 : -kPi / 4;
            f.t = T * std::cos(ang) + U * std::sin(ang);
            f.u = Vec3::Cross(b.n, f.t);
            f.n = b.n;
            f.origin = base + T * xm + U * ym + b.n * off - f.u * (wire * 0.5f);
            f.sx = len; f.sy = wire; f.sz = thick;
            shapes::Append(m, shapes::Box(f));
        }
    }
    return m;
}

// ---- The generator -------------------------------------------------------------------------------------------

void Fence(const Recipe& r, std::vector<GeneratedPart>& out) {
    const float height = Pf(r, "height"), postSize = Pf(r, "postSize"), spacing = Pf(r, "postSpacing");
    const int railCount = (int)r.Param("railCount");
    const float railH = Pf(r, "railHeight"), railD = Pf(r, "railDepth");
    const float infW = Pf(r, "infillWidth"), infG = Pf(r, "infillGap"), infT = Pf(r, "infillThick");
    const float infBottom = Pf(r, "infillBottom"), infDrop = Pf(r, "infillDrop"), infAlt = Pf(r, "infillAlt");
    const float embed = 0.2f;

    // --- the path -------------------------------------------------------------------------------------------
    std::vector<Vec3> path = r.path;
    if (path.size() < 2) {
        const float len = Pf(r, "length");
        path = {{0, 0, 0}, {len, std::tan(Pf(r, "slope") * kPi / 180.0f) * len, 0}};
    }
    std::vector<float> cum(1, 0.0f);
    std::vector<int> segOf;   // not needed beyond cum
    for (size_t i = 1; i < path.size(); ++i) {
        const Vec3 d = path[i] - path[i - 1];
        cum.push_back(cum.back() + std::sqrt(d.x * d.x + d.z * d.z));
    }
    const float total = cum.back();
    if (total < 0.05f) return;
    auto segAt = [&](float s) {
        int i = 0;
        while (i + 2 < (int)path.size() && s > cum[i + 1] + 1e-5f) ++i;
        return i;
    };
    auto sample = [&](float s, int seg) {
        const float l = cum[seg + 1] - cum[seg];
        const float t = l > 1e-6f ? (s - cum[seg]) / l : 0.0f;
        return path[seg] + (path[seg + 1] - path[seg]) * std::min(std::max(t, 0.0f), 1.0f);
    };

    // --- post positions (distance along the path) ------------------------------------------------------------
    std::vector<float> posts;
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        const float l = cum[i + 1] - cum[i];
        if (l < 1e-4f) continue;
        const int bays = std::max(1, (int)std::ceil(l / spacing - 1e-4f));
        for (int k = (i == 0 ? 0 : 1); k <= bays; ++k) posts.push_back(cum[i] + l * (float)k / (float)bays);
    }
    float ga = 0, gb = 0;
    const bool gate = r.Param("gate") >= 0.5;
    if (gate) {
        const int gs = segAt(std::min(std::max((float)r.Param("gatePos"), 0.0f) * total, total));
        const float lo = cum[gs], hi = cum[gs + 1];
        const float gw = std::min((float)r.Param("gateWidth"), (hi - lo) * 0.9f);
        float c = (float)r.Param("gatePos") * total;
        c = std::min(std::max(c, lo + gw * 0.5f), hi - gw * 0.5f);
        ga = c - gw * 0.5f; gb = c + gw * 0.5f;
        std::vector<float> keep;
        for (float p : posts) if (p <= ga + 1e-3f || p >= gb - 1e-3f) keep.push_back(p);
        keep.push_back(ga); keep.push_back(gb);
        std::sort(keep.begin(), keep.end());
        posts.clear();
        for (float p : keep) if (posts.empty() || p - posts.back() > 1e-3f) posts.push_back(p);
    }

    // --- posts and caps ----------------------------------------------------------------------------------------
    std::vector<Vec3> postPos;
    for (size_t i = 0; i < posts.size(); ++i) {
        const int seg = segAt(std::min(posts[i], total));
        const Vec3 p = sample(posts[i], seg);
        postPos.push_back(p);
        const Vec3 d = path[seg + 1] - path[seg];
        Vec3 h{d.x, 0, d.z};
        h = h.Normalized();
        if (h.SqrMagnitude() < 0.5f) h = {1, 0, 0};
        Frame f;
        f.t = h; f.u = kUp; f.n = Vec3::Cross(h, kUp);
        f.origin = p - kUp * embed;
        f.sx = postSize; f.sy = height + embed; f.sz = postSize;
        const std::string id = "post/" + std::to_string(i);
        out.push_back({id, "post", Fill(r, "post", id, f, r.DesignFor("post", id), [&](const std::string& d2) { return PostDesign(d2, f); })});
        Frame c = f;
        c.origin = p + kUp * height;
        c.sx = c.sz = postSize * 1.4f; c.sy = postSize * 0.9f;
        const std::string cid = "cap/" + std::to_string(i);
        out.push_back({cid, "cap", Fill(r, "cap", cid, c, r.DesignFor("cap", cid), [&](const std::string& d2) { return CapDesign(d2, c); })});
    }

    // --- bays ------------------------------------------------------------------------------------------------
    for (size_t bi = 0; bi + 1 < posts.size(); ++bi) {
        Bay b;
        const int seg = segAt(std::min(0.5f * (posts[bi] + posts[bi + 1]), total));
        b.start = postPos[bi]; b.end = postPos[bi + 1];
        const Vec3 d = b.end - b.start;
        b.lh = std::sqrt(d.x * d.x + d.z * d.z);
        if (b.lh < 0.02f) continue;
        b.h = Vec3{d.x, 0, d.z} / b.lh;
        b.n = Vec3::Cross(b.h, kUp);
        b.rise = d.y / b.lh;
        (void)seg;
        b.gate = gate && std::fabs(posts[bi] - ga) < 2e-3f && std::fabs(posts[bi + 1] - gb) < 2e-3f;
        const std::string bs = std::to_string(bi);

        if (b.gate) {
            const std::string id = "gate/leaf";
            const float x0 = postSize * 0.5f + 0.01f, W = b.lh - postSize - 0.02f;
            const float y0 = infBottom, H = std::max(0.2f, height - 0.05f - infBottom);
            const Frame whole = BayFrame(b, x0, y0, W, H, 0.04f, 0);
            auto built = [&](const std::string& design) {
                PolyMesh m;
                if (W < 0.1f) return m;
                const float st = 0.07f, rh = 0.08f, th = 0.04f;
                shapes::Append(m, shapes::Box(BayFrame(b, x0, y0, st, H, th, 0)));                    // hinge stile
                shapes::Append(m, shapes::Box(BayFrame(b, x0 + W - st, y0, st, H, th, 0)));           // latch stile
                shapes::Append(m, shapes::Box(BayFrame(b, x0 + st, y0, W - 2 * st, rh, th, 0)));       // bottom rail
                shapes::Append(m, shapes::Box(BayFrame(b, x0 + st, y0 + H - rh, W - 2 * st, rh, th, 0)));   // top rail
                const float iw = W - 2 * st, ih = H - 2 * rh;
                if (iw < 0.05f || ih < 0.05f) return m;
                if (design == "planks") {
                    const int n = std::max(2, (int)std::floor(iw / 0.1f));
                    const float pw = iw / (float)n - 0.004f;
                    for (int k = 0; k < n; ++k)
                        shapes::Append(m, shapes::Box(BayFrame(b, x0 + st + (iw / n) * k + 0.002f, y0 + rh, pw, ih, 0.025f, 0.03f)));
                } else if (design == "bars") {
                    const int n = std::max(2, (int)std::floor(iw / 0.12f));
                    for (int k = 0; k < n; ++k) {
                        Frame f = BayFrame(b, x0 + st + (iw / n) * (k + 0.5f) - 0.012f, y0 + rh, 0.024f, ih, 0.024f, 0);
                        shapes::Append(m, shapes::Prism(f, 8));
                    }
                } else if (design == "braced") {
                    // a diagonal brace across the opening
                    const Vec3 along = b.h + kUp * b.rise;
                    const float k2 = along.Magnitude();
                    const Vec3 T = along / k2, U = Vec3::Cross(b.n, T);
                    const float dx = iw * k2, dy = ih / k2;
                    const float len = std::sqrt(dx * dx + dy * dy), ang = std::atan2(dy, dx);
                    Frame f;
                    f.t = T * std::cos(ang) + U * std::sin(ang);
                    f.u = Vec3::Cross(b.n, f.t);
                    f.n = b.n;
                    const Vec3 c0 = b.start + b.h * (x0 + st + iw * 0.5f) + kUp * (b.rise * (x0 + st + iw * 0.5f) + y0 + rh + ih * 0.5f);
                    f.origin = c0 - f.t * (len * 0.5f) - f.u * 0.03f;
                    f.sx = len; f.sy = 0.06f; f.sz = 0.03f;
                    shapes::Append(m, shapes::Box(f));
                } else {   // open: the frame and one middle rail
                    shapes::Append(m, shapes::Box(BayFrame(b, x0 + st, y0 + H * 0.5f - rh * 0.5f, iw, rh, th, 0)));
                }
                return m;
            };
            out.push_back({id, "gate", Fill(r, "gate", id, whole, r.DesignFor("gate", id), built)});
            // rails run over the gate opening nowhere; nothing else in this bay
            continue;
        }

        // rails
        if (railCount > 0 && b.lh - postSize * 0.5f > 0.02f) {
            // Rails keep inside the fence height and never overlap: when the asked-for gaps leave no room, they
            // are spread over the whole height instead; when even that is too tight there are fewer of them.
            const int nmax = std::max(1, (int)std::floor((height - railH) / (railH * 1.5f)) + 1);
            const int cnt = std::min(railCount, nmax);
            float lo = std::min(std::max(Pf(r, "railBottom") + railH * 0.5f, railH * 0.5f), height - railH * 0.5f);
            float hi = std::min(std::max(lo, height - Pf(r, "railTop") - railH * 0.5f), height - railH * 0.5f);
            if (cnt > 1 && hi - lo < (float)(cnt - 1) * railH * 1.3f) { lo = railH * 0.5f; hi = height - railH * 0.5f; }
            for (int j = 0; j < cnt; ++j) {
                const float level = cnt == 1 ? 0.5f * (lo + hi) : lo + (hi - lo) * (float)j / (float)(cnt - 1);
                const std::string id = "rail/" + bs + "/" + std::to_string(j);
                const float e = postSize * 0.25f;
                const Frame f = BayFrame(b, e, level - railH * 0.5f, b.lh - 2 * e, railH, railD, 0);
                out.push_back({id, "rail", Fill(r, "rail", id, f, r.DesignFor("rail", id), [&](const std::string& d2) { return RailDesign(d2, b, level, railH, railD, e); })});
            }
        }

        // infill
        const float span = b.lh - postSize;
        if (span < 0.03f) continue;
        const float nOff = railCount > 0 ? railD * 0.5f + infT * 0.5f - 0.002f : 0.0f;
        const float x0 = postSize * 0.5f;
        const std::string infDesign = r.DesignFor("infill", "infill/" + bs);
        // chain link is one part per bay; the rest are one part per picket
        auto isChain = [&](const std::string& d2) { return d2 == "chainlink"; };
        if (isChain(infDesign)) {
            const float y0 = infBottom, H = std::max(0.1f, height - infBottom - infDrop);
            const std::string id = "infill/" + bs;
            const Frame f = BayFrame(b, x0, y0, span, H, infT, 0);
            out.push_back({id, "infill", Fill(r, "infill", id, f, r.DesignFor("infill", id), [&](const std::string& d2) {
                if (d2 == "chainlink") return ChainLink(b, x0, y0, span, H, infW, infW + infG, infT, 0);
                return PolyMesh();
            })});
            continue;
        }
        int n = std::max(1, (int)std::floor((span + infG) / (infW + infG) + 1e-5f));
        const float w = std::min(infW, span);
        const float step = n > 1 ? (span - w) / (float)(n - 1) : 0.0f;
        for (int k = 0; k < n; ++k) {
            const std::string id = "infill/" + bs + "/" + std::to_string(k);
            const std::string design = r.DesignFor("infill", id);
            if (design == "none") continue;
            const float cx = n > 1 ? x0 + w * 0.5f + step * (float)k : x0 + span * 0.5f;
            const float drop = infDrop + ((k & 1) ? infAlt : 0.0f);
            const float hh = std::max(0.05f, height - infBottom - drop);
            Frame f = BayFrame(b, cx - w * 0.5f, infBottom, w, hh, infT, nOff);
            f.t = b.h; f.sx = w;   // pickets are upright: no shear
            f.origin = b.start + b.h * cx + kUp * (b.GroundY(cx) + infBottom) + b.n * nOff;
            out.push_back({id, "infill", Fill(r, "infill", id, f, design, [&](const std::string& d2) {
                if (d2 == "bars" || d2 == "bars_spear") {
                    Frame bf = f; bf.sz = w;
                    PolyMesh m = shapes::Prism(bf, 8);
                    if (d2 == "bars_spear") {
                        Frame hf = bf;
                        hf.origin = bf.origin + kUp * bf.sy;
                        hf.sx = hf.sz = w * 2.2f; hf.sy = w * 4.0f;
                        shapes::Append(m, shapes::Pyramid(hf, 4));
                    }
                    return m;
                }
                return shapes::ExtrudeProfile(f, PicketProfile(d2, w, hh));
            })});
        }
    }
}

Template MakeFence() {
    Template t;
    t.usesPath = true;
    t.id = "fence"; t.name = "Fence"; t.category = "Architecture"; t.version = 1;
    t.description = "A fence along a straight run or a drawn path: posts, rails, pickets or bars, an optional gate.";
    using K = ParamDef::Kind;
    auto P = [&](const char* id, const char* label, K k, double def, double mn, double mx, const char* group, const char* tip) {
        ParamDef p; p.id = id; p.label = label; p.kind = k; p.def = def; p.min = mn; p.max = mx; p.group = group; p.tip = tip;
        t.params.push_back(p);
    };
    P("length", "Length", K::Float, 6, 0.5, 200, "Size", "How long the fence is (a straight run; ignored when you draw a path).");
    P("height", "Height", K::Float, 1.2, 0.3, 4, "Size", "Height of the posts above the ground.");
    P("slope", "Ground slope (degrees)", K::Float, 0, -35, 35, "Size", "Tilt the ground the fence follows: rails and gates follow it, posts stay upright.");
    P("postSpacing", "Post spacing", K::Float, 2, 0.5, 8, "Posts", "Distance between posts (the fence rounds it so the posts are even).");
    P("postSize", "Post size", K::Float, 0.12, 0.03, 0.6, "Posts", "Width of a post.");
    P("railCount", "Rails", K::Int, 2, 0, 6, "Rails", "How many rails run along each bay.");
    P("railHeight", "Rail height", K::Float, 0.08, 0.02, 0.3, "Rails", "How tall a rail is.");
    P("railDepth", "Rail thickness", K::Float, 0.04, 0.01, 0.2, "Rails", "How thick a rail is.");
    P("railBottom", "Lowest rail above ground", K::Float, 0.15, 0, 2, "Rails", "Gap under the lowest rail.");
    P("railTop", "Top rail below the top", K::Float, 0.12, 0, 1, "Rails", "Gap above the highest rail.");
    P("infillWidth", "Picket / bar width", K::Float, 0.09, 0.01, 0.5, "Infill", "Width of one picket or bar (the wire thickness for chain link).");
    P("infillGap", "Gap between pickets", K::Float, 0.05, 0.002, 0.5, "Infill", "Space between pickets (the mesh size for chain link).");
    P("infillThick", "Picket thickness", K::Float, 0.02, 0.005, 0.1, "Infill", "How thick a picket is.");
    P("infillBottom", "Picket gap above ground", K::Float, 0.05, 0, 1, "Infill", "Clearance under the pickets.");
    P("infillDrop", "Pickets below the top", K::Float, 0, 0, 1, "Infill", "How far the pickets stop below the fence height.");
    P("infillAlt", "Every other picket lower by", K::Float, 0, 0, 0.5, "Infill", "Makes a scalloped top.");
    P("gate", "Gate", K::Bool, 0, 0, 1, "Gate", "Put a gate in the fence.");
    P("gatePos", "Gate position", K::Float, 0.5, 0, 1, "Gate", "Where along the fence the gate is (0 = start, 1 = end).");
    P("gateWidth", "Gate width", K::Float, 1, 0.5, 4, "Gate", "Width of the opening.");

    auto S = [&](const char* id, const char* name, std::vector<DesignDef> d, Color c, int mat) {
        SlotDef s; s.id = id; s.name = name; s.designs = std::move(d); s.defaultColor = c; s.defaultMaterial = mat;
        t.slots.push_back(s);
    };
    S("post", "Posts", {{"square", "Square"}, {"round", "Round"}, {"octagon", "Octagon"}}, {0.55f, 0.38f, 0.22f, 1}, 0);
    S("cap", "Post caps", {{"none", "None"}, {"flat", "Flat"}, {"pyramid", "Pyramid"}, {"ball", "Ball"}}, {0.55f, 0.38f, 0.22f, 1}, 1);
    S("rail", "Rails", {{"board", "Board"}, {"round", "Round"}}, {0.55f, 0.38f, 0.22f, 1}, 2);
    S("infill", "Pickets and bars", {{"none", "None"}, {"flat", "Flat top"}, {"pointed", "Pointed"}, {"rounded", "Rounded"},
                                      {"dogear", "Dog ear"}, {"spear", "Spear (flat iron)"}, {"bars", "Round bars"},
                                      {"bars_spear", "Round bars with spear"}, {"chainlink", "Chain link"}}, {0.55f, 0.38f, 0.22f, 1}, 3);
    S("gate", "Gate", {{"planks", "Planks"}, {"bars", "Bars"}, {"braced", "Braced"}, {"open", "Open frame"}}, {0.55f, 0.38f, 0.22f, 1}, 4);

    const Color white{0.95f, 0.95f, 0.93f, 1}, wood{0.55f, 0.38f, 0.22f, 1}, dark{0.32f, 0.2f, 0.12f, 1},
                iron{0.12f, 0.12f, 0.14f, 1}, zinc{0.62f, 0.65f, 0.68f, 1}, green{0.2f, 0.45f, 0.28f, 1};
    auto St = [&](const char* id, const char* name, const char* desc, std::map<std::string, double> p,
                  std::map<std::string, std::string> sd, Color c) {
        StartDesign s; s.id = id; s.name = name; s.description = desc; s.params = std::move(p); s.slotDesign = std::move(sd);
        for (const char* sl : {"post", "cap", "rail", "infill", "gate"}) s.colors[sl] = c;
        t.starts.push_back(s);
    };
    St("picket", "Picket fence", "The classic white picket fence: pointed pickets on two rails.",
       {{"height", 1.1}, {"postSpacing", 2}, {"postSize", 0.1}, {"railCount", 2}, {"railHeight", 0.08}, {"railDepth", 0.04}, {"railBottom", 0.2},
        {"railTop", 0.2}, {"infillWidth", 0.09}, {"infillGap", 0.05}, {"infillThick", 0.02}, {"infillBottom", 0.05}},
       {{"post", "square"}, {"cap", "pyramid"}, {"rail", "board"}, {"infill", "pointed"}, {"gate", "planks"}}, white);
    St("post_rail", "Post and rail", "Round posts with three round rails, open between.",
       {{"height", 1.1}, {"postSpacing", 2.5}, {"postSize", 0.14}, {"railCount", 3}, {"railHeight", 0.07}, {"railDepth", 0.07}, {"railBottom", 0.25}, {"railTop", 0.1}},
       {{"post", "round"}, {"cap", "flat"}, {"rail", "round"}, {"infill", "none"}, {"gate", "open"}}, wood);
    St("ranch", "Ranch fence", "Heavy square posts and four wide boards.",
       {{"height", 1.3}, {"postSpacing", 2.4}, {"postSize", 0.15}, {"railCount", 4}, {"railHeight", 0.14}, {"railDepth", 0.035}, {"railBottom", 0.2}, {"railTop", 0.08}},
       {{"post", "square"}, {"cap", "flat"}, {"rail", "board"}, {"infill", "none"}, {"gate", "braced"}}, dark);
    St("wrought_iron", "Wrought iron", "Slim posts with ball caps and round bars with spear tops.",
       {{"height", 1.5}, {"postSpacing", 2}, {"postSize", 0.06}, {"railCount", 2}, {"railHeight", 0.04}, {"railDepth", 0.04}, {"railBottom", 0.12}, {"railTop", 0.12},
        {"infillWidth", 0.02}, {"infillGap", 0.1}, {"infillThick", 0.02}, {"infillBottom", 0.1}},
       {{"post", "square"}, {"cap", "ball"}, {"rail", "board"}, {"infill", "bars_spear"}, {"gate", "bars"}}, iron);
    St("chain_link", "Chain link", "Thin round posts and a diagonal wire mesh between a top and a bottom rail.",
       {{"height", 1.8}, {"postSpacing", 2.5}, {"postSize", 0.07}, {"railCount", 2}, {"railHeight", 0.04}, {"railDepth", 0.04}, {"railBottom", 0.06}, {"railTop", 0.0},
        {"infillWidth", 0.012}, {"infillGap", 0.05}, {"infillThick", 0.012}, {"infillBottom", 0.06}, {"infillDrop", 0.06}},
       {{"post", "round"}, {"cap", "none"}, {"rail", "round"}, {"infill", "chainlink"}, {"gate", "open"}}, zinc);
    St("stockade", "Stockade (privacy)", "Tall flat boards side by side on three rails: no gaps to see through.",
       {{"height", 1.8}, {"postSpacing", 2.4}, {"postSize", 0.12}, {"railCount", 3}, {"railHeight", 0.09}, {"railDepth", 0.04}, {"railBottom", 0.2}, {"railTop", 0.25},
        {"infillWidth", 0.14}, {"infillGap", 0.004}, {"infillThick", 0.025}, {"infillBottom", 0.03}},
       {{"post", "square"}, {"cap", "flat"}, {"rail", "board"}, {"infill", "flat"}, {"gate", "planks"}}, wood);
    St("low_decorative", "Low decorative", "A knee-high garden edge with rounded, alternating pickets.",
       {{"height", 0.6}, {"postSpacing", 1.2}, {"postSize", 0.08}, {"railCount", 1}, {"railHeight", 0.05}, {"railDepth", 0.03}, {"railBottom", 0.1}, {"railTop", 0.2},
        {"infillWidth", 0.07}, {"infillGap", 0.03}, {"infillThick", 0.02}, {"infillBottom", 0.03}, {"infillAlt", 0.05}},
       {{"post", "round"}, {"cap", "ball"}, {"rail", "round"}, {"infill", "rounded"}, {"gate", "bars"}}, green);

    t.generate = Fence;
    return t;
}

} // namespace

void RegisterArchitectureTemplates();
void RegisterVehicleTemplates();
void RegisterCharacterTemplates();
void RegisterEnvironmentTemplates();
void RegisterPropTemplates();

void RegisterBuiltinTemplates() {
    static bool done = false;
    if (done) return;
    done = true;
    Registry::Instance().Register(MakeFence());
    RegisterArchitectureTemplates();
    RegisterVehicleTemplates();
    RegisterCharacterTemplates();
    RegisterEnvironmentTemplates();
    RegisterPropTemplates();
}

} // namespace okay::modeling::guided
