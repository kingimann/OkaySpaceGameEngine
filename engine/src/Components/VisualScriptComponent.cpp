#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Debug/ConsoleModel.hpp"
#include "okay/Testing/ObjectPath.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/Gestures.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/VisualScript/NodesLoops.hpp"
#include "okay/VisualScript/VsVariables.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace okay {

namespace {
ConsoleSource GraphLogSource(GameObject* go, const std::string& path) {
    ConsoleSource s;
    if (!go) return s;
    s.kind = ConsoleSource::Kind::Graph;
    s.guid = go->Guid();
    s.component = "VisualScriptComponent";
    s.path = path.empty() ? testing::ObjectPath(go) : path;
    return s;
}
}

bool VisualScriptComponent::LoadFromText(const std::string& text, std::string* error) {
    std::string err;
    auto graph = vs::NodeGraph::Parse(text, &err);
    if (!graph) {
        if (error) *error = err;
        m_error = err;
        OKAY_ERROR("VisualScript parse error: ", err);
        return false;
    }
    graph->debugKey = m_resolved;   // the file itself: breakpoints, Run Graph and Spawn look beside it
    m_source = text;
    m_error.clear();
    // Replacing a graph while one of its chains is running (paused at a breakpoint,
    // or a node that reloads its own graph) would free it under its feet: swap it
    // in once it has finished.
    if (m_graph && m_graph->Running()) { m_pendingGraph = std::move(graph); return true; }
    AdoptGraph(std::move(graph));
    return true;
}

void VisualScriptComponent::AdoptGraph(std::unique_ptr<vs::NodeGraph> graph) {
    // Keep Once / timers / a loop paused on a Delay, matched by the node's file id.
    std::vector<vs::VsFlowRecord> flows;
    std::vector<int> parked;
    if (m_graph) m_graph->CaptureFlow(flows, parked);
    m_graph = std::move(graph);
    if (m_graph) m_graph->RestoreFlow(flows, parked);
    if (m_graph && m_started) ApplyDeclDefaults();   // a variable declared while playing starts at its default
    m_pendingGraph.reset();
}

void VisualScriptComponent::ApplyDeclDefaults() {
    if (!m_graph) return;
    for (const auto& d : m_graph->Decls()) {
        vs::VarMap* store = &m_ctx.variables;
        if (d.scope == vs::VarScope::Scene) store = &vs::SceneVariables(gameObject ? gameObject->scene() : nullptr);
        else if (d.scope == vs::VarScope::Global) store = &vs::GlobalVariables();
        if (!store->count(d.name)) (*store)[d.name] = vs::VsDeepCopy(d.value);   // each owner gets its own list / map
    }
}

void VisualScriptComponent::ApplyPendingGraph() {
    if (m_pendingGraph && !(m_graph && m_graph->Running())) AdoptGraph(std::move(m_pendingGraph));
}

