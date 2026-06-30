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
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Math/Mat4.hpp"
#include <array>
#include <vector>

namespace okay {

class SkinnedMesh : public Behaviour {
public:
    Mesh bind;                                   ///< bind-pose mesh (positions/normals/uvs/triangles)
    std::vector<std::array<int, 4>>   jointIdx;  ///< per-vertex joint indices (into `joints`)
    std::vector<std::array<float, 4>> jointWt;   ///< per-vertex blend weights (sum ~1)
    std::vector<Transform*> joints;              ///< bone transforms (runtime; not serialized)
    std::vector<Mat4>       inverseBind;          ///< per-joint inverse bind matrix

    void Start() override  { Skin(); }
    void Update(float) override { Skin(); }

    /// True once it has joints + a bind mesh to deform.
    bool Ready() const { return !joints.empty() && !bind.vertices.empty(); }

    /// Recompute the deformed mesh from the current joint poses.
    void Skin() {
        if (!gameObject || !Ready()) return;
        auto* mr = gameObject->GetComponent<MeshRenderer>();
        if (!mr) return;

        // skinMatrixⱼ = meshNode⁻¹ · jointWorldⱼ · inverseBindⱼ
        Mat4 meshInv = transform ? transform->LocalToWorldMatrix().Inverse() : Mat4::Identity();
        std::vector<Mat4> sk(joints.size());
        for (std::size_t j = 0; j < joints.size(); ++j) {
            Mat4 jw = joints[j] ? joints[j]->LocalToWorldMatrix() : Mat4::Identity();
            Mat4 ib = j < inverseBind.size() ? inverseBind[j] : Mat4::Identity();
            sk[j] = meshInv * jw * ib;
        }

        Mesh out = bind;   // keeps triangles + uvs; positions/normals get overwritten
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
            }   // total==0 -> leave the bind-pose vertex (unweighted)
        }
        mr->mesh = std::move(out);
    }
};

} // namespace okay
