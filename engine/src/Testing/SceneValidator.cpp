#include "okay/Testing/SceneValidator.hpp"

#include "okay/Components/AudioSource.hpp"
#include "okay/Components/BehaviorTreeRunner.hpp"
#include "okay/Components/BuildMode.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/CameraFollow.hpp"
#include "okay/Components/CinemachineBrain.hpp"
#include "okay/Components/DayNightCycle.hpp"
#include "okay/Components/EventSystem.hpp"
#include "okay/Components/FollowTarget2D.hpp"
#include "okay/Components/Foliage.hpp"
#include "okay/Components/Decal.hpp"
#include "okay/Components/LineRenderer.hpp"
#include "okay/Components/LogicLinks.hpp"
#include "okay/Components/LootSource.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Minimap.hpp"
#include "okay/Components/MusicPlayer.hpp"
#include "okay/Components/NavMeshSurface.hpp"
#include "okay/Components/NoCode.hpp"
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Components/PauseMenu.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/SequencePlayer.hpp"
#include "okay/Components/SplineMesh.hpp"
#include "okay/Components/SpriteAnimator.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/SteamManager.hpp"
#include "okay/Components/Terrain.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIImage.hpp"
#include "okay/Components/UIRawImage.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Components/Volume.hpp"
#include "okay/Components/VoxelTerrain.hpp"
#include "okay/Physics/Collider2D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/EnsureCamera.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Core/DataPack.hpp"
#include "okay/Scripting/ScriptVM.hpp"
#include "okay/Testing/ObjectPath.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace okay::testing {

const char* SeverityName(Severity s) {
    switch (s) {
        case Severity::Info:    return "info";
        case Severity::Warning: return "warning";
        case Severity::Error:   return "error";
    }
    return "info";
}

// ---- Report ----------------------------------------------------------------

int ValidationReport::Count(Severity s) const {
    int n = 0;
    for (const auto& i : issues) if (i.severity == s) ++n;
    return n;
}

int ValidationReport::CountCode(const std::string& code) const {
    int n = 0;
    for (const auto& i : issues) if (i.code == code) ++n;
    return n;
}

namespace {

std::string JsonEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 2);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
                else o += (char)c;
        }
    }
    return o;
}

std::string Plural(int n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

} // namespace

std::string ValidationReport::ToText(Severity atLeast) const {
    std::string out = "Scene '" + sceneName + "': ";
    if (issues.empty()) out += "no issues\n";
    else out += Plural(Errors(), "error", "errors") + ", " + Plural(Warnings(), "warning", "warnings") + ", " +
                Plural(Infos(), "info", "infos") + "\n";
    std::vector<const ValidationIssue*> order;
    for (const auto& i : issues) if ((int)i.severity >= (int)atLeast) order.push_back(&i);
    std::stable_sort(order.begin(), order.end(), [](const ValidationIssue* a, const ValidationIssue* b) {
        return (int)a->severity > (int)b->severity;
    });
    for (const ValidationIssue* i : order) {
        std::string tag = SeverityName(i->severity);
        for (char& c : tag) c = (char)std::toupper((unsigned char)c);
        out += "[" + tag + "] ";
        if (!i->object.empty()) out += i->object + ": ";
        out += i->message;
        out += " [" + i->code + "]\n";
    }
    return out;
}

std::string ValidationReport::ToJson() const {
    std::string j = "{\"scene\":\"" + JsonEscape(sceneName) + "\",\"errors\":" + std::to_string(Errors()) +
                    ",\"warnings\":" + std::to_string(Warnings()) + ",\"infos\":" + std::to_string(Infos()) +
                    ",\"issues\":[";
    for (std::size_t n = 0; n < issues.size(); ++n) {
        const ValidationIssue& i = issues[n];
        if (n) j += ",";
        j += "{\"severity\":\"" + std::string(SeverityName(i.severity)) + "\",\"code\":\"" + JsonEscape(i.code) +
             "\",\"object\":\"" + JsonEscape(i.object) + "\",\"component\":\"" + JsonEscape(i.component) +
             "\",\"message\":\"" + JsonEscape(i.message) + "\"}";
    }
    j += "]}";
    return j;
}

