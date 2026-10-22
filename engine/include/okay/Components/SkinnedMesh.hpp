#pragma once
// ---------------------------------------------------------------------------
// SkinnedMesh — CPU skinning. Deforms a bind-pose mesh by a set of bone (joint)
// Transforms each frame and writes the result into the sibling MeshRenderer, so a
// rigged glTF character bends with its skeleton (not just rigid per-node motion).
//
// Per vertex it blends up to 4 joints:  v' = Σ wᵢ · (mesh⁻¹ · jointWorldᵢ · invBindᵢ) · v
// The mesh-node inverse keeps the result in the renderer's local space (the renderer
// re-applies the node transform), so it's correct wherever the mesh node sits.
//
// Built by the importers; animate the joint Transforms (e.g. via a ModelAnimator)
// and the mesh follows. Joint pointers are runtime-resolved (by name, inside the
// mesh's own model first) and not serialized.
//
// `method` picks the blend: Linear (classic, GPU-skinnable) or Dual quaternion,
// which keeps volume at twisting / bending joints (no "candy wrapper" wrists or
// collapsing elbows). Dual quaternion runs on the CPU.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Math/Mat4.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace okay {

class SkinnedMesh : public Behaviour {
public:
    Mesh bind;                                   ///< bind-pose mesh (positions/normals/uvs/triangles)
    std::vector<std::array<int, 4>>   jointIdx;  ///< per-vertex joint indices (into `joints`)
    std::vector<std::array<float, 4>> jointWt;   ///< per-vertex blend weights (sum ~1)
    std::vector<Transform*>  joints;             ///< bone transforms (runtime; resolved from jointNames)
    std::vector<std::string> jointNames;         ///< bone object names (serialized; resolved on load)
    std::vector<Mat4>        inverseBind;         ///< per-joint inverse bind matrix

    enum class Method { Linear = 0, DualQuaternion = 1 };
    int method = (int)Method::Linear;            ///< see Method

    /// Bumped every time Skin() rewrites the deformed mesh. The GPU renderers key
    /// their cached vertex buffer on it: an idle rig is never re-uploaded, a moving
    /// one is uploaded once per frame however many views / passes draw it.
    std::uint64_t version = 1;
    /// Bumped when the bind-pose mesh handed to a GPU-skinning renderer changes
    /// (switching to GPU skinning, blend shapes). Its vertex buffer is keyed on
    /// this, so a moving GPU-skinned character is never re-uploaded.
    std::uint64_t bindVersion = 1;

    /// Skin matrices from the last Skin(): meshNode^-1 * jointWorld * inverseBind,
    /// one per joint. A GPU renderer that skins in its vertex shader draws `bind`
    /// with these instead of the CPU-deformed mesh.
    std::vector<Mat4> palette;
    /// Max joints a GPU skinning shader takes; bigger rigs deform on the CPU.
    static constexpr int kMaxGpuJoints = 256;

    /// Set by the host when every view it draws goes through a renderer that
    /// skins on the GPU: Skin() then only builds the palette (no per-vertex CPU
    /// work, no mesh re-upload) and MeshRenderer::mesh keeps the bind pose.
    static bool& GpuSkinning() { static bool on = false; return on; }
    /// Joints the active GPU renderer's shader takes (<= kMaxGpuJoints); set by
    /// the host with GpuSkinning. Bigger rigs keep deforming on the CPU.
    static int& GpuMaxJoints() { static int n = kMaxGpuJoints; return n; }
    /// Whether this mesh is being skinned on the GPU right now.
    bool UsesGpuSkinning() const {
        return GpuSkinning() && method == (int)Method::Linear && (int)joints.size() <= GpuMaxJoints() &&
               (int)joints.size() <= kMaxGpuJoints && Ready();
    }

    void Start() override  { ResolveJoints(); Skin(); }
    // Skin last: after animation, crossfades, layers and IK have all posed the
    // bones (IK runs at order 100), so this frame's corrections reach the mesh.
    int  ExecutionOrder() const override { return 200; }
    void LateUpdate(float) override { Skin(); }

