#include "okay/Scene/ObjectPool.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace okay {

bool ObjectPool::SetPrefabFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    // Go through a scratch scene so packed / plain files are both handled by the loader.
    Scene scratch("pool");
    GameObject* root = SceneSerializer::InstantiateFromFile(scratch, path);
    if (!root) return false;
    SetTemplate(*root);
    m_prefabPath = path;
    return !m_text.empty();
}

void ObjectPool::SetTemplate(const GameObject& t) {
    if (t.IsDestroyed() || !t.transform) return;   // a frozen shell serializes to an empty object: keep the old source
    m_text = SceneSerializer::SerializeObject(t);
    m_prefabPath.clear();
    Capture(t);
}

// What each object of the template looks like now, depth first: a reused object is put back to this.
void ObjectPool::Capture(const GameObject& root) {
    m_nodes.clear();
    root.ForEachDescendant([this](GameObject& g) {
        NodeState n;
        n.position = g.transform->localPosition;
        n.rotation = g.transform->localRotation;
        n.scale = g.transform->localScale;
        n.active = g.active;
        n.layer = g.layer;
        n.tag = g.tag;
        for (const auto& c : g.Components()) {
            n.types.push_back(&typeid(*c));
            n.enabled.push_back(c->enabled);
        }
        m_nodes.push_back(std::move(n));
    }, /*includeInactive=*/true, /*includeSelf=*/true);
}

GameObject* ObjectPool::Lookup(std::uint64_t id) const {
    ++m_lookups;
    GameObject* g = GameObject::FromInstanceId(id);
    return g && g->scene() == m_scene ? g : nullptr;
}

// Forget what is gone (destroyed, or freed with a cleared scene). Nothing can die without the scene's structure
// version moving, so a prune is skipped while it has not moved since the last one.
void ObjectPool::Prune() const {
    const std::uint64_t version = m_scene->StructureVersion();
    if (version == m_pruneStructure) return;
    for (auto it = m_owned.begin(); it != m_owned.end();) {
        ++m_lookups;
        GameObject* g = GameObject::FromInstanceId(it->first);
        it = (!g || g->scene() != m_scene) ? m_owned.erase(it) : std::next(it);
    }
    auto gone = [this](std::uint64_t id) { return m_owned.find(id) == m_owned.end(); };
    m_idle.erase(std::remove_if(m_idle.begin(), m_idle.end(), gone), m_idle.end());
    m_cooling.erase(std::remove_if(m_cooling.begin(), m_cooling.end(), gone), m_cooling.end());
    m_pruneStructure = m_scene->StructureVersion();
}

// Objects released before the scene's last Update have had their OnDisable: they can be handed out.
void ObjectPool::Settle() const {
    if (m_cooling.empty() || m_scene->UpdateCount() == m_coolStamp) return;
    m_idle.insert(m_idle.end(), m_cooling.begin(), m_cooling.end());   // release order kept: the newest stays last
    for (std::uint64_t id : m_cooling) { auto it = m_owned.find(id); if (it != m_owned.end()) it->second = kIdle; }
    m_cooling.clear();
}

GameObject* ObjectPool::Make() {
    GameObject* go = SceneSerializer::InstantiateFromText(*m_scene, m_text);
    if (!go) return nullptr;
    if (!m_prefabPath.empty()) go->prefabSource = m_prefabPath;
    m_owned[go->InstanceId()] = kActive;
    ++m_created;
    return go;
}

void ObjectPool::Prewarm(int count) {
    if (!HasSource()) return;
    Prune();
    while (count-- > 0 && static_cast<int>(m_idle.size() + m_cooling.size()) < maxSize) {
        GameObject* go = Make();
        if (!go) return;
        go->active = false;   // never seen live: its first Get starts it
        m_owned[go->InstanceId()] = kIdle;
        m_idle.push_back(go->InstanceId());
    }
}

// Put a reused object back to what the template looked like (see the class comment), then let each component reset
// what only it knows.
void ObjectPool::ResetForReuse(GameObject& root) {
    std::vector<GameObject*> objs;
    root.ForEachDescendant([&objs](GameObject& g) { objs.push_back(&g); }, /*includeInactive=*/true, /*includeSelf=*/true);
    const bool sameShape = objs.size() == m_nodes.size();
    for (std::size_t i = 0; i < objs.size() && i < m_nodes.size(); ++i) {
        if (i > 0 && !sameShape) break;   // children were added or removed: only the root can be matched to the template
        GameObject& g = *objs[i];
        const NodeState& n = m_nodes[i];
        g.transform->localPosition = n.position;
        g.transform->localRotation = n.rotation;
        g.transform->localScale = n.scale;
        if (i > 0) g.active = n.active;
        g.layer = n.layer;
        if (g.tag != n.tag) g.tag = n.tag;
        const auto& comps = g.Components();
        bool sameParts = comps.size() == n.types.size();
        for (std::size_t k = 0; sameParts && k < comps.size(); ++k) sameParts = *n.types[k] == typeid(*comps[k]);
        if (sameParts)
            for (std::size_t k = 0; k < comps.size(); ++k) comps[k]->enabled = n.enabled[k];
    }
    Scene::RemovalWatch watch(*m_scene);   // a component that removes another one while it resets must not be touched after
    std::vector<Component*> parts;
    for (GameObject* g : objs) {
        g->CopyComponentPointers(parts);
        for (Component* c : parts) {
            if (watch.Any() && watch.Gone(c)) continue;
            c->OnPoolReuse();
        }
    }
}

