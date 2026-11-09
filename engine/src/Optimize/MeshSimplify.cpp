// Quadric error metric edge-collapse simplifier. See MeshSimplify.hpp.
#include "okay/Optimize/MeshSimplify.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <map>
#include <queue>
#include <unordered_map>

namespace okay::optimize {
namespace {

// ---------------------------------------------------------------- small math
struct D3 { double x = 0, y = 0, z = 0; };
inline D3 operator+(const D3& a, const D3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline D3 operator-(const D3& a, const D3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline D3 operator*(const D3& a, double s)    { return {a.x * s, a.y * s, a.z * s}; }
inline double Dot(const D3& a, const D3& b)   { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline D3 Cross(const D3& a, const D3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double Len2(const D3& a) { return Dot(a, a); }
inline double Len(const D3& a)  { return std::sqrt(Dot(a, a)); }
inline D3 FromVec(const Vec3& v) { return {v.x, v.y, v.z}; }

/// Symmetric 4x4 error quadric: c = {a2 ab ac ad b2 bc bd c2 cd d2}.
struct Quadric {
    double c[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    void AddPlane(double a, double b, double cc, double d, double w) {
        c[0] += w * a * a; c[1] += w * a * b; c[2] += w * a * cc; c[3] += w * a * d;
        c[4] += w * b * b; c[5] += w * b * cc; c[6] += w * b * d;
        c[7] += w * cc * cc; c[8] += w * cc * d; c[9] += w * d * d;
    }
    Quadric& operator+=(const Quadric& o) { for (int i = 0; i < 10; ++i) c[i] += o.c[i]; return *this; }
    Quadric operator+(const Quadric& o) const { Quadric r = *this; r += o; return r; }
    double Eval(const D3& p) const {
        const double e = c[0] * p.x * p.x + 2 * c[1] * p.x * p.y + 2 * c[2] * p.x * p.z + 2 * c[3] * p.x +
                         c[4] * p.y * p.y + 2 * c[5] * p.y * p.z + 2 * c[6] * p.y +
                         c[7] * p.z * p.z + 2 * c[8] * p.z + c[9];
        return e < 0 ? 0 : e;
    }
    /// The point minimising the error; false when the system is (nearly) singular.
    bool Optimum(D3& out) const {
        const double a = c[0], b = c[1], cc = c[2], d = c[4], e = c[5], f = c[7];
        const double det = a * (d * f - e * e) - b * (b * f - e * cc) + cc * (b * e - d * cc);
        const double tr = a + d + f;
        if (tr <= 0 || std::fabs(det) < 1e-9 * tr * tr * tr) return false;
        const double r0 = -c[3], r1 = -c[6], r2 = -c[8];
        const double inv = 1.0 / det;
        out.x = (r0 * (d * f - e * e) - b * (r1 * f - e * r2) + cc * (r1 * e - d * r2)) * inv;
        out.y = (a * (r1 * f - e * r2) - r0 * (b * f - e * cc) + cc * (b * r2 - r1 * cc)) * inv;
        out.z = (a * (d * r2 - r1 * e) - b * (b * r2 - r1 * cc) + r0 * (b * e - d * cc)) * inv;
        return true;
    }
};

/// Barycentric coordinates of the point on triangle abc closest to p (Ericson).
void ClosestBary(const D3& p, const D3& a, const D3& b, const D3& c, double& u, double& v, double& w, double& dist2) {
    const D3 ab = b - a, ac = c - a, ap = p - a;
    const double d1 = Dot(ab, ap), d2 = Dot(ac, ap);
    auto done = [&](double bu, double bv, double bw) {
        u = bu; v = bv; w = bw;
        const D3 q = a * bu + b * bv + c * bw;
        dist2 = Len2(p - q);
    };
    if (d1 <= 0 && d2 <= 0) return done(1, 0, 0);
    const D3 bp = p - b;
    const double d3 = Dot(ab, bp), d4 = Dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return done(0, 1, 0);
    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) { const double t = d1 / (d1 - d3); return done(1 - t, t, 0); }
    const D3 cp = p - c;
    const double d5 = Dot(ab, cp), d6 = Dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return done(0, 0, 1);
    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) { const double t = d2 / (d2 - d6); return done(1 - t, 0, t); }
    const double va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
        const double t = (d4 - d3) / ((d4 - d3) + (d5 - d6)); return done(0, 1 - t, t);
    }
    const double den = 1.0 / (va + vb + vc);
    return done(1 - (vb + vc) * den, vb * den, vc * den);
}

// ---------------------------------------------------------------- mesh state
struct Wedge {
    int  pos = 0;
    Vec2 uv{0, 0};
    Vec3 n{0, 0, 0};
    std::array<int, 4>   sj{{0, 0, 0, 0}};
    std::array<float, 4> sw{{0, 0, 0, 0}};
};

struct Tri {
    int  v[3] = {0, 0, 0};
    int  w[3] = {0, 0, 0};
    int  src = 0;            // index of the source triangle (for per-triangle colours)
    bool alive = true;
    int  Corner(int vert) const { return v[0] == vert ? 0 : (v[1] == vert ? 1 : (v[2] == vert ? 2 : -1)); }
};

struct QKey {
    long long x, y, z;
    bool operator==(const QKey& o) const { return x == o.x && y == o.y && z == o.z; }
};
struct QKeyHash {
    std::size_t operator()(const QKey& k) const {
        std::uint64_t h = 1469598103934665603ull;
        for (long long v : {k.x, k.y, k.z}) { h ^= static_cast<std::uint64_t>(v); h *= 1099511628211ull; h ^= h >> 29; }
        return static_cast<std::size_t>(h);
    }
};

inline std::uint64_t EdgeKey(int a, int b) {
    if (a > b) std::swap(a, b);
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(a)) << 32) | static_cast<std::uint32_t>(b);
}

float SkinDiff(const std::array<int, 4>& ja, const std::array<float, 4>& wa,
               const std::array<int, 4>& jb, const std::array<float, 4>& wb) {
    float worst = 0.0f;
    auto weightOf = [](const std::array<int, 4>& j, const std::array<float, 4>& w, int joint) {
        float s = 0.0f;
        for (int i = 0; i < 4; ++i) if (j[i] == joint && w[i] > 0.0f) s += w[i];
        return s;
    };
    for (int i = 0; i < 4; ++i) {
        if (wa[i] > 0.0f) worst = std::max(worst, std::fabs(wa[i] - weightOf(jb, wb, ja[i])));
        if (wb[i] > 0.0f) worst = std::max(worst, std::fabs(wb[i] - weightOf(ja, wa, jb[i])));
    }
    return worst;
}

struct Candidate {
    bool   valid = false;
    int    from = -1, to = -1;
    D3     pos;
    bool   optimal = false;
    double cost = 0, dist2 = 0, area = 1;
    int    removed = 0;
};

struct HeapEntry {
    double cost;
    int a, b;
    unsigned sa, sb;
};
struct HeapGreater {
    bool operator()(const HeapEntry& x, const HeapEntry& y) const {
        if (x.cost != y.cost) return x.cost > y.cost;
        if (x.a != y.a) return x.a > y.a;
        return x.b > y.b;
    }
};

class Simplifier {
public:
    Simplifier(const Mesh& src, const SimplifyOptions& o, const SkinData* skin) : opt_(o), skin_(skin) { Build(src); }

