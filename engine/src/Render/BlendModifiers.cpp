// Mirror and Subdivision Surface for our own Blender reader. See BlendModifiers.hpp for what each rule was written
// from (the Catmull-Clark and DeRose et al. papers, the public description of Blender's Mirror modifier); no code
// from any other program was used.
//
// Subdivision is edge-map based: per level, every corner's edge is found through a bucket per lower vertex index
// (a counting sort, then a sort of each small bucket), so a level costs a few passes over flat arrays and no hash
// map. New vertices are numbered [old vertex points][edge points][face points]; an n-gon becomes n quads
// (corner, next edge point, face point, previous edge point), so the winding is kept.
#include "BlendModifiers.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <utility>

namespace okay {
namespace blendmod {
namespace {

constexpr float kSharp = std::numeric_limits<float>::infinity();   // boundary and non-manifold edges

struct Topo {
    int nV = 0, nE = 0;
    std::vector<int> loopEdge;   // the edge from corner i to corner i + 1 of the loop's face
    std::vector<int> e0, e1;     // edge ends, e0 < e1 (or equal for a degenerate edge)
};

// Next loop index inside face [s, s + n).
inline std::size_t NextLoop(std::size_t l, std::size_t s, std::size_t n) { return l + 1 == s + n ? s : l + 1; }

void BuildTopo(const std::vector<int>& fs, const std::vector<int>& fc, const std::vector<int>& lv, int nV, Topo& t) {
    const std::size_t L = lv.size();
    t.nV = nV;
    std::vector<int> start((std::size_t)nV + 1, 0);
    for (std::size_t f = 0; f < fs.size(); ++f) {
        const std::size_t s = (std::size_t)fs[f], n = (std::size_t)fc[f];
        for (std::size_t l = s; l < s + n; ++l) {
            const int a = lv[l], b = lv[NextLoop(l, s, n)];
            ++start[(std::size_t)std::min(a, b) + 1];
        }
    }
    for (int v = 0; v < nV; ++v) start[(std::size_t)v + 1] += start[(std::size_t)v];
    std::vector<std::uint64_t> keys(L);
    std::vector<int> fill(start.begin(), start.end() - 1);
    for (std::size_t f = 0; f < fs.size(); ++f) {
        const std::size_t s = (std::size_t)fs[f], n = (std::size_t)fc[f];
        for (std::size_t l = s; l < s + n; ++l) {
            const int a = lv[l], b = lv[NextLoop(l, s, n)];
            const int lo = std::min(a, b), hi = std::max(a, b);
            keys[(std::size_t)fill[(std::size_t)lo]++] = ((std::uint64_t)(std::uint32_t)hi << 32) | (std::uint64_t)l;
        }
    }
    t.loopEdge.assign(L, -1);
    t.e0.clear(); t.e1.clear();
    t.e0.reserve(L / 2 + 4); t.e1.reserve(L / 2 + 4);
    for (int v = 0; v < nV; ++v) {
        std::uint64_t* b = keys.data() + start[(std::size_t)v];
        std::uint64_t* e = keys.data() + start[(std::size_t)v + 1];
        if (e - b > 1) {
            if (e - b <= 12) {   // insertion sort: buckets are a vertex's few edges
                for (std::uint64_t* i = b + 1; i < e; ++i) { const std::uint64_t x = *i; std::uint64_t* j = i; while (j > b && *(j - 1) > x) { *j = *(j - 1); --j; } *j = x; }
            } else std::sort(b, e);
        }
        std::uint32_t prevHi = 0xffffffffu;
        for (std::uint64_t* i = b; i < e; ++i) {
            const std::uint32_t hi = (std::uint32_t)(*i >> 32);
            if (hi != prevHi) { t.e0.push_back(v); t.e1.push_back((int)hi); prevHi = hi; }
            t.loopEdge[(std::size_t)(*i & 0xffffffffu)] = (int)t.e0.size() - 1;
        }
    }
    t.nE = (int)t.e0.size();
}

// DeRose et al.'s variable-sharpness rule for the half of a creased edge at one end: (s_neighbour + 3 s) / 4 - 1,
// where the neighbour is the other creased edge at that end (the edge itself when the end has not exactly two).
inline float ChildSharp(float s, float neighbour) {
    if (std::isinf(s)) return s;
    if (std::isinf(neighbour)) neighbour = s;
    return std::max(0.0f, (neighbour + 3.0f * s) * 0.25f - 1.0f);
}

// Per-vertex options of one refinement step.
struct RefineOpts {
    const std::vector<float>* edgeSharp = nullptr;   // DeRose sharpness per edge (null or empty: no creases)
    bool linear = false;                             // simple subdivision / linear UVs
    bool keepCorners = false;                        // a boundary vertex with one face is a corner (stays)
    const std::vector<char>* hold = nullptr;         // per vertex: stays where it is (UV smoothing modes)
};

// The vertex rule by number of sharp edges (DeRose et al. 1998): fewer than 2 smooth, 2 crease, more corner.
inline int RuleOf(int sharpEdges) { return sharpEdges < 2 ? 0 : (sharpEdges == 2 ? 1 : 2); }

// What Refine and Limit need to know about the sharp edges around each vertex.
template <int D>
struct VertexSharp {
    std::vector<int> faces, edges, sharp0, sharp1;   // faces and edges around v; edges with s > 0; edges still creased one level down
    std::vector<int> boundary;                       // edges around v not shared by exactly two faces
    std::vector<float> other0, other1;               // sum of the far ends of those two edge sets, D per vertex
    std::vector<float> finSum;                       // sum of the finite sharpnesses s > 0 around v
    std::vector<int> finCnt;
    std::vector<char> anyInf;
};

template <int D>
void GatherSharp(const Topo& t, const std::vector<int>& fs, const std::vector<int>& fc, const std::vector<int>& lv, const float* vals,
                 const std::vector<int>& eCnt, const std::vector<float>* edgeSharp, VertexSharp<D>& vs) {
    const std::size_t nV = (std::size_t)t.nV, nE = (std::size_t)t.nE;
    vs.faces.assign(nV, 0); vs.edges.assign(nV, 0); vs.sharp0.assign(nV, 0); vs.sharp1.assign(nV, 0); vs.boundary.assign(nV, 0);
    vs.other0.assign(nV * D, 0.0f); vs.other1.assign(nV * D, 0.0f); vs.finSum.assign(nV, 0.0f); vs.finCnt.assign(nV, 0); vs.anyInf.assign(nV, 0);
    for (std::size_t f = 0; f < fs.size(); ++f)
        for (std::size_t l = (std::size_t)fs[f]; l < (std::size_t)fs[f] + (std::size_t)fc[f]; ++l) ++vs.faces[(std::size_t)lv[l]];
    const bool creases = edgeSharp && !edgeSharp->empty();
    // creased (not boundary) edges per vertex and the first two of them, for the child sharpness at each end
    std::vector<int> cCnt, cFirst, cSecond;
    if (creases) {
        cCnt.assign(nV, 0); cFirst.assign(nV, -1); cSecond.assign(nV, -1);
        for (std::size_t e = 0; e < nE; ++e) {
            if (!((*edgeSharp)[e] > 0.0f) || t.e0[e] == t.e1[e]) continue;
            for (const int v : {t.e0[e], t.e1[e]}) {
                const std::size_t vi = (std::size_t)v;
                if (cCnt[vi] == 0) cFirst[vi] = (int)e; else if (cCnt[vi] == 1) cSecond[vi] = (int)e;
                ++cCnt[vi];
            }
        }
    }
    for (std::size_t e = 0; e < nE; ++e) {
        const std::size_t ia = (std::size_t)t.e0[e], ib = (std::size_t)t.e1[e];
        if (ia == ib) continue;
        ++vs.edges[ia]; ++vs.edges[ib];
        const bool bnd = eCnt[e] != 2;
        if (bnd) { ++vs.boundary[ia]; ++vs.boundary[ib]; }
        const float s = bnd ? kSharp : (creases ? (*edgeSharp)[e] : 0.0f);
        if (!(s > 0.0f)) continue;
        const float* a = vals + ia * D;
        const float* b = vals + ib * D;
        ++vs.sharp0[ia]; ++vs.sharp0[ib];
        for (int d = 0; d < D; ++d) { vs.other0[ia * D + d] += b[d]; vs.other0[ib * D + d] += a[d]; }
        // is the half of this edge at each end still creased on the next level?
        for (int end = 0; end < 2; ++end) {
            const std::size_t v = end == 0 ? ia : ib;
            const float* farEnd = end == 0 ? b : a;
            float child = s;
            if (!std::isinf(s)) {
                float nb = s;
                if (creases && cCnt[v] == 2) nb = (*edgeSharp)[(std::size_t)(cFirst[v] == (int)e ? cSecond[v] : cFirst[v])];
                child = ChildSharp(s, nb);
            }
            if (child > 0.0f) { ++vs.sharp1[v]; for (int d = 0; d < D; ++d) vs.other1[v * D + d] += farEnd[d]; }
        }
        if (std::isinf(s)) { vs.anyInf[ia] = vs.anyInf[ib] = 1; }
        else { vs.finSum[ia] += s; vs.finSum[ib] += s; ++vs.finCnt[ia]; ++vs.finCnt[ib]; }
    }
}

// One Catmull-Clark (or linear) step of a D-dimensional value per vertex. `vals` has nV * D floats; the result has
// (nV + nE + nF) * D floats: vertex points, edge points, face points. Edges that are not shared by exactly two faces
// are always sharp.
template <int D>
void Refine(const Topo& t, const std::vector<int>& fs, const std::vector<int>& fc, const std::vector<int>& lv, const float* vals,
            const RefineOpts& op, std::vector<float>& out) {
    const std::size_t nV = (std::size_t)t.nV, nE = (std::size_t)t.nE, nF = fs.size();
    const bool linear = op.linear;
    out.assign((nV + nE + nF) * D, 0.0f);
    float* VP = out.data();
    float* EP = VP + nV * D;
    float* FP = EP + nE * D;
    // face points
    for (std::size_t f = 0; f < nF; ++f) {
        const std::size_t s = (std::size_t)fs[f], n = (std::size_t)fc[f];
        float acc[D] = {};
        for (std::size_t l = s; l < s + n; ++l) { const float* p = vals + (std::size_t)lv[l] * D; for (int d = 0; d < D; ++d) acc[d] += p[d]; }
        const float inv = 1.0f / (float)n;
        for (int d = 0; d < D; ++d) FP[f * D + d] = acc[d] * inv;
    }
    // faces around each edge
    std::vector<int> eCnt(nE, 0);
    std::vector<float> eSum(linear ? 0 : nE * D, 0.0f);
    for (std::size_t f = 0; f < nF; ++f) {
        const std::size_t s = (std::size_t)fs[f], n = (std::size_t)fc[f];
        for (std::size_t l = s; l < s + n; ++l) {
            const std::size_t e = (std::size_t)t.loopEdge[l];
            ++eCnt[e];
            if (!linear) for (int d = 0; d < D; ++d) eSum[e * D + d] += FP[f * D + d];
        }
    }
    const bool creases = op.edgeSharp && !op.edgeSharp->empty();
    auto sharpOf = [&](std::size_t e) -> float {
        if (eCnt[e] != 2) return kSharp;
        return creases ? (*op.edgeSharp)[e] : 0.0f;
    };
    // edge points: s >= 1 midpoint, 0 < s < 1 the smooth point moved towards the midpoint by s
    for (std::size_t e = 0; e < nE; ++e) {
        const float* a = vals + (std::size_t)t.e0[e] * D;
        const float* b = vals + (std::size_t)t.e1[e] * D;
        float* o = EP + e * D;
        const float s = linear ? kSharp : sharpOf(e);
        if (s >= 1.0f) { for (int d = 0; d < D; ++d) o[d] = 0.5f * (a[d] + b[d]); continue; }
        for (int d = 0; d < D; ++d) o[d] = 0.25f * (a[d] + b[d] + eSum[e * D + d]);
        if (s > 0.0f) for (int d = 0; d < D; ++d) o[d] = (1.0f - s) * o[d] + s * 0.5f * (a[d] + b[d]);
    }
    if (linear) { std::memcpy(VP, vals, nV * D * sizeof(float)); return; }
    // vertex points
    VertexSharp<D> vs;
    GatherSharp<D>(t, fs, fc, lv, vals, eCnt, op.edgeSharp, vs);
    std::vector<float> vFace(nV * D, 0.0f), vMid(nV * D, 0.0f);
    for (std::size_t f = 0; f < nF; ++f) {
        const std::size_t s = (std::size_t)fs[f], n = (std::size_t)fc[f];
        for (std::size_t l = s; l < s + n; ++l) {
            const std::size_t v = (std::size_t)lv[l];
            for (int d = 0; d < D; ++d) vFace[v * D + d] += FP[f * D + d];
        }
    }
    for (std::size_t e = 0; e < nE; ++e) {
        const std::size_t ia = (std::size_t)t.e0[e], ib = (std::size_t)t.e1[e];
        if (ia == ib) continue;
        const float* a = vals + ia * D;
        const float* b = vals + ib * D;
        for (int d = 0; d < D; ++d) { const float m = 0.5f * (a[d] + b[d]); vMid[ia * D + d] += m; vMid[ib * D + d] += m; }
    }
    for (std::size_t v = 0; v < nV; ++v) {
        const float* S = vals + v * D;
        float* o = VP + v * D;
        const int n = vs.edges[v], k = vs.faces[v];
        const bool held = (op.hold && (*op.hold)[v]) || (op.keepCorners && k == 1 && vs.boundary[v] > 0);
        if (n == 0 || k == 0 || held) { for (int d = 0; d < D; ++d) o[d] = S[d]; continue; }
        const int P = RuleOf(vs.sharp0[v]), C = RuleOf(vs.sharp1[v]);
        float res[2][D];
        for (int which = 0; which < 2; ++which) {
            const int rule = which == 0 ? P : C;
            if (which == 1 && C == P) break;
            const float* other = which == 0 ? &vs.other0[v * D] : &vs.other1[v * D];
            float* r = res[which];
            if (rule == 0) {
                const float invK = 1.0f / (float)k, invN = 1.0f / (float)n;
                for (int d = 0; d < D; ++d) r[d] = (vFace[v * D + d] * invK + 2.0f * vMid[v * D + d] * invN + (float)(n - 3) * S[d]) * invN;
            } else if (rule == 1) {
                for (int d = 0; d < D; ++d) r[d] = (other[d] + 6.0f * S[d]) * 0.125f;   // crease / boundary curve
            } else {
                for (int d = 0; d < D; ++d) r[d] = S[d];                                 // corner
            }
        }
        if (C == P) { for (int d = 0; d < D; ++d) o[d] = res[0][d]; continue; }
        // the rule changes on the way to the next level: blend by the vertex sharpness (the mean of its finite
        // crease sharpnesses, clamped to 1; DeRose et al. 1998)
        const float w = vs.finCnt[v] > 0 ? std::min(1.0f, vs.finSum[v] / (float)vs.finCnt[v]) : 1.0f;
        for (int d = 0; d < D; ++d) o[d] = w * res[0][d] + (1.0f - w) * res[1][d];
    }
}

// The limit positions when only boundaries are sharp (no creases, nothing held): one pass over the edges and one over
// the corners, the same masks as Limit below (the common case, kept lean because it runs on the largest mesh).
template <int D>
void LimitNoCreases(const Topo& t, const std::vector<int>& fs, const std::vector<int>& fc, const std::vector<int>& lv, const float* vals,
                    const std::vector<int>& eCnt, std::vector<float>& out) {
    const std::size_t nV = (std::size_t)t.nV, nE = (std::size_t)t.nE, nF = fs.size();
    std::vector<float> acc(nV * D, 0.0f), bnd(nV * D, 0.0f);   // 4 sum(edge neighbours) + sum(diagonals); boundary neighbours
    std::vector<int> nEdge(nV, 0), nBnd(nV, 0), nFace(nV, 0);
    std::vector<char> quadsOnly(nV, 1);
    for (std::size_t e = 0; e < nE; ++e) {
        const std::size_t ia = (std::size_t)t.e0[e], ib = (std::size_t)t.e1[e];
        if (ia == ib) continue;
        ++nEdge[ia]; ++nEdge[ib];
        const float* a = vals + ia * D;
        const float* b = vals + ib * D;
        for (int d = 0; d < D; ++d) { acc[ia * D + d] += 4.0f * b[d]; acc[ib * D + d] += 4.0f * a[d]; }
        if (eCnt[e] != 2) { ++nBnd[ia]; ++nBnd[ib]; for (int d = 0; d < D; ++d) { bnd[ia * D + d] += b[d]; bnd[ib * D + d] += a[d]; } }
    }
    for (std::size_t f = 0; f < nF; ++f) {
        const std::size_t s = (std::size_t)fs[f];
        if (fc[f] != 4) { for (std::size_t l = s; l < s + (std::size_t)fc[f]; ++l) { quadsOnly[(std::size_t)lv[l]] = 0; ++nFace[(std::size_t)lv[l]]; } continue; }
        for (std::size_t i = 0; i < 4; ++i) {
            const std::size_t v = (std::size_t)lv[s + i], o = (std::size_t)lv[s + (i + 2) % 4];
            ++nFace[v];
            for (int d = 0; d < D; ++d) acc[v * D + d] += vals[o * D + d];
        }
    }
    for (std::size_t v = 0; v < nV; ++v) {
        const int n = nEdge[v], k = nFace[v];
        if (n == 0 || k == 0) continue;
        const float* S = vals + v * D;
        float* o = &out[v * D];
        if (nBnd[v] >= 3) continue;                                           // corner
        if (nBnd[v] == 2) { for (int d = 0; d < D; ++d) o[d] = (bnd[v * D + d] + 4.0f * S[d]) * (1.0f / 6.0f); continue; }
        if (n != k || !quadsOnly[v]) continue;
        const float inv = 1.0f / (float)(n * (n + 5));
        for (int d = 0; d < D; ++d) o[d] = ((float)(n * n) * S[d] + acc[v * D + d]) * inv;
    }
}

// Moves every vertex of an all-quad mesh to its Catmull-Clark limit position (Halstead, Kass and DeRose 1993):
// smooth (n^2 S + 4 sum(edge neighbours) + sum(diagonal corners)) / (n (n + 5)); crease (a + 4 S + b) / 6; corner S.
// A vertex whose creases are only partly sharp (0 < s < 1 left) blends the sharp and the smooth limit by that
// sharpness (an approximation: the exact limit of a semi-sharp crease needs the remaining sharp levels).
template <int D>
void Limit(const Topo& t, const std::vector<int>& fs, const std::vector<int>& fc, const std::vector<int>& lv, const float* vals,
           const RefineOpts& op, std::vector<float>& out) {
    const std::size_t nV = (std::size_t)t.nV, nE = (std::size_t)t.nE, nF = fs.size();
    out.assign(vals, vals + nV * D);
    std::vector<int> eCnt(nE, 0);
    for (std::size_t f = 0; f < nF; ++f)
        for (std::size_t l = (std::size_t)fs[f]; l < (std::size_t)fs[f] + (std::size_t)fc[f]; ++l) ++eCnt[(std::size_t)t.loopEdge[l]];
    if (!(op.edgeSharp && !op.edgeSharp->empty()) && !op.hold && !op.keepCorners) { LimitNoCreases<D>(t, fs, fc, lv, vals, eCnt, out); return; }
    VertexSharp<D> vs;
    GatherSharp<D>(t, fs, fc, lv, vals, eCnt, op.edgeSharp, vs);
    std::vector<float> nbr(nV * D, 0.0f), diag(nV * D, 0.0f);
    std::vector<char> quadsOnly(nV, 1);
    for (std::size_t e = 0; e < nE; ++e) {
        const std::size_t ia = (std::size_t)t.e0[e], ib = (std::size_t)t.e1[e];
        if (ia == ib) continue;
        for (int d = 0; d < D; ++d) { nbr[ia * D + d] += vals[ib * D + d]; nbr[ib * D + d] += vals[ia * D + d]; }
    }
    for (std::size_t f = 0; f < nF; ++f) {
        const std::size_t s = (std::size_t)fs[f];
        if (fc[f] != 4) { for (std::size_t l = s; l < s + (std::size_t)fc[f]; ++l) quadsOnly[(std::size_t)lv[l]] = 0; continue; }
        for (std::size_t i = 0; i < 4; ++i) {
            const std::size_t v = (std::size_t)lv[s + i], o = (std::size_t)lv[s + (i + 2) % 4];
            for (int d = 0; d < D; ++d) diag[v * D + d] += vals[o * D + d];
        }
    }
    for (std::size_t v = 0; v < nV; ++v) {
        const float* S = vals + v * D;
        float* o = &out[v * D];
        const int n = vs.edges[v], k = vs.faces[v];
        const bool held = (op.hold && (*op.hold)[v]) || (op.keepCorners && k == 1 && vs.boundary[v] > 0);
        if (n == 0 || k == 0 || held) continue;
        const int rule = RuleOf(vs.sharp0[v]);
        float sharp[D], smooth[D];
        bool haveSmooth = false;
        if (n == k && quadsOnly[v]) {
            const float inv = 1.0f / (float)(n * (n + 5));
            for (int d = 0; d < D; ++d) smooth[d] = ((float)(n * n) * S[d] + 4.0f * nbr[v * D + d] + diag[v * D + d]) * inv;
            haveSmooth = true;
        }
        if (rule == 0) { if (haveSmooth) for (int d = 0; d < D; ++d) o[d] = smooth[d]; continue; }
        if (rule == 1) for (int d = 0; d < D; ++d) sharp[d] = (vs.other0[v * D + d] + 4.0f * S[d]) * (1.0f / 6.0f);
        else for (int d = 0; d < D; ++d) sharp[d] = S[d];
        float w = 1.0f;
        if (!vs.anyInf[v] && vs.finCnt[v] > 0) w = std::min(1.0f, vs.finSum[v] / (float)vs.finCnt[v]);
        if (w >= 1.0f || !haveSmooth) { for (int d = 0; d < D; ++d) o[d] = sharp[d]; continue; }
        for (int d = 0; d < D; ++d) o[d] = w * sharp[d] + (1.0f - w) * smooth[d];
    }
}

inline std::uint64_t EdgeKey(int a, int b) {
    const int lo = std::min(a, b), hi = std::max(a, b);
    return ((std::uint64_t)(std::uint32_t)lo << 32) | (std::uint32_t)hi;
}

// Weights of a new vertex as the weighted sum of old vertices' weights (groups merged, sorted by group).
void BlendWeights(const PolyMesh& m, const int* src, const float* coef, int n, std::vector<std::pair<int, float>>& tmp,
                  std::vector<std::uint32_t>& start, std::vector<int>& grp, std::vector<float>& wt) {
    tmp.clear();
    for (int i = 0; i < n; ++i) {
        const std::size_t v = (std::size_t)src[i];
        for (std::uint32_t k = m.dvStart[v]; k < m.dvStart[v + 1]; ++k) tmp.emplace_back(m.dvGroup[k], m.dvWeight[k] * coef[i]);
    }
    std::sort(tmp.begin(), tmp.end(), [](const std::pair<int, float>& a, const std::pair<int, float>& b) { return a.first < b.first; });
    for (std::size_t i = 0; i < tmp.size();) {
        std::size_t j = i; float w = 0.0f;
        while (j < tmp.size() && tmp[j].first == tmp[i].first) w += tmp[j++].second;
        if (w > 0.0f) { grp.push_back(tmp[i].first); wt.push_back(w); }
        i = j;
    }
    start.push_back((std::uint32_t)grp.size());
}

// Per-edge sharpness from the mesh's crease list (empty when no edge is creased).
void EdgeSharpness(const PolyMesh& m, const Topo& t, std::vector<float>& edgeSharp) {
    edgeSharp.clear();
    if (m.creases.empty()) return;
    std::unordered_map<std::uint64_t, float> creaseOf;
    creaseOf.reserve(m.creases.size() * 2);
    for (const Crease& c : m.creases) if (c.sharpness > 0.0f && c.v0 != c.v1) { float& s = creaseOf[EdgeKey(c.v0, c.v1)]; s = std::max(s, c.sharpness); }
    if (creaseOf.empty()) return;
    edgeSharp.assign((std::size_t)t.nE, 0.0f);
    for (std::size_t e = 0; e < (std::size_t)t.nE; ++e) { auto it = creaseOf.find(EdgeKey(t.e0[e], t.e1[e])); if (it != creaseOf.end()) edgeSharp[e] = it->second; }
}

// UV topology: corners welded by vertex and UV bits (bucket by vertex, then sort by UV inside the bucket), so a seam
// is a boundary. uvLoopVert[l] is loop l's UV vertex; uvOwner[u] the mesh vertex of UV vertex u.
void WeldUv(const PolyMesh& m, std::vector<int>& uvLoopVert, std::vector<Vec2>& uvVals, std::vector<int>& uvOwner) {
    const std::size_t L = m.loopVert.size();
    const int nV = (int)m.pos.size();
    std::vector<int> start((std::size_t)nV + 1, 0);
    for (std::size_t l = 0; l < L; ++l) ++start[(std::size_t)m.loopVert[l] + 1];
    for (int v = 0; v < nV; ++v) start[(std::size_t)v + 1] += start[(std::size_t)v];
    std::vector<std::pair<std::uint64_t, int>> items(L);
    std::vector<int> fill(start.begin(), start.end() - 1);
    for (std::size_t l = 0; l < L; ++l) {
        std::uint32_t ux, uy; std::memcpy(&ux, &m.loopUv[l].x, 4); std::memcpy(&uy, &m.loopUv[l].y, 4);
        items[(std::size_t)fill[(std::size_t)m.loopVert[l]]++] = {((std::uint64_t)ux << 32) | uy, (int)l};
    }
    uvLoopVert.assign(L, 0);
    uvVals.clear(); uvOwner.clear();
    uvVals.reserve((std::size_t)nV + nV / 4 + 4); uvOwner.reserve(uvVals.capacity());
    for (int v = 0; v < nV; ++v) {
        auto b = items.begin() + start[(std::size_t)v], e = items.begin() + start[(std::size_t)v + 1];
        if (e - b > 1) std::sort(b, e);
        std::uint64_t prevKey = 0; bool first = true;
        for (auto it = b; it != e; ++it) {
            if (first || it->first != prevKey) { uvVals.push_back(m.loopUv[(std::size_t)it->second]); uvOwner.push_back(v); prevKey = it->first; first = false; }
            uvLoopVert[(std::size_t)it->second] = (int)uvVals.size() - 1;
        }
    }
}

// The UV vertices a smoothing mode holds where they are (empty: none). Definitions (ours, from the mode names):
// boundary = on an edge of the UV topology with other than two faces; corner = boundary with one face around it;
// junction = its mesh vertex has 3 or more distinct UVs; concave = boundary whose corner angles in UV space add up to
// more than 180 degrees.
void UvHold(UvSmooth mode, const PolyMesh& m, const Topo& tu, const std::vector<int>& uvLoopVert, const std::vector<Vec2>& uvVals,
            const std::vector<int>& uvOwner, std::vector<char>& hold) {
    hold.clear();
    if (mode == UvSmooth::All || mode == UvSmooth::Linear) return;
    const std::size_t nU = uvVals.size(), nF = m.faceStart.size();
    std::vector<int> eCnt((std::size_t)tu.nE, 0), faces(nU, 0);
    std::vector<char> bnd(nU, 0);
    for (std::size_t f = 0; f < nF; ++f)
        for (std::size_t l = (std::size_t)m.faceStart[f]; l < (std::size_t)(m.faceStart[f] + m.faceCount[f]); ++l) { ++eCnt[(std::size_t)tu.loopEdge[l]]; ++faces[(std::size_t)uvLoopVert[l]]; }
    for (std::size_t e = 0; e < (std::size_t)tu.nE; ++e) if (eCnt[e] != 2) { bnd[(std::size_t)tu.e0[e]] = 1; bnd[(std::size_t)tu.e1[e]] = 1; }
    hold.assign(nU, 0);
    if (mode == UvSmooth::KeepBoundaries) { for (std::size_t u = 0; u < nU; ++u) hold[u] = bnd[u]; return; }
    for (std::size_t u = 0; u < nU; ++u) hold[u] = bnd[u] && faces[u] == 1;
    if (mode == UvSmooth::KeepCorners) return;
    std::vector<int> perVert(m.pos.size(), 0);
    for (std::size_t u = 0; u < nU; ++u) ++perVert[(std::size_t)uvOwner[u]];
    for (std::size_t u = 0; u < nU; ++u) if (perVert[(std::size_t)uvOwner[u]] >= 3) hold[u] = 1;
    if (mode != UvSmooth::KeepCornersJunctionsConcave) return;
    std::vector<double> angle(nU, 0.0);
    for (std::size_t f = 0; f < nF; ++f) {
        const std::size_t s = (std::size_t)m.faceStart[f], n = (std::size_t)m.faceCount[f];
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t l = s + i, nx = s + (i + 1) % n, pv = s + (i + n - 1) % n;
            const std::size_t u = (std::size_t)uvLoopVert[l];
            if (!bnd[u]) continue;
            const double ax = (double)m.loopUv[nx].x - m.loopUv[l].x, ay = (double)m.loopUv[nx].y - m.loopUv[l].y;
            const double bx = (double)m.loopUv[pv].x - m.loopUv[l].x, by = (double)m.loopUv[pv].y - m.loopUv[l].y;
            const double la = std::sqrt(ax * ax + ay * ay), lb = std::sqrt(bx * bx + by * by);
            if (!(la > 0.0) || !(lb > 0.0) || !std::isfinite(la * lb)) continue;
            angle[u] += std::acos(std::max(-1.0, std::min(1.0, (ax * bx + ay * by) / (la * lb))));
        }
    }
    const double kPi = 3.14159265358979323846;
    for (std::size_t u = 0; u < nU; ++u) if (bnd[u] && angle[u] > kPi + 1e-4) hold[u] = 1;
}

// One subdivision level. False (mesh unchanged) when the result would pass maxFaces.
// The topology of the mesh one level down, without sorting: parent edge e becomes the halves 2e (end e0 to the edge
// point) and 2e + 1 (edge point to end e1); corner l of the parent adds the edge 2E + l from its edge point to its face
// point. Child quad of corner l (corner, edge point, face point, previous edge point) then has its four edges directly.
// Used for the limit surface after the last level, so the largest mesh is not bucket-sorted again.
void ChildTopo(const Topo& t, const std::vector<int>& fs, const std::vector<int>& fc, const std::vector<int>& lv, Topo& c) {
    const std::size_t nV = (std::size_t)t.nV, nE = (std::size_t)t.nE, L = lv.size();
    c.nV = (int)(nV + nE + fs.size());
    c.nE = (int)(2 * nE + L);
    c.e0.resize((std::size_t)c.nE); c.e1.resize((std::size_t)c.nE);
    for (std::size_t e = 0; e < nE; ++e) {
        const int ep = (int)(nV + e);
        c.e0[2 * e] = t.e0[e]; c.e1[2 * e] = ep;
        c.e0[2 * e + 1] = t.e1[e]; c.e1[2 * e + 1] = ep;
    }
    c.loopEdge.resize(L * 4);
    for (std::size_t f = 0; f < fs.size(); ++f) {
        const std::size_t s = (std::size_t)fs[f], n = (std::size_t)fc[f];
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t l = s + i, prev = i == 0 ? s + n - 1 : l - 1;
            const std::size_t e = (std::size_t)t.loopEdge[l], ep = (std::size_t)t.loopEdge[prev];
            c.e0[2 * nE + l] = (int)(nV + e); c.e1[2 * nE + l] = (int)(nV + nE + f);
            int* o = &c.loopEdge[l * 4];
            o[0] = (int)(2 * e + (t.e0[e] == lv[l] ? 0 : 1));        // corner -> its edge point
            o[1] = (int)(2 * nE + l);                                  // edge point -> face point
            o[2] = (int)(2 * nE + prev);                               // face point -> previous edge point
            o[3] = (int)(2 * ep + (t.e0[ep] == lv[l] ? 0 : 1));       // previous edge point -> corner
        }
    }
}

bool SubdivideOnce(PolyMesh& m, const SubsurfSettings& st, std::size_t maxFaces, int levelsLeft, Topo* childTopo) {
    const std::size_t L = m.loopVert.size(), nF = m.faceStart.size();
    if (!m.loopUv.empty() && m.loopUv.size() != L) m.loopUv.clear();
    if (L > maxFaces || L > (std::size_t)std::numeric_limits<int>::max() / 4) return false;
    const int nV = (int)m.pos.size();
    Topo t;
    BuildTopo(m.faceStart, m.faceCount, m.loopVert, nV, t);
    const std::size_t nE = (std::size_t)t.nE;
    if ((std::size_t)nV + nE + nF > (std::size_t)std::numeric_limits<int>::max() / 2) return false;

    std::vector<float> edgeSharp;
    if (st.useCreases && !st.simple) EdgeSharpness(m, t, edgeSharp);

    std::vector<float> outPos;
    RefineOpts po;
    po.edgeSharp = &edgeSharp; po.linear = st.simple; po.keepCorners = st.keepCorners && !st.simple;
    Refine<3>(t, m.faceStart, m.faceCount, m.loopVert, &m.pos[0].x, po, outPos);
    static_assert(sizeof(Vec3) == 3 * sizeof(float), "Vec3 is three packed floats");
    static_assert(sizeof(Vec2) == 2 * sizeof(float), "Vec2 is two packed floats");

    PolyMesh r;
    const std::size_t nNew = (std::size_t)nV + nE + nF;
    r.pos.resize(nNew);
    std::memcpy(&r.pos[0].x, outPos.data(), nNew * sizeof(Vec3));
    r.faceStart.resize(L); r.faceCount.assign(L, 4); r.faceMat.resize(L); r.faceSmooth.resize(L);
    r.loopVert.resize(L * 4);
    const int eBase = nV, fBase = nV + (int)nE;
    std::size_t q = 0;
    std::vector<std::size_t> qBase(nF);
    for (std::size_t f = 0; f < nF; ++f) {
        const std::size_t s = (std::size_t)m.faceStart[f], n = (std::size_t)m.faceCount[f];
        qBase[f] = q;
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t l = s + i, prev = i == 0 ? s + n - 1 : l - 1;
            r.faceStart[q] = (int)(q * 4); r.faceMat[q] = m.faceMat[f]; r.faceSmooth[q] = m.faceSmooth[f];
            int* o = &r.loopVert[q * 4];
            o[0] = m.loopVert[l]; o[1] = eBase + t.loopEdge[l]; o[2] = fBase + (int)f; o[3] = eBase + t.loopEdge[prev];
            ++q;
        }
    }

