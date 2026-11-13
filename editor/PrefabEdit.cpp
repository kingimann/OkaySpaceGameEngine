#include "PrefabEdit.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

using namespace okay;
namespace fs = std::filesystem;
using okay::editor::EditorState;

namespace oked {

namespace {

std::string Lw(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }
std::string Compact(const std::string& s) { std::string o; for (char c : s) if (c != ' ' && c != '_') o += (char)std::tolower((unsigned char)c); return o; }
std::string FileName(const std::string& p) { return fs::path(p).filename().string(); }

std::string Norm(const std::string& p) {
    std::error_code ec;
    fs::path c = fs::weakly_canonical(fs::path(p), ec);
    return ec ? p : c.string();
}

// ---- file kind cache -------------------------------------------------------------------------------
double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
struct KindEntry { PrefabKind kind = PrefabKind::Plain; std::string base; bool exists = false; double stamp = 0; };
std::map<std::string, KindEntry>& KindCache() { static std::map<std::string, KindEntry> c; return c; }

KindEntry ReadKind(const std::string& path) {
    KindEntry e;
    e.stamp = Now();
    std::string text;
    if (!EditorState::ReadTextFile(path, text)) return e;
    e.exists = true;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::size_t b = line.find_first_not_of(" \t\r");
        if (b == std::string::npos) continue;
        const std::string t = line.substr(b);
        if (t.rfind("variant ", 0) == 0) {
            e.kind = PrefabKind::Variant;
            const std::size_t q1 = t.find('"');
            if (q1 != std::string::npos) {
                std::string v;
                for (std::size_t i = q1 + 1; i < t.size() && t[i] != '"'; ++i) { if (t[i] == '\\' && i + 1 < t.size()) ++i; v += t[i]; }
                e.base = v;
            }
            return e;
        }
        if (t.rfind("nestedprefab", 0) == 0) e.kind = PrefabKind::Nested;
    }
    return e;
}

// ---- Hierarchy marker cache ------------------------------------------------------------------------
// Keyed by InstanceId, not by pointer: a destroyed object's slot can be handed to a new
// object right after a scene change, and a raw-pointer key would then hand the new
// object the old object's stale count. InstanceIds are never reused, so a lookup can
// only ever hit the object it was computed for.
struct CountEntry { int count = 0; double stamp = 0; };
std::map<std::uint64_t, CountEntry>& CountCache() { static std::map<std::uint64_t, CountEntry> c; return c; }

// ---- scene helpers ---------------------------------------------------------------------------------
void Walk(GameObject* g, std::vector<GameObject*>& out) {
    out.push_back(g);
    for (Transform* c : g->transform->Children()) if (c && c->gameObject) Walk(c->gameObject, out);
}

bool Inside(const GameObject* root, const GameObject* go) {
    for (const Transform* t = go ? go->transform : nullptr; t; t = t->Parent())
        if (t->gameObject == root) return true;
    return false;
}

std::vector<std::string> Reach(const std::string& path) {
    std::vector<std::string> out;
    PrefabReport r = PrefabTools::ValidateAsset(path);
    for (const std::string& f : r.files) out.push_back(Norm(f));
    if (out.empty()) out.push_back(Norm(path));
    return out;
}

bool Intersects(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    for (const auto& x : a) for (const auto& y : b) if (x == y) return true;
    return false;
}

// Other outermost instances in the scene whose prefab files reach one of `files`.
std::vector<GameObject*> Dependents(Scene& scene, const GameObject* except, const std::vector<std::string>& files) {
    std::vector<GameObject*> out;
    std::set<GameObject*> seen;
    for (const auto& up : scene.Objects()) {
        GameObject* g = up.get();
        if (!g || g->IsDestroyed() || g->prefabSource.empty()) continue;
        GameObject* o = PrefabTools::PrefabRoot(*g);
        if (!o || o == except || seen.count(o)) continue;
        seen.insert(o);
        if (Intersects(Reach(o->prefabSource), files)) out.push_back(o);
    }
    return out;
}

struct Snap { std::string path; bool had = false; std::string text; };
std::vector<Snap> TakeFiles(const std::vector<std::string>& paths) {
    std::vector<Snap> out;
    std::set<std::string> seen;
    for (const std::string& p : paths) {
        if (!seen.insert(p).second) continue;
        Snap s; s.path = p;
        s.had = EditorState::ReadTextFile(p, s.text);
        out.push_back(std::move(s));
    }
    return out;
}
std::vector<EditorState::FileEdit> ChangedFiles(const std::vector<Snap>& before) {
    std::vector<EditorState::FileEdit> out;
    for (const Snap& s : before) {
        std::string now;
        const bool has = EditorState::ReadTextFile(s.path, now);
        if (has == s.had && now == s.text) continue;
        EditorState::FileEdit e;
        e.path = s.path; e.hadBefore = s.had; e.before = s.text; e.hasAfter = has; e.after = now;
        out.push_back(std::move(e));
    }
    return out;
}

// The fields an instance changed (against its prefab as it is now), as "object|record|occurrence|field" keys.
std::set<std::string> OverriddenKeys(GameObject& root) {
    std::set<std::string> keys;
    PrefabDiff d;
    if (!PrefabTools::Diff(root, d)) return keys;
    for (const PrefabRecordDiff& r : d.records)
        for (const PrefabFieldDiff& f : r.fields)
            keys.insert(std::to_string(r.object) + "|" + r.record + "|" + std::to_string(r.occurrence) + "|" + f.name);
    return keys;
}

struct Dependent {
    GameObject* root = nullptr;
    std::set<std::string> overridden;   // fields it changed before the prefab file was edited
    std::set<std::string> addedPaths;   // objects the user added under it (everything else that is not in the new file is stale)
};

// The other instances that use `files`, with what each one overrode FIELD by field (the stored overrides are whole records).
std::vector<Dependent> CaptureDependents(Scene& scene, const GameObject* except, const std::vector<std::string>& files) {
    std::vector<Dependent> out;
    for (GameObject* d : Dependents(scene, except, files)) {
        Dependent dep;
        dep.root = d;
        dep.overridden = OverriddenKeys(*d);
        PrefabDiff pd;
        if (PrefabTools::Diff(*d, pd))
            for (const PrefabObjectDiff& o : pd.objects) if (o.kind == PrefabObjectDiff::Kind::Added) dep.addedPaths.insert(o.path);
        PrefabTools::CaptureOverrides(*d);   // against the OLD file
        out.push_back(std::move(dep));
    }
    return out;
}

// Rebuild the dependent instances from the (changed) files. A stored override is a whole record, so a record the instance changed
// would also keep the old prefab values of its other fields: those are put back to the new prefab's values field by field.
int Rebuild(EditorState& ed, const std::vector<Dependent>& deps, const std::vector<std::string>& changed, std::string* firstError) {
    int n = 0;
    for (const Dependent& dep : deps) {
        GameObject* d = dep.root;
        if (!d || d->IsDestroyed()) continue;
        if (!Intersects(Reach(d->prefabSource), changed)) continue;
        const bool wasSelected = ed.selected() && Inside(d, ed.selected());
        const RootSlot slot = CaptureRootSlot(ed.scene(), *d);
        std::string err;
        GameObject* fresh = PrefabTools::Reapply(ed.scene(), *d, &err);
        if (!fresh) { if (firstError && firstError->empty()) *firstError = FileName(d->prefabSource) + ": " + err; continue; }
        {   // Reapply keeps every object that is not in the new file, also the ones the prefab just dropped: those go
            PrefabDiff od;
            bool any = false;
            if (PrefabTools::Diff(*fresh, od))
                for (const PrefabObjectDiff& o : od.objects)
                    if (o.kind == PrefabObjectDiff::Kind::Added && o.live && !dep.addedPaths.count(o.path)) { ed.scene().Destroy(o.live); any = true; }
            if (any) ed.scene().FlushDestroyed();
        }
        PrefabDiff pd;
        if (PrefabTools::Diff(*fresh, pd)) {
            std::vector<PrefabTarget> stale;
            for (const PrefabRecordDiff& r : pd.records) {
                if (r.kind != PrefabRecordDiff::Kind::Changed || r.wholeRecordOnly) continue;
                for (const PrefabFieldDiff& f : r.fields) {
                    const std::string key = std::to_string(r.object) + "|" + r.record + "|" + std::to_string(r.occurrence) + "|" + f.name;
                    if (!dep.overridden.count(key)) stale.push_back(PrefabTarget{r.object, r.record, f.name, r.occurrence});
                }
            }
            for (const PrefabTarget& t : stale) {
                GameObject* again = PrefabTools::RevertOverride(ed.scene(), *fresh, t.object, t.record, t.field, t.occurrence, &err);
                if (again) fresh = again;
            }
        }
        RestoreRootSlot(ed.scene(), fresh, slot);
        if (wasSelected) ed.Select(fresh);
        ++n;
    }
    return n;
}

PrefabOpResult Fail(const std::string& m) { PrefabOpResult r; r.message = m; return r; }

// Runs `edit` (which writes prefab files through PrefabTools) as one undo step, with the other instances updated.
template <class Fn>
PrefabOpResult SourceEdit(EditorState& ed, GameObject& root, const std::string& doneText, Fn edit) {
    if (root.prefabSource.empty()) return Fail("This object is not a prefab instance.");
    const std::vector<std::string> reach = Reach(root.prefabSource);
    const std::vector<Snap> before = TakeFiles(reach);
    ed.PushUndo(doneText);   // first: capturing the other instances' overrides below edits the scene and belongs to this one step
    GameObject* self = PrefabTools::PrefabRoot(root);
    std::vector<Dependent> deps = CaptureDependents(ed.scene(), self, reach);   // against the OLD file: what an instance overrode stays
    std::string err;
    if (!edit(err)) {
        ed.DiscardLastUndo();
        return Fail(err.empty() ? "Apply failed." : err);
    }
    std::vector<EditorState::FileEdit> files = ChangedFiles(before);
    SceneSerializer::ClearPrefabCache();
    ClearPrefabInfoCache();
    std::vector<std::string> changed;
    for (const auto& f : files) changed.push_back(f.path);
    PrefabOpResult r;
    r.root = &root;
    std::string rebuildErr;
    r.updatedInstances = Rebuild(ed, deps, changed, &rebuildErr);
    ed.scene().FlushDestroyed();
    ed.AttachUndoFiles(std::move(files));
    ed.dirty = true;
    r.ok = true;
    r.message = doneText + (r.updatedInstances ? " (" + std::to_string(r.updatedInstances) + " other instance" + (r.updatedInstances == 1 ? "" : "s") + " updated)" : "");
    if (!rebuildErr.empty()) r.message += "; could not update " + rebuildErr;
    return r;
}

} // namespace

