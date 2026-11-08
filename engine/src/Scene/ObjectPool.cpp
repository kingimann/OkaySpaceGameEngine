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
    m_text = SceneSerializer::SerializeObject(t);
    m_prefabPath.clear();
    m_pos = t.transform->localPosition;
    m_scale = t.transform->localScale;
    m_rotX = t.transform->localRotation.x; m_rotY = t.transform->localRotation.y;
    m_rotZ = t.transform->localRotation.z; m_rotW = t.transform->localRotation.w;
}

void ObjectPool::Prune() const {
    // Forget objects that were destroyed (or died with a cleared scene).
    auto gone = [&](std::uint64_t id) {
        GameObject* g = GameObject::FromInstanceId(id);
        return !g || g->scene() != m_scene;
    };
    m_idle.erase(std::remove_if(m_idle.begin(), m_idle.end(), gone), m_idle.end());
    for (auto it = m_owned.begin(); it != m_owned.end();) it = gone(*it) ? m_owned.erase(it) : std::next(it);
}

GameObject* ObjectPool::Make() {
    GameObject* go = SceneSerializer::InstantiateFromText(*m_scene, m_text);
    if (!go) return nullptr;
    if (!m_prefabPath.empty()) go->prefabSource = m_prefabPath;
    m_owned.insert(go->InstanceId());
    ++m_created;
    return go;
}

void ObjectPool::Prewarm(int count) {
    if (!HasSource()) return;
    Prune();
    while (count-- > 0 && (int)m_idle.size() < maxSize) {
        GameObject* go = Make();
        if (!go) return;
        go->active = false;   // never seen live: its first Get starts it
        m_idle.push_back(go->InstanceId());
    }
}

GameObject* ObjectPool::Get() {
    if (!HasSource()) return nullptr;
    Prune();
    GameObject* go = nullptr;
    // Newest first. An object switched off this frame has not had its OnDisable yet:
    // reusing it now would skip the reset, so it waits for the next frame.
    for (std::size_t i = m_idle.size(); i-- > 0;) {
        GameObject* c = GameObject::FromInstanceId(m_idle[i]);
        if (c && !c->HasLiveComponent()) {
            go = c;
            m_idle.erase(m_idle.begin() + (long)i);
            ++m_reused;
            break;
        }
    }
    if (!go) go = Make();
    if (!go) return nullptr;
    Transform* t = go->transform;
    if (t->Parent()) t->SetParent(nullptr, /*worldPositionStays=*/false);
    t->localPosition = m_pos;
    t->localRotation = Quat{m_rotX, m_rotY, m_rotZ, m_rotW};
    t->localScale = m_scale;
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
    if (!Owns(go) || go->scene() != m_scene) return false;
    const std::uint64_t id = go->InstanceId();
    if (std::find(m_idle.begin(), m_idle.end(), id) != m_idle.end()) return true;   // already idle
    Prune();
    if ((int)m_idle.size() >= maxSize) {
        m_owned.erase(id);
        m_scene->Destroy(go);
        return true;
    }
    if (go->transform->Parent()) go->transform->SetParent(nullptr, /*worldPositionStays=*/false);
    go->active = false;   // OnDisable follows at the start of the next frame
    m_idle.push_back(id);
    return true;
}

void ObjectPool::Clear() {
    Prune();
    for (std::uint64_t id : m_idle) {
        if (GameObject* g = GameObject::FromInstanceId(id)) { m_owned.erase(id); m_scene->Destroy(g); }
    }
    m_idle.clear();
}

int ObjectPool::IdleCount() const { Prune(); return (int)m_idle.size(); }
int ObjectPool::ActiveCount() const { Prune(); return (int)m_owned.size() - (int)m_idle.size(); }

} // namespace okay
