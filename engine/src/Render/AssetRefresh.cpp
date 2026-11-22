// Refreshing placed models and loaded sounds when their files change (see okay/Render/AssetRefresh.hpp).
// Written for this engine from its own scene, import and audio interfaces; the matching rules (parts by their name path
// under the model's top object, the k-th same-named sibling) are this file's own.
#include "okay/Render/AssetRefresh.hpp"

#include "okay/Audio/AudioClip.hpp"
#include "okay/Audio/AudioLoad.hpp"
#include "okay/Components/Animator.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/BlendShapes.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/ModelSource.hpp"
#include "okay/Components/SkinnedMesh.hpp"
#include "okay/Core/ImportSidecar.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Scene/ComponentSerializer.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/PropertyBuilder.hpp"
#include "okay/Scene/ReflectedRecord.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <istream>
#include <map>
#include <ostream>
#include <set>
#include <sstream>
#include <system_error>

namespace okay {

namespace {
namespace fs = std::filesystem;

std::string Canon(const std::string& p) {
    std::error_code ec;
    fs::path a = fs::u8path(p);
    if (a.is_relative()) a = fs::absolute(a, ec);
    std::string s = a.lexically_normal().generic_u8string();
#ifdef _WIN32
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    return s;
}

std::string FileName(const std::string& p) {
    const std::size_t slash = p.find_last_of("/\\");
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

// The name a placed model keeps for its file: relative to the working folder (the project) when it is inside it, so a
// saved scene still finds the file when the project folder moves.
std::string StoredPath(const std::string& path) {
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    if (ec) return path;
    const fs::path abs = fs::u8path(Canon(path));
    const fs::path rel = abs.lexically_relative(cwd.lexically_normal());
    if (rel.empty() || rel.is_absolute()) return path;
    const std::string r = rel.generic_u8string();
    if (r.rfind("..", 0) == 0) return path;
    return r;
}

// Every part path below `root` -> the object (root excluded), in depth-first order.
std::vector<std::pair<std::string, GameObject*>> PartsOf(GameObject* root) {
    std::vector<std::pair<std::string, GameObject*>> out;
    if (!root) return out;
    root->ForEachDescendant([&](GameObject& g) {
        if (!g.IsDestroyed()) out.emplace_back(ModelNodePath(root, &g), &g);
    });
    return out;
}

// Copy a whole component's data from the fresh import onto a component just added to a placed part.
template <class T>
void CopyInto(T& dst, const T& src) {
    GameObject* go = dst.gameObject;
    const bool on = dst.enabled;
    dst = src;
    dst.gameObject = go;
    dst.enabled = on;
}

// Replace what a part takes from the file: geometry, skin, shapes, clips. The material stays.
// Returns true when geometry was replaced.
bool RefreshPart(GameObject& dst, GameObject& src) {
    bool geometry = false;
    auto* smr = src.GetComponent<MeshRenderer>();
    auto* dmr = dst.GetComponent<MeshRenderer>();
    if (smr && dmr) {
        dmr->mesh = smr->mesh;
        dmr->modifiedMesh.reset();
        dmr->modifiedHash = 0;
        if (dmr->slotTextures.size() != smr->slotTextures.size()) dmr->slotTextures = smr->slotTextures;
        geometry = true;
    } else if (smr && !dmr) {
        if (auto* add = dst.AddComponent<MeshRenderer>()) { CopyInto(*add, *smr); geometry = true; }
    } else if (!smr && dmr) {
        dst.RemoveComponent(dmr);
        geometry = true;
    }

    auto* ssk = src.GetComponent<SkinnedMesh>();
    auto* dsk = dst.GetComponent<SkinnedMesh>();
    if (ssk && dsk) {
        dsk->bind = ssk->bind;
        dsk->jointIdx = ssk->jointIdx;
        dsk->jointWt = ssk->jointWt;
        dsk->jointNames = ssk->jointNames;
        dsk->inverseBind = ssk->inverseBind;
        dsk->joints.clear();
        dsk->ResolveJoints();
        ++dsk->version;
        ++dsk->bindVersion;
        geometry = true;
    } else if (ssk && !dsk) {
        if (auto* add = dst.AddComponent<SkinnedMesh>()) { CopyInto(*add, *ssk); add->joints.clear(); add->ResolveJoints(); geometry = true; }
    } else if (!ssk && dsk) {
        dst.RemoveComponent(dsk);
        geometry = true;
    }

    auto* sbs = src.GetComponent<BlendShapes>();
    auto* dbs = dst.GetComponent<BlendShapes>();
    if (sbs && dbs) {
        std::map<std::string, float> weights;   // the weights you set stay, for shapes that are still there
        for (const auto& sh : dbs->shapes) weights[sh.name] = sh.weight;
        dbs->shapes = sbs->shapes;
        for (auto& sh : dbs->shapes) { auto it = weights.find(sh.name); if (it != weights.end()) sh.weight = it->second; }
        dbs->base = Mesh();
    } else if (sbs && !dbs) {
        if (auto* add = dst.AddComponent<BlendShapes>()) CopyInto(*add, *sbs);
    } else if (!sbs && dbs) {
        dst.RemoveComponent(dbs);
    }

    auto* sma = src.GetComponent<ModelAnimator>();
    auto* dma = dst.GetComponent<ModelAnimator>();
    if (sma && dma) dma->clips = sma->clips;   // the slot names (idle, walk ...) you picked stay
    else if (sma && !dma) { if (auto* add = dst.AddComponent<ModelAnimator>()) CopyInto(*add, *sma); }
    else if (!sma && dma) dst.RemoveComponent(dma);

    auto* san = src.GetComponent<Animator>();
    auto* dan = dst.GetComponent<Animator>();
    if (san && dan) dan->clip = san->clip;
    else if (san && !dan) { if (auto* add = dst.AddComponent<Animator>()) CopyInto(*add, *san); }
    else if (!san && dan) dst.RemoveComponent(dan);
    return geometry;
}

std::string DisplayName(const std::string& file) { return FileName(file); }
} // namespace

bool SameAssetFile(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return false;
    const std::string ca = Canon(a), cb = Canon(b);
    if (ca == cb) return true;
    if (FileName(ca) != FileName(cb)) return false;
    std::error_code ec;
    return fs::equivalent(fs::u8path(a), fs::u8path(b), ec) && !ec;
}

std::string ModelNodePath(const GameObject* root, const GameObject* node) {
    if (!root || !node || node == root || !node->transform) return {};
    std::vector<std::string> parts;
    for (const Transform* t = node->transform; t && t->gameObject != root; t = t->Parent()) {
        const GameObject* g = t->gameObject;
        if (!g) return {};
        const Transform* p = t->Parent();
        if (!p) return {};   // walked off the top without meeting `root`: not under it
        int k = 0;
        for (const Transform* sib : p->Children()) {
            if (sib == t) break;
            if (sib && sib->gameObject && sib->gameObject->name == g->name && !sib->gameObject->IsDestroyed()) ++k;
        }
        parts.push_back(k ? g->name + "#" + std::to_string(k) : g->name);
    }
    std::string out;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) { if (!out.empty()) out += '/'; out += *it; }
    return out;
}

ModelSource* MarkModelSource(GameObject* root, const std::string& path, float appliedScale) {
    if (!root || path.empty()) return nullptr;
    ModelSource* src = root->GetComponent<ModelSource>();
    if (!src) src = root->AddComponent<ModelSource>();
    if (!src) return nullptr;
    src->path = StoredPath(path);
    src->appliedScale = appliedScale > 0.0f ? appliedScale : 1.0f;
    src->nodes.clear();
    for (auto& kv : PartsOf(root)) src->nodes.push_back(kv.first);
    return src;
}

std::string ModelRefreshReport::Summary() const {
    std::string s;
    if (instances == 0) s = "No placed model was refreshed";
    else s = "Refreshed " + std::to_string(instances) + (instances == 1 ? " placed model" : " placed models");
    std::vector<std::string> bits;
    if (meshesUpdated) bits.push_back(std::to_string(meshesUpdated) + (meshesUpdated == 1 ? " mesh updated" : " meshes updated"));
    if (added) bits.push_back(std::to_string(added) + (added == 1 ? " part added" : " parts added"));
    if (removed) bits.push_back(std::to_string(removed) + (removed == 1 ? " part removed" : " parts removed"));
    if (userChildrenKept) bits.push_back(std::to_string(userChildrenKept) + (userChildrenKept == 1 ? " of your objects moved up" : " of your objects moved up"));
    if (scaleChanged) bits.push_back("new Scale");
    for (std::size_t i = 0; i < bits.size(); ++i) s += (i ? ", " : ": ") + bits[i];
    if (!failed.empty()) s += std::string(instances ? "; " : ": ") + "could not import " + DisplayName(failed.front()) + " (left as it was)";
    return s;
}

bool RefreshModelInstance(Scene& scene, GameObject* root, ModelRefreshReport* report) {
    ModelSource* src = root ? root->GetComponent<ModelSource>() : nullptr;
    if (!src || src->path.empty()) return false;
    const std::string file = src->path;

    Scene fresh("model refresh");
    bool ok = false;
    GameObject* froot = ImportModelScene(fresh, file, &ok);
    if (!ok || !froot) {
        if (report) report->failed.push_back(file);
        return false;
    }
    ModelRefreshReport local;
    ModelRefreshReport& rep = report ? *report : local;

    // The top object: its transform is yours; only a changed Scale setting changes its size.
    const float scale = ModelImportScale(file);
    if (std::fabs(scale - src->appliedScale) > 1e-6f && src->appliedScale > 0.0f) {
        root->transform->localScale = root->transform->localScale * (scale / src->appliedScale);
        rep.scaleChanged = true;
    }
    src->appliedScale = scale;
    if (RefreshPart(*root, *froot)) ++rep.meshesUpdated;

    const auto freshParts = PartsOf(froot);
    std::set<std::string> freshNames;
    for (auto& kv : freshParts) freshNames.insert(kv.first);
    // With no list of parts (a Model Source added by hand to an older model), every object whose path the file has
    // counts as the model's part, and nothing is removed.
    std::set<std::string> oldNodes(src->nodes.begin(), src->nodes.end());
    if (src->nodes.empty())
        for (auto& kv : PartsOf(root)) if (freshNames.count(kv.first)) oldNodes.insert(kv.first);
    std::map<std::string, GameObject*> placed;      // the model's parts as placed, by path
    for (auto& kv : PartsOf(root)) if (oldNodes.count(kv.first)) placed[kv.first] = kv.second;

    // Parts the file still has: new geometry. Parts it gained: copied in under their parent's placed object.
    std::set<std::string> covered;   // fresh parts brought in as part of a new subtree
    for (auto& kv : freshParts) {
        const std::string& path = kv.first;
        if (covered.count(path)) continue;
        auto it = placed.find(path);
        if (it != placed.end()) {
            if (RefreshPart(*it->second, *kv.second)) ++rep.meshesUpdated;
            continue;
        }
        const std::size_t slash = path.find_last_of('/');
        const std::string parentPath = slash == std::string::npos ? std::string() : path.substr(0, slash);
        GameObject* parent = parentPath.empty() ? root : nullptr;
        if (!parent) { auto pit = placed.find(parentPath); if (pit != placed.end()) parent = pit->second; }
        if (!parent) continue;   // its parent is new too: it comes in with that parent's copy
        GameObject* copy = scene.Instantiate(*kv.second, parent->transform, /*worldPositionStays=*/false);
        if (!copy) continue;
        copy->name = kv.second->name;   // the file's name, not a "(1)" variant
        copy->transform->localPosition = kv.second->transform->localPosition;
        copy->transform->localRotation = kv.second->transform->localRotation;
        copy->transform->localScale = kv.second->transform->localScale;
        ++rep.added;
        kv.second->ForEachDescendant([&](GameObject& g) { covered.insert(ModelNodePath(froot, &g)); });
    }

    // Parts the file lost: your objects under them move up first, then the part goes.
    std::vector<GameObject*> gone;
    for (auto& kv : placed) if (!freshNames.count(kv.first)) gone.push_back(kv.second);
    std::set<GameObject*> goneSet(gone.begin(), gone.end());
    for (GameObject* g : gone) {
        std::vector<Transform*> kids(g->transform->Children().begin(), g->transform->Children().end());
        for (Transform* c : kids) {
            if (!c || !c->gameObject || c->gameObject->IsDestroyed()) continue;
            if (oldNodes.count(ModelNodePath(root, c->gameObject))) continue;   // a model part: goes with it
            Transform* up = g->transform->Parent();
            while (up && up->gameObject && goneSet.count(up->gameObject)) up = up->Parent();
            if (!up) up = root->transform;
            if (c->SetParent(up, /*worldPositionStays=*/true)) ++rep.userChildrenKept;
        }
    }
    for (GameObject* g : gone) {
        bool topmost = true;   // destroying the topmost removed part takes the removed parts under it along
        for (Transform* t = g->transform->Parent(); t; t = t->Parent()) if (t->gameObject && goneSet.count(t->gameObject)) { topmost = false; break; }
        if (topmost) scene.Destroy(g);
        ++rep.removed;
    }

    // Remember the parts as they are now: the file's parts that are placed (renamed ones and yours are not listed).
    src->nodes.clear();
    for (auto& kv : PartsOf(root)) {
        if (kv.second->IsDestroyed() || scene.IsDestroyPending(kv.second)) continue;
        if (freshNames.count(kv.first)) src->nodes.push_back(kv.first);
    }
    ++rep.instances;
    scene.MarkStructureChanged();
    return true;
}

ModelRefreshReport RefreshModelsForFile(Scene& scene, const std::string& file) {
    ModelRefreshReport rep;
    std::vector<GameObject*> roots;
    for (const auto& up : scene.Objects()) {
        if (!up || up->IsDestroyed()) continue;
        if (auto* src = up->GetComponent<ModelSource>(); src && SameAssetFile(src->path, file)) roots.push_back(up.get());
    }
    for (GameObject* r : roots) {
        const std::size_t failedBefore = rep.failed.size();
        RefreshModelInstance(scene, r, &rep);
        if (rep.failed.size() > failedBefore) {
            rep.failed.resize(failedBefore + 1);
            break;   // the file does not import: the other copies would fail the same way
        }
    }
    if (!rep.failed.empty()) Log::Warning("[asset] could not reimport model '", file, "': placed copies left as they were");
    return rep;
}

int RefreshAudioForFile(Scene& scene, const std::string& file) {
    int n = 0;
    for (AudioSource* au : scene.FindObjectsOfType<AudioSource>()) {
        if (!au || au->clipPath.empty() || !au->HasAudio() || !SameAssetFile(au->clipPath, file)) continue;
        std::string err;
        const bool wasPlaying = au->IsPlaying();
        if (au->UsesSharedClip()) {
            auto clip = AudioClip::LoadCached(au->clipPath, au->Clip().sampleRate, &err);
            if (!clip) { Log::Warning("[asset] could not reload sound '", file, "': ", err); continue; }
            if (AudioImportWantsStream(au->clipPath) || au->stream) { if (AttachAudioFile(*au, au->clipPath, &err) == AudioAttach::Failed) continue; }
            else au->UseSharedClip(clip);
        } else if (AttachAudioFile(*au, au->clipPath, &err) == AudioAttach::Failed) {
            Log::Warning("[asset] could not reload sound '", file, "': ", err);
            continue;
        }
        if (wasPlaying) au->Play();
        ++n;
    }
    return n;
}

namespace detail {
void RegisterModelSourceProperties() {
    PropertyBuilder<ModelSource> b("ModelSource", "modelsource", "Rendering", /*withEnabled=*/false);
    b.AddAssetPath("path", &ModelSource::path, "model").Tooltip("The model file this object was placed from. When the file changes, is reimported or gets new import settings, the model here is brought up to date: its meshes and animation clips come from the file again; its objects, their positions, your material changes and objects you added under it stay")
     .AddFloat("appliedScale", &ModelSource::appliedScale).Hidden();
    b.Commit();
}

void RegisterModelSourceSerializer() {
    ComponentSerializerRegistry::Entry e;
    e.name = "modelsource";
    e.write = [](const GameObject& go, std::ostream& out) {
        for (const ModelSource* s : go.GetComponents<ModelSource>()) {
            std::ostringstream num;
            num << s->appliedScale;
            out << "  modelsource " << reflected::QuoteValue(s->path) << ' ' << num.str() << ' ' << s->nodes.size();
            for (const std::string& n : s->nodes) out << ' ' << reflected::QuoteValue(n);
            out << "\n";
        }
    };
    e.read = [](GameObject& go, std::istream& in) {
        std::string line;
        std::getline(in, line);
        std::size_t i = 0;
        auto quoted = [&](std::string& v) {
            while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
            if (i >= line.size() || line[i] != '"') return false;
            ++i;
            v.clear();
            while (i < line.size() && line[i] != '"') { if (line[i] == '\\' && i + 1 < line.size()) ++i; v += line[i++]; }
            if (i >= line.size()) return false;
            ++i;
            return true;
        };
        auto word = [&](std::string& v) {
            while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
            const std::size_t s0 = i;
            while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) ++i;
            v = line.substr(s0, i - s0);
            return !v.empty();
        };
        ModelSource* s = go.GetComponent<ModelSource>();
        if (!s) s = go.AddComponent<ModelSource>();
        if (!s) return;
        std::string path, scale, count;
        if (!quoted(path)) return;
        s->path = path;
        if (!word(scale)) return;
        const float k = static_cast<float>(std::atof(scale.c_str()));
        s->appliedScale = k > 0.0f && k <= 10000.0f ? k : 1.0f;
        if (!word(count)) return;
        const long n = std::atol(count.c_str());
        s->nodes.clear();
        for (long j = 0; j < n && j < 100000; ++j) {
            std::string node;
            if (!quoted(node)) break;
            s->nodes.push_back(node);
        }
    };
    ComponentSerializerRegistry::Register(std::move(e));
}
} // namespace detail

} // namespace okay