// ---- Describe ---------------------------------------------------------------------------------------

PrefabKind PrefabKindOfFile(const std::string& path, std::string* base) {
    auto& c = KindCache();
    auto it = c.find(path);
    if (it == c.end() || Now() - it->second.stamp > 2.0) it = c.insert_or_assign(path, ReadKind(path)).first;   // re-read now and then: the file may be edited outside
    if (base) *base = it->second.base;
    return it->second.kind;
}

void ClearPrefabInfoCache() { KindCache().clear(); CountCache().clear(); }

bool DescribePrefabInstance(const GameObject& go, PrefabInstanceInfo& out) {
    out = PrefabInstanceInfo();
    GameObject* root = PrefabTools::NearestPrefabRoot(go);
    if (!root || root->prefabSource.empty()) return false;
    out.root = root;
    out.outer = PrefabTools::PrefabRoot(go);
    out.source = root->prefabSource;
    out.name = FileName(out.source);
    std::error_code ec;
    out.exists = fs::exists(out.source, ec) && !ec;
    out.kind = out.exists ? PrefabKindOfFile(out.source, &out.base) : PrefabKind::Plain;
    out.baseName = FileName(out.base);
    out.isNestedInstance = out.outer && out.outer != root;
    if (out.isNestedInstance) out.outerName = FileName(out.outer->prefabSource);
    out.nestedCount = (int)PrefabTools::NestedInstances(*root, false).size();
    out.isRoot = (&go == root);
    return true;
}

