#pragma once
// Shared by OkayScript builtins and the typed Engine API (visual-script nodes
// are generated from that API). One implementation, so a kind name means the
// same thing in every language that goes through this table.
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/FootIK.hpp"
#include "okay/Components/LookAtIK.hpp"
#include "okay/Components/AimIK.hpp"
#include "okay/Components/FullBodyIK.hpp"
#include "okay/Components/LimbIK.hpp"
#include "okay/Components/ChainIK.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Math/Vec3.hpp"
#include <string>

namespace okay {

enum class IkKind {
    None, Foot, Look, Aim, HandL, HandR, FootL, FootR, Full, Limb
};

inline std::string IkKindKey(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        if (c == ' ' || c == '-') continue;
        o.push_back((char)std::tolower((unsigned char)c));
    }
    return o;
}

/// foot, look, aim, hand_l, hand_r, full — plus foot_l / foot_r and a few aliases.
inline IkKind ParseIkKind(const std::string& raw) {
    const std::string k = IkKindKey(raw);
    if (k == "foot" || k == "feet") return IkKind::Foot;
    if (k == "look" || k == "lookat" || k == "head") return IkKind::Look;
    if (k == "aim") return IkKind::Aim;
    if (k == "hand_l" || k == "handl" || k == "lefthand" || k == "left_hand" || k == "lhand" || k == "l_hand")
        return IkKind::HandL;
    if (k == "hand_r" || k == "handr" || k == "righthand" || k == "right_hand" || k == "rhand" || k == "r_hand")
        return IkKind::HandR;
    if (k == "foot_l" || k == "footl" || k == "leftfoot" || k == "left_foot" || k == "lfoot")
        return IkKind::FootL;
    if (k == "foot_r" || k == "footr" || k == "rightfoot" || k == "right_foot" || k == "rfoot")
        return IkKind::FootR;
    if (k == "full" || k == "fullbody" || k == "body") return IkKind::Full;
    if (k == "limb" || k == "chain") return IkKind::Limb;
    return IkKind::None;
}

template <typename Fn>
void EachSelfAndChild(GameObject* g, Fn fn) {
    if (!g) return;
    fn(g);
    Scene* sc = g->scene();
    if (!sc) return;
    for (const auto& up : sc->Objects())
        if (up && up.get() != g && up->IsSelfOrDescendantOf(g)) fn(up.get());
}

inline Component* FindAnimatorComponent(GameObject* g) {
    Component* model = nullptr;
    Component* graph = nullptr;
    EachSelfAndChild(g, [&](GameObject* o) {
        if (graph) return;
        if (auto* sm = o->GetComponent<AnimStateMachine>()) graph = sm;
        else if (!model) if (auto* m = o->GetComponent<ModelAnimator>()) model = m;
    });
    return graph ? graph : model;
}

inline Component* FindIkComponent(GameObject* g) {
    Component* found = nullptr;
    EachSelfAndChild(g, [&](GameObject* o) {
        if (found) return;
        if (auto* c = o->GetComponent<LookAtIK>()) found = c;
        else if (auto* c = o->GetComponent<FootIK>()) found = c;
        else if (auto* c = o->GetComponent<FullBodyIK>()) found = c;
        else if (auto* c = o->GetComponent<AimIK>()) found = c;
        else if (auto* c = o->GetComponent<LimbIK>()) found = c;
        else if (auto* c = o->GetComponent<ChainIK>()) found = c;
    });
    return found;
}