    /// Resolve joint Transforms by name (after a load, when the pointers are empty
    /// but the names round-tripped). Bones are looked up inside this mesh's own
    /// model first (its top-most ancestor's subtree), so two copies of the same
    /// character — same bone names — each follow their own skeleton. No-op once
    /// joints are populated.
    void ResolveJoints() {
        if (!joints.empty() || jointNames.empty() || !gameObject) return;
        Scene* sc = gameObject->scene();
        if (!sc) return;
        Transform* top = transform;
        while (top && top->Parent()) top = top->Parent();
        std::unordered_map<std::string, Transform*> local;
        if (top) {
            std::vector<Transform*> stack{top};
            while (!stack.empty()) {
                Transform* t = stack.back(); stack.pop_back();
                if (t->gameObject) local.emplace(t->gameObject->name, t);
                for (Transform* c : t->Children()) stack.push_back(c);
            }
        }
        joints.clear();
        for (const std::string& nm : jointNames) {
            auto it = local.find(nm);
            if (it != local.end()) { joints.push_back(it->second); continue; }
            GameObject* g = sc->Find(nm);
            joints.push_back(g ? g->transform : nullptr);
        }
    }

    // ---- Rig checks (inspector / import report) ----
    /// Joints whose bone object couldn't be found.
    int MissingJoints() const { int n = 0; for (Transform* t : joints) if (!t) ++n; return n; }
    /// Vertices with no weight at all (they stay in the bind pose).
    int UnweightedVertices() const {
        int n = 0;
        for (const auto& w : jointWt) if (w[0] + w[1] + w[2] + w[3] <= 1e-6f) ++n;
        return n;
    }
    /// The most bones any one vertex is weighted to (1..4).
    int MaxInfluences() const {
        int m = 0;
        for (const auto& w : jointWt) { int c = 0; for (float x : w) if (x > 1e-4f) ++c; if (c > m) m = c; }
        return m;
    }
    /// Make every vertex's weights sum to 1 and drop tiny ones (< `minWeight`),
    /// which otherwise drag the vertex toward a far-away bone. Returns vertices changed.
    int NormalizeWeights(float minWeight = 0.01f) {
        int changed = 0;
        for (auto& w : jointWt) {
            std::array<float, 4> o = w;
            float sum = 0.0f;
            for (float& x : w) { if (x < minWeight) x = 0.0f; sum += x; }
            if (sum <= 1e-6f) { w = o; continue; }   // keep unweighted / all-tiny vertices as they were
            for (float& x : w) x /= sum;
            if (w != o) ++changed;
        }
        if (changed) m_haveHash = false;
        return changed;
    }
    /// How many vertices each joint moves (weight > 0) — for spotting dead bones.
    std::vector<int> VerticesPerJoint() const {
        std::vector<int> c(joints.size(), 0);
        for (std::size_t v = 0; v < jointIdx.size() && v < jointWt.size(); ++v)
            for (int k = 0; k < 4; ++k)
                if (jointWt[v][k] > 1e-4f && jointIdx[v][k] >= 0 && jointIdx[v][k] < (int)c.size()) ++c[jointIdx[v][k]];
        return c;
    }

    /// Call after changing `bind` (blend shapes): the next Skin() redeforms, and
    /// a GPU-skinned mesh re-uploads its bind pose.
    void BindChanged() { m_haveHash = false; m_bindDirty = true; }

    /// True once it has joints + a bind mesh to deform.
    bool Ready() const { return !joints.empty() && !bind.vertices.empty(); }

    /// Recompute the deformed mesh from the current joint poses. Skips the whole
    /// deform when nothing moved since the last call (byte-exact hash of the mesh
    /// node's and every joint's world matrix) — idle rigs then cost ~nothing.
    void Skin() {
        if (!gameObject || !Ready()) return;
        auto* mr = gameObject->GetComponent<MeshRenderer>();
        if (!mr) return;

        // skinMatrixⱼ = meshNode⁻¹ · jointWorldⱼ · inverseBindⱼ
        // "Did anything move?" from the transforms' world versions (they change
        // exactly when a world matrix does), so an idle rig costs a few compares.
        std::uint64_t h = 1469598103934665603ull;
        auto mix = [&h](std::uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); };
        mix(transform ? transform->WorldVersion() : 0);
        for (Transform* jt : joints) mix(jt ? jt->WorldVersion() : 0);
        mix((std::uint64_t)method * 7919u + jointWt.size());
        const bool gpu = UsesGpuSkinning();
        // Skip when nothing moved — unless we're switching between CPU and GPU
        // skinning (the mesh must go back to / leave the bind pose).
        if (m_haveHash && h == m_lastHash && gpu == m_lastGpu && !mr->mesh.vertices.empty()) return;
        m_lastHash = h; m_haveHash = true;
        ++version;