    bool Closed() const { return closed_; }
    int  AliveTriangles() const { return alive_; }
    int  InitialTriangles() const { return initialTris_; }

    void Run(int targetTris) {
        const int floorTris = std::max(opt_.minTriangles, closed_ ? 4 : 1);
        std::priority_queue<HeapEntry, std::vector<HeapEntry>, HeapGreater> heap;
        pushAll(heap);
        while (alive_ > targetTris && !heap.empty()) {
            const HeapEntry e = heap.top(); heap.pop();
            if (!pAlive_[e.a] || !pAlive_[e.b] || stamp_[e.a] != e.sa || stamp_[e.b] != e.sb) continue;
            Candidate c = Evaluate(e.a, e.b);
            if (!c.valid) continue;
            if (c.cost > e.cost * (1.0 + 1e-9) + 1e-18) {   // stale (cheaper direction no longer legal)
                heap.push({c.cost, e.a, e.b, stamp_[e.a], stamp_[e.b]});
                continue;
            }
            if (alive_ - c.removed < floorTris) continue;
            const double err = std::sqrt(c.dist2);
            if (opt_.maxError >= 0.0f && err > static_cast<double>(opt_.maxError)) continue;
            Apply(c);
            ++collapses_;
            maxErr_ = std::max(maxErr_, err);
            const int to = c.to;
            std::vector<int> nb;
            Neighbors(to, nb);
            for (int n : nb) pushEdge(heap, std::min(to, n), std::max(to, n));
        }
    }