// ---- Assets ----------------------------------------------------------------

namespace {

bool DiskFileExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(std::filesystem::path(path), ec);
}

std::string Slashes(std::string p) {
    for (char& c : p) if (c == '\\') c = '/';
    return p;
}

std::string JoinPath(const std::string& dir, const std::string& rel) {
    if (dir.empty()) return rel;
    const char last = dir.back();
    return (last == '/' || last == '\\') ? dir + rel : dir + "/" + rel;
}

} // namespace

bool SceneValidator::AssetExists(const std::string& path, const ValidatorOptions& o) {
    if (path.empty()) return true;
    if (path.rfind("proc:", 0) == 0 || path.find("://") != std::string::npos) return true;   // built in / remote
    if (path[0] == '@') return true;   // a picture made in memory (a terrain's baked layers)
    auto exists = [&](const std::string& p) { return o.fileExists ? o.fileExists(p) : DiskFileExists(p); };
    const std::string rel = Slashes(path);
    if (std::filesystem::path(rel).is_absolute()) return exists(rel);
    if (exists(JoinPath(o.assetRoot, rel))) return true;
    for (const auto& dir : o.searchDirs)
        if (exists(JoinPath(dir, rel))) return true;
    return false;
}

std::vector<AssetRef> SceneValidator::AssetReferences(const GameObject& go) {
    std::vector<AssetRef> out;
    auto add = [&](const char* comp, const char* field, const char* kind, const std::string& path) {
        if (!path.empty()) out.push_back({comp, field, kind, path});
    };
    for (auto* c : go.GetComponents<SpriteRenderer>()) add("SpriteRenderer", "texture", "texture", c->texture);
    for (auto* c : go.GetComponents<AudioSource>()) add("AudioSource", "clipPath", "audio", c->clipPath);
    for (auto* c : go.GetComponents<MusicPlayer>())
        for (const auto& t : c->tracks) add("MusicPlayer", "tracks", "audio", t);
    for (auto* c : go.GetComponents<MeshRenderer>()) {
        add("MeshRenderer", "meshPath", "mesh", c->meshPath);
        add("MeshRenderer", "texture", "texture", c->texture);
        add("MeshRenderer", "normalMap", "texture", c->normalMap);
        add("MeshRenderer", "specularMap", "texture", c->specularMap);
        add("MeshRenderer", "aoMap", "texture", c->aoMap);
    }
    for (auto* c : go.GetComponents<Terrain>()) {
        add("Terrain", "texture", "texture", c->texture);
        add("Terrain", "normalMap", "texture", c->normalMap);
        for (const auto& l : c->layers) add("Terrain", "layers.texture", "texture", l.texture);
    }
    for (auto* c : go.GetComponents<Decal>()) add("Decal", "texture", "texture", c->texture);
    for (auto* c : go.GetComponents<Foliage>()) {
        add("Foliage", "meshPath", "mesh", c->meshPath);
        add("Foliage", "prefabPath", "prefab", c->prefabPath);
    }
    for (auto* c : go.GetComponents<VoxelTerrain>()) add("VoxelTerrain", "texture", "texture", c->texture);
    for (auto* c : go.GetComponents<ParticleSystem>()) add("ParticleSystem", "texture", "texture", c->texture);
    for (auto* c : go.GetComponents<UIImage>()) add("UIImage", "texture", "texture", c->texture);
    for (auto* c : go.GetComponents<UIRawImage>()) add("UIRawImage", "texture", "texture", c->texture);
    for (auto* c : go.GetComponents<UIButton>()) {
        add("UIButton", "icon", "texture", c->icon);
        add("UIButton", "fontPath", "font", c->fontPath);
    }
    for (auto* c : go.GetComponents<TextRenderer>()) add("TextRenderer", "fontPath", "font", c->fontPath);
    for (auto* c : go.GetComponents<SpriteAnimator>()) {
        for (const auto& f : c->frames) add("SpriteAnimator", "frames", "texture", f);
        for (const auto& clip : c->clips)
            for (const auto& f : clip.frames) add("SpriteAnimator", "clips", "texture", f);
    }
    for (auto* c : go.GetComponents<BuildMode>())
        for (const auto& it : c->palette) {
            add("BuildMode", "palette.prefab", "prefab", it.prefab);
            add("BuildMode", "palette.icon", "texture", it.icon);
        }
    for (auto* c : go.GetComponents<SequencePlayer>()) add("SequencePlayer", "sequencePath", "data", c->sequencePath);
    for (auto* c : go.GetComponents<LogicTarget>()) add("LogicTarget", "spawnPrefab", "prefab", c->spawnPrefab);
    for (auto* c : go.GetComponents<LootSource>()) add("LootSource", "table", "data", c->table);
    for (auto* c : go.GetComponents<NavMeshSurface>()) add("NavMeshSurface", "navPath", "data", c->navPath);
    for (auto* c : go.GetComponents<BehaviorTreeRunner>()) add("BehaviorTreeRunner", "treePath", "data", c->treePath);
    for (auto* c : go.GetComponents<SplineMesh>())
        if (c->sourceMesh.find('.') != std::string::npos) add("SplineMesh", "sourceMesh", "mesh", c->sourceMesh);
    for (auto* c : go.GetComponents<SplineInstancer>()) add("SplineInstancer", "prefabPath", "prefab", c->prefabPath);
    for (auto* c : go.GetComponents<LineRenderer>()) add("LineRenderer", "texture", "texture", c->texture);
    for (auto* c : go.GetComponents<TrailRenderer>()) add("TrailRenderer", "texture", "texture", c->texture);
    return out;
}

