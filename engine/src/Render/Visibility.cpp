#include "okay/Render/Visibility.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/LODGroup.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Occluder.hpp"
#include "okay/Components/SkinnedMesh.hpp"
#include "okay/Optimize/BatchPlanner.hpp"
#include "okay/Optimize/LodGroup.hpp"
#include "okay/Optimize/MeshSimplify.hpp"
#include "okay/Optimize/OcclusionBuffer.hpp"
#include "okay/Render/SoftwareRenderer.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace okay {
namespace visibility {
namespace {

struct Cand {
    GameObject* go = nullptr;
    MeshRenderer* mr = nullptr;
    const Mesh* mesh = nullptr;
    Mat4 model;
    Bounds box;
    int lod = -1;
    bool batchable = false;
    bool dynamic = false;
    std::uint64_t key = 0;
};

std::uint64_t Mix(std::uint64_t h, std::uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    return h;
}
std::uint64_t MixF(std::uint64_t h, float f) {
    std::uint32_t u; std::memcpy(&u, &f, 4); return Mix(h, u);
}

std::uint64_t MaterialKey(const MeshRenderer& mr) {
    std::uint64_t h = 1469598103934665603ull;
    h = Mix(h, (std::uint64_t)mr.shader);
    h = Mix(h, mr.unlit ? 1 : 0);
    h = Mix(h, mr.doubleSided ? 1 : 0);
    h = Mix(h, (std::uint64_t)mr.texFilter);
    h = MixF(h, mr.color.r); h = MixF(h, mr.color.g); h = MixF(h, mr.color.b); h = MixF(h, mr.color.a);
    h = MixF(h, mr.emissive.r); h = MixF(h, mr.emissive.g); h = MixF(h, mr.emissive.b);
    h = MixF(h, mr.metallic); h = MixF(h, mr.specular); h = MixF(h, mr.shininess);
    h = MixF(h, mr.reflectivity); h = MixF(h, mr.tiling.x); h = MixF(h, mr.tiling.y);
    h = MixF(h, mr.texOffset.x); h = MixF(h, mr.texOffset.y);
    h = Mix(h, std::hash<std::string>{}(mr.texture));
    h = Mix(h, std::hash<std::string>{}(mr.normalMap));
    h = Mix(h, std::hash<std::string>{}(mr.specularMap));
    h = Mix(h, std::hash<std::string>{}(mr.aoMap));
    h = Mix(h, std::hash<std::string>{}(mr.metallicRoughnessMap));
    h = Mix(h, std::hash<std::string>{}(mr.emissiveMap));
    return h;
}

Bounds WorldBox(const GameObject* go, const Mesh& mesh) {
    Vec3 lo, hi;
    mesh.Bounds(lo, hi);
    Bounds b;
    if (mesh.vertices.empty() || !go->transform) return b;
    const Mat4 M = go->transform->LocalToWorldMatrix();
    for (int i = 0; i < 8; ++i)
        b.Encapsulate(M.MultiplyPoint({(i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z}));
    return b;
}

struct Store {
    Frame frame;
    std::unordered_map<const GameObject*, const Mesh*> lodMesh;
    std::unordered_set<const GameObject*> lodCulled;
    std::unordered_set<const GameObject*> visible;
    std::vector<Mesh> owned;          ///< merged meshes; item pointers address these
    std::uint64_t cacheKey = 0;
    std::vector<Mesh> cache;
};

Store& S() { static Store s; return s; }

const Mesh* ChooseLod(GameObject* go, MeshRenderer* mr, const Vec3& eye, float fovY, int viewH,
                      int& lodOut, bool& culled) {
    lodOut = -1;
    culled = false;
    auto* group = go->GetComponent<LODGroup>();
    const Mesh& src = mr->DisplayMesh();
    if (!group || group->levels.empty() || go->GetComponent<SkinnedMesh>() || go->GetComponent<Character>())
        return &src;
    if (group->forceLevel >= (int)group->levels.size()) { culled = true; group->activeLevel = -1; return nullptr; }
    if (group->forceLevel >= 0) {
        lodOut = group->forceLevel;
        group->activeLevel = lodOut;
        group->lodState = lodOut;
        return group->MeshForLevel(lodOut, mr);
    }
    Vec3 c; float r = 0.5f;
    if (!ObjectWorldSphere(go, mr, c, r) || !(r > 1e-4f)) {
        c = go->transform->Position();
        r = 0.5f;
    }
    const float dist = (c - eye).Magnitude();
    optimize::LodGroup g;
    g.size = std::max(r * 2.0f, 0.01f);
    g.hysteresis = group->hysteresis;
    g.fadeMode = optimize::LodFadeMode::None;
    const int n = std::min((int)group->levels.size(), LODGroup::kMaxLevels);
    g.levels.resize((std::size_t)n);
    optimize::LodView view{(float)std::max(viewH, 1), fovY > 1.0f ? fovY : 60.0f};
    if (group->useScreenSize) {
        for (int i = 0; i < n; ++i)
            g.levels[(std::size_t)i].screenHeight = i + 1 < n ? group->levels[(std::size_t)i].screenSize : 0.0f;
        g.cullScreenHeight = std::max(0.0f, group->cullScreen);
    } else {
        for (int i = 0; i < n; ++i) {
            const float d = std::max(group->levels[(std::size_t)i].distance, 0.05f);
            g.levels[(std::size_t)i].screenHeight = i + 1 < n
                ? std::clamp(optimize::LodScreenHeightFor(g.size, d, view.fovYDeg), 0.001f, 0.999f)
                : 0.0f;
        }
        g.cullScreenHeight = group->cullDistance > 0.05f
            ? std::clamp(optimize::LodScreenHeightFor(g.size, group->cullDistance, view.fovYDeg), 0.0f, 0.5f)
            : 0.0f;
    }
    // Thresholds must fall, or SelectLod rejects the group.
    for (int i = 1; i + 1 < n; ++i)
        if (!(g.levels[(std::size_t)i].screenHeight < g.levels[(std::size_t)i - 1].screenHeight))
            g.levels[(std::size_t)i].screenHeight = g.levels[(std::size_t)i - 1].screenHeight * 0.5f;
    if (n >= 2 && g.cullScreenHeight >= g.levels[(std::size_t)(n - 2)].screenHeight)
        g.cullScreenHeight = g.levels[(std::size_t)(n - 2)].screenHeight * 0.5f;
    if (!g.Valid()) return &src;
    const optimize::LodSelection sel = optimize::SelectLod(g, dist, view, 1.0f, group->lodState);
    group->lodState = sel.state;
    if (sel.culled) { culled = true; group->activeLevel = -1; return nullptr; }
    lodOut = sel.level;
    group->activeLevel = sel.level;
    return group->MeshForLevel(sel.level, mr);
}

} // namespace

const Frame& Current() { return S().frame; }

const Mesh* Frame::ShadowMesh(const GameObject* go, const MeshRenderer* mr) const {
    if (!ready || !go) return mr ? &mr->DisplayMesh() : nullptr;
    if (S().lodCulled.count(go)) return nullptr;
    auto it = S().lodMesh.find(go);
    if (it != S().lodMesh.end() && it->second) return it->second;
    return mr ? &mr->DisplayMesh() : nullptr;
}

bool Frame::Visible(const GameObject* go) const {
    return ready && go && S().visible.count(go) != 0;
}

const Mesh* Frame::MeshFor(const GameObject* go) const {
    if (!Visible(go)) return nullptr;
    auto it = S().lodMesh.find(go);
    return it != S().lodMesh.end() ? it->second : nullptr;
}

const Frame& Build(const Scene& scene, const Mat4& vp, const Vec3& eye, int viewW, int viewH,
                   float fovY, const GameObject* ignore) {
    Store& st = S();
    st.frame = Frame{};
    st.lodMesh.clear();
    st.lodCulled.clear();
    st.visible.clear();
    st.owned.clear();
    const Frustum fr = Frustum::FromVP(vp);
    const bool occOn = OcclusionCullingEnabled();
    optimize::OcclusionBuffer occ(160, 90);
    bool haveOcc = false;
    if (occOn) {
        bool any = false;
        for (const auto& up : scene.Objects()) {
            if (up && up->GetComponent<Occluder>() && up->ActiveInHierarchy()) { any = true; break; }
        }
        if (any) {
            occ.SetViewProjection(vp);
            haveOcc = true;
            int budget = 0;
            for (const auto& up : scene.Objects()) {
                GameObject* go = up.get();
                if (!go || !go->ActiveInHierarchy()) continue;
                auto* oc = go->GetComponent<Occluder>();
                auto* mr = go->GetComponent<MeshRenderer>();
                if (!oc || !mr || !mr->enabled || mr->wireframe || mr->color.a < 0.999f) continue;
                if (CulledByFrustum(fr, go, mr)) continue;
                const Mesh& mesh = mr->DisplayMesh();
                if (mesh.triangles.size() < 3) continue;
                if (oc->useMesh && mesh.TriangleCount() <= 8000) {
                    occ.RasterizeMesh(mesh, go->transform->LocalToWorldMatrix(), true);
                } else {
                    occ.RasterizeBox(WorldBox(go, mesh));
                }
                if (++budget >= 24) break;
            }
        }
    }

    std::vector<Cand> cands;
    cands.reserve(scene.Objects().size());
    for (const auto& up : scene.Objects()) {
        GameObject* go = up.get();
        if (!go || !go->ActiveInHierarchy() || go->HiddenFromCamera(ignore)) continue;
        if (!(RenderCullingMask() & (1 << (go->layer & 31)))) continue;
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr || !mr->enabled || mr->wireframe) continue;
        const Mesh& shown = mr->DisplayMesh();
        if (shown.vertices.empty() || shown.triangles.size() < 3) continue;
        if (CulledByFrustum(fr, go, mr)) { ++st.frame.frustumCulled; continue; }
        int lod = -1; bool lodOff = false;
        const Mesh* mesh = ChooseLod(go, mr, eye, fovY, viewH, lod, lodOff);
        if (lodOff || !mesh) { ++st.frame.lodCulled; st.lodCulled.insert(go); continue; }
        st.lodMesh[go] = mesh;
        if (haveOcc && !go->GetComponent<Occluder>()) {
            const Bounds box = WorldBox(go, *mesh);
            if (!box.IsEmpty() && occ.IsOccluded(box)) { ++st.frame.occluded; continue; }
        }
        Cand c;
        c.go = go; c.mr = mr; c.mesh = mesh; c.model = go->transform->LocalToWorldMatrix();
        c.lod = lod; c.box = WorldBox(go, *mesh);
        const bool skinned = go->GetComponent<SkinnedMesh>() || go->GetComponent<Character>();
        const bool movingUv = mr->uvScroll.x != 0.0f || mr->uvScroll.y != 0.0f;
        const bool opaque = mr->color.a >= 0.999f;
        const bool planar = mr->groundShadow && mr->castShadows && !ShadowsEnabled();
        c.batchable = opaque && !skinned && !movingUv && !mr->outline && !mr->triplanar && !planar
                      && mesh->TriangleCount() > 0 && mesh->TriangleCount() < 20000;
        c.dynamic = c.batchable && !go->isStatic;
        c.batchable = c.batchable && (go->isStatic ? StaticBatchingEnabled() : DynamicBatchingEnabled());
        c.key = MaterialKey(*mr);
        if (lod >= 0 && lod < 4) ++st.frame.lodCount[lod];
        st.visible.insert(go);
        cands.push_back(c);
    }
    st.frame.individuals = (int)cands.size();

