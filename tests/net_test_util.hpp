#pragma once
// Small readers the network tests share (net_easy, net_session, net_online): a script's or a
// graph's variable as text, and "does any overlay line contain this".
#include <Okay.hpp>
#include <string>
#include <vector>

namespace nettest {

inline std::string ScriptVar(okay::GameObject* go, const char* name) {
    auto* sc = go ? go->GetComponent<okay::ScriptComponent>() : nullptr;
    return sc && sc->VM() ? sc->VM()->GetGlobal(name).AsString() : std::string();
}
inline std::string GraphVar(okay::GameObject* go, const char* name) {
    auto* v = go ? go->GetComponent<okay::VisualScriptComponent>() : nullptr;
    return v ? v->GetVariable(name).AsString() : std::string();
}
inline bool HasLine(const std::vector<std::string>& lines, const std::string& part) {
    for (const auto& l : lines) if (l.find(part) != std::string::npos) return true;
    return false;
}

} // namespace nettest