// ---- Checks ----------------------------------------------------------------

namespace {

struct Checker {
    const Scene& scene;
    const ValidatorOptions& opt;
    ValidationReport& rep;

    void Add(Severity s, const char* code, const std::string& object, const std::string& component,
             const std::string& message) {
        rep.issues.push_back({s, code, object, component, message});
    }

    static std::string Num(float v) {
        char b[32];
        std::snprintf(b, sizeof(b), "%g", (double)v);
        return b;
    }

    void CheckTransform(const GameObject& go, const std::string& path) {
        const auto& t = *go.transform;
        const float comps[10] = {t.localPosition.x, t.localPosition.y, t.localPosition.z,
                                 t.localRotation.x, t.localRotation.y, t.localRotation.z, t.localRotation.w,
                                 t.localScale.x, t.localScale.y, t.localScale.z};
        bool bad = false;
        for (float c : comps) if (!std::isfinite(c)) bad = true;
        if (bad) {
            Add(Severity::Error, "transform.non_finite", path, "Transform",
                "Transform has a NaN or infinite value: position (" + Num(t.localPosition.x) + ", " +
                Num(t.localPosition.y) + ", " + Num(t.localPosition.z) + "), rotation (" + Num(t.localRotation.x) +
                ", " + Num(t.localRotation.y) + ", " + Num(t.localRotation.z) + ", " + Num(t.localRotation.w) +
                "), scale (" + Num(t.localScale.x) + ", " + Num(t.localScale.y) + ", " + Num(t.localScale.z) + ")");
            return;   // the other transform checks are meaningless on NaN
        }
        if (t.localScale.x == 0.0f || t.localScale.y == 0.0f || t.localScale.z == 0.0f)
            Add(Severity::Warning, "transform.zero_scale", path, "Transform",
                "Scale has a zero component (" + Num(t.localScale.x) + ", " + Num(t.localScale.y) + ", " +
                Num(t.localScale.z) + "): the object collapses to nothing on that axis");
        const double len2 = (double)t.localRotation.x * t.localRotation.x + (double)t.localRotation.y * t.localRotation.y +
                            (double)t.localRotation.z * t.localRotation.z + (double)t.localRotation.w * t.localRotation.w;
        if (std::fabs(std::sqrt(len2) - 1.0) > 0.01)
            Add(Severity::Warning, "transform.rotation_unnormalized", path, "Transform",
                "Rotation quaternion has length " + Num((float)std::sqrt(len2)) + " instead of 1");
        const float m = opt.maxCoordinate;
        if (std::fabs(t.localPosition.x) > m || std::fabs(t.localPosition.y) > m || std::fabs(t.localPosition.z) > m)
            Add(Severity::Warning, "transform.extreme_position", path, "Transform",
                "Position (" + Num(t.localPosition.x) + ", " + Num(t.localPosition.y) + ", " + Num(t.localPosition.z) +
                ") is beyond " + Num(m) + " units; floating point precision is poor out there");
    }

