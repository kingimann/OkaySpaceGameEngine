// Save framework: capturing and restoring a Scene, quick save / autosave ring /
// continue, checkpoints.
#include "okay/Core/SaveFramework.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Components/Inventory.hpp"
#include "okay/Components/Stats.hpp"
#include "okay/Physics/Collider2D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scene/Transform.hpp"
#include <algorithm>
#include <map>
#include <random>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace okay {

// =====================================================================
// Saveable
// =====================================================================

const std::string& Saveable::EnsureId() {
    if (saveId.empty()) saveId = SaveSystem::NewId();
    return saveId;
}

namespace {

// ---------------------------------------------------------------------
// Keys: how an object is found again
// ---------------------------------------------------------------------

struct KeyEntry {
    GameObject* go = nullptr;
    std::string key;
    std::string parentKey;       // key of the parent object ("" for a root)
    bool underDynamic = false;   // is, or sits under, a dynamic object
    bool duplicate = false;      // another object already holds this key
    Saveable* sv = nullptr;
};

struct KeyTable {
    std::vector<KeyEntry> entries;                         // parents before children
    std::unordered_map<std::string, std::size_t> byKey;    // the first holder of each key
    std::unordered_map<const GameObject*, std::size_t> byObject;
};

struct PendingChild {
    GameObject* go;
    std::string rel;             // the name, with #n for the nth same-named sibling
    std::string parentKey;
    bool underDynamic;
};

void ExpandChildren(const std::vector<Transform*>& kids, const std::string& parentKey, bool underDynamic,
                    std::vector<PendingChild>& stack) {
    std::map<std::string, int> seen;
    std::vector<PendingChild> frames;
    for (Transform* t : kids) {
        GameObject* g = t ? t->gameObject : nullptr;
        if (!g || g->IsDestroyed() || g->transient) continue;   // runtime-made objects don't take a sibling number
        const int nth = seen[g->name]++;
        frames.push_back({g, nth ? g->name + "#" + std::to_string(nth) : g->name, parentKey, underDynamic});
    }
    for (auto it = frames.rbegin(); it != frames.rend(); ++it) stack.push_back(std::move(*it));   // so they pop in order
}

KeyTable BuildKeys(Scene& scene) {
    KeyTable kt;
    std::vector<Transform*> roots;
    for (const auto& up : scene.Objects()) {
        GameObject* g = up.get();
        if (g && g->transform && !g->transform->Parent()) roots.push_back(g->transform);
    }
    std::vector<PendingChild> stack;
    ExpandChildren(roots, std::string(), false, stack);
    while (!stack.empty()) {
        PendingChild f = std::move(stack.back());
        stack.pop_back();
        KeyEntry e;
        e.go = f.go;
        e.sv = f.go->GetComponent<Saveable>();
        if (e.sv && !e.sv->saveId.empty()) e.key = "id:" + e.sv->saveId;
        else if (f.parentKey.empty()) e.key = "path:" + f.rel;
        else e.key = f.parentKey + "/" + f.rel;
        e.parentKey = f.parentKey;
        e.underDynamic = f.underDynamic || (e.sv && e.sv->dynamic);
        const std::size_t at = kt.entries.size();
        auto ins = kt.byKey.emplace(e.key, at);
        e.duplicate = !ins.second;
        kt.byObject[f.go] = at;
        const std::string key = e.key;
        const bool under = e.underDynamic;
        kt.entries.push_back(std::move(e));
        if (f.go->transform) ExpandChildren(f.go->transform->Children(), key, under, stack);
    }
    return kt;
}

// Does the capture options pick this object?
bool Qualifies(const KeyEntry& e, bool allObjects, const std::string& includeTag) {
    if (e.sv) return e.sv->enabled;
    if (allObjects) return true;
    return !includeTag.empty() && e.go->tag == includeTag;
}

// ---------------------------------------------------------------------
// Which static objects a level started with
// ---------------------------------------------------------------------

struct Baseline {
    std::string sceneName;
    std::set<std::string> keys;
};

std::unordered_map<std::uint64_t, Baseline>& Baselines() {
    static std::unordered_map<std::uint64_t, Baseline> m;
    return m;
}

Baseline& EnsureBaseline(Scene& scene, const KeyTable& kt, bool allObjects, const std::string& includeTag, bool reset) {
    Baseline& b = Baselines()[scene.Uid()];
    if (reset || b.sceneName != scene.Name() || b.keys.empty()) {
        b.sceneName = scene.Name();
        b.keys.clear();
        for (const KeyEntry& e : kt.entries)
            if (!e.duplicate && !e.underDynamic && Qualifies(e, allObjects, includeTag)) b.keys.insert(e.key);
    }
    return b;
}

// ---------------------------------------------------------------------
// ISaveable components
// ---------------------------------------------------------------------

struct NamedSaveable { ISaveable* comp; std::string name; };

std::vector<NamedSaveable> SaveablesOf(const GameObject& go) {
    std::vector<NamedSaveable> out;
    std::map<std::string, int> used;
    int index = 0;
    for (const auto& c : go.Components()) {
        ISaveable* s = dynamic_cast<ISaveable*>(c.get());
        if (!s) continue;
        std::string name = s->SaveName();
        if (name.empty()) name = "c" + std::to_string(index);
        const int n = used[name]++;
        if (n) name += "#" + std::to_string(n);
        out.push_back({s, name});
        ++index;
    }
    return out;
}

// ---------------------------------------------------------------------
// Adapters for built-in components
// ---------------------------------------------------------------------

SaveAdapter Rigidbody3DAdapter() {
    SaveAdapter a;
    a.name = "rigidbody3d";
    a.save = [](GameObject& go, SaveWriter& w) {
        auto* rb = go.GetComponent<Rigidbody3D>();
        if (!rb) return false;
        w.Vector3("vel", rb->velocity);
        w.Vector3("ang", rb->angularVelocity);
        return true;
    };
    a.load = [](GameObject& go, SaveReader& r) {
        auto* rb = go.GetComponent<Rigidbody3D>();
        if (!rb) return;
        rb->velocity = r.Vector3("vel", rb->velocity);
        rb->angularVelocity = r.Vector3("ang", rb->angularVelocity);
        rb->WakeUp();
    };
    return a;
}

SaveAdapter Rigidbody2DAdapter() {
    SaveAdapter a;
    a.name = "rigidbody2d";
    a.save = [](GameObject& go, SaveWriter& w) {
        auto* rb = go.GetComponent<Rigidbody2D>();
        if (!rb) return false;
        w.Float("vx", rb->velocity.x);
        w.Float("vy", rb->velocity.y);
        w.Float("ang", rb->angularVelocity);
        return true;
    };
    a.load = [](GameObject& go, SaveReader& r) {
        auto* rb = go.GetComponent<Rigidbody2D>();
        if (!rb) return;
        rb->velocity.x = r.Float("vx", rb->velocity.x);
        rb->velocity.y = r.Float("vy", rb->velocity.y);
        rb->angularVelocity = r.Float("ang", rb->angularVelocity);
        rb->WakeUp();
    };
    return a;
}

SaveAdapter InventoryAdapter() {
    SaveAdapter a;
    a.name = "inventory";
    a.save = [](GameObject& go, SaveWriter& w) {
        auto* inv = go.GetComponent<Inventory>();
        if (!inv) return false;
        w.Int("capacity", inv->capacity);
        w.Int("n", (int)inv->slots.size());
        for (std::size_t i = 0; i < inv->slots.size(); ++i) {
            const Inventory::Slot& s = inv->slots[i];
            SaveWriter sw = w.Scope("s" + std::to_string(i));
            sw.String("item", s.item);
            sw.Int("count", s.count);
            if (s.data.durability >= 0.0f) sw.Float("dur", s.data.durability);
            if (s.data.uid != 0) sw.Int64("uid", (long long)s.data.uid);
            sw.Int("nv", (int)s.data.values.size());
            for (std::size_t j = 0; j < s.data.values.size(); ++j) {
                sw.String("k" + std::to_string(j), s.data.values[j].first);
                sw.String("v" + std::to_string(j), s.data.values[j].second);
            }
        }
        return true;
    };
    a.load = [](GameObject& go, SaveReader& r) {
        auto* inv = go.GetComponent<Inventory>();
        if (!inv) return;
        const int n = std::max(0, std::min(r.Int("n", 0), 100000));
        std::vector<Inventory::Slot> slots;
        for (int i = 0; i < n; ++i) {
            SaveReader sr = r.Scope("s" + std::to_string(i));
            Inventory::Slot s;
            s.item = sr.String("item");
            s.count = sr.Int("count", 0);
            s.data.durability = sr.Float("dur", -1.0f);
            s.data.uid = (std::uint64_t)sr.Int64("uid", 0);
            const int nv = std::max(0, std::min(sr.Int("nv", 0), 10000));
            for (int j = 0; j < nv; ++j)
                s.data.values.push_back({sr.String("k" + std::to_string(j)), sr.String("v" + std::to_string(j))});
            slots.push_back(std::move(s));
        }
        inv->capacity = r.Int("capacity", inv->capacity);
        inv->slots = std::move(slots);
        inv->NotifyChanged();   // UI and scripts refresh, as they do when a bag is carried to the next scene
    };
    return a;
}

SaveAdapter StatsAdapter() {
    SaveAdapter a;
    a.name = "stats";
    a.save = [](GameObject& go, SaveWriter& w) {
        auto* s = go.GetComponent<Stats>();
        if (!s) return false;
        w.Float("health", s->health); w.Float("maxHealth", s->maxHealth);
        w.Float("mana", s->mana);     w.Float("maxMana", s->maxMana);
        w.Int("level", s->level); w.Int("xp", s->xp); w.Int("xpToNext", s->xpToNext);
        w.Int("strength", s->strength); w.Int("defense", s->defense);
        w.Int("intelligence", s->intelligence); w.Int("agility", s->agility);
        return true;
    };
    a.load = [](GameObject& go, SaveReader& r) {
        auto* s = go.GetComponent<Stats>();
        if (!s) return;
        s->health = r.Float("health", s->health); s->maxHealth = r.Float("maxHealth", s->maxHealth);
        s->mana = r.Float("mana", s->mana);       s->maxMana = r.Float("maxMana", s->maxMana);
        s->level = r.Int("level", s->level); s->xp = r.Int("xp", s->xp); s->xpToNext = r.Int("xpToNext", s->xpToNext);
        s->strength = r.Int("strength", s->strength); s->defense = r.Int("defense", s->defense);
        s->intelligence = r.Int("intelligence", s->intelligence); s->agility = r.Int("agility", s->agility);
    };
    return a;
}

std::vector<SaveAdapter> DefaultAdapters() {
    return {Rigidbody3DAdapter(), Rigidbody2DAdapter(), InventoryAdapter(), StatsAdapter()};
}

std::vector<SaveAdapter>& Adapters() {
    static std::vector<SaveAdapter> a = DefaultAdapters();
    return a;
}

std::map<std::string, std::function<GameObject*(Scene&)>>& Factories() {
    static std::map<std::string, std::function<GameObject*(Scene&)>> f;
    return f;
}

GameObject* MakeFromPrefab(Scene& scene, const std::string& prefab, std::string* error) {
    if (prefab.empty()) { if (error) *error = "no prefab recorded"; return nullptr; }
    if (prefab.rfind("factory:", 0) == 0) {
        auto it = Factories().find(prefab.substr(8));
        if (it == Factories().end() || !it->second) { if (error) *error = "no factory registered for \"" + prefab + "\""; return nullptr; }
        GameObject* g = it->second(scene);
        if (!g && error) *error = "factory \"" + prefab + "\" made nothing";
        return g;
    }
    std::string err;
    GameObject* g = SceneSerializer::InstantiateFromFile(scene, prefab, &err);
    if (!g && error) *error = "can't load prefab \"" + prefab + "\"" + (err.empty() ? "" : ": " + err);
    return g;
}

float& PlayBase() { static float t = 0.0f; return t; }
float& PlayBaseAt() { static float t = 0.0f; return t; }

} // namespace

