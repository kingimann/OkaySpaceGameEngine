#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/VisualScript/NodesLoops.hpp"
#include "okay/VisualScript/VsVariables.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace okay {

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
    m_graph = std::move(graph);
    m_pendingGraph.reset();
    return true;
}

void VisualScriptComponent::ApplyPendingGraph() {
    if (m_pendingGraph && !(m_graph && m_graph->Running())) m_graph = std::move(m_pendingGraph);
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
        } else if (n.rfind("OnVarChanged:", 0) == 0 || n.rfind("OnSceneVarChanged:", 0) == 0 ||
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
    Fire("OnEnable", nullptr);   // like a script: OnEnable, then Start
    if (m_graph) m_graph->RunEntries("OnStart", m_ctx);
    CheckWatches();
}

void VisualScriptComponent::Update(float dt) {
    ApplyPendingGraph();
    if (!m_graph || !ScriptsRun(gameObject)) return;
    if (m_startDeferred) Start();
    if (m_scanned != m_graph.get()) ScanPolledEvents();   // reloaded since the last frame
    m_ctx.deltaTime = dt;
    m_ctx.gameObject = gameObject;
    m_ctx.transform = transform;
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
    // On Timer: fire every `period` seconds (catching up at most a few per frame).
    for (TimerEntry& t : m_timerEntries) {
        t.acc += dt;
        int guard = 0;
        while (t.acc >= t.period && guard++ < 4) { t.acc -= t.period; Fire(t.entry.c_str(), nullptr); }
        if (t.acc > t.period) t.acc = 0.0f;
    }
    // On Variable Changed: also catches changes made by other graphs and scripts.
    CheckWatches();
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