    void Assets(const GameObject& go, const std::string& path) {
        for (const AssetRef& r : SceneValidator::AssetReferences(go))
            if (!SceneValidator::AssetExists(r.path, opt))
                Add(opt.missingAssetSeverity, "asset.missing", path, r.component,
                    "Missing " + r.kind + " '" + r.path + "' (" + r.component + "." + r.field + ")");
        if (!go.prefabSource.empty() && !SceneValidator::AssetExists(go.prefabSource, opt))
            Add(Severity::Warning, "asset.prefab_missing", path, "",
                "Instance of prefab '" + go.prefabSource + "' but that file is missing (it cannot Apply or Revert)");
    }

    void Scripts(const GameObject& go, const std::string& path) {
        for (ScriptComponent* sc : go.GetComponents<ScriptComponent>()) {
            const std::string& file = sc->Path();
            const bool embedded = !sc->Source().empty();
            bool fileOk = true;
            if (!file.empty()) {
                fileOk = SceneValidator::AssetExists(file, opt);
                if (!fileOk)
                    Add(embedded ? Severity::Warning : Severity::Error, "script.missing_file", path, "ScriptComponent",
                        embedded ? "Script file '" + file + "' is missing; the copy saved in the scene would run instead"
                                 : "References script file '" + file + "' which does not exist, and no source is saved in the scene");
            } else if (!embedded) {
                Add(Severity::Warning, "script.empty", path, "ScriptComponent", "Script has no source and no file");
            }
            // Does the text compile? The saved copy if there is one, else the file's content.
            std::string source = sc->Source();
            if (!embedded && !file.empty() && fileOk) {
                std::ifstream in(file, std::ios::binary);
                if (!in && !opt.assetRoot.empty()) in.open(JoinPath(opt.assetRoot, Slashes(file)), std::ios::binary);
                if (in) { std::ostringstream ss; ss << in.rdbuf(); source = ss.str(); }
            }
            if (!source.empty()) {
                std::unique_ptr<IScriptVM> vm = CreateScriptVM(sc->Language());
                if (!vm) {
                    std::string why = RemovedScriptLanguageMessage(sc->Language());
                    if (why.empty()) why = "Script language '" + sc->Language() + "' is not available in this build";
                    Add(Severity::Error, "script.backend", path, "ScriptComponent", why);
                } else {
                    std::string err;
                    if (!vm->Validate(source, &err))
                        Add(Severity::Error, "script.compile", path, "ScriptComponent",
                            "Script does not compile: " + err);
                }
            }
        }
        for (VisualScriptComponent* vs : go.GetComponents<VisualScriptComponent>()) {
            const bool embedded = !vs->Source().empty();
            if (!vs->Path().empty()) {
                if (!SceneValidator::AssetExists(vs->Path(), opt))
                    Add(embedded ? Severity::Warning : Severity::Error, "script.missing_file", path, "VisualScriptComponent",
                        embedded ? "Visual script file '" + vs->Path() + "' is missing; the copy saved in the scene would run instead"
                                 : "References visual script file '" + vs->Path() + "' which does not exist, and no graph is saved in the scene");
            } else if (!embedded) {
                Add(Severity::Warning, "script.empty", path, "VisualScriptComponent", "Visual script has no graph and no file");
            }
        }
    }

    // A component that names another object (a follow target, a teleport destination).
    void Reference(const std::string& path, const char* component, const char* field, const std::string& name) {
        if (name.empty() || scene.Find(name)) return;
        Add(Severity::Warning, "ref.missing_object", path, component,
            std::string(component) + "." + field + " names '" + name + "' but no object has that name");
    }

