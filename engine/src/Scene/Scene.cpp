#include "okay/Scene/Scene.hpp"
#include "okay/Inventory/InventoryPersistence.hpp"
#include "okay/VisualScript/VsVariables.hpp"
#include "okay/Scripting/NextScriptVM.hpp"
#include "okay/Scene/EnsureCamera.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Components/ActionList.hpp"   // per-scene variable pools follow this scene
#include "okay/Components/Camera.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/UIAnchor.hpp"        // UICanvas::Width/Height (viewport)
#include "okay/Physics/Collider2D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Physics3D.hpp"        // camera-ray picking for 3D mouse events
#include "okay/Physics/ColliderFit.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Render/Renderer.hpp"
#include "okay/Core/Profiler.hpp"
#include "okay/Net/NetTransport.hpp"   // per-frame pump for non-native transports
#include <algorithm>
#include <atomic>
#include <functional>
#include <limits>
#include <vector>

namespace okay {

Scene::Scene(std::string name) : m_name(std::move(name)) {
    static std::atomic<std::uint64_t> s_nextUid{1};
    m_uid = s_nextUid.fetch_add(1);
}
Scene::~Scene() {   // its scene variables and script statics go with it
    vs::ForgetSceneVariables(this);
    NextScriptVM::ForgetSceneStatics(this);
    ActionList::ForgetScene(this);
}

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
    ++m_structureVersion;
    ptr->transform = ptr->AddComponent<Transform>();
    return ptr;
}

void Scene::Clear() {
    vs::ForgetSceneVariables(this);   // a fresh scene (another level loaded) starts with none
    NextScriptVM::ForgetSceneStatics(this);   // ... and script statics start again
    ActionList::ForgetScene(this);    // action-list variables too — they belong to this scene
    m_pending.clear();
    m_active.clear();
    m_destroyQueue.clear();
    m_scheduler.Clear();
    m_tweens.Clear();
    m_physics.Clear();
    mainCamera = nullptr;
    // The pointer-dispatch state points at objects about to be freed: forget it,
    // or the next Update sends OnMouseExit/Up to freed memory (the 2.813.0 crash
    // when Play restarted or a level loaded while the cursor was over an object).
    m_mouseHover = nullptr;
    m_mousePress = nullptr;
    m_mouseWasDown = false;
    m_objects.clear();
    m_graveyard.clear();
    ++m_structureVersion;
    renderSettings = RenderSettings{};   // back to default sky/ambient
}

void Scene::QueuePending(Component* component) {
    ++m_componentVersion;
    m_pending.push_back(component);
}

void Scene::NotifyComponentRemoved(Component* component) {
    // Null the slot instead of erasing, so removing a component from inside an
    // m_active iteration (a component's Update/Render) can't invalidate the loop
    // or shift elements. Null slots are skipped by every loop and compacted
    // between frames.
    ++m_componentVersion;
    for (auto& c : m_active)  if (c == component) c = nullptr;
    for (auto& c : m_pending) if (c == component) c = nullptr;
    if (static_cast<Component*>(mainCamera) == component) mainCamera = nullptr;
    for (auto* w : m_removalWatches) w->push_back(component);
}

void Scene::FlushPending() {
    if (m_pending.empty()) return;
    // Awake the whole batch first, then Start it.
    std::vector<Component*> batch;
    batch.swap(m_pending);

    bool ordered = false;
    for (Component* c : batch) {
        if (!c) continue;                 // removed before it was flushed
        c->Awake();
        m_active.push_back(c);
        if (c->ExecutionOrder() != 0) ordered = true;
    }
    // Keep components with an execution order where they belong (IK after
    // animation); creation order holds among equals.
    if (ordered || m_hasOrdered) {
        m_hasOrdered = true;
        std::stable_sort(m_active.begin(), m_active.end(), [](const Component* a, const Component* b) {
            return (a ? a->ExecutionOrder() : 0) < (b ? b->ExecutionOrder() : 0);
        });
    }
    for (Component* c : batch) {
        if (c && !c->m_started && c->enabled) {
            c->Start();
            c->m_started = true;
        }
    }
}

void Scene::Start() {
    ActionList::UseScene(this);
    FlushPending();
}