// =====================================================================
// SaveSystem
// =====================================================================

std::string SaveSystem::NewId() {
    static std::mt19937_64 rng{std::random_device{}()};
    static std::uint64_t counter = 0;
    return savefmt::Hex16(rng() ^ (++counter * 0x9E3779B97F4A7C15ull));
}

float SaveSystem::PlayTime() { return PlayBase() + std::max(0.0f, Time::ElapsedTime() - PlayBaseAt()); }
void SaveSystem::SetPlayTime(float seconds) { PlayBase() = seconds > 0.0f ? seconds : 0.0f; PlayBaseAt() = Time::ElapsedTime(); }

void SaveSystem::RunNextFrame(Scene& scene, std::function<void()> fn) {
    Scheduler::Options o;
    o.unscaled = true;
    scene.scheduler().Invoke(0.0f, std::move(fn), o);
}

void SaveSystem::RegisterFactory(const std::string& name, std::function<GameObject*(Scene&)> make) {
    if (make) Factories()[name] = std::move(make);
    else Factories().erase(name);
}
void SaveSystem::UnregisterFactory(const std::string& name) { Factories().erase(name); }

void SaveSystem::RegisterAdapter(SaveAdapter adapter) {
    for (auto& a : Adapters()) if (a.name == adapter.name) { a = std::move(adapter); return; }
    Adapters().push_back(std::move(adapter));
}
void SaveSystem::ResetAdapters() { Adapters() = DefaultAdapters(); }
std::vector<std::string> SaveSystem::AdapterNames() {
    std::vector<std::string> out;
    for (const auto& a : Adapters()) out.push_back(a.name);
    return out;
}