    void References(const GameObject& go, const std::string& path) {
        for (auto* c : go.GetComponents<CameraFollow>()) Reference(path, "CameraFollow", "targetName", c->targetName);
        for (auto* c : go.GetComponents<FollowTarget2D>()) Reference(path, "FollowTarget2D", "target", c->target);
        for (auto* c : go.GetComponents<Minimap>()) Reference(path, "Minimap", "target", c->target);
        for (auto* c : go.GetComponents<Teleporter>()) Reference(path, "Teleporter", "targetName", c->targetName);
        for (auto* c : go.GetComponents<TriggerZone>())
            if (c->action == (int)TriggerZone::Action::ActivateTarget || c->action == (int)TriggerZone::Action::DeactivateTarget)
                Reference(path, "TriggerZone", "targetName", c->targetName);
    }

    void Physics(const GameObject& go, const std::string& path) {
        if (go.GetComponent<Rigidbody2D>() && !go.GetComponent<Collider2D>())
            Add(Severity::Warning, "physics.rigidbody_no_collider", path, "Rigidbody2D",
                "Rigidbody2D without a 2D collider: it will fall through everything");
        if (go.GetComponent<Rigidbody3D>() && !go.GetComponent<Collider3D>())
            Add(Severity::Warning, "physics.rigidbody_no_collider", path, "Rigidbody3D",
                "Rigidbody3D without a 3D collider: it will fall through everything");
    }

    void FoliageRules(const GameObject& go, const std::string& path) {
        for (auto* f : go.GetComponents<Foliage>()) {
            // Const-cast: Problem() / TrianglesPerInstance() build the shape on first use, which is not an edit.
            Foliage* fm = const_cast<Foliage*>(f);
            const int tpi = fm->TrianglesPerInstance();
            if (!fm->Problem().empty())
                Add(Severity::Error, "foliage.shape", path, "Foliage", "Foliage layer cannot draw: " + fm->Problem());
            if (fm->Count() > 0 && fm->TotalTriangles() > fm->maxTriangles)
                Add(Severity::Warning, "foliage.budget", path, "Foliage",
                    "Foliage layer has " + std::to_string(fm->Count()) + " copies (" + std::to_string(fm->Count() * tpi) +
                    " triangles), over its budget of " + std::to_string(fm->maxTriangles) + ": erase some or raise the budget");
            else if (fm->Count() == 0)
                Add(Severity::Info, "foliage.empty", path, "Foliage", "Foliage layer has nothing painted yet");
        }
    }

    // Volumes and logic zones: a region with no size never holds anything, and a zone that does nothing is a mistake.
    void Volumes(const GameObject& go, const std::string& path) {
        for (auto* v : go.GetComponents<Volume>()) {
            if (!v->Geometry().Valid())
                Add(Severity::Warning, "volume.zero_size", path, "Volume",
                    std::string(Volume::KindName(v->kind)) + " volume has a size of zero or less on an axis (" + Num(v->size.x) +
                    " x " + Num(v->size.y) + " x " + Num(v->size.z) + "): nothing is ever inside it");
            if (v->kind == Volume::Kind::Force && v->forceStrength == 0.0f)
                Add(Severity::Warning, "volume.force_zero", path, "Volume", "Force volume has a strength of 0: it pushes nothing");
            if (v->kind == Volume::Kind::Ambience) {
                auto* a = go.GetComponent<AudioSource>();
                if (!a)
                    Add(Severity::Warning, "volume.no_audio", path, "Volume",
                        "Ambience volume needs an Audio Source on the same object: there is nothing to play");
                else if (a->clipPath.empty() && !a->HasAudio())
                    Add(Severity::Warning, "volume.no_audio", path, "Volume",
                        "Ambience volume's Audio Source has no clip: there is nothing to play");
            }
        }
        for (auto* ls : go.GetComponents<LogicSource>()) {
            if ((ls->kind == LogicSource::Kind::PressurePlate || ls->kind == LogicSource::Kind::TriggerZone) &&
                !(ls->zoneSize.x > 0.0f && ls->zoneSize.y > 0.0f && ls->zoneSize.z > 0.0f))
                Add(Severity::Warning, "volume.zero_size", path, "LogicSource",
                    std::string(LogicSource::KindName(ls->kind)) + " zone has a size of zero or less on an axis: nothing is ever inside it");
        }
    }

