#pragma once
// ---------------------------------------------------------------------------
// HoldableObject — an object a character can pick up and hold in its hands (the
// character's ProceduralObjectHolding does the holding): where each hand grips
// it, how it is held, how heavy it is and where it sits.
//
// Styles:
//   * Automatic:  picked from the object's tag / name ("pistol", "crate"...),
//                 then its shape and weight (long = rifle / sword / flashlight,
//                 wide = box, very heavy = heavy, small = one hand under it).
//   * Custom:     the hands go to the grip points below and the object sits at
//                 holdOffset / holdRotation in front of the chest.
//   * Pistol:     one hand on the handle, aimed forward (supportHand: the other
//                 hand wraps the first). Rifle: stock at the shoulder, one hand on
//                 the grip, the other under the barrel. Box: both hands on its
//                 sides. Flashlight: one hand, pointing forward. Sword: one hand on
//                 the hilt, blade up and forward (supportHand: two-handed). Cup:
//                 one hand on the handle. Heavy: both hands under
//                 its bottom edges, held low.
//
// Named styles read the object's long side (its biggest extent) as the barrel,
// blade or beam; guns and lights point along local -Z (forward), a sword's blade
// along +Y, a cup's handle along +X. holdRotation corrects a model that's built
// another way.
//
// Grip points are in the object's local space. A grip's rotation orients the
// hand: with no rotation the palm faces down (local -Y) and the hand points
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
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace okay {

class HoldableObject : public Component {
public:
    enum class Style {
        Automatic, Custom, Pistol, Rifle, Box, Flashlight, Sword, Cup, Heavy,
        OneHandRight, OneHandLeft, TwoHanded, CarryFront, OverShoulder
    };
    enum class Preset { OneHandRight, OneHandLeft, TwoHanded, CarryFront, OverShoulder };
    static const char* StyleName(Style s) {
        static const char* n[] = {
            "Automatic", "Custom", "Pistol", "Rifle", "Box", "Flashlight", "Sword", "Cup", "Heavy",
            "One hand right", "One hand left", "Two handed", "Carry in front", "Over the shoulder"
        };
        const int i = (int)s;
        return i >= 0 && i < (int)(sizeof(n) / sizeof(n[0])) ? n[i] : "?";
    }
    static bool IsNamedHold(Style s) {
        return s == Style::OneHandRight || s == Style::OneHandLeft || s == Style::TwoHanded
            || s == Style::CarryFront || s == Style::OverShoulder;
    }
    /// Where one hand touches the object (object-local position, rotation in
    /// degrees). `curl` is kept so older scenes still load; hands have no fingers,
    /// so it is not applied.
    struct Grip {
        bool  enabled = false;
        Vec3  position{0, 0, 0};
        Vec3  rotation{0, 0, 0};
        float curl = 0.6f;
    };

    Style style = Style::Automatic;
    bool  supportHand = false;                ///< Pistol / Sword: the other hand helps (two-hand stance, two-handed sword)
    bool  twoHanded = false;                  ///< force both hands even when the shape would be one hand
    Grip  leftGrip, rightGrip;
    Vec3  holdOffset{0.0f, -0.25f, 0.35f};   ///< Custom: object origin from the chest (right, up, forward), metres
    Vec3  holdRotation{0, 0, 0};              ///< Custom: object rotation relative to the body; other styles: a model fix (degrees)
    float inHandScale = 1.0f;                 ///< scale while it is in the hands (1 = the object's own scale)
    Vec3  fpOffset{0, 0, 0};                  ///< first-person copy, in the hand's space
    Vec3  fpRotation{0, 0, 0};                ///< degrees, on the first-person copy
    float fpScale = 1.0f;
    Vec3  tpOffset{0, 0, 0};                  ///< third person, body space (side, up, forward)
    Vec3  tpRotation{0, 0, 0};                ///< degrees, after the hold's own rotation
    float weight = 0.0f;                      ///< kg (0 = the Rigidbody3D's mass, else 1)
    std::string socket;                       ///< hold at this object inside the character (a holster, a back slot)

    /// Grip a unit cube (local -0.5..0.5) the way `p` says, and remember the style.
    void ApplyPreset(Preset p) {
        leftGrip = {};
        rightGrip = {};
        fpOffset = {}; fpRotation = {}; fpScale = 1.0f;
        tpOffset = {}; tpRotation = {}; inHandScale = 1.0f;
        holdRotation = {};
        switch (p) {
        case Preset::OneHandLeft:
            style = Style::OneHandLeft;
            twoHanded = false;
            leftGrip.enabled = true;
            leftGrip.position = {0.0f, -0.5f, 0.0f};
            break;
        case Preset::TwoHanded:
            style = Style::TwoHanded;
            twoHanded = true;
            leftGrip.enabled = true;  leftGrip.position = {-0.5f, -0.05f, 0.15f};
            rightGrip.enabled = true; rightGrip.position = {0.5f, -0.05f, 0.15f};
            break;
        case Preset::CarryFront:
            style = Style::CarryFront;
            twoHanded = true;
            leftGrip.enabled = true;  leftGrip.position = {-0.5f, 0.0f, 0.1f};
            rightGrip.enabled = true; rightGrip.position = {0.5f, 0.0f, 0.1f};
            tpOffset = {0.0f, -0.05f, 0.02f};
            break;
        case Preset::OverShoulder:
            style = Style::OverShoulder;
            twoHanded = false;
            rightGrip.enabled = true;
            rightGrip.position = {0.15f, -0.5f, 0.2f};
            tpOffset = {0.04f, 0.02f, -0.02f};
            break;
        case Preset::OneHandRight:
        default:
            style = Style::OneHandRight;
            twoHanded = false;
            rightGrip.enabled = true;
            rightGrip.position = {0.0f, -0.5f, 0.0f};
            break;
        }
    }

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

    /// The style this object is held in: `style`, or for Automatic the one its
    /// tag / name names, else what its size and weight suggest. Automatic comes
    /// back for a small, light object (held in one hand, from underneath).
    Style ResolvedStyle() const {
        if (style != Style::Automatic) return style;
        std::string n;
        if (gameObject) for (char c : gameObject->tag + " " + gameObject->name) n += (char)std::tolower((unsigned char)c);
        auto has = [&](std::initializer_list<const char*> ks) {
            for (const char* k : ks) if (n.find(k) != std::string::npos) return true;
            return false;
        };
        if (has({"rifle", "shotgun", "smg", "carbine", "musket"})) return Style::Rifle;
        if (has({"pistol", "handgun", "revolver", "gun"})) return Style::Pistol;
        if (has({"flashlight", "torch"})) return Style::Flashlight;
        if (has({"sword", "blade", "katana", "saber", "sabre", "machete"})) return Style::Sword;
        if (has({"cup", "mug", "coffee"})) return Style::Cup;
        if (has({"heavy", "boulder", "anvil", "barrel"})) return Style::Heavy;
        if (has({"box", "crate", "parcel", "package"})) return Style::Box;
        Vec3 c, half;
        LocalBounds(c, half);
        if (gameObject && gameObject->transform) half = half * gameObject->transform->LossyScale();
        float e[3] = {std::fabs(half.x) * 2.0f, std::fabs(half.y) * 2.0f, std::fabs(half.z) * 2.0f};
        std::sort(e, e + 3);
        const float mass = Mass();
        if (e[2] > 2.5f * e[1] && e[2] > 0.15f) return e[2] > 0.5f ? (mass > 2.5f ? Style::Rifle : Style::Sword) : Style::Flashlight;
        if (mass > 15.0f) return Style::Heavy;
        if (e[2] > 0.3f || mass > 5.0f) return Style::Box;
        return Style::Automatic;
    }

    /// Both hands on this object: the flag, both grip points, a rifle / box /
    /// heavy style, or a pistol / sword with the support hand on.
    bool WantsTwoHands() const {
        if (twoHanded) return true;
        if (leftGrip.enabled && rightGrip.enabled) return true;
        const Style s = ResolvedStyle();
        if (s == Style::Rifle || s == Style::Box || s == Style::Heavy) return true;
        if (s == Style::TwoHanded || s == Style::CarryFront) return true;
        if (supportHand && (s == Style::Pistol || s == Style::Sword)) return true;
        return false;
    }

    void OnObjectsDestroyed(const std::unordered_set<const GameObject*>& dying) override {
        if (dying.count(m_holder)) m_holder = nullptr;
    }

    /// Put physics back and clear the holder. Safe to call more than once.
    void ReleaseHold() {
        if (m_froze) {
            if (m_rb) m_rb->bodyType = m_savedType;
            for (auto& c : m_triggers) if (c.first) c.first->isTrigger = c.second;
            m_froze = false;
            m_rb = nullptr;
            m_triggers.clear();
        }
        if (gameObject) gameObject->ownerCameraSkip = nullptr;
        if (gameObject && gameObject->transform && gameObject->transform->Parent()) {
            Transform* parent = gameObject->transform->Parent();
            GameObject* pg = parent->gameObject;
            if (pg && (pg == m_holder || (m_holder && pg->IsSelfOrDescendantOf(m_holder)))) {
                const Vec3 p = gameObject->transform->Position();
                const Quat r = gameObject->transform->Rotation();
                gameObject->transform->SetParent(nullptr, false);
                gameObject->transform->SetPosition(p);
                gameObject->transform->SetRotation(r);
            }
        }
        m_holder = nullptr;
    }

private:
    friend class ProceduralObjectHolding;
    void FreezeForHold(GameObject* holder) {
        ReleaseHold();
        m_holder = holder;
        if (!gameObject) return;
        if (auto* rb = gameObject->GetComponent<Rigidbody3D>()) {
            m_rb = rb;
            m_savedType = rb->bodyType;
            rb->bodyType = Rigidbody3D::BodyType::Kinematic;
            rb->velocity = Vec3::Zero;
            rb->angularVelocity = Vec3::Zero;
        }
        if (Scene* sc = gameObject->scene())
            for (const auto& up : sc->Objects())
                if (up && up->IsSelfOrDescendantOf(gameObject))
                    for (auto* c : up->GetComponents<Collider3D>()) {
                        m_triggers.push_back({c, c->isTrigger});
                        c->isTrigger = true;
                    }
        m_froze = true;
    }

    GameObject* m_holder = nullptr;
    bool m_froze = false;
    Rigidbody3D* m_rb = nullptr;
    Rigidbody3D::BodyType m_savedType = Rigidbody3D::BodyType::Dynamic;
    std::vector<std::pair<Collider3D*, bool>> m_triggers;
};

} // namespace okay