    // Static groups share a material. Dynamic groups also share the mesh, and
    // only small ones are merged (a moving crowd must not rebuild a huge mesh).
    struct Group { std::vector<int> idx; bool dyn = false; };
    std::unordered_map<std::uint64_t, Group> groups;
    for (int i = 0; i < (int)cands.size(); ++i) {
        const Cand& c = cands[(std::size_t)i];
        if (!c.batchable) continue;
        std::uint64_t k = c.key;
        if (c.dynamic) k = Mix(k, (std::uint64_t)reinterpret_cast<std::uintptr_t>(c.mesh));
        else k = Mix(k, 1);
        groups[k].idx.push_back(i);
        groups[k].dyn = c.dynamic;
    }
    std::vector<int> consumed(cands.size(), 0);   // 0 draw, 1 swallowed, 2 emit batch
    std::vector<int> batchAt(cands.size(), -1);
    struct Batch { Mesh mesh; MeshRenderer* mr = nullptr; GameObject* go = nullptr; int lod = -1; };
    std::vector<Batch> batches;
    for (auto& kv : groups) {
        Group& g = kv.second;
        if ((int)g.idx.size() < 2) continue;
        int verts = 0;
        for (int i : g.idx) verts += (int)cands[(std::size_t)i].mesh->vertices.size();
        if (g.dyn && (g.idx.size() < 4 || verts > 8000)) continue;
        if (!g.dyn && verts > 65535) continue;
        std::vector<optimize::MergeInput> in;
        in.reserve(g.idx.size());
        for (int i : g.idx) {
            optimize::MergeInput m;
            m.mesh = cands[(std::size_t)i].mesh;
            m.transform = cands[(std::size_t)i].model;
            in.push_back(m);
        }
        optimize::MergeResult merged = optimize::MergeStaticMeshes(in);
        if (merged.mesh.triangles.size() < 3 || merged.skipped == (int)in.size()) continue;
        const int slot = (int)batches.size();
        Batch b;
        b.mesh = std::move(merged.mesh);
        const int first = g.idx.front();
        b.mr = cands[(std::size_t)first].mr;
        b.go = cands[(std::size_t)first].go;
        b.lod = cands[(std::size_t)first].lod;
        for (int i : g.idx) consumed[(std::size_t)i] = 1;
        consumed[(std::size_t)first] = 2;
        batchAt[(std::size_t)first] = slot;
        batches.push_back(std::move(b));
        st.frame.drawsSaved += (int)g.idx.size() - 1;
        ++st.frame.batches;
    }
    st.owned.reserve(batches.size());
    for (Batch& b : batches) st.owned.push_back(std::move(b.mesh));
    st.frame.items.reserve(cands.size());
    for (int i = 0; i < (int)cands.size(); ++i) {
        if (consumed[(std::size_t)i] == 1) continue;
        if (consumed[(std::size_t)i] == 2) {
            const int slot = batchAt[(std::size_t)i];
            Item it;
            it.go = batches[(std::size_t)slot].go;
            it.mr = batches[(std::size_t)slot].mr;
            it.mesh = &st.owned[(std::size_t)slot];
            it.model = Mat4::Identity();
            it.lod = batches[(std::size_t)slot].lod;
            it.batched = true;
            st.frame.items.push_back(it);
            continue;
        }
        const Cand& c = cands[(std::size_t)i];
        Item it;
        it.go = c.go; it.mr = c.mr; it.mesh = c.mesh; it.model = c.model; it.lod = c.lod;
        st.frame.items.push_back(it);
    }
    // Transparents after opaques so a blended surface is not covered by a later solid.
    auto firstBlend = std::stable_partition(st.frame.items.begin(), st.frame.items.end(), [](const Item& it) {
        return it.mr && it.mr->color.a >= 0.999f;
    });
    // ...and the blended ones far to near: blending is order dependent, so a nearer glass
    // pane drawn first would be painted over by the farther one behind it.
    {
        auto distSq = [&](const Item& it) {
            const Vec3 p = it.go && it.go->transform ? it.go->transform->Position() : eye;
            const Vec3 d = p - eye;
            return d.x * d.x + d.y * d.y + d.z * d.z;
        };
        std::stable_sort(firstBlend, st.frame.items.end(),
                         [&](const Item& a, const Item& b) { return distSq(a) > distSq(b); });
    }
    st.frame.ready = true;
    LastCullCounters().drawn = (int)st.frame.items.size();
    LastCullCounters().culled = st.frame.frustumCulled;
    LastCullCounters().occluded = st.frame.occluded;
    LastCullCounters().lodCulled = st.frame.lodCulled;
    LastCullCounters().lod0 = st.frame.lodCount[0];
    LastCullCounters().lod1 = st.frame.lodCount[1];
    LastCullCounters().lod2 = st.frame.lodCount[2];
    LastCullCounters().lod3 = st.frame.lodCount[3];
    LastCullCounters().batches = st.frame.batches;
    LastCullCounters().drawsSaved = st.frame.drawsSaved;
    return st.frame;
}

} // namespace visibility
} // namespace okay
