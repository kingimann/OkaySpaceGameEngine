#pragma once
// Central, opt-in asset registry: typed handles, one cache entry per canonical file
// path, reference counting, per-type loaders, background loading finished on the
// main thread, hot reload and a usage report. See docs/asset_manager.md.
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <typeindex>
#include <utility>
#include <vector>

namespace okay {

namespace assets_detail {
enum class State : int { Pending = 0, Loading = 1, Ready = 2, Failed = 3 };

/// One cached file of one type. Shared by every handle to it; the asset is freed
/// when the last handle goes. `data` is set once, before state becomes Ready, and
/// never replaced (a hot reload assigns into the same object).
struct Entry {
    Entry(std::type_index t, std::string k, std::string p) : type(t), key(std::move(k)), path(std::move(p)) {}
    const std::type_index type;
    const std::string key;     // canonical cache key
    const std::string path;    // path used to open the file
    std::shared_ptr<void> data;
    std::atomic<int> state{static_cast<int>(State::Pending)};
    std::atomic<std::size_t> bytes{0};
    std::atomic<std::uint64_t> version{0};   // bumped by every hot reload
    std::string error;                       // set before state becomes Failed
    // Guarded by the manager's mutex:
    std::int64_t stamp = 0;                  // file mtime (ticks) at last (re)load attempt
    std::int64_t fileSize = -1;
    std::uint64_t hash = 0;                  // FNV-1a of the file when it was read (0 = not hashed)
    std::uint64_t lastUse = 0;               // manager tick of the last Get/Find (LRU order)
    bool pinned = false;                     // the manager itself holds a reference (budget cache)
    bool reloading = false;
    std::vector<std::function<void(const std::shared_ptr<Entry>&, const std::string&)>> waiters;
};
} // namespace assets_detail

/// Typed, cheap-to-copy reference to a cached asset. The asset lives as long as any
/// handle (or an in-flight load) refers to it. Get() is null until Ready().
template <class T>
class AssetHandle {
public:
    AssetHandle() = default;
    bool Ready() const { return m_e && m_e->state.load(std::memory_order_acquire) == static_cast<int>(assets_detail::State::Ready); }
    bool Loading() const {
        if (!m_e) return false;
        int s = m_e->state.load(std::memory_order_acquire);
        return s == static_cast<int>(assets_detail::State::Pending) || s == static_cast<int>(assets_detail::State::Loading);
    }
    explicit operator bool() const { return Ready(); }
    T* Get() const { return Ready() ? static_cast<T*>(m_e->data.get()) : nullptr; }
    T* operator->() const { return Get(); }
    T& operator*() const { return *Get(); }
    const std::string& Path() const { static const std::string e; return m_e ? m_e->key : e; }
    std::uint64_t Version() const { return m_e ? m_e->version.load() : 0; }
    long UseCount() const { return m_e ? m_e.use_count() : 0; }
    /// A shared_ptr to the asset that keeps the cache entry alive (for APIs that hand out shared_ptr<const T>).
    std::shared_ptr<T> Shared() const { return Ready() ? std::shared_ptr<T>(m_e, static_cast<T*>(m_e->data.get())) : nullptr; }
    void Reset() { m_e.reset(); }
    bool operator==(const AssetHandle& o) const { return m_e == o.m_e; }
    bool operator!=(const AssetHandle& o) const { return m_e != o.m_e; }
private:
    friend class AssetManager;
    explicit AssetHandle(std::shared_ptr<assets_detail::Entry> e) : m_e(std::move(e)) {}
    std::shared_ptr<assets_detail::Entry> m_e;
};

/// Counters and sizes of the whole manager (AssetManager::Stats()). The counters start at 0 and only grow.
struct AssetStats {
    std::uint64_t hits = 0, misses = 0, loads = 0, failures = 0, evictions = 0, reloads = 0, reloadsSkippedSameHash = 0;
    std::size_t budget = 0;          ///< bytes; 0 = unlimited
    std::size_t entries = 0;         ///< live cached assets of the manager itself
    std::size_t residentBytes = 0;   ///< bytes of those assets
    std::size_t pinnedCount = 0, pinnedBytes = 0;   ///< assets the manager keeps alive for the budget cache
    struct External { std::string name; std::size_t count = 0, bytes = 0; };
    std::vector<External> externals; ///< caches that report into the manager (audio clips, textures)
    std::size_t externalBytes = 0;
    std::size_t TotalBytes() const { return residentBytes + externalBytes; }
    std::string ToString() const;
};

/// A cache that lives elsewhere (the audio decode cache, the picture cache) but reports its size to the manager and
/// lets the manager's budget trim it. All callbacks run on the thread that calls Pump() / Trim().
struct AssetExternalCache {
    std::string name;
    std::function<std::size_t()> bytes;                        ///< current size
    std::function<std::size_t()> count;                        ///< number of items
    std::function<void(std::size_t targetBytes)> trim;         ///< drop least useful items until at most targetBytes (best effort)
    std::function<void()> clear;                               ///< drop everything
};

/// Usage report row/summary.
struct AssetReport {
    struct Item { std::string key, type; std::size_t bytes = 0; long refs = 0; bool loading = false; std::uint64_t version = 0; bool pinned = false; };
    struct TypeTotal { std::string type; std::size_t count = 0, bytes = 0; };
    std::vector<Item> items;
    std::vector<TypeTotal> types;
    std::size_t totalBytes = 0, loadedCount = 0, loadingCount = 0;
    std::string ToString() const;
};

class AssetManager {
public:
    AssetManager();
    ~AssetManager();
    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    /// Process-wide instance, with the default loaders registered.
    static AssetManager& Instance();