inline bool SetIkKindWeight(GameObject* root, const std::string& kind, float w) {
    const IkKind k = ParseIkKind(kind);
    if (k == IkKind::None || !root) return false;
    bool any = false;
    EachSelfAndChild(root, [&](GameObject* o) {
        switch (k) {
            case IkKind::Foot:
                if (auto* f = o->GetComponent<FootIK>()) { f->weight = w; any = true; }
                break;
            case IkKind::Look:
                if (auto* l = o->GetComponent<LookAtIK>()) { l->weight = w; any = true; }
                break;
            case IkKind::Aim:
                if (auto* a = o->GetComponent<AimIK>()) { a->weight = w; any = true; }
                break;
            case IkKind::Full:
                if (auto* f = o->GetComponent<FullBodyIK>()) { f->weight = w; any = true; }
                break;
            case IkKind::HandL:
            case IkKind::HandR:
            case IkKind::FootL:
            case IkKind::FootR:
                if (auto* f = o->GetComponent<FullBodyIK>()) {
                    const int limb = k == IkKind::HandL ? FullBodyIK::LeftHand
                                   : k == IkKind::HandR ? FullBodyIK::RightHand
                                   : k == IkKind::FootL ? FullBodyIK::LeftFoot
                                                        : FullBodyIK::RightFoot;
                    auto& e = f->effectors[limb];
                    e.positionWeight = w;
                    e.enabled = w > 0.0f;
                    any = true;
                }
                break;
            case IkKind::Limb:
                if (auto* l = o->GetComponent<LimbIK>()) { l->weight = w; any = true; }
                if (auto* c = o->GetComponent<ChainIK>()) { c->weight = w; any = true; }
                break;
            default: break;
        }
    });
    return any;
}

/// `named` wins over `point`. A hand or foot goal is enabled so the limb reaches.
inline bool SetIkLimbTarget(GameObject* root, const std::string& limb, GameObject* named, const Vec3* point) {
    const IkKind k = ParseIkKind(limb);
    if (k == IkKind::None || !root || (!named && !point)) return false;
    bool any = false;
    EachSelfAndChild(root, [&](GameObject* o) {
        auto setFb = [&](int li) {
            auto* f = o->GetComponent<FullBodyIK>();
            if (!f) return;
            auto& e = f->effectors[li];
            if (named) { e.target = named->transform; e.targetName = named->name; }
            else { e.target = nullptr; e.targetName.clear(); e.position = *point; }
            e.enabled = true;
            if (e.positionWeight <= 0.0f) e.positionWeight = 1.0f;
            any = true;
        };
        switch (k) {
            case IkKind::HandL: setFb(FullBodyIK::LeftHand); break;
            case IkKind::HandR: setFb(FullBodyIK::RightHand); break;
            case IkKind::FootL: setFb(FullBodyIK::LeftFoot); break;
            case IkKind::FootR: setFb(FullBodyIK::RightFoot); break;
            case IkKind::Foot:
                setFb(FullBodyIK::LeftFoot);
                setFb(FullBodyIK::RightFoot);
                break;
            case IkKind::Look:
                if (auto* l = o->GetComponent<LookAtIK>()) {
                    if (named) { l->targetObject = named->transform; l->targetName = named->name; }
                    else { l->targetObject = nullptr; l->targetName.clear(); l->target = *point; l->aimAtPoint = true; }
                    l->ikActive = true;
                    any = true;
                }
                break;
            case IkKind::Aim:
                if (auto* a = o->GetComponent<AimIK>()) {
                    if (named) { a->targetObject = named->transform; a->targetName = named->name; }
                    else { a->targetObject = nullptr; a->targetName.clear(); a->target = *point; }
                    a->ikActive = true;
                    any = true;
                }
                break;
            case IkKind::Limb:
                if (auto* l = o->GetComponent<LimbIK>()) {
                    if (named) { l->targetObject = named->transform; l->targetName = named->name; }
                    else { l->targetObject = nullptr; l->targetName.clear(); l->target = *point; }
                    l->ikActive = true;
                    any = true;
                }
                if (auto* c = o->GetComponent<ChainIK>()) {
                    if (named) { c->targetObject = named->transform; c->targetName = named->name; }
                    else { c->targetObject = nullptr; c->targetName.clear(); c->target = *point; }
                    c->ikActive = true;
                    any = true;
                }
                break;
            case IkKind::Full:
                if (auto* f = o->GetComponent<FullBodyIK>()) {
                    if (named) { f->lookTarget = named->transform; f->lookTargetName = named->name; }
                    else { f->lookTarget = nullptr; f->lookTargetName.clear(); f->lookPosition = *point; }
                    f->lookEnabled = true;
                    any = true;
                }
                break;
            default: break;
        }
    });
    return any;
}

