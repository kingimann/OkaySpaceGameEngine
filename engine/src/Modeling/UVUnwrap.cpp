#include "okay/Modeling/UVUnwrap.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <set>
#include <tuple>
#include <utility>

namespace okay::modeling {

namespace {

constexpr double kPi = 3.14159265358979323846;

using EdgeKey = std::pair<int, int>;
EdgeKey Key(int a, int b) { return a < b ? EdgeKey{a, b} : EdgeKey{b, a}; }

struct UnionFind {
    std::vector<int> p;
    explicit UnionFind(int n) : p(n) { std::iota(p.begin(), p.end(), 0); }
    int Find(int x) { while (p[x] != x) { p[x] = p[p[x]]; x = p[x]; } return x; }
    void Join(int a, int b) { a = Find(a); b = Find(b); if (a != b) p[b] = a; }
};

bool FaceOk(const PolyMesh& pm, int f) {
    if (f < 0 || f >= (int)pm.faces.size()) return false;
    const auto& vs = pm.faces[f].verts;
    if (vs.size() < 3) return false;
    for (int v : vs) if (v < 0 || v >= (int)pm.verts.size()) return false;
    return true;
}

std::vector<int> FaceSet(const PolyMesh& pm, const std::vector<int>& faces) {
    std::vector<int> out;
    if (faces.empty()) { for (int f = 0; f < (int)pm.faces.size(); ++f) if (FaceOk(pm, f)) out.push_back(f); }
    else {
        std::set<int> seen;
        for (int f : faces) if (FaceOk(pm, f) && seen.insert(f).second) out.push_back(f);
    }
    return out;
}

// Edge -> faces of `set` using it.
std::map<EdgeKey, std::vector<int>> EdgeFaces(const PolyMesh& pm, const std::vector<int>& set) {
    std::map<EdgeKey, std::vector<int>> ef;
    for (int f : set) {
        const auto& vs = pm.faces[f].verts;
        for (std::size_t i = 0; i < vs.size(); ++i) ef[Key(vs[i], vs[(i + 1) % vs.size()])].push_back(f);
    }
    return ef;
}

int CornerOf(const PolyFace& f, int v) {
    for (int i = 0; i < (int)f.verts.size(); ++i) if (f.verts[i] == v) return i;
    return -1;
}

bool HasUV(const PolyFace& f) { return f.uvs.size() == f.verts.size() && !f.uvs.empty(); }
bool SameUV(const Vec2& a, const Vec2& b) { return std::fabs(a.x - b.x) <= 1e-5f && std::fabs(a.y - b.y) <= 1e-5f; }

// Group faces by a joining rule over shared manifold edges.
std::vector<std::vector<int>> Group(const PolyMesh& pm, const std::vector<int>& set,
                                    const std::function<bool(const EdgeKey&, int, int)>& joins) {
    std::map<int, int> local;
    for (int i = 0; i < (int)set.size(); ++i) local[set[i]] = i;
    UnionFind uf((int)set.size());
    for (const auto& [e, fs] : EdgeFaces(pm, set))
        if (fs.size() == 2 && fs[0] != fs[1] && joins(e, fs[0], fs[1])) uf.Join(local[fs[0]], local[fs[1]]);
    std::map<int, std::vector<int>> groups;
    for (int i = 0; i < (int)set.size(); ++i) groups[uf.Find(i)].push_back(set[i]);
    std::vector<std::vector<int>> out;
    for (auto& [r, g] : groups) out.push_back(std::move(g));
    return out;
}

// ---- One island's corners, welded into the vertices the flattening solves for ----
struct Chart {
    std::vector<int> faces;
    std::vector<int> faceStart;          // first corner id of each face
    std::vector<int> cornerVert;         // corner id -> chart vertex
    std::vector<int> chartToMesh;        // chart vertex -> PolyMesh vertex
    int Corner(int fi, int k) const { return faceStart[fi] + k; }
};

Chart BuildChart(const PolyMesh& pm, const std::vector<int>& faces) {
    Chart c;
    c.faces = faces;
    int n = 0;
    for (int f : faces) { c.faceStart.push_back(n); n += (int)pm.faces[f].verts.size(); }
    std::map<int, int> fi;
    for (int i = 0; i < (int)faces.size(); ++i) fi[faces[i]] = i;
    UnionFind uf(n);
    // Corners of the same vertex join across every edge that isn't a seam.
    for (const auto& [e, fs] : EdgeFaces(pm, faces)) {
        if (fs.size() != 2 || fs[0] == fs[1] || pm.IsSeam(e.first, e.second)) continue;
        const int a = fi[fs[0]], b = fi[fs[1]];
        for (int v : {e.first, e.second}) {
            const int ka = CornerOf(pm.faces[fs[0]], v), kb = CornerOf(pm.faces[fs[1]], v);
            if (ka >= 0 && kb >= 0) uf.Join(c.Corner(a, ka), c.Corner(b, kb));
        }
    }
    std::map<int, int> id;
    c.cornerVert.resize(n);
    for (int i = 0; i < (int)faces.size(); ++i) {
        const auto& vs = pm.faces[faces[i]].verts;
        for (int k = 0; k < (int)vs.size(); ++k) {
            const int r = uf.Find(c.Corner(i, k));
            auto it = id.find(r);
            if (it == id.end()) { it = id.emplace(r, (int)c.chartToMesh.size()).first; c.chartToMesh.push_back(vs[k]); }
            c.cornerVert[c.Corner(i, k)] = it->second;
        }
    }
    return c;
}

// Chart edges used by only one face: the chart's border.
std::vector<EdgeKey> ChartBorder(const PolyMesh& pm, const Chart& c) {
    std::map<EdgeKey, int> uses;
    for (int i = 0; i < (int)c.faces.size(); ++i) {
        const int n = (int)pm.faces[c.faces[i]].verts.size();
        for (int k = 0; k < n; ++k) ++uses[Key(c.cornerVert[c.Corner(i, k)], c.cornerVert[c.Corner(i, (k + 1) % n)])];
    }
    std::vector<EdgeKey> out;
    for (const auto& [e, u] : uses) if (u == 1) out.push_back(e);
    return out;
}

// Shortest edge paths over an island's vertices.
struct EdgeGraph {
    std::map<int, std::vector<std::pair<int, double>>> adj;
    EdgeGraph(const PolyMesh& pm, const std::vector<int>& faces) {
        for (const auto& [e, fs] : EdgeFaces(pm, faces)) {
            const double len = (pm.verts[e.first] - pm.verts[e.second]).Magnitude();
            adj[e.first].push_back({e.second, len});
            adj[e.second].push_back({e.first, len});
        }
    }
    // Distances from every source; `stop` ends the search at the first vertex it accepts.
    int Search(const std::set<int>& sources, std::map<int, double>& dist, std::map<int, int>& prev,
               const std::function<bool(int)>& stop = nullptr) const {
        dist.clear(); prev.clear();
        using Q = std::pair<double, int>;
        std::priority_queue<Q, std::vector<Q>, std::greater<Q>> q;
        for (int s : sources) { dist[s] = 0; q.push({0, s}); }
        while (!q.empty()) {
            auto [d, v] = q.top(); q.pop();
            if (d > dist[v]) continue;
            if (stop && !sources.count(v) && stop(v)) return v;
            auto it = adj.find(v);
            if (it == adj.end()) continue;
            for (const auto& [w, len] : it->second) {
                auto jt = dist.find(w);
                if (jt == dist.end() || d + len < jt->second) { dist[w] = d + len; prev[w] = v; q.push({d + len, w}); }
            }
        }
        return -1;
    }
};

int MarkPath(PolyMesh& pm, int from, const std::map<int, int>& prev) {
    int cut = 0;
    for (int v = from; prev.count(v); v = prev.at(v)) {
        const int u = prev.at(v);
        if (!pm.IsSeam(v, u)) { pm.SetSeam(v, u, true); ++cut; }
    }
    return cut;
}

// A closed part (no border at all): cut along the shortest path between its two
// farthest-apart vertices.
int CutClosed(PolyMesh& pm, const std::vector<int>& faces) {
    const EdgeGraph g(pm, faces);
    if (g.adj.size() < 3) return 0;
    std::map<int, double> dist; std::map<int, int> prev;
    auto farthest = [&] {
        int best = dist.begin()->first; double bd = -1;
        for (const auto& [v, d] : dist) if (d > bd) { bd = d; best = v; }
        return best;
    };
    g.Search({g.adj.begin()->first}, dist, prev);
    const int a = farthest();
    g.Search({a}, dist, prev);
    return MarkPath(pm, farthest(), prev);
}

// A closed part with handles (a torus: Euler characteristic V - E + F below 2)
// needs more than a slit. Tree-cotree (Eppstein 2003): a shortest-path tree of
// the vertices plus a spanning tree of the faces across the remaining edges
// leaves exactly 2g edges; each closes a loop through the tree, and cutting
// along all of them opens the surface into a disc.
int CutHandles(PolyMesh& pm, const std::vector<int>& faces) {
    const EdgeGraph g(pm, faces);
    if (g.adj.empty()) return 0;
    std::map<int, double> dist; std::map<int, int> prev;
    g.Search({g.adj.begin()->first}, dist, prev);
    std::set<EdgeKey> tree;
    for (const auto& [v, u] : prev) tree.insert(Key(v, u));
    // Faces joined across non-tree edges, breadth first: the cotree.
    const auto ef = EdgeFaces(pm, faces);
    std::map<int, std::vector<std::pair<int, EdgeKey>>> fadj;
    for (const auto& [e, fs] : ef)
        if (fs.size() == 2 && !tree.count(e)) { fadj[fs[0]].push_back({fs[1], e}); fadj[fs[1]].push_back({fs[0], e}); }
    std::set<EdgeKey> cotree;
    std::set<int> seen{faces[0]};
    std::queue<int> q;
    q.push(faces[0]);
    while (!q.empty()) {
        const int f = q.front(); q.pop();
        for (const auto& [h, e] : fadj[f]) if (seen.insert(h).second) { cotree.insert(e); q.push(h); }
    }
    int cut = 0;
    for (const auto& [e, fs] : ef) {
        if (tree.count(e) || cotree.count(e)) continue;
        if (!pm.IsSeam(e.first, e.second)) { pm.SetSeam(e.first, e.second, true); ++cut; }
        cut += MarkPath(pm, e.first, prev) + MarkPath(pm, e.second, prev);
    }
    return cut;
}

// Euler characteristic of a chart: 1 for a disc, 2 for a closed sphere-like part.
int ChartEuler(const PolyMesh& pm, const Chart& c) {
    std::set<EdgeKey> edges;
    for (int i = 0; i < (int)c.faces.size(); ++i) {
        const int n = (int)pm.faces[c.faces[i]].verts.size();
        for (int k = 0; k < n; ++k) edges.insert(Key(c.cornerVert[c.Corner(i, k)], c.cornerVert[c.Corner(i, (k + 1) % n)]));
    }
    return (int)c.chartToMesh.size() - (int)edges.size() + (int)c.faces.size();
}

// The chart's border edges grouped into loops; each loop as the mesh vertices on it.
std::vector<std::set<int>> BorderLoops(const PolyMesh& pm, const Chart& c) {
    const std::vector<EdgeKey> border = ChartBorder(pm, c);
    UnionFind uf((int)c.chartToMesh.size());
    for (const EdgeKey& e : border) uf.Join(e.first, e.second);
    std::map<int, std::set<int>> loops;
    for (const EdgeKey& e : border) {
        const int r = uf.Find(e.first);
        loops[r].insert(c.chartToMesh[e.first]);
        loops[r].insert(c.chartToMesh[e.second]);
    }
    std::vector<std::set<int>> out;
    for (auto& [r, l] : loops) out.push_back(std::move(l));
    return out;
}

// Only a disc lies flat. Cut a closed part open, and join extra border loops
// (a tube's two rims, a hole) to the first with shortest paths, all marked as
// seams. Returns the seam edges added.
int OpenChart(PolyMesh& pm, const std::vector<int>& faces) {
    int cut = 0;
    for (int guard = 0; guard < 64; ++guard) {
        const Chart c = BuildChart(pm, faces);
        const std::vector<std::set<int>> loops = BorderLoops(pm, c);
        int added = 0;
        if (loops.empty()) {
            added = ChartEuler(pm, c) < 2 ? CutHandles(pm, faces) : CutClosed(pm, faces);
        } else if (loops.size() > 1) {
            std::map<int, int> loopOf;
            for (int l = 1; l < (int)loops.size(); ++l) for (int v : loops[l]) loopOf.emplace(v, l);
            const EdgeGraph g(pm, faces);
            std::map<int, double> dist; std::map<int, int> prev;
            const int hit = g.Search(loops[0], dist, prev, [&](int v) { return loopOf.count(v) > 0; });
            if (hit >= 0) added = MarkPath(pm, hit, prev);
        }
        if (added == 0) break;
        cut += added;
    }
    return cut;
}

// Principal axes of a point set (largest first).
void Axes(const std::vector<Vec3>& pts, Vec3& centre, Vec3& e1, Vec3& e2) {
    centre = {0, 0, 0};
    for (const Vec3& p : pts) centre += p;
    centre = centre * (1.0f / (float)std::max<std::size_t>(1, pts.size()));
    double c[3][3] = {};
    for (const Vec3& p : pts) {
        const double d[3] = {p.x - centre.x, p.y - centre.y, p.z - centre.z};
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) c[i][j] += d[i] * d[j];
    }
    auto power = [&](const double (&m)[3][3], double v[3]) {
        for (int it = 0; it < 64; ++it) {
            double w[3] = {0, 0, 0};
            for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) w[i] += m[i][j] * v[j];
            const double n = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
            if (n < 1e-30) return;
            for (int i = 0; i < 3; ++i) v[i] = w[i] / n;
        }
    };
    double v1[3] = {0.57735, 0.57735, 0.57735};
    power(c, v1);
    const double l1 = [&] { double s = 0; for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) s += v1[i] * c[i][j] * v1[j]; return s; }();
    double d2[3][3];
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) d2[i][j] = c[i][j] - l1 * v1[i] * v1[j];
    double v2[3] = {-v1[1] + 0.1, v1[0] + 0.2, 0.3 - v1[2]};
    power(d2, v2);
    e1 = Vec3{(float)v1[0], (float)v1[1], (float)v1[2]}.Normalized();
    Vec3 t{(float)v2[0], (float)v2[1], (float)v2[2]};
    t = t - e1 * Vec3::Dot(t, e1);
    if (t.SqrMagnitude() < 1e-12f) t = std::fabs(e1.x) < 0.9f ? Vec3::Cross(e1, Vec3{1, 0, 0}) : Vec3::Cross(e1, Vec3{0, 1, 0});
    e2 = t.Normalized();
}