    // UVs: linear, or Catmull-Clark on the UV topology (seams are boundaries), with the mode's held vertices
    if (!m.loopUv.empty()) {
        r.loopUv.resize(L * 4);
        if (st.simple || !st.smoothUv || st.uvSmooth == UvSmooth::Linear) {
            for (std::size_t f = 0; f < nF; ++f) {
                const std::size_t s = (std::size_t)m.faceStart[f], n = (std::size_t)m.faceCount[f];
                Vec2 c{0, 0};
                for (std::size_t l = s; l < s + n; ++l) c += m.loopUv[l];
                c = c * (1.0f / (float)n);
                for (std::size_t i = 0; i < n; ++i) {
                    const std::size_t l = s + i, next = NextLoop(l, s, n), prev = i == 0 ? s + n - 1 : l - 1;
                    Vec2* o = &r.loopUv[(qBase[f] + i) * 4];
                    o[0] = m.loopUv[l]; o[1] = (m.loopUv[l] + m.loopUv[next]) * 0.5f; o[2] = c; o[3] = (m.loopUv[prev] + m.loopUv[l]) * 0.5f;
                }
            }
        } else {
            std::vector<int> uvLoopVert, uvOwner;
            std::vector<Vec2> uvVals;
            WeldUv(m, uvLoopVert, uvVals, uvOwner);
            Topo tu;
            BuildTopo(m.faceStart, m.faceCount, uvLoopVert, (int)uvVals.size(), tu);
            std::vector<char> hold;
            UvHold(st.uvSmooth, m, tu, uvLoopVert, uvVals, uvOwner, hold);
            RefineOpts uo;
            uo.hold = hold.empty() ? nullptr : &hold;
            std::vector<float> outUv;
            Refine<2>(tu, m.faceStart, m.faceCount, uvLoopVert, &uvVals[0].x, uo, outUv);
            const Vec2* uvV = reinterpret_cast<const Vec2*>(outUv.data());
            const Vec2* uvE = uvV + uvVals.size();
            const Vec2* uvF = uvE + (std::size_t)tu.nE;
            for (std::size_t f = 0; f < nF; ++f) {
                const std::size_t s = (std::size_t)m.faceStart[f], n = (std::size_t)m.faceCount[f];
                for (std::size_t i = 0; i < n; ++i) {
                    const std::size_t l = s + i, prev = i == 0 ? s + n - 1 : l - 1;
                    Vec2* o = &r.loopUv[(qBase[f] + i) * 4];
                    o[0] = uvV[uvLoopVert[l]]; o[1] = uvE[tu.loopEdge[l]]; o[2] = uvF[f]; o[3] = uvE[tu.loopEdge[prev]];
                }
            }
        }
    }

