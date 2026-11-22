// Level streaming: additive loads of whole scenes without a loading screen. See LevelStreaming.hpp and
// docs/engine/world_streaming.md.
#include "okay/Components/LevelStreaming.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/StreamingSource.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneManager.hpp"
#include "okay/Scene/SceneStreamQueue.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scripting/ScriptVM.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <map>

namespace okay {

namespace {

/// The custom event a script or graph listens for (On Custom Event "LevelLoaded", OnEvent_LevelLoaded).
void Deliver(std::uint64_t objId, const std::string& ev, const std::string& level) {
    GameObject* o = objId ? GameObject::FromInstanceId(objId) : nullptr;
    if (!o || o->IsDestroyed()) return;
    const vs::VsValue v{std::string(level)};
    for (auto* vc : o->GetComponents<VisualScriptComponent>()) vc->SendEvent(ev, v, nullptr);
    for (auto* sc : o->GetComponents<ScriptComponent>())
        if (sc->VM()) sc->VM()->CallEventWith("OnEvent_" + ev, v);
}

/// Per scene: the streamed levels and the queue that brings them in. Lives on a hidden, never-saved object,
/// so it goes away with the scene (a replacing load clears it and the levels with it).
class LevelStreamManager : public Behaviour {
public:
    struct Level {
        std::string path;
        int ticket = 0;
        std::uint64_t notify = 0;
        std::vector<std::function<void(bool)>> waiting;
        bool reported = false;
    };
    std::map<std::string, Level> levels;   // by the name asked for
    SceneStreamQueue queue;

    int ExecutionOrder() const override { return -50; }   // before most gameplay: a level is reported early in the frame

    void Update(float) override {
        Scene* s = GetScene();
        if (!s) return;
        queue.Pump(*s);
        std::vector<std::pair<std::string, bool>> report;
        for (auto& kv : levels) {
            Level& l = kv.second;
            if (l.reported) continue;
            const StreamLoadState st = queue.State(l.ticket);
            if (st == StreamLoadState::Loaded) report.push_back({kv.first, true});
            else if (st == StreamLoadState::Failed || st == StreamLoadState::Unknown) report.push_back({kv.first, false});
        }
        for (const auto& r : report) {
            auto f = levels.find(r.first);
            if (f == levels.end()) continue;
            f->second.reported = true;
            auto waiting = std::move(f->second.waiting);
            const std::uint64_t notify = f->second.notify;
            if (!r.second) {
                Log::Warning("[level streaming] could not load '", r.first, "'");
                queue.Release(*s, f->second.ticket);
                levels.erase(f);
            } else {
                Log::Info("[level streaming] '", r.first, "' is in");
            }
            for (auto& w : waiting) if (w) w(r.second);
            Deliver(notify, r.second ? "LevelLoaded" : "LevelFailed", r.first);
        }
    }
    void OnDestroy() override {
        if (Scene* s = GetScene()) queue.Clear(*s);
        levels.clear();
    }

