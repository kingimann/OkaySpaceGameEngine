#include "okay/Core/AssetManager.hpp"
#include "okay/Core/FileStamp.hpp"
#include "okay/Audio/AudioClip.hpp"
#include "okay/Core/DataAsset.hpp"
#include "okay/Graphics/Image.hpp"
#include "okay/Modeling/MeshAsset.hpp"
#include "okay/Render/Material.hpp"
#include "okay/Render/Mesh.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <cstdlib>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif
#include "CoreThreads.hpp"   // our own Mutex/CondVar (no <mutex> under MinGW win32 threads)

namespace okay {

struct AssetManager::Sync {
    corethreads::Mutex mutex;
    corethreads::CondVar entryCv;   // an entry left Loading
};

namespace {
// A movable-state lock over corethreads::Mutex: lock()/unlock() like the standard one,
// plus a predicate wait on a CondVar (re-checks pred after every wake, so no lost wakeups).
class Lock {
public:
    explicit Lock(corethreads::Mutex& m) : m_m(m) { m_m.Lock(); m_held = true; }
    ~Lock() { if (m_held) m_m.Unlock(); }
    void lock() { m_m.Lock(); m_held = true; }
    void unlock() { m_held = false; m_m.Unlock(); }
    template <class Pred> void wait(corethreads::CondVar& cv, Pred pred) { while (!pred()) cv.Wait(m_m); }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
private:
    corethreads::Mutex& m_m;
    bool m_held = false;
};
} // namespace

using assets_detail::Entry;
using assets_detail::State;

namespace {
int S(State s) { return static_cast<int>(s); }

std::int64_t NowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Modification time and size of a file (size -1 when only the time can be read); false without a time
// (okay/Core/FileStamp.hpp).
bool StatFile(const std::string& path, std::int64_t& stamp, std::int64_t& size) {
    const auto s = StampOf(path);
    if (!s.timeOk) return false;
    stamp = s.time;
    size = s.sizeOk ? static_cast<std::int64_t>(s.size) : -1;
    return true;
}

std::shared_ptr<void> RunLoader(const std::function<std::shared_ptr<void>(const std::string&, std::string&)>& fn,
                                const std::string& path, std::string& error) {
    try {
        std::shared_ptr<void> d = fn(path, error);
        if (!d && error.empty()) error = "load failed: " + path;
        return d;
    } catch (const std::exception& ex) {
        error = std::string("load threw: ") + ex.what();
    } catch (...) {
        error = "load threw";
    }
    return nullptr;
}
} // namespace

// --- Paths -------------------------------------------------------------------------

std::string AssetManager::Normalize(const std::string& path, bool foldCase) {
    std::string s = path;
    for (char& c : s) if (c == '\\') c = '/';
    std::vector<std::string> parts;
    std::string cur;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == '/') { parts.push_back(cur); cur.clear(); }
        else cur += s[i];
    }
    bool absolute = !s.empty() && s[0] == '/';
    std::string drive;
    std::size_t first = absolute ? 1 : 0;
    if (!parts.empty() && parts[0].size() == 2 && parts[0][1] == ':' &&
        ((parts[0][0] >= 'a' && parts[0][0] <= 'z') || (parts[0][0] >= 'A' && parts[0][0] <= 'Z'))) {
        drive = parts[0];
        absolute = true;
        first = 1;
    }
    std::vector<std::string> out;
    for (std::size_t i = first; i < parts.size(); ++i) {
        const std::string& p = parts[i];
        if (p.empty() || p == ".") continue;
        if (p == "..") {
            if (!out.empty() && out.back() != "..") out.pop_back();
            else if (!absolute) out.push_back("..");
            continue;                       // above the root of an absolute path: dropped
        }
        out.push_back(p);
    }
    std::string r = drive;
    if (absolute) r += "/";
    for (std::size_t i = 0; i < out.size(); ++i) { if (i) r += "/"; r += out[i]; }
    if (r.empty() && !path.empty()) r = ".";
    if (foldCase)
        for (char& c : r) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return r;
}

