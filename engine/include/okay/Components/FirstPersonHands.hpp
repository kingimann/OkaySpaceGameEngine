#pragma once
// ---------------------------------------------------------------------------
// FirstPersonHands — how the owner's hands look, and how everyone else sees
// the same hold. Put it on the player. With no component, first person draws
// nothing until the player is holding an item (a run still hides the hands).
//
// Player view is the first-person copy. Others view nudges the real held
// object (the arms follow it) and the item's own grip points pose the body.
// Play mode reloads the scene on Stop; CaptureApplied / RestoreApplied keep
// an Apply click.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Render/Color.hpp"
#include <string>
#include <utility>
#include <vector>

namespace okay {

class FirstPersonHands : public Behaviour {
public:
    enum class HoldMode { Right = 0, Left = 1, Two = 2 };

    /// One hand in the owner's view. Right is the lower-right hand.
    struct HandSide {
        bool  enabled = true;
        Vec3  position{0, 0, 0};          ///< camera-space nudge, metres
        Vec3  rotation{0, 0, 0};          ///< extra euler, degrees
        float uniformScale = 1.0f;
        Vec3  scale{1, 1, 1};
        Vec3  size{1, 1, 1};              ///< brick size, on top of scale
        bool  useColour = false;          ///< false = the character's skin
        Color colour = Color::FromBytes(220, 176, 150);
        bool  sleeve = false;
        float sleeveLength = 0.28f;
        Color sleeveColour = Color::FromBytes(60, 95, 75);
    };

    // ---- Player view (first person) ----
    bool  showWhenEmpty = false;
    bool  showWhileRunning = false;
    float bobAmount = 1.0f;
    float swayAmount = 1.0f;
    float punchAmount = 1.0f;
    float handFov = 0.0f;                 ///< 0 = the camera's field of view
    Vec3  itemOffset{0, 0, 0};
    Vec3  itemRotation{0, 0, 0};
    float itemScale = 1.0f;
    HoldMode holdMode = HoldMode::Right;
    HandSide right, left;

    // ---- Others view (third person) ----
    /// Added to the hold, in body space (side, up, forward). The arms follow.
    Vec3  othersItemOffset{0, 0, 0};
    Vec3  othersItemRotation{0, 0, 0};    ///< degrees, after the hold's own rotation
    float othersArmLift = 0.0f;           ///< metres up
    float othersArmForward = 0.0f;        ///< metres forward

    void CopySettingsFrom(const FirstPersonHands& o) {
        showWhenEmpty = o.showWhenEmpty;
        showWhileRunning = o.showWhileRunning;
        bobAmount = o.bobAmount;
        swayAmount = o.swayAmount;
        punchAmount = o.punchAmount;
        handFov = o.handFov;
        itemOffset = o.itemOffset;
        itemRotation = o.itemRotation;
        itemScale = o.itemScale;
        holdMode = o.holdMode;
        right = o.right;
        left = o.left;
        othersItemOffset = o.othersItemOffset;
        othersItemRotation = o.othersItemRotation;
        othersArmLift = o.othersArmLift;
        othersArmForward = o.othersArmForward;
    }

    void ResetPlayerView() {
        FirstPersonHands d;
        showWhenEmpty = d.showWhenEmpty;
        showWhileRunning = d.showWhileRunning;
        bobAmount = d.bobAmount;
        swayAmount = d.swayAmount;
        punchAmount = d.punchAmount;
        handFov = d.handFov;
        itemOffset = d.itemOffset;
        itemRotation = d.itemRotation;
        itemScale = d.itemScale;
        holdMode = d.holdMode;
        right = d.right;
        left = d.left;
    }
    void ResetOthersView() {
        FirstPersonHands d;
        othersItemOffset = d.othersItemOffset;
        othersItemRotation = d.othersItemRotation;
        othersArmLift = d.othersArmLift;
        othersArmForward = d.othersArmForward;
    }

    /// On the player, or on a camera parented to the player.
    static FirstPersonHands* FindOn(const GameObject* player) {
        if (!player) return nullptr;
        if (auto* h = player->GetComponent<FirstPersonHands>()) return h;
        if (!player->transform) return nullptr;
        for (Transform* c : player->transform->Children())
            if (c && c->gameObject)
                if (auto* h = c->gameObject->GetComponent<FirstPersonHands>()) return h;
        return nullptr;
    }

    /// Remember every FirstPersonHands in the live scene. Stop copies them back
    /// onto the restored objects of the same name.
    static void CaptureApplied(Scene& sc);
    /// Returns true when a captured Apply was written onto the restored scene.
    static bool RestoreApplied(Scene& sc);
};

struct FpHandsSnap {
    std::string name;
    FirstPersonHands fields;
};
inline std::vector<FpHandsSnap>& FpHandsAppliedStore() {
    static std::vector<FpHandsSnap> s;
    return s;
}
inline void FirstPersonHands::CaptureApplied(Scene& sc) {
    auto& applied = FpHandsAppliedStore();
    applied.clear();
    for (auto* h : sc.FindObjectsOfType<FirstPersonHands>()) {
        if (!h || !h->gameObject) continue;
        FpHandsSnap sn;
        sn.name = h->gameObject->name;
        sn.fields.CopySettingsFrom(*h);
        applied.push_back(std::move(sn));
    }
}
inline bool FirstPersonHands::RestoreApplied(Scene& sc) {
    auto& applied = FpHandsAppliedStore();
    if (applied.empty()) return false;
    for (const FpHandsSnap& sn : applied) {
        GameObject* g = sc.Find(sn.name);
        if (!g) continue;
        auto* h = g->GetComponent<FirstPersonHands>();
        if (!h) h = g->AddComponent<FirstPersonHands>();
        h->CopySettingsFrom(sn.fields);
    }
    applied.clear();
    return true;
}

} // namespace okay