// Sparse symmetric matrix (CSR) for the normal equations.
struct Sparse {
    int n = 0;
    std::vector<int> start, col;
    std::vector<double> val;
    void Mul(const std::vector<double>& x, std::vector<double>& y) const {
        y.assign(n, 0.0);
        for (int i = 0; i < n; ++i) {
            double s = 0;
            for (int k = start[i]; k < start[i + 1]; ++k) s += val[k] * x[col[k]];
            y[i] = s;
        }
    }
};

// Jacobi-preconditioned conjugate gradient. False if it didn't converge to something finite.
bool SolveCG(const Sparse& A, const std::vector<double>& b, std::vector<double>& x, int maxIter, double tol) {
    const int n = A.n;
    std::vector<double> diag(n, 1.0);
    for (int i = 0; i < n; ++i)
        for (int k = A.start[i]; k < A.start[i + 1]; ++k)
            if (A.col[k] == i && std::fabs(A.val[k]) > 1e-300) diag[i] = A.val[k];
    std::vector<double> r(n), z(n), p(n), Ap(n);
    A.Mul(x, Ap);
    double bn = 0;
    for (int i = 0; i < n; ++i) { r[i] = b[i] - Ap[i]; bn += b[i] * b[i]; }
    if (bn < 1e-300) bn = 1;
    for (int i = 0; i < n; ++i) { z[i] = r[i] / diag[i]; p[i] = z[i]; }
    double rz = 0;
    for (int i = 0; i < n; ++i) rz += r[i] * z[i];
    for (int it = 0; it < maxIter; ++it) {
        double rr = 0;
        for (int i = 0; i < n; ++i) rr += r[i] * r[i];
        if (rr <= tol * tol * bn) break;
        A.Mul(p, Ap);
        double pAp = 0;
        for (int i = 0; i < n; ++i) pAp += p[i] * Ap[i];
        if (!(std::fabs(pAp) > 1e-300)) break;
        const double alpha = rz / pAp;
        for (int i = 0; i < n; ++i) { x[i] += alpha * p[i]; r[i] -= alpha * Ap[i]; }
        double rzNew = 0;
        for (int i = 0; i < n; ++i) { z[i] = r[i] / diag[i]; rzNew += r[i] * z[i]; }
        const double beta = rzNew / rz;
        rz = rzNew;
        for (int i = 0; i < n; ++i) p[i] = z[i] + beta * p[i];
    }
    for (double v : x) if (!std::isfinite(v)) return false;
    return true;
}

