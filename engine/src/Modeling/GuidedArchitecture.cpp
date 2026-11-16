// Architecture templates for guided modeling: wall, stairs, door, window, roof and a building that composes them.
// Written from general parametric modeling (grids with holes, boxes, extruded profiles, polylines): no code, models or
// assets from other projects. Every generated part is a closed solid; parts never share a corner position (small
// deliberate overlaps keep them apart), so the regeneration rules in Guided.hpp can tell them apart.
#include "okay/Modeling/Guided.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace okay::modeling::guided {
namespace {

constexpr float kPi = 3.14159265f;
const Vec3 kUp{0, 1, 0};
float Pf(const Recipe& r, const char* id) { return (float)r.Param(id); }

// ---- Small geometry helpers ------------------------------------------------------------------------------
PolyMesh BoxAA(Vec3 lo, Vec3 hi) {
    Frame f;
    f.origin = {(lo.x + hi.x) * 0.5f, lo.y, (lo.z + hi.z) * 0.5f};
    f.t = {1, 0, 0}; f.u = {0, 1, 0}; f.n = {0, 0, 1};
    f.sx = std::max(hi.x - lo.x, 1e-3f); f.sy = std::max(hi.y - lo.y, 1e-3f); f.sz = std::max(hi.z - lo.z, 1e-3f);
    return shapes::Box(f);
}

// A box from point a to point b with a w x h cross-section (w sideways, h up).
PolyMesh Beam(Vec3 a, Vec3 b, float w, float h) {
    const Vec3 d = b - a;
    const float len = d.Magnitude();
    if (len < 1e-4f) return PolyMesh();
    Frame f;
    f.t = d / len;
    Vec3 n = Vec3::Cross(f.t, kUp);
    if (n.SqrMagnitude() < 1e-6f) n = Vec3{0, 0, 1};
    f.n = n.Normalized();
    f.u = Vec3::Cross(f.n, f.t);
    f.sx = len; f.sy = h; f.sz = w;
    f.origin = (a + b) * 0.5f - f.u * (h * 0.5f);
    return shapes::Box(f);
}

// Local (x, y, z) -> o + t*x + up*y + n*z, keeping faces outward.
void Place(PolyMesh& m, Vec3 o, Vec3 t, Vec3 n) {
    for (Vec3& v : m.verts) v = o + t * v.x + kUp * v.y + n * v.z;
    if (Vec3::Dot(Vec3::Cross(t, kUp), n) < 0)
        for (PolyFace& f : m.faces) { std::reverse(f.verts.begin(), f.verts.end()); std::reverse(f.uvs.begin(), f.uvs.end()); }
}

void RotateY(PolyMesh& m, Vec3 pivot, float rad) {
    const float c = std::cos(rad), s = std::sin(rad);
    for (Vec3& v : m.verts) {
        const float x = v.x - pivot.x, z = v.z - pivot.z;
        v.x = pivot.x + c * x + s * z;
        v.z = pivot.z - s * x + c * z;
    }
}

struct Opening { float x0, y0, x1, y1; };

// A wall panel x 0..W, y 0..H, z -T/2..T/2 with rectangular holes: one closed shell (a grid of cells, the cells inside
// a hole left out, jamb faces where a wall cell meets a hole).
PolyMesh Panel(float W, float H, float T, const std::vector<Opening>& holes) {
    std::vector<float> xs = {0, W}, ys = {0, H};
    for (const Opening& o : holes) { xs.push_back(o.x0); xs.push_back(o.x1); ys.push_back(o.y0); ys.push_back(o.y1); }
    auto uniq = [](std::vector<float>& v) {
        std::sort(v.begin(), v.end());
        std::vector<float> r;
        for (float x : v) if (r.empty() || x - r.back() > 1e-3f) r.push_back(x);
        v = r;
    };
    uniq(xs); uniq(ys);
    const int nx = (int)xs.size() - 1, ny = (int)ys.size() - 1;
    auto solid = [&](int i, int j) {
        if (i < 0 || j < 0 || i >= nx || j >= ny) return false;
        const float cx = 0.5f * (xs[i] + xs[i + 1]), cy = 0.5f * (ys[j] + ys[j + 1]);
        for (const Opening& o : holes) if (cx > o.x0 && cx < o.x1 && cy > o.y0 && cy < o.y1) return false;
        return true;
    };
    PolyMesh m;
    std::map<std::array<int, 3>, int> vid;
    auto V = [&](int i, int j, int side) {   // side 0 = front (+z), 1 = back (-z)
        auto key = std::array<int, 3>{i, j, side};
        auto it = vid.find(key);
        if (it != vid.end()) return it->second;
        const int id = (int)m.verts.size();
        m.verts.push_back({xs[i], ys[j], side ? -T * 0.5f : T * 0.5f});
        vid[key] = id;
        return id;
    };
    auto quad = [&](int a, int b, int c, int d) { PolyFace f; f.verts = {a, b, c, d}; m.faces.push_back(f); };
    for (int i = 0; i < nx; ++i)
        for (int j = 0; j < ny; ++j) {
            if (!solid(i, j)) continue;
            quad(V(i, j, 0), V(i + 1, j, 0), V(i + 1, j + 1, 0), V(i, j + 1, 0));   // front, seen from +z
            quad(V(i, j, 1), V(i, j + 1, 1), V(i + 1, j + 1, 1), V(i + 1, j, 1));   // back
            // sides where the neighbour is not wall: outward = edge direction turned clockwise
            auto side = [&](int ai, int aj, int bi, int bj) { quad(V(ai, aj, 0), V(ai, aj, 1), V(bi, bj, 1), V(bi, bj, 0)); };
            if (!solid(i, j - 1)) side(i, j, i + 1, j);
            if (!solid(i + 1, j)) side(i + 1, j, i + 1, j + 1);
            if (!solid(i, j + 1)) side(i + 1, j + 1, i, j + 1);
            if (!solid(i - 1, j)) side(i, j + 1, i, j);
        }
    return m;
}

PolyMesh Fill(const Recipe& r, const std::string& design, const Frame& f, const std::function<PolyMesh(const std::string&)>& builtin) {
    if (design.rfind("user:", 0) == 0) {
        auto it = r.userParts.find(std::string(design, 5));
        if (it != r.userParts.end()) return FitUserPart(it->second, f);
        return shapes::Box(f);
    }
    return builtin(design);
}
Frame LocalFrame(float w, float h, float d) {   // the box x 0..w, y 0..h, z -d/2..d/2 of a local part
    Frame f;
    f.origin = {w * 0.5f, 0, 0};
    f.t = {1, 0, 0}; f.u = {0, 1, 0}; f.n = {0, 0, 1};
    f.sx = w; f.sy = h; f.sz = d;
    return f;
}

// Raised brick courses across a wall panel (local x 0..len, y 0..H, z centred, front face at +T/2): thin horizontal boards that
// stop at the openings. High: every 0.3 m; Ultra: every 0.15 m.
PolyMesh Courses(float len, float H, float T, const std::vector<Opening>& holes, float step) {
    PolyMesh m;
    for (float y = step; y < H - step * 0.6f; y += step) {
        std::vector<std::pair<float, float>> free = {{0.04f, len - 0.04f}};
        for (const Opening& o : holes) {
            if (y + 0.012f < o.y0 - 0.03f || y - 0.012f > o.y1 + 0.03f) continue;
            std::vector<std::pair<float, float>> nf;
            for (const auto& f : free) {
                if (o.x1 + 0.03f <= f.first || o.x0 - 0.03f >= f.second) { nf.push_back(f); continue; }
                if (o.x0 - 0.03f > f.first + 0.05f) nf.push_back({f.first, o.x0 - 0.03f});
                if (o.x1 + 0.03f < f.second - 0.05f) nf.push_back({o.x1 + 0.03f, f.second});
            }
            free = nf;
        }
        for (const auto& f : free) shapes::Append(m, BoxAA({f.first, y - 0.006f, T / 2 - 0.004f}, {f.second, y + 0.006f, T / 2 + 0.01f}));
    }
    return m;
}

// ---- Window ------------------------------------------------------------------------------------------------
struct WindowSpec { float w, h, d, fw; std::string frame, grid, glass, sill, shutters; };
struct WindowParts { PolyMesh frame, grid, glass, sill, shutters[2]; };

void GridCells(const std::string& g, int& c, int& r) {
    c = 1; r = 1;
    if (g == "casement") c = 2;
    else if (g == "cross") { c = 2; r = 2; }
    else if (g == "colonial") { c = 3; r = 3; }
    else if (g == "tall") r = 2;
}

WindowParts BuildWindow(const WindowSpec& s) {
    WindowParts p;
    const float w = s.w, h = s.h, d = s.d, e = 0.004f;
    const float fw = std::min(s.fw * (s.frame == "heavy" ? 1.6f : 1.0f), std::min(w, h) * 0.3f);
    shapes::Append(p.frame, BoxAA({0, 0, -d / 2}, {w, fw, d / 2}));
    shapes::Append(p.frame, BoxAA({0, h - fw, -d / 2}, {w, h, d / 2}));
    shapes::Append(p.frame, BoxAA({0, fw - e, -d / 2}, {fw, h - fw + e, d / 2}));
    shapes::Append(p.frame, BoxAA({w - fw, fw - e, -d / 2}, {w, h - fw + e, d / 2}));
    if (s.frame == "ornate") shapes::Append(p.frame, BoxAA({-0.06f, h - e, -d / 2 - 0.03f}, {w + 0.06f, h + 0.09f, d / 2 + 0.03f}));
    int c, r;
    GridCells(s.grid, c, r);
    const float mw = 0.03f, ix0 = fw, ix1 = w - fw, iy0 = fw, iy1 = h - fw;
    const float cw = (ix1 - ix0) / (float)c, ch = (iy1 - iy0) / (float)r;
    for (int k = 1; k < c; ++k) shapes::Append(p.grid, BoxAA({ix0 + cw * k - mw / 2, iy0 - e, -d * 0.3f}, {ix0 + cw * k + mw / 2, iy1 + e, d * 0.3f}));
    for (int k = 1; k < r; ++k) shapes::Append(p.grid, BoxAA({ix0 - e, iy0 + ch * k - mw / 2, -d * 0.3f - 0.002f}, {ix1 + e, iy0 + ch * k + mw / 2, d * 0.3f + 0.002f}));
    if (s.glass != "none")
        for (int i = 0; i < c; ++i)
            for (int j = 0; j < r; ++j) {
                const float x0 = ix0 + cw * i + (i > 0 ? mw / 2 : 0) - 0.006f, x1 = ix0 + cw * (i + 1) - (i + 1 < c ? mw / 2 : 0) + 0.006f;
                const float y0 = iy0 + ch * j + (j > 0 ? mw / 2 : 0) - 0.006f, y1 = iy0 + ch * (j + 1) - (j + 1 < r ? mw / 2 : 0) + 0.006f;
                shapes::Append(p.glass, BoxAA({x0, y0, -0.007f}, {x1, y1, 0.007f}));
            }
    if (s.sill == "slab") p.sill = BoxAA({-0.05f, -0.04f, -d / 2 - 0.06f}, {w + 0.05f, 0.02f, d / 2 + 0.04f});
    if (s.shutters != "none") {
        const float sw = w * 0.5f;
        for (int side = 0; side < 2; ++side) {
            const float x0 = side == 0 ? -sw - 0.03f : w + 0.03f, x1 = x0 + sw;
            PolyMesh& m = p.shutters[side];
            if (s.shutters == "louvre") {
                const int n = std::max(3, (int)(h / 0.07f));
                shapes::Append(m, BoxAA({x0, 0, -0.012f}, {x0 + 0.04f, h, 0.012f}));
                shapes::Append(m, BoxAA({x1 - 0.04f, 0, -0.012f}, {x1, h, 0.012f}));
                for (int k = 0; k < n; ++k) {
                    const float y = 0.02f + (h - 0.06f) * (float)k / (float)n;
                    shapes::Append(m, BoxAA({x0 + 0.036f, y, -0.018f}, {x1 - 0.036f, y + 0.035f, 0.018f}));
                }
            } else {
                shapes::Append(m, BoxAA({x0, 0, -0.015f}, {x1, h, 0.015f}));
            }
        }
    }
    return p;
}

// ---- Door --------------------------------------------------------------------------------------------------
struct DoorSpec { float w, h, d, fj, lt, openDeg; bool dbl; std::string frame, leaf, handle; };
struct DoorParts { PolyMesh frame; PolyMesh leaf[2]; PolyMesh handle[2]; int leaves = 1; };

PolyMesh LeafMesh(const std::string& design, float lw, float lh, float lt) {
    PolyMesh m;
    const float e = 0.004f;
    if (design == "panelled") {
        shapes::Append(m, BoxAA({0, 0, -lt / 2}, {lw, lh, lt / 2}));
        const float mx = lw * 0.18f, gap = lw * 0.08f, pw = (lw - 2 * mx - gap) * 0.5f;
        const float py[3] = {lh * 0.08f, lh * 0.52f, 0};
        const float ph[3] = {lh * 0.38f, lh * 0.38f, 0};
        for (int c = 0; c < 2; ++c)
            for (int r = 0; r < 2; ++r) {
                const float x0 = mx + c * (pw + gap), y0 = py[r];
                shapes::Append(m, BoxAA({x0, y0, lt / 2 - 0.01f}, {x0 + pw, y0 + ph[r], lt / 2 + 0.015f}));
                shapes::Append(m, BoxAA({x0, y0, -lt / 2 - 0.015f}, {x0 + pw, y0 + ph[r], -lt / 2 + 0.01f}));
            }
    } else if (design == "glazed") {
        const float st = std::min(0.1f, lw * 0.2f), rl = 0.12f;
        shapes::Append(m, BoxAA({0, 0, -lt / 2}, {st, lh, lt / 2}));
        shapes::Append(m, BoxAA({lw - st, 0, -lt / 2}, {lw, lh, lt / 2}));
        shapes::Append(m, BoxAA({st - e, 0, -lt / 2}, {lw - st + e, lh * 0.45f, lt / 2}));
        shapes::Append(m, BoxAA({st - e, lh - rl, -lt / 2}, {lw - st + e, lh, lt / 2}));
        shapes::Append(m, BoxAA({st - 0.01f, lh * 0.45f - 0.01f, -0.007f}, {lw - st + 0.01f, lh - rl + 0.01f, 0.007f}));
    } else if (design == "ledged") {
        const int n = std::max(2, (int)std::ceil(lw / 0.14f));
        const float pw = lw / (float)n;
        for (int k = 0; k < n; ++k) shapes::Append(m, BoxAA({pw * k + 0.003f, 0, -lt / 2}, {pw * (k + 1) - 0.003f, lh, lt / 2 - 0.012f}));
        for (int k = 0; k < 3; ++k) {
            const float y = lh * (0.1f + 0.4f * k);
            shapes::Append(m, BoxAA({0.04f, y, lt / 2 - 0.02f}, {lw - 0.04f, y + 0.12f, lt / 2 + 0.02f}));
        }
    } else {
        m = BoxAA({0, 0, -lt / 2}, {lw, lh, lt / 2});
    }
    return m;
}

PolyMesh HandleMesh(const std::string& design, float x, float y, float lt) {
    PolyMesh m;
    if (design == "none") return m;
    for (int side = 0; side < 2; ++side) {
        const float z = side ? -lt / 2 : lt / 2, sgn = side ? -1.0f : 1.0f;
        if (design == "knob") {
            Frame f; f.origin = {x, y - 0.035f, z + sgn * 0.045f}; f.t = {1, 0, 0}; f.u = {0, 1, 0}; f.n = {0, 0, 1};
            f.sx = f.sy = f.sz = 0.07f;
            shapes::Append(m, shapes::Ball(f, 4, 8));
            shapes::Append(m, BoxAA({x - 0.012f, y - 0.012f, std::min(z - sgn * 0.01f, z + sgn * 0.045f)}, {x + 0.012f, y + 0.012f, std::max(z - sgn * 0.01f, z + sgn * 0.045f)}));
        } else if (design == "bar") {
            const float z0 = z + sgn * 0.01f, z1 = z + sgn * 0.06f;
            shapes::Append(m, BoxAA({x - 0.015f, y - 0.25f, std::min(z0, z1)}, {x + 0.015f, y + 0.25f, std::max(z0, z1)}));
        } else {   // lever: a rose and an arm
            const float z0 = z + sgn * 0.005f, z1 = z + sgn * 0.035f;
            shapes::Append(m, BoxAA({x - 0.025f, y - 0.025f, std::min(z0, z1)}, {x + 0.025f, y + 0.025f, std::max(z0, z1)}));
            const float z2 = z + sgn * 0.03f, z3 = z + sgn * 0.055f;
            shapes::Append(m, BoxAA({x - 0.12f, y - 0.012f, std::min(z2, z3)}, {x + 0.012f, y + 0.012f, std::max(z2, z3)}));
        }
    }
    return m;
}

DoorParts BuildDoor(const DoorSpec& s) {
    DoorParts p;
    const float w = s.w, h = s.h, d = s.d, fj = std::min(s.fj, w * 0.2f), e = 0.004f;
    shapes::Append(p.frame, BoxAA({0, 0, -d / 2}, {fj, h, d / 2}));
    shapes::Append(p.frame, BoxAA({w - fj, 0, -d / 2}, {w, h, d / 2}));
    shapes::Append(p.frame, BoxAA({fj - e, h - fj, -d / 2}, {w - fj + e, h, d / 2}));
        if (s.frame == "architrave") {
        const float cw = 0.08f;
        for (int side = 0; side < 2; ++side) {
            const float z0 = side ? -d / 2 - 0.02f : d / 2 - 0.004f, z1 = side ? -d / 2 + 0.004f : d / 2 + 0.02f;
            shapes::Append(p.frame, BoxAA({-cw, 0, z0}, {-0.004f, h + cw, z1}));
            shapes::Append(p.frame, BoxAA({w + 0.004f, 0, z0}, {w + cw, h + cw, z1}));
            shapes::Append(p.frame, BoxAA({-cw - 0.002f, h + 0.004f, z0}, {w + cw + 0.002f, h + cw, z1}));
        }
    }
    const float ox0 = fj + 0.01f, ox1 = w - fj - 0.01f, oh = h - fj - 0.01f, oy = 0.01f;
    const float openRad = s.openDeg * kPi / 180.0f;
    const int leaves = s.dbl ? 2 : 1;
    p.leaves = leaves;
    for (int i = 0; i < leaves; ++i) {
        const float lx0 = leaves == 1 ? ox0 : (i == 0 ? ox0 : 0.5f * (ox0 + ox1) + 0.004f);
        const float lx1 = leaves == 1 ? ox1 : (i == 0 ? 0.5f * (ox0 + ox1) - 0.004f : ox1);
        const float lw = lx1 - lx0;
        PolyMesh leaf = LeafMesh(s.leaf, lw, oh, s.lt);
        const bool hingeLeft = i == 0;
        const float hx = hingeLeft ? 0.0f : lw;
        const float handleX = hingeLeft ? lw - 0.09f : 0.09f;
        PolyMesh hd = HandleMesh(s.handle, handleX, std::min(1.0f, oh * 0.47f), s.lt);
        for (PolyMesh* m : {&leaf, &hd}) {
            for (Vec3& v : m->verts) { v.x += lx0; v.y += oy; }
        }
        const float ang = hingeLeft ? openRad : -openRad;
        const Vec3 pivot{lx0 + hx, 0, 0};
        RotateY(leaf, pivot, ang);
        RotateY(hd, pivot, ang);
        p.leaf[i] = leaf; p.handle[i] = hd;
    }
    return p;
}

// ---- Roof ------------------------------------------------------------------------------------------------------
// Built around the origin: eaves at y = 0, the footprint w (x) by d (z) centred, `o` of overhang all round.
PolyMesh BuildRoof(const std::string& type, float w, float d, float pitchDeg, float o, float thick) {
    const float hx = w * 0.5f + o, hz = d * 0.5f + o;
    const float tn = std::tan(std::min(std::max(pitchDeg, 1.0f), 70.0f) * kPi / 180.0f);
    if (type == "hip") {
        const bool alongX = hx >= hz;
        const float rise = std::min(hx, hz) * tn, half = std::max(std::fabs(hx - hz), 0.01f);
        PolyMesh m;
        m.verts = {{-hx, 0, -hz}, {hx, 0, -hz}, {hx, 0, hz}, {-hx, 0, hz},
                   alongX ? Vec3{-half, rise, 0} : Vec3{0, rise, -half}, alongX ? Vec3{half, rise, 0} : Vec3{0, rise, half}};
        auto face = [&](std::vector<int> v) { PolyFace f; f.verts = std::move(v); m.faces.push_back(f); };
        face({0, 1, 2, 3});
        if (alongX) { face({0, 1, 5, 4}); face({1, 2, 5}); face({2, 3, 4, 5}); face({3, 0, 4}); }
        else        { face({0, 3, 5, 4}); face({1, 2, 5, 4}); face({0, 1, 4}); face({2, 3, 5}); }
        shapes::OrientOutward(m, {0, rise * 0.3f, 0});
        return m;
    }
    if (type == "shed") {
        const float hh = 2 * hz * tn;
        return shapes::ExtrudeAxes({0, 0, 0}, {0, 0, 1}, {0, 1, 0}, {1, 0, 0}, {{-hz, 0}, {hz, 0}, {hz, thick + hh}, {-hz, thick}}, 2 * hx);
    }
    if (type == "flat") return BoxAA({-hx, 0, -hz}, {hx, thick, hz});
    // gable: ridge along x
    const float rise = hz * tn;
    return shapes::ExtrudeAxes({0, 0, 0}, {0, 0, 1}, {0, 1, 0}, {1, 0, 0}, {{-hz, 0}, {hz, 0}, {0, rise}}, 2 * hx);
}

// Roof tiles and gutters (High and Ultra): a row of tiles along every slope, a gutter along each low edge. Same local frame as BuildRoof.
PolyMesh RoofRod(Vec3 a, Vec3 b, float r, bool round, int sides) {
    const Vec3 d = b - a;
    const float len = d.Magnitude();
    if (len < 1e-4f) return PolyMesh();
    Frame f;
    f.u = d / len;
    const Vec3 ref = std::fabs(f.u.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    f.t = Vec3::Cross(ref, f.u).Normalized();
    f.n = Vec3::Cross(f.t, f.u);
    f.origin = a; f.sx = r * 2; f.sz = r * 2; f.sy = len;
    return round ? shapes::Prism(f, sides) : shapes::Box(f);
}
std::vector<PolyMesh> RoofTiles(const std::string& type, float w, float d, float pitchDeg, float o, float thick) {   // one mesh per slope, then the gutters
    std::vector<PolyMesh> res;
    PolyMesh m;
    if (type == "flat") return res;
    const float hx = w * 0.5f + o, hz = d * 0.5f + o;
    const float tn = std::tan(std::min(std::max(pitchDeg, 1.0f), 70.0f) * kPi / 180.0f);
    struct Quad { Vec3 eL, eR, rL, rR; };
    std::vector<Quad> planes;
    if (type == "shed") {
        const float hh = 2 * hz * tn;
        planes.push_back({{-hx, thick, -hz}, {hx, thick, -hz}, {-hx, thick + hh, hz}, {hx, thick + hh, hz}});
    } else if (type == "hip") {
        const bool alongX = hx >= hz;
        const float rise = std::min(hx, hz) * tn, half = std::max(std::fabs(hx - hz), 0.01f);
        const Vec3 pa = alongX ? Vec3{-half, rise, 0} : Vec3{0, rise, -half}, pb = alongX ? Vec3{half, rise, 0} : Vec3{0, rise, half};
        if (alongX) {
            planes.push_back({{-hx, 0, hz}, {hx, 0, hz}, {-half, rise, 0}, {half, rise, 0}});
            planes.push_back({{hx, 0, -hz}, {-hx, 0, -hz}, {half, rise, 0}, {-half, rise, 0}});
            planes.push_back({{hx, 0, hz}, {hx, 0, -hz}, pb, pb});
            planes.push_back({{-hx, 0, -hz}, {-hx, 0, hz}, pa, pa});
        } else {
            planes.push_back({{hx, 0, hz}, {-hx, 0, hz}, {0, rise, half}, {0, rise, half}});
            planes.push_back({{-hx, 0, -hz}, {hx, 0, -hz}, {0, rise, -half}, {0, rise, -half}});
            planes.push_back({{hx, 0, -hz}, {hx, 0, hz}, {0, rise, -half}, {0, rise, half}});
            planes.push_back({{-hx, 0, hz}, {-hx, 0, -hz}, {0, rise, half}, {0, rise, -half}});
        }
    } else {   // gable, ridge along x
        const float rise = hz * tn;
        planes.push_back({{-hx, 0, hz}, {hx, 0, hz}, {-hx, rise, 0}, {hx, rise, 0}});
        planes.push_back({{hx, 0, -hz}, {-hx, 0, -hz}, {hx, rise, 0}, {-hx, rise, 0}});
    }
    const int sd = 8;
    for (const Quad& q : planes) {
        PolyMesh pm;
        const Vec3 mid = (q.eL + q.eR + q.rL + q.rR) * 0.25f;
        Vec3 nrm = Vec3::Cross(q.eR - q.eL, q.rL - q.eL);
        if (nrm.Magnitude() < 1e-5f) nrm = Vec3::Cross(q.eR - q.eL, q.rR - q.eL);
        nrm = nrm.Normalized();
        if (Vec3::Dot(nrm, {0, 1, 0}) < 0) nrm = nrm * -1.0f;
        const float slope = ((q.rL + q.rR) * 0.5f - (q.eL + q.eR) * 0.5f).Magnitude();
        const int rows = std::max(3, std::min(60, (int)(slope / 0.17f)));
        for (int k = 0; k < rows; ++k) {
            const float t = ((float)k + 0.62f) / (float)rows;
            const Vec3 a = q.eL + (q.rL - q.eL) * t, b = q.eR + (q.rR - q.eR) * t;
            const Vec3 dirv = (b - a);
            if (dirv.Magnitude() < 0.25f) continue;
            const Vec3 u = dirv.Normalized();
            const Vec3 a2 = a + u * 0.05f + nrm * 0.016f, b2 = b - u * 0.05f + nrm * 0.004f;
            shapes::Append(pm, RoofRod(a2, b2, 0.02f, false, sd));
        }
        (void)mid;
        if (!pm.faces.empty()) res.push_back(std::move(pm));
    }
    // gutters along the low edges
    auto gutter = [&](Vec3 a, Vec3 b) { shapes::Append(m, RoofRod(a + Vec3{0, -0.012f, 0}, b + Vec3{0, -0.012f, 0}, 0.045f, true, 8)); };
    if (type == "shed") gutter({-hx, thick - 0.0f, -hz - 0.02f}, {hx, thick - 0.0f, -hz - 0.02f});
    else if (type == "hip") { gutter({-hx - 0.02f, 0, -hz}, {-hx - 0.02f, 0, hz}); gutter({hx + 0.02f, 0, -hz}, {hx + 0.02f, 0, hz}); gutter({-hx, 0, hz + 0.02f}, {hx, 0, hz + 0.02f}); gutter({-hx, 0, -hz - 0.02f}, {hx, 0, -hz - 0.02f}); }
    else { gutter({-hx, 0, hz + 0.02f}, {hx, 0, hz + 0.02f}); gutter({-hx, 0, -hz - 0.02f}, {hx, 0, -hz - 0.02f}); }
    if (!m.faces.empty()) res.push_back(std::move(m));
    return res;
}

PolyMesh RoofTrim(const std::string& type, float w, float d, float pitchDeg, float o, float thick) {
    PolyMesh m;
    const float hx = w * 0.5f + o, hz = d * 0.5f + o;
    const float tn = std::tan(std::min(std::max(pitchDeg, 1.0f), 70.0f) * kPi / 180.0f);
    if (type == "gable") {
        const float rise = hz * tn;
        m = BoxAA({-hx - 0.04f, rise - 0.03f, -0.07f}, {hx + 0.04f, rise + 0.07f, 0.07f});
    } else if (type == "hip") {
        const bool alongX = hx >= hz;
        const float rise = std::min(hx, hz) * tn, half = std::max(std::fabs(hx - hz), 0.01f);
        m = alongX ? BoxAA({-half, rise - 0.03f, -0.07f}, {half, rise + 0.07f, 0.07f}) : BoxAA({-0.07f, rise - 0.03f, -half}, {0.07f, rise + 0.07f, half});
    } else if (type == "flat") {
        const float ph = 0.3f, t = 0.1f;
        shapes::Append(m, BoxAA({-hx, thick - 0.01f, -hz}, {hx, thick + ph, -hz + t}));
        shapes::Append(m, BoxAA({-hx, thick - 0.01f, hz - t}, {hx, thick + ph, hz}));
        shapes::Append(m, BoxAA({-hx, thick - 0.01f, -hz + t - 0.004f}, {-hx + t, thick + ph, hz - t + 0.004f}));
        shapes::Append(m, BoxAA({hx - t, thick - 0.01f, -hz + t - 0.004f}, {hx, thick + ph, hz - t + 0.004f}));
    } else {   // shed: a fascia board along the high edge
        const float hh = 2 * hz * tn;
        m = BoxAA({-hx, thick + hh - 0.1f, hz - 0.004f}, {hx, thick + hh + 0.05f, hz + 0.05f});
    }
    return m;
}

// ---- Path helper (walls) --------------------------------------------------------------------------------------
struct Run {
    std::vector<Vec3> pts;
    std::vector<float> cum;
    float total = 0;
};
Run MakeRun(const Recipe& r, float defaultLength) {
    Run run;
    run.pts = r.path;
    if (run.pts.size() < 2) run.pts = {{0, 0, 0}, {defaultLength, 0, 0}};
    run.cum.push_back(0);
    for (size_t i = 1; i < run.pts.size(); ++i) {
        const Vec3 d = run.pts[i] - run.pts[i - 1];
        run.cum.push_back(run.cum.back() + std::sqrt(d.x * d.x + d.z * d.z));
    }
    run.total = run.cum.back();
    return run;
}

// Which design a part gets, with the user-part fallback to a plain box.
std::string Design(const Recipe& r, const std::string& slot, const std::string& id) { return r.DesignFor(slot, id); }

// ---- Templates -------------------------------------------------------------------------------------------------
struct Builder {
    Template t;
    using K = ParamDef::Kind;
    void P(const char* id, const char* label, K k, double def, double mn, double mx, const char* group, const char* tip) {
        ParamDef p; p.id = id; p.label = label; p.kind = k; p.def = def; p.min = mn; p.max = mx; p.group = group; p.tip = tip;
        t.params.push_back(p);
    }
    void Choice(const char* id, const char* label, std::vector<std::string> ch, int def, const char* group, const char* tip) {
        ParamDef p; p.id = id; p.label = label; p.kind = K::Choice; p.def = def; p.min = 0; p.max = (double)ch.size() - 1; p.group = group; p.tip = tip;
        p.choices = std::move(ch);
        t.params.push_back(p);
    }
    void S(const char* id, const char* name, std::vector<DesignDef> d, Color c, int mat) {
        SlotDef s; s.id = id; s.name = name; s.designs = std::move(d); s.defaultColor = c; s.defaultMaterial = mat;
        t.slots.push_back(s);
    }
    void St(const char* id, const char* name, const char* desc, std::map<std::string, double> p, std::map<std::string, std::string> sd,
            std::map<std::string, Color> colors) {
        StartDesign s; s.id = id; s.name = name; s.description = desc; s.params = std::move(p); s.slotDesign = std::move(sd); s.colors = std::move(colors);
        t.starts.push_back(s);
    }
};

const Color kPlaster{0.90f, 0.86f, 0.78f, 1}, kBrick{0.62f, 0.30f, 0.22f, 1}, kStone{0.60f, 0.60f, 0.58f, 1}, kWood{0.55f, 0.38f, 0.22f, 1},
            kWhite{0.94f, 0.94f, 0.92f, 1}, kGlass{0.62f, 0.80f, 0.86f, 1}, kMetal{0.35f, 0.35f, 0.38f, 1}, kRoofRed{0.55f, 0.22f, 0.16f, 1},
            kSlate{0.30f, 0.32f, 0.36f, 1}, kDark{0.25f, 0.17f, 0.12f, 1};

// -- shared slot lists --
std::vector<DesignDef> WindowGridDesigns() {
    return {{"single", "Single pane"}, {"casement", "Casement (2 wide)"}, {"cross", "Cross (2 x 2)"}, {"colonial", "Colonial (3 x 3)"}, {"tall", "Tall (2 high)"}};
}
std::vector<DesignDef> WindowFrameDesigns() { return {{"plain", "Plain"}, {"heavy", "Heavy"}, {"ornate", "Ornate (with head)"}}; }
std::vector<DesignDef> DoorLeafDesigns() { return {{"flat", "Flat"}, {"panelled", "Panelled"}, {"glazed", "Glazed"}, {"ledged", "Planks and ledges"}}; }
std::vector<DesignDef> DoorHandleDesigns() { return {{"lever", "Lever"}, {"knob", "Knob"}, {"bar", "Pull bar"}, {"none", "None"}}; }
std::vector<DesignDef> DoorFrameDesigns() { return {{"plain", "Plain"}, {"architrave", "With architrave"}}; }
std::vector<DesignDef> RoofDesigns() { return {{"gable", "Gable"}, {"hip", "Hip"}, {"shed", "Shed"}, {"flat", "Flat"}}; }

// ============================== Window template =====================================================================
void GenerateWindow(const Recipe& r, std::vector<GeneratedPart>& out) {
    WindowSpec s;
    s.w = Pf(r, "width"); s.h = Pf(r, "height"); s.d = Pf(r, "depth"); s.fw = Pf(r, "frameWidth");
    s.frame = Design(r, "frame", "frame/0"); s.grid = Design(r, "grid", "grid/0"); s.glass = Design(r, "glass", "glass/0");
    s.sill = Design(r, "sill", "sill/0"); s.shutters = Design(r, "shutters", "shutter/0");
    const bool ufr = s.frame.rfind("user:", 0) == 0;
    if (ufr) s.frame = "plain";
    WindowParts p = BuildWindow(s);
    const Frame box = LocalFrame(s.w, s.h, s.d);
    PolyMesh frame = ufr ? Fill(r, Design(r, "frame", "frame/0"), box, [&](const std::string&) { return PolyMesh(); }) : p.frame;
    const Vec3 o{-s.w * 0.5f, 0, 0};
    auto emit = [&](const std::string& id, const std::string& slot, PolyMesh m) { Place(m, o, {1, 0, 0}, {0, 0, 1}); out.push_back({id, slot, std::move(m)}); };
    emit("frame/0", "frame", frame);
    emit("grid/0", "grid", p.grid);
    emit("glass/0", "glass", p.glass);
    emit("sill/0", "sill", p.sill);
    emit("shutter/0", "shutters", p.shutters[0]);
    emit("shutter/1", "shutters", p.shutters[1]);
}

Template MakeWindow() {
    Builder b;
    b.t.id = "window"; b.t.name = "Window"; b.t.category = "Architecture"; b.t.version = 1;
    b.t.description = "A window with a frame, panes, bars, an optional sill and shutters. Use it on its own or as the window of a wall or building.";
    b.P("width", "Width", Builder::K::Float, 1.0, 0.3, 4, "Size", "Outside width of the frame.");
    b.P("height", "Height", Builder::K::Float, 1.3, 0.3, 4, "Size", "Outside height of the frame.");
    b.P("depth", "Depth", Builder::K::Float, 0.12, 0.04, 0.5, "Size", "How deep the frame is.");
    b.P("frameWidth", "Frame width", Builder::K::Float, 0.07, 0.02, 0.3, "Size", "Width of the frame bars.");
    b.S("frame", "Frame", WindowFrameDesigns(), kWhite, 0);
    b.S("grid", "Window bars", WindowGridDesigns(), kWhite, 0);
    b.S("glass", "Glass", {{"clear", "Glass"}, {"none", "No glass"}}, kGlass, 1);
    b.S("sill", "Sill", {{"slab", "Stone sill"}, {"none", "None"}}, kStone, 2);
    b.S("shutters", "Shutters", {{"none", "None"}, {"panel", "Panel shutters"}, {"louvre", "Louvre shutters"}}, kDark, 3);
    b.St("plain", "Plain window", "One pane in a plain frame with a sill.", {{"width", 1.0}, {"height", 1.3}},
         {{"frame", "plain"}, {"grid", "single"}, {"glass", "clear"}, {"sill", "slab"}, {"shutters", "none"}}, {});
    b.St("casement", "Casement", "Two side-by-side panes.", {{"width", 1.2}, {"height", 1.3}},
         {{"frame", "plain"}, {"grid", "casement"}, {"glass", "clear"}, {"sill", "slab"}, {"shutters", "none"}}, {});
    b.St("cross", "Cross window", "Four panes split by a cross.", {{"width", 1.1}, {"height", 1.4}},
         {{"frame", "heavy"}, {"grid", "cross"}, {"glass", "clear"}, {"sill", "slab"}, {"shutters", "none"}}, {});
    b.St("colonial", "Colonial", "Nine small panes with shutters.", {{"width", 1.0}, {"height", 1.5}},
         {{"frame", "plain"}, {"grid", "colonial"}, {"glass", "clear"}, {"sill", "slab"}, {"shutters", "panel"}}, {});
    b.St("tall_ornate", "Tall ornate", "A tall window with a decorated head and louvre shutters.", {{"width", 0.9}, {"height", 1.8}},
         {{"frame", "ornate"}, {"grid", "tall"}, {"glass", "clear"}, {"sill", "slab"}, {"shutters", "louvre"}}, {});
    b.t.generate = GenerateWindow;
    return b.t;
}

// ============================== Door template =======================================================================
void GenerateDoor(const Recipe& r, std::vector<GeneratedPart>& out) {
    DoorSpec s;
    s.w = Pf(r, "width"); s.h = Pf(r, "height"); s.d = Pf(r, "frameDepth"); s.fj = Pf(r, "frameWidth"); s.lt = Pf(r, "leafThickness");
    s.openDeg = Pf(r, "open"); s.dbl = r.Param("double") >= 0.5;
    s.frame = Design(r, "frame", "frame/0"); s.leaf = Design(r, "leaf", "leaf/0"); s.handle = Design(r, "handle", "handle/0");
    auto user = [&](const std::string& d) { return d.rfind("user:", 0) == 0; };
    const bool uf = user(s.frame), ul = user(s.leaf), uh = user(s.handle);
    if (uf) s.frame = "plain";
    if (ul) s.leaf = "flat";
    if (uh) s.handle = "lever";
    DoorParts p = BuildDoor(s);
    const Vec3 o{-s.w * 0.5f, 0, 0};
    auto emit = [&](const std::string& id, const std::string& slot, PolyMesh m) { Place(m, o, {1, 0, 0}, {0, 0, 1}); out.push_back({id, slot, std::move(m)}); };
    PolyMesh frame = p.frame;
    if (uf) frame = Fill(r, Design(r, "frame", "frame/0"), LocalFrame(s.w, s.h, s.d), [&](const std::string&) { return PolyMesh(); });
    emit("frame/0", "frame", frame);
    for (int i = 0; i < p.leaves; ++i) {
        PolyMesh leaf = p.leaf[i];
        if (ul) {
            const float lw = (s.w - 2 * s.fj - 0.02f) / (float)p.leaves;
            Frame f = LocalFrame(lw, s.h - s.fj - 0.02f, s.lt);
            f.origin = {s.fj + 0.01f + lw * (i + 0.5f), 0.01f, 0};
            leaf = Fill(r, Design(r, "leaf", "leaf/" + std::to_string(i)), f, [&](const std::string&) { return PolyMesh(); });
        }
        emit("leaf/" + std::to_string(i), "leaf", leaf);
        PolyMesh hd = p.handle[i];
        emit("handle/" + std::to_string(i), "handle", hd);
    }
    (void)uh;
}

Template MakeDoor() {
    Builder b;
    b.t.id = "door"; b.t.name = "Door"; b.t.category = "Architecture"; b.t.version = 1;
    b.t.description = "A door with its frame, leaf (or two) and handle. It can stand open. Use it alone or as the door of a wall or building.";
    b.P("width", "Width", Builder::K::Float, 1.0, 0.6, 3, "Size", "Outside width of the frame.");
    b.P("height", "Height", Builder::K::Float, 2.1, 1.6, 3.5, "Size", "Outside height of the frame.");
    b.P("frameWidth", "Frame width", Builder::K::Float, 0.08, 0.03, 0.2, "Size", "Width of the frame bars.");
    b.P("frameDepth", "Frame depth", Builder::K::Float, 0.15, 0.05, 0.5, "Size", "How deep the frame is (the wall thickness).");
    b.P("leafThickness", "Door thickness", Builder::K::Float, 0.045, 0.02, 0.12, "Size", "Thickness of the door leaf.");
    b.P("double", "Double door", Builder::K::Bool, 0, 0, 1, "Door", "Two leaves instead of one.");
    b.P("open", "Open (degrees)", Builder::K::Float, 0, 0, 100, "Door", "How far the door stands open.");
    b.S("frame", "Frame", DoorFrameDesigns(), kWhite, 0);
    b.S("leaf", "Door", DoorLeafDesigns(), kWood, 1);
    b.S("handle", "Handle", DoorHandleDesigns(), kMetal, 2);
    b.St("flat", "Flat door", "A plain door with a lever handle.", {{"width", 0.95}}, {{"frame", "plain"}, {"leaf", "flat"}, {"handle", "lever"}}, {});
    b.St("panelled", "Panelled door", "Four raised panels and an architrave.", {{"width", 1.0}},
         {{"frame", "architrave"}, {"leaf", "panelled"}, {"handle", "knob"}}, {});
    b.St("glazed", "Glazed door", "A door with a glass panel.", {{"width", 1.0}}, {{"frame", "plain"}, {"leaf", "glazed"}, {"handle", "bar"}}, {});
    b.St("barn", "Plank door", "Vertical planks on ledges.", {{"width", 1.1}, {"height", 2.2}}, {{"frame", "plain"}, {"leaf", "ledged"}, {"handle", "lever"}}, {{"leaf", kDark}});
    b.St("double", "Double door", "Two panelled leaves.", {{"width", 1.8}, {"double", 1}}, {{"frame", "architrave"}, {"leaf", "panelled"}, {"handle", "lever"}}, {});
    b.t.generate = GenerateDoor;
    return b.t;
}

// ============================== Roof template =======================================================================
void GenerateRoof(const Recipe& r, std::vector<GeneratedPart>& out) {
    const float w = Pf(r, "width"), d = Pf(r, "depth"), pitch = Pf(r, "pitch"), o = Pf(r, "overhang"), th = Pf(r, "thickness");
    const std::string type = Design(r, "roof", "roof/0");
    const bool user = type.rfind("user:", 0) == 0;
    PolyMesh body = BuildRoof(user ? "gable" : type, w, d, pitch, o, th);
    if (user) {
        Frame f = LocalFrame(w + 2 * o, std::tan(pitch * kPi / 180.0f) * (d * 0.5f + o), d + 2 * o);
        f.origin = {0, 0, 0};
        body = FitUserPart(r.userParts.count(std::string(type, 5)) ? r.userParts.at(std::string(type, 5)) : UserPart(), f);
        if (body.faces.empty()) body = BuildRoof("gable", w, d, pitch, o, th);
    }
    out.push_back({"roof/0", "roof", body});
    if (shapes::DetailF() >= 1.99f && Design(r, "tiles", "tiles/0") == "fine" && !user) { int k = 0; for (PolyMesh& t : RoofTiles(type, w, d, pitch, o, th)) out.push_back({"tiles/" + std::to_string(k++), "tiles", std::move(t)}); }
    if (Design(r, "trim", "trim/0") == "edge") out.push_back({"trim/0", "trim", RoofTrim(user ? "gable" : type, w, d, pitch, o, th)});
}

Template MakeRoof() {
    Builder b;
    b.t.id = "roof"; b.t.name = "Roof"; b.t.category = "Architecture"; b.t.version = 1;
    b.t.description = "A roof over a rectangle: gable, hip, shed or flat, with an overhang. Use it alone or as the roof of a building.";
    b.P("width", "Width (x)", Builder::K::Float, 8, 1, 60, "Size", "Length of the walls under the roof along x.");
    b.P("depth", "Depth (z)", Builder::K::Float, 6, 1, 60, "Size", "Length of the walls under the roof along z.");
    b.P("pitch", "Pitch (degrees)", Builder::K::Float, 30, 5, 65, "Size", "How steep the slopes are.");
    b.P("overhang", "Overhang", Builder::K::Float, 0.4, 0, 2, "Size", "How far the roof reaches past the walls.");
    b.P("thickness", "Slab thickness", Builder::K::Float, 0.2, 0.05, 0.6, "Size", "Thickness of a flat roof or the low edge of a shed roof.");
    b.S("roof", "Roof shape", RoofDesigns(), kRoofRed, 0);
    b.S("trim", "Trim", {{"none", "None"}, {"edge", "Ridge cap / parapet / fascia"}}, kSlate, 1);
    b.S("tiles", "Tiles and gutters", {{"fine", "Rows of tiles and gutters (High and up)"}, {"none", "None"}}, kSlate, 5);
    b.St("gable", "Gable roof", "Two slopes meeting at a ridge.", {{"pitch", 32}}, {{"roof", "gable"}, {"trim", "edge"}}, {});
    b.St("hip", "Hip roof", "Four slopes.", {{"pitch", 28}}, {{"roof", "hip"}, {"trim", "edge"}}, {});
    b.St("shed", "Shed roof", "One slope.", {{"pitch", 15}}, {{"roof", "shed"}, {"trim", "edge"}}, {{"roof", kSlate}});
    b.St("flat", "Flat roof", "A slab with a parapet.", {{"overhang", 0.1}, {"thickness", 0.25}}, {{"roof", "flat"}, {"trim", "edge"}}, {{"roof", kStone}});
    b.t.noSubdivisionSlots = {"roof"};   // a roof is flat planes meeting at hard edges; subdividing would round it into a dome
    b.t.generate = GenerateRoof;
    return b.t;
}

// ============================== Wall template =======================================================================
// A wall along a straight run or a path, with windows and a door cut into it and filled with real windows and doors.
void GenerateWall(const Recipe& r, std::vector<GeneratedPart>& out) {
    const float height = Pf(r, "height"), T = Pf(r, "thickness");
    const Run run = MakeRun(r, Pf(r, "length"));
    if (run.total < 0.2f) return;
    const int nWin = (int)r.Param("windows");
    const float ww = Pf(r, "windowWidth"), wh = std::min(Pf(r, "windowHeight"), height - 0.3f), sill = std::min(Pf(r, "sillHeight"), std::max(0.1f, height - wh - 0.15f));
    const bool door = r.Param("door") >= 0.5;
    const float dw = Pf(r, "doorWidth"), dh = std::min(Pf(r, "doorHeight"), height - 0.15f);
    const std::string wallDesign = Design(r, "wall", "wall/0");
    // openings along the whole run: (centre distance, width, kind)
    struct Op { float c, w; bool door; };
    std::vector<Op> ops;
    if (door) ops.push_back({std::min(std::max((float)r.Param("doorPos"), 0.0f), 1.0f) * run.total, dw, true});
    for (int k = 0; k < nWin; ++k) {
        const float c = run.total * ((float)k + 0.5f) / (float)nWin;
        bool clash = false;
        for (const Op& o : ops) if (o.door && std::fabs(o.c - c) < (o.w + ww) * 0.5f + 0.2f) clash = true;
        if (!clash) ops.push_back({c, ww, false});
    }
    int winIndex = 0;
    const int segs = (int)run.pts.size() - 1;
    for (int si = 0; si < segs; ++si) {
        const Vec3 a = run.pts[si], b = run.pts[si + 1];
        const float len = run.cum[si + 1] - run.cum[si];
        if (len < 0.1f) continue;
        const Vec3 t = Vec3{b.x - a.x, 0, b.z - a.z} / len;
        const Vec3 n = Vec3::Cross(t, kUp);
        const bool extStart = si > 0, extEnd = si + 1 < segs;
        const float e0 = extStart ? T * 0.5f - 0.003f : 0.0f, e1 = extEnd ? T * 0.5f - 0.003f : 0.0f;
        std::vector<Opening> holes;
        std::vector<std::pair<Opening, bool>> fills;
        for (const Op& o : ops) {
            const float x0 = o.c - o.w * 0.5f - run.cum[si], x1 = o.c + o.w * 0.5f - run.cum[si];
            if (x0 < 0.1f || x1 > len - 0.1f) continue;
            Opening h = o.door ? Opening{x0 + e0, 0, x1 + e0, dh} : Opening{x0 + e0, sill, x1 + e0, sill + wh};
            holes.push_back(h);
            fills.push_back({h, o.door});
        }
        const std::string wid = "wall/" + std::to_string(si);
        PolyMesh panel = Panel(len + e0 + e1, height, T, holes);
        if (wallDesign == "banded") {
            for (int level = 1; level <= 1; ++level) {
                const float y = height - 0.25f;
                bool hit = false;
                for (const Opening& h : holes) if (h.y1 > y - 0.1f) hit = true;
                if (!hit) shapes::Append(panel, BoxAA({0.02f, y, -T / 2 - 0.03f}, {len + e0 + e1 - 0.02f, y + 0.12f, T / 2 + 0.03f}));
            }
        }
        if (shapes::Detail() >= 2) shapes::Append(panel, Courses(len + e0 + e1, height - 0.25f, T, holes, shapes::Detail() >= 3 ? 0.15f : 0.3f));
        if (shapes::Detail() >= 2) {   // High: a cornice along the top; Ultra: and a second ledge under it
            shapes::Append(panel, BoxAA({0.02f, height - 0.1f, -T / 2 - 0.03f}, {len + e0 + e1 - 0.02f, height - 0.0f, T / 2 + 0.03f}));
            if (shapes::Detail() >= 3) shapes::Append(panel, BoxAA({0.03f, height - 0.2f, -T / 2 - 0.02f}, {len + e0 + e1 - 0.03f, height - 0.14f, T / 2 + 0.02f}));
        }
        Place(panel, a - t * e0, t, n);
        out.push_back({wid, "wall", panel});
        if (Design(r, "coping", "coping/" + std::to_string(si)) == "slab") {
            PolyMesh cap = BoxAA({-0.03f - 0.0f, height - 0.004f, -T / 2 - 0.04f}, {len + e0 + e1 + 0.03f, height + 0.06f, T / 2 + 0.04f});
            Place(cap, a - t * e0, t, n);
            out.push_back({"coping/" + std::to_string(si), "coping", cap});
        }
        for (const auto& f : fills) {
            const Opening& h = f.first;
            const float ow = h.x1 - h.x0, oh = h.y1 - h.y0;
            const Vec3 origin = a - t * e0 + t * h.x0 + kUp * h.y0;
            const std::string id = f.second ? "door/" + std::to_string(si) : "window/" + std::to_string(winIndex++);
            const std::string slot = f.second ? "door" : "window";
            const std::string design = Design(r, slot, id);
            if (design == "none") continue;
            PolyMesh m;
            const float inset = 0.012f;
            if (f.second) {
                DoorSpec s; s.w = ow - 2 * inset; s.h = oh - inset; s.d = T * 0.6f; s.fj = 0.07f; s.lt = 0.045f; s.openDeg = 0; s.dbl = ow > 1.5f;
                s.frame = "plain"; s.leaf = design == "user" ? "flat" : design; s.handle = "lever";
                if (design.rfind("user:", 0) == 0) s.leaf = "flat";
                DoorParts p = BuildDoor(s);
                shapes::Append(m, p.frame);
                for (int i = 0; i < p.leaves; ++i) { shapes::Append(m, p.leaf[i]); shapes::Append(m, p.handle[i]); }
                if (design.rfind("user:", 0) == 0) m = Fill(r, design, LocalFrame(ow - 2 * inset, oh - inset, T * 0.6f), [&](const std::string&) { return PolyMesh(); });
            } else {
                WindowSpec s; s.w = ow - 2 * inset; s.h = oh - 2 * inset; s.d = T * 0.6f; s.fw = 0.06f;
                s.frame = "plain"; s.grid = design; s.glass = "clear"; s.sill = "none"; s.shutters = "none";
                if (design.rfind("user:", 0) == 0) s.grid = "single";
                WindowParts p = BuildWindow(s);
                shapes::Append(m, p.frame); shapes::Append(m, p.grid); shapes::Append(m, p.glass);
                if (design.rfind("user:", 0) == 0) m = Fill(r, design, LocalFrame(ow - 2 * inset, oh - 2 * inset, T * 0.6f), [&](const std::string&) { return PolyMesh(); });
            }
            for (Vec3& v : m.verts) { v.x += inset; v.y += f.second ? 0.0f : inset; }
            Place(m, origin, t, n);
            out.push_back({id, slot, m});
        }
    }
}

Template MakeWall() {
    Builder b;
    b.t.usesPath = true;
    b.t.id = "wall"; b.t.name = "Wall"; b.t.category = "Architecture"; b.t.version = 1;
    b.t.description = "A wall along a straight run or a path, with windows and a door cut into it and filled in. Corners are butt-joined (best at right angles).";
    b.P("length", "Length", Builder::K::Float, 6, 0.5, 100, "Size", "Length of a straight wall (ignored when you draw a path).");
    b.P("height", "Height", Builder::K::Float, 2.8, 0.4, 8, "Size", "Wall height.");
    b.P("thickness", "Thickness", Builder::K::Float, 0.25, 0.08, 1, "Size", "Wall thickness.");
    b.P("windows", "Windows", Builder::K::Int, 2, 0, 12, "Windows", "How many windows, spread evenly along the wall.");
    b.P("windowWidth", "Window width", Builder::K::Float, 1.0, 0.3, 3, "Windows", "Width of each window opening.");
    b.P("windowHeight", "Window height", Builder::K::Float, 1.3, 0.3, 3, "Windows", "Height of each window opening.");
    b.P("sillHeight", "Sill height", Builder::K::Float, 0.9, 0.1, 3, "Windows", "Height of the window above the ground.");
    b.P("door", "Door", Builder::K::Bool, 0, 0, 1, "Door", "Cut a door into the wall.");
    b.P("doorPos", "Door position", Builder::K::Float, 0.5, 0, 1, "Door", "Where along the wall the door is (0 = start, 1 = end).");
    b.P("doorWidth", "Door width", Builder::K::Float, 1.0, 0.6, 3, "Door", "Width of the door opening.");
    b.P("doorHeight", "Door height", Builder::K::Float, 2.1, 1.6, 3.5, "Door", "Height of the door opening.");
    b.S("wall", "Wall", {{"plain", "Plain"}, {"banded", "With a band under the top"}}, kPlaster, 0);
    b.S("coping", "Cap on top", {{"none", "None"}, {"slab", "Stone cap"}}, kStone, 1);
    b.S("window", "Windows", WindowGridDesigns(), kWhite, 2);
    b.S("door", "Door", DoorLeafDesigns(), kWood, 3);
    b.St("plaster", "Plaster wall with windows", "A house wall with two windows.", {{"windows", 2}}, {{"wall", "plain"}, {"window", "cross"}}, {});
    b.St("brick_door", "Brick wall with a door", "A brick wall with a panelled door and a window each side.", {{"windows", 2}, {"door", 1}},
         {{"wall", "banded"}, {"window", "casement"}, {"door", "panelled"}}, {{"wall", kBrick}});
    b.St("garden", "Garden wall", "A low stone wall with a cap and no openings.", {{"height", 1.0}, {"thickness", 0.3}, {"windows", 0}},
         {{"wall", "plain"}, {"coping", "slab"}}, {{"wall", kStone}, {"coping", kStone}});
    b.St("barn", "Wall with a plank door", "A tall wall with a plank door and colonial windows.", {{"height", 3.2}, {"windows", 2}, {"door", 1}, {"doorWidth", 1.4}},
         {{"wall", "plain"}, {"window", "colonial"}, {"door", "ledged"}}, {{"wall", kWood}});
    b.St("blank", "Blank wall", "A plain wall to start from.", {{"windows", 0}}, {{"wall", "plain"}}, {});
    b.t.generate = GenerateWall;
    return b.t;
}

// ============================== Stairs template =====================================================================
struct StairCtx {
    const Recipe& r;
    std::vector<GeneratedPart>& out;
    float sgn = 1;   // -1 mirrors z
    Vec3 M(Vec3 p) const { return {p.x, p.y, p.z * sgn}; }
    PolyMesh Box(Vec3 lo, Vec3 hi) const {
        const Vec3 a = M(lo), b = M(hi);
        return BoxAA({std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}, {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)});
    }
};

void GenerateStairs(const Recipe& r, std::vector<GeneratedPart>& out) {
    const int type = (int)r.Param("type");   // 0 straight, 1 L, 2 U, 3 spiral
    const int N = std::max(3, (int)r.Param("steps"));
    const float H = Pf(r, "totalHeight"), W = Pf(r, "width"), run = Pf(r, "treadDepth"), tt = Pf(r, "treadThickness");
    const float rise = H / (float)N;
    const std::string treadD = Design(r, "tread", "step/0"), stringerD = Design(r, "stringer", "stringer/0"), railD = Design(r, "rail", "rail/0");
    const bool railA = r.Param("railLeft") >= 0.5, railB = r.Param("railRight") >= 0.5;
    const float railH = Pf(r, "railHeight");
    StairCtx c{r, out, r.Param("turnLeft") >= 0.5 ? 1.0f : -1.0f};

    // A tread or landing: a block down to `base`, or a slab with a nosing.
    auto tread = [&](const std::string& id, Vec3 lo, Vec3 hi, float topY, float nose, bool noseFwd) {
        const std::string d = r.DesignFor("tread", id);
        PolyMesh m;
        if (d.rfind("user:", 0) == 0) {
            const Vec3 a = c.M({lo.x, topY, lo.z}), b = c.M({hi.x, topY, hi.z});
            Frame f; f.origin = {(a.x + b.x) * 0.5f, topY - tt, (a.z + b.z) * 0.5f}; f.t = {1, 0, 0}; f.u = {0, 1, 0}; f.n = {0, 0, 1};
            f.sx = std::fabs(b.x - a.x); f.sy = tt; f.sz = std::fabs(b.z - a.z);
            out.push_back({id, "tread", Fill(r, d, f, [&](const std::string&) { return PolyMesh(); })});
            return;
        }
        if (d == "slab") m = c.Box({lo.x, topY - tt, lo.z}, {hi.x + (noseFwd ? nose : 0), topY, hi.z});
        else m = c.Box({lo.x, lo.y, lo.z}, {hi.x, topY, hi.z});
        out.push_back({id, "tread", m});
    };
    auto railFor = [&](const std::string& prefix, std::vector<Vec3> tops, std::vector<Vec3> bases) {
        if (railD == "none" || tops.size() < 2) return;
        PolyMesh posts;
        for (size_t i = 0; i < tops.size(); ++i) {
            const Vec3 a = c.M(bases[i]), t = c.M(tops[i]);
            out.push_back({prefix + "/post/" + std::to_string(i), "rail", BoxAA({a.x - 0.025f, a.y - 0.03f, a.z - 0.025f}, {a.x + 0.025f, t.y, a.z + 0.025f})});
        }
        PolyMesh hand;
        for (size_t i = 0; i + 1 < tops.size(); ++i) shapes::Append(hand, Beam(c.M(tops[i]) + Vec3{0, 0.0f, 0}, c.M(tops[i + 1]), 0.06f, 0.05f));
        out.push_back({prefix + "/hand", "rail", hand});
        if (railD == "balusters") {
            PolyMesh bal;
            for (size_t i = 0; i + 1 < tops.size(); ++i) {
                const int nb = 3;
                for (int k = 1; k <= nb; ++k) {
                    const float f = (float)k / (float)(nb + 1);
                    const Vec3 tp = c.M(tops[i] + (tops[i + 1] - tops[i]) * f), bs = c.M(bases[i] + (bases[i + 1] - bases[i]) * f);
                    shapes::Append(bal, BoxAA({tp.x - 0.012f, bs.y - 0.03f, tp.z - 0.012f}, {tp.x + 0.012f, tp.y - 0.02f, tp.z + 0.012f}));
                }
            }
            out.push_back({prefix + "/balusters", "rail", bal});
        }
    };

    if (type == 3) {   // spiral
        const float R = std::max(W, 0.5f), rc = std::min(0.12f, R * 0.3f), total = Pf(r, "turn") * kPi / 180.0f;
        const float dth = total / (float)N;
        out.push_back({"column/0", "stringer", shapes::Prism([&] { Frame f; f.origin = {0, 0, 0}; f.t = {1, 0, 0}; f.u = {0, 1, 0}; f.n = {0, 0, 1}; f.sx = f.sz = rc * 2; f.sy = H; return f; }(), 12)});
        std::vector<Vec3> tops, bases;
        for (int i = 0; i < N; ++i) {
            const float a0 = dth * (float)i, a1 = a0 + dth * 1.12f;
            const float top = rise * (float)(i + 1);
            std::vector<Vec2> prof;
            const int seg = shapes::Scale(3, 1);
            for (int k = 0; k <= seg; ++k) { const float a = a0 + (a1 - a0) * (float)k / seg; prof.push_back({rc * 0.6f * std::cos(a), rc * 0.6f * std::sin(a)}); }
            for (int k = seg; k >= 0; --k) { const float a = a0 + (a1 - a0) * (float)k / seg; prof.push_back({R * std::cos(a), R * std::sin(a)}); }
            // profile order: inner arc forward then outer arc back is clockwise; reverse it so it runs counter-clockwise
            std::reverse(prof.begin(), prof.end());
            const float th = std::min(tt, rise * 0.9f);
            PolyMesh m = shapes::ExtrudeAxes({0, top - th * 0.5f, 0}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}, prof, th);
            for (Vec3& v : m.verts) v.z *= c.sgn;
            if (c.sgn < 0) for (PolyFace& f : m.faces) std::reverse(f.verts.begin(), f.verts.end());
            const std::string id = "step/" + std::to_string(i);
            if (r.DesignFor("tread", id).rfind("user:", 0) == 0) {
                // a user part replaces the wedge: fit to the wedge's bounding box
                Frame f; f.origin = {R * 0.5f * std::cos((a0 + a1) / 2), top - th, R * 0.5f * std::sin((a0 + a1) / 2) * c.sgn}; f.t = {1, 0, 0}; f.u = {0, 1, 0}; f.n = {0, 0, 1};
                f.sx = R; f.sy = th; f.sz = std::max(0.1f, R * 2 * std::sin(dth * 0.5f));
                m = Fill(r, r.DesignFor("tread", id), f, [&](const std::string&) { return PolyMesh(); });
            }
            out.push_back({id, "tread", m});
            const float am = (a0 + a1) * 0.5f;
            tops.push_back({(R - 0.05f) * std::cos(am), top + railH, (R - 0.05f) * std::sin(am)});
            bases.push_back({(R - 0.05f) * std::cos(am), top, (R - 0.05f) * std::sin(am)});
        }
        if (railA) railFor("rail/a", tops, bases);
        return;
    }