std::string ResolveVariantBase(const PrefabInstanceInfo& i) {
    if (i.kind != PrefabKind::Variant || i.base.empty()) return "";
    std::error_code ec;
    if (fs::exists(i.base, ec)) return i.base;
    const fs::path beside = fs::path(i.source).parent_path() / i.base;
    if (fs::exists(beside, ec)) return beside.string();
    return "";
}

bool SaveAsPrefabFile(const GameObject& root, const std::string& path, std::string* error) {
    bool nested = false;
    std::vector<const GameObject*> stack{&root};
    while (!stack.empty() && !nested) {
        const GameObject* g = stack.back(); stack.pop_back();
        for (const Transform* c : g->transform->Children()) {
            if (!c || !c->gameObject) continue;
            if (!c->gameObject->prefabSource.empty()) { nested = true; break; }
            stack.push_back(c->gameObject);
        }
    }
    if (nested) return PrefabTools::SavePrefab(root, path, error);
    if (SceneSerializer::SaveObjectToFile(root, path)) return true;
    if (error) *error = "could not write " + path;
    return false;
}

std::string PrefabKindLabel(const PrefabInstanceInfo& i) {
    std::string s;
    switch (i.kind) {
        case PrefabKind::Variant: s = "Prefab Variant" + (i.baseName.empty() ? std::string() : " of " + i.baseName); break;
        case PrefabKind::Nested:  s = "Prefab with nested prefabs"; break;
        default:                  s = "Prefab"; break;
    }
    if (i.isNestedInstance) s += " (nested in " + i.outerName + ")";
    return s;
}

