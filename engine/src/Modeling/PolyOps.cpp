#include "okay/Modeling/PolyOps.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace okay::modeling {

namespace {
std::pair<int, int> Key(int a, int b) { return a < b ? std::make_pair(a, b) : std::make_pair(b, a); }

int CornerIn(const PolyFace& f, int v) {
    for (int i = 0; i < f.Size(); ++i) if (f.verts[i] == v) return i;
    return -1;
}
// Does face f run u -> v (v right after u)?
bool Runs(const PolyFace& f, int u, int v) {
    const int i = CornerIn(f, u);
    return i >= 0 && f.verts[(i + 1) % f.Size()] == v;
}
int Next(const PolyFace& f, int v) { const int i = CornerIn(f, v); return i < 0 ? -1 : f.verts[(i + 1) % f.Size()]; }
int Prev(const PolyFace& f, int v) { const int i = CornerIn(f, v); return i < 0 ? -1 : f.verts[(i + f.Size() - 1) % f.Size()]; }
bool HasUV(const PolyFace& f) { return !f.uvs.empty() && f.uvs.size() == f.verts.size(); }
Vec2 UVOf(const PolyFace& f, int v) {
    const int i = CornerIn(f, v);
    return (i >= 0 && HasUV(f)) ? f.uvs[i] : Vec2{0, 0};
}
Vec2 Lerp2(const Vec2& a, const Vec2& b, float t) { return Vec2{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}; }
Vec3 Lerp3(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }

// Remove repeated corners and back-and-forth spikes (x v x) left by merging faces.
void CleanFace(PolyFace& f) {
    const bool uv = HasUV(f);
    bool changed = true;
    while (changed && f.Size() >= 3) {
        changed = false;
        for (int i = 0; i < f.Size() && f.Size() >= 3; ++i) {
            const int n = f.Size();
            const int j = (i + 1) % n;
            if (f.verts[i] == f.verts[j]) {   // repeated corner
                f.verts.erase(f.verts.begin() + j);
                if (uv) f.uvs.erase(f.uvs.begin() + j);
                changed = true; break;
            }
            const int k = (i + 2) % n;
            if (n > 3 && f.verts[i] == f.verts[k]) {   // spike i -> j -> i: drop j and the second i
                const int hi = std::max(j, k), lo = std::min(j, k);
                f.verts.erase(f.verts.begin() + hi); f.verts.erase(f.verts.begin() + lo);
                if (uv) { f.uvs.erase(f.uvs.begin() + hi); f.uvs.erase(f.uvs.begin() + lo); }
                changed = true; break;
            }
        }
    }
}

// Merge the two faces on edge a-b into the first. False if they aren't two different faces.
bool MergeAcross(PolyMesh& pm, int a, int b) {
    int f0 = -1, f1 = -1;
    for (int f = 0; f < (int)pm.faces.size(); ++f) {
        if (Runs(pm.faces[f], a, b)) f0 = f;
        else if (Runs(pm.faces[f], b, a)) f1 = f;
    }
    if (f0 < 0 || f1 < 0 || f0 == f1) return false;
    const PolyFace& A = pm.faces[f0];
    const PolyFace& B = pm.faces[f1];
    const bool uv = HasUV(A) && HasUV(B);
    PolyFace m;
    m.material = A.material; m.hasColor = A.hasColor; m.color = A.color;
    // A from b round to a, then B from just after a round to just before b.
    const int ib = CornerIn(A, b), n = A.Size();
    for (int k = 0; k < n; ++k) {
        const int c = (ib + k) % n;
        m.verts.push_back(A.verts[c]);
        if (uv) m.uvs.push_back(A.uvs[c]);
    }
    const int ia = CornerIn(B, a), nb = B.Size();
    for (int k = 1; k < nb - 1; ++k) {
        const int c = (ia + k) % nb;
        m.verts.push_back(B.verts[c]);
        if (uv) m.uvs.push_back(B.uvs[c]);
    }
    CleanFace(m);
    pm.faces[f0] = m;
    pm.faces.erase(pm.faces.begin() + f1);
    return true;
}

// Take vertex v out of every face (faces left with fewer than 3 corners go).
void RemoveVertexFromFaces(PolyMesh& pm, int v) {
    for (int f = (int)pm.faces.size() - 1; f >= 0; --f) {
        PolyFace& F = pm.faces[f];
        const bool uv = HasUV(F);
        for (int i = F.Size() - 1; i >= 0; --i)
            if (F.verts[i] == v) { F.verts.erase(F.verts.begin() + i); if (uv) F.uvs.erase(F.uvs.begin() + i); }
        CleanFace(F);
        if (F.Size() < 3) pm.faces.erase(pm.faces.begin() + f);
    }
}

// The UV a vertex has in any face (in edit mode every corner of a vertex shares it).
bool AnyUV(const PolyMesh& pm, int v, Vec2& out) {
    for (const PolyFace& f : pm.faces)
        if (HasUV(f)) { const int i = CornerIn(f, v); if (i >= 0) { out = f.uvs[i]; return true; } }
    return false;
}
bool MeshHasUV(const PolyMesh& pm) {
    for (const PolyFace& f : pm.faces) if (HasUV(f)) return true;
    return false;
}
} // namespace