    // straight, L and U: flights along +x, with a landing and a second flight for L and U
    const int n1 = type == 0 ? N : std::max(1, (N - 1) / 2);
    const int landingIdx = type == 0 ? -1 : n1;
    const int n2 = type == 0 ? 0 : N - n1 - 1;
    const float gap = std::max(0.02f, Pf(r, "gap"));
    std::vector<Vec3> railTopsA, railBasesA, railTopsB, railBasesB;   // rail points per flight side
    // flight 1
    for (int i = 0; i < n1; ++i) {
        const float x0 = run * (float)i, top = rise * (float)(i + 1);
        const bool last = i + 1 == n1 && type == 0;
        const float ov = last ? 0.0f : 0.012f;
        tread("step/" + std::to_string(i), {x0, 0, 0}, {x0 + run + ov, top, W}, top, 0.03f, last || true);
        if (i % std::max(1, (int)r.Param("postEvery")) == 0 || i + 1 == n1) {
            railTopsA.push_back({x0 + run * 0.5f, top + railH, 0.04f}); railBasesA.push_back({x0 + run * 0.5f, top, 0.04f});
            railTopsB.push_back({x0 + run * 0.5f, top + railH, W - 0.04f}); railBasesB.push_back({x0 + run * 0.5f, top, W - 0.04f});
        }
    }
    auto stringer = [&](const std::string& id, Vec3 p0, Vec3 p1, float z0, float z1) {
        if (stringerD == "none") return;
        const Vec3 a = c.M({p0.x, p0.y, z0}), b = c.M({p1.x, p1.y, z0});
        const float x0 = a.x, x1 = b.x;
        const float zlo = std::min(c.M({0, 0, z0}).z, c.M({0, 0, z1}).z), zhi = std::max(c.M({0, 0, z0}).z, c.M({0, 0, z1}).z);
        const float L = x1 - x0, Hh = p1.y - p0.y, th = 0.22f;
        const std::vector<Vec2> prof = {{0, 0}, {L, Hh}, {L, Hh + th}, {0, th}};
        PolyMesh m = shapes::ExtrudeAxes({x0, p0.y - 0.0f, (zlo + zhi) * 0.5f}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, prof, zhi - zlo);
        out.push_back({id, "stringer", m});
    };
    stringer("stringer/0", {0, 0, 0}, {run * (float)n1, rise * (float)n1 - 0.0f, 0}, -0.04f, 0.012f);
    stringer("stringer/1", {0, 0, 0}, {run * (float)n1, rise * (float)n1, 0}, W - 0.012f, W + 0.04f);
    if (type == 0) {
        if (railA) railFor("rail/a", railTopsA, railBasesA);
        if (railB) railFor("rail/b", railTopsB, railBasesB);
        return;
    }
    // landing
    const float lx0 = run * (float)n1, ltop = rise * (float)(n1 + 1);
    const float lz1 = type == 1 ? W + 0.012f : 2 * W + gap;
    tread("landing/0", {lx0, 0, 0}, {lx0 + W + (type == 2 ? 0.012f : 0.0f), ltop, lz1}, ltop, 0.0f, false);
    // flight 2
    std::vector<Vec3> tA, bA, tB, bB;
    for (int k = 0; k < n2; ++k) {
        const int idx = n1 + 1 + k;
        const float top = rise * (float)(idx + 1);
        if (type == 1) {   // along +z from the landing
            const float z0 = W + run * (float)k;
            tread("step/" + std::to_string(idx), {lx0, 0, z0}, {lx0 + W, top, z0 + run + (k + 1 == n2 ? 0.0f : 0.012f)}, top, 0.03f, false);
            tA.push_back({lx0 + 0.04f, top + railH, z0 + run * 0.5f}); bA.push_back({lx0 + 0.04f, top, z0 + run * 0.5f});
            tB.push_back({lx0 + W - 0.04f, top + railH, z0 + run * 0.5f}); bB.push_back({lx0 + W - 0.04f, top, z0 + run * 0.5f});
        } else {           // back along -x on the far side
            const float x1 = lx0 + W - run * (float)k;
            tread("step/" + std::to_string(idx), {x1 - run - (k + 1 == n2 ? 0.0f : 0.012f), 0, W + gap}, {x1, top, 2 * W + gap}, top, 0.03f, false);
            tA.push_back({x1 - run * 0.5f, top + railH, W + gap + 0.04f}); bA.push_back({x1 - run * 0.5f, top, W + gap + 0.04f});
            tB.push_back({x1 - run * 0.5f, top + railH, 2 * W + gap - 0.04f}); bB.push_back({x1 - run * 0.5f, top, 2 * W + gap - 0.04f});
        }
    }
    (void)landingIdx;
    if (railA) { railFor("rail/a", railTopsA, railBasesA); railFor("rail/c", tA, bA); }
    if (railB) { railFor("rail/b", railTopsB, railBasesB); railFor("rail/d", tB, bB); }
}