std::string SaveSystem::KeyOf(const GameObject& go) {
    Scene* s = go.scene();
    if (!s) return std::string();
    const KeyTable kt = BuildKeys(*s);
    auto it = kt.byObject.find(&go);
    return it == kt.byObject.end() ? std::string() : kt.entries[it->second].key;
}

GameObject* SaveSystem::Spawn(Scene& scene, const std::string& prefab, const Vec3& position, std::string* error) {
    GameObject* go = MakeFromPrefab(scene, prefab, error);
    if (!go) return nullptr;
    Saveable* sv = go->GetComponent<Saveable>();
    if (!sv) sv = go->AddComponent<Saveable>();
    sv->dynamic = true;
    sv->prefab = prefab;
    sv->EnsureId();
    go->transform->localPosition = position;
    return go;
}

void SaveSystem::MarkSceneLoaded(Scene& scene, const SaveCaptureOptions& options) {
    const KeyTable kt = BuildKeys(scene);
    EnsureBaseline(scene, kt, options.allObjects, options.includeTag, /*reset=*/true);
}

SaveDocument SaveSystem::CaptureScene(Scene& scene, const SaveCaptureOptions& options, SaveCaptureReport* report) {
    SaveCaptureReport rep;
    for (const auto& up : scene.Objects()) {   // dynamic objects need their id before keys are built
        GameObject* g = up.get();
        if (!g || g->IsDestroyed() || g->transient) continue;
        if (Saveable* sv = g->GetComponent<Saveable>()) if (sv->dynamic) sv->EnsureId();
    }
    const KeyTable kt = BuildKeys(scene);
    SaveDocument doc;

    std::vector<std::size_t> chosen;
    for (std::size_t i = 0; i < kt.entries.size(); ++i) {
        const KeyEntry& e = kt.entries[i];
        if (!Qualifies(e, options.allObjects, options.includeTag)) continue;
        if (e.duplicate) {
            rep.warnings.push_back("two objects share the save key \"" + e.key + "\"; the second (\"" + e.go->name + "\") is not saved");
            continue;
        }
        chosen.push_back(i);
    }

    int n = 0;
    for (std::size_t idx : chosen) {
        const KeyEntry& e = kt.entries[idx];
        GameObject* go = e.go;
        const bool dyn = e.sv && e.sv->dynamic;
        const bool saveTransform = !e.sv || e.sv->saveTransform;
        const bool saveActive = !e.sv || e.sv->saveActive;
        const bool saveComponents = options.components && (!e.sv || e.sv->saveComponents);
        SaveBlock& block = doc.Block("obj." + std::to_string(n++));
        SaveWriter w(block);
        w.String("key", e.key);
        w.String("name", go->name);
        if (!e.parentKey.empty()) w.String("parent", e.parentKey);
        if (dyn) {
            std::string prefab = e.sv->prefab.empty() ? go->prefabSource : e.sv->prefab;
            w.Bool("dynamic", true);
            w.String("prefab", prefab);
            w.String("tag", go->tag);
            w.Int("layer", go->layer);
            ++rep.dynamic;
            if (prefab.empty())
                rep.warnings.push_back("dynamic object \"" + go->name + "\" has no prefab and can't be respawned on load (set Saveable::prefab, or use SaveSystem::Spawn)");
        }
        if (saveActive) w.Bool("active", go->active);
        if (saveTransform && go->transform) {
            w.Vector3("lp", go->transform->localPosition);
            w.Rotation("lr", go->transform->localRotation);
            w.Vector3("ls", go->transform->localScale);
        }
        for (const NamedSaveable& s : SaveablesOf(*go)) {
            SaveWriter cw = w.Scope("c." + s.name);
            s.comp->OnSave(cw);
        }
        if (saveComponents)
            for (const SaveAdapter& a : Adapters()) {
                if (!a.save) continue;
                SaveWriter aw = w.Scope("a." + a.name);
                a.save(*go, aw);
            }
        ++rep.objects;
    }

    // Static objects the level started with that are gone now.
    Baseline& base = EnsureBaseline(scene, kt, options.allObjects, options.includeTag, /*reset=*/false);
    std::vector<std::string> destroyed;
    for (const std::string& k : base.keys)
        if (!kt.byKey.count(k)) destroyed.push_back(k);
    rep.destroyed = (int)destroyed.size();

    SaveWriter sw = doc.Writer("scene");
    sw.Int("format", kSceneFormat);
    sw.String("name", scene.Name());
    sw.Int("count", n);
    sw.Float("playTime", PlayTime());
    sw.Bool("allObjects", options.allObjects);
    sw.String("includeTag", options.includeTag);
    sw.Strings("destroyed", destroyed);

    if (report) *report = rep;
    return doc;
}

