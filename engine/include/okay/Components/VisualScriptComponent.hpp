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
#include <vector>

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
    // Event entries: "OnCollision" / "OnTrigger" (2D and 3D, on enter, plus the
    // "...Exit" pair) and the mouse events run with the other object available
    // to Other Name / Other Tag.
    void OnCollisionEnter2D(const Collision2D& c) override { Fire("OnCollision", c.gameObject); }
    void OnCollisionEnter3D(const Collision3D& c) override { Fire("OnCollision", c.gameObject); }
    void OnTriggerEnter2D(Collider2D* other) override { Fire("OnTrigger", other ? other->gameObject : nullptr); }
    void OnTriggerEnter3D(Collider3D* other) override { Fire("OnTrigger", other ? other->gameObject : nullptr); }
    void OnCollisionExit2D(const Collision2D& c) override { Fire("OnCollisionExit", c.gameObject); }
    void OnCollisionExit3D(const Collision3D& c) override { Fire("OnCollisionExit", c.gameObject); }
    void OnTriggerExit2D(Collider2D* other) override { Fire("OnTriggerExit", other ? other->gameObject : nullptr); }
    void OnTriggerExit3D(Collider3D* other) override { Fire("OnTriggerExit", other ? other->gameObject : nullptr); }
    void OnMouseDown() override { Fire("OnClick", gameObject); }
    void OnMouseEnter() override { Fire("OnMouseEnter", gameObject); }
    void OnMouseExit() override { Fire("OnMouseExit", gameObject); }
    /// Clip events run "OnAnimEvent:<name>" (an On Anim Event node filtered to
    /// that name) and then the unfiltered "OnAnimEvent" entry.
    void OnAnimationEvent(const std::string& name) override {
        m_ctx.variables["anim_event"] = vs::VsValue(name);
        Fire(("OnAnimEvent:" + name).c_str(), nullptr);
        Fire("OnAnimEvent", nullptr);
    }
    /// A custom event from another graph (Send / Broadcast Event nodes) or from
    /// code: runs this graph's "On Custom Event" nodes named `name`. `value` is
    /// readable with the Event Value node; `sender` with Other Name.
    void SendEvent(const std::string& name, const vs::VsValue& value = {}, GameObject* sender = nullptr) {
        m_ctx.variables["event_value"] = value;
        m_ctx.variables["event_name"] = vs::VsValue(name);
        std::string key = name;   // entry names are one token: "Player Hit" -> "Player_Hit"
        for (char& c : key) if (c == ' ' || c == '\t') c = '_';
        Fire(("OnEvent:" + key).c_str(), sender);
    }
    /// Set a blackboard variable from outside (Set Object Var node, gameplay code).
    void SetVariable(const std::string& name, const vs::VsValue& v) { m_ctx.variables[name] = v; }

private:
    void Fire(const char* entryName, GameObject* other) {
        if (!m_graph) return;
        if (m_graph->Entries(entryName).empty()) return;
        m_ctx.gameObject = gameObject; m_ctx.transform = transform;
        GameObject* prev = m_ctx.other;
        m_ctx.other = other;
        m_graph->RunEntries(entryName, m_ctx);
        m_ctx.other = prev;
    }
    // Polled events, rebuilt whenever a new graph is loaded.
    struct KeyEntry { std::string entry; char key; bool up; };
    struct TimerEntry { std::string entry; float period; float acc; };
    void ScanPolledEvents();
    const vs::NodeGraph* m_scanned = nullptr;
    std::vector<KeyEntry> m_keyEntries;
    std::vector<TimerEntry> m_timerEntries;
    std::unique_ptr<vs::NodeGraph> m_graph;
    vs::VsContext m_ctx;
    std::string m_source;
    std::string m_path;
    std::string m_error;
};

} // namespace okay