    void Emit(const Mesh& src, SimplifyResult& r) const {
        Mesh out;
        out.name = src.name;
        out.autoSmoothAngle = src.autoSmoothAngle;
        const bool colours = src.HasFaceColors();
        std::vector<int> remap(wedges_.size(), -1);
        const bool hasSkin = skin_ != nullptr;
        for (const Tri& t : tris_) {
            if (!t.alive) continue;
            for (int k = 0; k < 3; ++k) {
                const int wid = t.w[k];
                if (remap[wid] < 0) {
                    remap[wid] = static_cast<int>(out.vertices.size());
                    const Wedge& w = wedges_[wid];
                    const D3& p = P_[w.pos];
                    out.vertices.push_back({static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)});
                    if (hasUV_) out.uvs.push_back(w.uv);
                    if (hasNormals_) {
                        Vec3 n = w.n;
                        const float m = n.Magnitude();
                        out.normals.push_back(m > 1e-8f ? n * (1.0f / m) : Vec3{0, 1, 0});
                    }
                    if (hasSkin) { r.skin.joints.push_back(w.sj); r.skin.weights.push_back(w.sw); }
                }
                out.triangles.push_back(remap[wid]);
            }
            if (colours) out.triColors.push_back(src.triColors[t.src]);
        }
        r.mesh = std::move(out);
        r.collapses = collapses_;
        r.error = static_cast<float>(maxErr_);
    }

private:
    SimplifyOptions opt_;
    const SkinData* skin_;
    std::vector<D3> P_;
    std::vector<char> pAlive_, border_, seamV_, locked_;
    std::vector<Quadric> Q_;
    std::vector<double> W_;
    std::vector<unsigned> stamp_;
    std::vector<std::vector<int>> vTris_;
    std::vector<Wedge> wedges_;
    std::vector<Tri> tris_;
    bool hasUV_ = false, hasNormals_ = false, closed_ = true;
    int alive_ = 0, initialTris_ = 0, collapses_ = 0;
    double maxErr_ = 0.0, diag_ = 1.0;

    static constexpr float kUvTol = 1e-4f;
    static constexpr float kNormalDot = 0.9999f;

    bool SameWedge(const Wedge& a, const Wedge& b) const {
        if (hasUV_ && (std::fabs(a.uv.x - b.uv.x) > kUvTol || std::fabs(a.uv.y - b.uv.y) > kUvTol)) return false;
        if (hasNormals_ && Vec3::Dot(a.n, b.n) < kNormalDot) return false;
        if (skin_ && SkinDiff(a.sj, a.sw, b.sj, b.sw) > 1e-3f) return false;
        return true;
    }