bool SaveSystem::VerifyCapture(Scene& scene, std::string* why, const SaveCaptureOptions& options) {
    SaveCaptureReport rep;
    const SaveDocument doc = CaptureScene(scene, options, &rep);
    if (!rep.warnings.empty()) { if (why) *why = rep.warnings.front(); return false; }
    return doc.RoundTrips(why);
}

SaveApplyReport SaveSystem::ApplyScene(Scene& scene, const SaveDocument& doc, const SaveApplyOptions& options) {
    SaveApplyReport rep;
    const SaveBlock* sb = doc.Find("scene");
    if (!sb) { rep.ok = false; rep.error = "the save has no scene data"; return rep; }
    SaveReader sr(*sb);
    const int format = sr.Int("format", 0);
    if (format < 1 || format > kSceneFormat) {
        rep.ok = false;
        rep.error = "the scene data is in format " + std::to_string(format) + ", this build reads up to " + std::to_string(kSceneFormat);
        return rep;
    }
    const std::string savedName = sr.String("name");
    if (savedName != scene.Name()) {
        if (options.sceneMustMatch) {
            rep.ok = false;
            rep.error = "the save was made in scene \"" + savedName + "\", not \"" + scene.Name() + "\"";
            return rep;
        }
        rep.warnings.push_back("the save was made in scene \"" + savedName + "\", this is \"" + scene.Name() + "\"");
    }
    const bool allObjects = sr.Bool("allObjects", false);
    const std::string includeTag = sr.String("includeTag");
    const int count = std::min(std::max(0, sr.Int("count", 0)), (int)doc.Count());   // a hostile count can't make this loop for long
    const std::vector<std::string> destroyedKeys = sr.Strings("destroyed");

    if (options.prepare) {
        options.prepare(scene);
        if (options.startSpawned) scene.Start();   // components it attached get Awake / Start before state goes in
    }

    KeyTable kt = BuildKeys(scene);
    {   // the level as it is now is what "start of level" means for later captures
        Baseline& base = EnsureBaseline(scene, kt, allObjects, includeTag, /*reset=*/false);
        for (const KeyEntry& e : kt.entries)
            if (!e.duplicate && !e.underDynamic && Qualifies(e, allObjects, includeTag)) base.keys.insert(e.key);
        for (const std::string& k : destroyedKeys) base.keys.insert(k);
    }

    // 1. Static objects the save says were destroyed.
    for (const std::string& k : destroyedKeys) {
        auto it = kt.byKey.find(k);
        if (it == kt.byKey.end()) continue;   // already gone
        scene.Destroy(kt.entries[it->second].go);
        ++rep.destroyed;
    }

    // 2. Dynamic objects the scene doesn't have yet.
    std::vector<const SaveBlock*> blocks;
    std::set<std::string> savedKeys;
    for (int i = 0; i < count; ++i) {
        const SaveBlock* b = doc.Find("obj." + std::to_string(i));
        if (!b) { rep.warnings.push_back("object block " + std::to_string(i) + " is missing from the save"); continue; }
        blocks.push_back(b);
        savedKeys.insert(b->GetString("key"));
    }
    int spawned = 0;
    for (const SaveBlock* b : blocks) {
        SaveReader r(*b);
        const std::string key = r.String("key");
        if (!r.Bool("dynamic", false) || kt.byKey.count(key)) continue;
        if (key.rfind("id:", 0) != 0) { rep.warnings.push_back("dynamic object with a bad key \"" + key + "\" skipped"); continue; }
        const std::string prefab = r.String("prefab");
        std::string err;
        GameObject* go = MakeFromPrefab(scene, prefab, &err);
        if (!go) { rep.warnings.push_back("couldn't respawn \"" + r.String("name") + "\": " + err); continue; }
        Saveable* sv = go->GetComponent<Saveable>();
        if (!sv) sv = go->AddComponent<Saveable>();
        sv->dynamic = true;
        sv->saveId = key.substr(3);
        sv->prefab = prefab;
        go->name = r.String("name", go->name);
        go->tag = r.String("tag", go->tag);
        go->layer = r.Int("layer", go->layer);
        const std::string parentKey = r.String("parent");
        if (!parentKey.empty()) {
            if (!kt.byKey.count(parentKey)) kt = BuildKeys(scene);   // the parent may be a part of an object spawned just now
            auto pit = kt.byKey.find(parentKey);
            if (pit != kt.byKey.end() && go->transform) go->transform->SetParent(kt.entries[pit->second].go->transform, false);
        }
        // Make the new object findable: the next parent lookup, and the state pass below.
        KeyEntry e;
        e.go = go; e.sv = sv; e.key = key; e.parentKey = parentKey; e.underDynamic = true;
        kt.byKey.emplace(key, kt.entries.size());
        kt.byObject[go] = kt.entries.size();
        kt.entries.push_back(std::move(e));
        ++spawned;
    }
    rep.spawned = spawned;
    if (spawned > 0) {
        if (options.startSpawned) scene.Start();   // Awake / Start first, so they can't overwrite restored values
        kt = BuildKeys(scene);                     // the spawned prefabs' own children have keys now
    }

    // 3. State, object by object.
    std::vector<GameObject*> restored;
    for (const SaveBlock* b : blocks) {
        SaveReader r(*b);
        const std::string key = r.String("key");
        auto it = kt.byKey.find(key);
        if (it == kt.byKey.end()) {
            if (!r.Bool("dynamic", false)) ++rep.missing;
            continue;
        }
        const KeyEntry& e = kt.entries[it->second];
        GameObject* go = e.go;
        const bool saveTransform = !e.sv || e.sv->saveTransform;
        const bool saveActive = !e.sv || e.sv->saveActive;
        const bool saveComponents = options.restoreComponents && (!e.sv || e.sv->saveComponents);
        if (saveActive && r.Has("active")) go->active = r.Bool("active", go->active);
        if (saveTransform && go->transform && r.Has("lp")) {
            go->transform->localPosition = r.Vector3("lp", go->transform->localPosition);
            go->transform->localRotation = r.Rotation("lr", go->transform->localRotation);
            go->transform->localScale = r.Vector3("ls", go->transform->localScale);
        }
        for (const NamedSaveable& s : SaveablesOf(*go)) {
            SaveReader cr = r.Scope("c." + s.name);
            s.comp->OnLoad(cr);
        }
        if (saveComponents)
            for (const SaveAdapter& a : Adapters()) {
                if (!a.load) continue;
                SaveReader ar = r.Scope("a." + a.name);
                if (ar.HasAny()) a.load(*go, ar);
            }
        restored.push_back(go);
        ++rep.restored;
    }

    // 4. Dynamic objects that were made after this save: they didn't exist then.
    if (options.removeUnsavedDynamic) {
        for (const KeyEntry& e : kt.entries) {
            if (!e.sv || !e.sv->dynamic || e.duplicate || savedKeys.count(e.key)) continue;
            scene.Destroy(e.go);
            ++rep.destroyed;
        }
    }

    // 5. Everything is back: let components fix up references.
    for (GameObject* go : restored)
        for (const NamedSaveable& s : SaveablesOf(*go)) s.comp->OnLoadComplete();

    if (options.restorePlayTime && sb->Has("playTime")) SetPlayTime(sr.Float("playTime", 0.0f));
    return rep;
}