    // vertex weights: vertex points keep theirs, edge points the mean of the two ends, face points the mean of the corners
    if (!m.dvStart.empty() && m.dvStart.size() == (std::size_t)nV + 1) {
        r.dvStart.reserve(nNew + 1); r.dvStart.push_back(0);
        r.dvGroup.reserve(m.dvGroup.size() * 4); r.dvWeight.reserve(m.dvGroup.size() * 4);
        std::vector<std::pair<int, float>> tmp;
        for (int v = 0; v < nV; ++v) { const float one = 1.0f; BlendWeights(m, &v, &one, 1, tmp, r.dvStart, r.dvGroup, r.dvWeight); }
        for (std::size_t e = 0; e < nE; ++e) { const int src[2] = {t.e0[e], t.e1[e]}; const float c[2] = {0.5f, 0.5f}; BlendWeights(m, src, c, 2, tmp, r.dvStart, r.dvGroup, r.dvWeight); }
        std::vector<float> c;
        for (std::size_t f = 0; f < nF; ++f) {
            const std::size_t n = (std::size_t)m.faceCount[f];
            c.assign(n, 1.0f / (float)n);
            BlendWeights(m, &m.loopVert[(std::size_t)m.faceStart[f]], c.data(), (int)n, tmp, r.dvStart, r.dvGroup, r.dvWeight);
        }
    }

