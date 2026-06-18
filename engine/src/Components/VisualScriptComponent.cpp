#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Core/Log.hpp"

#include <fstream>
#include <sstream>

namespace okay {

bool VisualScriptComponent::LoadFromText(const std::string& text, std::string* error) {
    std::string err;
    auto graph = vs::NodeGraph::Parse(text, &err);
    if (!graph) {
        if (error) *error = err;
        OKAY_ERROR("VisualScript parse error: ", err);
        return false;
    }
    m_graph = std::move(graph);
    return true;
}

bool VisualScriptComponent::LoadFromFile(const std::string& path, std::string* error) {
    std::ifstream f(path);
    if (!f) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return LoadFromText(ss.str(), error);
}

vs::VsValue VisualScriptComponent::GetVariable(const std::string& name) const {
    auto it = m_ctx.variables.find(name);
    return it != m_ctx.variables.end() ? it->second : vs::VsValue{};
}

void VisualScriptComponent::Start() {
    m_ctx.gameObject = gameObject;
    m_ctx.transform  = transform;
    if (!m_graph) return;
    int entry = m_graph->Entry("OnStart");
    if (entry >= 0) m_graph->Run(entry, m_ctx);
}

void VisualScriptComponent::Update(float dt) {
    if (!m_graph) return;
    m_ctx.deltaTime = dt;
    m_ctx.transform = transform;
    int entry = m_graph->Entry("OnUpdate");
    if (entry >= 0) m_graph->Run(entry, m_ctx);
}

} // namespace okay
