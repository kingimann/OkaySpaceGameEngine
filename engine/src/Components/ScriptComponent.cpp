#include "okay/Components/ScriptComponent.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Core/Log.hpp"

#include <fstream>
#include <sstream>

namespace okay {

bool ScriptComponent::LoadSource(const std::string& source, std::string* error) {
    m_vm = CreateScriptVM(m_language);
    if (!m_vm) {
        std::string e = "scripting backend '" + m_language + "' is not available in this build";
        if (error) *error = e;
        OKAY_ERROR(e);
        return false;
    }
    return m_vm->Load(source, error);
}

bool ScriptComponent::LoadFile(const std::string& path, std::string* error) {
    std::ifstream f(path);
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
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