    void Build(const Mesh& in) {
        const Mesh src = in.HasCornerUVs() ? in.SplitCornerUVs() : in;
        hasUV_ = src.HasUVs();
        hasNormals_ = src.HasNormals();
        const int nv = static_cast<int>(src.vertices.size());
        // Bounding box -> weld grid.
        Vec3 lo = src.vertices.empty() ? Vec3{0, 0, 0} : src.vertices[0], hi = lo;
        for (const Vec3& v : src.vertices) {
            lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)};
            hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)};
        }
        diag_ = std::max(static_cast<double>((hi - lo).Magnitude()), 1e-9);
        const double cell = std::max(diag_ * static_cast<double>(std::max(opt_.weldTolerance, 1e-9f)), 1e-12);
        std::unordered_map<QKey, int, QKeyHash> weld;
        std::vector<int> posOf(nv, -1);
        for (int i = 0; i < nv; ++i) {
            const Vec3& v = src.vertices[i];
            const QKey k{std::llround(v.x / cell), std::llround(v.y / cell), std::llround(v.z / cell)};
            auto it = weld.find(k);
            if (it == weld.end()) {
                it = weld.emplace(k, static_cast<int>(P_.size())).first;
                P_.push_back(FromVec(v));
            }
            posOf[i] = it->second;
        }
        const int np = static_cast<int>(P_.size());
        pAlive_.assign(np, 1); border_.assign(np, 0); seamV_.assign(np, 0); locked_.assign(np, 0);
        Q_.assign(np, Quadric()); W_.assign(np, 0.0); stamp_.assign(np, 0u); vTris_.assign(np, {});
        std::vector<std::vector<int>> wedgesAt(np);

        const double areaEps = 1e-14 * diag_ * diag_;
        const int srcTris = static_cast<int>(src.triangles.size() / 3);
        for (int t = 0; t < srcTris; ++t) {
            const int i0 = src.triangles[t * 3], i1 = src.triangles[t * 3 + 1], i2 = src.triangles[t * 3 + 2];
            if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= nv || i1 >= nv || i2 >= nv) continue;
            const int p0 = posOf[i0], p1 = posOf[i1], p2 = posOf[i2];
            if (p0 == p1 || p1 == p2 || p0 == p2) continue;           // collapsed by the weld
            if (Len(Cross(P_[p1] - P_[p0], P_[p2] - P_[p0])) <= areaEps) continue;   // zero area
            Tri tr;
            tr.src = t;
            const int ii[3] = {i0, i1, i2}, pp[3] = {p0, p1, p2};
            for (int k = 0; k < 3; ++k) {
                Wedge w;
                w.pos = pp[k];
                if (hasUV_) w.uv = src.uvs[ii[k]];
                if (hasNormals_) w.n = src.normals[ii[k]];
                if (skin_) { w.sj = skin_->joints[ii[k]]; w.sw = skin_->weights[ii[k]]; }
                int found = -1;
                for (int wid : wedgesAt[pp[k]]) if (SameWedge(wedges_[wid], w)) { found = wid; break; }
                if (found < 0) {
                    found = static_cast<int>(wedges_.size());
                    wedges_.push_back(w);
                    wedgesAt[pp[k]].push_back(found);
                }
                tr.v[k] = pp[k]; tr.w[k] = found;
            }
            const int id = static_cast<int>(tris_.size());
            tris_.push_back(tr);
            for (int k = 0; k < 3; ++k) vTris_[tr.v[k]].push_back(id);
        }
        alive_ = initialTris_ = static_cast<int>(tris_.size());

        // Edge classification: border / seam / non-manifold.
        struct EdgeInfo { int n = 0; int t[2] = {-1, -1}; };
        std::unordered_map<std::uint64_t, EdgeInfo> edges;
        edges.reserve(tris_.size() * 2);
        for (int t = 0; t < static_cast<int>(tris_.size()); ++t)
            for (int k = 0; k < 3; ++k) {
                EdgeInfo& e = edges[EdgeKey(tris_[t].v[k], tris_[t].v[(k + 1) % 3])];
                if (e.n < 2) e.t[e.n] = t;
                ++e.n;
            }
        for (int t = 0; t < static_cast<int>(tris_.size()); ++t) {
            const Tri& tr = tris_[t];
            const D3 nrm = Cross(P_[tr.v[1]] - P_[tr.v[0]], P_[tr.v[2]] - P_[tr.v[0]]);
            const double twice = Len(nrm);
            const D3 unit = nrm * (1.0 / twice);
            const double area = 0.5 * twice;
            for (int k = 0; k < 3; ++k) {
                const int a = tr.v[k];
                const double d = -Dot(unit, P_[a]);
                Q_[a].AddPlane(unit.x, unit.y, unit.z, d, area);
                W_[a] += area;
            }
            for (int k = 0; k < 3; ++k) {
                const int a = tr.v[k], b = tr.v[(k + 1) % 3];
                const EdgeInfo& e = edges[EdgeKey(a, b)];
                double weight = 0.0;
                if (e.n == 1) { border_[a] = border_[b] = 1; closed_ = false; weight = opt_.boundaryWeight; }
                else if (e.n == 2) {
                    const Tri& o = tris_[e.t[0] == t ? e.t[1] : e.t[0]];
                    const int ca = o.Corner(a), cb = o.Corner(b);
                    const bool mismatch = tr.w[k] != o.w[ca] || tr.w[(k + 1) % 3] != o.w[cb];
                    if (mismatch) { seamV_[a] = seamV_[b] = 1; weight = opt_.seamWeight; }
                } else { locked_[a] = locked_[b] = 1; }
                if (weight > 0.0) {
                    const D3 ed = P_[b] - P_[a];
                    const D3 m = Cross(ed, unit);
                    const double ml = Len(m);
                    if (ml > 1e-30) {
                        const D3 mu = m * (1.0 / ml);
                        const double dd = -Dot(mu, P_[a]);
                        const double w = weight * Len2(ed);
                        Q_[a].AddPlane(mu.x, mu.y, mu.z, dd, w);
                        Q_[b].AddPlane(mu.x, mu.y, mu.z, dd, w);
                    }
                }
            }
        }
        // Vertices whose triangles do not form one connected fan (bow ties) stay put.
        for (int v = 0; v < np; ++v) {
            const std::vector<int>& ts = vTris_[v];
            if (ts.size() < 2) continue;
            std::vector<int> comp(ts.size());
            for (std::size_t i = 0; i < ts.size(); ++i) comp[i] = static_cast<int>(i);
            auto find = [&](int x) { while (comp[x] != x) { comp[x] = comp[comp[x]]; x = comp[x]; } return x; };
            for (std::size_t i = 0; i < ts.size(); ++i)
                for (std::size_t j = i + 1; j < ts.size(); ++j) {
                    int shared = 0;
                    for (int a = 0; a < 3; ++a)
                        for (int b = 0; b < 3; ++b)
                            if (tris_[ts[i]].v[a] == tris_[ts[j]].v[b]) ++shared;
                    if (shared >= 2) comp[find(static_cast<int>(i))] = find(static_cast<int>(j));
                }
            int roots = 0;
            for (std::size_t i = 0; i < ts.size(); ++i) if (find(static_cast<int>(i)) == static_cast<int>(i)) ++roots;
            if (roots > 1) locked_[v] = 1;
        }
    }

    void Compact(int v) {
        std::vector<int>& ts = vTris_[v];
        std::size_t o = 0;
        for (std::size_t i = 0; i < ts.size(); ++i) {
            const Tri& t = tris_[ts[i]];
            if (t.alive && t.Corner(v) >= 0) ts[o++] = ts[i];
        }
        ts.resize(o);
    }

    void Neighbors(int v, std::vector<int>& out) {
        Compact(v);
        out.clear();
        for (int t : vTris_[v])
            for (int k = 0; k < 3; ++k) if (tris_[t].v[k] != v) out.push_back(tris_[t].v[k]);
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
    }

    void WedgesOf(int v, std::vector<int>& out) const {
        out.clear();
        for (int t : vTris_[v]) {
            const Tri& tr = tris_[t];
            const int c = tr.Corner(v);
            if (c >= 0 && tr.alive) out.push_back(tr.w[c]);
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
    }

    /// Map each wedge of `from` to the wedge of `to` across the edge. False when the
    /// collapse would tear or merge UV seams / hard edges or blend incompatible skin.
    bool BuildWedgeMap(int from, int to, const int* E, int ne, std::vector<std::pair<int, int>>& map, double& uvDelta2) const {
        map.clear();
        uvDelta2 = 0.0;
        for (int i = 0; i < ne; ++i) {
            const Tri& t = tris_[E[i]];
            const int wf = t.w[t.Corner(from)], wt = t.w[t.Corner(to)];
            bool seen = false;
            for (const auto& m : map) {
                if (m.first == wf) { if (m.second != wt) return false; seen = true; }
                else if (m.second == wt) return false;       // two source wedges onto one: would merge a seam
            }
            if (!seen) map.emplace_back(wf, wt);
        }
        std::vector<int> mine;
        WedgesOf(from, mine);
        for (int w : mine) {
            bool ok = false;
            for (const auto& m : map) if (m.first == w) { ok = true; break; }
            if (!ok) return false;
        }
        for (const auto& m : map) {
            const Wedge& a = wedges_[m.first];
            const Wedge& b = wedges_[m.second];
            if (skin_ && SkinDiff(a.sj, a.sw, b.sj, b.sw) > opt_.skinTolerance) return false;
            if (hasUV_) {
                const double dx = a.uv.x - b.uv.x, dy = a.uv.y - b.uv.y;
                uvDelta2 = std::max(uvDelta2, dx * dx + dy * dy);
            }
        }
        return true;
    }

    bool FlipOk(int t, int a, int b, const D3& wpos) const {
        const Tri& tr = tris_[t];
        D3 old[3], now[3];
        for (int k = 0; k < 3; ++k) {
            old[k] = P_[tr.v[k]];
            now[k] = (tr.v[k] == a || tr.v[k] == b) ? wpos : old[k];
        }
        const D3 n0 = Cross(old[1] - old[0], old[2] - old[0]);
        const D3 n1 = Cross(now[1] - now[0], now[2] - now[0]);
        const double l0 = Len(n0), l1 = Len(n1);
        if (l0 < 1e-30) return true;
        if (l1 <= 1e-4 * l0) return false;
        return Dot(n0, n1) >= static_cast<double>(opt_.minNormalDot) * l0 * l1;
    }

    Candidate Evaluate(int a, int b) {
        Candidate best;
        Compact(a); Compact(b);
        int E[3]; int ne = 0;
        for (int t : vTris_[a]) if (tris_[t].Corner(b) >= 0) { if (ne < 3) E[ne] = t; ++ne; }
        if (ne < 1 || ne > 2 || locked_[a] || locked_[b]) return best;
        // Link condition: the common neighbours of a and b are exactly the apexes of the edge's triangles.
        {
            std::vector<int> na, nb, common, apex;
            Neighbors(a, na); Neighbors(b, nb);
            std::set_intersection(na.begin(), na.end(), nb.begin(), nb.end(), std::back_inserter(common));
            for (int i = 0; i < ne; ++i)
                for (int k = 0; k < 3; ++k) if (tris_[E[i]].v[k] != a && tris_[E[i]].v[k] != b) apex.push_back(tris_[E[i]].v[k]);
            std::sort(apex.begin(), apex.end());
            if (ne == 2 && apex[0] == apex[1]) return best;
            if (common != apex) return best;
        }
        const Quadric q = Q_[a] + Q_[b];
        const double wsum = std::max(W_[a] + W_[b], 1e-30);
        const double uvScale = static_cast<double>(opt_.uvWeight) * diag_ * diag_ * wsum;
        auto consider = [&](int from, int to) {
            if (border_[from]) { if (ne != 1 || !border_[to] || opt_.lockBoundary) return; }
            if (opt_.lockSeams && seamV_[from]) return;
            std::vector<std::pair<int, int>> map;
            double uv2 = 0.0;
            if (!BuildWedgeMap(from, to, E, ne, map, uv2)) return;
            const D3 pos = P_[to];
            for (int t : vTris_[from]) {
                if (t == E[0] || (ne == 2 && t == E[1])) continue;
                if (!FlipOk(t, from, from, pos)) return;
            }
            const double err = q.Eval(pos);
            const double cost = err + uvScale * uv2;
            if (!best.valid || cost < best.cost) {
                best.valid = true; best.from = from; best.to = to; best.pos = pos; best.optimal = false;
                best.cost = cost; best.dist2 = err / wsum; best.removed = ne;
            }
        };
        consider(std::max(a, b), std::min(a, b));
        consider(std::min(a, b), std::max(a, b));
        // Better position for interior vertices with a single wedge each.
        if (opt_.optimalPosition && best.valid && !border_[a] && !border_[b] && !seamV_[a] && !seamV_[b]) {
            D3 w;
            if (q.Optimum(w)) {
                const D3 pa = P_[a], pb = P_[b];
                const double el = Len(pa - pb);
                const D3 mid = (pa + pb) * 0.5;
                if (Len(w - mid) <= 1.5 * el) {
                    const double err = q.Eval(w);
                    std::vector<int> wa, wb;
                    WedgesOf(a, wa); WedgesOf(b, wb);
                    if (wa.size() == 1 && wb.size() == 1 && err < best.cost * (1.0 - 1e-6) - 1e-18) {
                        bool ok = true;
                        for (int side = 0; side < 2 && ok; ++side) {
                            const int v = side == 0 ? a : b;
                            for (int t : vTris_[v]) {
                                if (t == E[0] || (ne == 2 && t == E[1])) continue;
                                if (!FlipOk(t, v, v, w)) { ok = false; break; }
                            }
                        }
                        if (ok) {
                            const double uv2 = hasUV_ ? static_cast<double>(
                                (wedges_[wa[0]].uv.x - wedges_[wb[0]].uv.x) * (wedges_[wa[0]].uv.x - wedges_[wb[0]].uv.x) +
                                (wedges_[wa[0]].uv.y - wedges_[wb[0]].uv.y) * (wedges_[wa[0]].uv.y - wedges_[wb[0]].uv.y)) : 0.0;
                            const bool skinOk = !skin_ || SkinDiff(wedges_[wa[0]].sj, wedges_[wa[0]].sw,
                                                                   wedges_[wb[0]].sj, wedges_[wb[0]].sw) <= opt_.skinTolerance;
                            const double cost = err + uvScale * uv2;
                            if (skinOk && cost < best.cost) {
                                best.from = std::max(a, b); best.to = std::min(a, b); best.pos = w; best.optimal = true;
                                best.cost = cost; best.dist2 = err / wsum;
                            }
                        }
                    }
                }
            }
        }
        return best;
    }

    /// Attributes of the surface at `p`, interpolated from the closest triangle around the edge.
    Wedge AttributesAt(int a, int b, const D3& p, int toVertex) const {
        double bestD = 1e300;
        Wedge out;
        out.pos = toVertex;
        bool have = false;
        for (int side = 0; side < 2; ++side) {
            const int v = side == 0 ? a : b;
            for (int t : vTris_[v]) {
                const Tri& tr = tris_[t];
                if (!tr.alive) continue;
                double u, vv, w, d2;
                ClosestBary(p, P_[tr.v[0]], P_[tr.v[1]], P_[tr.v[2]], u, vv, w, d2);
                if (d2 >= bestD) continue;
                bestD = d2; have = true;
                const double bc[3] = {u, vv, w};
                Wedge acc;
                acc.pos = toVertex;
                Vec3 n{0, 0, 0};
                Vec2 uv{0, 0};
                std::map<int, float> wt;
                for (int k = 0; k < 3; ++k) {
                    const Wedge& wk = wedges_[tr.w[k]];
                    uv += wk.uv * static_cast<float>(bc[k]);
                    n += wk.n * static_cast<float>(bc[k]);
                    if (skin_)
                        for (int s = 0; s < 4; ++s) if (wk.sw[s] > 0.0f) wt[wk.sj[s]] += wk.sw[s] * static_cast<float>(bc[k]);
                }
                acc.uv = uv; acc.n = n;
                if (skin_) {
                    std::vector<std::pair<float, int>> sorted;
                    for (const auto& kv : wt) sorted.emplace_back(kv.second, kv.first);
                    std::stable_sort(sorted.begin(), sorted.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
                    float sum = 0.0f;
                    for (int s = 0; s < 4 && s < static_cast<int>(sorted.size()); ++s) {
                        acc.sj[s] = sorted[s].second; acc.sw[s] = sorted[s].first; sum += sorted[s].first;
                    }
                    if (sum > 0.0f) for (float& x : acc.sw) x /= sum;
                }
                out = acc;
            }
        }
        (void)have;
        return out;
    }

    void Apply(const Candidate& c) {
        const int from = c.from, to = c.to;
        int E[3]; int ne = 0;
        for (int t : vTris_[from]) if (tris_[t].alive && tris_[t].Corner(to) >= 0) { if (ne < 3) E[ne] = t; ++ne; }
        std::vector<std::pair<int, int>> map;
        double uv2 = 0.0;
        BuildWedgeMap(from, to, E, ne, map, uv2);
        Wedge optimalAttr;
        std::vector<int> toWedges;
        if (c.optimal) {
            WedgesOf(to, toWedges);
            optimalAttr = AttributesAt(from, to, c.pos, to);
        }
        for (int i = 0; i < ne; ++i) { tris_[E[i]].alive = false; --alive_; }
        for (int t : vTris_[from]) {
            Tri& tr = tris_[t];
            if (!tr.alive) continue;
            const int k = tr.Corner(from);
            if (k < 0) continue;
            int mapped = tr.w[k];
            for (const auto& m : map) if (m.first == tr.w[k]) { mapped = m.second; break; }
            tr.v[k] = to; tr.w[k] = mapped;
            vTris_[to].push_back(t);
        }
        if (c.optimal) {
            P_[to] = c.pos;
            if (!toWedges.empty()) wedges_[toWedges[0]] = optimalAttr;
        }
        Q_[to] += Q_[from];
        W_[to] += W_[from];
        pAlive_[from] = 0;
        vTris_[from].clear();
        ++stamp_[to];
    }

    template <class Heap>
    void pushEdge(Heap& heap, int a, int b) {
        Candidate c = Evaluate(a, b);
        if (c.valid) heap.push({c.cost, a, b, stamp_[a], stamp_[b]});
    }
    template <class Heap>
    void pushAll(Heap& heap) {
        std::unordered_map<std::uint64_t, bool> seen;
        seen.reserve(tris_.size() * 2);
        for (std::size_t t = 0; t < tris_.size(); ++t)
            for (int k = 0; k < 3; ++k) {
                const int a = tris_[t].v[k], b = tris_[t].v[(k + 1) % 3];
                if (!seen.emplace(EdgeKey(a, b), true).second) continue;
                pushEdge(heap, std::min(a, b), std::max(a, b));
            }
    }
};

} // namespace

