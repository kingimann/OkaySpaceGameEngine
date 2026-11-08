#include "okay/Core/AssetManager.hpp"
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

namespace okay {
using assets_detail::Entry;
using assets_detail::State;

namespace {
int S(State s) { return static_cast<int>(s); }

std::int64_t NowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Modification time and size of a file; false if it can't be read.
bool StatFile(const std::string& path, std::int64_t& stamp, std::int64_t& size) {
    std::error_code ec;
    auto t = std::filesystem::last_write_time(std::filesystem::u8path(path), ec);
    if (ec) return false;
    auto sz = std::filesystem::file_size(std::filesystem::u8path(path), ec);
    stamp = static_cast<std::int64_t>(t.time_since_epoch().count());
    size = ec ? -1 : static_cast<std::int64_t>(sz);
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

AssetManager::AssetManager() = default;

AssetManager::~AssetManager() {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_stop = true;
    }
    m_jobCv.notify_all();
    if (m_worker.joinable()) m_worker.join();
}

AssetManager& AssetManager::Instance() {
    static AssetManager* inst = [] { auto* m = new AssetManager(); m->RegisterDefaultLoaders(); return m; }();
    return *inst;   // intentionally leaked: handles may outlive static destruction
}

void AssetManager::AddLoader(std::type_index t, LoaderRec r) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_loaders[t] = std::move(r);
}

bool AssetManager::LoaderFor(std::type_index t, LoaderRec& out) const {
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_loaders.find(t);
    if (it == m_loaders.end()) return false;
    out = it->second;
    return true;
}

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
        [](const Mesh& m) {
            return sizeof(Mesh) + m.vertices.size() * sizeof(m.vertices[0]) + m.triangles.size() * sizeof(int) +
                   m.uvs.size() * sizeof(m.uvs[0]) + m.normals.size() * sizeof(m.normals[0]);
        });
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

void AssetManager::StartWorkerLocked() {
    if (!m_worker.joinable()) m_worker = std::thread([this] { WorkerMain(); });
}

void AssetManager::WorkerMain() {
    std::unique_lock<std::mutex> lk(m_mutex);
    for (;;) {
        m_jobCv.wait(lk, [this] { return m_stop || !m_jobs.empty(); });
        if (m_stop) return;
        Job job = std::move(m_jobs.front());
        m_jobs.pop_front();
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
                if (m_stop) return;
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
                if (m_stop) return;
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
            m_entryCv.notify_all();
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
    std::unique_lock<std::mutex> lk(m_mutex);
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
        m_entryCv.wait(lk, [&] { return e->state.load() != S(State::Loading); });
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
    m_entryCv.notify_all();
    if (!data) { if (error) *error = err; return nullptr; }
    return e;
}

AssetManager::EntryPtr AssetManager::AcquireAsync(std::type_index t, const std::string& path,
                                                  std::function<void(const EntryPtr&, const std::string&)> cb) {
    LoaderRec rec;
    if (!LoaderFor(t, rec)) {
        // Report through Pump like any other failure.
        auto e = std::make_shared<Entry>(t, Normalize(path), Normalize(path, false));
        e->error = "no loader registered for this asset type";
        e->state = S(State::Failed);
        std::lock_guard<std::mutex> lk(m_mutex);
        if (cb) e->waiters.push_back(std::move(cb));
        Done d; d.entry = e;
        m_done.push_back(std::move(d));
        return e;
    }
    const std::string key = Normalize(path), open = Normalize(path, false);
    std::lock_guard<std::mutex> lk(m_mutex);
    EntryPtr e;
    for (;;) {
        auto& slot = m_cache[Key(t, key)];
        e = slot.lock();
        if (e && e->state.load() == S(State::Failed)) { slot.reset(); e.reset(); }
        if (!e) {
            e = std::make_shared<Entry>(t, key, open);
            slot = e;
            if (cb) e->waiters.push_back(std::move(cb));
            m_jobs.push_back(Job{e, false});
            ++m_batchSubmitted;
            StartWorkerLocked();
            m_jobCv.notify_one();
            return e;
        }
        break;
    }
    if (cb) e->waiters.push_back(std::move(cb));
    int s = e->state.load();
    if (s == S(State::Ready) || s == S(State::Loading)) {
        // Ready: nothing to decode. Loading (sync in progress): Pump defers until it ends.
        if (cb || !e->waiters.empty()) { Done d; d.entry = e; m_done.push_back(std::move(d)); }
    }
    return e;
}

AssetManager::EntryPtr AssetManager::FindEntry(std::type_index t, const std::string& path) {
    std::lock_guard<std::mutex> lk(m_mutex);
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
        std::unique_lock<std::mutex> lk(m_mutex);
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
                    StartWorkerLocked();
                    m_jobCv.notify_one();
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
    m_entryCv.notify_all();
    for (Fire& f : fires)
        for (auto& cb : f.cbs) cb(f.e, f.err);
    if (onReload)
        for (auto& r : reloaded) onReload(r.first, r.second);
    return handled;
}

int AssetManager::PendingCount() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_batchSubmitted - m_batchFinished;
}

float AssetManager::Progress() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_batchSubmitted <= 0) return 1.0f;
    return static_cast<float>(m_batchFinished) / static_cast<float>(m_batchSubmitted);
}

void AssetManager::SetHotReload(bool enabled, double intervalSeconds) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_hot = enabled;
    m_hotInterval = intervalSeconds < 0 ? 0 : intervalSeconds;
    m_lastScanNs = 0;
}

void AssetManager::SetReloadCallback(std::function<void(const std::string&, const std::string&)> cb) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_onReload = std::move(cb);
}

// --- Housekeeping --------------------------------------------------------------------

void AssetManager::Purge() {
    std::lock_guard<std::mutex> lk(m_mutex);
    for (auto it = m_cache.begin(); it != m_cache.end();)
        it = it->second.expired() ? m_cache.erase(it) : std::next(it);
}

std::size_t AssetManager::CachedCount() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    std::size_t n = 0;
    for (const auto& kv : m_cache) if (!kv.second.expired()) ++n;
    return n;
}

AssetReport AssetManager::Report() const {
    AssetReport r;
    std::lock_guard<std::mutex> lk(m_mutex);
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
