// A grid of collider world boxes for the no-NavMesh path search (Finding 107). See NavProbeIndex.hpp.
// Written for this engine from the general "uniform grid" spatial index idea (bucket each box into the cells it
// covers; a query visits only the cells it covers); no outside code.
#include "okay/AI/NavProbeIndex.hpp"

#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Scene/Scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace okay {

namespace {

std::uint64_t g_gridBuilds = 0, g_probes = 0, g_shapeTests = 0;

int g_enabled = -1;   // -1 = not decided yet (read the environment once)

constexpr float kMargin = 0.05f;          // slack on every box (metres), far more than Raycast's own bounds pad
constexpr float kFarOut = 1.0e6f;         // boxes reaching past this are tested by every probe
constexpr int kMaxCellsPerBox = 64;       // bigger boxes (a floor) are tested by every probe instead
constexpr int kMaxCellsPerAxis = 256;

bool Finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

} // namespace

bool NavProbeIndex::Enabled() {
    if (g_enabled < 0) {
        const char* e = std::getenv("OKAY_NAV_PROBE_INDEX");
        g_enabled = (e && (std::strcmp(e, "0") == 0 || std::strcmp(e, "off") == 0)) ? 0 : 1;
    }
    return g_enabled == 1;
}
void NavProbeIndex::SetEnabled(bool on) { g_enabled = on ? 1 : 0; }

std::uint64_t NavProbeIndex::GridBuilds() { return g_gridBuilds; }
std::uint64_t NavProbeIndex::Probes() { return g_probes; }
std::uint64_t NavProbeIndex::ShapeTests() { return g_shapeTests; }

NavProbeIndex& NavProbeIndex::For(Scene& scene) {
    static std::vector<std::unique_ptr<NavProbeIndex>> indexes;
    NavProbeIndex* found = nullptr;
    for (auto& p : indexes) if (p->m_uid == scene.Uid()) { found = p.get(); break; }
    if (!found) {
        if (indexes.size() >= 8) indexes.erase(indexes.begin());   // scenes come and go; keep a few
        indexes.push_back(std::make_unique<NavProbeIndex>());
        found = indexes.back().get();
        found->m_uid = scene.Uid();
    }
    found->Refresh(scene);
    return *found;
}

void NavProbeIndex::Refresh(Scene& scene) {
    if (!m_listed || m_structure != scene.StructureVersion() || m_components != scene.ComponentVersion()) {
        m_listed = true;
        m_structure = scene.StructureVersion(); m_components = scene.ComponentVersion();
        m_colliders = scene.physics3D().QueryColliders(scene);
        m_built = false;
    }
    // Measure every box again (cheap next to the probes); rebuild the grid only if one moved.
    const std::size_t n = m_colliders.size();
    if (m_mn.size() != n) { m_mn.assign(n, Vec3{}); m_mx.assign(n, Vec3{}); m_built = false; }
    for (std::size_t i = 0; i < n; ++i) {
        Vec3 mn, mx;
        m_colliders[i]->WorldAABB(mn, mx);
        if (Finite(mn) && Finite(mx)) {
            const Vec3 pad = (mx - mn) * 1e-3f + Vec3(kMargin, kMargin, kMargin);
            mn = mn - pad; mx = mx + pad;
        }
        if (mn.x != m_mn[i].x || mn.y != m_mn[i].y || mn.z != m_mn[i].z ||
            mx.x != m_mx[i].x || mx.y != m_mx[i].y || mx.z != m_mx[i].z) {
            m_mn[i] = mn; m_mx[i] = mx; m_built = false;
        }
    }
    if (m_seen.size() != n) { m_seen.assign(n, 0u); m_gather = 0; }
    if (!m_built) BuildGrid();
}