    static int CountDescendants(const Transform* t) {
        int n = 0;
        for (const Transform* c : t->Children()) n += 1 + CountDescendants(c);
        return n;
    }

    void Inactive(const GameObject& go, const std::string& path) {
        if (go.active) return;
        const Transform* parent = go.transform->Parent();
        if (parent && parent->gameObject && !ActiveInHierarchy(parent->gameObject)) return;   // reported at the top
        const int below = CountDescendants(go.transform);
        Add(Severity::Info, "object.inactive", path, "",
            below ? "Inactive: it and its " + std::to_string(below) + " descendant" + (below == 1 ? "" : "s") + " never run or draw"
                  : "Inactive: it never runs or draws");
    }

    void Layer(const GameObject& go, const std::string& path) {
        if (go.layer < 0 || go.layer > 31)
            Add(Severity::Warning, "object.layer_range", path, "", "Layer " + std::to_string(go.layer) + " is outside 0..31");
    }

    template <class T>
    void Singleton(const char* name) {
        const auto all = scene.FindObjectsOfType<T>();
        for (std::size_t i = 1; i < all.size(); ++i)
            Add(Severity::Warning, "component.duplicate_singleton", all[i]->gameObject ? ObjectPath(all[i]->gameObject) : "",
                name, std::string("There is more than one ") + name + " in the scene (" + std::to_string(all.size()) +
                      "); only one is meant to exist");
    }

    void Names() {
        std::map<std::pair<const void*, std::string>, std::vector<const GameObject*>> groups;
        std::vector<std::pair<const void*, std::string>> order;
        for (const auto& up : scene.Objects()) {
            const GameObject& go = *up;
            if (go.IsDestroyed() || go.transient) continue;
            const Transform* parent = go.transform ? go.transform->Parent() : nullptr;
            auto key = std::make_pair((const void*)parent, go.name);
            auto it = groups.find(key);
            if (it == groups.end()) { order.push_back(key); groups[key].push_back(&go); }
            else it->second.push_back(&go);
        }
        for (const auto& key : order) {
            const auto& g = groups[key];
            if (g.size() < 2) continue;
            if (std::find(opt.allowDuplicateNames.begin(), opt.allowDuplicateNames.end(), key.second) != opt.allowDuplicateNames.end())
                continue;
            const Transform* parent = g[0]->transform->Parent();
            const std::string where = parent && parent->gameObject ? "under '" + ObjectPath(parent->gameObject) + "'" : "at the scene root";
            Add(opt.duplicateNameSeverity, "name.duplicate", ObjectPath(g[0]), "",
                std::to_string(g.size()) + " objects are named '" + key.second + "' " + where + "; Find(\"" + key.second +
                "\") only reaches the first");
        }
    }

    void Cameras() {
        const auto cams = scene.FindObjectsOfType<Camera>();
        if (cams.empty()) {
            Add(opt.noCameraSeverity, "scene.no_camera", "", "Camera", "The scene has no Camera, so nothing can be rendered");
            return;
        }
        for (Camera* c : cams) if (CameraUsable(c)) return;
        Add(Severity::Warning, "scene.no_active_camera", "", "Camera",
            "The scene has " + std::to_string(cams.size()) + " camera" + (cams.size() == 1 ? "" : "s") +
            " but none is enabled and active, so nothing can be rendered");
    }