Template MakeStairs() {
    Builder b;
    b.t.id = "stairs"; b.t.name = "Stairs"; b.t.category = "Architecture"; b.t.version = 1;
    b.t.description = "Straight, L-shaped, U-shaped or spiral stairs with side boards and railings. Landing edges have no railing.";
    b.Choice("type", "Shape", {"Straight", "L-shaped", "U-shaped", "Spiral"}, 0, "Size", "The layout of the stairs.");
    b.P("steps", "Steps", Builder::K::Int, 12, 3, 40, "Size", "How many steps (a landing counts as one).");
    b.P("totalHeight", "Total height", Builder::K::Float, 2.8, 0.5, 8, "Size", "Height from the floor to the top.");
    b.P("width", "Width (spiral: radius)", Builder::K::Float, 1.0, 0.5, 3, "Size", "How wide the stairs are.");
    b.P("treadDepth", "Step depth", Builder::K::Float, 0.28, 0.15, 0.6, "Size", "Depth of a step.");
    b.P("treadThickness", "Step thickness", Builder::K::Float, 0.05, 0.02, 0.3, "Size", "Thickness of a slab step.");
    b.P("turn", "Spiral turn (degrees)", Builder::K::Float, 300, 90, 720, "Size", "How far a spiral turns in total.");
    b.P("turnLeft", "Turn left", Builder::K::Bool, 1, 0, 1, "Size", "L and U stairs and spirals turn left or right.");
    b.P("gap", "U-turn gap", Builder::K::Float, 0.1, 0.02, 1, "Size", "Space between the two flights of a U.");
    b.P("railLeft", "Rail on the first side", Builder::K::Bool, 1, 0, 1, "Railing", "A railing along one side.");
    b.P("railRight", "Rail on the other side", Builder::K::Bool, 1, 0, 1, "Railing", "A railing along the other side (not for spirals).");
    b.P("railHeight", "Railing height", Builder::K::Float, 0.9, 0.5, 1.5, "Railing", "Height of the handrail above the steps.");
    b.P("postEvery", "Post every N steps", Builder::K::Int, 3, 1, 8, "Railing", "How often a railing post stands.");
    b.S("tread", "Steps", {{"block", "Solid blocks"}, {"slab", "Slabs"}}, kWood, 0);
    b.S("stringer", "Side boards", {{"side", "Side boards"}, {"none", "None"}}, kWood, 1);
    b.S("rail", "Railing", {{"none", "None"}, {"post_rail", "Posts and handrail"}, {"balusters", "With balusters"}}, kDark, 2);
    b.St("straight", "Straight stairs", "A straight flight with a railing.", {{"type", 0}, {"steps", 12}}, {{"tread", "block"}, {"stringer", "side"}, {"rail", "post_rail"}}, {});
    b.St("open_slab", "Open slab stairs", "Floating slabs between two side boards, with balusters.", {{"type", 0}, {"steps", 14}, {"treadThickness", 0.06}},
         {{"tread", "slab"}, {"stringer", "side"}, {"rail", "balusters"}}, {{"tread", kPlaster}});
    b.St("l_shaped", "L-shaped stairs", "A flight, a landing and a quarter turn.", {{"type", 1}, {"steps", 13}}, {{"tread", "block"}, {"stringer", "none"}, {"rail", "post_rail"}}, {});
    b.St("u_shaped", "U-shaped stairs", "Two flights and a landing, turning back on themselves.", {{"type", 2}, {"steps", 17}, {"totalHeight", 3.4}},
         {{"tread", "slab"}, {"stringer", "none"}, {"rail", "post_rail"}}, {});
    b.St("spiral", "Spiral stairs", "Wedge steps around a column.", {{"type", 3}, {"steps", 16}, {"width", 0.9}, {"railRight", 0}},
         {{"tread", "slab"}, {"stringer", "none"}, {"rail", "post_rail"}}, {{"stringer", kMetal}});
    b.t.generate = GenerateStairs;
    return b.t;
}