    /// Registers loaders for Image, AudioClip, Mesh, DataAsset and Material that call
    /// the types' existing load functions. Instance() does this already.
    void RegisterDefaultLoaders();

    // --- Paths ------------------------------------------------------------------
    /// The canonical cache key: '\' -> '/', repeated slashes collapsed, '.' and '..'
    /// resolved lexically, no trailing slash. With `foldCase` letters are lower-cased
    /// (the default on Windows, where "A.png" and "a.PNG" are the same file).
    static std::string Normalize(const std::string& path, bool foldCase);
    static std::string Normalize(const std::string& path);   // platform default case policy

    // --- Loaders ----------------------------------------------------------------
    /// `load` returns the decoded asset (null plus `error` on failure). It may run
    /// on a background thread, so it must not touch main-thread-only state. T must be
    /// move-assignable (hot reload moves a fresh decode into the live object).
    /// `sizeOf` feeds the usage report (default sizeof(T)).
    template <class T>
    void RegisterLoader(std::string typeName,
                        std::function<std::shared_ptr<T>(const std::string& path, std::string& error)> load,
                        std::function<std::size_t(const T&)> sizeOf = {}) {
        LoaderRec r;
        r.name = std::move(typeName);
        r.load = [load](const std::string& p, std::string& e) -> std::shared_ptr<void> { return load(p, e); };
        r.assign = [](void* d, void* s) { *static_cast<T*>(d) = std::move(*static_cast<T*>(s)); };
        r.size = [sizeOf](const void* d) -> std::size_t {
            return sizeOf ? sizeOf(*static_cast<const T*>(d)) : sizeof(T);
        };
        AddLoader(std::type_index(typeid(T)), std::move(r));
    }

    // --- Loading ----------------------------------------------------------------
    /// Synchronous load or cache hit. Empty handle (and `*error`) if there is no
    /// loader or the load fails; failures are not cached. Blocks if another thread is
    /// loading the same file.
    template <class T>
    AssetHandle<T> Get(const std::string& path, std::string* error = nullptr) {
        return AssetHandle<T>(AcquireSync(std::type_index(typeid(T)), path, error));
    }
    /// Cached and ready only; never loads.
    template <class T>
    AssetHandle<T> Find(const std::string& path) {
        return AssetHandle<T>(FindEntry(std::type_index(typeid(T)), path));
    }
    /// Starts a background decode and returns at once. The handle is Loading() until
    /// the worker finishes (it may turn Ready before the next Pump); `done` always runs
    /// from Pump() on the main thread, with the ready handle and an empty error, or an
    /// empty handle and the error text. A sync Get of the same file waits for the decode.
    template <class T>
    AssetHandle<T> LoadAsync(const std::string& path,
                             std::function<void(AssetHandle<T>, const std::string& error)> done = {}) {
        std::function<void(const std::shared_ptr<assets_detail::Entry>&, const std::string&)> cb;
        if (done) cb = [done](const std::shared_ptr<assets_detail::Entry>& e, const std::string& err) {
            done(err.empty() ? AssetHandle<T>(e) : AssetHandle<T>(), err);
        };
        return AssetHandle<T>(AcquireAsync(std::type_index(typeid(T)), path, std::move(cb)));
    }

    /// Main-thread tick: runs the callbacks of finished background loads,
    /// checks watched files for changes (when hot reload is on) and applies finished
    /// reloads. Callbacks run with no internal lock held. Returns events handled.
    int Pump();
    /// Loads queued or decoding, and the finished fraction of the current batch (1 when idle).
    int PendingCount() const;
    float Progress() const;

    // --- Hot reload ---------------------------------------------------------------
    /// Off by default. When on, Pump() compares each loaded file's modification time
    /// and size (at most every `intervalSeconds`) and re-decodes changed files on the
    /// worker thread. The new data is moved into the existing object, so handles stay
    /// valid; code reading the asset should run on the thread that calls Pump().
    /// A changed modification time or size is only the first hint: the file is then hashed (FNV-1a 64) and a rewrite with
    /// identical bytes (a "touch", a save with no change) is NOT decoded again. Assets loaded while hot reload was off have
    /// no hash yet and are re-decoded on the first change.
    void SetHotReload(bool enabled, double intervalSeconds = 0.5);
    /// Called from Pump() after an asset was reloaded in place.
    void SetReloadCallback(std::function<void(const std::string& key, const std::string& typeName)> cb);