std::string AssetManager::Normalize(const std::string& path) {
#ifdef _WIN32
    return Normalize(path, true);
#else
    return Normalize(path, false);
#endif
}

// --- Lifetime ------------------------------------------------------------------------

AssetManager::AssetManager() : m_sync(new Sync) {}

AssetManager::~AssetManager() {
    std::vector<Drain> drains;
    {
        corethreads::LockGuard lk(m_sync->mutex);
        m_stop = true;   // a running drain stops after its current file and drops the result
        drains.swap(m_drains);
    }
    // No job may touch this object after it is gone: a drain that has not started is
    // cancelled, one that is running is waited for (it finishes the file it is decoding).
    for (Drain& d : drains) {
        if (!d.sys || d.job.IsDone() || d.sys->Cancel(d.job)) continue;
        try { d.sys->Wait(d.job); } catch (...) {}
    }
}

AssetManager& AssetManager::Instance() {
    static AssetManager* inst = [] { auto* m = new AssetManager(); m->RegisterDefaultLoaders(); return m; }();
    return *inst;   // intentionally leaked: handles may outlive static destruction
}

void AssetManager::AddLoader(std::type_index t, LoaderRec r) {
    corethreads::LockGuard lk(m_sync->mutex);
    m_loaders[t] = std::move(r);
}

bool AssetManager::LoaderFor(std::type_index t, LoaderRec& out) const {
    corethreads::LockGuard lk(m_sync->mutex);
    auto it = m_loaders.find(t);
    if (it == m_loaders.end()) return false;
    out = it->second;
    return true;
}

namespace {
std::size_t MeshBytes(const Mesh& m) {
    return m.vertices.size() * sizeof(m.vertices[0]) + m.triangles.size() * sizeof(int) +
           m.uvs.size() * sizeof(m.uvs[0]) + m.normals.size() * sizeof(m.normals[0]);
}
} // namespace

void AssetManager::RegisterDefaultLoaders() {
    RegisterLoader<Image>("Image",
        [](const std::string& p, std::string& e) -> std::shared_ptr<Image> {
            auto a = std::make_shared<Image>();
            return a->Load(p, &e) ? a : nullptr;
        },
        [](const Image& a) { return a.Pixels().size(); });
    RegisterLoader<AudioClip>("AudioClip",
        [](const std::string& p, std::string& e) -> std::shared_ptr<AudioClip> {
            auto a = std::make_shared<AudioClip>();
            return a->Load(p, &e) ? a : nullptr;
        },
        [](const AudioClip& a) { return a.samples.size() * sizeof(float); });
    RegisterLoader<Mesh>("Mesh",
        [](const std::string& p, std::string& e) -> std::shared_ptr<Mesh> {
            auto a = std::make_shared<Mesh>();
            return modeling::LoadMeshAsset(p, *a, nullptr, &e) ? a : nullptr;
        },
        [](const Mesh& m) { return sizeof(Mesh) + MeshBytes(m); });
    // Mesh files as the streamer keeps them (geometry + an OBJ's texture); LoadMeshAsset copies from here.
    RegisterLoader<modeling::MeshFile>("MeshFile",
        [](const std::string& p, std::string& e) -> std::shared_ptr<modeling::MeshFile> {
            auto a = std::make_shared<modeling::MeshFile>();
            return modeling::DecodeMeshFile(p, *a, &e) ? a : nullptr;
        },
        [](const modeling::MeshFile& f) { return sizeof(modeling::MeshFile) + MeshBytes(f.mesh) + f.texture.size(); });
    RegisterLoader<DataAsset>("DataAsset",
        [](const std::string& p, std::string& e) -> std::shared_ptr<DataAsset> {
            auto a = std::make_shared<DataAsset>();
            if (a->Load(p)) return a;
            e = "cannot read " + p;
            return nullptr;
        },
        [](const DataAsset& a) {
            std::size_t n = sizeof(DataAsset);
            for (const auto& f : a.Fields()) n += f.first.size() + f.second.size();
            return n;
        });
    RegisterLoader<Material>("Material",
        [](const std::string& p, std::string& e) -> std::shared_ptr<Material> {
            auto a = std::make_shared<Material>();
            if (Material::LoadFromFile(p, *a)) return a;
            e = "cannot read " + p;
            return nullptr;
        });
}