double TriArea2D(const Vec2& a, const Vec2& b, const Vec2& c) {
    return 0.5 * ((double)(b.x - a.x) * (c.y - a.y) - (double)(b.y - a.y) * (c.x - a.x));
}

// Flatten one chart with LSCM. Writes chart vertex UVs; false if it couldn't.
bool SolveChart(const PolyMesh& pm, const Chart& c, std::vector<Vec2>& uv) {
    const int n = (int)c.chartToMesh.size();
    uv.assign(n, Vec2{0, 0});
    if (n < 3) return false;
    std::vector<Vec3> pos(n);
    for (int i = 0; i < n; ++i) pos[i] = pm.verts[c.chartToMesh[i]];
    // Planar projection: the starting guess (and the fallback).
    Vec3 centre, e1, e2;
    Axes(pos, centre, e1, e2);
    for (int i = 0; i < n; ++i) uv[i] = Vec2{Vec3::Dot(pos[i] - centre, e1), Vec3::Dot(pos[i] - centre, e2)};

    // Triangles, in chart vertices.
    std::vector<std::array<int, 3>> tris;
    for (int fi = 0; fi < (int)c.faces.size(); ++fi) {
        const std::vector<int> t = pm.TriangulateFace(c.faces[fi]);
        for (std::size_t k = 0; k + 2 < t.size(); k += 3)
            tris.push_back({c.cornerVert[c.Corner(fi, t[k])], c.cornerVert[c.Corner(fi, t[k + 1])], c.cornerVert[c.Corner(fi, t[k + 2])]});
    }
    if (tris.empty()) return false;

    // Pins: the two border vertices farthest apart along the main axis.
    const std::vector<EdgeKey> border = ChartBorder(pm, c);
    if (border.empty()) return false;
    int p0 = border[0].first, p1 = border[0].first;
    float lo = 1e30f, hi = -1e30f;
    for (const EdgeKey& e : border)
        for (int v : {e.first, e.second}) {
            const float d = Vec3::Dot(pos[v] - centre, e1);
            if (d < lo) { lo = d; p0 = v; }
            if (d > hi) { hi = d; p1 = v; }
        }
    if (p0 == p1 || (pos[p0] - pos[p1]).SqrMagnitude() < 1e-14f) return false;

    // Unknowns: u, v of every unpinned vertex.
    std::vector<int> var(2 * n, -1);
    int nf = 0;
    for (int i = 0; i < n; ++i)
        if (i != p0 && i != p1) { var[2 * i] = nf++; var[2 * i + 1] = nf++; }
    auto fixedVal = [&](int vi) { return (vi & 1) ? (double)uv[vi >> 1].y : (double)uv[vi >> 1].x; };

    // Each triangle adds two rows (real and imaginary part of the conformality
    // condition, Lévy 2002), weighted so each counts by its area.
    std::vector<std::tuple<int, int, double>> trip;
    std::vector<double> rhs(nf, 0.0);
    std::vector<double> x(nf, 0.0);
    for (int i = 0; i < n; ++i)
        if (var[2 * i] >= 0) { x[var[2 * i]] = uv[i].x; x[var[2 * i + 1]] = uv[i].y; }
    for (const auto& t : tris) {
        const Vec3 &q0 = pos[t[0]], &q1 = pos[t[1]], &q2 = pos[t[2]];
        const Vec3 d1 = q1 - q0, d2 = q2 - q0;
        const Vec3 nrm = Vec3::Cross(d1, d2);
        const double area2 = nrm.Magnitude();
        if (area2 < 1e-14) continue;
        const Vec3 ax = d1.Normalized();
        const Vec3 ay = Vec3::Cross(nrm * (float)(1.0 / area2), ax);
        const double a = d1.Magnitude(), b = 0.0;
        const double cc = Vec3::Dot(d2, ax), d = Vec3::Dot(d2, ay);
        const double w = 1.0 / std::sqrt(area2);
        const int u0 = 2 * t[0], v0 = 2 * t[0] + 1, u1 = 2 * t[1], v1 = 2 * t[1] + 1, u2 = 2 * t[2], v2 = 2 * t[2] + 1;
        const std::pair<int, double> re[5] = {{u0, -a + cc}, {v0, b - d}, {u1, -cc}, {v1, d}, {u2, a}};
        const std::pair<int, double> im[5] = {{u0, -b + d}, {v0, -a + cc}, {u1, -d}, {v1, -cc}, {v2, a}};
        for (const auto* row : {re, im}) {
            double rr = 0;   // the pinned part moves to the right-hand side
            for (int k = 0; k < 5; ++k) if (var[row[k].first] < 0) rr -= row[k].second * w * fixedVal(row[k].first);
            for (int k = 0; k < 5; ++k) {
                const int ik = var[row[k].first];
                if (ik < 0) continue;
                const double ck = row[k].second * w;
                rhs[ik] += ck * rr;
                for (int j = 0; j < 5; ++j) {
                    const int jk = var[row[j].first];
                    if (jk >= 0) trip.emplace_back(ik, jk, ck * row[j].second * w);
                }
            }
        }
    }
    if (nf == 0) return true;
    std::sort(trip.begin(), trip.end(), [](const auto& l, const auto& r) {
        return std::get<0>(l) != std::get<0>(r) ? std::get<0>(l) < std::get<0>(r) : std::get<1>(l) < std::get<1>(r);
    });
    Sparse A;
    A.n = nf;
    A.start.assign(nf + 1, 0);
    for (std::size_t k = 0; k < trip.size(); ++k) {
        const auto [i, j, v] = trip[k];
        if (!A.col.empty() && k > 0 && std::get<0>(trip[k - 1]) == i && std::get<1>(trip[k - 1]) == j) { A.val.back() += v; continue; }
        A.col.push_back(j); A.val.push_back(v); ++A.start[i + 1];
    }
    for (int i = 0; i < nf; ++i) A.start[i + 1] += A.start[i];
    if (!SolveCG(A, rhs, x, std::max(2000, std::min(20000, nf * 4)), 1e-9)) return false;
    for (int i = 0; i < n; ++i)
        if (var[2 * i] >= 0) uv[i] = Vec2{(float)x[var[2 * i]], (float)x[var[2 * i + 1]]};
    // Texture rows run top-down (v = 0 is the image's top, as in glTF and on the
    // GPU renderers), so a face seen from outside reads the right way round when
    // its UV triangle winds clockwise: negative area in (u, v).
    double signedArea = 0;
    for (const auto& t : tris) signedArea += TriArea2D(uv[t[0]], uv[t[1]], uv[t[2]]);
    if (signedArea > 0) for (Vec2& p : uv) p.y = -p.y;
    for (const Vec2& p : uv) if (!std::isfinite(p.x) || !std::isfinite(p.y)) return false;
    return true;
}