// =====================================================================
// SaveManager
// =====================================================================

SaveManager& SaveManager::Default() {
    static SaveManager m(SaveStore::Default());
    return m;
}

bool SaveManager::Save(Scene& scene, const std::string& slot, const SaveRequest& request, std::string* error) {
    if (!SaveStore::ValidSlot(slot)) {
        if (error) *error = "\"" + slot + "\" isn't a valid slot name (letters, digits, - and _)";
        if (request.onDone) request.onDone(false, "invalid slot name");
        return false;
    }
    if (request.deferred) {
        SaveRequest copy = request;
        copy.deferred = false;
        SaveManager* self = this;
        Scene* sc = &scene;
        SaveSystem::RunNextFrame(scene, [self, sc, slot, copy]() {
            std::string err;
            if (!self->Save(*sc, slot, copy, &err)) Log::Warning("Save to slot ", slot, " failed: ", err);
        });
        return true;
    }
    SaveCaptureReport cap;
    SaveDocument doc = SaveSystem::CaptureScene(scene, request.capture, &cap);
    std::string err;
    for (const std::string& w : cap.warnings) Log::Warning("Save: ", w);
    auto finish = [&](bool ok) {
        if (!ok && error) *error = err;
        if (request.onDone) request.onDone(ok, err);
        return ok;
    };
    for (const std::string& name : request.extra.Names()) {
        if (name == "scene" || name.rfind("obj.", 0) == 0) { err = "the block name \"" + name + "\" is reserved"; return finish(false); }
    }
    doc.Merge(request.extra);
    if (request.collect) {
        SaveDocument more;
        request.collect(more);
        for (const std::string& name : more.Names())
            if (name == "scene" || name.rfind("obj.", 0) == 0) { err = "the block name \"" + name + "\" is reserved"; return finish(false); }
        doc.Merge(more);
    }
    SaveSlotInfo meta;
    meta.label = request.label;
    meta.scene = scene.Name();
    meta.playTime = request.playTime >= 0.0f ? request.playTime : SaveSystem::PlayTime();
    meta.thumbnail = request.thumbnail;
    meta.kind = request.kind.empty() ? std::string(SlotKind::Manual) : request.kind;
    meta.fields = request.fields;
    if (!m_store->Write(slot, doc, meta, &err)) return finish(false);
    Log::Info("Saved to slot ", slot);
    return finish(true);
}