// --- Loading -------------------------------------------------------------------------

bool AssetManager::NeedDrainLocked() {
    if (m_stop || m_draining || m_jobs.empty()) return false;
    m_draining = true;
    return true;
}

void AssetManager::KickDrain() {
    JobSystem& sys = EngineJobs();
    {
        corethreads::LockGuard lk(m_sync->mutex);
        if (!NeedDrainLocked()) return;
        // Forget drains that are over, so the list stays short.
        m_drains.erase(std::remove_if(m_drains.begin(), m_drains.end(), [](const Drain& d) { return d.job.IsDone(); }),
                       m_drains.end());
    }
    // Submitted without the lock: an inline JobSystem(0) runs the drain right here.
    JobOptions opt;
    opt.priority = JobPriority::Low;   // frame work (physics, skinning) goes first
    opt.name = "AssetManager decode";
    JobHandle h;
    try {
        h = sys.Submit([this] { DrainQueue(); }, opt);
    } catch (...) {
        corethreads::LockGuard lk(m_sync->mutex);
        m_draining = false;   // could not queue it; the next LoadAsync or Pump tries again
        return;
    }
    corethreads::LockGuard lk(m_sync->mutex);
    m_drains.push_back(Drain{&sys, h});
}

// One drain job decodes everything queued, one file at a time in queue order (as the old
// dedicated worker thread did), then ends; the next LoadAsync or hot reload starts another.
void AssetManager::DrainQueue() {
    Lock lk(m_sync->mutex);
    for (;;) {
        if (m_stop || m_jobs.empty()) { m_draining = false; return; }
        // Highest priority first; equal priorities in the order they were asked for.
        std::size_t pick = 0;
        for (std::size_t i = 1; i < m_jobs.size(); ++i) if (m_jobs[i].priority > m_jobs[pick].priority) pick = i;
        Job job = std::move(m_jobs[pick]);
        m_jobs.erase(m_jobs.begin() + static_cast<std::ptrdiff_t>(pick));
        if (job.streamed && !job.reload && job.entry.use_count() == 1 && job.entry->state.load() == S(State::Pending)) {
            // Streamed, and nobody holds it any more (CancelStream took it out of the cache): skip the decode.
            ++m_cancelled;
            job.entry->error = "cancelled";
            FinishFailedLocked(job.entry);
            Done d; d.entry = job.entry; d.counted = true;
            m_done.push_back(std::move(d));
            continue;
        }
        Done d;
        d.entry = job.entry;
        d.reload = job.reload;
        d.counted = !job.reload;
        auto it = m_loaders.find(job.entry->type);
        Entry& e = *job.entry;
        if (job.reload) {
            if (it == m_loaders.end()) { d.error = "no loader registered"; }
            else {
                auto fn = it->second.load;
                lk.unlock();
                StatFile(e.path, d.stamp, d.size);   // before decoding: a later write is seen next scan
                d.data = RunLoader(fn, e.path, d.error);
                lk.lock();
                if (m_stop) { m_draining = false; return; }
            }
        } else if (e.state.load() == S(State::Pending)) {
            // Claim it, so a sync Get waits for this decode instead of repeating it.
            if (it == m_loaders.end()) {
                e.error = "no loader registered";
                FinishFailedLocked(job.entry);
            } else {
                auto rec = it->second;
                e.state = S(State::Loading);
                lk.unlock();
                std::int64_t st = 0, sz = -1;
                StatFile(e.path, st, sz);
                std::string err;
                std::shared_ptr<void> data = RunLoader(rec.load, e.path, err);
                lk.lock();
                if (m_stop) { m_draining = false; return; }
                if (data) {
                    e.data = data;
                    e.bytes = rec.size(data.get());
                    e.stamp = st;
                    e.fileSize = sz;
                    e.state.store(S(State::Ready), std::memory_order_release);
                } else {
                    e.error = err;
                    FinishFailedLocked(job.entry);
                }
            }
            m_sync->entryCv.NotifyAll();
        }   // else a sync Get already claimed or finished it
        m_done.push_back(std::move(d));
    }
}

