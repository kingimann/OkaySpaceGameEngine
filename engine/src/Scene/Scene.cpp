#include "okay/Scene/Scene.hpp"
#include "okay/Inventory/InventoryPersistence.hpp"
#include "okay/VisualScript/VsVariables.hpp"
#include "okay/Scripting/NextScriptVM.hpp"
#include "okay/Scene/EnsureCamera.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scene/SceneLocalization.hpp"
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
#include "okay/Environment/Weather.hpp"   // rain / snow / storm rig following the camera
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include <cmath>
#include <algorithm>
#include <atomic>
#include <functional>
#include <iostream>
#include <limits>
#include <unordered_map>
#include <vector>

namespace okay {

Scene::Scene(std::string name) : m_name(std::move(name)) {
    static std::atomic<std::uint64_t> s_nextUid{1};
    m_uid = s_nextUid.fetch_add(1);
}
Scene::~Scene() {   // its scene variables and script statics go with it
    DestroyAllLive();   // OnDisable / OnDestroy for what is still alive
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

GameObject* Scene::FindByGuid(const std::string& guid) const {
    if (guid.empty()) return nullptr;
    for (const auto& o : m_objects)
        if (!o->IsDestroyed() && o->Guid() == guid) return o.get();
    return nullptr;
}

void Scene::DestroyAllLive() {
    // Components that were awakened get OnDisable (if owed) and OnDestroy once, like a
    // destroyed object's. Slots of components a handler removes are nulled in m_active
    // (never erased), so walking by index stays valid; anything a handler adds waits in
    // m_pending and is dropped with the rest. Cannot re-enter: the list is emptied first.
    m_pending.clear();   // never awakened: nothing to undo
    for (std::size_t i = 0; i < m_active.size(); ++i) {
        Component* c = m_active[i];
        if (!c || !c->gameObject) continue;
        c->DeliverDisableBeforeDestroy();
        if (i < m_active.size() && m_active[i] == c) c->OnDestroy();   // OnDisable may have removed it
    }
    m_active.clear();
    m_pending.clear();
}

void Scene::Clear() {
    DestroyAllLive();   // OnDestroy for what the old scene still had running
    vs::ForgetSceneVariables(this);   // a fresh scene (another level loaded) starts with none
    NextScriptVM::ForgetSceneStatics(this);   // ... and script statics start again
    ActionList::ForgetScene(this);    // action-list variables too — they belong to this scene
    std::vector<std::function<void(bool)>> abandoned;   // callbacks of loads this Clear cancels
    for (auto& r : m_loads) if (!r.finished && r.opts.onDone) abandoned.push_back(std::move(r.opts.onDone));
    m_loads.clear();
    m_pending.clear();
    m_active.clear();
    m_destroyQueue.clear();
    m_destroyQueued.clear();
    m_delayedDestroy.clear();
    m_dontDestroy.clear();
    m_scheduler.Clear();
    m_tweens.Clear();
    m_physics.Clear();
    m_physics3d.Clear();                       // stale contact pairs point at colliders about to be freed
    m_physics3d.gravity = Vec3{0.0f, -9.81f, 0.0f};   // gravity is a scene setting: a new scene starts at the default
    m_physicsAcc = 0.0f;
    uiFont.clear();                            // the loaded scene sets its own, if any
    mainCamera = nullptr;
    ResetLayerCollisions();   // the layer matrix belongs to the scene being cleared
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
    for (auto& d : abandoned) d(false);
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
    for (auto& r : m_loads) for (auto& c : r.held) if (c == component) c = nullptr;
    if (m_flushBatch) for (auto& c : *m_flushBatch) if (c == component) c = nullptr;   // the batch FlushPending is walking
    if (static_cast<Component*>(mainCamera) == component) mainCamera = nullptr;
    for (auto* w : m_removalWatches) w->push_back(component);
}

namespace {
// What Update runs a component on: enabled, on an active GameObject.
bool CanRun(const Component* c) {
    return c->enabled && (!c->gameObject || c->gameObject->ActiveInHierarchy());
}
} // namespace

void Scene::FlushPending() {
    if (m_pending.empty()) return;
    // Awake the whole batch first, then Start it.
    std::vector<Component*> batch;
    batch.swap(m_pending);
    // While this batch is walked, a component removed by another's Awake / Start has its
    // entry nulled here (NotifyComponentRemoved), so it is never touched after it is freed.
    struct BatchGuard {
        std::vector<Component*>*& slot; std::vector<Component*>* prev;
        BatchGuard(std::vector<Component*>*& s, std::vector<Component*>* b) : slot(s), prev(s) { slot = b; }
        ~BatchGuard() { slot = prev; }
    } guard(m_flushBatch, &batch);

    bool ordered = false;
    for (std::size_t i = 0; i < batch.size(); ++i) {
        Component* c = batch[i];
        if (!c) continue;                 // removed before it was flushed
        c->Awake();
        if (!batch[i]) continue;          // ... or by its own Awake
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
    for (std::size_t i = 0; i < batch.size(); ++i) {
        Component* c = batch[i];          // re-read each time: a Start may have removed a later one
        if (c && !c->m_started && CanRun(c)) StartComponent(c);
    }
    // A component that is off now (disabled, or on an inactive object) is started by
    // Update the first frame it can run.
}

void Scene::StartComponent(Component* c) {
    if (!c->m_live) { c->m_live = true; c->OnEnable(); }   // OnEnable, then the first Start
    c->Start();
    c->m_started = true;
}

void Scene::Start() {
    ActionList::UseScene(this);
    FlushPending();
}

void Scene::DetachKept(KeptState& k) {
    std::vector<Transform*> stack;
    for (GameObject* g : m_dontDestroy) if (g && g->transform) stack.push_back(g->transform);
    while (!stack.empty()) {
        Transform* t = stack.back(); stack.pop_back();
        if (!t->gameObject || !k.keep.insert(t->gameObject).second) continue;
        for (Transform* ch : t->Children()) stack.push_back(ch);
    }
    k.marked = m_dontDestroy;
    if (k.keep.empty()) return;
    for (const auto& up : m_objects) {   // a kept root must not hang off what is about to be freed
        GameObject* g = up.get();
        Transform* par = g->transform ? g->transform->Parent() : nullptr;
        if (k.keep.count(g) && par && par->gameObject && !k.keep.count(par->gameObject))
            g->transform->SetParent(nullptr, /*worldPositionStays=*/true);
    }
    for (auto it = m_objects.begin(); it != m_objects.end();) {
        if (k.keep.count(it->get())) { k.objects.push_back(std::move(*it)); it = m_objects.erase(it); }
        else ++it;
    }
    auto owned = [&](Component* c) { return c && c->gameObject && k.keep.count(c->gameObject); };
    for (Component* c : m_active)  if (owned(c)) k.active.push_back(c);
    for (Component* c : m_pending) if (owned(c)) k.pending.push_back(c);
    m_active.erase(std::remove_if(m_active.begin(), m_active.end(), owned), m_active.end());
    m_pending.erase(std::remove_if(m_pending.begin(), m_pending.end(), owned), m_pending.end());
    if (mainCamera && k.keep.count(mainCamera->gameObject)) mainCamera = nullptr;
    if (m_mouseHover && k.keep.count(m_mouseHover)) m_mouseHover = nullptr;
    if (m_mousePress && k.keep.count(m_mousePress)) m_mousePress = nullptr;
    ++m_structureVersion;
}

void Scene::ReattachKept(KeptState& k) {
    if (k.objects.empty()) return;
    for (auto& up : k.objects) m_objects.push_back(std::move(up));
    m_dontDestroy = std::move(k.marked);
    m_active.insert(m_active.end(), k.active.begin(), k.active.end());
    m_pending.insert(m_pending.end(), k.pending.begin(), k.pending.end());
    if (m_hasOrdered)
        std::stable_sort(m_active.begin(), m_active.end(), [](const Component* a, const Component* b) {
            return (a ? a->ExecutionOrder() : 0) < (b ? b->ExecutionOrder() : 0);
        });
    ++m_structureVersion;
}

void Scene::Update(float deltaTime) {
    ActionList::UseScene(this);
    FlushPending(); // adopt anything created since last frame
    // Drop any components removed last frame (their slots were nulled).
    m_active.erase(std::remove(m_active.begin(), m_active.end(), nullptr), m_active.end());
    DispatchActivation();
    LocalizeScene(*this);   // localized text follows the language (a no-op when nothing changed)

    // Physics debug view: this frame's rays / contacts replace last frame's.
    m_physics.BeginDebugFrame();
    m_physics3d.BeginDebugFrame();

    m_scheduler.Update(deltaTime);
    UpdateWeather(*this, deltaTime);   // the scene's weather preset -> emitters, wind, lightning
    {   // Tweens set to ignore the time scale use the real frame time.
        m_tweens.Update(deltaTime, Time::UnscaledFor(deltaTime));   // exact even in a frame where a hit stop ends or a ramp runs
    }

    // Pump a non-native realtime transport (e.g. Photon needs its client
    // serviced every frame). Native is a no-op — the NetworkManager component
    // ticks itself — and when no transport was ever selected this skips
    // entirely, so there's no cost in the common case.
    if (NetTransport::Exists()) NetTransport::Get().Update();

    {
        OKAY_PROFILE("Scripts.Update");
        for (Component* c : m_active) {
            if (c && c->enabled && c->gameObject && c->gameObject->ActiveInHierarchy()) {
                if (!c->m_started) {   // switched on since it joined: its Start comes first
                    StartComponent(c);
                    if (!CanRun(c)) continue;
                }
                c->Update(deltaTime);
            }
        }
    }
    TickDelayedDestroys(deltaTime);

    DispatchPointer();   // OnMouseEnter/Exit/Over/Down/Up/Click against the cursor

    // Keep auto-fit colliders matched to their renderer before the physics step.
    for (const auto& go : m_objects)
        if (go->Components().size() > 1 && go->ActiveInHierarchy()) FitColliders(go.get(), /*onlyAuto*/ true);

    if (physicsEnabled) StepPhysics(deltaTime);

    {
        OKAY_PROFILE("Scripts.LateUpdate");
        for (Component* c : m_active) {
            if (c && c->m_started && c->enabled && c->gameObject && c->gameObject->ActiveInHierarchy())
                c->LateUpdate(deltaTime);
        }
    }

    FlushDestroyed();
    ResolveMainCamera();   // follow cameras being switched on/off this frame
    Scheduler::TickGlobal(this, deltaTime);   // process-wide timers: once per frame, however many scenes update

    // A deferred scene load (requested via RequestLoad) happens here, after all
    // iteration is done, so it's safe to replace the whole scene.
    if (m_hasPendingLoad) {
        std::string path = m_pendingLoad;
        auto done = std::move(m_pendingLoadDone);
        m_hasPendingLoad = false;
        m_pendingLoad.clear();
        m_pendingLoadDone = {};
        m_pendingMerges.clear();   // a full load supersedes queued merges
        InventoryPersistence::BeforeSceneLoad(*this);   // bags marked keepOnSceneLoad come along

        // Objects marked DontDestroyOnLoad (and their children) are taken out before the
        // old scene is cleared and put back after, as the same objects with the same ids.
        KeptState kept;
        DetachKept(kept);
        std::string loadError;
        const bool loaded = SceneSerializer::LoadFromFile(*this, path, &loadError);
        if (loaded) m_lastLoadError.clear();
        else {
            m_lastLoadError = "could not load scene '" + path + "': " + (loadError.empty() ? "unknown error" : loadError);
            std::cerr << "[scene] " << m_lastLoadError << "\n";
        }
        ReattachKept(kept);   // back in, whether or not the load worked
        if (loaded) {
            InventoryPersistence::AfterSceneLoad(*this);
            Start(); // run Awake/Start for the freshly loaded objects
        }
        if (!kept.keep.empty()) ResolveMainCamera();   // a kept camera may be the only one
        if (done) done(loaded);
    }

    // A save game being loaded (RequestLoadText): same as a file load, then the
    // loader puts back what the scene text doesn't hold.
    if (m_hasPendingText) {
        std::string text = std::move(m_pendingText);
        auto after = std::move(m_pendingTextAfter);
        m_hasPendingText = false;
        m_pendingText.clear(); m_pendingTextAfter = {};
        m_pendingMerges.clear();
        std::string textError;
        if (SceneSerializer::Deserialize(*this, text, &textError)) {
            m_lastLoadError.clear();
            Start();
            if (after) after(*this);
        } else {
            m_lastLoadError = "could not load scene text: " + (textError.empty() ? std::string("unknown error") : textError);
            std::cerr << "[scene] " << m_lastLoadError << "\n";
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

    StepAsyncLoads();
}

namespace {

// Render interpolation: a body is drawn between its last two physics poses.
bool SamePose(Transform* t, const Vec3& p, const Quat& r) {
    return (t->Position() - p).SqrMagnitude() < 1e-10f && std::fabs(Quat::Dot(t->Rotation(), r)) > 1.0f - 1e-6f;
}
// Before stepping: put the body back at its real physics pose (it was drawn at an
// in-between one). If game code moved it since, it was teleported: keep that.
void RestorePose(Transform* t, PoseHistory& h) {
    if (!h.valid) return;
    if (SamePose(t, h.shownPos, h.shownRot)) { t->SetPosition(h.curPos); t->SetRotation(h.curRot); }
    else h.valid = false;
}
void BeginStep(Transform* t, PoseHistory& h) {
    h.prevPos = t->Position();
    h.prevRot = t->Rotation();
}
void ShowPose(Transform* t, PoseHistory& h, bool stepped, float alpha) {
    if (!h.valid && !stepped) return;            // not simulated yet: draw where it is
    if (!h.valid) { h.valid = true; }
    h.curPos = t->Position();
    h.curRot = t->Rotation();
    h.shownPos = Vec3::LerpUnclamped(h.prevPos, h.curPos, alpha);
    h.shownRot = Quat::Slerp(h.prevRot, h.curRot, alpha);
    t->SetPosition(h.shownPos);
    t->SetRotation(h.shownRot);
}

} // namespace

void Scene::RunFixedUpdate(float step) {
    // Index loop: a FixedUpdate may remove components (slots are nulled) or add them (they wait in m_pending).
    for (std::size_t i = 0; i < m_active.size(); ++i) {
        Component* c = m_active[i];
        if (c && c->m_started && c->enabled && c->gameObject && c->gameObject->ActiveInHierarchy())
            c->FixedUpdate(step);
    }
}

void Scene::StepPhysics(float deltaTime) {
    const float step = fixedTimeStep;
    if (step <= 0.0f) {   // one step per frame, sized to the frame
        RunFixedUpdate(deltaTime);
        { OKAY_PROFILE("Physics.2D"); m_physics.Step(*this, deltaTime); }
        { OKAY_PROFILE("Physics.3D"); m_physics3d.Step(*this, deltaTime); }
        m_physicsSteps = 1; m_physicsAlpha = 0.0f;
        return;
    }
    const auto bodies3 = FindObjectsOfType<Rigidbody3D>();
    const auto bodies2 = FindObjectsOfType<Rigidbody2D>();
    if (interpolatePhysics) {
        for (Rigidbody3D* rb : bodies3) if (rb->transform) RestorePose(rb->transform, rb->poseHistory);
        for (Rigidbody2D* rb : bodies2) if (rb->transform) RestorePose(rb->transform, rb->poseHistory);
    }
    m_physicsAcc = std::min(m_physicsAcc + std::max(0.0f, deltaTime), std::max(step, maxPhysicsCatchUp));
    m_physicsSteps = 0;
    while (m_physicsAcc >= step) {
        if (interpolatePhysics) {
            for (Rigidbody3D* rb : bodies3) if (rb->transform) BeginStep(rb->transform, rb->poseHistory);
            for (Rigidbody2D* rb : bodies2) if (rb->transform) BeginStep(rb->transform, rb->poseHistory);
        }
        RunFixedUpdate(step);
        { OKAY_PROFILE("Physics.2D"); m_physics.Step(*this, step); }
        { OKAY_PROFILE("Physics.3D"); m_physics3d.Step(*this, step); }
        m_physicsAcc -= step;
        ++m_physicsSteps;
    }
    m_physicsAlpha = m_physicsAcc / step;
    if (interpolatePhysics) {
        const bool stepped = m_physicsSteps > 0;
        for (Rigidbody3D* rb : bodies3) if (rb->transform) ShowPose(rb->transform, rb->poseHistory, stepped, m_physicsAlpha);
        for (Rigidbody2D* rb : bodies2) if (rb->transform) ShowPose(rb->transform, rb->poseHistory, stepped, m_physicsAlpha);
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
            if (!go->transform || !go->ActiveInHierarchy()) continue;
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
        if (c && c->enabled && c->gameObject && c->gameObject->ActiveInHierarchy())
            c->OnRender(renderer);
    }

    renderer.EndFrame();
}

void Scene::Destroy(GameObject* go) {
    if (!go) return;
    // The object stays in its parent's child list, so its world pose holds and
    // OnDestroy sees its parent. FlushDestroyed detaches it from a surviving parent
    // when it frees the object, so no child list is left with a dangling pointer
    // (deleting a child UI widget, hierarchy node, etc. — the #1 editor delete crash).
    // Queue the whole subtree: a child outliving its parent would itself be left
    // with a dangling parent Transform pointer.
    std::vector<GameObject*> stack{go};   // iterative: a deep hierarchy cannot overflow the stack
    while (!stack.empty()) {
        GameObject* g = stack.back(); stack.pop_back();
        if (!g) continue;
        // Don't queue the same object twice (a double Destroy would double-free it).
        if (!m_destroyQueued.insert(g).second) continue;
        m_destroyQueue.push_back(g);
        if (g->transform) {
            const auto& kids = g->transform->Children();
            for (std::size_t i = kids.size(); i-- > 0;)   // reversed: popped in sibling order
                if (kids[i] && kids[i]->gameObject) stack.push_back(kids[i]->gameObject);
        }
    }
}

void Scene::Destroy(GameObject* go, float seconds) {
    if (!go) return;
    if (seconds <= 0.0f) { Destroy(go); return; }
    for (auto& d : m_delayedDestroy)
        if (d.first == go->InstanceId()) { d.second = std::min(d.second, seconds); return; }
    m_delayedDestroy.push_back({go->InstanceId(), seconds});
}

void Scene::TickDelayedDestroys(float deltaTime) {
    if (m_delayedDestroy.empty()) return;
    auto due = std::move(m_delayedDestroy);
    m_delayedDestroy.clear();
    for (auto& d : due) {
        GameObject* go = GameObject::FromInstanceId(d.first);   // null once it is gone
        if (!go || go->scene() != this) continue;
        d.second -= deltaTime;
        if (d.second <= 0.0f) Destroy(go);
        else m_delayedDestroy.push_back(d);
    }
}

void Scene::DontDestroyOnLoad(GameObject* go) {
    if (go && !IsDontDestroyOnLoad(go)) m_dontDestroy.push_back(go);
}

bool Scene::IsDontDestroyOnLoad(const GameObject* go) const {
    return std::find(m_dontDestroy.begin(), m_dontDestroy.end(), go) != m_dontDestroy.end();
}

void Scene::ResolveMainCamera() {
    std::vector<Camera*> cams;
    for (Component* c : m_active)
        if (auto* cam = dynamic_cast<Camera*>(c)) cams.push_back(cam);
    mainCamera = PickRenderCamera(mainCamera, cams);
}

void Scene::DispatchActivation() {
    // OnEnable / OnDisable as a started component stops or starts being live (its
    // `enabled` or its object's `active` flipped). A component that never started gets
    // its OnEnable from StartComponent instead. Index loop: a handler may add or
    // remove components (removed slots are nulled, new ones wait in m_pending).
    for (std::size_t i = 0; i < m_active.size(); ++i) {
        Component* c = m_active[i];
        if (!c || !c->m_started) continue;
        const bool live = CanRun(c);
        if (live == c->m_live) continue;
        c->m_live = live;
        if (live) c->OnEnable(); else c->OnDisable();
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
    m_destroyQueued.clear();   // what OnDestroy handlers queue from here on is the next batch
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

    // Which of the batch are still in this scene (an object queued twice across rounds is not).
    std::unordered_set<const GameObject*> inScene;
    inScene.reserve(batch.size() * 2);
    for (const auto& obj : m_objects) if (dying.count(obj.get())) inScene.insert(obj.get());

    // OnDestroy for every live component of the dying objects, found in ONE pass over the
    // active list (not one scan per object). A handler may remove other components: the
    // watch skips those instead of touching freed memory.
    {
        RemovalWatch watch(*this);
        std::unordered_map<const GameObject*, std::vector<Component*>> own;
        for (Component* c : m_active)
            if (c && inScene.count(c->gameObject)) own[c->gameObject].push_back(c);
        for (GameObject* go : batch) {
            auto it = own.find(go);
            if (it == own.end()) continue;
            for (Component* c : it->second) {
                if (watch.Any() && watch.Gone(c)) continue;
                c->DeliverDisableBeforeDestroy();
                if (watch.Any() && watch.Gone(c)) continue;
                c->OnDestroy();
            }
        }
    }
    // Drop the dying objects' components from the lists, one pass each.
    auto isOwned = [&dying](Component* c) { return !c || dying.count(c->gameObject) != 0; };
    m_active.erase(std::remove_if(m_active.begin(), m_active.end(), isOwned), m_active.end());
    m_pending.erase(std::remove_if(m_pending.begin(), m_pending.end(), isOwned), m_pending.end());
    for (auto& r : m_loads) r.held.erase(std::remove_if(r.held.begin(), r.held.end(), isOwned), r.held.end());
    if (mainCamera && dying.count(mainCamera->gameObject)) mainCamera = nullptr;
    if (m_mouseHover && dying.count(m_mouseHover)) m_mouseHover = nullptr;
    if (m_mousePress && dying.count(m_mousePress)) m_mousePress = nullptr;
    m_dontDestroy.erase(std::remove_if(m_dontDestroy.begin(), m_dontDestroy.end(),
        [&dying](GameObject* g) { return dying.count(g) != 0; }), m_dontDestroy.end());
    // Keep the shells (GameObject + Transform) in the graveyard; free the rest. One pass
    // that keeps the survivors in order.
    {
        std::vector<std::unique_ptr<GameObject>> kept;
        kept.reserve(m_objects.size());
        for (auto& obj : m_objects) {
            GameObject* go = obj.get();
            if (!inScene.count(go)) { kept.push_back(std::move(obj)); continue; }
            // Unlink the shell from a surviving parent, or it lingers in the
            // Hierarchy as an empty, inactive child (a deleted camera showed up
            // as "a camera with no Camera component").
            // A dying parent's shell is detached from too, so a shell freed from the
            // graveyard never leaves a child pointing at it.
            if (Transform* t = go->transform)
                if (t->Parent()) t->SetParent(nullptr, /*worldPositionStays=*/true);
            go->StripToTransform();
            m_graveyard.push_back(std::move(obj));
        }
        m_objects.swap(kept);
    }
    ++m_structureVersion;   // again after the free: a query from OnDestroy may have cached the dying components
    }
    if (m_graveyard.size() > 20000) {   // bullets etc.: oldest shells go
        // Anything still parented to a shell about to be freed (adopted after it was
        // destroyed) is detached first, keeping its pose, or it would point at freed memory.
        for (auto it = m_graveyard.begin(); it != m_graveyard.begin() + 10000; ++it) {
            Transform* t = (*it)->transform;
            if (!t) continue;
            if (t->Parent()) t->SetParent(nullptr, /*worldPositionStays=*/true);
            std::vector<Transform*> kids = t->Children();
            for (Transform* kid : kids) kid->SetParent(nullptr, /*worldPositionStays=*/true);
        }
        m_graveyard.erase(m_graveyard.begin(), m_graveyard.begin() + 10000);
    }
}

GameObject* Scene::Find(const std::string& name) const {
    for (const auto& o : m_objects)
        if (o->name == name) return o.get();
    if (name.find('/') != std::string::npos) return FindByPath(name);   // "Parent/Child"
    return nullptr;
}

GameObject* Scene::FindByPath(const std::string& path) const {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t slash = path.find('/', start);
        std::string part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (!part.empty()) parts.push_back(std::move(part));   // skips a leading / doubled '/'
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    if (parts.empty()) return nullptr;
    GameObject* cur = nullptr;
    for (const auto& o : m_objects)
        if (o->name == parts[0] && (!o->transform || !o->transform->Parent())) { cur = o.get(); break; }
    for (std::size_t i = 1; cur && i < parts.size(); ++i) {
        GameObject* next = nullptr;
        if (cur->transform)
            for (const Transform* ch : cur->transform->Children())
                if (ch && ch->gameObject && ch->gameObject->name == parts[i]) { next = ch->gameObject; break; }
        cur = next;
    }
    return cur;
}

std::vector<GameObject*> Scene::Roots() const {
    std::vector<GameObject*> out;
    for (const auto& o : m_objects)
        if (!o->transform || !o->transform->Parent()) out.push_back(o.get());
    return out;
}

SceneValidationReport Scene::Validate(const std::string& assetRoot) const {
    return SceneSerializer::Validate(SceneSerializer::Serialize(*this), assetRoot);
}

GameObject* Scene::FindWithTag(const std::string& tag) const {
    if (tag.empty()) return nullptr;   // "no tag" is not a tag: every untagged object would match
    auto it = TagIndex().find(tag);
    return it == TagIndex().end() || it->second.empty() ? nullptr : it->second.front();
}

std::vector<GameObject*> Scene::FindGameObjectsWithTag(const std::string& tag) const {
    if (tag.empty()) return {};
    auto it = TagIndex().find(tag);
    return it == TagIndex().end() ? std::vector<GameObject*>{} : it->second;
}

const std::unordered_map<std::string, std::vector<GameObject*>>& Scene::TagIndex() const {
    const std::uint64_t tv = TagString::Version();
    if (m_tagIndexStructure != m_structureVersion || m_tagIndexTags != tv || m_tagIndexCount != m_objects.size()) {
        m_tagIndex.clear();
        for (const auto& o : m_objects)
            if (!o->tag.empty()) m_tagIndex[o->tag].push_back(o.get());
        m_tagIndexStructure = m_structureVersion; m_tagIndexTags = tv; m_tagIndexCount = m_objects.size();
    }
    return m_tagIndex;
}

void Scene::SetLayerCollision(int a, int b, bool enabled) {
    m_physics.SetLayerCollision(a, b, enabled);
    m_physics3d.SetLayerCollision(a, b, enabled);
}

void Scene::ResetLayerCollisions() {
    m_physics.ResetLayerCollisions();
    m_physics3d.ResetLayerCollisions();
}

bool Scene::HasCustomLayerCollisions() const {
    for (int i = 0; i < 32; ++i) if (m_physics.CollisionMask(i) != 0xFFFFFFFFu) return true;
    return false;
}

std::vector<Camera*> Scene::CameraStack() const {
    std::vector<Camera*> cams;
    for (const Component* c : m_active)
        if (auto* cam = dynamic_cast<Camera*>(const_cast<Component*>(c)))
            if (CameraUsable(cam)) cams.push_back(cam);
    std::stable_sort(cams.begin(), cams.end(), [](const Camera* a, const Camera* b) { return a->depth < b->depth; });
    return cams;
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

// ---- Incremental (multi-frame) scene loads ----------------------------------

int Scene::BeginAsyncLoad(const std::string& path, AsyncLoadOptions opts) {
    std::string err;
    auto job = SceneLoadJob::OpenFile(path, opts.additive, opts.offset, &err);
    if (!job) {
        if (opts.onDone) opts.onDone(false);
        return 0;
    }
    LoadRecord r;
    r.handle = m_nextLoadHandle++;
    r.job = std::move(job);
    r.opts = std::move(opts);
    if (r.opts.objectsPerFrame < 1) r.opts.objectsPerFrame = 1;
    m_loads.push_back(std::move(r));
    return m_loads.back().handle;
}

Scene::LoadRecord* Scene::FindLoad(int handle) {
    for (auto& r : m_loads) if (r.handle == handle) return &r;
    return nullptr;
}
const Scene::LoadRecord* Scene::FindLoad(int handle) const {
    for (const auto& r : m_loads) if (r.handle == handle) return &r;
    return nullptr;
}

bool Scene::IsLoading(int handle) const {
    const LoadRecord* r = FindLoad(handle);
    return r && !r->finished;
}
bool Scene::IsLoaded(int handle) const {
    const LoadRecord* r = FindLoad(handle);
    return r && r->finished && r->ok;
}
bool Scene::AnyLoading() const {
    for (const auto& r : m_loads) if (!r.finished) return true;
    return false;
}
float Scene::LoadProgress(int handle) const {
    const LoadRecord* r = FindLoad(handle);
    if (!r) return 0.0f;
    if (r->finished) return r->ok ? 1.0f : 0.0f;
    return r->job->Progress();
}

void Scene::DestroyLoadedObjects(const std::vector<std::uint64_t>& ids) {
    for (std::uint64_t id : ids) {
        GameObject* go = GameObject::FromInstanceId(id);
        if (go && go->scene() == this) Destroy(go);
    }
}

bool Scene::UnloadAdditive(int handle) {
    LoadRecord* r = FindLoad(handle);
    if (!r) return false;
    LoadRecord rec = std::move(*r);
    m_loads.erase(m_loads.begin() + (r - m_loads.data()));
    // Components still held back never started: drop them before their objects go.
    rec.held.clear();
    DestroyLoadedObjects(rec.finished ? rec.job->RootIds() : rec.job->AllIds());
    if (!rec.finished && rec.opts.onDone) rec.opts.onDone(false);
    return true;
}

void Scene::StepAsyncLoads() {
    for (std::size_t i = 0; i < m_loads.size();) {
        if (m_loads[i].finished) { ++i; continue; }
        LoadRecord rec = std::move(m_loads[i]);   // out of the list: callbacks and Clear() can't invalidate it
        const int handle = rec.handle;
        m_loads.erase(m_loads.begin() + (long)i);

        const bool first = !rec.job->Started();
        KeptState kept;
        if (first && !rec.opts.additive) {
            m_pendingMerges.clear();
            InventoryPersistence::BeforeSceneLoad(*this);
            DetachKept(kept);
        }
        const std::size_t before = (first && !rec.opts.additive) ? 0 : m_pending.size();
        std::string err;
        const bool ok = rec.job->Step(*this, rec.opts.objectsPerFrame, &err);
        // Hold what this step made until the whole scene is in: nothing Awakes against a
        // half-built hierarchy.
        const std::size_t from = std::min(before, m_pending.size());
        for (std::size_t k = from; k < m_pending.size(); ++k) if (m_pending[k]) rec.held.push_back(m_pending[k]);
        m_pending.resize(from);
        if (first && !rec.opts.additive) ReattachKept(kept);

        if (!ok) {
            rec.held.clear();
            DestroyLoadedObjects(rec.job->AllIds());
            rec.finished = true; rec.ok = false;
            auto done = std::move(rec.opts.onDone);
            m_loads.insert(m_loads.begin() + (long)i, std::move(rec));
            ++i;
            if (done) done(false);
            continue;
        }
        const bool finished = rec.job->Finished();
        auto progress = rec.opts.onProgress;
        const float p = finished ? 1.0f : rec.job->Progress();
        if (finished) {
            rec.finished = true; rec.ok = true;
            for (Component* c : rec.held) m_pending.push_back(c);
            rec.held.clear();
            if (!rec.opts.additive) InventoryPersistence::AfterSceneLoad(*this);
            Start();   // Awake / Start for everything that came in
            if (!rec.opts.additive && !kept.keep.empty()) ResolveMainCamera();
        }
        auto done = finished ? std::move(rec.opts.onDone) : std::function<void(bool)>{};
        m_loads.insert(m_loads.begin() + (long)i, std::move(rec));
        ++i;
        if (progress) progress(p);
        if (done) done(true);
        (void)handle;
    }
}

} // namespace okay