// ---------------------------------------------------------------------------
PolyTopology::PolyTopology(const PolyMesh& pm) : m_pm(pm), m_nbr(pm.verts.size()) {
    for (int f = 0; f < (int)pm.faces.size(); ++f) {
        const auto& v = pm.faces[f].verts;
        for (int i = 0; i < (int)v.size(); ++i) {
            const int a = v[i], b = v[(i + 1) % v.size()];
            if (a == b || a < 0 || b < 0 || a >= (int)pm.verts.size() || b >= (int)pm.verts.size()) continue;
            auto& list = m_edgeFaces[Key(a, b)];
            if (list.empty()) { m_nbr[a].push_back(b); m_nbr[b].push_back(a); }
            if (std::find(list.begin(), list.end(), f) == list.end()) list.push_back(f);
        }
    }
}
const std::vector<int>& PolyTopology::FacesOf(int a, int b) const {
    auto it = m_edgeFaces.find(Key(a, b));
    return it == m_edgeFaces.end() ? m_none : it->second;
}
const std::vector<int>& PolyTopology::Neighbours(int v) const {
    return (v >= 0 && v < (int)m_nbr.size()) ? m_nbr[v] : m_none;
}
bool PolyTopology::IsEdge(int a, int b) const { return m_edgeFaces.count(Key(a, b)) != 0; }
int PolyTopology::Corner(int f, int v) const {
    return (f >= 0 && f < (int)m_pm.faces.size()) ? CornerIn(m_pm.faces[f], v) : -1;
}

// ---------------------------------------------------------------------------
EdgeList EdgeLoop(const PolyMesh& pm, int a, int b) {
    PolyTopology T(pm);
    if (!T.IsEdge(a, b)) return {};
    const bool border = T.IsBoundary(a, b);
    std::set<std::pair<int, int>> seen{Key(a, b)};
    auto walk = [&](int prev, int cur) {
        EdgeList out;
        for (int guard = 0; guard < 100000; ++guard) {
            int next = -1;
            if (border) {
                for (int n : T.Neighbours(cur))
                    if (n != prev && T.IsBoundary(cur, n)) { if (next >= 0) return out; next = n; }
            } else {
                // A 4-way vertex: the way on is the one edge that shares no face with the way in.
                if (T.Neighbours(cur).size() != 4) return out;
                const std::vector<int>& in = T.FacesOf(prev, cur);
                if (in.size() != 2) return out;
                for (int n : T.Neighbours(cur)) {
                    if (n == prev) continue;
                    const std::vector<int>& fs = T.FacesOf(cur, n);
                    if (fs.size() != 2) continue;
                    bool shares = false;
                    for (int f : fs) if (f == in[0] || f == in[1]) shares = true;
                    if (shares) continue;
                    if (next >= 0) return out;
                    next = n;
                }
            }
            if (next < 0 || !seen.insert(Key(cur, next)).second) return out;
            out.push_back({cur, next});
            prev = cur; cur = next;
        }
        return out;
    };
    EdgeList fwd = walk(a, b);
    EdgeList bwd = walk(b, a);
    EdgeList out;
    for (auto it = bwd.rbegin(); it != bwd.rend(); ++it) out.push_back({it->second, it->first});
    out.push_back({a, b});
    out.insert(out.end(), fwd.begin(), fwd.end());
    return out;
}

namespace {
// The ring through a-b, with edges oriented so each edge's first vertex sits
// next to the previous edge's first vertex, and the quads between them
// (quads[i] joins edges[i] and edges[i + 1], wrapping when closed).
struct Ring { EdgeList edges; std::vector<int> quads; bool closed = false; };
Ring RingWalk(const PolyMesh& pm, const PolyTopology& T, int a, int b) {
    Ring r;
    if (!T.IsEdge(a, b)) return r;
    const std::vector<int> start = T.FacesOf(a, b);
    std::set<std::pair<int, int>> seen{Key(a, b)};
    std::set<int> used;
    // The edge facing (s, t) across quad f, oriented the same way.
    auto opposite = [&](int f, int s, int t) {
        const PolyFace& F = pm.faces[f];
        const int ns = Next(F, s) == t ? Prev(F, s) : Next(F, s);
        const int nt = Next(F, t) == s ? Prev(F, t) : Next(F, t);
        return std::make_pair(ns, nt);
    };
    auto walk = [&](int face, EdgeList& edges, std::vector<int>& quads) {
        int s = a, t = b;
        for (int guard = 0; guard < 100000 && face >= 0; ++guard) {
            if (pm.faces[face].Size() != 4 || used.count(face)) return;
            const auto o = opposite(face, s, t);
            used.insert(face);
            quads.push_back(face);
            if (Key(o.first, o.second) == Key(a, b)) { r.closed = true; return; }
            if (!seen.insert(Key(o.first, o.second)).second) { quads.pop_back(); return; }
            edges.push_back(o);
            int nextFace = -1;
            const std::vector<int>& fs = T.FacesOf(o.first, o.second);
            if (fs.size() != 2) return;
            for (int g : fs) if (g != face) nextFace = g;
            face = nextFace; s = o.first; t = o.second;
        }
    };
    EdgeList fe, be;
    std::vector<int> fq, bq;
    if (!start.empty()) walk(start[0], fe, fq);
    if (!r.closed && start.size() == 2) walk(start[1], be, bq);
    for (auto it = be.rbegin(); it != be.rend(); ++it) r.edges.push_back(*it);
    r.edges.push_back({a, b});
    r.edges.insert(r.edges.end(), fe.begin(), fe.end());
    for (auto it = bq.rbegin(); it != bq.rend(); ++it) r.quads.push_back(*it);
    r.quads.insert(r.quads.end(), fq.begin(), fq.end());
    return r;
}
} // namespace