void AssetManager::FinishFailedLocked(const EntryPtr& e) {
    e->state.store(S(State::Failed), std::memory_order_release);
    auto it = m_cache.find(Key(e->type, e->key));
    if (it != m_cache.end() && (it->second.expired() || it->second.lock() == e)) m_cache.erase(it);
}

AssetManager::EntryPtr AssetManager::AcquireSync(std::type_index t, const std::string& path, std::string* error) {
    LoaderRec rec;
    if (!LoaderFor(t, rec)) {
        if (error) *error = "no loader registered for this asset type";
        return nullptr;
    }
    const std::string key = Normalize(path), open = Normalize(path, false);
    EntryPtr e;
    Lock lk(m_sync->mutex);
    for (;;) {
        auto& slot = m_cache[Key(t, key)];
        e = slot.lock();
        if (!e) {
            e = std::make_shared<Entry>(t, key, open);
            slot = e;
            e->state = S(State::Loading);
            break;
        }
        int s = e->state.load();
        if (s == S(State::Ready)) return e;
        if (s == S(State::Failed)) { slot.reset(); continue; }
        if (s == S(State::Pending)) { e->state = S(State::Loading); break; }   // claim it from the queue
        lk.wait(m_sync->entryCv, [&] { return e->state.load() != S(State::Loading); });
        if (e->state.load() == S(State::Ready)) return e;
        if (e->state.load() == S(State::Failed)) { if (error) *error = e->error; return nullptr; }
    }
    lk.unlock();

    std::int64_t stamp = 0, size = -1;
    StatFile(open, stamp, size);
    std::string err;
    std::shared_ptr<void> data = RunLoader(rec.load, open, err);

    lk.lock();
    if (data) {
        e->data = data;
        e->bytes = rec.size(data.get());
        e->stamp = stamp;
        e->fileSize = size;
        e->state.store(S(State::Ready), std::memory_order_release);
    } else {
        e->error = err;
        FinishFailedLocked(e);
    }
    lk.unlock();
    m_sync->entryCv.NotifyAll();
    if (!data) { if (error) *error = err; return nullptr; }
    return e;
}

AssetManager::EntryPtr AssetManager::AcquireAsync(std::type_index t, const std::string& path,
                                                  std::function<void(const EntryPtr&, const std::string&)> cb) {
    return AcquireAsyncImpl(t, path, std::move(cb), false, 0.0f);
}

AssetManager::EntryPtr AssetManager::AcquireStreamed(std::type_index t, const std::string& path, float priority,
                                                     std::function<void(const EntryPtr&, const std::string&)> cb) {
    return AcquireAsyncImpl(t, path, std::move(cb), true, priority);
}

AssetManager::EntryPtr AssetManager::AcquireAsyncImpl(std::type_index t, const std::string& path,
                                                      std::function<void(const EntryPtr&, const std::string&)> cb,
                                                      bool streamed, float priority) {
    LoaderRec rec;
    if (!LoaderFor(t, rec)) {
        // Report through Pump like any other failure.
        auto e = std::make_shared<Entry>(t, Normalize(path), Normalize(path, false));
        e->error = "no loader registered for this asset type";
        e->state = S(State::Failed);
        corethreads::LockGuard lk(m_sync->mutex);
        if (cb) e->waiters.push_back(std::move(cb));
        Done d; d.entry = e;
        m_done.push_back(std::move(d));
        return e;
    }
    const std::string key = Normalize(path), open = Normalize(path, false);
    EntryPtr e;
    bool kick = false;
    {
    corethreads::LockGuard lk(m_sync->mutex);
    for (;;) {
        auto& slot = m_cache[Key(t, key)];
        e = slot.lock();
        if (e && e->state.load() == S(State::Failed)) { slot.reset(); e.reset(); }
        if (!e) {
            e = std::make_shared<Entry>(t, key, open);
            slot = e;
            if (cb) e->waiters.push_back(std::move(cb));
            m_jobs.push_back(Job{e, false, streamed, priority});
            ++m_batchSubmitted;
            kick = true;
            break;
        }
        break;
    }
    if (streamed) {
        // Kept in the streaming cache at this priority; a queued decode moves to the new priority.
        Resident& r = m_resident[Key(t, key)];
        r.entry = e;
        r.priority = priority;
        r.lastWanted = m_streamFrame;
        if (!kick)
            for (Job& j : m_jobs) if (j.entry == e && !j.reload) { j.priority = priority; j.streamed = true; }
    }
    if (!kick) {
        if (cb) e->waiters.push_back(std::move(cb));
        int s = e->state.load();
        if (s == S(State::Ready) || s == S(State::Loading)) {
            // Ready: nothing to decode. Loading (sync in progress): Pump defers until it ends.
            if (cb || !e->waiters.empty()) { Done d; d.entry = e; m_done.push_back(std::move(d)); }
        }
    }
    }
    if (kick) KickDrain();   // after the lock: an inline job system decodes right here
    return e;
}

