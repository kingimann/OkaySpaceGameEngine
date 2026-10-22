#pragma once
// ---------------------------------------------------------------------------
// BlendShapes — morph targets (Unity's blend shapes): named sets of per-vertex
// offsets mixed into a mesh by weights 0..1 — facial expressions, lip-sync
// visemes, muscle bulges, corrective shapes. Imported from glTF ("targets") and
// FBX (blend shape channels); weights can be animated by Model Animator clips
// ("weights.<n>" tracks on the mesh's node), set from the inspector or scripts.
//
// The morph is applied to the sibling SkinnedMesh's bind pose (before skinning),
// so a morphed face still follows the skeleton and the GPU skinning path picks
// it up. Shapes are stored sparse: only the vertices a shape moves.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/SkinnedMesh.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Render/Mesh.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace okay {

class BlendShapes : public Behaviour {
public:
    struct Shape {
        std::string name;
        float weight = 0.0f;
        std::vector<int>  index;    ///< vertices this shape moves
        std::vector<Vec3> dPos;     ///< position offset per entry in `index`
        std::vector<Vec3> dNrm;     ///< normal offset per entry (may be empty)
    };
    std::vector<Shape> shapes;
    /// The unmorphed mesh (positions / normals the offsets apply to). Filled from
    /// the SkinnedMesh's bind (or the renderer's mesh) the first time it's needed.
    Mesh base;

    // After animation and IK pose the bones, before the skin is computed (200).
    int ExecutionOrder() const override { return 150; }
    void LateUpdate(float) override { Apply(); }

    int Find(const std::string& name) const {
        for (int i = 0; i < (int)shapes.size(); ++i) if (shapes[i].name == name) return i;
        return -1;
    }
    void SetWeight(int i, float w) { if (i >= 0 && i < (int)shapes.size()) shapes[i].weight = w; }
    bool SetWeight(const std::string& name, float w) { int i = Find(name); SetWeight(i, w); return i >= 0; }
    float Weight(const std::string& name) const { int i = Find(name); return i >= 0 ? shapes[i].weight : 0.0f; }

    /// Add a shape from a full per-vertex offset array (zeros are dropped).
    void AddShape(const std::string& name, const std::vector<Vec3>& dp, const std::vector<Vec3>& dn = {}, float weight = 0.0f) {
        Shape s; s.name = name; s.weight = weight;
        for (std::size_t v = 0; v < dp.size(); ++v) {
            const Vec3& d = dp[v];
            Vec3 n = v < dn.size() ? dn[v] : Vec3{0, 0, 0};
            if (d.x * d.x + d.y * d.y + d.z * d.z + n.x * n.x + n.y * n.y + n.z * n.z < 1e-14f) continue;
            s.index.push_back((int)v); s.dPos.push_back(d);
            if (!dn.empty()) s.dNrm.push_back(n);
        }
        shapes.push_back(std::move(s));
    }

    /// Mix the shapes into the mesh when any weight changed since last time.
    void Apply(bool force = false) {
        if (!gameObject) return;
        SkinnedMesh* sm = gameObject->GetComponent<SkinnedMesh>();
        MeshRenderer* mr = gameObject->GetComponent<MeshRenderer>();
        Mesh* target = sm ? &sm->bind : (mr ? &mr->mesh : nullptr);
        if (!target) return;
        if (base.vertices.empty() || base.vertices.size() != target->vertices.size()) { base = *target; force = true; }
        std::uint64_t h = 1469598103934665603ull;
        for (const Shape& s : shapes) { std::uint32_t b; std::memcpy(&b, &s.weight, 4); h = (h ^ b) * 1099511628211ull; }
        if (!force && h == m_lastHash) return;
        m_lastHash = h;
        const bool hasN = base.normals.size() == base.vertices.size() && target->normals.size() == target->vertices.size();
        target->vertices = base.vertices;
        if (hasN) target->normals = base.normals;
        for (const Shape& s : shapes) {
            const float w = s.weight;
            if (w == 0.0f) continue;
            const bool sn = hasN && s.dNrm.size() == s.index.size();
            for (std::size_t k = 0; k < s.index.size(); ++k) {
                int v = s.index[k];
                if (v < 0 || v >= (int)target->vertices.size()) continue;
                target->vertices[v] = target->vertices[v] + s.dPos[k] * w;
                if (sn) target->normals[v] = target->normals[v] + s.dNrm[k] * w;
            }
        }
        if (hasN)
            for (auto& n : target->normals) { float l = n.Magnitude(); if (l > 1e-8f) n = n * (1.0f / l); }
        if (sm) sm->BindChanged();
        else if (mr) mr->deformed = true;
    }

private:
    std::uint64_t m_lastHash = 0;
};

} // namespace okay
