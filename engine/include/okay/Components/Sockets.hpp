#pragma once
#include "okay/Components/Character.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include <string>

namespace okay {

/// Where to attach something on a character, by name. Shared by Equipment and object
/// holding so both find the same hand. Tries, in order:
///  1. a descendant of `character` with exactly that name (a socket object you placed,
///     e.g. "HandSocket_R", or an imported bone);
///  2. a built-in Character bone token ("head", "torso", "r_hand", ...) on its part rig
///     (needs separate parts; see Character::BuildParts);
///  3. a node of the character's ModelAnimator model.
/// Returns null when nothing matches; callers then attach to the character itself.
inline GameObject* FindSocket(GameObject* character, const std::string& name) {
    if (!character || !character->transform || name.empty()) return nullptr;
    // 1. Named descendant, breadth-first so a near socket wins over a deep namesake.
    std::vector<Transform*> queue(character->transform->Children().begin(), character->transform->Children().end());
    for (std::size_t i = 0; i < queue.size(); ++i) {
        Transform* t = queue[i];
        if (!t) continue;
        if (t->gameObject && t->gameObject->name == name) return t->gameObject;
        queue.insert(queue.end(), t->Children().begin(), t->Children().end());
    }
    // 2. Character part-rig bone.
    if (auto* ch = character->GetComponent<Character>()) {
        const int bone = Character::BoneIndex(name);
        if (bone >= 0 && ch->PartsBuilt())
            if (GameObject* part = ch->Part(bone)) return part;
    }
    // 3. ModelAnimator node, only inside this character (FindNode can fall back to
    //    the whole scene, which could pick another character's bone).
    if (auto* ma = character->GetComponent<ModelAnimator>())
        if (GameObject* node = ma->FindNode(name))
            for (Transform* t = node->transform; t; t = t->Parent())
                if (t == character->transform) return node;
    return nullptr;
}

} // namespace okay