AssetManager::EntryPtr AssetManager::FindEntry(std::type_index t, const std::string& path) {
    corethreads::LockGuard lk(m_sync->mutex);
    auto it = m_cache.find(Key(t, Normalize(path)));
    if (it == m_cache.end()) return nullptr;
    EntryPtr e = it->second.lock();
    if (e && e->state.load() == S(State::Ready)) return e;
    return nullptr;
}

// --- Pump ----------------------------------------------------------------------------

int AssetManager::Pump() {
    int handled = 0;
    struct Fire { EntryPtr e; std::vector<std::function<void(const EntryPtr&, const std::string&)>> cbs; std::string err; };
    std::vector<Fire> fires;
    std::vector<std::pair<std::string, std::string>> reloaded;
    std::function<void(const std::string&, const std::string&)> onReload;
    {
        Lock lk(m_sync->mutex);
        onReload = m_onReload;
        // Hot reload scan.
        if (m_hot) {
            std::int64_t now = NowNs();
            if (m_lastScanNs == 0 || static_cast<double>(now - m_lastScanNs) * 1e-9 >= m_hotInterval) {
                m_lastScanNs = now;
                for (auto it = m_cache.begin(); it != m_cache.end();) {
                    EntryPtr e = it->second.lock();
                    if (!e) { it = m_cache.erase(it); continue; }
                    ++it;
                    if (e->state.load() != S(State::Ready) || e->reloading) continue;
                    std::int64_t st = 0, sz = -1;
                    if (!StatFile(e->path, st, sz)) continue;
                    if (st == e->stamp && sz == e->fileSize) continue;
                    e->reloading = true;
                    m_jobs.push_back(Job{e, true});
                }
            }
        }
        std::deque<Done> work;
        work.swap(m_done);
        std::vector<Done> deferred;
        for (Done& d : work) {
            Entry& e = *d.entry;
            if (d.reload) {
                e.reloading = false;
                LoaderRec rec;
                auto li = m_loaders.find(e.type);
                if (d.data && li != m_loaders.end() && e.state.load() == S(State::Ready)) {
                    li->second.assign(e.data.get(), d.data.get());
                    e.bytes = li->second.size(e.data.get());
                    e.stamp = d.stamp;
                    e.fileSize = d.size;
                    e.version.fetch_add(1);
                    reloaded.emplace_back(e.key, li->second.name);
                } else if (!d.data) {
                    e.stamp = d.stamp;          // failed re-decode: keep the old data, retry on the next change
                    e.fileSize = d.size;
                }
                ++handled;
                continue;
            }
            int s = e.state.load();
            if (s == S(State::Loading) || s == S(State::Pending)) { deferred.push_back(std::move(d)); continue; }   // a sync load is finishing
            if (d.counted) ++m_batchFinished;
            Fire f;
            f.e = d.entry;
            f.cbs = std::move(e.waiters);
            e.waiters.clear();
            f.err = e.state.load() == S(State::Ready) ? std::string() : e.error;
            if (!f.cbs.empty()) fires.push_back(std::move(f));
            ++handled;
        }
        for (Done& d : deferred) m_done.push_back(std::move(d));
        if (m_jobs.empty() && m_done.empty() && m_batchFinished >= m_batchSubmitted) m_batchSubmitted = m_batchFinished = 0;
    }
    std::vector<EntryPtr> freed;   // let go of after the lock: an asset's destructor may do real work
    {
        corethreads::LockGuard lk(m_sync->mutex);
        ++m_streamFrame;
        TrimLocked(freed);
    }
    freed.clear();
    m_sync->entryCv.NotifyAll();
    KickDrain();   // decode the changed files found by the hot reload scan (no-op when nothing is queued)
    for (Fire& f : fires)
        for (auto& cb : f.cbs) cb(f.e, f.err);
    if (onReload)
        for (auto& r : reloaded) onReload(r.first, r.second);
    return handled;
}