// ---- Packing ------------------------------------------------------------------
struct IslandBox {
    std::vector<int> faces;
    double w = 0, h = 0;          // size after rotation
    double x = 0, y = 0;          // placed corner
};

std::vector<Vec2> Hull(std::vector<Vec2> p) {
    std::sort(p.begin(), p.end(), [](const Vec2& a, const Vec2& b) { return a.x != b.x ? a.x < b.x : a.y < b.y; });
    p.erase(std::unique(p.begin(), p.end(), [](const Vec2& a, const Vec2& b) { return a.x == b.x && a.y == b.y; }), p.end());
    if (p.size() < 3) return p;
    auto cross = [](const Vec2& o, const Vec2& a, const Vec2& b) { return (double)(a.x - o.x) * (b.y - o.y) - (double)(a.y - o.y) * (b.x - o.x); };
    std::vector<Vec2> h(2 * p.size());
    std::size_t k = 0;
    for (std::size_t i = 0; i < p.size(); ++i) { while (k >= 2 && cross(h[k - 2], h[k - 1], p[i]) <= 0) --k; h[k++] = p[i]; }
    for (std::size_t i = p.size() - 1, t = k + 1; i > 0; --i) { while (k >= t && cross(h[k - 2], h[k - 1], p[i - 1]) <= 0) --k; h[k++] = p[i - 1]; }
    h.resize(k - 1);
    return h;
}

