#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/VisualScript/NodeGraph.hpp"
#include <memory>
#include <string>

namespace okay {

/// Attaches a visual-script graph to a GameObject. The graph's "OnStart" entry
/// runs once and its "OnUpdate" entry runs every frame, with the component's
/// Transform and frame delta available to the nodes.
class VisualScriptComponent : public Behaviour {
public:
    /// Use an already-built graph.
    void SetGraph(std::unique_ptr<vs::NodeGraph> graph) { m_graph = std::move(graph); }
    /// Build the graph from the OkayVS text format. Returns false on parse error.
    bool LoadFromText(const std::string& text, std::string* error = nullptr);
    /// Load a graph from a .okayvs file.
    bool LoadFromFile(const std::string& path, std::string* error = nullptr);

    vs::NodeGraph* Graph() const { return m_graph.get(); }
    /// Read back a blackboard variable (handy for tests/gameplay glue).
    vs::VsValue GetVariable(const std::string& name) const;

    void Start() override;
    void Update(float dt) override;

private:
    std::unique_ptr<vs::NodeGraph> m_graph;
    vs::VsContext m_ctx;
};

} // namespace okay