void Scene::Update(float deltaTime) {
    ActionList::UseScene(this);
    FlushPending(); // adopt anything created since last frame
    // Drop any components removed last frame (their slots were nulled).
    m_active.erase(std::remove(m_active.begin(), m_active.end(), nullptr), m_active.end());
    DispatchActivation();

    // Physics debug view: this frame's rays / contacts replace last frame's.
    m_physics.BeginDebugFrame();
    m_physics3d.BeginDebugFrame();

    m_scheduler.Update(deltaTime);
    {   // Tweens set to ignore the time scale use the real frame time.
        const float ts = Time::TimeScale();
        m_tweens.Update(deltaTime, ts > 1e-6f ? deltaTime / ts : Time::UnscaledDeltaTime());
    }

    // Pump a non-native realtime transport (e.g. Photon needs its client
    // serviced every frame). Native is a no-op — the NetworkManager component
    // ticks itself — and when no transport was ever selected this skips
    // entirely, so there's no cost in the common case.
    if (NetTransport::Exists()) NetTransport::Get().Update();

    {
        OKAY_PROFILE("Scripts.Update");
        for (Component* c : m_active) {
            if (c && c->enabled && c->gameObject && c->gameObject->active)
                c->Update(deltaTime);
        }
    }

    DispatchPointer();   // OnMouseEnter/Exit/Over/Down/Up/Click against the cursor

    // Keep auto-fit colliders matched to their renderer before the physics step.
    for (const auto& go : m_objects)
        if (go->active && go->Components().size() > 1) FitColliders(go.get(), /*onlyAuto*/ true);

    if (physicsEnabled) {
        { OKAY_PROFILE("Physics.2D"); m_physics.Step(*this, deltaTime); }
        { OKAY_PROFILE("Physics.3D"); m_physics3d.Step(*this, deltaTime); }
    }

    {
        OKAY_PROFILE("Scripts.LateUpdate");
        for (Component* c : m_active) {
            if (c && c->enabled && c->gameObject && c->gameObject->active)
                c->LateUpdate(deltaTime);
        }
    }

    FlushDestroyed();
    ResolveMainCamera();   // follow cameras being switched on/off this frame

    // A deferred scene load (requested via RequestLoad) happens here, after all
    // iteration is done, so it's safe to replace the whole scene.
    if (m_hasPendingLoad) {
        std::string path = m_pendingLoad;
        m_hasPendingLoad = false;
        m_pendingLoad.clear();
        m_pendingMerges.clear();   // a full load supersedes queued merges
        InventoryPersistence::BeforeSceneLoad(*this);   // bags marked keepOnSceneLoad come along
        if (SceneSerializer::LoadFromFile(*this, path)) {
            InventoryPersistence::AfterSceneLoad(*this);
            Start(); // run Awake/Start for the freshly loaded objects
        }
    }

    // A save game being loaded (RequestLoadText): same as a file load, then the
    // loader puts back what the scene text doesn't hold.
    if (m_hasPendingText) {
        std::string text = std::move(m_pendingText);
        auto after = std::move(m_pendingTextAfter);
        m_hasPendingText = false;
        m_pendingText.clear(); m_pendingTextAfter = {};
        m_pendingMerges.clear();
        if (SceneSerializer::Deserialize(*this, text)) {
            Start();
            if (after) after(*this);
        }
    }

    // Deferred additive merges (RequestMerge): fold another scene's objects into this
    // one, then Start() — which only Awakes/Starts the newly added components.
    if (!m_pendingMerges.empty()) {
        auto merges = std::move(m_pendingMerges);
        m_pendingMerges.clear();
        bool any = false;
        for (auto& m : merges)
            if (SceneSerializer::MergeFromFile(*this, m.first, m.second)) any = true;
        if (any) Start();
    }
}