    // creases one level down (DeRose et al.'s variable-sharpness rule; s - 1 along a uniform crease). Also kept after
    // the last level when the limit surface needs them.
    if (!edgeSharp.empty() && (levelsLeft > 1 || st.limitSurface)) {
        // per vertex: how many creased (0 < s, not a boundary) edges, and the first two of them
        std::vector<int> cnt((std::size_t)nV, 0), first((std::size_t)nV, -1), second((std::size_t)nV, -1);
        for (std::size_t e = 0; e < nE; ++e) {
            if (!(edgeSharp[e] > 0.0f) || t.e0[e] == t.e1[e]) continue;
            for (const int v : {t.e0[e], t.e1[e]}) {
                const std::size_t vi = (std::size_t)v;
                if (cnt[vi] == 0) first[vi] = (int)e; else if (cnt[vi] == 1) second[vi] = (int)e;
                ++cnt[vi];
            }
        }
        auto neighbour = [&](int v, std::size_t e) -> float {
            const std::size_t vi = (std::size_t)v;
            if (cnt[vi] != 2) return edgeSharp[e];
            const int o = first[vi] == (int)e ? second[vi] : first[vi];
            return edgeSharp[(std::size_t)o];
        };
        for (std::size_t e = 0; e < nE; ++e) {
            const float s = edgeSharp[e];
            if (!(s > 0.0f) || t.e0[e] == t.e1[e]) continue;
            const float s0 = ChildSharp(s, neighbour(t.e0[e], e)), s1 = ChildSharp(s, neighbour(t.e1[e], e));
            if (s0 > 0.0f) r.creases.push_back(Crease{t.e0[e], eBase + (int)e, s0});
            if (s1 > 0.0f) r.creases.push_back(Crease{eBase + (int)e, t.e1[e], s1});
        }
    }
    // only for a level that will be the last one: the last asked for, or the next one would pass the face cap
    if (childTopo && (levelsLeft <= 1 || L * 4 > maxFaces)) ChildTopo(t, m.faceStart, m.faceCount, m.loopVert, *childTopo);
    m = std::move(r);
    return true;
}