    static LevelStreamManager* Find(const Scene& scene) {
        for (LevelStreamManager* m : scene.FindObjectsOfType<LevelStreamManager>())
            if (m && m->gameObject && !m->gameObject->IsDestroyed()) return m;
        return nullptr;
    }
    static LevelStreamManager& Get(Scene& scene) {
        if (LevelStreamManager* m = Find(scene)) return *m;
        GameObject* g = scene.CreateGameObject("Level Streaming");
        g->transient = true;
        return *g->AddComponent<LevelStreamManager>();
    }
};

std::string Key(const std::string& nameOrPath) { return nameOrPath; }

}  // namespace

std::string LevelStreaming::Resolve(const std::string& nameOrPath, const std::string& sceneDir) {
    namespace fs = std::filesystem;
    if (nameOrPath.empty()) return {};
    std::error_code ec;
    std::vector<std::string> tries;
    const int idx = SceneManager::IndexOf(nameOrPath);
    if (idx >= 0) tries.push_back(SceneManager::Scenes()[(std::size_t)idx]);
    tries.push_back(nameOrPath);
    tries.push_back(nameOrPath + ".okayscene");
    if (!sceneDir.empty() && fs::path(nameOrPath).is_relative()) {
        tries.push_back((fs::path(sceneDir) / nameOrPath).generic_string());
        tries.push_back((fs::path(sceneDir) / (nameOrPath + ".okayscene")).generic_string());
    }
    for (const std::string& t : tries)
        if (fs::is_regular_file(t, ec)) return t;
    return {};
}

int LevelStreaming::LoadLevelAsync(Scene& scene, const std::string& nameOrPath, const Vec3& offset,
                                   std::function<void(bool)> onDone, GameObject* notify, const std::string& sceneDir) {
    LevelStreamManager& m = LevelStreamManager::Get(scene);
    auto f = m.levels.find(Key(nameOrPath));
    if (f != m.levels.end()) {
        if (f->second.reported) { if (onDone) onDone(true); if (notify) Deliver(notify->InstanceId(), "LevelLoaded", nameOrPath); }
        else { f->second.waiting.push_back(std::move(onDone)); if (notify) f->second.notify = notify->InstanceId(); }
        return f->second.ticket;
    }
    const std::string path = Resolve(nameOrPath, sceneDir);
    if (path.empty()) {
        Log::Warning("[level streaming] no level called '", nameOrPath, "'");
        if (onDone) onDone(false);
        if (notify) Deliver(notify->InstanceId(), "LevelFailed", nameOrPath);
        return 0;
    }
    LevelStreamManager::Level l;
    l.path = path;
    l.notify = notify ? notify->InstanceId() : 0;
    if (onDone) l.waiting.push_back(std::move(onDone));
    l.ticket = m.queue.RequestFile(path, offset);
    const int t = l.ticket;
    m.levels[Key(nameOrPath)] = std::move(l);
    return t;
}

bool LevelStreaming::UnloadLevel(Scene& scene, const std::string& nameOrPath, GameObject* notify) {
    LevelStreamManager* m = LevelStreamManager::Find(scene);
    if (!m) return false;
    auto f = m->levels.find(Key(nameOrPath));
    if (f == m->levels.end()) return false;
    const bool wasIn = f->second.reported;
    auto waiting = std::move(f->second.waiting);
    m->queue.Release(scene, f->second.ticket);
    m->levels.erase(f);
    for (auto& w : waiting) if (w) w(false);   // a load stopped half way
    if (wasIn && notify) Deliver(notify->InstanceId(), "LevelUnloaded", nameOrPath);
    return true;
}

bool LevelStreaming::IsLevelLoaded(const Scene& scene, const std::string& nameOrPath) {
    const LevelStreamManager* m = LevelStreamManager::Find(scene);
    if (!m) return false;
    auto f = m->levels.find(Key(nameOrPath));
    return f != m->levels.end() && m->queue.State(f->second.ticket) == StreamLoadState::Loaded && f->second.reported;
}

bool LevelStreaming::IsLevelLoading(const Scene& scene, const std::string& nameOrPath) {
    const LevelStreamManager* m = LevelStreamManager::Find(scene);
    if (!m) return false;
    auto f = m->levels.find(Key(nameOrPath));
    return f != m->levels.end() && !f->second.reported;
}

float LevelStreaming::LevelProgress(const Scene& scene, const std::string& nameOrPath) {
    if (IsLevelLoaded(scene, nameOrPath)) return 1.0f;
    return IsLevelLoading(scene, nameOrPath) ? 0.5f : 0.0f;
}

std::vector<std::string> LevelStreaming::Levels(const Scene& scene) {
    std::vector<std::string> out;
    if (const LevelStreamManager* m = LevelStreamManager::Find(scene))
        for (const auto& kv : m->levels) out.push_back(kv.first);
    return out;
}

std::vector<std::uint64_t> LevelStreaming::LevelRoots(const Scene& scene, const std::string& nameOrPath) {
    const LevelStreamManager* m = LevelStreamManager::Find(scene);
    if (!m) return {};
    auto f = m->levels.find(Key(nameOrPath));
    return f == m->levels.end() ? std::vector<std::uint64_t>{} : m->queue.Roots(f->second.ticket);
}

void LevelStreaming::SetAttachBudgetMs(Scene& scene, float ms) {
    LevelStreamManager::Get(scene).queue.attachBudgetMs = ms > 0.05f ? ms : 0.05f;
}

// ---- LevelStreamingVolume -------------------------------------------------------------------------------

LevelStreamingVolume::LevelStreamingVolume() : sceneDir(CurrentSceneLoadDirectory()) {}

float LevelStreamingVolume::NearestSourceDistance() const {
    Scene* s = GetScene();
    float best = std::numeric_limits<float>::max();
    if (!s || !gameObject || !gameObject->transform) return best;
    std::vector<Vec3> pts;
    Transform* t = nullptr;
    if (!target.empty()) { if (GameObject* go = s->Find(target)) t = go->transform; }
    else if (s->mainCamera && s->mainCamera->gameObject) t = s->mainCamera->gameObject->transform;
    if (t) pts.push_back(t->Position());
    if (followSources) for (const auto& p : StreamingSource::Collect(s)) pts.push_back(p.position);
    const Vec3 c = gameObject->transform->Position();
    const Vec3 h{std::fabs(size.x) * 0.5f, std::fabs(size.y) * 0.5f, std::fabs(size.z) * 0.5f};
    for (const Vec3& p : pts) {
        auto gap = [](float v, float lo, float hi) { return v < lo ? lo - v : (v > hi ? v - hi : 0.0f); };
        const float dx = gap(p.x, c.x - h.x, c.x + h.x), dy = gap(p.y, c.y - h.y, c.y + h.y), dz = gap(p.z, c.z - h.z, c.z + h.z);
        best = std::min(best, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    return best;
}

bool LevelStreamingVolume::IsLoaded() const {
    const Scene* s = GetScene();
    return s && !level.empty() && LevelStreaming::IsLevelLoaded(*s, level);
}

bool LevelStreamingVolume::IsLoading() const {
    const Scene* s = GetScene();
    return s && !level.empty() && LevelStreaming::IsLevelLoading(*s, level);
}

void LevelStreamingVolume::Update(float) {
    Scene* s = GetScene();
    if (!s || level.empty()) return;
    const float d = NearestSourceDistance();
    const float pre = preloadDistance < 0.0f ? 0.0f : preloadDistance;
    const float margin = unloadMargin < 0.0f ? 0.0f : unloadMargin;
    if (!m_wanted && d <= pre) {
        m_wanted = true;
        ++loadCount;
        const std::uint64_t me = gameObject ? gameObject->InstanceId() : 0;
        LevelStreaming::LoadLevelAsync(*s, level, offset, [me](bool ok) {
            GameObject* g = GameObject::FromInstanceId(me);
            if (!g) return;
            for (LevelStreamingVolume* v : g->GetComponents<LevelStreamingVolume>())
                if (v->onLoaded) v->onLoaded(ok);
        }, gameObject, sceneDir);
    } else if (m_wanted && !keepLoaded && d > pre + margin) {
        m_wanted = false;
        ++unloadCount;
        if (LevelStreaming::UnloadLevel(*s, level, gameObject) && onUnloaded) onUnloaded();
    }
}

void LevelStreamingVolume::OnDestroy() {
    // A volume that goes away (its own level unloading it, the scene closing) leaves the level as it is.
    m_wanted = false;
}

} // namespace okay
