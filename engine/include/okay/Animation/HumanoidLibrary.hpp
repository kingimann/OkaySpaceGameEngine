#pragma once
// ---------------------------------------------------------------------------
// HumanoidLibrary — the built-in humanoid's default clips and Animator graph.
// The clips are baked from HumanoidMotion: idle; walk, run and crouch-walk at
// several speeds in eight directions (the gait's hip bob in each clip's root
// track); crouch and the prone crawl; jump, fall, land and hang; the gestures
// and emotions. Every gait cycle loops seamlessly. Clip names are stable: the
// default graph and saved scenes refer to them. Every Character can play them
// (they aren't copied into the body or the scene).
// ---------------------------------------------------------------------------
#include "okay/Components/AnimClip.hpp"
#include <string>
#include <vector>

namespace okay {

class AnimStateMachine;
class Character;
class GameObject;

namespace HumanoidLibrary {

/// One baked clip and what it was baked from: the built-in state (the
/// Character's `anim` numbering) and, for the gaits, the speed and the
/// direction of travel as a point (x = right, z = forward, rig units/s).
struct Entry {
    AnimClip clip;
    int   state = 0;
    float speed = 0.0f;
    float x = 0.0f, z = 0.0f;
};

/// The library, baked once per run.
const std::vector<Entry>& Entries();
/// Its clip names, sorted, and a clip by name (null if none).
const std::vector<std::string>& Names();
const AnimClip* Find(const std::string& name);
/// Replace `sm`'s graph with the default one: Idle, Walk, Run, Crouch,
/// CrouchWalk, Prone, Jump, Fall, Land, Hang and the gestures, picked from the
/// movement parameters a controller reports (see CharacterAnimBackend). A state
/// clip bound on `body` (clipIdle, clipWalk, ...) plays in place of the default.
void DefaultGraph(AnimStateMachine& sm, const Character* body = nullptr);
/// The Animator of a Character body: its base state machine, added with the
/// default graph when it has none. Null when `go` has no Character, or plays an
/// imported model's clips (a Model Animator) instead.
AnimStateMachine* EnsureAnimator(GameObject* go);

} // namespace HumanoidLibrary
} // namespace okay