namespace {

bool FinishLoad(Scene& scene, const SaveDocument& doc, const SaveSlotInfo& info, const SaveLoadOptions& o,
                SaveApplyReport* out, std::string* err) {
    SaveApplyReport rep = SaveSystem::ApplyScene(scene, doc, o.apply);
    for (const std::string& w : rep.warnings) Log::Warning("Load: ", w);
    if (out) *out = rep;
    if (!rep.ok) { if (err) *err = rep.error; return false; }
    if (o.onLoaded) o.onLoaded(doc, info);
    return true;
}

} // namespace

bool SaveManager::Load(Scene& scene, const std::string& slot, const SaveLoadOptions& options, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        if (options.onDone) options.onDone(false, why);
        return false;
    };
    if (!SaveStore::ValidSlot(slot)) return fail("\"" + slot + "\" isn't a valid slot name");
    if (!m_store->Exists(slot)) return fail("No save in slot \"" + slot + "\"");
    if (options.deferred) {
        SaveLoadOptions copy = options;
        copy.deferred = false;
        copy.outDocument = nullptr;
        copy.outReport = nullptr;
        SaveManager* self = this;
        Scene* sc = &scene;
        SaveSystem::RunNextFrame(scene, [self, sc, slot, copy]() {
            std::string err;
            if (!self->Load(*sc, slot, copy, &err)) Log::Warning("Load of slot ", slot, " failed: ", err);
        });
        return true;
    }
    SaveDocument doc;
    SaveSlotInfo info;
    std::string rerr;
    if (!m_store->Read(slot, doc, &info, &rerr)) return fail(rerr);
    if (options.outDocument) *options.outDocument = doc;
    if (!options.reloadScene.empty()) {
        SaveLoadOptions copy = options;
        Scene* sc = &scene;
        scene.RequestLoad(options.reloadScene, [sc, doc, info, copy](bool loaded) {
            std::string err;
            if (!loaded) { err = "couldn't load the scene file \"" + copy.reloadScene + "\""; if (copy.onDone) copy.onDone(false, err); return; }
            const bool ok = FinishLoad(*sc, doc, info, copy, nullptr, &err);
            if (!ok) Log::Warning("Load after the scene reload failed: ", err);
            if (copy.onDone) copy.onDone(ok, err);
        });
        return true;
    }
    std::string err;
    const bool ok = FinishLoad(scene, doc, info, options, options.outReport, &err);
    if (!ok) return fail(err);
    if (options.onDone) options.onDone(true, rerr);
    if (error) *error = rerr;   // "" unless the backup was used
    return true;
}

