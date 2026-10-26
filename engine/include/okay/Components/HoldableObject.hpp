#pragma once
// ---------------------------------------------------------------------------
// HoldableObject — an object a character can pick up and hold in its hands (the
// character's ProceduralObjectHolding does the holding): where each hand grips
// it, how it is held, how heavy it is and where it sits.
//
//   * Automatic: the holder works out a carry from the object's size and weight
//     (small = one hand under it, large or heavy = both hands on its sides).
//   * Custom:    the hands go to the grip points below and the object sits at
//     holdOffset / holdRotation in front of the chest.
//
// Grip points are in the object's local space. A grip's rotation orients the
// hand: with no rotation the palm faces down (local -Y) and the fingers point
// along local -Z (forward), like a hand laid flat on top of the object. A
// one-handed object authored for one hand is mirrored onto the other hand when
// the holder's primary hand is the other one.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Math/Vec3.hpp"
#include <algorithm>
#include <string>
#include <unordered_set>

namespace okay {

class HoldableObject : public Component {
public:
    enum class Style { Automatic, Custom };
    /// Where one hand touches the object (object-local position, rotation in degrees).
    struct Grip {
        bool enabled = false;
        Vec3 position{0, 0, 0};
        Vec3 rotation{0, 0, 0};
    };

    Style style = Style::Automatic;
    Grip  leftGrip, rightGrip;
    Vec3  holdOffset{0.0f, -0.25f, 0.35f};   ///< Custom: object origin from the chest (right, up, forward), metres
    Vec3  holdRotation{0, 0, 0};              ///< Custom: object rotation relative to the body (degrees)
    float weight = 0.0f;                      ///< kg (0 = the Rigidbody3D's mass, else 1)
    std::string socket;                       ///< hold at this object inside the character (a holster, a back slot)

    /// The character holding this object right now (null when it's free).
    GameObject* Holder() const { return m_holder; }

    /// The weight used for carrying: `weight`, else the Rigidbody3D's mass, else 1 kg.
    float Mass() const {
        if (weight > 0.0f) return weight;
        if (gameObject)
            if (auto* rb = gameObject->GetComponent<Rigidbody3D>()) return rb->mass > 0.0f ? rb->mass : 1.0f;
        return 1.0f;
    }

    /// Bounds of the meshes under this object in its local space (unscaled): the
    /// centre and half extents. Falls back to a box collider, then a 20 cm cube.
    void LocalBounds(Vec3& centre, Vec3& half) const {
        Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
        bool any = false;
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        if (sc && gameObject->transform) {
            const Mat4 toLocal = gameObject->transform->LocalToWorldMatrix().Inverse();
            for (const auto& up : sc->Objects()) {
                GameObject* g = up.get();
                if (!g || !g->transform || !g->IsSelfOrDescendantOf(gameObject)) continue;
                auto* mr = g->GetComponent<MeshRenderer>();
                if (!mr || mr->mesh.vertices.empty()) continue;
                Vec3 a, b; mr->mesh.Bounds(a, b);
                const Mat4 m = toLocal * g->transform->LocalToWorldMatrix();
                for (int i = 0; i < 8; ++i) {
                    Vec3 p = m.MultiplyPoint({(i & 1) ? b.x : a.x, (i & 2) ? b.y : a.y, (i & 4) ? b.z : a.z});
                    lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
                    hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
                }
                any = true;
            }
        }
        if (any) { centre = (lo + hi) * 0.5f; half = (hi - lo) * 0.5f; return; }
        if (auto* bc = gameObject ? gameObject->GetComponent<BoxCollider3D>() : nullptr) {
            centre = bc->offset; half = bc->size * 0.5f; return;
        }
        centre = {0, 0, 0}; half = {0.1f, 0.1f, 0.1f};
    }

    void OnObjectsDestroyed(const std::unordered_set<const GameObject*>& dying) override {
        if (dying.count(m_holder)) m_holder = nullptr;
    }

private:
    friend class ProceduralObjectHolding;
    GameObject* m_holder = nullptr;
};

} // namespace okay