// ---- What differs -----------------------------------------------------------------------------------

std::vector<std::string> ChangedFieldsForLabel(const PrefabChange& ch, const Component& comp, const std::string& rawLabel) {
    std::vector<std::string> out;
    if (ch.fields.empty()) return out;
    std::string label = rawLabel.substr(0, rawLabel.find("##"));
    while (!label.empty() && label.back() == ' ') label.pop_back();
    const std::string want = Compact(label);
    if (want.empty()) return out;
    std::string prop;
    if (auto info = PropertyRegistry::FindFor(comp)) {
        for (const PropertyDesc& d : info->properties)
            if (Compact(PrettyName(d.name)) == want || Compact(d.name) == want) { prop = Lw(d.name); break; }
    }
    if (prop.empty()) prop = want;   // hand-drawn label that is also the field's name
    for (const PrefabFieldDiff& f : ch.fields) {
        const std::string fn = Compact(f.name);
        if (fn == Compact(prop) || fn.rfind(Compact(prop) + ".", 0) == 0 || (f.name.find('.') != std::string::npos && Compact(f.name.substr(0, f.name.find('.'))) == Compact(prop)))
            out.push_back(f.name);
    }
    return out;
}

std::vector<PrefabObjectChange> PrefabObjectChangesOf(GameObject& root) {
    std::vector<PrefabObjectChange> out;
    PrefabDiff d;
    if (root.prefabSource.empty() || !PrefabTools::Diff(root, d)) return out;
    for (const PrefabObjectDiff& o : d.objects) {
        PrefabObjectChange c;
        c.added = o.kind == PrefabObjectDiff::Kind::Added;
        c.live = o.live;
        c.prefabObject = c.added ? -1 : o.object;
        c.parentObject = o.parentObject;
        c.name = o.name;
        c.path = o.path;
        c.description = std::string(c.added ? "Added object " : "Removed object ") + o.name;
        out.push_back(std::move(c));
    }
    return out;
}

int PrefabOverrideCount(GameObject& root) {
    const double now = Now();
    auto& c = CountCache();
    const std::uint64_t id = root.InstanceId();
    auto it = c.find(id);
    if (it != c.end() && now - it->second.stamp < 1.5) return it->second.count;
    int n = 0;
    if (!root.prefabSource.empty()) {
        PrefabDiff d;
        if (PrefabTools::Diff(root, d)) n = (int)d.records.size() + (int)d.objects.size();
    }
    // Entries age out quickly, so the map cannot grow with every object ever counted.
    if (c.size() >= 512) {
        for (auto jt = c.begin(); jt != c.end() && c.size() >= 384;) {
            if (now - jt->second.stamp >= 1.5) jt = c.erase(jt); else ++jt;
        }
        while (c.size() >= 512) c.erase(c.begin());   // oldest stamps first, as a hard stop
    }
    CountEntry e; e.count = n; e.stamp = now;
    c[id] = e;
    return n;
}