bool SaveManager::QuickSave(Scene& scene, SaveRequest request, std::string* error) {
    request.kind = SlotKind::Quick;
    if (request.label.empty()) request.label = "Quick save";
    return Save(scene, m_quickSlot, request, error);
}

bool SaveManager::QuickLoad(Scene& scene, const SaveLoadOptions& options, std::string* error) {
    return Load(scene, m_quickSlot, options, error);
}

std::vector<std::string> SaveManager::AutosaveSlots() const {
    std::vector<std::string> out;
    for (int i = 1; i <= m_autoCount; ++i) out.push_back(m_autoPrefix + std::to_string(i));
    return out;
}

std::string SaveManager::NextAutosaveSlot() const {
    std::string oldest;
    long long oldestAt = 0;
    for (const std::string& s : AutosaveSlots()) {
        if (!m_store->Exists(s)) return s;   // an empty place in the ring comes first
        const SaveSlotInfo i = m_store->Info(s);
        const long long at = i.corrupt ? -1 : i.savedAtMs;   // a damaged one is the first to be replaced
        if (oldest.empty() || at < oldestAt) { oldest = s; oldestAt = at; }
    }
    return oldest;
}

bool SaveManager::AutoSave(Scene& scene, SaveRequest request, std::string* error, std::string* slotUsed) {
    const std::string slot = NextAutosaveSlot();
    request.kind = SlotKind::Auto;
    if (request.label.empty()) request.label = "Autosave";
    if (slotUsed) *slotUsed = slot;
    return Save(scene, slot, request, error);
}

bool SaveManager::Continue(Scene& scene, const SaveLoadOptions& options, std::string* error, std::string* slotUsed) {
    std::string last;
    for (const SaveSlotInfo& i : m_store->ListSlots(SlotSort::Newest, true)) {
        if (i.corrupt || i.incompatible) continue;
        std::string err;
        if (Load(scene, i.slot, options, &err)) {
            if (slotUsed) *slotUsed = i.slot;
            return true;
        }
        last = err;
    }
    if (error) *error = last.empty() ? "There is no save to continue" : last;
    return false;
}

// =====================================================================
// Checkpoints
// =====================================================================

namespace { CheckpointManager* g_activeCheckpoints = nullptr; }

