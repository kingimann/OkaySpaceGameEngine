#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Components/UIButton.hpp"

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
    m_graph = std::move(graph);
    m_source = text;
    m_error.clear();
    return true;
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
        }
    }
}

void VisualScriptComponent::Start() {
    if (!ScriptsRun(gameObject)) { m_startDeferred = true; return; }
    m_startDeferred = false;
    m_ctx.gameObject = gameObject;
    m_ctx.transform  = transform;
    if (!m_graph) return;
    ScanPolledEvents();
    m_started = true;
    Fire("OnEnable", nullptr);   // like a script: OnEnable, then Start
    if (m_graph) m_graph->RunEntries("OnStart", m_ctx);
}

void VisualScriptComponent::Update(float dt) {
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
}

void VisualScriptComponent::OnDestroy() {
    if (!m_started) return;   // never ran: nothing to tear down
    Fire("OnDestroy", nullptr);
}

void VisualScriptComponent::LateUpdate(float dt) {
    if (!m_graph || m_startDeferred || !ScriptsRun(gameObject) || m_graph->Entries("OnLateUpdate").empty()) return;
    m_ctx.deltaTime = dt;
    m_graph->RunEntries("OnLateUpdate", m_ctx);
}

} // namespace okay
