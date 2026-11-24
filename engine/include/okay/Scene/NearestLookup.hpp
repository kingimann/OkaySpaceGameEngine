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
#include "okay/Components/Joint3D.hpp"
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


/// The partner a joint names, found with FindNearest once and remembered on the joint until the scene's objects change
/// (Scene::StructureVersion), so a scene with hundreds of joints does not search by name every physics step. `J` is Joint2D
/// or Joint3D (it has connectedBody, gameObject and a partner cache). A name that finds nothing is searched again next time.
template <class J>
inline GameObject* CachedPartner(const Scene& scene, J* j) {
    if (j->connectedBody.empty()) return nullptr;
    const std::uint64_t version = scene.StructureVersion();
    auto& c = j->partner;
    if (c.object && c.version == version && c.object->name == j->connectedBody) return c.object;
    c.object = FindNearest(scene, j->connectedBody, j->gameObject);
    c.version = c.object ? version : 0;
    return c.object;
}

/// The other body of a Joint3D: the object ConnectTo linked, or the one its saved guid names, or (when
/// neither was used) the one named `connectedBody`. A linked object that is gone does not fall back to a
/// same-named stand-in.
inline GameObject* Joint3DPartner(const Scene& scene, Joint3D* j) {
    if (!j) return nullptr;
    if (j->connectedId != 0) {
        GameObject* g = j->Linked();
        if (!g || g->scene() != &scene) return nullptr;
        return g;
    }
    if (!j->connectedGuid.empty()) {
        GameObject* g = scene.FindByGuid(j->connectedGuid);
        if (!g || g->IsDestroyed() || g->scene() != &scene) return nullptr;
        j->connectedId = g->InstanceId();
        j->partner.object = g;
        j->partner.version = 0;
        return g;
    }
    return j->connectedBody.empty() ? nullptr : CachedPartner(scene, j);
}

namespace jointlink {

inline bool Under(const GameObject* root, const GameObject* obj) {
    for (const GameObject* p = obj; p; ) {
        if (p == root) return true;
        const Transform* up = p->transform ? p->transform->Parent() : nullptr;
        p = up ? up->gameObject : nullptr;
    }
    return false;
}

inline std::string PathFrom(const GameObject* root, const GameObject* obj) {
    std::vector<std::string> names;
    for (const GameObject* p = obj; p && p != root; ) {
        names.push_back(p->name);
        const Transform* up = p->transform ? p->transform->Parent() : nullptr;
        p = up ? up->gameObject : nullptr;
    }
    std::string path;
    for (auto it = names.rbegin(); it != names.rend(); ++it) {
        if (!path.empty()) path += "/";
        path += *it;
    }
    return path;
}

inline GameObject* AtPath(GameObject* root, const std::string& path) {
    if (!root) return nullptr;
    if (path.empty()) return root;
    GameObject* cur = root;
    std::size_t i = 0;
    while (cur && i < path.size()) {
        std::size_t j = path.find('/', i);
        if (j == std::string::npos) j = path.size();
        const std::string name = path.substr(i, j - i);
        GameObject* next = nullptr;
        if (cur->transform)
            for (Transform* ch : cur->transform->Children())
                if (ch && ch->gameObject && !ch->gameObject->IsDestroyed() && ch->gameObject->name == name) {
                    next = ch->gameObject;
                    break;
                }
        cur = next;
        i = j + 1;
    }
    return cur;
}

} // namespace jointlink

/// After a hierarchy is cloned, a joint whose partner was inside that hierarchy must link the copy,
/// not the original. A partner that was outside the hierarchy keeps its link.
inline void RetargetClonedJoints(const GameObject* oldRoot, GameObject* newRoot) {
    if (!oldRoot || !newRoot || !newRoot->scene()) return;
    std::vector<GameObject*> stack{newRoot};
    while (!stack.empty()) {
        GameObject* g = stack.back();
        stack.pop_back();
        if (!g || g->IsDestroyed()) continue;
        for (Joint3D* j : g->GetComponents<Joint3D>()) {
            if (!j) continue;
            GameObject* partner = nullptr;
            if (!j->connectedGuid.empty()) partner = newRoot->scene()->FindByGuid(j->connectedGuid);
            if (!partner && j->connectedId != 0) partner = j->Linked();
            if (!partner || !jointlink::Under(oldRoot, partner)) continue;
            GameObject* copy = jointlink::AtPath(newRoot, jointlink::PathFrom(oldRoot, partner));
            if (copy && copy != partner) j->ConnectTo(copy);
        }
        if (g->transform)
            for (Transform* ch : g->transform->Children())
                if (ch && ch->gameObject) stack.push_back(ch->gameObject);
    }
}

} // namespace okay