template <class F> void ForCorners(PolyMesh& pm, const std::vector<int>& faces, F fn) {
    for (int f : faces) if (HasUV(pm.faces[f])) for (Vec2& t : pm.faces[f].uvs) fn(t);
}

double SurfaceArea(const PolyMesh& pm, const std::vector<int>& faces) {
    double a = 0;
    for (int f : faces) {
        const std::vector<int> t = pm.TriangulateFace(f);
        const auto& vs = pm.faces[f].verts;
        for (std::size_t k = 0; k + 2 < t.size(); k += 3)
            a += 0.5 * Vec3::Cross(pm.verts[vs[t[k + 1]]] - pm.verts[vs[t[k]]], pm.verts[vs[t[k + 2]]] - pm.verts[vs[t[k]]]).Magnitude();
    }
    return a;
}
double UVArea(const PolyMesh& pm, const std::vector<int>& faces) {
    double a = 0;
    for (int f : faces) {
        if (!HasUV(pm.faces[f])) continue;
        const std::vector<int> t = pm.TriangulateFace(f);
        const auto& uv = pm.faces[f].uvs;
        for (std::size_t k = 0; k + 2 < t.size(); k += 3) a += std::fabs(TriArea2D(uv[t[k]], uv[t[k + 1]], uv[t[k + 2]]));
    }
    return a;
}

