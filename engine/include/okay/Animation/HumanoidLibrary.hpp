#pragma once
// ---------------------------------------------------------------------------
// HumanoidLibrary — the built-in humanoid's default clips and Animator graph.
// The clips are baked from HumanoidMotion: idle; walk, run and crouch-walk at
// several speeds in eight directions (the gait's hip bob in each clip's root
// track); crouch; prone, the crawl four ways, getting up through a kneel and
// dropping down; jump, fall, land and hang; the gestures and emotions. Every gait cycle loops seamlessly. Clip names are stable: the
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
/// Time the default graph's stance changes by `sm.stanceTimes`: the crossfades
/// between standing, crouched and prone states, and how long getting up from
/// prone (GetUp) and dropping prone (Drop) take. States with other names are
/// left alone. Those pairs can be interrupted from either state.
void ApplyStanceTimes(AnimStateMachine& sm);
/// Time the default graph's locomotion crossfades by `sm.crossfades`: idle <->
/// walk, walk <-> run (and idle <-> run), into the air, and back onto the
/// ground. Those pairs can be interrupted from either state. Stance changes
/// are left to ApplyStanceTimes. A saved scene keeps the blends it was saved
/// with until this table is edited.
void ApplyCrossfades(AnimStateMachine& sm);
/// The Animator of a Character body: its base state machine, added with the
/// default graph when it has none. Null when `go` has no Character, or plays an
/// imported model's clips (a Model Animator) instead.
AnimStateMachine* EnsureAnimator(GameObject* go);

} // namespace HumanoidLibrary
} // namespace okay
