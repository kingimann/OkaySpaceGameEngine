#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Render/Renderer.hpp"
#include <algorithm>

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
}

void Scene::QueuePending(Component* component) {
    m_pending.push_back(component);
}

void Scene::NotifyComponentRemoved(Component* component) {
    m_active.erase(std::remove(m_active.begin(), m_active.end(), component), m_active.end());
    m_pending.erase(std::remove(m_pending.begin(), m_pending.end(), component), m_pending.end());
    if (static_cast<Component*>(mainCamera) == component) mainCamera = nullptr;
}

void Scene::FlushPending() {
    if (m_pending.empty()) return;
    // Awake the whole batch first, then Start it (Unity message order).
    std::vector<Component*> batch;
    batch.swap(m_pending);

    for (Component* c : batch) {
        c->Awake();
        m_active.push_back(c);
    }
    for (Component* c : batch) {
        if (!c->m_started && c->enabled) {
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

    m_scheduler.Update(deltaTime);

    for (Component* c : m_active) {
        if (c->enabled && c->gameObject && c->gameObject->active)
            c->Update(deltaTime);
    }

    if (physicsEnabled) m_physics.Step(*this, deltaTime);

    for (Component* c : m_active) {
        if (c->enabled && c->gameObject && c->gameObject->active)
            c->LateUpdate(deltaTime);
    }

    if (!m_destroyQueue.empty()) {
        for (GameObject* go : m_destroyQueue) {
            // Run OnDestroy and drop the object's components from the lists.
            for (auto& obj : m_objects) {
                if (obj.get() != go) continue;
                for (Component* c : m_active)
                    if (c->gameObject == go) c->OnDestroy();
                break;
            }
            auto isOwned = [go](Component* c) { return c->gameObject == go; };
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
        if (c->enabled && c->gameObject && c->gameObject->active)
            c->OnRender(renderer);
    }

    renderer.EndFrame();
}

void Scene::Destroy(GameObject* go) {
    if (go) m_destroyQueue.push_back(go);
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

} // namespace okay
