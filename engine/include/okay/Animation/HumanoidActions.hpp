#pragma once
// ---------------------------------------------------------------------------
// HumanoidActions — built-in action clips (death and swim, and later dive, sit,
// hit, roll, vault, emotes) baked the same way as HumanoidLibrary, plus the
// states, params and Any-State transitions they add to the default Animator.
// Gameplay displacement stays on the controllers. Clip root tracks are visual.
// ---------------------------------------------------------------------------
#include "okay/Animation/HumanoidLibrary.hpp"
#include "okay/Math/Vec3.hpp"
#include <string>
#include <vector>

namespace okay {

class GameObject;

namespace HumanoidActions {

/// How far the built-in action set has got. Death is 1, swim is 2. A saved
/// graph records the revision it already has, so a later action is added once
/// and a state the user deleted is not put back when the next action lands.
constexpr int kActionsRev = 2;

/// One authored pose. Degrees, hip height in rig units, rootOffset.x/z a visual
/// lean only (y is unused; hipY is the root height).
struct KeyPose {
    float t = 0.0f;
    Vec3  bones[15]{};
    float hipY = 0.0f;
    Vec3  rootOffset{};
};

/// Collider height remembered across a death shrink, so revive stands back up.
struct DeadShape {
    bool  saved = false;
    bool  hadCap = false, hadBox = false;
    float capH = 0.0f, capOff = 0.0f;
    float boxY = 0.0f, boxOff = 0.0f;
};

/// Append this revision's clips to the library bake.
void Bake(std::vector<HumanoidLibrary::Entry>& out);
/// Append this revision's states, params and Any-State transitions. Safe to
/// call again: a state that already exists is left as the user edited it.
/// `force` fills anything missing (the inspector button) and still does not
/// overwrite a state. Otherwise only actions newer than `sm.actionsRev` are added.
void AddStates(AnimStateMachine& sm, const Character* body, bool force = false);
/// Stance-timed blends for action transitions (called from ApplyStanceTimes).
void ApplyTimes(AnimStateMachine& sm);
/// Saved default graphs (Idle/Walk/Run/Jump) gain any action whose revision is
/// newer than `sm.actionsRev`. `force` adds whatever is missing anyway — the
/// inspector's "Add built-in actions" — and still never overwrites a state.
void MergeMissing(AnimStateMachine& sm, const Character* body, bool force);

/// State names and the clip names play_action accepts (Death, Death_F, ...).
const std::vector<std::string>& ActionNames();

/// Go to the action state, or a blend-tree motion whose clip has this name.
/// A Model Animator with no such state falls through to play_clip. 0 if nothing
/// played. `blend` < 0 uses 0.2s.
bool Play(GameObject* go, const std::string& name, float blend = 0.2f);
bool IsInAction(GameObject* go);
float ActionTime(GameObject* go);

/// "forward"/"back"/"left"/"right" (and f/b/l/r, 0..3). Anything else is forward.
int DirFromName(const std::string& name);

/// Play Death. `hitFromWorld` is where the hit came from; the body falls away
/// from it. Null means forward. Sets `dead` and does not leave the `die`
/// trigger latched (a latched trigger would re-enter Death after revive).
bool PlayDeath(GameObject* go, const Vec3* hitFromWorld);
/// Script/VS entry: if a HealthStat is here, this kills it (which plays the
/// anim when playDeathAnim is on). Otherwise just the anim.
void RequestDeath(GameObject* go, int dir, const Vec3* hitFromWorld);
/// Clears `dead` so Death/DeadHold can get up. A dead HealthStat respawns.
void RequestRevive(GameObject* go);
void ReviveAnim(GameObject* go);
/// HealthStat::IsDead when that component is present, otherwise the `dead` param.
bool IsDead(GameObject* go);

/// Sets `swimming` (and `swimSpeed` when `speed` >= 0). The default graph enters
/// Swim while the bool is true and leaves for Idle when it is false. 0 treads,
/// about 1.5 plays the forward stroke. No water volume: a later controller sets this.
void SetSwimming(GameObject* go, bool on, float speed = -1.0f);
/// True while `swimming` is set, or the base machine is in Swim.
bool IsSwimming(GameObject* go);

/// True once the body is in DeadHold (or Death has played out).
bool DeathPoseSettled(GameObject* go);
void ShrinkDeadBody(GameObject* go, DeadShape& shape);
void RestoreDeadBody(GameObject* go, DeadShape& shape);

} // namespace HumanoidActions
} // namespace okay