GameObject* ObjectPool::Get() {
    if (!HasSource()) return nullptr;
    Settle();
    GameObject* go = nullptr;
    // Newest first. Anyone can have destroyed an idle object, queued it for destruction or switched it back on:
    // only what is still ours, off, and has had its OnDisable is handed out.
    while (!m_idle.empty()) {
        const std::uint64_t id = m_idle.back();
        m_idle.pop_back();
        GameObject* c = Lookup(id);
        if (!c) { m_owned.erase(id); continue; }
        if (m_scene->IsDestroyPending(c)) { m_owned.erase(id); continue; }   // it is going: not ours to hand out
        if (c->active) { m_owned[id] = kActive; continue; }                  // somebody took it
        if (c->HasLiveComponentInHierarchy()) {                               // its OnDisable is still owed
            if (m_cooling.empty()) m_coolStamp = m_scene->UpdateCount();
            m_cooling.push_back(id);
            m_owned[id] = kCooling;
            continue;
        }
        go = c;
        break;
    }
    if (go) {
        ++m_reused;
        if (go->transform->Parent()) go->transform->SetParent(nullptr, /*worldPositionStays=*/false);
        ResetForReuse(*go);
    } else {
        go = Make();
        if (!go) return nullptr;
        Transform* t = go->transform;
        if (t->Parent()) t->SetParent(nullptr, /*worldPositionStays=*/false);
    }
    m_owned[go->InstanceId()] = kActive;
    go->active = true;
    return go;
}

GameObject* ObjectPool::Get(const Vec3& position) {
    GameObject* go = Get();
    if (go) go->transform->localPosition = position;
    return go;
}

bool ObjectPool::Owns(const GameObject* go) const {
    return go && m_owned.count(go->InstanceId()) != 0 && !go->IsDestroyed();
}

bool ObjectPool::Release(GameObject* go) {
    if (!go || go->IsDestroyed() || go->scene() != m_scene) return false;
    const std::uint64_t id = go->InstanceId();
    auto owned = m_owned.find(id);
    if (owned == m_owned.end()) return false;
    if (owned->second != kActive) return true;   // already idle
    ++m_lookups;
    if (m_scene->IsDestroyPending(go)) {   // it is going anyway: the pool lets it go
        m_scene->CancelDelayedDestroy(go);   // a delayed request is the pool's to take back; a queued one stands
        if (m_scene->IsDestroyPending(go)) { m_owned.erase(id); return true; }
    }
    Settle();
    if (static_cast<int>(m_idle.size() + m_cooling.size()) >= maxSize) {
        Prune();   // the idle objects that were destroyed behind our back do not count against the limit
        if (static_cast<int>(m_idle.size() + m_cooling.size()) >= maxSize) {
            m_owned.erase(id);
            m_scene->Destroy(go);
            return true;
        }
    }
    if (go->transform->Parent()) go->transform->SetParent(nullptr, /*worldPositionStays=*/false);
    m_scene->CancelDelayedDestroy(go);   // the old request belongs to the use that just ended
    go->active = false;                  // OnDisable follows at the start of the next frame
    if (go->HasLiveComponentInHierarchy()) {
        if (m_cooling.empty()) m_coolStamp = m_scene->UpdateCount();
        m_cooling.push_back(id);
        m_owned[id] = kCooling;
    } else {
        m_idle.push_back(id);   // nothing owes an OnDisable (never started): usable at once
        m_owned[id] = kIdle;
    }
    return true;
}

void ObjectPool::Clear() {
    for (const auto* list : {&m_idle, &m_cooling})
        for (std::uint64_t id : *list)
            if (GameObject* g = Lookup(id)) m_scene->Destroy(g);
    for (const auto* list : {&m_idle, &m_cooling})
        for (std::uint64_t id : *list) m_owned.erase(id);
    m_idle.clear();
    m_cooling.clear();
}

int ObjectPool::IdleCount() const {
    Prune();
    return static_cast<int>(m_idle.size() + m_cooling.size());
}

int ObjectPool::ActiveCount() const {
    Prune();
    return static_cast<int>(m_owned.size()) - static_cast<int>(m_idle.size() + m_cooling.size());
}

} // namespace okay