void Pack(PolyMesh& pm, std::vector<std::vector<int>> islands, float margin, bool rescale) {
    std::vector<IslandBox> boxes;
    for (auto& faces : islands) {
        std::vector<Vec2> pts;
        for (int f : faces) if (HasUV(pm.faces[f])) for (const Vec2& t : pm.faces[f].uvs) pts.push_back(t);
        if (pts.empty()) continue;
        if (rescale) {
            const double a3 = SurfaceArea(pm, faces), a2 = UVArea(pm, faces);
            if (a2 > 1e-20 && a3 > 1e-20) {
                const float s = (float)std::sqrt(a3 / a2);
                ForCorners(pm, faces, [&](Vec2& t) { t = t * s; });
                for (Vec2& p : pts) p = p * s;
            }
        }
        // Turn the island to its smallest bounding box (one hull edge lies on a side), lying flat.
        const std::vector<Vec2> hull = Hull(pts);
        double bestA = 1e300, bestAng = 0;
        for (std::size_t i = 0; i < hull.size(); ++i) {
            const Vec2 d = hull[(i + 1) % hull.size()] - hull[i];
            const double ang = -std::atan2((double)d.y, (double)d.x);
            const double cs = std::cos(ang), sn = std::sin(ang);
            double x0 = 1e300, x1 = -1e300, y0 = 1e300, y1 = -1e300;
            for (const Vec2& p : hull) {
                const double x = p.x * cs - p.y * sn, y = p.x * sn + p.y * cs;
                x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
            }
            double ang2 = ang;
            if (y1 - y0 > x1 - x0) ang2 += kPi / 2;   // wider than tall
            const double area = (x1 - x0) * (y1 - y0);
            if (area < bestA - 1e-12) { bestA = area; bestAng = ang2; }
        }
        const double cs = std::cos(bestAng), sn = std::sin(bestAng);
        double x0 = 1e300, x1 = -1e300, y0 = 1e300, y1 = -1e300;
        ForCorners(pm, faces, [&](Vec2& t) {
            const double x = t.x * cs - t.y * sn, y = t.x * sn + t.y * cs;
            t = Vec2{(float)x, (float)y};
            x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
        });
        ForCorners(pm, faces, [&](Vec2& t) { t = Vec2{(float)(t.x - x0), (float)(t.y - y0)}; });
        IslandBox b;
        b.faces = faces;
        b.w = std::max(1e-9, x1 - x0);
        b.h = std::max(1e-9, y1 - y0);
        boxes.push_back(std::move(b));
    }
    if (boxes.empty()) return;
    std::sort(boxes.begin(), boxes.end(), [](const IslandBox& a, const IslandBox& b) { return a.h > b.h; });
    // Shelf packing: try a range of shelf widths, keep the one with the smallest square.
    auto shelf = [&](double width, double pad, bool place) {
        double x = pad, y = pad, rowH = 0, usedW = 0;
        for (IslandBox& b : boxes) {
            if (x > pad && x + b.w + pad > width) { y += rowH + pad; x = pad; rowH = 0; }
            if (place) { b.x = x; b.y = y; }
            x += b.w + pad;
            usedW = std::max(usedW, x);
            rowH = std::max(rowH, b.h);
        }
        return std::max(usedW, y + rowH + pad);
    };
    auto best = [&](double pad) {
        double total = 0, widest = 0;
        for (const IslandBox& b : boxes) { total += (b.w + pad) * (b.h + pad); widest = std::max(widest, b.w + 2 * pad); }
        double bestW = widest, bestSide = 1e300;
        for (int k = 0; k <= 24; ++k) {
            const double width = std::max(widest, std::sqrt(total) * (0.8 + 0.06 * k));
            const double side = shelf(width, pad, false);
            if (side < bestSide) { bestSide = side; bestW = width; }
        }
        return std::make_pair(bestW, bestSide);
    };
    const auto first = best(0.0);
    const double pad = std::clamp((double)margin, 0.0, 0.2) * first.second;
    const auto chosen = best(pad);
    const double side = shelf(chosen.first, pad, true);
    const double inv = side > 1e-20 ? 1.0 / side : 1.0;
    for (const IslandBox& b : boxes)
        ForCorners(pm, b.faces, [&](Vec2& t) { t = Vec2{(float)((t.x + b.x) * inv), (float)((t.y + b.y) * inv)}; });
}

} // namespace