PrefabTarget TargetOf(const PrefabChange& ch, const std::string& field) {
    PrefabTarget t;
    t.object = ch.prefabObject; t.record = ch.record; t.occurrence = ch.occurrence;
    t.field = ch.wholeRecordOnly || ch.added || ch.removed ? std::string() : field;
    return t;
}

// ---- Actions ----------------------------------------------------------------------------------------

PrefabOpResult PrefabRevert(EditorState& ed, GameObject& root, const std::vector<PrefabTarget>& targets) {
    if (root.prefabSource.empty()) return Fail("This object is not a prefab instance.");
    if (targets.empty()) return Fail("Nothing to revert.");
    ed.PushUndo("Revert Prefab Overrides");
    const RootSlot slot = CaptureRootSlot(ed.scene(), root);
    GameObject* cur = &root;
    std::string err;
    int done = 0;
    for (const PrefabTarget& t : targets) {
        GameObject* fresh = PrefabTools::RevertOverride(ed.scene(), *cur, t.object, t.record, t.field, t.occurrence, &err);
        if (!fresh) break;
        cur = fresh; ++done;
    }
    if (done == 0) { ed.DiscardLastUndo(); return Fail("Revert failed: " + err); }
    RestoreRootSlot(ed.scene(), cur, slot);
    ed.scene().FlushDestroyed();   // the instances this replaced are gone now, not at the end of the frame (the next undo step must not see both)
    ed.Select(cur);
    ed.dirty = true;
    ClearPrefabInfoCache();
    PrefabOpResult r; r.ok = true; r.root = cur;
    r.message = "Reverted to " + FileName(cur->prefabSource) + (done < (int)targets.size() ? " (stopped early: " + err + ")" : "");
    return r;
}

PrefabOpResult PrefabRevertAll(EditorState& ed, GameObject& root) {
    if (root.prefabSource.empty()) return Fail("This object is not a prefab instance.");
    ed.PushUndo("Revert Prefab");
    const RootSlot slot = CaptureRootSlot(ed.scene(), root);
    std::string err;
    GameObject* fresh = PrefabTools::RevertAll(ed.scene(), root, &err);
    if (!fresh) { ed.DiscardLastUndo(); return Fail("Revert failed: " + err); }
    RestoreRootSlot(ed.scene(), fresh, slot);
    ed.scene().FlushDestroyed();
    ed.Select(fresh);
    ed.dirty = true;
    ClearPrefabInfoCache();
    PrefabOpResult r; r.ok = true; r.root = fresh; r.message = "Reverted all changes to " + FileName(fresh->prefabSource);
    return r;
}

PrefabOpResult PrefabApply(EditorState& ed, GameObject& root, const std::vector<PrefabTarget>& targets) {
    if (targets.empty()) return Fail("Nothing to apply.");
    const std::string name = FileName(root.prefabSource);
    return SourceEdit(ed, root, "Applied to " + name, [&](std::string& err) {
        for (const PrefabTarget& t : targets)
            if (!PrefabTools::ApplyToSource(root, t.object, t.record, t.field, t.occurrence, &err)) return false;
        return true;
    });
}

std::string ApplyObjectBlockedReason(const PrefabInstanceInfo& info) {
    if (info.kind == PrefabKind::Variant)
        return "This is a variant: an added or removed object is kept by Create Variant, not written into the base.";
    if (info.isNestedInstance)
        return "This instance is a part of " + info.outerName + ": apply the object through the outer prefab.";
    return "";
}

