#pragma once
// Finding an object by name from the point of view of another object: the one in the same hierarchy first.
//
// Components that point at another object do it by name (Joint2D / Joint3D `connectedBody`, CameraFollow
// `targetName`, ...), and Scene::Find(name) answers with the first object of that name in scene order. That is
// the right answer for a scene of unique names, and the wrong one after a clone: a copy of a vehicle made with
// Scene::Instantiate has the same names as the original, so a joint in the copy resolved "Axle" to the
// original's axle. FindNearest asks the question from where the joint sits instead.
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include <string>
#include <vector>

namespace okay {

/// The object called `name` that is nearest to `from` in the hierarchy: `from` and everything below it
/// first, then the rest of its parent's subtree, then its grandparent's, and so on up to the top (each level in
/// depth-first, sibling order). Only when the whole hierarchy `from` belongs to has no such object does it fall
/// back to scene.Find(name), which also reads paths ("Parent/Child") and may answer with an object of another
/// hierarchy. With a null `from`, an object of another scene or a name that is a path it is scene.Find(name).
/// Cost: the size of the hierarchy that is searched, never more than the whole scene.
inline GameObject* FindNearest(const Scene& scene, const std::string& name, const GameObject* from) {
    if (name.empty()) return nullptr;
    if (from && from->scene() == &scene && from->transform && !from->IsDestroyed() && name.find('/') == std::string::npos) {
        const GameObject* level = from;
        const GameObject* searched = nullptr;   // the child of `level` whose subtree an earlier, lower level already covered
        while (level) {
            std::vector<GameObject*> stack{const_cast<GameObject*>(level)};
            while (!stack.empty()) {
                GameObject* g = stack.back();
                stack.pop_back();
                if (g == searched) continue;
                if (g->name == name) return g;
                if (g->transform) {
                    const auto& kids = g->transform->Children();
                    for (std::size_t i = kids.size(); i-- > 0;)
                        if (kids[i] && kids[i]->gameObject) stack.push_back(kids[i]->gameObject);
                }
            }
            const Transform* up = level->transform ? level->transform->Parent() : nullptr;
            searched = level;
            level = up ? up->gameObject : nullptr;
        }
    }
    return scene.Find(name);
}

} // namespace okay