int AssetManager::PendingCount() const {
    corethreads::LockGuard lk(m_sync->mutex);
    return m_batchSubmitted - m_batchFinished;
}

float AssetManager::Progress() const {
    corethreads::LockGuard lk(m_sync->mutex);
    if (m_batchSubmitted <= 0) return 1.0f;
    return static_cast<float>(m_batchFinished) / static_cast<float>(m_batchSubmitted);
}

void AssetManager::SetHotReload(bool enabled, double intervalSeconds) {
    corethreads::LockGuard lk(m_sync->mutex);
    m_hot = enabled;
    m_hotInterval = intervalSeconds < 0 ? 0 : intervalSeconds;
    m_lastScanNs = 0;
}

void AssetManager::SetReloadCallback(std::function<void(const std::string&, const std::string&)> cb) {
    corethreads::LockGuard lk(m_sync->mutex);
    m_onReload = std::move(cb);
}

// --- Housekeeping --------------------------------------------------------------------

void AssetManager::Purge() {
    corethreads::LockGuard lk(m_sync->mutex);
    for (auto it = m_cache.begin(); it != m_cache.end();)
        it = it->second.expired() ? m_cache.erase(it) : std::next(it);
}

std::size_t AssetManager::CachedCount() const {
    corethreads::LockGuard lk(m_sync->mutex);
    std::size_t n = 0;
    for (const auto& kv : m_cache) if (!kv.second.expired()) ++n;
    return n;
}

// --- Streaming -----------------------------------------------------------------------

bool AssetManager::CancelStreamed(std::type_index t, const std::string& path) {
    EntryPtr dropped;   // freed after the lock
    corethreads::LockGuard lk(m_sync->mutex);
    auto it = m_resident.find(Key(t, Normalize(path)));
    if (it == m_resident.end()) return false;
    if (it->second.entry->state.load() != S(State::Pending)) return false;   // decoding or done: keep it cached
    dropped = std::move(it->second.entry);
    m_resident.erase(it);
    return true;
}

AssetManager::EntryPtr AssetManager::FindFreshEntry(std::type_index t, const std::string& path) {
    EntryPtr e = FindEntry(t, path);
    if (!e) return nullptr;
    std::int64_t st = 0, sz = -1;
    if (!StatFile(e->path, st, sz)) return nullptr;
    corethreads::LockGuard lk(m_sync->mutex);
    if (e->stamp == 0 || st != e->stamp || sz != e->fileSize) return nullptr;
    auto it = m_resident.find(Key(t, e->key));
    if (it != m_resident.end()) it->second.lastWanted = m_streamFrame;   // used again: the last to be let go of
    return e;
}

std::uint64_t AssetManager::SystemMemoryBytes() {
#if defined(_WIN32)
    MEMORYSTATUSEX m;
    m.dwLength = sizeof(m);
    if (GlobalMemoryStatusEx(&m)) return static_cast<std::uint64_t>(m.ullTotalPhys);
    return 0;
#elif defined(_SC_PHYS_PAGES) && defined(_SC_PAGESIZE)
    const long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGESIZE);
    if (pages <= 0 || page <= 0) return 0;
    return static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(page);