std::vector<std::vector<int>> SeamIslands(const PolyMesh& pm, const std::vector<int>& faces) {
    return Group(pm, FaceSet(pm, faces), [&](const EdgeKey& e, int, int) { return !pm.IsSeam(e.first, e.second); });
}

std::vector<std::vector<int>> UVIslands(const PolyMesh& pm, const std::vector<int>& faces) {
    return Group(pm, FaceSet(pm, faces), [&](const EdgeKey& e, int f, int g) {
        const PolyFace &A = pm.faces[f], &B = pm.faces[g];
        if (!HasUV(A) || !HasUV(B)) return false;
        for (int v : {e.first, e.second}) {
            const int ka = CornerOf(A, v), kb = CornerOf(B, v);
            if (ka < 0 || kb < 0 || !SameUV(A.uvs[ka], B.uvs[kb])) return false;
        }
        return true;
    });
}

bool Unwrap(PolyMesh& pm, const std::vector<int>& faces, const UnwrapOptions& opt, UnwrapReport* report) {
    UnwrapReport rep;
    const std::vector<int> set = FaceSet(pm, faces);
    if (set.empty()) { if (report) *report = rep; return false; }
    std::vector<std::vector<int>> done;
    for (const std::vector<int>& island : SeamIslands(pm, set)) {
        if (opt.cutClosed) {
            const int cut = OpenChart(pm, island);
            if (cut > 0) { ++rep.openedClosed; rep.seamsAdded += cut; }
        }
        const Chart chart = BuildChart(pm, island);
        std::vector<Vec2> uv;
        if (!SolveChart(pm, chart, uv)) ++rep.projected;   // uv holds the planar projection
        for (int fi = 0; fi < (int)island.size(); ++fi) {
            PolyFace& f = pm.faces[island[fi]];
            f.uvs.resize(f.verts.size());
            for (int k = 0; k < (int)f.verts.size(); ++k) f.uvs[k] = uv[chart.cornerVert[chart.Corner(fi, k)]];
        }
        ++rep.islands;
        done.push_back(island);
    }
    if (opt.pack) Pack(pm, done, opt.margin, true);
    if (report) *report = rep;
    return true;
}