CheckpointManager& CheckpointManager::Global() {
    static CheckpointManager m(SaveManager::Default());
    return m;
}
CheckpointManager& CheckpointManager::Active() { return g_activeCheckpoints ? *g_activeCheckpoints : Global(); }
void CheckpointManager::SetActive(CheckpointManager* manager) { g_activeCheckpoints = manager; }

std::string CheckpointManager::SlotFor(const std::string& name) {
    std::string s = "checkpoint_";
    for (char c : name) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_';
        if (s.size() >= 64) break;
        s += ok ? c : '_';
    }
    return s;
}

bool CheckpointManager::Reach(Scene& scene, const std::string& name, bool deferred, std::string* error) {
    if (name.empty()) { if (error) *error = "a checkpoint needs a name"; return false; }
    SaveRequest req;
    req.kind = SlotKind::Checkpoint;
    req.label = name;
    req.fields["checkpoint"] = name;
    req.deferred = deferred;
    CheckpointManager* self = this;
    auto reached = [self, name]() { self->Prune(); if (self->onReached) self->onReached(name); };
    if (deferred) {
        req.onDone = [reached](bool ok, const std::string&) { if (ok) reached(); };
        return m_saves->Save(scene, SlotFor(name), req, error);
    }
    if (!m_saves->Save(scene, SlotFor(name), req, error)) return false;
    reached();
    return true;
}

bool CheckpointManager::Restore(Scene& scene, const std::string& name, const SaveLoadOptions& options, std::string* error) {
    return m_saves->Load(scene, SlotFor(name), options, error);
}

std::vector<std::string> CheckpointManager::Names() const {
    std::vector<std::string> out;
    for (const SaveSlotInfo& i : m_saves->ListSlots(SlotSort::Newest)) {
        if (i.kind != SlotKind::Checkpoint || i.corrupt) continue;
        auto f = i.fields.find("checkpoint");
        out.push_back(f != i.fields.end() ? f->second : i.slot);
    }
    return out;
}

std::string CheckpointManager::Last() const {
    const auto names = Names();
    return names.empty() ? std::string() : names.front();
}

bool CheckpointManager::Has(const std::string& name) const { return m_saves->Store().Exists(SlotFor(name)); }

bool CheckpointManager::Remove(const std::string& name) { return m_saves->Delete(SlotFor(name)); }

int CheckpointManager::Clear() {
    int n = 0;
    for (const SaveSlotInfo& i : m_saves->ListSlots(SlotSort::Newest))
        if (i.kind == SlotKind::Checkpoint && m_saves->Delete(i.slot)) ++n;
    return n;
}

void CheckpointManager::Prune() {
    if (m_max <= 0) return;
    int kept = 0;
    for (const SaveSlotInfo& i : m_saves->ListSlots(SlotSort::Newest)) {
        if (i.kind != SlotKind::Checkpoint) continue;
        if (++kept > m_max) m_saves->Delete(i.slot);
    }
}

bool CheckpointManager::RestoreLast(Scene& scene, const SaveLoadOptions& options, std::string* error) {
    std::string last;
    for (const SaveSlotInfo& i : m_saves->ListSlots(SlotSort::Newest, true)) {
        if (i.kind != SlotKind::Checkpoint || i.corrupt || i.incompatible) continue;
        std::string err;
        if (m_saves->Load(scene, i.slot, options, &err)) return true;
        last = err;
    }
    if (error) *error = last.empty() ? "There is no checkpoint to restore" : last;
    return false;
}

// ---- CheckpointTrigger ----

bool CheckpointTrigger::Fire() {
    if (once && triggered) return false;
    Scene* s = GetScene();
    if (!s) return false;
    std::string err;
    if (!CheckpointManager::Active().Reach(*s, checkpoint, deferred, &err)) {
        Log::Warning("Checkpoint \"", checkpoint, "\" failed: ", err);
        return false;
    }
    triggered = true;
    return true;
}

void CheckpointTrigger::Update(float dt) {
    if (radius <= 0.0f || targetTag.empty() || (once && triggered)) return;
    m_timer += dt;
    if (m_timer < checkInterval) return;
    m_timer = 0.0f;
    Scene* s = GetScene();
    if (!s || !transform) return;
    GameObject* t = s->FindWithTag(targetTag);
    if (!t || t == gameObject || !t->transform) return;
    if ((t->transform->Position() - transform->Position()).SqrMagnitude() <= radius * radius) Fire();
}

void CheckpointTrigger::OnTriggerEnter3D(Collider3D* other) {
    if (!other || !other->gameObject) return;
    if (!targetTag.empty() && other->gameObject->tag != targetTag) return;
    Fire();
}

void CheckpointTrigger::OnTriggerEnter2D(Collider2D* other) {
    if (!other || !other->gameObject) return;
    if (!targetTag.empty() && other->gameObject->tag != targetTag) return;
    Fire();
}

} // namespace okay
