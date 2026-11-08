#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"

namespace okay {

namespace {

int SlotForState(const std::string& name) {
    if (name == "Idle") return ModelAnimator::SlotIdle;
    if (name == "Walk" || name == "Locomotion") return ModelAnimator::SlotWalk;
    if (name == "Run") return ModelAnimator::SlotRun;
    if (name == "Jump") return ModelAnimator::SlotJump;
    if (name == "Fall") return ModelAnimator::SlotFall;
    if (name == "Land") return ModelAnimator::SlotLand;
    if (name == "Crouch") return ModelAnimator::SlotCrouch;
    if (name == "CrouchWalk") return ModelAnimator::SlotCrouchWalk;
    if (name == "Prone") return ModelAnimator::SlotProne;
    if (name == "Crawl" || name == "Drop") return ModelAnimator::SlotCrawl;
    if (name == "GetUp") return ModelAnimator::SlotGetUp;
    return -1;
}

bool RigHas(const ModelAnimator& ma, const std::string& name) {
    return !name.empty() && ma.FindClip(name) >= 0;
}

} // namespace

bool ModelAnimator::ActionPoseUncovered() const {
    const AnimStateMachine* sm = nullptr;
    for (Transform* t = transform; t && !sm; t = t->Parent()) {
        if (!t->gameObject) continue;
        for (AnimStateMachine* m : t->gameObject->GetComponents<AnimStateMachine>())
            if (m->layer == 0) { sm = m; break; }
    }
    if (!sm) return false;
    const std::string cur = sm->Current();
    if (cur.empty()) return false;
    const AnimStateMachine::State* st = nullptr;
    for (const auto& s : sm->states)
        if (s.name == cur) { st = &s; break; }
    if (!st) return false;

    const int slot = SlotForState(cur);
    if (slot >= 0) {
        if (SlotFilled(slot)) return false;
        // A locomotion blend still owns the pose when any gait clip is filled.
        if (cur == "Locomotion" && (SlotFilled(SlotIdle) || SlotFilled(SlotWalk) || SlotFilled(SlotRun)))
            return false;
        return true;
    }
    if (RigHas(*this, st->clip) || RigHas(*this, st->fallbackClip)) return false;
    for (const auto& m : st->motions)
        if (RigHas(*this, m.clip)) return false;
    return true;
}

} // namespace okay