float LodScreenHeightFor(float size, float distance, float fovYDeg) {
    const float t = std::tan(std::max(fovYDeg, 1e-3f) * 0.5f * 0.01745329252f);
    const float d = std::max(distance, 1e-6f);
    return size / (2.0f * d * std::max(t, 1e-6f));
}

float LodDistanceFor(float size, float screenHeight, float fovYDeg) {
    if (screenHeight <= 0.0f) return Mathf::Infinity;
    const float t = std::tan(std::max(fovYDeg, 1e-3f) * 0.5f * 0.01745329252f);
    return size / (2.0f * screenHeight * std::max(t, 1e-6f));
}

float MeshSurfaceArea(const Mesh& m) {
    double a = 0.0;
    const int n = static_cast<int>(m.triangles.size() / 3);
    const int nv = static_cast<int>(m.vertices.size());
    for (int t = 0; t < n; ++t) {
        const int i = m.triangles[t * 3], j = m.triangles[t * 3 + 1], k = m.triangles[t * 3 + 2];
        if (i < 0 || j < 0 || k < 0 || i >= nv || j >= nv || k >= nv) continue;
        a += 0.5 * Len(Cross(FromVec(m.vertices[j]) - FromVec(m.vertices[i]), FromVec(m.vertices[k]) - FromVec(m.vertices[i])));
    }
    return static_cast<float>(a);
}

