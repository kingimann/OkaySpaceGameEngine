#include "okay/Scene/Scene.hpp"
#include "okay/Inventory/InventoryPersistence.hpp"
#include "okay/VisualScript/VsVariables.hpp"
#include "okay/Scripting/NextScriptVM.hpp"
#include "okay/Scene/EnsureCamera.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Layers.hpp"
#include "okay/Scene/ObjectPool.hpp"
#include "okay/Scene/ComponentSerializer.hpp"
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
#include <typeindex>
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
    ObjectPool::ForgetScene(Uid());   // the pools scripts made for this scene (ObjectPool::ForPrefab) go with it
}

namespace {
// A destroyed object is only a Transform shell: cloning it would make an empty, switched-off object.
GameObject* Clone(Scene& scene, const GameObject& prefab) {
    return prefab.IsDestroyed() ? nullptr : SceneSerializer::Instantiate(scene, prefab);
}
} // namespace

GameObject* Scene::Instantiate(const GameObject& prefab) {
    return Clone(*this, prefab);
}

GameObject* Scene::Instantiate(const GameObject& prefab, const Vec3& position) {
    GameObject* go = Clone(*this, prefab);
    if (go) go->transform->localPosition = position;
    return go;
}

GameObject* Scene::Instantiate(const GameObject& prefab, const Vec3& position, const Quat& rotation) {
    GameObject* go = Clone(*this, prefab);
    if (go) { go->transform->localPosition = position; go->transform->localRotation = rotation.Normalized(); }
    return go;
}

GameObject* Scene::Instantiate(const GameObject& prefab, Transform* parent, bool worldPositionStays) {
    GameObject* go = Clone(*this, prefab);
    if (go && parent) go->transform->SetParent(parent, worldPositionStays);   // refused: it stays a root
    return go;
}

GameObject* Scene::Instantiate(const GameObject& prefab, const Vec3& position, const Quat& rotation, Transform* parent) {
    GameObject* go = Clone(*this, prefab);
    if (!go) return nullptr;
    if (parent) go->transform->SetParent(parent, /*worldPositionStays=*/false);   // refused: it stays a root
    go->transform->SetPosition(position);   // world values, whatever it ended up under
    go->transform->SetRotation(rotation);
    return go;
}

std::string Scene::UniqueName(const std::string& base, const Transform* parent) const {
    std::unordered_set<std::string> used;   // the names among the siblings
    if (parent) {
        for (const Transform* c : parent->Children()) if (c && c->gameObject) used.insert(c->gameObject->name);
    } else {
        for (const auto& o : m_objects) if (!o->transform || !o->transform->Parent()) used.insert(o->name);
    }
    const std::string wanted = base.empty() ? std::string("GameObject") : base;
    if (!used.count(wanted)) return wanted;
    // "Crate (3)" counts from "Crate": cut a trailing " (digits)".
    std::string stem = wanted;
    const std::size_t open = stem.rfind(" (");
    if (open != std::string::npos && stem.size() > open + 3 && stem.back() == ')') {
        bool digits = true;
        for (std::size_t i = open + 2; i + 1 < stem.size(); ++i) if (stem[i] < '0' || stem[i] > '9') { digits = false; break; }
        if (digits) stem.resize(open);
    }
    for (int n = 1;; ++n) {
        std::string candidate = stem + " (" + std::to_string(n) + ")";
        if (!used.count(candidate)) return candidate;
    }
}

GameObject* Scene::Duplicate(const GameObject& original) {
    if (original.IsDestroyed() || !original.transform) return nullptr;
    Transform* parent = original.transform->Parent();
    if (parent && (!parent->gameObject || parent->gameObject->scene() != this || parent->gameObject->IsDestroyed())) parent = nullptr;
    const std::string name = UniqueName(original.name, parent);   // before the copy exists: it must not count
    GameObject* go = SceneSerializer::Instantiate(*this, original);
    if (!go) return nullptr;
    go->name = name;
    if (parent) go->transform->SetParent(parent, /*worldPositionStays=*/false);   // same local pose under the same parent
    if (original.scene() == this)
        go->transform->PlaceAfter(original.transform);   // right behind the original
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
    if (m_guidIndexStructure != m_structureVersion || m_guidIndexCount != m_objects.size()) {
        m_guidIndex.clear();
        m_guidIndex.reserve(m_objects.size());
        for (const auto& o : m_objects)
            if (!o->IsDestroyed()) m_guidIndex.emplace(o->Guid(), o.get());   // the first object with an id keeps it
        m_guidIndexStructure = m_structureVersion;
        m_guidIndexCount = m_objects.size();
    }
    auto it = m_guidIndex.find(guid);
    if (it != m_guidIndex.end() && !it->second->IsDestroyed() && it->second->Guid() == guid) return it->second;
    // Not listed, or listed for an object whose id was changed since (SetGuid does not tell the scene): scan.
    for (const auto& o : m_objects)
        if (!o->IsDestroyed() && o->Guid() == guid) return o.get();
    return nullptr;
}