        Mat4 meshWorld = transform ? transform->LocalToWorldMatrix() : Mat4::Identity();
        Mat4 meshInv = meshWorld.Inverse();
        std::vector<Mat4>& sk = palette;
        sk.resize(joints.size());
        for (std::size_t j = 0; j < joints.size(); ++j) {
            Mat4 jw = joints[j] ? joints[j]->LocalToWorldMatrix() : Mat4::Identity();
            Mat4 ib = j < inverseBind.size() ? inverseBind[j] : Mat4::Identity();
            sk[j] = meshInv * jw * ib;
        }
        if (gpu) {
            // The vertex shader deforms; leave the bind pose in the renderer's mesh
            // (restored once when switching over from CPU skinning).
            if (!m_lastGpu || m_bindDirty || mr->mesh.vertices.size() != bind.vertices.size()) { mr->mesh = bind; ++bindVersion; }
            m_bindDirty = false;
            m_lastGpu = true;
            mr->deformed = false;
            return;
        }
        m_lastGpu = false;

        // Deform IN PLACE: after the first frame the renderer's mesh already has the
        // bind topology, so only positions/normals are rewritten — no per-frame copy
        // of the whole mesh, and the arrays keep their addresses (the GPU renderers
        // then re-upload once per frame and share it across views and passes).
        Mesh& out = mr->mesh;
        if (out.vertices.size() != bind.vertices.size() || out.triangles.size() != bind.triangles.size() ||
            out.normals.size() != bind.normals.size() || out.uvs.size() != bind.uvs.size())
            out = bind;   // first deform (or the bind mesh changed): take the topology once
        const bool hasN = bind.normals.size() == bind.vertices.size();
        mr->deformed = true;
        if (method == (int)Method::DualQuaternion) { SkinDualQuat(out, hasN); return; }
        // Skin matrices as 3x4 affine rows (no perspective row, no divide). Per
        // vertex the weighted matrices are summed first, then applied once to the
        // position and normal — 12 multiply-adds per influence instead of a full
        // 4x4 transform of both.
        const std::size_t nj = sk.size();
        m_aff.resize(nj);
        for (std::size_t j = 0; j < nj; ++j) {
            const Mat4& M = sk[j];
            float* a = m_aff[j].m;
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 4; ++c) a[r * 4 + c] = M.at(c, r);
        }
        const int njInt = (int)nj;
        for (std::size_t v = 0; v < bind.vertices.size(); ++v) {
            const std::array<int, 4>&   ji = jointIdx[v];
            const std::array<float, 4>& jw = jointWt[v];
            float B[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
            float total = 0.0f;
            for (int k = 0; k < 4; ++k) {
                const float w = jw[k];
                const int idx = ji[k];
                if (w <= 0.0f || idx < 0 || idx >= njInt) continue;
                const float* a = m_aff[idx].m;
                for (int e = 0; e < 12; ++e) B[e] += a[e] * w;
                total += w;
            }
            const Vec3 p = bind.vertices[v];
            if (total <= 1e-6f) {   // unweighted: the bind-pose vertex
                out.vertices[v] = p;
                if (hasN) out.normals[v] = bind.normals[v];
                continue;
            }
            const float it = (total > 0.999f && total < 1.001f) ? 1.0f : 1.0f / total;   // guard un-normalized weights
            out.vertices[v] = Vec3{(B[0] * p.x + B[1] * p.y + B[2]  * p.z + B[3])  * it,
                                   (B[4] * p.x + B[5] * p.y + B[6]  * p.z + B[7])  * it,
                                   (B[8] * p.x + B[9] * p.y + B[10] * p.z + B[11]) * it};
            if (hasN) {
                const Vec3 n = bind.normals[v];
                Vec3 nn{B[0] * n.x + B[1] * n.y + B[2]  * n.z,
                        B[4] * n.x + B[5] * n.y + B[6]  * n.z,
                        B[8] * n.x + B[9] * n.y + B[10] * n.z};
                float l2 = nn.x * nn.x + nn.y * nn.y + nn.z * nn.z;
                out.normals[v] = l2 > 1e-12f ? nn * (1.0f / std::sqrt(l2)) : n;
            }
        }
    }

