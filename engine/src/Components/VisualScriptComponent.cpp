#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Debug/ConsoleModel.hpp"
#include "okay/Testing/ObjectPath.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Input/GameInput.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/Gestures.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Components/Damage.hpp"
#include "okay/VisualScript/NodesLoops.hpp"
#include "okay/VisualScript/VsVariables.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace okay {

namespace {
// On Damaged: one listener for the whole engine (Damage.hpp keeps a plain list, so it is found again by
// its type). It hands each hit to the Visual Scripts on the object that was hit; they run the event on
// their next frame. Nothing is kept that points at an object: the source travels as an instance id.
struct VsDamagedRelay {
    void operator()(const DamageEvent& e) const {
        if (!e.target || e.target->IsDestroyed()) return;
        for (auto* vc : e.target->GetComponents<VisualScriptComponent>()) {
            VisualScriptComponent::PendingHit h;
            h.amount = e.dealt;
            h.type = DamageTypeName(e.type);
            h.source = (e.source && !e.source->IsDestroyed()) ? e.source->InstanceId() : 0;
            vc->QueueDamaged(h);
        }
    }
};
void EnsureDamagedRelay() {
    for (const auto& f : Listeners().damaged)
        if (f.target<VsDamagedRelay>()) return;
    AddDamagedListener(VsDamagedRelay{});
}
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
    graph->debugKey = m_path;
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

bool VisualScriptComponent::LoadFromFile(const std::string& path, std::string* error) {
    std::ifstream f(path);
    if (!f) {
        if (error) *error = "cannot open " + path;
        m_error = "cannot open " + path;
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    if (path != m_path) m_scenePath.clear();   // another file: the scene's text no longer names it
    m_path = path;
    return LoadFromText(ss.str(), error);
}

bool VisualScriptComponent::Reload(std::string* error) {
    if (m_path.empty()) { if (error) *error = "no file"; return false; }
    return LoadFromFile(m_path, error);
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
    m_buttonEntries.clear();
    m_watches.clear();
    if (!m_graph) return;
    if (!m_graph->Entries("OnDamaged").empty()) EnsureDamagedRelay();
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
        else if (n.rfind("OnButtonClicked:", 0) == 0)
            m_buttonEntries.push_back({n, n.substr(16)});
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
    ConsoleSourceScope scope(GraphLogSource(gameObject, Path()));
    ApplyDeclDefaults();   // declared variables' starting values, unless something already set them
    Fire("OnEnable", nullptr);   // like a script: OnEnable, then Start
    if (m_graph) m_graph->RunEntries("OnStart", m_ctx);
    CheckWatches();
}

void VisualScriptComponent::Update(float dt) {
    ApplyPendingGraph();
    if (!m_graph || !ScriptsRun(gameObject)) return;
    if (m_startDeferred) Start();
    ConsoleSourceScope scope(GraphLogSource(gameObject, Path()));
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
    // On Button Clicked: a UI button anywhere in the scene, by its object's name (spaces typed as '_').
    if (!m_buttonEntries.empty() && gameObject && gameObject->scene()) {
        for (const ButtonEntry& be : m_buttonEntries) {
            GameObject* clicked = nullptr;
            for (const auto& up : gameObject->scene()->Objects()) {
                if (!up || !up->active) continue;
                std::string nm = up->name;
                for (char& c : nm) if (c == ' ' || c == '\t') c = '_';
                if (nm != be.name) continue;
                if (auto* b = up->GetComponent<UIButton>(); b && b->WasClicked()) { clicked = up.get(); break; }
            }
            if (clicked) Fire(be.entry.c_str(), clicked);
            if (!m_graph) return;
        }
    }
    // On Key Pressed / Released.
    for (const KeyEntry& k : m_keyEntries)
        if (k.up ? Input::GetKeyUp(k.key) : Input::GetKeyDownGame(k.key)) Fire(k.entry.c_str(), nullptr);
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
    if (name == "interact" || name == "use") return InteractPressed();
    if (name == "jump") return Input::GetKeyDownGame(' ');
    if (name == "fire" || name == "attack") return Input::GetMouseButtonDown(0);
    if (name == "crouch") return Input::GetKeyDownGame('c');
    return Input::GetKeyDownGame(Input::KeyFromName(name));
}
bool ActionUp(const std::string& name) {
    if (name == "interact" || name == "use") return InteractReleased();
    if (name == "jump") return Input::GetKeyUp(' ');
    if (name == "fire" || name == "attack") return Input::GetMouseButtonUp(0);
    if (name == "crouch") return Input::GetKeyUp('c');
    return Input::GetKeyUp(Input::KeyFromName(name));
}
bool ActionHeld(const std::string& name) {
    if (name == "interact" || name == "use") return InteractHeld();
    if (name == "jump") return Input::GetKeyGame(' ');
    if (name == "fire" || name == "attack") return Input::GetMouseButton(0);
    if (name == "crouch") return Input::GetKeyGame('c');
    return Input::GetKeyGame(Input::KeyFromName(name));
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
    // On Damaged: the hits queued since the last frame, in order. Amount is the health really lost,
    // Type the damage kind's name, Source who dealt it (nothing for a trap or hunger).
    if (!m_pendingHits.empty()) {
        std::vector<PendingHit> hits;
        hits.swap(m_pendingHits);
        if (!m_graph->Entries("OnDamaged").empty())
            for (const PendingHit& h : hits) {
                m_ctx.variables["damage_amount"] = vs::VsValue(h.amount);
                m_ctx.variables["damage_type"] = vs::VsValue(h.type);
                m_ctx.variables["damage_source"] = h.source ? vs::VsValue::Object(h.source) : vs::VsValue();
                m_ctx.variables["event_value"] = vs::VsValue(h.amount);
                Fire("OnDamaged", nullptr);
                if (!m_graph) return;
            }
    } else if (!m_graph->Entries("OnDamaged").empty()) {
        EnsureDamagedRelay();   // listeners cleared since the graph loaded (a test, a scene reset): listen again
    }
    if (!m_graph->Entries("OnInteract").empty() && InteractPressed() && transform && gameObject->scene()) {
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
