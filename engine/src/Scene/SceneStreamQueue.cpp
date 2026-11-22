// Streaming scene documents into a running scene. See SceneStreamQueue.hpp.
#include "okay/Scene/SceneStreamQueue.hpp"
#include "okay/Core/Jobs.hpp"
#include "okay/Core/DataPack.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include <chrono>
#include <deque>
#include <map>

namespace okay {

namespace {

/// What a worker leaves behind. Written by the job, read by the main thread only once the job is done.
struct ReadSlot {
    std::unique_ptr<SceneLoadJob> job;
    std::string error;
    double ms = 0.0;
};

void ReadInto(ReadSlot& slot, bool fromText, const std::string& pathOrText, const Vec3& offset) {
    const auto t0 = std::chrono::steady_clock::now();
    if (fromText) {
        std::string text = DataPack::Unpack(pathOrText);
        if (text.empty() && DataPack::IsPacked(pathOrText)) slot.error = "unreadable snapshot";
        else slot.job = SceneLoadJob::OpenText(std::move(text), /*additive=*/true, offset);
    } else {
        slot.job = SceneLoadJob::OpenFile(pathOrText, /*additive=*/true, offset, &slot.error);
    }
    slot.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

}  // namespace

struct SceneStreamQueue::Impl {
    struct Item {
        StreamLoadState state = StreamLoadState::Reading;
        Done onDone;
        JobHandle read;
        std::shared_ptr<ReadSlot> slot;
        std::unique_ptr<SceneLoadJob> job;     // read, not yet handed to the scene
        int sceneHandle = 0;                   // the scene's async load (Attaching / Loaded through the scene)
        std::vector<std::uint64_t> roots;      // Loaded
        bool ownRoots = false;                 // made by Flush: release destroys the roots itself
    };
    std::map<int, Item> items;
    std::deque<int> waiting;                   // read, in the order the reads were requested
    int next = 1;
    SceneStreamStats stats;

    int Start(SceneStreamQueue& q, bool fromText, std::string pathOrText, const Vec3& offset, Done onDone) {
        const int id = next++;
        Item& it = items[id];
        it.onDone = std::move(onDone);
        it.slot = std::make_shared<ReadSlot>();
        ++stats.requested;
        if (q.background) {
            std::shared_ptr<ReadSlot> slot = it.slot;
            JobOptions o;
            o.name = "stream read";
            it.read = EngineJobs().Submit([slot, fromText, pathOrText, offset]() { ReadInto(*slot, fromText, pathOrText, offset); }, o);
        } else {
            ReadInto(*it.slot, fromText, pathOrText, offset);
        }
        return id;
    }

    void Fail(int id) {
        auto f = items.find(id);
        if (f == items.end()) return;
        f->second.state = StreamLoadState::Failed;
        f->second.job.reset();
        ++stats.failed;
        Done d = std::move(f->second.onDone);
        if (d) d(id, false);
    }

    void Finish(int id, std::vector<std::uint64_t> roots) {
        auto f = items.find(id);
        if (f == items.end()) return;
        f->second.state = StreamLoadState::Loaded;
        f->second.roots = std::move(roots);
        ++stats.finished;
        Done d = std::move(f->second.onDone);
        if (d) d(id, true);
    }