namespace {

// The prefab at `source` instantiated into a scratch scene, with its objects in depth-first order.
struct Scratch {
    Scene scene{"prefab-scratch"};
    GameObject* root = nullptr;
    std::vector<GameObject*> objs;
    bool Load(const std::string& source, std::string& err) {
        scene.runScripts = false;
        root = PrefabTools::Instantiate(scene, source, {}, &err);
        if (!root) return false;
        Walk(root, objs);
        return true;
    }
    int IndexOf(const GameObject* g) const {
        for (std::size_t i = 0; i < objs.size(); ++i) if (objs[i] == g) return (int)i;
        return -1;
    }
};

// The live object at the same "Root/Arm/Hand" place as `g` in the scratch tree (the root maps to the root).
GameObject* LiveCounterpart(GameObject& liveRoot, const GameObject* g, const GameObject* scratchRoot) {
    std::vector<std::string> names;
    for (const Transform* t = g->transform; t && t->gameObject != scratchRoot; t = t->Parent()) names.push_back(t->gameObject->name);
    std::reverse(names.begin(), names.end());
    GameObject* cur = &liveRoot;
    for (const std::string& n : names) {
        GameObject* next = nullptr;
        for (Transform* c : cur->transform->Children())
            if (c && c->gameObject && c->gameObject->name == n && !c->gameObject->IsDestroyed()) { next = c->gameObject; break; }
        if (!next) return nullptr;
        cur = next;
    }
    return cur;
}

} // namespace

PrefabOpResult PrefabApplyObject(EditorState& ed, GameObject& root, const PrefabObjectChange& ch) {
    PrefabInstanceInfo info;
    if (!DescribePrefabInstance(root, info) || info.root != &root) return Fail("This object is not a prefab instance root.");
    if (const std::string why = ApplyObjectBlockedReason(info); !why.empty()) return Fail(why);
    if (ch.added && (!ch.live || ch.live->IsDestroyed())) return Fail("The object is gone.");
    return SourceEdit(ed, root, std::string(ch.added ? "Added " : "Removed ") + ch.name + (ch.added ? " to " : " from ") + info.name, [&](std::string& err) {
        Scratch sc;
        if (!sc.Load(root.prefabSource, err)) return false;
        if (ch.added) {
            if (ch.parentObject < 0 || ch.parentObject >= (int)sc.objs.size()) { err = "Cannot find where the object belongs in the prefab."; return false; }
            const std::string text = SceneSerializer::SerializeObject(*ch.live);
            GameObject* c = SceneSerializer::InstantiateFromText(sc.scene, text, &err);
            if (!c) return false;
            c->prefabSource.clear();   // its own prefab link (if any) is kept by SavePrefab through nestedprefab only when it has one
            if (!ch.live->prefabSource.empty()) c->prefabSource = ch.live->prefabSource;
            c->transform->SetParent(sc.objs[(std::size_t)ch.parentObject]->transform, false);
        } else {
            if (ch.prefabObject <= 0 || ch.prefabObject >= (int)sc.objs.size()) { err = "Cannot find the object in the prefab."; return false; }
            sc.scene.Destroy(sc.objs[(std::size_t)ch.prefabObject]);
            sc.scene.FlushDestroyed();
        }
        if (!PrefabTools::SavePrefab(*sc.root, root.prefabSource, &err)) return false;
        return PrefabTools::CaptureOverrides(root, &err);
    });
}

