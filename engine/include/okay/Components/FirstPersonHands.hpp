#pragma once
// ---------------------------------------------------------------------------
// FirstPersonHands — how the owner's hands look, and how everyone else sees
// the same hold. Put it on the player. With no component, first person draws
// nothing until the player is holding an item (a run still hides the hands).
//
// Presets (ApplyPreset / the Preset menu in the Inspector and the Fp Hands window):
// Normal (the default for a new component: the arm is always in view, with a
// forearm in the shirt colour that leaves the bottom of the screen, and a left
// click swings it), Both arms, Right hand only, Minimal (the hand brick alone)
// and Old look (the behaviour before presets, kept for comparison). The numbers
// stay editable; Preset is only the name of the last one applied.
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
    enum class Preset { Normal = 0, BothArms = 1, RightHandOnly = 2, Minimal = 3, OldLook = 4, Custom = 5 };
    static constexpr int kPresetCount = 5;   ///< the ones you can pick (Custom is just "edited by hand")
    static const char* PresetName(Preset p) {
        switch (p) {
            case Preset::Normal:        return "Normal";
            case Preset::BothArms:      return "Both arms";
            case Preset::RightHandOnly: return "Right hand only";
            case Preset::Minimal:       return "Minimal (hand only)";
            case Preset::OldLook:       return "Old look";
            default:                    return "Custom";
        }
    }
    static const char* PresetHelp(Preset p) {
        switch (p) {
            case Preset::Normal:
                return "The arm is always in view (not only when you hold something): a hand and a forearm in your shirt colour that leaves the bottom of the screen. Left click swings it. A two-handed item uses both.";
            case Preset::BothArms:
                return "Normal, but asks for both hands on a held item. Today an item whose own grip is one-handed still shows one hand, and with empty hands one arm shows, so this looks the same as Normal for those (a two-arm stance is not built yet).";
            case Preset::RightHandOnly:
                return "Normal, but only the right arm ever shows, even for a two-handed item.";
            case Preset::Minimal:
                return "Just the hand block, no forearm. Small and out of the way. Left click swings it.";
            case Preset::OldLook:
                return "How it looked before presets: a floating hand block that only shows while you hold something, no forearm, no click swing. For comparing.";
            default:
                return "You changed the numbers by hand.";
        }
    }

    /// One hand in the owner's view. Right is the lower-right hand.
    /// The brick stays one block. There are no fingers.
    struct HandSide {
        bool  enabled = true;
        Vec3  position{0, 0, 0};          ///< camera-space nudge, metres
        Vec3  rotation{0, 0, 0};          ///< extra euler, degrees
        float uniformScale = 0.75f;       ///< Old look: 1.0
        Vec3  scale{1, 1, 1};
        Vec3  size{1, 1, 1};              ///< width, height, depth of the brick, on top of scale
        bool  useColour = false;          ///< false = the character's skin
        Color colour = Color::FromBytes(220, 176, 150);
        bool  sleeve = false;
        float sleeveLength = 0.28f;
        Color sleeveColour = Color::FromBytes(60, 95, 75);
        bool  forearm = true;             ///< the arm under the hand. The Old look preset turns it off.
        float forearmLength = 1.1f;       ///< long enough to leave the bottom of the screen
        float forearmThickness = 0.105f;
        bool  matchShirt = true;          ///< sleeve and forearm take the character's shirt colour (off = the colours below)
        Color forearmColour = Color::FromBytes(60, 95, 75);
        Vec3  wristOffset{0, 0, 0};       ///< extra camera-space nudge at the wrist
        Vec3  wristRotation{0, 0, 0};     ///< extra euler at the wrist, degrees
        float elbowBend = 0.0f;           ///< degrees the forearm folds
        float reach = 0.0f;               ///< metres further out from the lens
    };

    /// How one arm reads on the full body (what other players see).
    struct OthersArm {
        bool  forearm = true;             ///< the body's own forearm. On, so the arm still connects.
        float forearmLength = 0.28f;
        float forearmThickness = 0.05f;
        Color forearmColour = Color::FromBytes(60, 95, 75);
        Vec3  handSize{1, 1, 1};          ///< width, height, depth multiplier on that hand while holding
        Vec3  wristOffset{0, 0, 0};       ///< metres, body space (side, up, forward), added at the grip
        Vec3  wristRotation{0, 0, 0};     ///< degrees added to the hand's grip
        float elbowBend = 0.0f;           ///< degrees the elbow hint folds out (0 = the built-in hint)
        float reach = 0.0f;               ///< metres the grip is pushed forward
    };

    /// The Normal look sits the hands a little further in from the screen edge (the right hand one
    /// way, the left the other). Old look puts both back to zero.
    FirstPersonHands() { right.position.x = -0.05f; left.position.x = 0.05f; }

    // ---- Player view (first person) ----
    Preset preset = Preset::Normal;       ///< the last preset applied (a label only)
    bool  swingOnClick = true;            ///< the attack button swings the arm (the real arm, so others see it)
    int   attackButton = 0;               ///< mouse button that swings (0 = left)
    /// Item actions in first person (off for a component added by hand, on in the starter players):
    /// with a pistol or rifle in hand, the attack button plays the "fire" kick, the aim button (right mouse) aims
    /// down the sights and the reload key plays "reload". Other items keep the click swing. Not part of a preset.
    bool  itemActions = false;
    int   aimButton = 1;                  ///< mouse button that aims (1 = right)
    char  reloadKey = 'r';                ///< 0 = no reload key
    bool  showWhenEmpty = true;           ///< arms in view with nothing held (Old look: only while holding)
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
    OthersArm othersRight, othersLeft;

    /// Left click (attackButton) swings the arm in first person. Defined in Character.cpp.
    void Update(float dt) override;

    /// Set every player-view number to one preset. Others-view numbers are not touched.
    void ApplyPreset(Preset p) {
        const OthersArm keepR = othersRight, keepL = othersLeft;
        const Vec3 oi = othersItemOffset, orot = othersItemRotation;
        const float olift = othersArmLift, ofwd = othersArmForward;
        const bool keepEnabled = enabled;
        ResetPlayerView();
        enabled = keepEnabled;
        othersRight = keepR; othersLeft = keepL;
        othersItemOffset = oi; othersItemRotation = orot; othersArmLift = olift; othersArmForward = ofwd;
        preset = p;
        switch (p) {
            case Preset::Normal: break;
            case Preset::BothArms:
                holdMode = HoldMode::Two;
                break;
            case Preset::RightHandOnly:
                left.enabled = false;
                break;
            case Preset::Minimal:
                right.forearm = false; left.forearm = false;
                right.uniformScale = 0.6f; left.uniformScale = 0.6f;
                left.enabled = false;
                break;
            case Preset::OldLook:
                showWhenEmpty = false;
                swingOnClick = false;
                right.forearm = false; left.forearm = false;
                right.forearmLength = left.forearmLength = 0.42f;
                right.forearmThickness = left.forearmThickness = 0.07f;
                right.matchShirt = left.matchShirt = false;
                right.uniformScale = left.uniformScale = 1.0f;
                right.position = left.position = Vec3{0, 0, 0};
                break;
            default: break;
        }
    }
    void UseNormalArms() { ApplyPreset(Preset::Normal); }
    void UseOldLook()    { ApplyPreset(Preset::OldLook); }

    void CopySettingsFrom(const FirstPersonHands& o) {
        preset = o.preset;
        swingOnClick = o.swingOnClick;
        attackButton = o.attackButton;
        itemActions = o.itemActions;
        aimButton = o.aimButton;
        reloadKey = o.reloadKey;
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
        othersRight = o.othersRight;
        othersLeft = o.othersLeft;
    }

    void ResetPlayerView() {
        FirstPersonHands d;
        preset = d.preset;
        swingOnClick = d.swingOnClick;
        attackButton = d.attackButton;
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
        othersRight = d.othersRight;
        othersLeft = d.othersLeft;
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