void Scene::DispatchPointer() {
    const float w = UICanvas::Width(), h = UICanvas::Height();
    if (w < 1.0f || h < 1.0f) return;

    // Cursor -> world XY using the same 2D projection the player/editor draw with.
    float ortho = mainCamera ? mainCamera->EffectiveOrthoSize() : 5.0f;
    Vec3 camPos = (mainCamera && mainCamera->transform) ? mainCamera->RenderPosition() : Vec3::Zero;
    float scale = h / (2.0f * ortho);
    if (scale < 1e-6f) scale = 1.0f;
    Vec2 mp = Input::MousePosition();
    Vec3 world{camPos.x + (mp.x - w * 0.5f) / scale,
               camPos.y + (h * 0.5f - mp.y) / scale, 0.0f};

    // Topmost object under the cursor. In a 3D (perspective) scene we cast a real
    // camera ray against 3D colliders so OnMouse* fires on meshes; in
    // a 2D (orthographic) scene we pick by SpriteRenderer / BoxCollider2D bounds.
    GameObject* hit = nullptr;
    if (mainCamera && mainCamera->projection == Camera::Projection::Perspective) {
        Vec3 ro, rd;
        mainCamera->ScreenPointToRay(mp.x, mp.y, w, h, ro, rd);
        Physics3D phys;
        RaycastHit3D rh = phys.Raycast(*this, ro, rd);
        hit = rh.gameObject;   // needs a Collider3D on the object (primitives get one by default)
    } else {
        // A SpriteRenderer's size or, failing that, a BoxCollider2D defines the bounds.
        int best = std::numeric_limits<int>::min();
        for (const auto& up : m_objects) {
            GameObject* go = up.get();
            if (!go->active || !go->transform) continue;
            Vec3 c = go->transform->Position();
            Vec3 ls = go->transform->LossyScale();
            Vec2 half; int order = 0;
            if (auto* sr = go->GetComponent<SpriteRenderer>()) {
                half = {sr->size.x * ls.x * 0.5f, sr->size.y * ls.y * 0.5f};
                order = sr->sortOrder;
            } else if (auto* bc = go->GetComponent<BoxCollider2D>()) {
                half = {bc->size.x * ls.x * 0.5f, bc->size.y * ls.y * 0.5f};
            } else {
                continue;
            }
            if (world.x >= c.x - half.x && world.x <= c.x + half.x &&
                world.y >= c.y - half.y && world.y <= c.y + half.y && order >= best) {
                best = order; hit = go;
            }
        }
    }

    // Backstop: never message an object that's no longer in the scene, however it
    // left (Clear and FlushDestroyed reset these; this catches any other path).
    auto inScene = [this](GameObject* go) {
        for (const auto& up : m_objects) if (up.get() == go) return true;
        return false;
    };
    if (m_mouseHover && !inScene(m_mouseHover)) m_mouseHover = nullptr;
    if (m_mousePress && !inScene(m_mousePress)) m_mousePress = nullptr;

    auto send = [](GameObject* go, void (Component::*fn)()) {
        if (!go) return;
        for (Component* c : go->GetComponents<Component>())
            if (c && c->enabled) (c->*fn)();
    };

    // Enter / exit as the hovered object changes.
    if (hit != m_mouseHover) {
        send(m_mouseHover, &Component::OnMouseExit);
        send(hit,          &Component::OnMouseEnter);
        m_mouseHover = hit;
    }
    if (hit) send(hit, &Component::OnMouseOver);

    // Press / release / click on the left button.
    bool down = Input::GetMouseButton(0);
    if (down && !m_mouseWasDown) {            // press edge
        m_mousePress = hit;
        send(hit, &Component::OnMouseDown);
    } else if (!down && m_mouseWasDown) {     // release edge
        send(m_mousePress, &Component::OnMouseUp);
        if (m_mousePress && m_mousePress == hit)   // released on the same object = click
            send(hit, &Component::OnMouseClick);
        m_mousePress = nullptr;
    }
    m_mouseWasDown = down;
}

void Scene::Render(IRenderer& renderer) {
    ActionList::UseScene(this);
    Color clear = mainCamera ? mainCamera->backgroundColor : Color::Black;
    renderer.BeginFrame(clear);

    if (mainCamera && mainCamera->transform) {
        Viewport vp = renderer.GetViewport();
        renderer.SetCamera(mainCamera->RenderPosition(),
                           mainCamera->EffectiveOrthoSize(), vp.Aspect());
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
        // Don't queue the same object twice (a double Destroy would double-free it).
        if (std::find(m_destroyQueue.begin(), m_destroyQueue.end(), g) != m_destroyQueue.end()) return;
        m_destroyQueue.push_back(g);
        if (g->transform) {
            std::vector<Transform*> kids = g->transform->Children();   // copy: recursion-safe
            for (Transform* child : kids)
                if (child && child->gameObject) queue(child->gameObject);
        }
    };
    queue(go);
}

void Scene::ResolveMainCamera() {
    std::vector<Camera*> cams;
    for (Component* c : m_active)
        if (auto* cam = dynamic_cast<Camera*>(c)) cams.push_back(cam);
    mainCamera = PickRenderCamera(mainCamera, cams);
}

void Scene::DispatchActivation() {
    // Index loop: a handler may create objects (they are first seen next frame).
    const std::size_t n = m_objects.size();
    for (std::size_t i = 0; i < n && i < m_objects.size(); ++i) {
        GameObject* go = m_objects[i].get();
        if (!go) continue;
        const signed char now = go->active ? 1 : 0;
        if (go->activeSeen == now) continue;
        const bool first = go->activeSeen < 0;
        go->activeSeen = now;
        if (first) continue;   // its starting state, not a change
        std::vector<Component*> comps;
        for (const auto& c : go->Components()) comps.push_back(c.get());
        for (Component* c : comps) {
            if (!c || !c->enabled) continue;
            if (now) c->OnEnable(); else c->OnDisable();
        }
    }
}

