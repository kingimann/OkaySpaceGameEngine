#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Render/Renderer.hpp"
#include <algorithm>
#include <functional>
#include <vector>

namespace okay {

Scene::Scene(std::string name) : m_name(std::move(name)) {}
Scene::~Scene() = default;

GameObject* Scene::Instantiate(const GameObject& prefab) {
    return SceneSerializer::Instantiate(*this, prefab);
}

GameObject* Scene::Instantiate(const GameObject& prefab, const Vec3& position) {
    GameObject* go = SceneSerializer::Instantiate(*this, prefab);
    if (go) go->transform->localPosition = position;
    return go;
}

GameObject* Scene::CreateGameObject(const std::string& name) {
    auto go = std::unique_ptr<GameObject>(new GameObject(this, name));
    GameObject* ptr = go.get();
    m_objects.push_back(std::move(go));
    ptr->transform = ptr->AddComponent<Transform>();
    return ptr;
}

void Scene::Clear() {
    m_pending.clear();
    m_active.clear();
    m_destroyQueue.clear();
    m_scheduler.Clear();
    m_physics.Clear();
    mainCamera = nullptr;
    m_objects.clear();
    renderSettings = RenderSettings{};   // back to default sky/ambient
}

void Scene::QueuePending(Component* component) {
    m_pending.push_back(component);
}

void Scene::NotifyComponentRemoved(Component* component) {
    // Null the slot instead of erasing, so removing a component from inside an
    // m_active iteration (a component's Update/Render) can't invalidate the loop
    // or shift elements. Null slots are skipped by every loop and compacted
    // between frames.
    for (auto& c : m_active)  if (c == component) c = nullptr;
    for (auto& c : m_pending) if (c == component) c = nullptr;
    if (static_cast<Component*>(mainCamera) == component) mainCamera = nullptr;
}

void Scene::FlushPending() {
    if (m_pending.empty()) return;
    // Awake the whole batch first, then Start it (Unity message order).
    std::vector<Component*> batch;
    batch.swap(m_pending);

    for (Component* c : batch) {
        if (!c) continue;                 // removed before it was flushed
        c->Awake();
        m_active.push_back(c);
    }
    for (Component* c : batch) {
        if (c && !c->m_started && c->enabled) {
            c->Start();
            c->m_started = true;
        }
    }
}

void Scene::Start() {
    FlushPending();
}

void Scene::Update(float deltaTime) {
    FlushPending(); // adopt anything created since last frame
    // Drop any components removed last frame (their slots were nulled).
    m_active.erase(std::remove(m_active.begin(), m_active.end(), nullptr), m_active.end());

    m_scheduler.Update(deltaTime);

    for (Component* c : m_active) {
        if (c && c->enabled && c->gameObject && c->gameObject->active)
            c->Update(deltaTime);
    }

    if (physicsEnabled) { m_physics.Step(*this, deltaTime); m_physics3d.Step(*this, deltaTime); }

    for (Component* c : m_active) {
        if (c && c->enabled && c->gameObject && c->gameObject->active)
            c->LateUpdate(deltaTime);
    }

    if (!m_destroyQueue.empty()) {
        for (GameObject* go : m_destroyQueue) {
            // Run OnDestroy and drop the object's components from the lists.
            for (auto& obj : m_objects) {
                if (obj.get() != go) continue;
                for (Component* c : m_active)
                    if (c && c->gameObject == go) c->OnDestroy();
                break;
            }
            // Also drops any null slots (components removed this frame).
            auto isOwned = [go](Component* c) { return !c || c->gameObject == go; };
            m_active.erase(std::remove_if(m_active.begin(), m_active.end(), isOwned),
                           m_active.end());
            m_pending.erase(std::remove_if(m_pending.begin(), m_pending.end(), isOwned),
                            m_pending.end());
            if (mainCamera && mainCamera->gameObject == go) mainCamera = nullptr;
            m_objects.erase(
                std::remove_if(m_objects.begin(), m_objects.end(),
                               [go](const std::unique_ptr<GameObject>& o) {
                                   return o.get() == go;
                               }),
                m_objects.end());
        }
        m_destroyQueue.clear();
    }

    // A deferred scene load (requested via RequestLoad) happens here, after all
    // iteration is done, so it's safe to replace the whole scene.
    if (m_hasPendingLoad) {
        std::string path = m_pendingLoad;
        m_hasPendingLoad = false;
        m_pendingLoad.clear();
        if (SceneSerializer::LoadFromFile(*this, path)) {
            Start(); // run Awake/Start for the freshly loaded objects
        }
    }
}

void Scene::Render(IRenderer& renderer) {
    Color clear = mainCamera ? mainCamera->backgroundColor : Color::Black;
    renderer.BeginFrame(clear);

    if (mainCamera && mainCamera->transform) {
        Viewport vp = renderer.GetViewport();
        renderer.SetCamera(mainCamera->transform->Position(),
                           mainCamera->orthographicSize, vp.Aspect());
    }

    for (Component* c : m_active) {
        if (c && c->enabled && c->gameObject && c->gameObject->active)
            c->OnRender(renderer);
    }

    renderer.EndFrame();
}

void Scene::Destroy(GameObject* go) {
    if (!go) return;
    // Detach from the parent first, so a surviving parent's child list isn't left
    // with a dangling pointer to this freed object (deleting a child UI widget,
    // hierarchy node, etc. — the #1 editor delete crash).
    if (go->transform && go->transform->Parent())
        go->transform->SetParent(nullptr, /*worldPositionStays=*/false);
    // Queue the whole subtree: a child outliving its parent would itself be left
    // with a dangling parent Transform pointer. (Detaching is only needed for the
    // root — the descendants' parents are destroyed together with them.)
    std::function<void(GameObject*)> queue = [&](GameObject* g) {
        if (!g) return;
        m_destroyQueue.push_back(g);
        if (g->transform) {
            std::vector<Transform*> kids = g->transform->Children();   // copy: recursion-safe
            for (Transform* child : kids)
                if (child && child->gameObject) queue(child->gameObject);
        }
    };
    queue(go);
}

GameObject* Scene::Find(const std::string& name) const {
    for (const auto& o : m_objects)
        if (o->name == name) return o.get();
    return nullptr;
}

GameObject* Scene::FindWithTag(const std::string& tag) const {
    for (const auto& o : m_objects)
        if (o->tag == tag) return o.get();
    return nullptr;
}

void Scene::MoveToFront(GameObject* go) {
    for (std::size_t i = 0; i < m_objects.size(); ++i)
        if (m_objects[i].get() == go) {
            auto held = std::move(m_objects[i]);
            m_objects.erase(m_objects.begin() + i);
            m_objects.push_back(std::move(held));   // drawn last = on top
            return;
        }
}

void Scene::MoveToBack(GameObject* go) {
    for (std::size_t i = 0; i < m_objects.size(); ++i)
        if (m_objects[i].get() == go) {
            auto held = std::move(m_objects[i]);
            m_objects.erase(m_objects.begin() + i);
            m_objects.insert(m_objects.begin(), std::move(held));  // drawn first = behind
            return;
        }
}

void Scene::MoveSibling(GameObject* go, int dir) {
    if (!go || dir == 0) return;
    // A child reorders within its parent's child list (the order the hierarchy
    // and UI layering use).
    if (go->transform && go->transform->Parent()) {
        go->transform->Parent()->MoveChild(go->transform, dir);
        return;
    }
    // A root object reorders among the other ROOT objects (parented objects keep
    // their place), so only the visible top-level order changes.
    std::vector<int> roots; int selfPos = -1;
    for (int i = 0; i < (int)m_objects.size(); ++i) {
        GameObject* o = m_objects[i].get();
        if (o->transform && o->transform->Parent()) continue;
        if (o == go) selfPos = (int)roots.size();
        roots.push_back(i);
    }
    int other = selfPos + dir;
    if (selfPos >= 0 && other >= 0 && other < (int)roots.size())
        std::swap(m_objects[roots[selfPos]], m_objects[roots[other]]);
}

void Scene::MoveSiblingToEdge(GameObject* go, bool toFront) {
    if (!go) return;
    if (go->transform && go->transform->Parent()) {
        go->transform->Parent()->MoveChildToEdge(go->transform, toFront);
        return;
    }
    // Root: a root's hierarchy position is the order of its entry in m_objects
    // (children draw under their parents regardless), so moving the entry to the
    // front/back of m_objects puts it first/last among roots.
    for (std::size_t i = 0; i < m_objects.size(); ++i)
        if (m_objects[i].get() == go) {
            auto held = std::move(m_objects[i]);
            m_objects.erase(m_objects.begin() + i);
            if (toFront) m_objects.insert(m_objects.begin(), std::move(held));
            else         m_objects.push_back(std::move(held));
            return;
        }
}

void Scene::ReorderSibling(GameObject* go, GameObject* anchor, bool after) {
    if (!go || !anchor || go == anchor) return;
    Transform* ap = anchor->transform ? anchor->transform->Parent() : nullptr;
    // Reparent into the anchor's parent (keeping world pose) if needed.
    if (go->transform && go->transform->Parent() != ap)
        go->transform->SetParent(ap, /*worldPositionStays=*/true);
    if (ap) {
        ap->ReorderChild(go->transform, anchor->transform, after);
        return;
    }
    // Both roots: move go's m_objects entry adjacent to the anchor's.
    int gi = -1;
    for (int i = 0; i < (int)m_objects.size(); ++i) if (m_objects[i].get() == go) { gi = i; break; }
    if (gi < 0) return;
    auto held = std::move(m_objects[gi]);
    m_objects.erase(m_objects.begin() + gi);
    int ai = -1;
    for (int i = 0; i < (int)m_objects.size(); ++i) if (m_objects[i].get() == anchor) { ai = i; break; }
    if (ai < 0) { m_objects.push_back(std::move(held)); return; }
    m_objects.insert(m_objects.begin() + (after ? ai + 1 : ai), std::move(held));
}

} // namespace okay