    void Run() {
        for (const auto& up : scene.Objects()) {
            const GameObject& go = *up;
            if (go.IsDestroyed() || go.transient || !go.transform) continue;
            const std::string path = ObjectPath(&go);
            if (opt.checkTransforms) { CheckTransform(go, path); Layer(go, path); }
            if (opt.checkAssets) Assets(go, path);
            if (opt.checkScripts) Scripts(go, path);
            if (opt.checkReferences) References(go, path);
            FoliageRules(go, path);
            if (opt.checkPhysics) { Physics(go, path); Volumes(go, path); }
            if (opt.checkInactive) Inactive(go, path);
        }
        if (opt.checkAssets && !scene.uiFont.empty() && !SceneValidator::AssetExists(scene.uiFont, opt))
            Add(opt.missingAssetSeverity, "asset.missing", "", "Scene", "Missing font '" + scene.uiFont + "' (Scene.uiFont)");
        if (opt.checkCamera) Cameras();
        if (opt.checkNames) {
            Names();
            Singleton<EventSystem>("EventSystem");
            Singleton<DayNightCycle>("DayNightCycle");
            Singleton<CinemachineBrain>("CinemachineBrain");
            Singleton<PauseMenu>("PauseMenu");
            Singleton<SteamManager>("SteamManager");
        }
    }
};

} // namespace

ValidationReport SceneValidator::Validate(const Scene& scene, const ValidatorOptions& options) {
    ValidationReport rep;
    rep.sceneName = scene.Name();
    Checker c{scene, options, rep};
    c.Run();
    return rep;
}

namespace {

ValidationReport ValidateLoaded(const std::string& sceneName, bool loaded, Scene& scene, const std::string& error,
                                const std::vector<std::string>& warnings, const ValidatorOptions& options) {
    if (!loaded) {
        ValidationReport rep;
        rep.sceneName = sceneName;
        rep.issues.push_back({Severity::Error, "scene.load", "", "", "The scene did not load: " + error});
        return rep;
    }
    ValidationReport rep = SceneValidator::Validate(scene, options);
    std::vector<ValidationIssue> loadNotes;
    for (const auto& w : warnings) loadNotes.push_back({Severity::Warning, "scene.load_warning", "", "", w});
    rep.issues.insert(rep.issues.begin(), loadNotes.begin(), loadNotes.end());
    return rep;
}

} // namespace

namespace {

// What the loader repairs silently and a look at the loaded scene cannot see: found in the text, reported first.
void AddLinkIssues(ValidationReport& rep, const std::string& text, const ValidatorOptions& options) {
    if (!options.checkLinks) return;
    std::vector<ValidationIssue> found;
    for (const SceneIssue& i : ValidateSceneLinks(text).issues) {
        if (i.kind == SceneIssue::Kind::DuplicateGuid)
            found.push_back({Severity::Warning, "scene.duplicate_guid", i.object, "", i.message + " (one of them is given a new guid on load)"});
        else if (i.kind == SceneIssue::Kind::BrokenParentLink)
            found.push_back({Severity::Error, "scene.broken_parent", i.object, "", i.message + " (it loads as a root object, so it is not where it was placed)"});
    }
    rep.issues.insert(rep.issues.begin(), found.begin(), found.end());
}

} // namespace

ValidationReport SceneValidator::ValidateText(const std::string& sceneText, const ValidatorOptions& options) {
    Scene scene("scene");
    std::string err;
    std::vector<std::string> warnings;
    const bool ok = SceneSerializer::Deserialize(scene, sceneText, &err, &warnings);
    ValidationReport rep = ValidateLoaded(scene.Name(), ok, scene, err, warnings, options);
    AddLinkIssues(rep, sceneText, options);
    return rep;
}

ValidationReport SceneValidator::ValidateFile(const std::string& path, const ValidatorOptions& options) {
    Scene scene("scene");
    std::string err;
    std::vector<std::string> warnings;
    const bool ok = SceneSerializer::LoadFromFile(scene, path, &err, &warnings);
    ValidatorOptions opt = options;
    const std::size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos) opt.searchDirs.push_back(path.substr(0, slash));
    ValidationReport rep = ValidateLoaded(scene.Name(), ok, scene, err, warnings, opt);
    if (!ok) rep.sceneName = path;
    else if (options.checkLinks) {
        std::ifstream in(path, std::ios::binary);
        std::ostringstream raw; raw << in.rdbuf();
        bool packOk = true;
        const std::string text = DataPack::Unpack(raw.str(), &packOk);   // the same text the load just read
        if (packOk) AddLinkIssues(rep, text, options);
    }
    return rep;
}

} // namespace okay::testing