// After the last level: every point to its limit position (positions, and UVs when they are smoothed).
// t: the mesh's topology from ChildTopo (its edge numbering need not match BuildTopo's).
void ApplyLimit(PolyMesh& m, const SubsurfSettings& st, const Topo& t) {
    if (m.faceStart.empty() || m.pos.empty()) return;
    for (int c : m.faceCount) if (c != 4) return;   // only after at least one Catmull-Clark level
    if ((std::size_t)t.nV != m.pos.size() || t.loopEdge.size() != m.loopVert.size()) return;
    std::vector<float> edgeSharp;
    if (st.useCreases) EdgeSharpness(m, t, edgeSharp);
    RefineOpts po;
    po.edgeSharp = &edgeSharp; po.keepCorners = st.keepCorners;
    std::vector<float> out;
    Limit<3>(t, m.faceStart, m.faceCount, m.loopVert, &m.pos[0].x, po, out);
    std::memcpy(&m.pos[0].x, out.data(), m.pos.size() * sizeof(Vec3));
    if (m.loopUv.empty() || m.loopUv.size() != m.loopVert.size() || !st.smoothUv || st.uvSmooth == UvSmooth::Linear) return;
    std::vector<int> uvLoopVert, uvOwner;
    std::vector<Vec2> uvVals;
    WeldUv(m, uvLoopVert, uvVals, uvOwner);
    Topo tu;
    BuildTopo(m.faceStart, m.faceCount, uvLoopVert, (int)uvVals.size(), tu);
    std::vector<char> hold;
    UvHold(st.uvSmooth, m, tu, uvLoopVert, uvVals, uvOwner, hold);
    RefineOpts uo;
    uo.hold = hold.empty() ? nullptr : &hold;
    std::vector<float> outUv;
    Limit<2>(tu, m.faceStart, m.faceCount, uvLoopVert, &uvVals[0].x, uo, outUv);
    const Vec2* uvV = reinterpret_cast<const Vec2*>(outUv.data());
    for (std::size_t l = 0; l < m.loopUv.size(); ++l) m.loopUv[l] = uvV[uvLoopVert[l]];
}