#else
    return 0;
#endif
}

namespace {
std::size_t EnvMB(const char* name) {
    const char* v = std::getenv(name);
    if (!v || !*v) return 0;
    const long long mb = std::atoll(v);
    return mb > 0 ? static_cast<std::size_t>(mb) * 1024u * 1024u : 0;
}
std::size_t Clamp(std::uint64_t v, std::uint64_t lo, std::uint64_t hi) {
    return static_cast<std::size_t>(v < lo ? lo : (v > hi ? hi : v));
}
constexpr std::uint64_t kMB = 1024ull * 1024ull;
} // namespace

void AssetManager::SetStreamSettings(const StreamSettings& st) {
    std::vector<EntryPtr> freed;
    {
        corethreads::LockGuard lk(m_sync->mutex);
        m_stream = st;
        if (!(m_stream.frameBudgetMs >= 0.0)) m_stream.frameBudgetMs = 0.0;
        TrimLocked(freed);
    }
}

StreamSettings AssetManager::GetStreamSettings() const {
    corethreads::LockGuard lk(m_sync->mutex);
    return m_stream;
}

std::size_t AssetManager::RamBudget() const {
    std::size_t custom = 0;
    {
        corethreads::LockGuard lk(m_sync->mutex);
        if (!m_stream.automatic) custom = m_stream.ramBudgetBytes;
    }
    if (custom) return custom;
    if (const std::size_t env = EnvMB("OKAY_STREAM_RAM_MB")) return env;
    const std::uint64_t sys = SystemMemoryBytes();
    if (sys == 0) return static_cast<std::size_t>(1024 * kMB);
    return Clamp(sys / 4, 256 * kMB, 8192 * kMB);
}

std::size_t AssetManager::VramBudget() const {
    std::size_t custom = 0;
    std::uint64_t reported = 0;
    {
        corethreads::LockGuard lk(m_sync->mutex);
        if (!m_stream.automatic) custom = m_stream.vramBudgetBytes;
        reported = m_videoMemory;
    }
    if (custom) return custom;
    if (const std::size_t env = EnvMB("OKAY_STREAM_VRAM_MB")) return env;
    if (reported) return static_cast<std::size_t>(reported / 5 * 4);
    const std::uint64_t sys = SystemMemoryBytes();
    if (sys == 0) return static_cast<std::size_t>(1024 * kMB);
    return Clamp(sys / 8, 512 * kMB, 2048 * kMB);
}

double AssetManager::FrameBudgetMs() const {
    corethreads::LockGuard lk(m_sync->mutex);
    return m_stream.frameBudgetMs;
}

void AssetManager::ReportVideoMemory(std::uint64_t bytes) {
    corethreads::LockGuard lk(m_sync->mutex);
    m_videoMemory = bytes;
}

StreamStats AssetManager::GetStreamStats() const {
    StreamStats st;
    st.ramBudget = RamBudget();
    st.vramBudget = VramBudget();
    st.systemMemory = static_cast<std::size_t>(SystemMemoryBytes());
    corethreads::LockGuard lk(m_sync->mutex);
    for (const auto& kv : m_resident) {
        st.residentBytes += kv.second.entry->bytes.load();
        ++st.resident;
    }
    st.queued = static_cast<int>(m_jobs.size());
    st.evicted = m_evicted;
    st.cancelled = m_cancelled;
    st.frame = m_streamFrame;
    return st;
}

int AssetManager::TrimToBudget() {
    std::vector<EntryPtr> freed;
    int n = 0;
    {
        corethreads::LockGuard lk(m_sync->mutex);
        n = TrimLocked(freed);
    }
    return n;
}

