#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/VisualScript/NodeGraph.hpp"
#include "okay/Physics/Physics2D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Collider2D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include <memory>
#include <string>
#include <unordered_map>

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
    /// The last OkayVS text loaded (retained for editing and serialization).
    const std::string& Source() const { return m_source; }
    /// The .okayvs file this graph came from ("" = embedded text only). Saving
    /// the graph in the editor reloads every object that uses the file.
    const std::string& Path() const { return m_path; }
    void SetPath(const std::string& p) { m_path = p; }
    /// Re-read the file (keeps variables). False if there's no file or it doesn't parse.
    bool Reload(std::string* error = nullptr);
    /// The last load error ("" = the graph loaded fine).
    const std::string& LastError() const { return m_error; }
    /// The graph's blackboard (live values in Play mode).
    const std::unordered_map<std::string, vs::VsValue>& Variables() const { return m_ctx.variables; }
    /// Read back a blackboard variable (handy for tests/gameplay glue).
    vs::VsValue GetVariable(const std::string& name) const;

    void Start() override;
    void Update(float dt) override;
    // Event entries: "OnCollision" / "OnTrigger" (2D and 3D, on enter) and
    // "OnClick" run with the other object available to Other Name / Other Tag.
    void OnCollisionEnter2D(const Collision2D& c) override { Fire("OnCollision", c.gameObject); }
    void OnCollisionEnter3D(const Collision3D& c) override { Fire("OnCollision", c.gameObject); }
    void OnTriggerEnter2D(Collider2D* other) override { Fire("OnTrigger", other ? other->gameObject : nullptr); }
    void OnTriggerEnter3D(Collider3D* other) override { Fire("OnTrigger", other ? other->gameObject : nullptr); }
    void OnMouseDown() override { Fire("OnClick", gameObject); }
    /// Clip events run "OnAnimEvent:<name>" (an On Anim Event node filtered to
    /// that name) and then the unfiltered "OnAnimEvent" entry.
    void OnAnimationEvent(const std::string& name) override {
        m_ctx.variables["anim_event"] = vs::VsValue(name);
        Fire(("OnAnimEvent:" + name).c_str(), nullptr);
        Fire("OnAnimEvent", nullptr);
    }

private:
    void Fire(const char* entryName, GameObject* other) {
        if (!m_graph) return;
        int entry = m_graph->Entry(entryName);
        if (entry < 0) return;
        m_ctx.gameObject = gameObject; m_ctx.transform = transform;
        m_ctx.other = other;
        m_graph->Run(entry, m_ctx);
        m_ctx.other = nullptr;
    }
    std::unique_ptr<vs::NodeGraph> m_graph;
    vs::VsContext m_ctx;
    std::string m_source;
    std::string m_path;
    std::string m_error;
};

} // namespace okay