PrefabOpResult PrefabApplyAll(EditorState& ed, GameObject& root) {
    PrefabInstanceInfo info;
    if (!DescribePrefabInstance(root, info) || info.root != &root) return Fail("This object is not a prefab instance root.");
    const bool objectsOk = ApplyObjectBlockedReason(info).empty();
    return SourceEdit(ed, root, "Applied all changes to " + info.name, [&](std::string& err) {
        if (!PrefabTools::ApplyAllToSource(root, &err)) return false;
        if (!objectsOk) return true;
        for (int guard = 0; guard < 64; ++guard) {   // each object apply shifts the indices: look again after every one
            std::vector<PrefabObjectChange> objs = PrefabObjectChangesOf(root);
            if (objs.empty()) break;
            const PrefabObjectChange& c = objs.front();
            Scratch sc;
            if (!sc.Load(root.prefabSource, err)) return false;
            if (c.added) {
                if (c.parentObject < 0 || c.parentObject >= (int)sc.objs.size() || !c.live) { err = "Cannot find where " + c.name + " belongs in the prefab."; return false; }
                GameObject* n = SceneSerializer::InstantiateFromText(sc.scene, SceneSerializer::SerializeObject(*c.live), &err);
                if (!n) return false;
                n->transform->SetParent(sc.objs[(std::size_t)c.parentObject]->transform, false);
            } else {
                if (c.prefabObject <= 0 || c.prefabObject >= (int)sc.objs.size()) { err = "Cannot find " + c.name + " in the prefab."; return false; }
                sc.scene.Destroy(sc.objs[(std::size_t)c.prefabObject]);
                sc.scene.FlushDestroyed();
            }
            if (!PrefabTools::SavePrefab(*sc.root, root.prefabSource, &err)) return false;
            if (!PrefabTools::CaptureOverrides(root, &err)) return false;
        }
        return true;
    });
}

PrefabOpResult PrefabRevertObject(EditorState& ed, GameObject& root, const PrefabObjectChange& ch) {
    if (root.prefabSource.empty()) return Fail("This object is not a prefab instance.");
    if (ch.added) {
        if (!ch.live || ch.live->IsDestroyed() || !Inside(&root, ch.live) || ch.live == &root) return Fail("The object is gone.");
        ed.PushUndo("Revert Prefab Object");
        ed.Deselect(ch.live);
        ed.scene().Destroy(ch.live);
        ed.scene().FlushDestroyed();
        ed.dirty = true;
        ClearPrefabInfoCache();
        PrefabOpResult r; r.ok = true; r.root = &root; r.message = "Removed " + ch.name + " (it was added to the instance)";
        return r;
    }
    Scratch sc;
    std::string err;
    if (!sc.Load(root.prefabSource, err)) return Fail(err);
    if (ch.prefabObject <= 0 || ch.prefabObject >= (int)sc.objs.size()) return Fail("Cannot find " + ch.name + " in the prefab.");
    GameObject* proto = sc.objs[(std::size_t)ch.prefabObject];
    GameObject* parent = proto->transform->Parent() ? LiveCounterpart(root, proto->transform->Parent()->gameObject, sc.root) : nullptr;
    if (!parent) return Fail("The parent of " + ch.name + " is gone too: use Revert All.");
    ed.PushUndo("Restore Prefab Object");
    GameObject* back = SceneSerializer::InstantiateFromText(ed.scene(), SceneSerializer::SerializeObject(*proto), &err);
    if (!back) { ed.DiscardLastUndo(); return Fail(err); }
    back->transform->SetParent(parent->transform, false);
    ed.dirty = true;
    ClearPrefabInfoCache();
    PrefabOpResult r; r.ok = true; r.root = &root; r.message = "Brought " + ch.name + " back from " + FileName(root.prefabSource);
    return r;
}

std::string DefaultVariantPath(const std::string& source) {
    fs::path p(source);
    const std::string stem = p.stem().string();
    fs::path out = p.parent_path() / (stem + " Variant.okayprefab");
    std::error_code ec;
    for (int n = 2; fs::exists(out, ec); ++n) out = p.parent_path() / (stem + " Variant " + std::to_string(n) + ".okayprefab");
    return out.string();
}

PrefabOpResult PrefabCreateVariant(EditorState& ed, GameObject& root, const std::string& variantPath) {
    if (root.prefabSource.empty()) return Fail("This object is not a prefab instance.");
    std::error_code ec;
    if (fs::exists(variantPath, ec)) return Fail(FileName(variantPath) + " already exists.");
    ed.PushUndo("Create Prefab Variant");
    std::string err;
    if (!PrefabTools::SaveVariant(root, variantPath, &err)) { ed.DiscardLastUndo(); return Fail("Create Variant failed: " + err); }
    EditorState::FileEdit fe;
    fe.path = Norm(variantPath); fe.hadBefore = false; fe.hasAfter = true;
    // The file was just written and the step must carry what it holds: a failed read
    // would make Undo write an empty variant over the scene's changes.
    if (!EditorState::ReadTextFile(variantPath, fe.after)) { ed.DiscardLastUndo(); return Fail("Create Variant failed: cannot read back " + FileName(variantPath)); }
    // The instance now is an instance of the new variant: what it changed is stored there.
    const std::string baseName = FileName(root.prefabSource);
    root.prefabSource = variantPath;
    root.prefabOverrides.clear();
    PrefabTools::CaptureOverrides(root);
    ed.AttachUndoFiles({fe});
    SceneSerializer::ClearPrefabCache();
    ClearPrefabInfoCache();
    ed.dirty = true;
    PrefabOpResult r; r.ok = true; r.root = &root;
    r.message = "Created variant " + FileName(variantPath) + " of " + baseName + "; this object is now an instance of it";
    return r;
}