void Scene::FlushDestroyed() {
    // OnDestroy handlers (scripts, visual scripts) may destroy or spawn more
    // objects: take the queue as a batch so they can't invalidate it, and treat
    // what they queued as the next batch (a few rounds; the rest waits a frame).
    for (int round = 0; round < 8 && !m_destroyQueue.empty(); ++round) {
    ++m_structureVersion;   // objects go away: name / bone caches re-resolve (only then, not every frame)
    std::vector<GameObject*> batch;
    batch.swap(m_destroyQueue);
    // Survivors get to drop references to what's going away (skinned meshes to
    // their bones, IK to its targets, ...) before anything is freed.
    const std::unordered_set<const GameObject*> dying(batch.begin(), batch.end());
    {
        std::vector<Component*> survivors;
        for (Component* c : m_active)  if (c && !dying.count(c->gameObject)) survivors.push_back(c);
        for (Component* c : m_pending) if (c && !dying.count(c->gameObject)) survivors.push_back(c);
        for (Component* c : survivors) c->OnObjectsDestroyed(dying);
    }
    m_tweens.OnObjectsDestroyed(dying);   // nothing keeps animating a freed object
    for (GameObject* go : batch) {
        // Run OnDestroy and drop the object's components from the lists.
        bool live = false;
        for (auto& obj : m_objects) if (obj.get() == go) { live = true; break; }
        if (live) {
            std::vector<Component*> own;   // copy: OnDestroy may add components / objects
            for (Component* c : m_active) if (c && c->gameObject == go) own.push_back(c);
            for (Component* c : own) c->OnDestroy();
        }
        auto isOwned = [go](Component* c) { return !c || c->gameObject == go; };
        m_active.erase(std::remove_if(m_active.begin(), m_active.end(), isOwned), m_active.end());
        m_pending.erase(std::remove_if(m_pending.begin(), m_pending.end(), isOwned), m_pending.end());
        if (mainCamera && mainCamera->gameObject == go) mainCamera = nullptr;
        if (m_mouseHover == go) m_mouseHover = nullptr;
        if (m_mousePress == go) m_mousePress = nullptr;
        // Keep the shell (GameObject + Transform) in the graveyard; free the rest.
        for (auto it = m_objects.begin(); it != m_objects.end(); ++it)
            if (it->get() == go) {
                // Unlink the shell from a surviving parent, or it lingers in the
                // Hierarchy as an empty, inactive child (a deleted camera showed up
                // as "a camera with no Camera component").
                if (Transform* t = go->transform)
                    if (Transform* par = t->Parent(); par && par->gameObject && !dying.count(par->gameObject))
                        t->SetParent(nullptr, /*worldPositionStays=*/true);
                (*it)->StripToTransform();
                m_graveyard.push_back(std::move(*it));
                m_objects.erase(it);
                break;
            }
    }
    ++m_structureVersion;   // again after the free: a query from OnDestroy may have cached the dying components
    }
    if (m_graveyard.size() > 20000) m_graveyard.erase(m_graveyard.begin(), m_graveyard.begin() + 10000);   // bullets etc.: oldest shells go
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
    // UI draw order follows the hierarchy (a parent's child list), so a parented
    // widget must move to the LAST sibling slot to come out on top. Root objects
    // (no parent) instead move to the end of the scene's object list.
    if (go && go->transform && go->transform->Parent()) {
        go->transform->Parent()->MoveChildToEdge(go->transform, /*toFront=*/false);  // last child = on top
        return;
    }
    for (std::size_t i = 0; i < m_objects.size(); ++i)
        if (m_objects[i].get() == go) {
            auto held = std::move(m_objects[i]);
            m_objects.erase(m_objects.begin() + i);
            m_objects.push_back(std::move(held));   // drawn last = on top
            ++m_structureVersion;   // root order changed (UI draws in this order)
            return;
        }
}

void Scene::MoveToBack(GameObject* go) {
    if (go && go->transform && go->transform->Parent()) {
        go->transform->Parent()->MoveChildToEdge(go->transform, /*toFront=*/true);   // first child = behind
        return;
    }
    for (std::size_t i = 0; i < m_objects.size(); ++i)
        if (m_objects[i].get() == go) {
            auto held = std::move(m_objects[i]);
            m_objects.erase(m_objects.begin() + i);
            m_objects.insert(m_objects.begin(), std::move(held));  // drawn first = behind
            ++m_structureVersion;   // root order changed (UI draws in this order)
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
    if (selfPos >= 0 && other >= 0 && other < (int)roots.size()) {
        std::swap(m_objects[roots[selfPos]], m_objects[roots[other]]);
        ++m_structureVersion;   // root order changed (UI draws in this order)
    }
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
            ++m_structureVersion;   // root order changed (UI draws in this order)
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