bool VisualScriptComponent::ReadGraphFile(const std::string& file, std::string* error) {
    std::ifstream f(file);
    if (!f) {
        if (error) *error = "cannot open " + file;
        m_error = "cannot open " + file;
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return LoadFromText(ss.str(), error);
}

bool VisualScriptComponent::LoadFromFile(const std::string& path, std::string* error) {
    if (!std::ifstream(path)) {
        if (error) *error = "cannot open " + path;
        m_error = "cannot open " + path;
        return false;
    }
    m_path = path;
    m_resolved = ResolvedPath();
    return ReadGraphFile(path, error);
}

bool VisualScriptComponent::Reload(std::string* error) {
    if (m_path.empty()) { if (error) *error = "no file"; return false; }
    m_resolved = ResolvedPath();   // the path stays as written; only where it is found is looked up again
    return ReadGraphFile(m_resolved, error);
}

// ---- Where the graph file is (#888 finding 42) ------------------------------
// Path() is kept exactly as the scene file wrote it. These find the file it names
// for reading (ResolvedPath) and decide what a save writes (PathToSave), so a
// project that moves keeps finding its graph files and no machine path is saved.
// Written from the finding's description and std::filesystem's documented behaviour.

namespace {
namespace fs = std::filesystem;

// "/home/..", "\\server\..", "C:\..", "C:/..": a full path on any OS (a scene saved on
// Windows and opened on Linux still counts its full paths as full).
bool IsFullPath(const std::string& p) {
    if (p.empty()) return false;
    if (p[0] == '/' || p[0] == '\\') return true;
    return p.size() >= 2 && p[1] == ':' && std::isalpha(static_cast<unsigned char>(p[0])) != 0;
}

bool IsFile(const std::string& p) {
    std::error_code ec;
    return !p.empty() && fs::is_regular_file(fs::path(p), ec);
}

// `dir` + `rel`, the way the scene loader always joined them (so the editor sees the same strings).
std::string Join(const std::string& dir, const std::string& rel) { return (fs::path(dir) / fs::path(rel)).string(); }

// The parts of a path, split at both '/' and '\', empty parts dropped.
std::vector<std::string> PartsOf(const std::string& p) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : p) {
        if (c == '/' || c == '\\') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// A folder without its trailing separators ("/a/b/" -> "/a/b"; "/" stays "/").
fs::path Folder(const std::string& dir) {
    std::string d = dir;
    while (d.size() > 1 && (d.back() == '/' || d.back() == '\\')) d.pop_back();
    if (d.size() == 2 && d[1] == ':') d += '/';   // "C:" alone is not the drive's root
    return fs::path(d);
}

// `dir` and then each folder above it, nearest first (at most 64, so a strange path cannot loop).
std::vector<fs::path> FolderAndAbove(const std::string& dir) {
    std::vector<fs::path> out;
    fs::path cur = Folder(dir);
    for (int i = 0; i < 64 && !cur.empty(); ++i) {
        out.push_back(cur);
        const fs::path up = cur.parent_path();
        if (up.empty() || up == cur) break;
        cur = up;
    }
    return out;
}

// Is `file` inside `dir`? Then `rel` is its path from `dir` with '/' separators.
bool InsideFolder(const std::string& file, const std::string& dir, std::string& rel) {
    auto inside = [&rel](const fs::path& f, const fs::path& d) {
        if (f.empty() || d.empty()) return false;
        const fs::path r = f.lexically_relative(d);
        if (r.empty()) return false;
        const std::string s = r.generic_string();
        if (s == "." || s == ".." || s.rfind("../", 0) == 0) return false;
        rel = s;
        return true;
    };
    if (inside(fs::path(file).lexically_normal(), Folder(dir).lexically_normal())) return true;
    // Spelled differently (a link, a "..", another letter case on Windows): compare the real folders.
    std::error_code e1, e2;
    const fs::path cf = fs::weakly_canonical(fs::path(file), e1);
    const fs::path cd = fs::weakly_canonical(Folder(dir), e2);
    return !e1 && !e2 && inside(cf, cd);
}

// The folders named "Assets" that hold `dir` (or are `dir`), deepest first: each one's project
// is the folder above it.
std::vector<fs::path> AssetsFoldersHolding(const std::string& dir) {
    std::vector<fs::path> out;
    for (const fs::path& f : FolderAndAbove(dir))
        if (f.filename() == "Assets") out.push_back(f);
    return out;
}

// The scene folder last seen per object guid (SceneDirForLoad).
struct SceneDirMemory {
    std::mutex mu;
    std::unordered_map<std::string, std::string> byGuid;
};
SceneDirMemory& DirMemory() {
    static SceneDirMemory m;
    return m;
}
} // namespace

void VisualScriptComponent::SetPath(const std::string& p) {
    m_path = p;
    m_resolved = ResolvedPath();
    if (m_graph) m_graph->debugKey = m_resolved;
}

std::string VisualScriptComponent::ResolvedPath(bool* found) const {
    if (found) *found = false;
    if (m_path.empty()) return m_path;
    auto hit = [found](const std::string& file) {
        if (found) *found = true;
        return file;
    };
    if (!IsFullPath(m_path)) {
        if (!sceneDir.empty()) {
            const std::string beside = Join(sceneDir, m_path);
            if (IsFile(beside)) return hit(beside);
        }
        if (IsFile(m_path)) return hit(m_path);   // as written: from the working folder
        const std::vector<std::string> parts = PartsOf(m_path);
        if (!sceneDir.empty() && !parts.empty() && parts[0] == "Assets") {
            // "Assets/..." is written from the project folder: one of the folders above the scene.
            const std::vector<fs::path> folders = FolderAndAbove(sceneDir);
            for (std::size_t i = 1; i < folders.size(); ++i) {
                const std::string c = Join(folders[i].string(), m_path);
                if (IsFile(c)) return hit(c);
            }
        }
        // Not there: where it belongs, next to the scene (what the editor opens and writes).
        return sceneDir.empty() ? m_path : Join(sceneDir, m_path);
    }
    if (IsFile(m_path)) return hit(m_path);
    if (!sceneDir.empty()) {
        // An old scene saved with a full path the project has since left: the file name next
        // to the scene, then the path from its last "Assets" folder (then the one before ...)
        // under the scene's folder and each folder above it.
        const std::vector<std::string> parts = PartsOf(m_path);
        if (!parts.empty()) {
            const std::string beside = Join(sceneDir, parts.back());
            if (IsFile(beside)) return hit(beside);
        }
        const std::vector<fs::path> folders = FolderAndAbove(sceneDir);
        for (std::size_t a = parts.size(); a-- > 0;) {
            if (parts[a] != "Assets" || a + 1 >= parts.size()) continue;
            std::string tail;
            for (std::size_t i = a; i < parts.size(); ++i) tail += (i > a ? "/" : "") + parts[i];
            for (const fs::path& f : folders) {
                const std::string c = Join(f.string(), tail);
                if (IsFile(c)) return hit(c);
            }
        }
    }
    return m_path;
}

std::string VisualScriptComponent::PathToSave() const {
    if (m_path.empty() || sceneDir.empty() || !IsFullPath(m_path)) return m_path;
    bool found = false;
    const std::string file = ResolvedPath(&found);
    if (!found) return m_path;   // nothing better is known: keep it
    std::string rel;
    if (InsideFolder(file, sceneDir, rel)) return rel;
    for (const fs::path& assets : AssetsFoldersHolding(sceneDir))
        if (InsideFolder(file, assets.string(), rel)) return "Assets/" + rel;
    return m_path;   // outside the scene's project: only the full path names it
}

std::string VisualScriptComponent::SceneDirForLoad(const GameObject& go, const std::string& path, const std::string& loadingDir) {
    SceneDirMemory& m = DirMemory();
    if (!loadingDir.empty()) {
        // From a file: remember the folder for this object, so the same scene rebuilt from
        // text later (the editor's Undo and Stop) still knows where it came from.
        if (!go.Guid().empty()) {
            std::lock_guard<std::mutex> lock(m.mu);
            m.byGuid[go.Guid()] = loadingDir;
        }
        return loadingDir;
    }
    if (!go.Guid().empty()) {
        std::lock_guard<std::mutex> lock(m.mu);
        const auto it = m.byGuid.find(go.Guid());
        if (it != m.byGuid.end()) return it->second;
    }
    // A copy made in memory gets a new guid; its original is in the same scene.
    if (!path.empty())
        if (const Scene* scene = go.scene())
            for (const auto& up : scene->Objects()) {
                if (!up || up.get() == &go) continue;
                for (const VisualScriptComponent* other : up->GetComponents<VisualScriptComponent>())
                    if (other && !other->sceneDir.empty() && other->Path() == path) return other->sceneDir;
            }
    return std::string();
}

vs::VsValue VisualScriptComponent::GetVariable(const std::string& name) const {
    auto it = m_ctx.variables.find(name);
    return it != m_ctx.variables.end() ? it->second : vs::VsValue{};
}

void VisualScriptComponent::ScanPolledEvents() {
    m_scanned = m_graph.get();
    m_keyEntries.clear();
    m_timerEntries.clear();
    m_actionEntries.clear();
    m_gestureEntries.clear();
    m_watches.clear();
    if (!m_graph) return;
    for (const auto& kv : m_graph->AllEntries()) {
        const std::string& n = kv.first;
        if (n.rfind("OnKey:", 0) == 0)
            m_keyEntries.push_back({n, Input::KeyFromName(n.substr(6)), false});
        else if (n.rfind("OnKeyUp:", 0) == 0)
            m_keyEntries.push_back({n, Input::KeyFromName(n.substr(8)), true});
        else if (n.rfind("OnTimer:", 0) == 0) {
            float p = 1.0f;
            try { p = std::stof(n.substr(8)); } catch (...) {}
            m_timerEntries.push_back({n, p > 0.001f ? p : 0.001f, 0.0f});
        } else if (n.rfind("OnAction:", 0) == 0)
            m_actionEntries.push_back({n, n.substr(9), 0});
        else if (n.rfind("OnActionUp:", 0) == 0)
            m_actionEntries.push_back({n, n.substr(11), 1});
        else if (n.rfind("OnActionHeld:", 0) == 0)
            m_actionEntries.push_back({n, n.substr(13), 2});
        else if (n == "OnTap" || n == "OnDoubleTap" || n == "OnLongPress" || n == "OnPinch" || n == "OnRotate" ||
                 n == "OnSwipe" || n.rfind("OnSwipe:", 0) == 0)
            m_gestureEntries.push_back(n);
        else if (n.rfind("OnVarChanged:", 0) == 0 || n.rfind("OnSceneVarChanged:", 0) == 0 ||
                   n.rfind("OnGlobalVarChanged:", 0) == 0) {
            for (int id : kv.second)
                if (auto* ev = dynamic_cast<vs::VarChangedEventNode*>(m_graph->Node(id))) {
                    // Start from the current value: only later changes fire the event.
                    WatchEntry w{n, (int)ev->scope, ev->name, {}, false};
                    if (const vs::VsValue* v = WatchedValue(w)) { w.last = *v; w.had = true; }
                    m_watches.push_back(std::move(w));
                    break;
                }
        }
    }
}

const vs::VsValue* VisualScriptComponent::WatchedValue(const WatchEntry& w) {
    using S = vs::VarChangedEventNode::Scope;
    vs::VarMap* m = &m_ctx.variables;
    if (w.scope == (int)S::Scene) m = &vs::SceneVariables(gameObject ? gameObject->scene() : nullptr);
    else if (w.scope == (int)S::Global) m = &vs::GlobalVariables();
    auto it = m->find(w.name);
    return it != m->end() ? &it->second : nullptr;
}

void VisualScriptComponent::CheckWatches() {
    if (m_watches.empty() || m_checkingWatches || !m_graph) return;
    m_checkingWatches = true;
    // A handler may change watched variables again: settle within a few rounds,
    // and say so if a variable keeps changing (a handler changing its own variable).
    constexpr int kRounds = 16;
    for (int round = 0; round < kRounds && m_graph; ++round) {
        bool any = false;
        for (std::size_t i = 0; i < m_watches.size() && m_graph; ++i) {
            const vs::VsValue* cur = WatchedValue(m_watches[i]);
            WatchEntry& w = m_watches[i];
            const bool has = cur != nullptr;
            if (has == w.had && (!has || vs::VsSameValue(*cur, w.last))) continue;
            if (round == kRounds - 1) {
                Log::Error("Visual Script on '", gameObject ? gameObject->name : std::string("?"), "': variable '", w.name,
                           "' kept changing every time its On Variable Changed ran (does the event change it again?); "
                           "stopped after ", kRounds, " rounds");
                w.had = has; if (has) w.last = *cur;
                continue;
            }
            const vs::VsValue now = has ? *cur : vs::VsValue{};
            const vs::VsValue before = w.had ? w.last : vs::VsValue{};
            w.had = has; w.last = now;
            for (int id : m_graph->Entries(w.entry))
                if (auto* ev = dynamic_cast<vs::VarChangedEventNode*>(m_graph->Node(id))) { ev->value = now; ev->old = before; }
            const std::string entry = w.entry;
            Fire(entry.c_str(), nullptr);   // its own CheckWatches() returns at once (m_checkingWatches)
            any = true;
        }
        if (!any) break;
    }
    m_checkingWatches = false;
}

void VisualScriptComponent::Start() {
    ApplyPendingGraph();
    if (!ScriptsRun(gameObject)) { m_startDeferred = true; return; }
    m_startDeferred = false;
    m_ctx.gameObject = gameObject;
    m_ctx.transform  = transform;
    if (!m_graph) return;
    ScanPolledEvents();
    m_started = true;
    ConsoleSourceScope scope(GraphLogSource(gameObject, m_resolved));
    ApplyDeclDefaults();   // declared variables' starting values, unless something already set them
    Fire("OnEnable", nullptr);   // like a script: OnEnable, then Start
    if (m_graph) m_graph->RunEntries("OnStart", m_ctx);
    CheckWatches();
}

void VisualScriptComponent::Update(float dt) {
    ApplyPendingGraph();
    if (!m_graph || !ScriptsRun(gameObject)) return;
    if (m_startDeferred) Start();
    ConsoleSourceScope scope(GraphLogSource(gameObject, m_resolved));
    if (m_scanned != m_graph.get()) ScanPolledEvents();   // reloaded since the last frame
    m_ctx.deltaTime = dt;
    m_ctx.gameObject = gameObject;
    m_ctx.transform = transform;
    // Remember health before this frame's graph, so a hit in On Update is a change.
    if (!m_sawHealth) {
        if (auto* hp = gameObject->GetComponent<HealthStat>()) {
            m_lastHealth = hp->health;
            m_sawHealth = true;
            m_deathFired = hp->IsDead();
        }
    }
    m_graph->Resume(m_ctx);   // chains parked by Delay / Wait / Actions steps carry on
    if (!m_graph) return;
    // On Fixed Update: whole fixed steps for the time that passed (at most 5 per
    // frame so a hitch can't snowball), before this frame's On Update - the same
    // rule as a text script's FixedUpdate(). Delta Time reads the fixed step.
    if (!m_graph->Entries("OnFixedUpdate").empty()) {
        m_fixedAcc = std::min(m_fixedAcc + dt, kFixedStep * 5.0f);
        while (m_fixedAcc >= kFixedStep && m_graph) {
            m_fixedAcc -= kFixedStep;
            m_ctx.deltaTime = kFixedStep;
            m_graph->RunEntries("OnFixedUpdate", m_ctx);
        }
        m_ctx.deltaTime = dt;
        if (!m_graph) return;
    }
    m_graph->RunEntries("OnUpdate", m_ctx);
    // On Click also covers UI buttons (they don't get scene mouse picks).
    if (!m_graph->Entries("OnClick").empty())
        if (auto* b = gameObject ? gameObject->GetComponent<UIButton>() : nullptr)
            if (b->WasClicked()) Fire("OnClick", gameObject);
    // On Key Pressed / Released.
    for (const KeyEntry& k : m_keyEntries)
        if (k.up ? Input::GetKeyUp(k.key) : Input::GetKeyDown(k.key)) Fire(k.entry.c_str(), nullptr);
    for (const std::string& e : m_gestureEntries) {
        bool go = false;
        if (e == "OnTap") go = Gestures::Happened("tap");
        else if (e == "OnDoubleTap") go = Gestures::Happened("double tap");
        else if (e == "OnLongPress") go = Gestures::Happened("long press");
        else if (e == "OnPinch") go = Gestures::Happened("pinch");
        else if (e == "OnRotate") go = Gestures::Happened("rotate");
        else if (e == "OnSwipe") go = Gestures::Happened("swipe");
        else if (e.rfind("OnSwipe:", 0) == 0) go = Gestures::Happened("swipe " + e.substr(8));
        if (!go) continue;
        m_ctx.variables["gesture_direction"] = vs::VsValue(Gestures::SwipeDirection());
        m_ctx.variables["gesture_scale"] = vs::VsValue(Gestures::PinchScale());
        m_ctx.variables["gesture_degrees"] = vs::VsValue(Gestures::TwistDegrees());
        Fire(e.c_str(), nullptr);
    }
    // On Timer: fire every `period` seconds (catching up at most a few per frame).
    for (TimerEntry& t : m_timerEntries) {
        t.acc += dt;
        int guard = 0;
        while (t.acc >= t.period && guard++ < 4) { t.acc -= t.period; Fire(t.entry.c_str(), nullptr); }
        if (t.acc > t.period) t.acc = 0.0f;
    }
    PollGameplay(dt);
    // On Variable Changed: also catches changes made by other graphs and scripts.
    CheckWatches();
}

namespace {
bool ActionDown(const std::string& name) {
    if (name == "interact" || name == "use") return Input::GetKeyDown('e');
    if (name == "jump") return Input::GetKeyDown(' ');
    if (name == "fire" || name == "attack") return Input::GetMouseButtonDown(0);
    if (name == "crouch") return Input::GetKeyDown('c');
    return Input::GetKeyDown(Input::KeyFromName(name));
}
bool ActionUp(const std::string& name) {
    if (name == "interact" || name == "use") return Input::GetKeyUp('e');
    if (name == "jump") return Input::GetKeyUp(' ');
    if (name == "fire" || name == "attack") return Input::GetMouseButtonUp(0);
    if (name == "crouch") return Input::GetKeyUp('c');
    return Input::GetKeyUp(Input::KeyFromName(name));
}
bool ActionHeld(const std::string& name) {
    if (name == "interact" || name == "use") return Input::GetKey('e');
    if (name == "jump") return Input::GetKey(' ');
    if (name == "fire" || name == "attack") return Input::GetMouseButton(0);
    if (name == "crouch") return Input::GetKey('c');
    return Input::GetKey(Input::KeyFromName(name));
}
}

void VisualScriptComponent::FireTagged(const char* prefix, GameObject* other) {
    if (!other || other->tag.empty()) return;
    std::string tag = other->tag;
    for (char& c : tag) if (c == ' ' || c == '\t') c = '_';
    Fire((std::string(prefix) + tag).c_str(), other);
}

void VisualScriptComponent::PollGameplay(float) {
    if (!m_graph || !gameObject) return;
    if (!m_graph->Entries("OnInteract").empty() && Input::GetKeyDown('e') && transform && gameObject->scene()) {
        GameObject* best = nullptr;
        float bestD = 2.0f;
        for (const auto& up : gameObject->scene()->Objects()) {
            if (!up || up.get() == gameObject || !up->transform || !up->active) continue;
            const float d = (up->transform->Position() - transform->Position()).Magnitude();
            if (d <= bestD) { bestD = d; best = up.get(); }
        }
        if (best) Fire("OnInteract", best);
    }
    for (const ActionEntry& a : m_actionEntries) {
        const bool go = a.edge == 0 ? ActionDown(a.action) : a.edge == 1 ? ActionUp(a.action) : ActionHeld(a.action);
        if (go) Fire(a.entry.c_str(), nullptr);
    }
    if (auto* hp = gameObject->GetComponent<HealthStat>()) {
        if (m_sawHealth && hp->health != m_lastHealth && !m_graph->Entries("OnHealthChanged").empty()) {
            m_ctx.variables["event_value"] = vs::VsValue(hp->health);
            Fire("OnHealthChanged", nullptr);
        }
        if (hp->IsDead()) {
            if (!m_deathFired && !m_graph->Entries("OnDeath").empty()) {
                m_deathFired = true;
                m_ctx.variables["event_value"] = vs::VsValue(0.0f);
                Fire("OnDeath", nullptr);
            }
        } else m_deathFired = false;
        m_lastHealth = hp->health;
        m_sawHealth = true;
    }
}

void VisualScriptComponent::OnDestroy() {
    if (!m_started) return;   // never ran: nothing to tear down
    Fire("OnDestroy", nullptr);
}

void VisualScriptComponent::LateUpdate(float dt) {
    if (!m_graph || m_startDeferred || !ScriptsRun(gameObject) || m_graph->Entries("OnLateUpdate").empty()) return;
    m_ctx.deltaTime = dt;
    m_graph->RunEntries("OnLateUpdate", m_ctx);
    CheckWatches();
}

} // namespace okay