// Keeps only loops that belong to faces, in face order, and drops faces that name a vertex outside the vertex list or
// have fewer than 3 corners (a reader may hand over loops of faces it dropped). Afterwards face f's loops start where
// face f-1's end, which the steps above rely on.
void Compact(PolyMesh& m) {
    const std::size_t nV = m.pos.size(), L = m.loopVert.size();
    const bool uv = m.loopUv.size() == L;
    if (!uv) m.loopUv.clear();
    std::size_t expect = 0;
    bool tidy = true;
    for (std::size_t f = 0; f < m.faceStart.size() && tidy; ++f) {
        if (m.faceStart[f] < 0 || (std::size_t)m.faceStart[f] != expect || m.faceCount[f] < 3) { tidy = false; break; }
        expect += (std::size_t)m.faceCount[f];
        if (expect > L) { tidy = false; break; }
        for (std::size_t l = (std::size_t)m.faceStart[f]; l < expect; ++l) if (m.loopVert[l] < 0 || (std::size_t)m.loopVert[l] >= nV) { tidy = false; break; }
    }
    if (tidy && expect == L) return;
    PolyMesh r;
    r.pos = std::move(m.pos);
    r.dvStart = std::move(m.dvStart); r.dvGroup = std::move(m.dvGroup); r.dvWeight = std::move(m.dvWeight);
    r.creases = std::move(m.creases);
    for (std::size_t f = 0; f < m.faceStart.size(); ++f) {
        const long long st = m.faceStart[f], n = m.faceCount[f];
        if (st < 0 || n < 3 || (std::size_t)(st + n) > L) continue;
        bool ok = true;
        for (long long l = st; l < st + n; ++l) if (m.loopVert[(std::size_t)l] < 0 || (std::size_t)m.loopVert[(std::size_t)l] >= nV) { ok = false; break; }
        if (!ok) continue;
        r.faceStart.push_back((int)r.loopVert.size()); r.faceCount.push_back((int)n);
        r.faceMat.push_back(f < m.faceMat.size() ? m.faceMat[f] : 0); r.faceSmooth.push_back(f < m.faceSmooth.size() ? m.faceSmooth[f] : 1);
        for (long long l = st; l < st + n; ++l) { r.loopVert.push_back(m.loopVert[(std::size_t)l]); if (uv) r.loopUv.push_back(m.loopUv[(std::size_t)l]); }
    }
    m = std::move(r);
}