    // --- Memory budget ------------------------------------------------------------
    /// 0 (the default) = unlimited: nothing changes, an asset is freed with its last handle. With a budget in bytes the manager
    /// keeps every loaded asset alive after its last handle goes (a real cache) and, when the total is over the budget, drops the
    /// least recently used assets nobody else holds. Held handles are never evicted. Eviction happens in Pump() (never for an
    /// asset used since the previous Pump, so raw pointers used this frame stay valid) and in Trim().
    void SetBudget(std::size_t bytes);
    std::size_t Budget() const;
    /// Evicts now (even assets used this frame) until the total fits the budget; returns assets dropped. Call where nothing
    /// holds a raw pointer into an asset.
    std::size_t Trim();
    /// Drops every asset the manager keeps alive only for the cache (handles in use stay). Returns assets dropped.
    std::size_t EvictUnused();
    /// Registers a cache that reports into the manager and is trimmed with it. Same name replaces.
    void RegisterExternalCache(AssetExternalCache cache);
    /// Registers an already-decoded object under `key` (a "cache hit" for later Get). The object stays valid and shared; `watchPath`
    /// (optional) is the file the hot reload watches, and reload re-decodes it through the registered loader and moves the result
    /// into `obj`.
    template <class T>
    AssetHandle<T> Adopt(const std::string& key, std::shared_ptr<T> obj, const std::string& watchPath = {}) {
        return AssetHandle<T>(AdoptEntry(std::type_index(typeid(T)), key, std::static_pointer_cast<void>(obj), watchPath));
    }
    AssetStats Stats() const;

    // --- Housekeeping -------------------------------------------------------------
    /// Drops cache slots whose assets are gone (happens lazily anyway).
    void Purge();
    AssetReport Report() const;
    std::size_t CachedCount() const;

private:
    struct LoaderRec {
        std::string name;
        std::function<std::shared_ptr<void>(const std::string&, std::string&)> load;
        std::function<void(void*, void*)> assign;
        std::function<std::size_t(const void*)> size;
    };
    using EntryPtr = std::shared_ptr<assets_detail::Entry>;
    using Key = std::pair<std::type_index, std::string>;
    struct Job { EntryPtr entry; bool reload = false; };
    struct Done {
        EntryPtr entry; bool reload = false, counted = false, unchanged = false;
        std::shared_ptr<void> data; std::string error; std::int64_t stamp = 0, size = -1; std::uint64_t hash = 0;
    };

    void AddLoader(std::type_index t, LoaderRec r);
    EntryPtr AcquireSync(std::type_index t, const std::string& path, std::string* error);
    EntryPtr AcquireAsync(std::type_index t, const std::string& path,
                          std::function<void(const EntryPtr&, const std::string&)> cb);
    EntryPtr FindEntry(std::type_index t, const std::string& path);
    void StartWorkerLocked();
    void FinishFailedLocked(const EntryPtr& e);
    void WorkerMain();
    bool LoaderFor(std::type_index t, LoaderRec& out) const;   // locks
    void CompleteLocked(const EntryPtr& e, const LoaderRec& rec, std::shared_ptr<void> data);
    EntryPtr AdoptEntry(std::type_index t, const std::string& key, std::shared_ptr<void> obj, const std::string& watchPath);
    void PinLocked(const EntryPtr& e);
    void UnpinAllLocked();
    std::size_t EvictLocked(std::uint64_t maxLastUse);   // pins nobody else holds, least recently used first, until under budget
    std::size_t OwnBytesLocked() const;

    mutable std::mutex m_mutex;
    std::condition_variable m_entryCv;    // an entry left Loading
    std::condition_variable m_jobCv;      // worker wake-up
    std::map<std::type_index, LoaderRec> m_loaders;
    std::map<Key, std::weak_ptr<assets_detail::Entry>> m_cache;
    std::deque<Job> m_jobs;
    std::deque<Done> m_done;
    std::thread m_worker;
    bool m_stop = false;
    std::size_t m_budget = 0;
    std::uint64_t m_tick = 0, m_frameMark = 0;
    std::vector<EntryPtr> m_pins;
    std::vector<AssetExternalCache> m_externals;
    AssetStats m_counters;
    int m_batchSubmitted = 0, m_batchFinished = 0;
    bool m_hot = false;
    double m_hotInterval = 0.5;
    std::int64_t m_lastScanNs = 0;
    std::function<void(const std::string&, const std::string&)> m_onReload;
};

} // namespace okay