SimplifyResult SimplifyMesh(const Mesh& mesh, const SimplifyOptions& opt) {
    SimplifyResult r;
    r.trianglesBefore = mesh.TriangleCount();
    r.verticesBefore = static_cast<int>(mesh.vertices.size());
    r.skinned = opt.skin != nullptr && !opt.skin->Empty();
    auto unchanged = [&](const char* why) {
        r.mesh = mesh;
        if (r.skinned && opt.skin->Matches(mesh)) r.skin = *opt.skin;
        r.trianglesAfter = r.trianglesBefore;
        r.verticesAfter = r.verticesBefore;
        if (why) r.note = why;
    };
    if (r.skinned) {
        if (opt.skinPolicy == SkinPolicy::Refuse) {
            r.skinSupported = false;
            unchanged("skinned meshes are not supported by this policy (SkinPolicy::Refuse)");
            return r;
        }
        if (!opt.skin->Matches(mesh)) {
            r.skinSupported = false;
            unchanged("skin data does not match the mesh vertex count");
            return r;
        }
    }
    int target = opt.targetTriangles >= 0
        ? opt.targetTriangles
        : static_cast<int>(std::floor(static_cast<double>(r.trianglesBefore) * static_cast<double>(std::max(opt.targetRatio, 0.0f)) + 0.5));
    if (target >= r.trianglesBefore && opt.maxError < 0.0f) {
        unchanged(nullptr);
        r.reachedTarget = true;
        return r;
    }
    if (mesh.triangles.size() < 3) { unchanged(nullptr); r.reachedTarget = true; return r; }
    if (opt.maxError >= 0.0f && opt.targetTriangles < 0 && opt.targetRatio >= 1.0f) target = 0;   // error-driven
    Simplifier s(mesh, opt, r.skinned ? opt.skin : nullptr);
    s.Run(target);
    s.Emit(mesh, r);
    r.trianglesAfter = r.mesh.TriangleCount();
    r.verticesAfter = static_cast<int>(r.mesh.vertices.size());
    r.reachedTarget = r.trianglesAfter <= std::max(target, 0) || (opt.maxError >= 0.0f && target == 0);
    return r;
}