// Vertex weights are used only when their arrays agree with each other and with the vertex list.
void CheckWeights(PolyMesh& m) {
    if (m.dvStart.empty()) return;
    bool ok = m.dvStart.size() == m.pos.size() + 1 && m.dvGroup.size() == m.dvWeight.size() && m.dvStart[0] == 0 && m.dvStart.back() == m.dvGroup.size();
    for (std::size_t i = 1; ok && i < m.dvStart.size(); ++i) if (m.dvStart[i] < m.dvStart[i - 1]) ok = false;
    if (!ok) { m.dvStart.clear(); m.dvGroup.clear(); m.dvWeight.clear(); }
}

}  // namespace

int Subdivide(PolyMesh& m, const SubsurfSettings& s, std::size_t maxFaces) {
    int done = 0;
    Compact(m);
    CheckWeights(m);
    if (s.joinCoincident && !m.pos.empty() && !m.faceStart.empty()) {
        // corners at bit-identical positions name one vertex, so a mesh stored split along its edges is smoothed as one
        // surface (what Assimp shows); the unused copies stay in the vertex list and are never drawn
        std::vector<int> order(m.pos.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
        auto bits = [&](int i) { std::uint32_t b[3]; std::memcpy(b, &m.pos[(std::size_t)i].x, 12); return std::array<std::uint32_t, 3>{b[0], b[1], b[2]}; };
        std::sort(order.begin(), order.end(), [&](int a, int b) { const auto ka = bits(a), kb = bits(b); return ka != kb ? ka < kb : a < b; });
        std::vector<int> canon(m.pos.size());
        for (std::size_t i = 0; i < order.size(); ++i)
            canon[(std::size_t)order[i]] = (i > 0 && bits(order[i]) == bits(order[i - 1])) ? canon[(std::size_t)order[i - 1]] : order[i];
        for (int& v : m.loopVert) v = canon[(std::size_t)v];
        for (Crease& c : m.creases) if (c.v0 >= 0 && c.v1 >= 0 && (std::size_t)c.v0 < canon.size() && (std::size_t)c.v1 < canon.size()) { c.v0 = canon[(std::size_t)c.v0]; c.v1 = canon[(std::size_t)c.v1]; }
    }
    const bool limit = s.limitSurface && !s.simple;
    Topo last;
    for (int lv = 0; lv < s.levels; ++lv) {
        if (m.faceStart.empty() || m.pos.empty()) break;
        // the child topology is kept from every level that might be the last one (a later level can stop at the cap)
        if (!SubdivideOnce(m, s, maxFaces, s.levels - lv, limit ? &last : nullptr)) break;
        ++done;
    }
    if (limit && done > 0) ApplyLimit(m, s, last);
    m.creases.clear();
    return done;
}

int Mirror(PolyMesh& m, const MirrorSettings& s) {
    int merged = 0;
    Compact(m);
    CheckWeights(m);
    for (int a = 0; a < 3; ++a) {
        if (!s.axis[a]) continue;
        const std::size_t nV = m.pos.size(), nF = m.faceStart.size(), L = m.loopVert.size();
        if (nV > (std::size_t)std::numeric_limits<int>::max() / 2 || L > (std::size_t)std::numeric_limits<int>::max() / 2) break;
        if (!m.loopUv.empty() && m.loopUv.size() != L) m.loopUv.clear();
        std::vector<int> map(nV);
        std::vector<char> same(nV, 0);
        m.pos.reserve(nV * 2);
        const bool weights = !m.dvStart.empty() && m.dvStart.size() == nV + 1;
        for (std::size_t v = 0; v < nV; ++v) {
            const Vec3 p = m.pos[v];
            Vec3 q = p;
            if (s.useSpace) { const Vec4 r = s.toSpace * Vec4{p.x, p.y, p.z, 1.0f}; q = Vec3{r.x, r.y, r.z}; }
            q[a] = -q[a];
            if (s.useSpace) { const Vec4 r = s.fromSpace * Vec4{q.x, q.y, q.z, 1.0f}; q = Vec3{r.x, r.y, r.z}; }
            const Vec3 d = q - p;
            if (s.merge && std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z) <= s.tolerance) {
                m.pos[v] = (p + q) * 0.5f;   // onto the mirror plane
                map[v] = (int)v; same[v] = 1; ++merged;
            } else {
                map[v] = (int)m.pos.size();
                m.pos.push_back(q);
            }
        }
        if (weights) {
            for (std::size_t v = 0; v < nV; ++v) {
                if (same[v]) continue;
                for (std::uint32_t k = m.dvStart[v]; k < m.dvStart[v + 1]; ++k) {
                    int g = m.dvGroup[k];
                    if (g >= 0 && (std::size_t)g < s.groupFlip.size() && s.groupFlip[(std::size_t)g] >= 0) g = s.groupFlip[(std::size_t)g];
                    m.dvGroup.push_back(g); m.dvWeight.push_back(m.dvWeight[k]);
                }
                m.dvStart.push_back((std::uint32_t)m.dvGroup.size());
            }
        }
        const bool uv = !m.loopUv.empty() && m.loopUv.size() == L;
        m.loopVert.reserve(L * 2);
        if (uv) m.loopUv.reserve(L * 2);
        for (std::size_t f = 0; f < nF; ++f) {
            const std::size_t st = (std::size_t)m.faceStart[f], n = (std::size_t)m.faceCount[f];
            bool all = true;
            for (std::size_t l = st; l < st + n; ++l) if (!same[(std::size_t)m.loopVert[l]]) { all = false; break; }
            if (all) continue;   // the face lies in the mirror plane: its mirror image is itself
            m.faceStart.push_back((int)m.loopVert.size()); m.faceCount.push_back((int)n);
            m.faceMat.push_back(m.faceMat[f]); m.faceSmooth.push_back(m.faceSmooth[f]);
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t l = i == 0 ? st : st + n - i;   // reversed winding, same first corner
                m.loopVert.push_back(map[(std::size_t)m.loopVert[l]]);
                if (uv) {
                    Vec2 u = m.loopUv[l];
                    if (s.flipU) u.x = 1.0f - u.x + s.flipOffset[0];
                    if (s.flipV) u.y = 1.0f - u.y + s.flipOffset[1];
                    u.x += s.copyOffset[0]; u.y += s.copyOffset[1];
                    m.loopUv.push_back(u);
                }
            }
        }
        const std::size_t nc = m.creases.size();
        for (std::size_t i = 0; i < nc; ++i) {
            const Crease c = m.creases[i];
            if (c.v0 < 0 || c.v1 < 0 || (std::size_t)c.v0 >= nV || (std::size_t)c.v1 >= nV) continue;
            if (same[(std::size_t)c.v0] && same[(std::size_t)c.v1]) continue;
            m.creases.push_back(Crease{map[(std::size_t)c.v0], map[(std::size_t)c.v1], c.sharpness});
        }
    }
    return merged;
}

