#pragma once
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Physics/Collider2D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Math/Mathf.hpp"

namespace okay {

/// Size a GameObject's colliders to match its renderer, the way Unity's
/// "Add Collider" auto-fits to the sprite/mesh bounds. Collider size is in local
/// units (the Transform's scale is applied on top by the collider), so it's set
/// straight from the un-scaled sprite size / mesh bounds and stays correct as the
/// object is scaled. The 2D family fits a SpriteRenderer; the 3D family fits a
/// MeshRenderer (offsetting to the mesh's local center when it isn't centered).
///
/// `onlyAuto` restricts the refit to colliders flagged autoFit (the per-frame
/// path); pass false to force-fit everything (the "Fit to Object" button / add).
inline void FitColliders(GameObject* go, bool onlyAuto = false) {
    if (!go) return;

    // ---- 2D: fit to the SpriteRenderer ----
    if (auto* sr = go->GetComponent<SpriteRenderer>()) {
        Vec2 sz = sr->size;
        float maxXY = Mathf::Max(sz.x, sz.y);
        for (auto* c : go->GetComponents<Collider2D>()) {
            if (onlyAuto && !c->autoFit) continue;
            c->offset = Vec2::Zero;                       // sprites are centered
            if (auto* b = dynamic_cast<BoxCollider2D*>(c))      b->size = sz;
            else if (auto* ci = dynamic_cast<CircleCollider2D*>(c)) ci->radius = maxXY * 0.5f;
            else if (auto* cap = dynamic_cast<CapsuleCollider2D*>(c)) cap->size = sz;
        }
    }

    // ---- 3D: fit to the MeshRenderer's local bounds ----
    if (auto* mr = go->GetComponent<MeshRenderer>()) {
        Vec3 lo, hi; mr->mesh.Bounds(lo, hi);
        // Never collapse a dimension to exactly 0 — a flat plane / a face extruded flat
        // would otherwise get a zero-thickness collider that nothing can land on.
        Vec3 size{Mathf::Max(hi.x - lo.x, 0.01f),
                  Mathf::Max(hi.y - lo.y, 0.01f),
                  Mathf::Max(hi.z - lo.z, 0.01f)};
        Vec3 center{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
        // A collider's `offset` is applied in WORLD space (WorldCenter = position +
        // offset), so convert the mesh's local centre through the Transform. Using the
        // raw local centre put the collider in the wrong place once the mesh was no
        // longer centred (e.g. after an extrude) or whenever the object was scaled /
        // rotated — which reads as "the collider stopped working".
        Vec3 offs = center;
        if (go->transform)
            offs = go->transform->LocalToWorldMatrix().MultiplyPoint(center) - go->transform->Position();
        float maxXZ = Mathf::Max(size.x, size.z);
        float maxXYZ = Mathf::Max(size.x, Mathf::Max(size.y, size.z));
        for (auto* c : go->GetComponents<Collider3D>()) {
            if (onlyAuto && !c->autoFit) continue;
            c->offset = offs;
            if (auto* b = dynamic_cast<BoxCollider3D*>(c)) b->size = size;
            else if (auto* sp = dynamic_cast<SphereCollider3D*>(c)) sp->radius = maxXYZ * 0.5f;
            else if (auto* cap = dynamic_cast<CapsuleCollider3D*>(c)) {
                cap->radius = maxXZ * 0.5f;
                cap->height = size.y;
            }
        }
    }
}

} // namespace okay