// ============================== Building template ===================================================================
void GenerateBuilding(const Recipe& r, std::vector<GeneratedPart>& out) {
    const float W = Pf(r, "width"), D = Pf(r, "depth"), FH = Pf(r, "floorHeight"), T = std::min(Pf(r, "wallThickness"), std::min(W, D) * 0.3f);
    const int floors = std::max(1, (int)r.Param("floors"));
    const int nFront = (int)r.Param("windowsFront"), nSide = (int)r.Param("windowsSide");
    const float ww = Pf(r, "windowWidth"), wh = std::min(Pf(r, "windowHeight"), FH - 0.6f);
    const float sill = std::min(Pf(r, "sillHeight"), std::max(0.1f, FH - wh - 0.35f));
    const float dw = Pf(r, "doorWidth"), dh = std::min(Pf(r, "doorHeight"), FH - 0.25f);
    const float found = Pf(r, "foundation"), eps = 0.004f;
    const std::string wallD = Design(r, "wall", "wall/0/0/0");
    out.push_back({"foundation/0", "foundation", BoxAA({-W / 2 - 0.08f, -found, -D / 2 - 0.08f}, {W / 2 + 0.08f, 0.02f, D / 2 + 0.08f})});
    // sides: 0 front (+z), 1 back (-z), 2 right (+x), 3 left (-x); each described by origin, direction along the wall and its length
    struct Side { Vec3 o, t; float len; int count; };
    const Side sides[4] = {
        {{-W / 2, 0, D / 2 - T / 2}, {1, 0, 0}, W, nFront},          // front: outside faces +z
        {{W / 2, 0, -D / 2 + T / 2}, {-1, 0, 0}, W, nFront},         // back
        {{W / 2 - T / 2, 0, D / 2 - T + eps * 0}, {0, 0, -1}, D, nSide},   // right (inner span only, set below)
        {{-W / 2 + T / 2, 0, -D / 2 + T}, {0, 0, 1}, D, nSide},      // left
    };
    for (int f = 0; f < floors; ++f) {
        const float y0 = FH * (float)f;
        for (int s = 0; s < 4; ++s) {
            Side sd = sides[s];
            float len = sd.len;
            Vec3 o = sd.o;
            if (s >= 2) { len = D - 2 * T + 2 * 0.004f; o = s == 2 ? Vec3{W / 2 - T / 2, 0, D / 2 - T + 0.004f} : Vec3{-W / 2 + T / 2, 0, -D / 2 + T - 0.004f}; }
            o.y = y0;
            const Vec3 t = sd.t;
            const Vec3 n = Vec3::Cross(t, kUp);
            std::vector<Opening> holes;
            std::vector<std::pair<Opening, int>> fills;   // opening, 0 window / 1 door
            const float margin = 0.35f;
            std::vector<float> doorX;
            if (f == 0 && s == 0) {
                const float cx = len * 0.5f;
                Opening h{cx - dw / 2, 0, cx + dw / 2, dh};
                holes.push_back(h); fills.push_back({h, 1});
            }
            const bool sideWall = s >= 2;
            const float inner = len;
            for (int k = 0; k < sd.count; ++k) {
                const float cx = inner * (float)(k + 1) / (float)(sd.count + 1);
                Opening h{cx - ww / 2, sill, cx + ww / 2, sill + wh};
                if (h.x0 < margin || h.x1 > inner - margin) continue;
                bool clash = false;
                for (const auto& o2 : fills) if (o2.second == 1 && h.x1 > o2.first.x0 - 0.3f && h.x0 < o2.first.x1 + 0.3f) clash = true;
                if (clash) continue;
                holes.push_back(h); fills.push_back({h, 0});
            }
            (void)sideWall;
            const std::string idSuffix = std::to_string(f) + "/" + std::to_string(s);
            PolyMesh panel = Panel(len, FH + 0.004f, T, holes);
            if (wallD == "banded" && f + 1 < floors) {
                bool hit = false;
                for (const Opening& h : holes) if (h.y1 > FH - 0.4f) hit = true;
                if (!hit) shapes::Append(panel, BoxAA({0.02f, FH - 0.16f, -T / 2 - 0.03f}, {len - 0.02f, FH - 0.04f, T / 2 + 0.03f}));
            }
            if (shapes::Detail() >= 2) shapes::Append(panel, Courses(len, FH - 0.25f, T, holes, shapes::Detail() >= 3 ? 0.15f : 0.3f));
            if (shapes::Detail() >= 2 && f + 1 == floors) {   // cornice under the roof
                shapes::Append(panel, BoxAA({0.02f, FH - 0.1f, -T / 2 - 0.03f}, {len - 0.02f, FH - 0.0f, T / 2 + 0.03f}));
                if (shapes::Detail() >= 3) shapes::Append(panel, BoxAA({0.03f, FH - 0.2f, -T / 2 - 0.02f}, {len - 0.03f, FH - 0.14f, T / 2 + 0.02f}));
            }
            Place(panel, o, t, n);
            out.push_back({"wall/" + idSuffix + "/0", "wall", panel});
            int wi = 0;
            for (const auto& fl : fills) {
                const Opening& h = fl.first;
                const float ow = h.x1 - h.x0, oh = h.y1 - h.y0;
                const std::string id = (fl.second ? "door/" : "window/") + idSuffix + "/" + std::to_string(wi++);
                const std::string slot = fl.second ? "door" : "window";
                const std::string design = Design(r, slot, id);
                if (design == "none") continue;
                PolyMesh m;
                const float inset = 0.012f;
                if (fl.second) {
                    DoorSpec ds; ds.w = ow - 2 * inset; ds.h = oh - inset; ds.d = T * 0.6f; ds.fj = 0.07f; ds.lt = 0.045f; ds.openDeg = Pf(r, "doorOpen"); ds.dbl = ow > 1.5f;
                    ds.frame = "plain"; ds.leaf = design.rfind("user:", 0) == 0 ? "flat" : design; ds.handle = "lever";
                    DoorParts p = BuildDoor(ds);
                    shapes::Append(m, p.frame);
                    for (int i = 0; i < p.leaves; ++i) { shapes::Append(m, p.leaf[i]); shapes::Append(m, p.handle[i]); }
                    if (design.rfind("user:", 0) == 0) m = Fill(r, design, LocalFrame(ow - 2 * inset, oh - inset, T * 0.6f), [&](const std::string&) { return PolyMesh(); });
                } else {
                    WindowSpec ws; ws.w = ow - 2 * inset; ws.h = oh - 2 * inset; ws.d = T * 0.6f; ws.fw = 0.06f;
                    ws.frame = "plain"; ws.grid = design.rfind("user:", 0) == 0 ? "single" : design; ws.glass = "clear"; ws.sill = "slab"; ws.shutters = "none";
                    WindowParts p = BuildWindow(ws);
                    shapes::Append(m, p.frame); shapes::Append(m, p.grid); shapes::Append(m, p.glass); shapes::Append(m, p.sill);
                    if (shapes::Detail() >= 2) {   // trim round the window on the outside face (High and up)
                        const float w2 = ws.w, h2 = ws.h, zt = T / 2 - ws.d * 0.0f;
                        shapes::Append(m, BoxAA({-0.07f, h2, zt - 0.008f}, {w2 + 0.07f, h2 + 0.09f, zt + 0.035f}));
                        shapes::Append(m, BoxAA({-0.07f, -0.02f, zt - 0.008f}, {0.0f - 0.004f, h2 + 0.01f, zt + 0.03f}));
                        shapes::Append(m, BoxAA({w2 + 0.004f, -0.02f, zt - 0.008f}, {w2 + 0.07f, h2 + 0.01f, zt + 0.03f}));
                    }
                    if (design.rfind("user:", 0) == 0) m = Fill(r, design, LocalFrame(ow - 2 * inset, oh - 2 * inset, T * 0.6f), [&](const std::string&) { return PolyMesh(); });
                }
                for (Vec3& v : m.verts) { v.x += h.x0 + inset; v.y += h.y0 + (fl.second ? 0.0f : inset); }
                Place(m, o, t, n);
                out.push_back({id, slot, m});
            }
        }
        if (f > 0) out.push_back({"floor/" + std::to_string(f), "floor", BoxAA({-W / 2 + T + 0.01f, y0 - 0.15f, -D / 2 + T + 0.01f}, {W / 2 - T - 0.01f, y0, D / 2 - T - 0.01f})});
    }
    // roof: ridge along x (or z when the building is turned), sitting on the top floor
    const std::string roofD = Design(r, "roof", "roof/0");
    const bool alongZ = r.Param("ridgeAlong") >= 0.5;
    const float topY = FH * (float)floors;
    const float rw = alongZ ? D : W, rd = alongZ ? W : D;
    const std::string rtype = roofD.rfind("user:", 0) == 0 ? "gable" : roofD;
    PolyMesh roof = BuildRoof(rtype, rw, rd, Pf(r, "roofPitch"), Pf(r, "overhang"), Pf(r, "roofThickness"));
    PolyMesh trim;
    if (Design(r, "trim", "trim/0") == "edge") trim = RoofTrim(rtype, rw, rd, Pf(r, "roofPitch"), Pf(r, "overhang"), Pf(r, "roofThickness"));
    if (roofD.rfind("user:", 0) == 0) {
        Frame f = LocalFrame(rw + 2 * Pf(r, "overhang"), std::max(0.3f, std::tan(Pf(r, "roofPitch") * kPi / 180.0f) * (rd * 0.5f + Pf(r, "overhang"))), rd + 2 * Pf(r, "overhang"));
        f.origin = {0, 0, 0};
        auto it = r.userParts.find(std::string(roofD, 5));
        if (it != r.userParts.end()) roof = FitUserPart(it->second, f);
    }
    for (PolyMesh* m : {&roof, &trim}) {
        if (alongZ) for (Vec3& v : m->verts) { const float x = v.x; v.x = v.z; v.z = -x; }   // turn a quarter
        for (Vec3& v : m->verts) v.y += topY;
        if (alongZ) for (PolyFace& f : m->faces) { /* a pure rotation keeps winding */ (void)f; }
    }
    out.push_back({"roof/0", "roof", roof});
    out.push_back({"trim/0", "trim", trim});
    if (shapes::DetailF() >= 1.99f && Design(r, "tiles", "tiles/0") == "fine" && roofD.rfind("user:", 0) != 0) {
        int tk = 0;
        for (PolyMesh& tiles : RoofTiles(rtype, rw, rd, Pf(r, "roofPitch"), Pf(r, "overhang"), Pf(r, "roofThickness"))) {
            if (alongZ) for (Vec3& v : tiles.verts) { const float x = v.x; v.x = v.z; v.z = -x; }
            for (Vec3& v : tiles.verts) v.y += topY;
            out.push_back({"tiles/" + std::to_string(tk++), "tiles", std::move(tiles)});
        }
        // a down pipe from the gutter to the ground at the front right corner
        const float pipeX = W / 2 - 0.01f, pipeZ = D / 2 + 0.03f;
        out.push_back({"downpipe/0", "tiles", RoofRod({pipeX, topY - 0.05f, pipeZ}, {pipeX, 0.05f, pipeZ}, 0.035f, true, 8)});
    }
}

