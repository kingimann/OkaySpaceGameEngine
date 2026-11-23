// Private to engine/src/Animation/HumanoidActions*.cpp: the helpers of the action bakes and state wiring
// (HumanoidActions.cpp) that the runtime half (HumanoidActionsRuntime.cpp: Play, death, sit, hit, dodge, traversal and
// Seat) also calls. Not installed, not for other folders.
#pragma once

#include "okay/Components/AnimStateMachine.hpp"
#include <string>

namespace okay {

class GameObject;
struct Vec3;

namespace HumanoidActions {
namespace humanoid_actions {
void NoteNetEvent(GameObject* go, int id, float param);
bool HasState(const AnimStateMachine& sm, const char* name);
bool IsKnownAction(const std::string& name);
bool IsDeathLabel(const std::string& name);
bool IsSwimLabel(const std::string& name);
void LatchSwim(AnimStateMachine& sm, float speed);
int DirOfClip(const std::string& name);
AnimStateMachine* BaseMachine(GameObject* go);
int DirFromWorld(GameObject* go, const Vec3& fromWorld);
void LatchDeath(AnimStateMachine& sm, int dir);
extern const char* kEmoteNames[];
} // namespace humanoid_actions
} // namespace HumanoidActions

} // namespace okay
