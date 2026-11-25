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
void BindHitLayer(AnimStateMachine& base);
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
// Graph editing helpers defined in HumanoidActions.cpp, used by the state builders in HumanoidActionsStates.cpp.
AnimStateMachine::State* FindMutable(AnimStateMachine& sm, const std::string& name);
void EnsureParam(AnimStateMachine& sm, const char* name, int type);
bool HasAny(const AnimStateMachine& sm, const char* to, const char* param);
void WireDeath(AnimStateMachine::State& death, const AnimStateMachine& sm);
void WireHold(AnimStateMachine::State& hold, const AnimStateMachine& sm);
AnimStateMachine::State MakeDeathState();
AnimStateMachine::State MakeHoldState();
AnimStateMachine::State MakeSwimState();
void WireSwim(AnimStateMachine::State& swim, const AnimStateMachine& sm);
AnimStateMachine::State MakeEmoteState(const char* name);
void WireEmote(AnimStateMachine::State& st, const AnimStateMachine& sm);
void AddEmoteAny(AnimStateMachine& sm, const char* to, float id);
} // namespace humanoid_actions
} // namespace HumanoidActions

} // namespace okay