EdgeList EdgeRing(const PolyMesh& pm, int a, int b) {
    PolyTopology T(pm);
    return RingWalk(pm, T, a, b).edges;
}

std::vector<EdgeChain> OrderChains(const EdgeList& edges) {
    std::map<int, std::vector<int>> adj;
    std::set<std::pair<int, int>> left;
    for (const auto& e : edges) {
        if (e.first == e.second || !left.insert(Key(e.first, e.second)).second) continue;
        adj[e.first].push_back(e.second);
        adj[e.second].push_back(e.first);
    }
    std::vector<EdgeChain> out;
    auto take = [&](int a, int b) { return left.erase(Key(a, b)) != 0; };
    auto extend = [&](std::vector<int>& chain) {
        for (;;) {
            const int cur = chain.back();
            int next = -1;
            for (int n : adj[cur]) if (left.count(Key(cur, n))) { next = n; break; }
            if (next < 0) return;
            take(cur, next);
            chain.push_back(next);
        }
    };
    // Open chains first (start at an end), then whatever is left are loops.
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& kv : adj) {
            if (pass == 0 && kv.second.size() != 1) continue;
            for (int n : kv.second) {
                if (!left.count(Key(kv.first, n))) continue;
                EdgeChain c;
                c.verts = {kv.first, n};
                take(kv.first, n);
                extend(c.verts);
                if (c.verts.size() > 2 && c.verts.front() == c.verts.back()) { c.verts.pop_back(); c.closed = true; }
                out.push_back(c);
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
EdgeList LoopCut(PolyMesh& pm, int a, int b, int cuts, float position) {
    if (cuts < 1) cuts = 1;
    PolyTopology T(pm);
    const Ring r = RingWalk(pm, T, a, b);
    if (r.edges.empty() || r.quads.empty()) return {};
    const int n = (int)r.edges.size();
    const float pos = std::clamp(position, 0.01f, 0.99f);
    auto frac = [&](int k) { return cuts == 1 ? pos : (float)k / (float)(cuts + 1); };
    // New vertices along each ring edge, from its first vertex to its second.
    std::vector<std::vector<int>> along(n);
    for (int i = 0; i < n; ++i) {
        const Vec3 A = pm.verts[r.edges[i].first], B = pm.verts[r.edges[i].second];
        along[i].push_back(r.edges[i].first);
        for (int k = 1; k <= cuts; ++k) { along[i].push_back((int)pm.verts.size()); pm.verts.push_back(Lerp3(A, B, frac(k))); }
        along[i].push_back(r.edges[i].second);
    }
    auto fracOf = [&](int j) { return j == 0 ? 0.0f : j == cuts + 1 ? 1.0f : frac(j); };
    const std::set<int> ringQuads(r.quads.begin(), r.quads.end());
    // Faces on the ring edges that aren't ring quads (the ends of an open ring)
    // get the new vertices on their side, so the mesh stays connected.
    for (int i = 0; i < n; ++i) {
        const int s = r.edges[i].first, t = r.edges[i].second;
        for (int g : T.FacesOf(s, t)) {
            if (ringQuads.count(g)) continue;
            PolyFace& G = pm.faces[g];
            const bool uv = HasUV(G);
            const bool st = Runs(G, s, t);
            const int at = CornerIn(G, st ? s : t);
            const Vec2 us = UVOf(G, s), ut = UVOf(G, t);
            std::vector<int> ins; std::vector<Vec2> insUV;
            for (int k = 1; k <= cuts; ++k) {
                const int j = st ? k : cuts + 1 - k;
                ins.push_back(along[i][j]);
                insUV.push_back(Lerp2(us, ut, fracOf(j)));
            }
            G.verts.insert(G.verts.begin() + at + 1, ins.begin(), ins.end());
            if (uv) G.uvs.insert(G.uvs.begin() + at + 1, insUV.begin(), insUV.end());
        }
    }
    // Each ring quad becomes cuts + 1 quads.
    const int nq = (int)r.quads.size();
    for (int q = 0; q < nq; ++q) {
        const int i = q, i2 = (q + 1) % n;
        const PolyFace Q = pm.faces[r.quads[q]];
        const bool uv = HasUV(Q);
        const bool fwd = Runs(Q, r.edges[i].first, r.edges[i].second);
        auto uvAt = [&](int edge, int j) {
            return Lerp2(UVOf(Q, r.edges[edge].first), UVOf(Q, r.edges[edge].second), fracOf(j));
        };
        for (int j = 0; j <= cuts; ++j) {
            PolyFace F;
            F.material = Q.material; F.hasColor = Q.hasColor; F.color = Q.color;
            const int ja = fwd ? j : j + 1, jb = fwd ? j + 1 : j;
            F.verts = {along[i][ja], along[i][jb], along[i2][jb], along[i2][ja]};
            if (uv) F.uvs = {uvAt(i, ja), uvAt(i, jb), uvAt(i2, jb), uvAt(i2, ja)};
            if (j == 0) pm.faces[r.quads[q]] = F; else pm.faces.push_back(F);
        }
    }
    EdgeList made;
    for (int k = 1; k <= cuts; ++k)
        for (int q = 0; q < nq; ++q) made.push_back({along[q][k], along[(q + 1) % n][k]});
    return made;
}

// ---------------------------------------------------------------------------
int EdgeSlide(PolyMesh& pm, const EdgeList& loop, float amount, const std::vector<Vec3>* from) {
    const std::vector<Vec3> start = from ? *from : pm.verts;
    if (start.size() < pm.verts.size()) return 0;
    const float t = std::clamp(std::fabs(amount), 0.0f, 1.0f);
    const bool left = amount > 0;
    std::set<int> onLoop;
    for (const auto& e : loop) { onLoop.insert(e.first); onLoop.insert(e.second); }
    // The face that runs u -> v.
    auto faceRunning = [&](int u, int v) -> const PolyFace* {
        for (const PolyFace& f : pm.faces) if (Runs(f, u, v)) return &f;
        return nullptr;
    };
    struct Move { int v; int rail; };
    std::vector<Move> moves;
    for (const EdgeChain& c : OrderChains(loop)) {
        const int m = (int)c.verts.size();
        for (int k = 0; k < m; ++k) {
            const int v = c.verts[k];
            int rail = -1;
            const bool hasAfter = k + 1 < m || c.closed;
            if (hasAfter) {
                const int w = c.verts[(k + 1) % m];
                const PolyFace* f = left ? faceRunning(v, w) : faceRunning(w, v);
                if (f) rail = left ? Prev(*f, v) : Next(*f, v);
            } else if (k > 0) {
                const int u = c.verts[k - 1];
                const PolyFace* f = left ? faceRunning(u, v) : faceRunning(v, u);
                if (f) rail = left ? Next(*f, v) : Prev(*f, v);
            }
            if (rail >= 0 && !onLoop.count(rail)) moves.push_back({v, rail});
        }
    }
    for (const Move& mv : moves) pm.verts[mv.v] = Lerp3(start[mv.v], start[mv.rail], t);
    return (int)moves.size();
}

// ---------------------------------------------------------------------------
int DissolveEdges(PolyMesh& pm, const EdgeList& edges, bool cleanVerts) {
    int done = 0;
    std::set<int> ends;
    for (const auto& e : edges)
        if (MergeAcross(pm, e.first, e.second)) { ++done; ends.insert(e.first); ends.insert(e.second); }
    if (cleanVerts) {
        // A vertex left with two edges sits in the middle of a side: take it out.
        for (int v : ends) {
            PolyTopology T(pm);
            if (T.Neighbours(v).size() == 2) RemoveVertexFromFaces(pm, v);
        }
    }
    return done;
}

int DissolveVerts(PolyMesh& pm, const std::vector<int>& verts) {
    int done = 0;
    for (int v : verts) {
        if (v < 0 || v >= (int)pm.verts.size()) continue;
        bool used = false;
        for (const PolyFace& f : pm.faces) if (CornerIn(f, v) >= 0) used = true;
        if (!used) continue;
        for (int guard = 0; guard < 1000; ++guard) {
            PolyTopology T(pm);
            bool merged = false;
            for (int n : T.Neighbours(v))
                if (MergeAcross(pm, v, n) || MergeAcross(pm, n, v)) { merged = true; break; }
            if (!merged) break;
        }
        RemoveVertexFromFaces(pm, v);
        ++done;
    }
    return done;
}

// ---------------------------------------------------------------------------
int BridgeEdgeLoops(PolyMesh& pm, const EdgeList& edges, std::string* error) {
    auto fail = [&](const char* why) { if (error) *error = why; return 0; };
    PolyTopology T(pm);
    for (const auto& e : edges)
        if (!T.IsBoundary(e.first, e.second)) return fail("Bridge needs edges on an open border of the mesh (select the rims of two holes or tube ends).");
    std::vector<EdgeChain> chains = OrderChains(edges);
    if (chains.size() != 2) return fail("Select exactly two separate edge loops to bridge.");
    EdgeChain A = chains[0], B = chains[1];
    if (A.closed != B.closed) return fail("Both loops need to be closed, or both open.");
    if (A.verts.size() != B.verts.size()) return fail("Both loops need the same number of edges.");
    for (int v : A.verts) if (std::find(B.verts.begin(), B.verts.end(), v) != B.verts.end())
        return fail("The two loops share a vertex.");
    const int n = (int)A.verts.size();
    // Orient each loop so the new faces run its border edges the other way round
    // from the faces already there (consistent normals).
    auto runs = [&](int u, int v) { for (const PolyFace& f : pm.faces) if (Runs(f, u, v)) return true; return false; };
    if (runs(A.verts[0], A.verts[1])) std::reverse(A.verts.begin(), A.verts.end());
    if (runs(B.verts[1], B.verts[0])) std::reverse(B.verts.begin(), B.verts.end());
    int shift = 0;
    if (A.closed) {
        float best = 1e30f;
        for (int r = 0; r < n; ++r) {
            float d = 0;
            for (int i = 0; i < n; ++i) d += (pm.verts[A.verts[i]] - pm.verts[B.verts[(i + r) % n]]).SqrMagnitude();
            if (d < best) { best = d; shift = r; }
        }
    }
    const bool uv = MeshHasUV(pm);
    const int faces = A.closed ? n : n - 1;
    for (int i = 0; i < faces; ++i) {
        const int a0 = A.verts[i], a1 = A.verts[(i + 1) % n];
        const int b0 = B.verts[(i + shift) % n], b1 = B.verts[(i + 1 + shift) % n];
        PolyFace f;
        f.verts = {a0, a1, b1, b0};
        if (uv) for (int v : f.verts) { Vec2 t{0, 0}; AnyUV(pm, v, t); f.uvs.push_back(t); }
        pm.faces.push_back(f);
    }
    return faces;
}

// ---------------------------------------------------------------------------
int SplitEdge(PolyMesh& pm, int a, int b, float t) {
    bool any = false;
    for (const PolyFace& f : pm.faces) if (Runs(f, a, b) || Runs(f, b, a)) any = true;
    if (!any) return -1;
    const int nv = (int)pm.verts.size();
    pm.verts.push_back(Lerp3(pm.verts[a], pm.verts[b], t));
    for (PolyFace& f : pm.faces) {
        const bool ab = Runs(f, a, b), ba = Runs(f, b, a);
        if (!ab && !ba) continue;
        const int at = CornerIn(f, ab ? a : b);
        if (HasUV(f)) {
            const Vec2 uv = Lerp2(UVOf(f, a), UVOf(f, b), t);
            f.uvs.insert(f.uvs.begin() + at + 1, uv);
        }
        f.verts.insert(f.verts.begin() + at + 1, nv);
    }
    return nv;
}

int SplitFace(PolyMesh& pm, int f, int u, int v) {
    if (f < 0 || f >= (int)pm.faces.size()) return -1;
    const PolyFace F = pm.faces[f];
    const int i = CornerIn(F, u), j = CornerIn(F, v), n = F.Size();
    if (i < 0 || j < 0 || i == j || (i + 1) % n == j || (j + 1) % n == i) return -1;
    const bool uv = HasUV(F);
    auto part = [&](int from, int to) {
        PolyFace P;
        P.material = F.material; P.hasColor = F.hasColor; P.color = F.color;
        for (int k = from;; k = (k + 1) % n) {
            P.verts.push_back(F.verts[k]);
            if (uv) P.uvs.push_back(F.uvs[k]);
            if (k == to) break;
        }
        return P;
    };
    pm.faces[f] = part(i, j);
    pm.faces.push_back(part(j, i));
    return (int)pm.faces.size() - 1;
}

EdgeList KnifeCut(PolyMesh& pm, const std::vector<KnifePoint>& points, std::string* error) {
    auto fail = [&](const std::string& why) { if (error) *error = why; return EdgeList{}; };
    if (points.size() < 2) return fail("The knife needs at least two points.");
    PolyMesh work = pm;
    // Turn every point into a vertex (splitting the edge it's on).
    std::vector<int> vs;
    for (const KnifePoint& p : points) {
        if (p.a < 0 || p.a >= (int)work.verts.size()) return fail("A knife point is off the mesh.");
        if (p.b < 0 || p.t <= 1e-4f) { vs.push_back(p.a); continue; }
        if (p.b >= (int)work.verts.size()) return fail("A knife point is off the mesh.");
        if (p.t >= 1.0f - 1e-4f) { vs.push_back(p.b); continue; }
        const Vec3 A = pm.verts[p.a], B = pm.verts[p.b];
        const Vec3 P = Lerp3(A, B, p.t);
        // The edge may already have been cut by an earlier point: walk the pieces from a to b.
        int cur = p.a, made = -1;
        for (int guard = 0; guard < 1000 && made < 0; ++guard) {
            PolyTopology T(work);
            if (T.IsEdge(cur, p.b)) {
                const Vec3 C = work.verts[cur], D = work.verts[p.b];
                const float len = (D - C).Magnitude();
                made = SplitEdge(work, cur, p.b, len > 1e-9f ? (P - C).Magnitude() / len : 0.5f);
                break;
            }
            int step = -1;
            for (int nb : T.Neighbours(cur)) {
                const Vec3 Q = work.verts[nb];
                const Vec3 ab = B - A;
                const float L2 = ab.SqrMagnitude();
                const float s = L2 > 0 ? Vec3::Dot(Q - A, ab) / L2 : 0;
                const Vec3 onLine = A + ab * s;
                if ((Q - onLine).SqrMagnitude() > 1e-10f * std::max(1.0f, L2)) continue;
                const float sc = L2 > 0 ? Vec3::Dot(work.verts[cur] - A, ab) / L2 : 0;
                if (s <= sc + 1e-6f || s > 1.0f) continue;
                if (s >= p.t) {   // P lies between cur and nb
                    const float len = (Q - work.verts[cur]).Magnitude();
                    made = SplitEdge(work, cur, nb, len > 1e-9f ? (P - work.verts[cur]).Magnitude() / len : 0.5f);
                } else step = nb;
                break;
            }
            if (made >= 0) break;
            if (step < 0) return fail("A knife point's edge was lost.");
            cur = step;
        }
        if (made < 0) return fail("A knife point's edge was lost.");
        vs.push_back(made);
    }
    EdgeList cut;
    for (std::size_t k = 0; k + 1 < vs.size(); ++k) {
        const int u = vs[k], v = vs[k + 1];
        if (u == v) continue;
        PolyTopology T(work);
        if (T.IsEdge(u, v)) { cut.push_back({u, v}); continue; }
        int face = -1;
        for (int f = 0; f < (int)work.faces.size() && face < 0; ++f)
            if (CornerIn(work.faces[f], u) >= 0 && CornerIn(work.faces[f], v) >= 0) face = f;
        if (face < 0) return fail("Knife points " + std::to_string(k + 1) + " and " + std::to_string(k + 2) + " aren't on the same face.");
        if (SplitFace(work, face, u, v) < 0) return fail("Couldn't split a face for the knife.");
        cut.push_back({u, v});
    }
    pm = std::move(work);
    return cut;
}

// ---------------------------------------------------------------------------
int BevelEdges(PolyMesh& pm, const EdgeList& edgesIn, float width, std::string* error) {
    auto fail = [&](const std::string& why) { if (error) *error = why; return 0; };
    if (width <= 0) return fail("Bevel width must be more than 0.");
    PolyTopology T(pm);
    std::set<std::pair<int, int>> sel;
    for (const auto& e : edgesIn) {
        if (!T.IsEdge(e.first, e.second)) continue;
        if (T.FacesOf(e.first, e.second).size() != 2) return fail("Bevel needs edges with a face on both sides (not on an open border).");
        sel.insert(Key(e.first, e.second));
    }
    if (sel.empty()) return fail("Select edges to bevel.");
    // The ring of faces round a vertex: spokes s[i] and faces f[i] between s[i] and s[i+1].
    struct Fan { std::vector<int> spokes, faces; };
    std::map<int, Fan> fans;
    for (const auto& e : sel) for (int v : {e.first, e.second}) {
        if (fans.count(v)) continue;
        Fan fan;
        int f0 = -1;
        for (int f = 0; f < (int)pm.faces.size() && f0 < 0; ++f) if (CornerIn(pm.faces[f], v) >= 0) f0 = f;
        int f = f0, p = Prev(pm.faces[f0], v);
        for (int guard = 0; guard < 256; ++guard) {
            fan.spokes.push_back(p);
            fan.faces.push_back(f);
            const int n = Next(pm.faces[f], v);
            int g = -1;
            for (int h : T.FacesOf(v, n)) if (h != f && Runs(pm.faces[h], n, v)) g = h;
            if (g < 0) return fail("Bevel needs vertices with faces all the way round them (no open borders or holes).");
            if (g == f0) break;
            f = g; p = n;
        }
        if ((int)fan.spokes.size() != (int)T.Neighbours(v).size())
            return fail("Bevel needs vertices with faces all the way round them (no open borders or holes).");
        fans[v] = fan;
    }
    auto dir = [&](int v, int u) { return (pm.verts[u] - pm.verts[v]).Normalized(); };
    auto sinBetween = [&](const Vec3& x, const Vec3& y) { return Vec3::Cross(x, y).Magnitude(); };
    // What each (vertex, face) corner turns into, and each vertex's corner patch.
    std::map<std::pair<int, int>, std::vector<int>> rep;
    std::map<std::pair<int, int>, std::vector<Vec2>> repUV;
    std::map<int, std::vector<int>> patch;
    std::map<int, bool> endWithMiddle;   // a chain end with 3+ plain edges: the original vertex stays in the bevel face
    std::vector<Vec3>& V = pm.verts;
    for (auto& kv : fans) {
        const int v = kv.first;
        const Fan& fan = kv.second;
        const int d = (int)fan.spokes.size();
        std::vector<int> K;
        for (int i = 0; i < d; ++i) if (sel.count(Key(v, fan.spokes[i]))) K.push_back(i);
        const int k = (int)K.size();
        std::vector<int> patchList;
        for (int j = 0; j < k; ++j) {
            const int ka = K[j], kb = K[(j + 1) % k];
            const int m = k == 1 ? d - 1 : ((kb - ka - 1) % d + d) % d;
            const Vec3 eA = dir(v, fan.spokes[ka]), eB = dir(v, fan.spokes[kb]);
            auto onSpoke = [&](int spokeIdx, float sinv) {
                const int u = fan.spokes[spokeIdx];
                const float len = (V[u] - V[v]).Magnitude();
                const float t = std::min(width / std::max(sinv, 0.2f), 0.45f * len);
                const int id = (int)V.size();
                V.push_back(V[v] + dir(v, u) * t);
                return std::make_pair(id, len > 1e-9f ? t / len : 0.0f);
            };
            auto faceAt = [&](int q) { return fan.faces[(ka + q) % d]; };
            auto setRep = [&](int q, std::vector<int> ids, std::vector<Vec2> uvs) {
                rep[{v, faceAt(q)}] = std::move(ids); repUV[{v, faceAt(q)}] = std::move(uvs);
            };
            const auto uvF = [&](int face, int vert) { return UVOf(pm.faces[face], vert); };
            if (m == 0) {
                // Two beveled edges on one face: a point inside it, width away from both.
                const int face = faceAt(0);
                const Vec3 sum = eA + eB;
                Vec3 bis;
                float off;
                if (sum.SqrMagnitude() > 1e-8f) {
                    bis = sum.Normalized();
                    const float s = std::sqrt(std::max(0.0f, (1.0f - Vec3::Dot(eA, eB)) * 0.5f));   // sin(theta / 2)
                    off = width / std::max(s, 0.2f);
                } else {
                    bis = Vec3::Cross(pm.FaceNormal(face), eA).Normalized();
                    off = width;
                }
                const float lim = 0.45f * std::min((V[fan.spokes[ka]] - V[v]).Magnitude(), (V[fan.spokes[kb]] - V[v]).Magnitude());
                off = std::min(off, lim / std::max(Vec3::Dot(bis, eA), 0.2f));
                const int id = (int)V.size();
                V.push_back(V[v] + bis * off);
                // UV: write the offset in terms of the two edges.
                const Vec3 a = V[fan.spokes[ka]] - V[v], b = V[fan.spokes[kb]] - V[v], p = V[id] - V[v];
                const float aa = Vec3::Dot(a, a), bb = Vec3::Dot(b, b), ab = Vec3::Dot(a, b);
                const float pa = Vec3::Dot(p, a), pb = Vec3::Dot(p, b), det = aa * bb - ab * ab;
                float al = 0, be = 0;
                if (std::fabs(det) > 1e-12f) { al = (pa * bb - pb * ab) / det; be = (pb * aa - pa * ab) / det; }
                const Vec2 u0 = uvF(face, v), ua = uvF(face, fan.spokes[ka]), ub = uvF(face, fan.spokes[kb]);
                const Vec2 uv{u0.x + (ua.x - u0.x) * al + (ub.x - u0.x) * be, u0.y + (ua.y - u0.y) * al + (ub.y - u0.y) * be};
                setRep(0, {id}, {uv});
                patchList.push_back(id);
            } else if (m == 1) {
                if (k == 1) return fail("Can't bevel an edge ending at a vertex with only two edges.");
                const int sp = (ka + 1) % d;
                const auto P = onSpoke(sp, 0.5f * (sinBetween(eA, dir(v, fan.spokes[sp])) + sinBetween(eB, dir(v, fan.spokes[sp]))));
                for (int q = 0; q <= 1; ++q) {
                    const int face = faceAt(q);
                    setRep(q, {P.first}, {Lerp2(uvF(face, v), uvF(face, fan.spokes[sp]), P.second)});
                }
                patchList.push_back(P.first);
            } else {
                const int s1 = (ka + 1) % d, sm = (ka + m) % d;
                const auto A1 = onSpoke(s1, sinBetween(eA, dir(v, fan.spokes[s1])));
                const auto B1 = onSpoke(sm, sinBetween(eB, dir(v, fan.spokes[sm])));
                auto uvA = [&](int face) { return Lerp2(uvF(face, v), uvF(face, fan.spokes[s1]), A1.second); };
                auto uvB = [&](int face) { return Lerp2(uvF(face, v), uvF(face, fan.spokes[sm]), B1.second); };
                setRep(0, {A1.first}, {uvA(faceAt(0))});
                setRep(m, {B1.first}, {uvB(faceAt(m))});
                if (m == 2) {
                    setRep(1, {A1.first, B1.first}, {uvA(faceAt(1)), uvB(faceAt(1))});
                    patchList.insert(patchList.end(), {A1.first, B1.first});
                } else {
                    setRep(1, {A1.first, v}, {uvA(faceAt(1)), uvF(faceAt(1), v)});
                    setRep(m - 1, {v, B1.first}, {uvF(faceAt(m - 1), v), uvB(faceAt(m - 1))});
                    for (int q = 2; q <= m - 2; ++q) setRep(q, {v}, {uvF(faceAt(q), v)});
                    patchList.insert(patchList.end(), {A1.first, v, B1.first});
                    if (k == 1) endWithMiddle[v] = true;
                }
            }
        }
        if (k >= 2 && patchList.size() >= 3) {
            std::reverse(patchList.begin(), patchList.end());
            patch[v] = patchList;
        }
    }
    // Rebuild the faces with the new corners.
    const std::vector<PolyFace> old = pm.faces;
    for (int f = 0; f < (int)old.size(); ++f) {
        const PolyFace& F = old[f];
        const bool uv = HasUV(F);
        PolyFace N;
        N.material = F.material; N.hasColor = F.hasColor; N.color = F.color;
        for (int c = 0; c < F.Size(); ++c) {
            auto it = rep.find({F.verts[c], f});
            if (it == rep.end()) { N.verts.push_back(F.verts[c]); if (uv) N.uvs.push_back(F.uvs[c]); continue; }
            const std::vector<Vec2>& u = repUV[{F.verts[c], f}];
            for (std::size_t i = 0; i < it->second.size(); ++i) {
                N.verts.push_back(it->second[i]);
                if (uv) N.uvs.push_back(u[i]);
            }
        }
        pm.faces[f] = N;
    }
    const bool anyUV = MeshHasUV(pm);
    auto uvIn = [&](int face, int vert) { return UVOf(pm.faces[face], vert); };
    // A face for each beveled edge.
    for (const auto& e : sel) {
        const int v = e.first, w = e.second;
        const Fan& fv = fans[v];
        const int d = (int)fv.spokes.size();
        int kv = -1;
        for (int i = 0; i < d; ++i) if (fv.spokes[i] == w) kv = i;
        if (kv < 0) continue;
        const int fR = fv.faces[kv], fL = fv.faces[(kv + d - 1) % d];
        const std::vector<int>& rvR = rep[{v, fR}];
        const std::vector<int>& rvL = rep[{v, fL}];
        const std::vector<int>& rwR = rep[{w, fR}];
        const std::vector<int>& rwL = rep[{w, fL}];
        if (rvR.empty() || rvL.empty() || rwR.empty() || rwL.empty()) continue;
        PolyFace B;
        const PolyFace& src = old[fR];
        B.material = src.material; B.hasColor = src.hasColor; B.color = src.color;
        auto add = [&](int vert, int face) { B.verts.push_back(vert); if (anyUV) B.uvs.push_back(uvIn(face, vert)); };
        add(rvR.front(), fR);
        add(rwR.back(), fR);
        if (endWithMiddle.count(w)) add(w, fR);
        add(rwL.front(), fL);
        add(rvL.back(), fL);
        if (endWithMiddle.count(v)) add(v, fL);
        CleanFace(B);
        if (B.Size() >= 3) pm.faces.push_back(B);
    }
    // Corner patches where three or more bevels meet.
    for (const auto& kv : patch) {
        PolyFace P;
        P.verts = kv.second;
        if (anyUV) for (int vert : P.verts) { Vec2 t{0, 0}; AnyUV(pm, vert, t); P.uvs.push_back(t); }
        pm.faces.push_back(P);
    }
    return (int)sel.size();
}

std::vector<int> CompactVerts(PolyMesh& pm) {
    std::vector<char> used(pm.verts.size(), 0);
    for (const PolyFace& f : pm.faces) for (int v : f.verts) if (v >= 0 && v < (int)used.size()) used[v] = 1;
    std::vector<int> remap(pm.verts.size(), -1);
    std::vector<Vec3> kept;
    for (std::size_t i = 0; i < pm.verts.size(); ++i)
        if (used[i]) { remap[i] = (int)kept.size(); kept.push_back(pm.verts[i]); }
    pm.verts = std::move(kept);
    for (PolyFace& f : pm.faces) for (int& v : f.verts) v = remap[v];
    return remap;
}

} // namespace okay::modeling