    /// Reads that are done move on to the waiting line (or fail).
    void CollectReads() {
        std::vector<int> done;
        for (auto& kv : items) {
            Item& it = kv.second;
            if (it.state != StreamLoadState::Reading) continue;
            if (it.read.Valid() && !it.read.IsDone()) continue;
            done.push_back(kv.first);
        }
        for (int id : done) {
            auto f = items.find(id);
            if (f == items.end()) continue;
            Item& it = f->second;
            stats.readMs += it.slot ? it.slot->ms : 0.0;
            if (it.slot && it.slot->job && !it.read.Failed()) {
                it.job = std::move(it.slot->job);
                it.slot.reset();
                it.state = StreamLoadState::Waiting;
                waiting.push_back(id);
            } else {
                it.slot.reset();
                Fail(id);
            }
        }
    }
};

SceneStreamQueue::SceneStreamQueue() : m(std::make_unique<Impl>()) {}
SceneStreamQueue::~SceneStreamQueue() = default;

int SceneStreamQueue::RequestFile(const std::string& path, const Vec3& offset, Done onDone) {
    return m->Start(*this, false, path, offset, std::move(onDone));
}

int SceneStreamQueue::RequestText(std::string text, const Vec3& offset, Done onDone) {
    return m->Start(*this, true, std::move(text), offset, std::move(onDone));
}

void SceneStreamQueue::Pump(Scene& scene) {
    m->CollectReads();
    // Documents being made: finished, or gone (a replacing scene load cleared them).
    int attaching = 0;
    std::vector<int> ids;
    for (auto& kv : m->items) if (kv.second.state == StreamLoadState::Attaching) ids.push_back(kv.first);
    for (int id : ids) {
        auto f = m->items.find(id);
        if (f == m->items.end()) continue;
        const int h = f->second.sceneHandle;
        if (scene.IsLoaded(h)) m->Finish(id, scene.LoadedRoots(h));
        else if (!scene.IsLoading(h)) { f->second.sceneHandle = 0; m->Fail(id); }
        else ++attaching;
    }
    // Start the next documents.
    const int cap = maxAttaching < 1 ? 1 : maxAttaching;
    while (attaching < cap && !m->waiting.empty()) {
        const int id = m->waiting.front();
        m->waiting.pop_front();
        auto f = m->items.find(id);
        if (f == m->items.end() || f->second.state != StreamLoadState::Waiting) continue;
        Scene::AsyncLoadOptions o;
        o.objectsPerFrame = objectsPerStep < 1 ? 1 : objectsPerStep;
        o.timeBudgetMs = attachBudgetMs;
        const int h = scene.BeginAsyncLoad(std::move(f->second.job), std::move(o));
        if (h == 0) { m->Fail(id); continue; }
        f->second.sceneHandle = h;
        f->second.state = StreamLoadState::Attaching;
        ++attaching;
    }
}

void SceneStreamQueue::Flush(Scene& scene) {
    for (auto& kv : m->items)
        if (kv.second.state == StreamLoadState::Reading && kv.second.read.Valid()) EngineJobs().WaitFor(kv.second.read, 60.0);
    m->CollectReads();
    while (!m->waiting.empty()) {
        const int id = m->waiting.front();
        m->waiting.pop_front();
        auto f = m->items.find(id);
        if (f == m->items.end() || f->second.state != StreamLoadState::Waiting) continue;
        std::unique_ptr<SceneLoadJob> job = std::move(f->second.job);
        std::string err;
        if (!job->Step(scene, 0, &err) || !job->Finished()) { m->Fail(id); continue; }
        f->second.ownRoots = true;
        m->Finish(id, job->RootIds());
    }
}

bool SceneStreamQueue::Release(Scene& scene, int ticket) {
    auto f = m->items.find(ticket);
    if (f == m->items.end()) return false;
    Impl::Item it = std::move(f->second);
    m->items.erase(f);
    ++m->stats.released;
    if (it.state == StreamLoadState::Reading && it.read.Valid()) EngineJobs().Cancel(it.read);   // a read already running just finishes into its slot
    if (it.sceneHandle) scene.UnloadAdditive(it.sceneHandle);
    else if (it.ownRoots)
        for (std::uint64_t id : it.roots)
            if (GameObject* go = GameObject::FromInstanceId(id))
                if (go->scene() == &scene) scene.Destroy(go);
    return true;
}

void SceneStreamQueue::Clear(Scene& scene) {
    std::vector<int> ids;
    for (auto& kv : m->items) ids.push_back(kv.first);
    for (int id : ids) Release(scene, id);
    m->waiting.clear();
}

StreamLoadState SceneStreamQueue::State(int ticket) const {
    auto f = m->items.find(ticket);
    return f == m->items.end() ? StreamLoadState::Unknown : f->second.state;
}

std::vector<std::uint64_t> SceneStreamQueue::Roots(int ticket) const {
    auto f = m->items.find(ticket);
    if (f == m->items.end() || f->second.state != StreamLoadState::Loaded) return {};
    return f->second.roots;
}

bool SceneStreamQueue::Busy() const {
    for (const auto& kv : m->items) {
        const StreamLoadState s = kv.second.state;
        if (s == StreamLoadState::Reading || s == StreamLoadState::Waiting || s == StreamLoadState::Attaching) return true;
    }
    return false;
}

SceneStreamStats SceneStreamQueue::Stats() const {
    SceneStreamStats s = m->stats;
    s.reading = s.waiting = s.attaching = s.loaded = 0;
    for (const auto& kv : m->items) {
        switch (kv.second.state) {
            case StreamLoadState::Reading: ++s.reading; break;
            case StreamLoadState::Waiting: ++s.waiting; break;
            case StreamLoadState::Attaching: ++s.attaching; break;
            case StreamLoadState::Loaded: ++s.loaded; break;
            default: break;
        }
    }
    return s;
}

} // namespace okay
