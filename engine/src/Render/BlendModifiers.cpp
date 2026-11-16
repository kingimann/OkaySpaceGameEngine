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

// One Catmull-Clark (or linear) step of a D-dimensional value per vertex. `vals` has nV * D floats; the result has
// (nV + nE + nF) * D floats: vertex points, edge points, face points. edgeSharp (nE, may be empty) is the DeRose
// sharpness of each edge; edges that are not shared by exactly two faces are always sharp.
template <int D>
void Refine(const Topo& t, const std::vector<int>& fs, const std::vector<int>& fc, const std::vector<int>& lv, const float* vals,
            const std::vector<float>& edgeSharp, bool linear, std::vector<float>& out) {
    const std::size_t nV = (std::size_t)t.nV, nE = (std::size_t)t.nE, nF = fs.size();
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
    auto sharpOf = [&](std::size_t e) -> float {
        if (eCnt[e] != 2) return kSharp;
        return edgeSharp.empty() ? 0.0f : edgeSharp[e];
    };
    // edge points
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
    std::vector<float> vFace(nV * D, 0.0f), vMid(nV * D, 0.0f), vSharpOther(nV * D, 0.0f), vSharpSum(nV, 0.0f);
    std::vector<int> vFaceCnt(nV, 0), vEdgeCnt(nV, 0), vSharpCnt(nV, 0);
    for (std::size_t f = 0; f < nF; ++f) {
        const std::size_t s = (std::size_t)fs[f], n = (std::size_t)fc[f];
        for (std::size_t l = s; l < s + n; ++l) {
            const std::size_t v = (std::size_t)lv[l];
            ++vFaceCnt[v];
            for (int d = 0; d < D; ++d) vFace[v * D + d] += FP[f * D + d];
        }
    }
    for (std::size_t e = 0; e < nE; ++e) {
        const std::size_t ia = (std::size_t)t.e0[e], ib = (std::size_t)t.e1[e];
        if (ia == ib) continue;
        const float* a = vals + ia * D;
        const float* b = vals + ib * D;
        ++vEdgeCnt[ia]; ++vEdgeCnt[ib];
        for (int d = 0; d < D; ++d) { const float m = 0.5f * (a[d] + b[d]); vMid[ia * D + d] += m; vMid[ib * D + d] += m; }
        const float s = sharpOf(e);
        if (s > 0.0f) {
            const float w = std::min(s, 1.0f);
            ++vSharpCnt[ia]; ++vSharpCnt[ib]; vSharpSum[ia] += w; vSharpSum[ib] += w;
            for (int d = 0; d < D; ++d) { vSharpOther[ia * D + d] += b[d]; vSharpOther[ib * D + d] += a[d]; }
        }
    }
    for (std::size_t v = 0; v < nV; ++v) {
        const float* S = vals + v * D;
        float* o = VP + v * D;
        const int n = vEdgeCnt[v], k = vFaceCnt[v], sc = vSharpCnt[v];
        if (n == 0 || k == 0) { for (int d = 0; d < D; ++d) o[d] = S[d]; continue; }
        float smooth[D];
        const float invK = 1.0f / (float)k, invN = 1.0f / (float)n;
        for (int d = 0; d < D; ++d) smooth[d] = (vFace[v * D + d] * invK + 2.0f * vMid[v * D + d] * invN + (float)(n - 3) * S[d]) * invN;
        if (sc < 2) { for (int d = 0; d < D; ++d) o[d] = smooth[d]; continue; }   // smooth vertex or dart
        float sharp[D];
        if (sc == 2) for (int d = 0; d < D; ++d) sharp[d] = (vSharpOther[v * D + d] + 6.0f * S[d]) * 0.125f;   // crease / boundary curve
        else for (int d = 0; d < D; ++d) sharp[d] = S[d];                                                       // corner
        const float w = std::min(1.0f, vSharpSum[v] / (float)sc);
        for (int d = 0; d < D; ++d) o[d] = w >= 1.0f ? sharp[d] : (1.0f - w) * smooth[d] + w * sharp[d];
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

// One subdivision level. False (mesh unchanged) when the result would pass maxFaces.
bool SubdivideOnce(PolyMesh& m, const SubsurfSettings& st, std::size_t maxFaces, int levelsLeft) {
    const std::size_t L = m.loopVert.size(), nF = m.faceStart.size();
    if (!m.loopUv.empty() && m.loopUv.size() != L) m.loopUv.clear();
    if (L > maxFaces || L > (std::size_t)std::numeric_limits<int>::max() / 4) return false;
    const int nV = (int)m.pos.size();
    Topo t;
    BuildTopo(m.faceStart, m.faceCount, m.loopVert, nV, t);
    const std::size_t nE = (std::size_t)t.nE;
    if ((std::size_t)nV + nE + nF > (std::size_t)std::numeric_limits<int>::max() / 2) return false;

    // creases -> per edge sharpness
    std::vector<float> edgeSharp;
    std::unordered_map<std::uint64_t, float> creaseOf;
    if (st.useCreases && !m.creases.empty() && !st.simple) {
        creaseOf.reserve(m.creases.size() * 2);
        for (const Crease& c : m.creases) if (c.sharpness > 0.0f && c.v0 != c.v1) { float& s = creaseOf[EdgeKey(c.v0, c.v1)]; s = std::max(s, c.sharpness); }
        if (!creaseOf.empty()) {
            edgeSharp.assign(nE, 0.0f);
            for (std::size_t e = 0; e < nE; ++e) { auto it = creaseOf.find(EdgeKey(t.e0[e], t.e1[e])); if (it != creaseOf.end()) edgeSharp[e] = it->second; }
        }
    }

    std::vector<float> outPos;
    Refine<3>(t, m.faceStart, m.faceCount, m.loopVert, &m.pos[0].x, edgeSharp, st.simple, outPos);
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

    // UVs: linear, or Catmull-Clark on the UV topology (corners welded by vertex and UV; seams become boundaries)
    if (!m.loopUv.empty()) {
        r.loopUv.resize(L * 4);
        if (st.simple || !st.smoothUv) {
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
            // weld corners: bucket by vertex, then by UV bits inside the bucket
            std::vector<int> start((std::size_t)nV + 1, 0);
            for (std::size_t l = 0; l < L; ++l) ++start[(std::size_t)m.loopVert[l] + 1];
            for (int v = 0; v < nV; ++v) start[(std::size_t)v + 1] += start[(std::size_t)v];
            std::vector<std::pair<std::uint64_t, int>> items(L);
            std::vector<int> fill(start.begin(), start.end() - 1);
            for (std::size_t l = 0; l < L; ++l) {
                std::uint32_t ux, uy; std::memcpy(&ux, &m.loopUv[l].x, 4); std::memcpy(&uy, &m.loopUv[l].y, 4);
                items[(std::size_t)fill[(std::size_t)m.loopVert[l]]++] = {((std::uint64_t)ux << 32) | uy, (int)l};
            }
            std::vector<int> uvLoopVert(L);
            std::vector<Vec2> uvVals;
            uvVals.reserve((std::size_t)nV + nV / 4 + 4);
            for (int v = 0; v < nV; ++v) {
                auto b = items.begin() + start[(std::size_t)v], e = items.begin() + start[(std::size_t)v + 1];
                if (e - b > 1) std::sort(b, e);
                std::uint64_t prevKey = 0; bool first = true;
                for (auto it = b; it != e; ++it) {
                    if (first || it->first != prevKey) { uvVals.push_back(m.loopUv[(std::size_t)it->second]); prevKey = it->first; first = false; }
                    uvLoopVert[(std::size_t)it->second] = (int)uvVals.size() - 1;
                }
            }
            Topo tu;
            BuildTopo(m.faceStart, m.faceCount, uvLoopVert, (int)uvVals.size(), tu);
            std::vector<float> outUv;
            Refine<2>(tu, m.faceStart, m.faceCount, uvLoopVert, &uvVals[0].x, std::vector<float>(), false, outUv);
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

    // creases one level down: both halves of a creased edge keep sharpness s - 1
    if (!creaseOf.empty() && levelsLeft > 1) {
        for (std::size_t e = 0; e < nE; ++e) {
            if (edgeSharp[e] <= 1.0f) continue;
            const float s = std::isinf(edgeSharp[e]) ? edgeSharp[e] : edgeSharp[e] - 1.0f;
            r.creases.push_back(Crease{t.e0[e], eBase + (int)e, s});
            r.creases.push_back(Crease{eBase + (int)e, t.e1[e], s});
        }
    }
    m = std::move(r);
    return true;
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
    for (int lv = 0; lv < s.levels; ++lv) {
        if (m.faceStart.empty() || m.pos.empty()) break;
        if (!SubdivideOnce(m, s, maxFaces, s.levels - lv)) break;
        ++done;
    }
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