PrefabOpResult PrefabNewVariantFile(EditorState& ed, const std::string& basePath, const std::string& variantPath) {
    std::error_code ec;
    if (!fs::exists(basePath, ec)) return Fail(FileName(basePath) + " is missing.");
    if (fs::exists(variantPath, ec)) return Fail(FileName(variantPath) + " already exists.");
    Scratch sc;
    std::string err;
    if (!sc.Load(basePath, err)) return Fail("Cannot read " + FileName(basePath) + ": " + err);
    sc.root->prefabSource = basePath;
    if (!PrefabTools::SaveVariant(*sc.root, variantPath, &err)) return Fail("Create Variant failed: " + err);
    ed.PushUndo("Create Prefab Variant");   // the scene does not change; the step carries the new file so Undo deletes it
    EditorState::FileEdit fe;
    fe.path = Norm(variantPath); fe.hadBefore = false; fe.hasAfter = true;
    if (!EditorState::ReadTextFile(variantPath, fe.after)) { ed.DiscardLastUndo(); return Fail("Create Variant failed: cannot read back " + FileName(variantPath)); }
    ed.AttachUndoFiles({fe});
    ClearPrefabInfoCache();
    PrefabOpResult r; r.ok = true; r.message = "Created variant " + FileName(variantPath) + " of " + FileName(basePath);
    return r;
}

PrefabOpResult PrefabUnpack(EditorState& ed, GameObject& go, bool completely) {
    if (!PrefabTools::IsPrefabInstance(go)) return Fail("This object is not a prefab instance.");
    ed.PushUndo(completely ? "Unpack Prefab Completely" : "Unpack Prefab");
    const int n = PrefabTools::Unpack(go, completely);
    if (n == 0) { ed.DiscardLastUndo(); return Fail("Nothing to unpack."); }
    ed.dirty = true;
    ClearPrefabInfoCache();
    PrefabOpResult r; r.ok = true; r.root = &go;
    r.message = "Unpacked " + std::to_string(n) + " prefab link" + (n == 1 ? "" : "s");
    return r;
}

PrefabOpResult PrefabUpdateInstances(EditorState& ed, const std::string& sourcePath) {
    if (sourcePath.empty()) return Fail("No prefab.");
    const std::vector<std::string> files{Norm(sourcePath)};
    ed.PushUndo("Update Prefab Instances");   // before the capture: it edits the scene and belongs to the same step
    std::vector<Dependent> deps = CaptureDependents(ed.scene(), nullptr, files);   // keep what each one changed
    if (deps.empty()) { ed.DiscardLastUndo(); return Fail("No instance of " + FileName(sourcePath) + " in the scene."); }
    SceneSerializer::ClearPrefabCache();
    std::string err;
    PrefabOpResult r;
    r.updatedInstances = Rebuild(ed, deps, files, &err);
    ed.scene().FlushDestroyed();
    if (r.updatedInstances == 0) { ed.DiscardLastUndo(); return Fail(err.empty() ? "Nothing was updated." : "Update failed: " + err); }
    ed.dirty = true;
    ClearPrefabInfoCache();
    r.ok = true;
    r.message = "Updated " + std::to_string(r.updatedInstances) + " instance" + (r.updatedInstances == 1 ? "" : "s") + " of " + FileName(sourcePath);
    return r;
}

} // namespace oked