LodChain GenerateLodChain(const Mesh& mesh, const std::vector<float>& ratios, const LodChainOptions& opt) {
    LodChain chain;
    Vec3 lo = mesh.vertices.empty() ? Vec3{0, 0, 0} : mesh.vertices[0], hi = lo;
    for (const Vec3& v : mesh.vertices) {
        lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)};
        hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)};
    }
    chain.objectSize = opt.objectSize > 0.0f ? opt.objectSize : (hi - lo).Magnitude();
    const float tanHalf = std::tan(std::max(opt.fovYDeg, 1e-3f) * 0.5f * 0.01745329252f);
    const float pxPerUnit = opt.viewportHeightPx / (2.0f * std::max(tanHalf, 1e-6f));
    const float pxErr = std::max(opt.pixelError, 1e-3f);
    for (std::size_t i = 0; i < ratios.size(); ++i) {
        LodMeshLevel lv;
        lv.ratio = ratios[i];
        SimplifyOptions so = opt.base;
        so.targetRatio = std::min(std::max(ratios[i], 0.0f), 1.0f);
        so.targetTriangles = -1;
        SimplifyResult r = SimplifyMesh(mesh, so);
        if (r.skinned && !r.skinSupported) chain.skinSupported = false;
        if (i > 0 && r.trianglesAfter > chain.levels.back().triangles) {
            // Never let a level grow past the one above it.
            lv.mesh = chain.levels.back().mesh;
            lv.skin = chain.levels.back().skin;
            lv.triangles = chain.levels.back().triangles;
            lv.error = chain.levels.back().error;
            lv.reachedTarget = false;
        } else {
            lv.mesh = std::move(r.mesh);
            lv.skin = std::move(r.skin);
            lv.triangles = r.trianglesAfter;
            lv.error = r.error;
            lv.reachedTarget = r.reachedTarget;
        }
        lv.mesh.name = (mesh.name.empty() ? std::string("Mesh") : mesh.name) + "_LOD" + std::to_string(i);
        if (i == 0) {
            lv.switchDistance = 0.0f;
            lv.switchScreenHeight = 1.0f;
        } else {
            const float prev = chain.levels.back().switchDistance;
            float d = lv.error * pxPerUnit / pxErr;
            const float minD = prev * 1.25f + 0.05f * chain.objectSize;
            if (d < minD) d = minD;
            lv.switchDistance = d;
            lv.switchScreenHeight = std::min(1.0f, LodScreenHeightFor(chain.objectSize, d, opt.fovYDeg));
        }
        chain.levels.push_back(std::move(lv));
    }
    return chain;
}

} // namespace okay::optimize
