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
// Built by the glTF importer; animate the joint Transforms (e.g. via Animator clips
// imported alongside) and the mesh follows. Joint pointers are runtime-resolved and
// not serialized.
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

namespace okay {

class SkinnedMesh : public Behaviour {
public:
    Mesh bind;                                   ///< bind-pose mesh (positions/normals/uvs/triangles)
    std::vector<std::array<int, 4>>   jointIdx;  ///< per-vertex joint indices (into `joints`)
    std::vector<std::array<float, 4>> jointWt;   ///< per-vertex blend weights (sum ~1)
    std::vector<Transform*>  joints;             ///< bone transforms (runtime; resolved from jointNames)
    std::vector<std::string> jointNames;         ///< bone object names (serialized; resolved on load)
    std::vector<Mat4>        inverseBind;         ///< per-joint inverse bind matrix

    /// Bumped every time Skin() rewrites the deformed mesh. The GPU renderers key
    /// their cached vertex buffer on it: an idle rig is never re-uploaded, a moving
    /// one is uploaded once per frame however many views / passes draw it.
    std::uint64_t version = 1;

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
    /// Whether this mesh is being skinned on the GPU right now.
    bool UsesGpuSkinning() const { return GpuSkinning() && (int)joints.size() <= kMaxGpuJoints && Ready(); }

    void Start() override  { ResolveJoints(); Skin(); }
    void Update(float) override { Skin(); }

    /// Resolve joint Transforms by name from the scene (after a load, when the pointers
    /// are empty but the names round-tripped). No-op once joints are populated.
    void ResolveJoints() {
        if (!joints.empty() || jointNames.empty() || !gameObject) return;
        Scene* sc = gameObject->scene();
        if (!sc) return;
        joints.clear();
        for (const std::string& nm : jointNames) {
            GameObject* g = sc->Find(nm);
            joints.push_back(g ? g->transform : nullptr);
        }
    }

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
        Mat4 meshWorld = transform ? transform->LocalToWorldMatrix() : Mat4::Identity();
        std::uint64_t h = 1469598103934665603ull;   // FNV-1a over the raw matrix bytes
        auto mix = [&h](const Mat4& m) {
            const unsigned char* b = reinterpret_cast<const unsigned char*>(m.m);
            for (std::size_t i = 0; i < sizeof(m.m); ++i) { h ^= b[i]; h *= 1099511628211ull; }
        };
        mix(meshWorld);
        std::vector<Mat4> sk(joints.size());
        for (std::size_t j = 0; j < joints.size(); ++j) {
            Mat4 jw = joints[j] ? joints[j]->LocalToWorldMatrix() : Mat4::Identity();
            mix(jw);
            sk[j] = jw;   // finished below once we know we're not skipping
        }
        const bool gpu = UsesGpuSkinning();
        // Skip when nothing moved — unless we're switching between CPU and GPU
        // skinning (the mesh must go back to / leave the bind pose).
        if (m_haveHash && h == m_lastHash && gpu == m_lastGpu && !mr->mesh.vertices.empty()) return;
        m_lastHash = h; m_haveHash = true;
        ++version;

        Mat4 meshInv = meshWorld.Inverse();
        for (std::size_t j = 0; j < joints.size(); ++j) {
            Mat4 ib = j < inverseBind.size() ? inverseBind[j] : Mat4::Identity();
            sk[j] = meshInv * sk[j] * ib;
        }
        palette = sk;
        if (gpu) {
            // The vertex shader deforms; leave the bind pose in the renderer's mesh
            // (restored once when switching over from CPU skinning).
            if (!m_lastGpu || mr->mesh.vertices.size() != bind.vertices.size()) mr->mesh = bind;
            m_lastGpu = true;
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
        for (std::size_t v = 0; v < bind.vertices.size(); ++v) {
            const Vec3 p = bind.vertices[v];
            const Vec3 n = hasN ? bind.normals[v] : Vec3{0, 0, 0};
            Vec3 accP{0, 0, 0}, accN{0, 0, 0};
            float total = 0.0f;
            const std::array<int, 4>&   ji = jointIdx[v];
            const std::array<float, 4>& jw = jointWt[v];
            for (int k = 0; k < 4; ++k) {
                float w = jw[k];
                if (w <= 0.0f) continue;
                int idx = ji[k];
                if (idx < 0 || idx >= (int)sk.size()) continue;
                const Mat4& M = sk[idx];
                accP = accP + M.MultiplyPoint(p) * w;
                if (hasN) accN = accN + M.MultiplyVector(n) * w;
                total += w;
            }
            if (total > 1e-6f) {
                out.vertices[v] = accP * (1.0f / total);          // guard un-normalized weights
                if (hasN) {
                    Vec3 nn = accN;
                    float l = std::sqrt(nn.x*nn.x + nn.y*nn.y + nn.z*nn.z);
                    out.normals[v] = l > 1e-6f ? nn * (1.0f / l) : n;
                }
            } else {   // unweighted: the bind-pose vertex
                out.vertices[v] = p;
                if (hasN) out.normals[v] = n;
            }
        }
    }

private:
    std::uint64_t m_lastHash = 0;    ///< pose hash of the last deform (skip when equal)
    bool          m_haveHash = false;
    bool          m_lastGpu = false;     ///< the last Skin() left the deform to the GPU
};

} // namespace okay