// Counts the stretches where the scene is walking its component lists and calling user code
// (Awake/Start, Update, physics, render, OnDestroy). DestroyImmediate waits for the end of those.
struct Scene::BusyScope {
    Scene& s;
    explicit BusyScope(Scene& sc) : s(sc) { ++s.m_busy; }
    ~BusyScope() { --s.m_busy; }
};

void Scene::DestroyAllLive() {
    BusyScope busy(*this);   // an OnDisable / OnDestroy that calls Clear() or loads a scene is queued, not re-entered
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

bool Scene::Despawn(GameObject* go) {
    if (!go) return false;
    if (ObjectPool::ReleaseToOwner(*this, go)) return true;
    Destroy(go);
    return false;
}

void Scene::Clear() {
    if (m_busy > 0) { m_clearPending = true; return; }   // objects are in use right now: the end of the frame does it
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
    m_delayedIndex.clear();
    m_dontDestroy.clear();
    m_scheduler.Clear();
    m_tweens.Clear();
    m_physics.Clear();
    m_physics3d.Clear();                       // stale contact pairs point at colliders about to be freed
    m_physics3d.gravity = Vec3{0.0f, -9.81f, 0.0f};   // gravity is a scene setting: a new scene starts at the default
    m_physicsAcc = 0.0f;
    uiFont.clear();                            // the loaded scene sets its own, if any
    unknownRecords.clear();                    // ... and brings the unknown records of its own file
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
    m_clearPending = false;              // what asked for another Clear while this one ran is done
    for (auto& d : abandoned) d(false);
}

void Scene::ApplyPendingClear() {
    if (!m_clearPending || m_busy > 0) return;
    m_clearPending = false;
    Clear();
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
    {
        BusyScope busy(*this);
        FlushPending();
    }
    ApplyPendingClear();   // an Awake / Start that asked for a Clear
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
    k.order.reserve(m_objects.size());
    for (const auto& up : m_objects) k.order.push_back(up.get());
    k.activeOrder = m_active;
    if (mainCamera && k.keep.count(mainCamera->gameObject)) k.mainCamera = mainCamera;
    for (const auto& up : m_objects) {   // a kept root must not hang off what is about to be freed
        GameObject* g = up.get();
        Transform* par = g->transform ? g->transform->Parent() : nullptr;
        if (k.keep.count(g) && par && par->gameObject && !k.keep.count(par->gameObject)) {
            k.unparented.push_back({g, par, g->transform->GetSiblingIndex()});
            g->transform->SetParent(nullptr, /*worldPositionStays=*/true);
        }
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

void Scene::ReattachKept(KeptState& k, bool undo) {
    if (k.objects.empty()) return;
    for (auto& up : k.objects) m_objects.push_back(std::move(up));
    if (undo) {
        // The load did not happen: the scene must be what it was. Objects stand in their old order again, and a kept
        // object that was moved off a parent that stayed goes back under it, at its old place (the parents that
        // were not kept are all still here: nothing was cleared).
        std::unordered_map<const GameObject*, std::size_t> place;
        for (std::size_t i = 0; i < k.order.size(); ++i) place[k.order[i]] = i;
        std::stable_sort(m_objects.begin(), m_objects.end(), [&](const std::unique_ptr<GameObject>& a, const std::unique_ptr<GameObject>& b) {
            auto ia = place.find(a.get()), ib = place.find(b.get());
            const std::size_t pa = ia == place.end() ? place.size() : ia->second, pb = ib == place.end() ? place.size() : ib->second;
            return pa < pb;
        });
        for (auto it = k.unparented.rbegin(); it != k.unparented.rend(); ++it)   // last taken off, first put back: each index was read after the ones before it left
            if (it->go && it->go->transform && it->parent) it->go->transform->SetParent(it->parent, /*worldPositionStays=*/true, it->index);
        if (k.mainCamera) mainCamera = k.mainCamera;
    }
    m_dontDestroy = std::move(k.marked);
    m_active.insert(m_active.end(), k.active.begin(), k.active.end());
    m_pending.insert(m_pending.end(), k.pending.begin(), k.pending.end());
    if (undo) {   // the update list is what it was too (the kept components updated in their old turn)
        std::unordered_map<const Component*, std::size_t> turn;
        for (std::size_t i = 0; i < k.activeOrder.size(); ++i) turn[k.activeOrder[i]] = i;
        std::stable_sort(m_active.begin(), m_active.end(), [&](const Component* a, const Component* b) {
            auto ia = turn.find(a), ib = turn.find(b);
            return (ia == turn.end() ? turn.size() : ia->second) < (ib == turn.end() ? turn.size() : ib->second);
        });
    }
    if (m_hasOrdered)
        std::stable_sort(m_active.begin(), m_active.end(), [](const Component* a, const Component* b) {
            return (a ? a->ExecutionOrder() : 0) < (b ? b->ExecutionOrder() : 0);
        });
    ++m_structureVersion;
}

void Scene::MoveKeptToEnd() {
    std::unordered_set<const GameObject*> keep;
    std::vector<Transform*> stack;
    for (GameObject* g : m_dontDestroy) if (g && g->transform) stack.push_back(g->transform);
    while (!stack.empty()) {
        Transform* t = stack.back(); stack.pop_back();
        if (!t->gameObject || !keep.insert(t->gameObject).second) continue;
        for (Transform* ch : t->Children()) stack.push_back(ch);
    }
    if (keep.empty()) return;
    std::stable_partition(m_objects.begin(), m_objects.end(), [&](const std::unique_ptr<GameObject>& up) { return !keep.count(up.get()); });
    ++m_structureVersion;
}

void Scene::Update(float deltaTime) {
    if (!std::isfinite(deltaTime)) deltaTime = 0.0f;   // NaN or infinity is no time: it would poison the bodies, the timers and every script
    ActionList::UseScene(this);
    {
    BusyScope busy(*this);   // component lists are walked until the end of LateUpdate
    FlushPending(); // adopt anything created since last frame
    // Drop any components removed last frame (their slots were nulled).
    m_active.erase(std::remove(m_active.begin(), m_active.end(), nullptr), m_active.end());
    DispatchActivation();
    ++m_updateCount;   // everything switched off before this point has now heard about it (see UpdateCount)
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
    }   // end of BusyScope

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
        ReattachKept(kept, /*undo=*/!loaded);   // back in whether or not the load worked; a failed load leaves them where they stood
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
            // Only an object that can be picked pays for its world pose (reading it walks the parent
            // chain: for every object that made a frame over a deep hierarchy quadratic).
            auto* sr = go->GetComponent<SpriteRenderer>();
            auto* bc = sr ? nullptr : go->GetComponent<BoxCollider2D>();
            if (!sr && !bc) continue;
            Vec3 c = go->transform->Position();
            Vec3 ls = go->transform->LossyScale();
            Vec2 half; int order = 0;
            if (sr) {
                half = {sr->size.x * ls.x * 0.5f, sr->size.y * ls.y * 0.5f};
                order = sr->sortOrder;
            } else {
                half = {bc->size.x * ls.x * 0.5f, bc->size.y * ls.y * 0.5f};
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
    BusyScope busy(*this);
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

int Scene::DestroyChildren(GameObject* go) {
    if (!go || go->IsDestroyed() || !go->transform) return 0;
    int asked = 0;
    for (const Transform* c : go->transform->Children())   // Destroy only queues: the child list is not edited here
        if (c && c->gameObject) { Destroy(c->gameObject); ++asked; }
    return asked;
}

void Scene::Destroy(GameObject* go, float seconds) {
    if (!go) return;
    if (!(seconds > 0.0f)) { Destroy(go); return; }   // zero, negative or NaN: now
    auto waiting = m_delayedIndex.find(go->InstanceId());
    if (waiting != m_delayedIndex.end()) { auto& d = m_delayedDestroy[waiting->second]; d.second = std::min(d.second, seconds); return; }
    m_delayedIndex[go->InstanceId()] = m_delayedDestroy.size();
    m_delayedDestroy.push_back({go->InstanceId(), seconds});
}

bool Scene::CancelDelayedDestroy(const GameObject* go) {
    if (!go) return false;
    const auto waiting = m_delayedIndex.find(go->InstanceId());
    if (waiting == m_delayedIndex.end()) return false;
    m_delayedDestroy[waiting->second].first = 0;   // no object has id 0: the next tick drops the entry
    m_delayedIndex.erase(waiting);
    return true;
}

bool Scene::IsDestroyPending(const GameObject* go) const {
    return go && (m_destroyQueued.count(go) != 0 || m_delayedIndex.count(go->InstanceId()) != 0);
}

void Scene::DestroyImmediate(GameObject* go) {
    if (!go || go->IsDestroyed()) return;
    Destroy(go);
    if (m_busy == 0) FlushDestroyed();   // not while a component list is being walked: the end of the frame does it then
}

void Scene::TickDelayedDestroys(float deltaTime) {
    if (m_delayedDestroy.empty()) return;
    auto due = std::move(m_delayedDestroy);
    m_delayedDestroy.clear();
    m_delayedIndex.clear();
    if (!(deltaTime > 0.0f) || !std::isfinite(deltaTime)) deltaTime = 0.0f;   // a bad frame time takes no time off
    for (auto& d : due) {
        GameObject* go = GameObject::FromInstanceId(d.first);   // null once it is gone
        if (!go || go->scene() != this) continue;
        d.second -= deltaTime;
        if (d.second <= 0.0f) Destroy(go);
        else { m_delayedIndex[d.first] = m_delayedDestroy.size(); m_delayedDestroy.push_back(d); }
    }
}

void Scene::DontDestroyOnLoad(GameObject* go) {
    if (go && !IsDontDestroyOnLoad(go)) m_dontDestroy.push_back(go);
}

bool Scene::IsDontDestroyOnLoad(const GameObject* go) const {
    return std::find(m_dontDestroy.begin(), m_dontDestroy.end(), go) != m_dontDestroy.end();
}

bool Scene::CancelDontDestroyOnLoad(const GameObject* go) {
    auto it = std::find(m_dontDestroy.begin(), m_dontDestroy.end(), go);
    if (it == m_dontDestroy.end()) return false;
    m_dontDestroy.erase(it);
    return true;
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

// Take every dying object out of the hierarchy: each becomes a root shell at the world pose it had, and
// each list of children (a surviving parent's, or a dying parent's) loses its dying children in one pass.
// The objects are visited top down (a parent before its children) with each one's world matrix, rotation
// and scale carried from its parent's, so the whole job is linear in the number of objects: reading a
// pose through the chain for every object, or erasing one child at a time from a long list, is not.
void Scene::DetachDying(const std::vector<GameObject*>& batch, const std::unordered_set<const GameObject*>& inScene) {
    auto dying = [&inScene](const Transform* t) { return t && t->gameObject && inScene.count(t->gameObject) != 0; };
    struct Frozen { Mat4 world; Quat rotation; Vec3 scale; };
    struct Entry { Transform* t; int parent; };   // parent: index into `order` of the dying parent, or -1
    std::vector<Entry> order;
    std::vector<Frozen> frozen;
    order.reserve(inScene.size());
    frozen.reserve(inScene.size());
    std::unordered_set<Transform*> survivingParents;   // lists a dying top is taken out of
    std::vector<std::pair<Transform*, int>> stack;
    for (GameObject* go : batch) {
        if (!go || !inScene.count(go) || !go->transform) continue;
        Transform* top = go->transform;
        if (dying(top->m_parent)) continue;   // reached from its dying parent below
        if (top->m_parent) survivingParents.insert(top->m_parent);
        stack.push_back({top, -1});
        while (!stack.empty()) {
            auto [t, parent] = stack.back(); stack.pop_back();
            const int me = static_cast<int>(order.size());
            Frozen f;
            if (parent < 0) { f.world = t->LocalToWorldMatrix(); f.rotation = t->Rotation(); f.scale = t->LossyScale(); }
            else {
                const Frozen& up = frozen[static_cast<std::size_t>(parent)];
                f.world = up.world * Mat4::TRS(t->localPosition, t->localRotation, t->localScale);
                f.rotation = (up.rotation * t->localRotation).Normalized();
                f.scale = {up.scale.x * t->localScale.x, up.scale.y * t->localScale.y, up.scale.z * t->localScale.z};
            }
            order.push_back({t, parent});
            frozen.push_back(f);
            const auto& kids = t->Kids();
            for (std::size_t i = kids.size(); i-- > 0;)
                if (dying(kids[i])) stack.push_back({kids[i], me});
        }
    }
    if (order.empty()) return;
    for (std::size_t i = 0; i < order.size(); ++i) {   // each becomes a root at the pose that was read
        Transform* t = order[i].t;
        const Frozen& f = frozen[i];
        t->m_parent = nullptr;
        t->localPosition = {f.world.m[12], f.world.m[13], f.world.m[14]};
        t->localRotation = f.rotation.Normalized();
        t->localScale = f.scale;
    }
    auto trim = [&dying](Transform* list) { list->RemoveChildrenIf(dying); };   // the dying children leave this list together; the others keep their order
    for (Transform* p : survivingParents) trim(p);
    for (const Entry& e : order) if (e.t->ChildCount() > 0) trim(e.t);
    detail::g_activeEpoch.fetch_add(1, std::memory_order_relaxed);   // parent links changed
}

void Scene::FlushDestroyed() {
    FlushDestroyedNow();
    ApplyPendingClear();   // a Clear asked for during the walk (not when this flush itself runs inside one)
}

void Scene::FlushDestroyedNow() {
    BusyScope busy(*this);   // OnDestroy handlers run: a DestroyImmediate from one only queues
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
    // Unlink every dying object from its parent first, all of them in one pass (see DetachDying).
    DetachDying(batch, inScene);
    // Keep the shells (GameObject + Transform) in the graveyard; free the rest. One pass
    // that keeps the survivors in order.
    {
        std::vector<std::unique_ptr<GameObject>> kept;
        kept.reserve(m_objects.size());
        for (auto& obj : m_objects) {
            GameObject* go = obj.get();
            if (!inScene.count(go)) { kept.push_back(std::move(obj)); continue; }
            // Anything DetachDying did not reach (a hierarchy whose child lists disagree) is unlinked
            // here the slow way, so no shell is left with a parent.
            if (Transform* t = go->transform)
                if (t->Parent()) t->SetParent(nullptr, /*worldPositionStays=*/true);
            go->StripToTransform();
            m_graveyard.push_back(std::move(obj));
        }
        m_objects.swap(kept);
    }
    // An object an OnDestroy handler parented under a dying one is not part of the batch: it
    // lives on, as a root at the pose it had, instead of hanging under a frozen shell for good.
    // (Every dying child has detached itself above, so what is still listed was adopted.)
    for (GameObject* go : batch) {
        Transform* t = go->transform;
        if (!inScene.count(go) || !t || t->Children().empty()) continue;
        const std::vector<Transform*> adopted = t->Children();
        for (Transform* kid : adopted) kid->SetParent(nullptr, /*worldPositionStays=*/true);
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

namespace {
// The objects a path names, in scene order, stopping after `limit` matches. A path is read from the left: an
// object matches when its name is what the path starts with, followed by '/' or the end; the next level
// then has to match the rest. So an object called "a/b" is found by its whole name, and two siblings with the
// same name are both tried (an object whose subtree has no match is not the end of the search). Runs of '/' count
// as one, a leading and a trailing '/' are allowed. An explicit stack keeps a very long path from recursing.
std::vector<GameObject*> CollectByPath(const std::vector<std::unique_ptr<GameObject>>& objects, const std::string& path,
                                       std::size_t limit) {
    std::vector<GameObject*> out;
    std::size_t first = path.find_first_not_of('/');
    if (first == std::string::npos || limit == 0) return out;
    struct Frame { GameObject* go; std::size_t at; };   // `at`: where in the path this object's name has to start
    std::vector<Frame> stack;
    for (std::size_t i = objects.size(); i-- > 0;)
        if (!objects[i]->transform || !objects[i]->transform->Parent()) stack.push_back({objects[i].get(), first});
    while (!stack.empty()) {
        const Frame f = stack.back();
        stack.pop_back();
        const std::string& name = f.go->name;
        if (name.empty() || path.compare(f.at, name.size(), name) != 0) continue;
        std::size_t after = f.at + name.size();
        if (after < path.size()) {
            if (path[after] != '/') continue;                 // the name is only the start of a longer one
            after = path.find_first_not_of('/', after);
        }
        if (after == std::string::npos || after >= path.size()) {   // the path ends here (maybe with a '/')
            out.push_back(f.go);
            if (out.size() >= limit) return out;
            continue;
        }
        if (!f.go->transform) continue;
        const auto& kids = f.go->transform->Children();
        for (std::size_t i = kids.size(); i-- > 0;)
            if (kids[i] && kids[i]->gameObject) stack.push_back({kids[i]->gameObject, after});
    }
    return out;
}
} // namespace

GameObject* Scene::FindByPath(const std::string& path) const {
    const std::vector<GameObject*> hit = CollectByPath(m_objects, path, 1);
    return hit.empty() ? nullptr : hit.front();
}

std::vector<GameObject*> Scene::FindAllByPath(const std::string& path) const {
    return CollectByPath(m_objects, path, std::numeric_limits<std::size_t>::max());
}

std::vector<GameObject*> Scene::Roots() const {
    std::vector<GameObject*> out;
    for (const auto& o : m_objects)
        if (!o->transform || !o->transform->Parent()) out.push_back(o.get());
    return out;
}

SceneGraphStats Scene::Stats() const {
    SceneGraphStats st;
    st.objects = static_cast<int>(m_objects.size());
    st.pendingDestroy = static_cast<int>(m_destroyQueue.size());
    st.shells = static_cast<int>(m_graveyard.size());
    for (const Component* c : m_active) if (c) ++st.runningComponents;
    for (const Component* c : m_pending) if (c) ++st.awaitingStart;
    std::unordered_map<std::type_index, std::pair<const Component*, int>> kinds;   // per type: one exemplar and a count
    std::vector<std::pair<const Transform*, int>> stack;   // explicit stack: a chain 100000 deep is no recursion
    for (const auto& o : m_objects) {
        const GameObject* g = o.get();
        for (const auto& c : g->Components()) {
            auto& k = kinds[std::type_index(typeid(*c))];
            if (!k.first) k.first = c.get();
            ++k.second;
            ++st.components;
        }
        if (g->ActiveInHierarchy()) ++st.activeInHierarchy;
        if (g->transform && !g->transform->Parent()) { ++st.roots; stack.push_back({g->transform, 0}); }
    }
    st.inactive = st.objects - st.activeInHierarchy;
    while (!stack.empty()) {
        auto [t, depth] = stack.back(); stack.pop_back();
        st.maxDepth = std::max(st.maxDepth, depth);
        for (const Transform* c : t->Children()) if (c) stack.push_back({c, depth + 1});
    }
    st.byType.reserve(kinds.size());
    for (const auto& kv : kinds)
        st.byType.push_back({ComponentSerializerRegistry::TypeKey(*kv.second.first), kv.second.second});
    std::sort(st.byType.begin(), st.byType.end(), [](const SceneGraphStats::TypeCount& a, const SceneGraphStats::TypeCount& b) {
        return a.count != b.count ? a.count > b.count : a.type < b.type;
    });
    return st;
}

std::vector<std::string> SceneGraphStats::Lines(int maxTypes) const {
    std::vector<std::string> out;
    out.push_back("Objects: " + std::to_string(objects) + " (" + std::to_string(roots) + " roots, " +
                  std::to_string(inactive) + " inactive, depth " + std::to_string(maxDepth) + ")");
    out.push_back("Components: " + std::to_string(components) + " (" + std::to_string(runningComponents) + " running, " +
                  std::to_string(awaitingStart) + " waiting to start)");
    if (pendingDestroy > 0 || shells > 0)
        out.push_back("Destroy queue: " + std::to_string(pendingDestroy) + ", shells kept: " + std::to_string(shells));
    for (int i = 0; i < maxTypes && i < static_cast<int>(byType.size()); ++i)
        out.push_back("  " + byType[static_cast<std::size_t>(i)].type + ": " + std::to_string(byType[static_cast<std::size_t>(i)].count));
    return out;
}

std::string SceneGraphStats::ToString(int maxTypes) const {
    std::string text;
    for (const std::string& l : Lines(maxTypes)) { text += l; text += '\n'; }
    return text;
}

SceneValidationReport Scene::Validate(const std::string& assetRoot) const {
    return SceneSerializer::Validate(SceneSerializer::Serialize(*this), assetRoot);
}

// The objects the index lists for `tag`. A tag can be changed through std::string calls the
// index cannot see, so each entry is checked against what the object carries now; one that
// disagrees means the index is stale and it is rebuilt once.
const std::vector<GameObject*>* Scene::TagList(const std::string& tag) const {
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto& idx = TagIndex();
        auto it = idx.find(tag);
        if (it == idx.end()) return nullptr;
        bool fresh = true;
        for (const GameObject* o : it->second) if (o->tag != tag) { fresh = false; break; }
        if (fresh) return &it->second;
        m_tagIndexTags = 0;   // force the rebuild
    }
    return nullptr;
}

GameObject* Scene::FindWithTag(const std::string& tag) const {
    if (tag.empty()) return nullptr;   // "no tag" is not a tag: every untagged object would match
    // Only the first entry is checked against what the object carries now (the one answered): with 10000
    // objects under one tag a lookup stays a hash and a compare. The list-returning calls check every entry.
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto& idx = TagIndex();
        auto it = idx.find(tag);
        if (it == idx.end() || it->second.empty()) return nullptr;
        if (it->second.front()->tag == tag) return it->second.front();
        m_tagIndexTags = 0;   // a tag was changed behind the index's back: rebuild once
    }
    return nullptr;
}

std::vector<GameObject*> Scene::FindGameObjectsWithTag(const std::string& tag) const {
    if (tag.empty()) return {};
    const auto* list = TagList(tag);
    return list ? *list : std::vector<GameObject*>{};
}

int Scene::CountWithTag(const std::string& tag) const {
    if (tag.empty()) return 0;
    const auto* list = TagList(tag);
    return list ? static_cast<int>(list->size()) : 0;
}

std::vector<GameObject*> Scene::FindGameObjectsWithName(const std::string& name) const {
    std::vector<GameObject*> out;
    for (const auto& o : m_objects) if (o->name == name) out.push_back(o.get());
    return out;
}

std::vector<GameObject*> Scene::FindGameObjectsOnLayer(int layer) const {
    std::vector<GameObject*> out;
    if (layer < 0 || layer > 31) return out;
    for (const auto& o : m_objects) if (o->layer == layer) out.push_back(o.get());
    return out;
}

int Scene::CountOnLayer(int layer) const {
    if (layer < 0 || layer > 31) return 0;
    int n = 0;
    for (const auto& o : m_objects) if (o->layer == layer) ++n;
    return n;
}

std::vector<std::string> Scene::UnlistedTags() const {
    std::unordered_set<std::string> seen;   // a hash, not a scan of the result: 10000 different tags stay linear
    for (const auto& o : m_objects)
        if (!o->tag.empty() && !seen.count(o->tag) && !Tags::Contains(o->tag)) seen.insert(o->tag);
    std::vector<std::string> out(seen.begin(), seen.end());
    std::sort(out.begin(), out.end());
    return out;
}

int Scene::ReplaceTag(const std::string& from, const std::string& to) {
    if (from.empty() || !Tags::Accepts(to)) return 0;
    int n = 0;
    for (const auto& o : m_objects)
        if (o->tag == from) { o->tag = to; ++n; }
    return n;
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

void Scene::ApplyLayerCollisionPreset(LayerCollisionPreset preset) {
    if (preset == LayerCollisionPreset::Everything) { ResetLayerCollisions(); return; }
    for (int a = 0; a < 32; ++a)
        for (int b = a; b < 32; ++b)
            SetLayerCollision(a, b, preset == LayerCollisionPreset::SameLayerOnly && a == b);
}

void Scene::SetLayerCollisionGroups(std::uint32_t maskA, std::uint32_t maskB, bool enabled) {
    for (int a = 0; a < 32; ++a) {
        if (!((maskA >> a) & 1u)) continue;
        for (int b = 0; b < 32; ++b)
            if ((maskB >> b) & 1u) SetLayerCollision(a, b, enabled);
    }
}

bool Scene::SetLayerCollisionByName(const std::string& a, const std::string& b, bool enabled) {
    const int la = Layers::NameToLayer(a), lb = Layers::NameToLayer(b);
    if (la < 0 || lb < 0) return false;
    SetLayerCollision(la, lb, enabled);
    return true;
}

std::vector<std::pair<int, int>> Scene::DisabledLayerPairs() const {
    std::vector<std::pair<int, int>> out;
    for (int a = 0; a < 32; ++a)
        for (int b = a; b < 32; ++b)
            if (!LayersCollide(a, b)) out.push_back({a, b});
    return out;
}

Bounds Scene::WorldBounds(const GameObject& root, bool includeChildren) {
    Bounds out;   // empty until a renderer adds to it
    std::vector<const GameObject*> stack{&root};
    while (!stack.empty()) {
        const GameObject* g = stack.back(); stack.pop_back();
        if (g->IsDestroyed() || !g->active || !g->transform) continue;   // an inactive object takes its subtree with it
        Vec3 lo, hi;
        bool have = false;
        if (const auto* mr = g->GetComponent<MeshRenderer>(); mr && mr->enabled && !mr->DisplayMesh().vertices.empty()) {
            mr->DisplayMesh().Bounds(lo, hi);
            have = true;
        } else if (const auto* sr = g->GetComponent<SpriteRenderer>(); sr && sr->enabled) {
            lo = {-sr->size.x * 0.5f, -sr->size.y * 0.5f, 0.0f};
            hi = { sr->size.x * 0.5f,  sr->size.y * 0.5f, 0.0f};
            have = true;
        }
        if (have) {
            const Mat4 m = g->transform->LocalToWorldMatrix();   // the eight corners, so a turned box is boxed as drawn
            for (int i = 0; i < 8; ++i)
                out.Encapsulate(m.MultiplyPoint({(i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z}));
        }
        if (includeChildren) {
            const auto& kids = g->transform->Children();
            for (std::size_t i = kids.size(); i-- > 0;)
                if (kids[i] && kids[i]->gameObject) stack.push_back(kids[i]->gameObject);
        }
    }
    return out;
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
    if (ai < 0) m_objects.push_back(std::move(held));
    else        m_objects.insert(m_objects.begin() + (after ? ai + 1 : ai), std::move(held));
    ++m_structureVersion;   // root order changed (UI draws in this order)
}

// ---- Incremental (multi-frame) scene loads ----------------------------------

int Scene::BeginAsyncLoad(const std::string& path, AsyncLoadOptions opts) {
    std::string err;
    auto job = SceneLoadJob::OpenFile(path, opts.additive, opts.offset, &err);
    if (!job) {
        if (opts.onDone) opts.onDone(false);
        return 0;
    }
    if (!opts.additive) {
        // The last replacing request wins. A deferred load or text load still waiting is told it never ran, and a
        // replacing load still in flight is stopped (what it made is removed) before the new one clears the scene.
        if (m_hasPendingLoad || m_hasPendingText) {
            auto done = std::move(m_pendingLoadDone);
            m_hasPendingLoad = false; m_hasPendingText = false;
            m_pendingLoad.clear(); m_pendingLoadDone = {};
            m_pendingText.clear(); m_pendingTextAfter = {};
            if (done) done(false);
        }
        std::vector<int> earlier;
        for (const auto& r : m_loads) if (!r.finished && !r.opts.additive) earlier.push_back(r.handle);
        for (int h : earlier) UnloadAdditive(h);
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
    // Each unfinished load is stepped once per frame, found again by its handle every time: a callback may start,
    // unload or finish other loads, and the first step of a replacing load clears the scene and with it every other
    // load in flight, so no position in the list can be trusted from one step to the next. A load started by a
    // callback in this pass takes its first step in this pass too.
    std::unordered_set<int> stepped;
    const std::size_t limit = m_loads.size() * 4 + 64;   // a callback that begins a load as each one ends must not spin for ever
    while (stepped.size() < limit) {
        std::size_t i = 0;
        for (; i < m_loads.size(); ++i) if (!m_loads[i].finished && !stepped.count(m_loads[i].handle)) break;
        if (i >= m_loads.size()) break;
        stepped.insert(m_loads[i].handle);
        LoadRecord rec = std::move(m_loads[i]);   // out of the list: callbacks and Clear() can't invalidate it
        m_loads.erase(m_loads.begin() + (long)i);
        // Back in the list at its old place (clamped: the list may have shrunk meanwhile).
        auto putBack = [&](LoadRecord&& r) { m_loads.insert(m_loads.begin() + (long)std::min(i, m_loads.size()), std::move(r)); };

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
        if (first && !rec.opts.additive) ReattachKept(kept, /*undo=*/!ok);   // a load that failed before it began leaves the scene as it was

        if (!ok) {
            rec.held.clear();
            DestroyLoadedObjects(rec.job->AllIds());
            rec.finished = true; rec.ok = false;
            auto done = std::move(rec.opts.onDone);
            putBack(std::move(rec));
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
            if (!rec.opts.additive && !m_dontDestroy.empty()) MoveKeptToEnd();   // the order a one-go load gives
            Start();   // Awake / Start for everything that came in
            if (!rec.opts.additive && !m_dontDestroy.empty()) ResolveMainCamera();
        }
        auto done = finished ? std::move(rec.opts.onDone) : std::function<void(bool)>{};
        putBack(std::move(rec));
        if (progress) progress(p);
        if (done) done(true);
    }
}

} // namespace okay
