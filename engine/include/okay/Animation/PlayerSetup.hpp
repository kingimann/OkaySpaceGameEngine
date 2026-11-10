#pragma once
// ---------------------------------------------------------------------------
// PlayerSetup — one repeatable step that puts animation and the standard IK
// on a player that already has a character model. It does not import a file,
// does not replace a state graph, and does not change IK weights or chains
// the user already set.
// ---------------------------------------------------------------------------
#include "okay/Scene/GameObject.hpp"
#include <string>

namespace okay {

class Scene;

/// What the controller panel shows before the user presses the button.
/// Read-only: it does not add components or rewrite slots.
struct PlayerAnimStatus {
    bool hasModel = false;
    std::string model;
    int bones = 0;          ///< humanoid bones found (of humanoid::BoneCount)
    bool legs = false;      ///< both legs are a thigh-shin-foot chain
    bool head = false;
    bool footIk = false;
    bool lookAt = false;
    bool animator = false;  ///< a base-layer Animator state machine on the player
};

PlayerAnimStatus InspectPlayerAnimation(Scene& scene, GameObject* player);

/// Add Foot IK, Look-At and the default Animator only where they are missing,
/// and run the existing animation fix (empty slots, stale clip names, built-in
/// fallback when the rig matches). A second call adds nothing and leaves
/// weights, chains and a custom graph alone.
/// The returned line is for the console.
std::string WirePlayerAnimation(Scene& scene, GameObject* player);

} // namespace okay