inline bool ClearIkLimbTarget(GameObject* root, const std::string& limb) {
    const IkKind k = ParseIkKind(limb);
    if (k == IkKind::None || !root) return false;
    bool any = false;
    EachSelfAndChild(root, [&](GameObject* o) {
        auto clr = [&](int li) {
            auto* f = o->GetComponent<FullBodyIK>();
            if (!f) return;
            auto& e = f->effectors[li];
            e.target = nullptr;
            e.targetName.clear();
            e.enabled = false;
            any = true;
        };
        switch (k) {
            case IkKind::HandL: clr(FullBodyIK::LeftHand); break;
            case IkKind::HandR: clr(FullBodyIK::RightHand); break;
            case IkKind::FootL: clr(FullBodyIK::LeftFoot); break;
            case IkKind::FootR: clr(FullBodyIK::RightFoot); break;
            case IkKind::Foot:
                clr(FullBodyIK::LeftFoot);
                clr(FullBodyIK::RightFoot);
                break;
            case IkKind::Look:
                if (auto* l = o->GetComponent<LookAtIK>()) {
                    l->targetObject = nullptr;
                    l->targetName.clear();
                    l->aimAtPoint = false;
                    any = true;
                }
                break;
            case IkKind::Aim:
                if (auto* a = o->GetComponent<AimIK>()) {
                    a->targetObject = nullptr;
                    a->targetName.clear();
                    a->ikActive = false;
                    any = true;
                }
                break;
            case IkKind::Limb:
                if (auto* l = o->GetComponent<LimbIK>()) {
                    l->targetObject = nullptr; l->targetName.clear(); l->ikActive = false; any = true;
                }
                if (auto* c = o->GetComponent<ChainIK>()) {
                    c->targetObject = nullptr; c->targetName.clear(); c->ikActive = false; any = true;
                }
                break;
            case IkKind::Full:
                if (auto* f = o->GetComponent<FullBodyIK>()) {
                    f->lookTarget = nullptr; f->lookTargetName.clear(); f->lookEnabled = false;
                    for (auto& e : f->effectors) { e.target = nullptr; e.targetName.clear(); e.enabled = false; }
                    any = true;
                }
                break;
            default: break;
        }
    });
    return any;
}

/// `byName`: match AnimStateMachine::layerName. Otherwise match the layer index.
inline AnimStateMachine* FindAnimLayer(GameObject* g, bool byName, const std::string& name, int index) {
    if (!g) return nullptr;
    AnimStateMachine* found = nullptr;
    EachSelfAndChild(g, [&](GameObject* o) {
        if (found) return;
        for (AnimStateMachine* sm : o->GetComponents<AnimStateMachine>()) {
            if (!sm) continue;
            if (byName ? sm->layerName == name : sm->layer == index) { found = sm; return; }
        }
    });
    return found;
}

inline bool SetRootMotionOn(GameObject* g, bool on) {
    if (!g) return false;
    bool any = false;
    EachSelfAndChild(g, [&](GameObject* o) {
        if (any) return;
        if (auto* m = o->GetComponent<ModelAnimator>()) { m->rootMotion = on; any = true; }
    });
    return any;
}

inline bool SetCharacterModelPath(GameObject* g, const std::string& path) {
    if (!g || !g->scene() || path.empty()) return false;
    GameObject* host = g;
    for (Transform* t = g->transform; t; t = t->Parent()) {
        GameObject* p = t->gameObject;
        if (p && (p->GetComponent<Character>() || p->GetComponent<ThirdPersonController>() ||
                  p->GetComponent<FirstPersonController>()))
            host = p;
    }
    return AttachCharacterModel(*g->scene(), host, path) != nullptr;
}

} // namespace okay