Template MakeBuilding() {
    Builder b;
    b.t.id = "building"; b.t.name = "Building"; b.t.category = "Architecture"; b.t.version = 1;
    b.t.description = "A simple building made of walls, windows, a door and a roof. Every window, the door and the roof are parts you can swap for another design.";
    b.P("width", "Width (x)", Builder::K::Float, 8, 3, 40, "Size", "Outside width.");
    b.P("depth", "Depth (z)", Builder::K::Float, 6, 3, 40, "Size", "Outside depth.");
    b.P("floors", "Floors", Builder::K::Int, 2, 1, 6, "Size", "How many floors.");
    b.P("floorHeight", "Floor height", Builder::K::Float, 2.8, 2.2, 5, "Size", "Height of one floor.");
    b.P("wallThickness", "Wall thickness", Builder::K::Float, 0.25, 0.1, 0.8, "Size", "Wall thickness.");
    b.P("foundation", "Foundation depth", Builder::K::Float, 0.3, 0.05, 2, "Size", "How far the base goes below the ground.");
    b.P("windowsFront", "Windows (front, back)", Builder::K::Int, 3, 0, 8, "Windows", "Windows per floor on the long walls.");
    b.P("windowsSide", "Windows (sides)", Builder::K::Int, 2, 0, 8, "Windows", "Windows per floor on the short walls.");
    b.P("windowWidth", "Window width", Builder::K::Float, 1.0, 0.4, 2.5, "Windows", "Width of a window.");
    b.P("windowHeight", "Window height", Builder::K::Float, 1.3, 0.4, 2.5, "Windows", "Height of a window.");
    b.P("sillHeight", "Sill height", Builder::K::Float, 0.9, 0.1, 2, "Windows", "Height of a window above its floor.");
    b.P("doorWidth", "Door width", Builder::K::Float, 1.1, 0.7, 3, "Door", "Width of the front door.");
    b.P("doorHeight", "Door height", Builder::K::Float, 2.1, 1.7, 3.5, "Door", "Height of the front door.");
    b.P("doorOpen", "Door open (degrees)", Builder::K::Float, 0, 0, 100, "Door", "How far the front door stands open.");
    b.P("roofPitch", "Roof pitch (degrees)", Builder::K::Float, 30, 5, 65, "Roof", "How steep the roof is.");
    b.P("overhang", "Roof overhang", Builder::K::Float, 0.4, 0, 2, "Roof", "How far the roof reaches past the walls.");
    b.P("roofThickness", "Flat roof thickness", Builder::K::Float, 0.2, 0.05, 0.6, "Roof", "Thickness of a flat roof or the low edge of a shed roof.");
    b.P("ridgeAlong", "Ridge along the depth", Builder::K::Bool, 0, 0, 1, "Roof", "Turn the roof a quarter so the ridge runs front to back.");
    b.S("wall", "Walls", {{"plain", "Plain"}, {"banded", "With a band between floors"}}, kPlaster, 0);
    b.S("window", "Windows", WindowGridDesigns(), kWhite, 1);
    b.S("door", "Front door", DoorLeafDesigns(), kWood, 2);
    b.S("roof", "Roof", RoofDesigns(), kRoofRed, 3);
    b.S("trim", "Roof trim", {{"none", "None"}, {"edge", "Ridge cap / parapet / fascia"}}, kSlate, 4);
    b.S("tiles", "Tiles and gutters", {{"fine", "Rows of tiles, gutters and a down pipe (High and up)"}, {"none", "None"}}, kSlate, 6);
    b.S("foundation", "Foundation", {{"slab", "Stone base"}}, kStone, 5);
    b.S("floor", "Floors", {{"slab", "Floor slabs"}}, kWood, 6);
    b.St("cottage", "Cottage", "One floor, a gable roof, a panelled door.", {{"floors", 1}, {"windowsFront", 2}, {"windowsSide", 1}, {"width", 7}, {"depth", 5}},
         {{"wall", "plain"}, {"window", "cross"}, {"door", "panelled"}, {"roof", "gable"}, {"trim", "edge"}}, {});
    b.St("townhouse", "Townhouse", "Two floors, a hip roof, tall windows.", {{"floors", 2}, {"windowsFront", 3}, {"windowsSide", 2}},
         {{"wall", "banded"}, {"window", "tall"}, {"door", "glazed"}, {"roof", "hip"}, {"trim", "edge"}}, {{"wall", kBrick}});
    b.St("barn", "Barn", "A tall plank-walled building with a big door and a shallow roof.", {{"floors", 1}, {"floorHeight", 4.2}, {"width", 10}, {"depth", 7}, {"windowsFront", 1}, {"windowsSide", 2},
          {"doorWidth", 2.4}, {"doorHeight", 3.2}, {"roofPitch", 24}, {"ridgeAlong", 0}},
         {{"wall", "plain"}, {"window", "colonial"}, {"door", "ledged"}, {"roof", "gable"}, {"trim", "edge"}}, {{"wall", kWood}, {"door", kDark}});
    b.St("shed", "Shed", "A small hut with a single sloping roof.", {{"floors", 1}, {"width", 4}, {"depth", 3}, {"windowsFront", 1}, {"windowsSide", 0}, {"roofPitch", 12}, {"doorWidth", 0.9}},
         {{"wall", "plain"}, {"window", "single"}, {"door", "ledged"}, {"roof", "shed"}, {"trim", "edge"}}, {{"wall", kWood}});
    b.St("flat_block", "Flat-roofed block", "Three floors, a flat roof with a parapet.", {{"floors", 3}, {"windowsFront", 4}, {"windowsSide", 2}, {"width", 10}, {"depth", 7}, {"overhang", 0.1}, {"roofThickness", 0.25}},
         {{"wall", "plain"}, {"window", "casement"}, {"door", "glazed"}, {"roof", "flat"}, {"trim", "edge"}}, {{"wall", kStone}});
    b.t.noSubdivisionSlots = {"roof"};   // a roof is flat planes meeting at hard edges; subdividing would round it into a dome
    b.t.generate = GenerateBuilding;
    return b.t;
}

} // namespace

void RegisterArchitectureTemplates() {
    Registry& r = Registry::Instance();
    r.Register(MakeWall());
    r.Register(MakeStairs());
    r.Register(MakeDoor());
    r.Register(MakeWindow());
    r.Register(MakeRoof());
    r.Register(MakeBuilding());
}

} // namespace okay::modeling::guided
