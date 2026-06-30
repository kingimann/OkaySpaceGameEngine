#pragma once
// ---------------------------------------------------------------------------
// Shared helpers for wiring the IK components onto a controller's Character, so
// every controller can opt into foot IK with one flag instead of duplicating the
// bone lookup. Used by the player + NPC controllers' `footIK` setting.
// ---------------------------------------------------------------------------
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/FootIK.hpp"

namespace okay {

/// Find a Character on `go` or any descendant (controllers may keep the rig on a
/// child of the movement object).
inline Character* FindCharacterIn(GameObject* go) {
    if (!go) return nullptr;
    if (auto* ch = go->GetComponent<Character>()) return ch;
    if (go->transform)
        for (Transform* c : go->transform->Children())
            if (c && c->gameObject)
                if (auto* ch = FindCharacterIn(c->gameObject)) return ch;
    return nullptr;
}

/// Attach (once) a fully-wired FootIK to the controller's Character: part rig built,
/// both legs hooked to their bones, pelvis adjustment + ground-slope foot tilt on.
/// Returns the FootIK (existing or new), or null if there's no Character to wire.
inline FootIK* AttachCharacterFootIK(GameObject* owner) {
    Character* pc = FindCharacterIn(owner);
    if (!pc || !pc->gameObject) return nullptr;
    GameObject* host = pc->gameObject;
    if (auto* existing = host->GetComponent<FootIK>()) return existing;
    pc->separateParts = true;
    if (!pc->PartsBuilt()) pc->BuildParts();
    auto T = [&](int b) -> Transform* { GameObject* g = pc->Part(b); return g ? g->transform : nullptr; };
    auto* fik = host->AddComponent<FootIK>();
    fik->leftHip  = T(9);  fik->leftKnee  = T(10); fik->leftFoot  = T(11);
    fik->rightHip = T(12); fik->rightKnee = T(13); fik->rightFoot = T(14);
    fik->useRaycast = true; fik->weight = 1.0f;
    fik->pelvis = T(0); fik->adjustPelvis = true; fik->plantDown = true;
    fik->alignToGround = true; fik->maxKneeBend = 178.0f;
    return fik;
}

} // namespace okay
