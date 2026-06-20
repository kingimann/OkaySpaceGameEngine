#include "okay/Components/ScriptComponent.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Core/Log.hpp"

#include <fstream>
#include <sstream>
#include <cctype>
#include <cstdlib>

namespace okay {

// Parse an inspector value string into a VsValue: true/false -> bool, a number
// -> float, otherwise a string (surrounding quotes are stripped).
static vs::VsValue ParseFieldValue(const std::string& raw) {
    std::string s = raw;
    auto a = s.find_first_not_of(" \t");
    auto b = s.find_last_not_of(" \t");
    if (a == std::string::npos) return vs::VsValue{std::string{}};
    s = s.substr(a, b - a + 1);
    if (s == "true")  return vs::VsValue{true};
    if (s == "false") return vs::VsValue{false};
    if (s.size() >= 2 && (s.front() == '"' || s.front() == '\'') && s.back() == s.front())
        return vs::VsValue{s.substr(1, s.size() - 2)};
    // Numeric? (allow a leading sign, digits, one dot, and an f/d suffix)
    char* end = nullptr;
    double d = std::strtod(s.c_str(), &end);
    if (end && end != s.c_str()) {
        while (*end == 'f' || *end == 'F' || *end == 'd' || *end == 'D') ++end;
        if (*end == '\0') return vs::VsValue{(float)d};
    }
    return vs::VsValue{s};
}

void ScriptComponent::ApplyFieldOverrides() {
    if (!m_vm) return;
    for (const auto& kv : fields)
        m_vm->SetGlobal(kv.first, ParseFieldValue(kv.second));
}

bool ScriptComponent::LoadSource(const std::string& source, std::string* error) {
    m_source = source;
    m_vm = CreateScriptVM(m_language);
    if (!m_vm) {
        std::string e = "scripting backend '" + m_language + "' is not available in this build";
        if (error) *error = e;
        OKAY_ERROR(e);
        return false;
    }
    bool ok = m_vm->Load(source, error);
    if (ok) ApplyFieldOverrides();   // override the script's defaults with inspector values
    return ok;
}

bool ScriptComponent::LoadFile(const std::string& path, std::string* error) {
    std::ifstream f(path);
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    m_path = path;
    return LoadSource(ss.str(), error);
}

void ScriptComponent::Start() {
    if (!m_vm) return;
    m_host.gameObject = gameObject;
    m_host.transform  = transform;
    m_vm->Bind(&m_host);
    m_vm->CallStart();
}

void ScriptComponent::Update(float dt) {
    if (!m_vm) return;
    m_host.transform = transform;
    m_host.deltaTime = dt;
    m_vm->CallUpdate(dt);
}

} // namespace okay