float CreaseToSharpness(float crease, int levels, bool openSubdiv) {
    if (!(crease > 0.0f)) return 0.0f;
    if (crease >= 1.0f) return std::numeric_limits<float>::infinity();
    return openSubdiv ? 10.0f * crease * crease : crease * (float)std::max(0, levels);
}

std::string FlipSideName(const std::string& name) {
    if (name.size() < 2) return std::string();
    auto isSep = [](char c) { return c == '.' || c == '_' || c == '-' || c == ' '; };
    auto swapLR = [](char c) -> char { switch (c) { case 'L': return 'R'; case 'R': return 'L'; case 'l': return 'r'; case 'r': return 'l'; default: return 0; } };
    std::string r = name;
    // suffix: "Arm.L", "Arm_r"
    if (isSep(name[name.size() - 2])) { if (const char c = swapLR(name.back())) { r.back() = c; return r; } }
    // prefix: "L_Arm", "r.arm"
    if (isSep(name[1])) { if (const char c = swapLR(name[0])) { r[0] = c; return r; } }
    // words: left / right in any of three cases
    static const char* const kPairs[][2] = {{"left", "right"}, {"Left", "Right"}, {"LEFT", "RIGHT"}};
    for (const auto& p : kPairs) {
        for (int k = 0; k < 2; ++k) {
            const std::string from = p[k], to = p[1 - k];
            const std::size_t at = name.find(from);
            if (at != std::string::npos) { r = name; r.replace(at, from.size(), to); return r; }
        }
    }
    return std::string();
}

}  // namespace blendmod
}  // namespace okay