private:
    /// Dual-quaternion blend: each joint's skin matrix becomes a rotation +
    /// translation (dual quaternion) plus a per-axis scale; per vertex the dual
    /// quaternions are blended (sign-aligned) and normalized, so the result is
    /// always a rigid transform — no volume loss at twisting joints.
    void SkinDualQuat(Mesh& out, bool hasN) {
        const std::size_t nj = palette.size();
        m_dqR.resize(nj); m_dqD.resize(nj); m_dqS.resize(nj);
        for (std::size_t j = 0; j < nj; ++j) {
            const Mat4& M = palette[j];
            Vec3 c0{M.at(0, 0), M.at(0, 1), M.at(0, 2)}, c1{M.at(1, 0), M.at(1, 1), M.at(1, 2)}, c2{M.at(2, 0), M.at(2, 1), M.at(2, 2)};
            Vec3 sc{c0.Magnitude(), c1.Magnitude(), c2.Magnitude()};
            if (sc.x > 1e-8f) c0 = c0 * (1.0f / sc.x);
            if (sc.y > 1e-8f) c1 = c1 * (1.0f / sc.y);
            if (sc.z > 1e-8f) c2 = c2 * (1.0f / sc.z);
            Quat r = Quat::FromRotationMatrix(c0.x, c1.x, c2.x, c0.y, c1.y, c2.y, c0.z, c1.z, c2.z);
            Vec3 t{M.at(3, 0), M.at(3, 1), M.at(3, 2)};
            Quat tq{t.x, t.y, t.z, 0.0f};
            Quat d = tq * r;
            m_dqR[j] = r; m_dqD[j] = Quat{d.x * 0.5f, d.y * 0.5f, d.z * 0.5f, d.w * 0.5f}; m_dqS[j] = sc;
        }
        for (std::size_t v = 0; v < bind.vertices.size(); ++v) {
            const std::array<int, 4>&   ji = jointIdx[v];
            const std::array<float, 4>& jw = jointWt[v];
            Quat br{0, 0, 0, 0}, bd{0, 0, 0, 0}; Vec3 bs{0, 0, 0};
            float total = 0.0f; Quat pivot; bool havePivot = false;
            for (int k = 0; k < 4; ++k) {
                float w = jw[k];
                int idx = ji[k];
                if (w <= 0.0f || idx < 0 || idx >= (int)nj) continue;
                const Quat& r = m_dqR[idx]; const Quat& d = m_dqD[idx];
                if (!havePivot) { pivot = r; havePivot = true; }
                float sw = (pivot.x * r.x + pivot.y * r.y + pivot.z * r.z + pivot.w * r.w) < 0.0f ? -w : w;
                br.x += r.x * sw; br.y += r.y * sw; br.z += r.z * sw; br.w += r.w * sw;
                bd.x += d.x * sw; bd.y += d.y * sw; bd.z += d.z * sw; bd.w += d.w * sw;
                bs = bs + m_dqS[idx] * w;
                total += w;
            }
            const Vec3 p = bind.vertices[v];
            float len = std::sqrt(br.x * br.x + br.y * br.y + br.z * br.z + br.w * br.w);
            if (total <= 1e-6f || len < 1e-8f) {
                out.vertices[v] = p;
                if (hasN) out.normals[v] = bind.normals[v];
                continue;
            }
            float inv = 1.0f / len;
            br = Quat{br.x * inv, br.y * inv, br.z * inv, br.w * inv};
            bd = Quat{bd.x * inv, bd.y * inv, bd.z * inv, bd.w * inv};
            bs = bs * (1.0f / total);
            // translation = 2 * dual * conj(real)
            Quat tq = bd * br.Conjugate();
            Vec3 tr{tq.x * 2.0f, tq.y * 2.0f, tq.z * 2.0f};
            Vec3 ps{p.x * bs.x, p.y * bs.y, p.z * bs.z};
            out.vertices[v] = br * ps + tr;
            if (hasN) {
                Vec3 n = br * bind.normals[v];
                float l = n.Magnitude();
                out.normals[v] = l > 1e-6f ? n * (1.0f / l) : bind.normals[v];
            }
        }
    }

    struct Affine { float m[12]; };
    std::vector<Affine> m_aff;
    std::vector<Quat> m_dqR, m_dqD;
    std::vector<Vec3> m_dqS;
    std::uint64_t m_lastHash = 0;    ///< pose hash of the last deform (skip when equal)
    bool          m_bindDirty = false;   ///< bind changed (blend shapes): re-copy it for GPU skinning
    bool          m_haveHash = false;
    bool          m_lastGpu = false;     ///< the last Skin() left the deform to the GPU
};

} // namespace okay