int AssetManager::TrimLocked(std::vector<EntryPtr>& freed) {
    // A streamed decode that failed (or was cancelled) is not kept: asking again tries again.
    for (auto it = m_resident.begin(); it != m_resident.end();)
        if (it->second.entry->state.load() == S(State::Failed)) { freed.push_back(std::move(it->second.entry)); it = m_resident.erase(it); }
        else ++it;
    if (m_resident.empty()) return 0;
    // The budget is read without the lock held by RamBudget(); work it out here from the same rules.
    std::size_t budget = (!m_stream.automatic && m_stream.ramBudgetBytes) ? m_stream.ramBudgetBytes : 0;
    if (!budget) budget = EnvMB("OKAY_STREAM_RAM_MB");
    if (!budget) {
        const std::uint64_t sys = SystemMemoryBytes();
        budget = sys ? Clamp(sys / 4, 256 * kMB, 8192 * kMB) : static_cast<std::size_t>(1024 * kMB);
    }
    std::size_t total = 0;
    for (const auto& kv : m_resident) total += kv.second.entry->bytes.load();
    if (total <= budget) return 0;
    // Only assets no one else holds can go (use_count 1: the cache's own reference); a decode in flight stays.
    std::vector<std::map<Key, Resident>::iterator> cands;
    for (auto it = m_resident.begin(); it != m_resident.end(); ++it)
        if (it->second.entry.use_count() == 1 && it->second.entry->state.load() != S(State::Loading) &&
            it->second.entry->state.load() != S(State::Pending))
            cands.push_back(it);
    std::stable_sort(cands.begin(), cands.end(), [](const auto& a, const auto& b) {
        if (a->second.priority != b->second.priority) return a->second.priority < b->second.priority;
        return a->second.lastWanted < b->second.lastWanted;
    });
    int n = 0;
    for (auto it : cands) {
        if (total <= budget) break;
        total -= std::min(total, it->second.entry->bytes.load());
        freed.push_back(std::move(it->second.entry));
        m_resident.erase(it);
        ++m_evicted;
        ++n;
    }
    return n;
}

void AssetManager::ResetStreamingForTests() {
    std::vector<EntryPtr> freed;
    corethreads::LockGuard lk(m_sync->mutex);
    for (auto& kv : m_resident) freed.push_back(std::move(kv.second.entry));
    m_resident.clear();
    m_stream = StreamSettings();
    m_videoMemory = 0;
    m_evicted = m_cancelled = 0;
}

AssetReport AssetManager::Report() const {
    AssetReport r;
    corethreads::LockGuard lk(m_sync->mutex);
    std::map<std::string, AssetReport::TypeTotal> totals;
    for (const auto& kv : m_cache) {
        EntryPtr e = kv.second.lock();
        if (!e) continue;
        auto li = m_loaders.find(e->type);
        AssetReport::Item it;
        it.key = e->key;
        it.type = li != m_loaders.end() ? li->second.name : e->type.name();
        it.bytes = e->bytes.load();
        it.refs = std::max<long>(0, e.use_count() - 1);
        it.version = e->version.load();
        int s = e->state.load();
        it.loading = s == S(State::Pending) || s == S(State::Loading);
        if (it.loading) ++r.loadingCount; else ++r.loadedCount;
        r.totalBytes += it.bytes;
        auto& tt = totals[it.type];
        tt.type = it.type;
        ++tt.count;
        tt.bytes += it.bytes;
        r.items.push_back(std::move(it));
    }
    for (auto& kv : totals) r.types.push_back(kv.second);
    std::sort(r.items.begin(), r.items.end(), [](const AssetReport::Item& a, const AssetReport::Item& b) {
        return a.bytes != b.bytes ? a.bytes > b.bytes : a.key < b.key;
    });
    return r;
}

std::string AssetReport::ToString() const {
    std::ostringstream o;
    o << "Assets: " << loadedCount << " loaded, " << loadingCount << " loading, " << totalBytes << " bytes\n";
    for (const auto& t : types) o << "  " << t.type << ": " << t.count << " (" << t.bytes << " bytes)\n";
    for (const auto& i : items)
        o << "  " << i.key << " [" << i.type << "] " << i.bytes << " bytes, refs " << i.refs
          << (i.loading ? ", loading" : "") << (i.version ? ", reloaded " + std::to_string(i.version) + "x" : "") << "\n";
    return o.str();
}

} // namespace okay