void NavProbeIndex::BuildGrid() {
    m_built = true;
    ++g_gridBuilds;
    const std::size_t n = m_colliders.size();
    m_always.clear(); m_cellStart.clear(); m_cellItems.clear(); m_nx = m_nz = 0;
    std::vector<char> placed(n, 0);
    std::vector<float> sizes;
    float lox = 1e30f, loz = 1e30f, hix = -1e30f, hiz = -1e30f;
    for (std::size_t i = 0; i < n; ++i) {
        const Vec3& mn = m_mn[i]; const Vec3& mx = m_mx[i];
        const bool usable = Finite(mn) && Finite(mx) && mn.x <= mx.x && mn.z <= mx.z &&
                            std::fabs(mn.x) < kFarOut && std::fabs(mx.x) < kFarOut &&
                            std::fabs(mn.z) < kFarOut && std::fabs(mx.z) < kFarOut;
        if (!usable) { m_always.push_back((int)i); continue; }
        placed[i] = 1;
        lox = std::min(lox, mn.x); hix = std::max(hix, mx.x);
        loz = std::min(loz, mn.z); hiz = std::max(hiz, mx.z);
        sizes.push_back(std::max(mx.x - mn.x, mx.z - mn.z));
    }
    if (sizes.empty()) return;
    // Cells about twice the typical box, so most boxes sit in one to four cells; no more than 256 a side.
    std::nth_element(sizes.begin(), sizes.begin() + (long)(sizes.size() / 2), sizes.end());
    m_cell = std::max(1.0f, 2.0f * sizes[sizes.size() / 2]);
    m_cell = std::max(m_cell, std::max((hix - lox), (hiz - loz)) / (float)kMaxCellsPerAxis);
    m_x0 = lox; m_z0 = loz;
    m_nx = std::min(kMaxCellsPerAxis + 1, (int)((hix - lox) / m_cell) + 1);
    m_nz = std::min(kMaxCellsPerAxis + 1, (int)((hiz - loz) / m_cell) + 1);
    auto range = [&](std::size_t i, int& cx0, int& cx1, int& cz0, int& cz1) {
        cx0 = std::clamp((int)std::floor((m_mn[i].x - m_x0) / m_cell), 0, m_nx - 1);
        cx1 = std::clamp((int)std::floor((m_mx[i].x - m_x0) / m_cell), 0, m_nx - 1);
        cz0 = std::clamp((int)std::floor((m_mn[i].z - m_z0) / m_cell), 0, m_nz - 1);
        cz1 = std::clamp((int)std::floor((m_mx[i].z - m_z0) / m_cell), 0, m_nz - 1);
    };
    std::vector<int> count((std::size_t)m_nx * (std::size_t)m_nz + 1, 0);
    for (std::size_t i = 0; i < n; ++i) {
        if (!placed[i]) continue;
        int cx0, cx1, cz0, cz1; range(i, cx0, cx1, cz0, cz1);
        if ((cx1 - cx0 + 1) * (cz1 - cz0 + 1) > kMaxCellsPerBox) { placed[i] = 0; m_always.push_back((int)i); continue; }
        for (int cz = cz0; cz <= cz1; ++cz)
            for (int cx = cx0; cx <= cx1; ++cx) ++count[(std::size_t)cz * (std::size_t)m_nx + (std::size_t)cx + 1];
    }
    std::sort(m_always.begin(), m_always.end());
    m_cellStart.assign(count.size(), 0);
    for (std::size_t k = 1; k < count.size(); ++k) m_cellStart[k] = m_cellStart[k - 1] + count[k];
    m_cellItems.assign((std::size_t)m_cellStart.back(), 0);
    std::vector<int> fill(m_cellStart.begin(), m_cellStart.end() - 1);
    for (std::size_t i = 0; i < n; ++i) {
        if (!placed[i]) continue;
        int cx0, cx1, cz0, cz1; range(i, cx0, cx1, cz0, cz1);
        for (int cz = cz0; cz <= cz1; ++cz)
            for (int cx = cx0; cx <= cx1; ++cx)
                m_cellItems[(std::size_t)fill[(std::size_t)cz * (std::size_t)m_nx + (std::size_t)cx]++] = (int)i;
    }
}

void NavProbeIndex::Gather(float x0, float y0, float z0, float x1, float y1, float z1) {
    m_scratchIdx.clear();
    if (++m_gather == 0) { std::fill(m_seen.begin(), m_seen.end(), 0u); m_gather = 1; }
    auto take = [&](int i) {
        if (m_seen[(std::size_t)i] == m_gather) return;
        m_seen[(std::size_t)i] = m_gather;
        const Vec3& mn = m_mn[(std::size_t)i]; const Vec3& mx = m_mx[(std::size_t)i];
        // Inclusive overlap of the padded box with the query box (a non-finite box is always taken).
        const bool finite = Finite(mn) && Finite(mx);
        if (finite && (mx.x < x0 || mn.x > x1 || mx.y < y0 || mn.y > y1 || mx.z < z0 || mn.z > z1)) return;
        m_scratchIdx.push_back(i);
    };
    if (m_nx > 0) {
        // (clamped as floats first: a huge but finite coordinate must not overflow an int)
        auto cellOf = [](float v, float o, float cell, int count) {
            float f = std::floor((v - o) / cell);
            if (!(f >= 0.0f)) return 0;
            if (f > (float)(count - 1)) return count - 1;
            return (int)f;
        };
        const int cx0 = cellOf(x0, m_x0, m_cell, m_nx), cx1 = cellOf(x1, m_x0, m_cell, m_nx);
        const int cz0 = cellOf(z0, m_z0, m_cell, m_nz), cz1 = cellOf(z1, m_z0, m_cell, m_nz);
        for (int cz = cz0; cz <= cz1; ++cz)
            for (int cx = cx0; cx <= cx1; ++cx) {
                const std::size_t c = (std::size_t)cz * (std::size_t)m_nx + (std::size_t)cx;
                for (int k = m_cellStart[c]; k < m_cellStart[c + 1]; ++k) take(m_cellItems[(std::size_t)k]);
            }
    }
    for (int i : m_always) take(i);
    std::sort(m_scratchIdx.begin(), m_scratchIdx.end());   // keep the order Raycast walks them in
    m_candidates.clear();
    for (int i : m_scratchIdx) m_candidates.push_back(m_colliders[(std::size_t)i]);
    ++g_probes;
    g_shapeTests += m_candidates.size();
}

RaycastHit3D NavProbeIndex::CastDown(Scene& scene, const Vec3& origin, float maxDistance, GameObject* ignore) {
    Gather(origin.x, origin.y - maxDistance, origin.z, origin.x, origin.y, origin.z);
    return scene.physics3D().RaycastAmong(scene, m_candidates, origin, Vec3{0.0f, -1.0f, 0.0f}, maxDistance, ignore);
}

const std::vector<Collider3D*>& NavProbeIndex::OverlapSphere(Scene& scene, const Vec3& center, float radius) {
    Gather(center.x - radius, center.y - radius, center.z - radius, center.x + radius, center.y + radius, center.z + radius);
    m_hits.clear();
    if (!m_candidates.empty()) m_hits = scene.physics3D().OverlapSphereAmong(m_candidates, center, radius);
    return m_hits;
}

} // namespace okay
