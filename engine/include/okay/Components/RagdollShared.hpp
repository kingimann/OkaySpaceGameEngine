#pragma once
// RagdollShared - helpers of the Ragdoll component, in one place:
//  - FindBones: the 15 humanoid bones (the blocky Character's part rig, or an
//    imported rig through humanoid::MapHumanoidBones);
//  - DriverKind / SwitchOff / SwitchBackOn: the one list of components that
//    drive a body (animation and IK, movement controllers and Rigidbody3D) and
//    switching them off while a ragdoll owns the body;
//  - HealthWatch: death, revive and hits read from a HealthStat (or a script
//    die() through DeathLocksInput), as events each component acts on.
#include "okay/Animation/HumanoidBones.hpp"
#include "okay/Math/Vec3.hpp"
#include <vector>

namespace okay {

class Component;
class GameObject;
class Transform;

namespace ragdoll {

/// Which kind of driver a component is (0 = none).
enum DriverKinds : unsigned { kAnimation = 1u, kMovement = 2u, kAllDrivers = 3u };
unsigned DriverKind(const Component* c);

/// Switch off the enabled drivers of the given kinds on `root` (and every
/// object below it when `wholeTree`), skipping `self`. Rigidbody3D velocity is
/// zeroed. The switched components are appended to `switched`.
void SwitchOff(GameObject* root, const Component* self, unsigned kinds, bool wholeTree,
               std::vector<Component*>& switched);
/// Switch back on what SwitchOff switched off, and clear the list.
void SwitchBackOn(std::vector<Component*>& switched);

/// First surface along from -> to, skipping `self` and everything below it
/// (Physics3D::Raycast under an IgnoreHierarchy; triggers do not count).
bool CastIgnoringSelf(GameObject* self, const Vec3& from, const Vec3& to, Vec3& point, Vec3& normal,
                      GameObject** hitObject = nullptr);

/// The 15 humanoid bones under `go`. The blocky Character is split into its
/// part rig first if it is not yet. False if any bone is missing.
bool FindBones(GameObject* go, Transform* out[humanoid::BoneCount]);

/// Death, revive and hit events from a body's HealthStat.
struct HealthWatch {
    enum Event { None, Died, Revived, Hurt };
    bool  wasDead = false;
    float lastHealth = -1.0f;
    /// Remember the starting state (call from Start).
    void Begin(GameObject* go);
    /// One event per call. `damage` and `from` are filled when known.
    Event Poll(GameObject* go, float* damage = nullptr, Vec3* from = nullptr, bool* hasFrom = nullptr);
};

} // namespace ragdoll
} // namespace okay