int MarkSeamsByAngle(PolyMesh& pm, float degrees, const std::vector<int>& faces) {
    const std::vector<int> set = FaceSet(pm, faces);
    const double cosLimit = std::cos(std::clamp((double)degrees, 0.0, 180.0) * kPi / 180.0);
    int added = 0;
    for (const auto& [e, fs] : EdgeFaces(pm, set)) {
        if (fs.size() != 2 || pm.IsSeam(e.first, e.second)) continue;
        if (Vec3::Dot(pm.FaceNormal(fs[0]), pm.FaceNormal(fs[1])) <= cosLimit + 1e-6) { pm.SetSeam(e.first, e.second, true); ++added; }
    }
    return added;
}

void PackUVIslands(PolyMesh& pm, const std::vector<int>& faces, float margin, bool rescale) {
    std::vector<std::vector<int>> islands;
    for (auto& is : UVIslands(pm, faces)) {
        std::vector<int> withUV;
        for (int f : is) if (HasUV(pm.faces[f])) withUV.push_back(f);
        if (!withUV.empty()) islands.push_back(std::move(withUV));
    }
    Pack(pm, islands, margin, rescale);
}

float UVAngleDistortion(const PolyMesh& pm, const std::vector<int>& faces) {
    double sum = 0;
    int count = 0;
    auto angle = [](double ax, double ay, double bx, double by) {
        const double la = std::sqrt(ax * ax + ay * ay), lb = std::sqrt(bx * bx + by * by);
        if (la < 1e-20 || lb < 1e-20) return -1.0;
        return std::acos(std::clamp((ax * bx + ay * by) / (la * lb), -1.0, 1.0));
    };
    for (int f : FaceSet(pm, faces)) {
        const PolyFace& F = pm.faces[f];
        if (!HasUV(F)) continue;
        const std::vector<int> t = pm.TriangulateFace(f);
        for (std::size_t k = 0; k + 2 < t.size(); k += 3)
            for (int c = 0; c < 3; ++c) {
                const int i0 = t[k + c], i1 = t[k + (c + 1) % 3], i2 = t[k + (c + 2) % 3];
                const Vec3 p0 = pm.verts[F.verts[i0]], d1 = pm.verts[F.verts[i1]] - p0, d2 = pm.verts[F.verts[i2]] - p0;
                const double l1 = d1.Magnitude(), l2 = d2.Magnitude();
                if (l1 < 1e-12 || l2 < 1e-12) continue;
                const double a3 = std::acos(std::clamp((double)Vec3::Dot(d1, d2) / (l1 * l2), -1.0, 1.0));
                const Vec2 u0 = F.uvs[i0], u1 = F.uvs[i1], u2 = F.uvs[i2];
                const double a2 = angle(u1.x - u0.x, u1.y - u0.y, u2.x - u0.x, u2.y - u0.y);
                if (a2 < 0) { sum += a3; ++count; continue; }   // collapsed in UV: count the whole angle
                sum += std::fabs(a3 - a2);
                ++count;
            }
    }
    return count ? (float)(sum / count * 180.0 / kPi) : 0.0f;
}

} // namespace okay::modeling
