#pragma once
// ---------------------------------------------------------------------------
// IKBones — resolve a bone, target or pole by name for an IK component. Two
// copies of one imported model share every bone name, so a scene-wide Find()
// hands the second character the first one's bones. ResolveBone looks close to
// home first:
//   1. under the component's own object;
//   2. under the character it belongs to: the nearest object up the hierarchy
//      with a Character or a Rigidbody3D (the controllers' movement object);
//   3. under the top of that hierarchy;
//   4. anywhere in the scene (a target on a wall, a pole helper in the level).
// ---------------------------------------------------------------------------
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include <string>
#include <vector>

namespace okay {

/// The object named `name` at or below `top`, or null.
inline Transform* FindInTree(Transform* top, const std::string& name) {
    if (!top) return nullptr;
    std::vector<Transform*> stack{top};
    while (!stack.empty()) {
        Transform* t = stack.back(); stack.pop_back();
        if (t->gameObject && t->gameObject->name == name) return t;
        for (Transform* c : t->Children()) if (c) stack.push_back(c);
    }
    return nullptr;
}

/// The character `t` belongs to: the nearest ancestor-or-self carrying a
/// Character or a Rigidbody3D. Null when there is none.
inline Transform* CharacterRootOf(Transform* t) {
    for (Transform* p = t; p; p = p->Parent())
        if (p->gameObject && (p->gameObject->GetComponent<Character>() || p->gameObject->GetComponent<Rigidbody3D>()))
            return p;
    return nullptr;
}

/// See the header comment for the search order. Returns null for an empty name
/// or when nothing matches.
inline Transform* ResolveBone(Scene* s, GameObject* owner, const std::string& name) {
    if (name.empty()) return nullptr;
    Transform* self = owner ? owner->transform : nullptr;
    if (Transform* t = FindInTree(self, name)) return t;
    Transform* root = CharacterRootOf(self);
    if (root && root != self) if (Transform* t = FindInTree(root, name)) return t;
    Transform* top = self;
    while (top && top->Parent()) top = top->Parent();
    if (top && top != self && top != root) if (Transform* t = FindInTree(top, name)) return t;
    if (s) if (GameObject* g = s->Find(name)) return g->transform;
    return nullptr;
}

} // namespace okay
