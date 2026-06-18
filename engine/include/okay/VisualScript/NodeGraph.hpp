#pragma once
#include "okay/VisualScript/VsValue.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {
class Transform;
class GameObject;
}

namespace okay::vs {

/// Runtime state shared with executing nodes: the owning object, frame delta,
/// and a blackboard of named variables (the graph's persistent memory).
struct VsContext {
    GameObject* gameObject = nullptr;
    Transform*  transform  = nullptr;
    float       deltaTime  = 0.0f;
    std::unordered_map<std::string, VsValue> variables;
};

class NodeGraph;

/// Base class for a node in a visual script. Nodes come in two flavors:
/// *data* nodes (compute a value via Eval) and *exec* nodes (perform side
/// effects via Exec and pass control to the next node).
class VsNode {
public:
    virtual ~VsNode() = default;

    int id = -1;

    /// Number of execution output pins (0 means this is a pure data node).
    virtual int ExecOutCount() const { return 0; }
    /// Compute the value of data output pin `outPin`.
    virtual VsValue Eval(int /*outPin*/, NodeGraph& /*g*/, VsContext& /*ctx*/) { return {}; }
    /// Run side effects; return which exec out pin to follow next (or -1 to stop).
    virtual int Exec(NodeGraph& /*g*/, VsContext& /*ctx*/) { return 0; }

    /// Wiring, indexed by pin. Filled in by NodeGraph::Connect*.
    struct DataLink { int node = -1; int pin = 0; };
    std::vector<DataLink> dataIn;   // [inputPin] -> producer
    std::vector<int>      execOut;  // [execOutPin] -> next node id (-1 = none)

protected:
    /// Pull the value wired into data input pin `inPin` (or a default).
    VsValue In(int inPin, NodeGraph& g, VsContext& ctx, const VsValue& fallback = {}) const;
    /// Follow exec output pin `outPin`.
    int Next(int outPin) const {
        return (outPin >= 0 && outPin < (int)execOut.size()) ? execOut[outPin] : -1;
    }
};

/// A directed graph of nodes wired together by data and execution links.
/// Build it in code (Add/Connect) or load it from a text description.
class NodeGraph {
public:
    /// Create a node of type T, returning a pointer (ownership stays in graph).
    template <typename T, typename... Args>
    T* Add(Args&&... args) {
        auto* ptr = static_cast<T*>(
            AddNode(std::make_unique<T>(std::forward<Args>(args)...)));
        return ptr;
    }

    /// Adopt a pre-constructed node (used by the text parser).
    VsNode* AddNode(std::unique_ptr<VsNode> node) {
        VsNode* ptr = node.get();
        ptr->id = static_cast<int>(m_nodes.size());
        ptr->execOut.assign(ptr->ExecOutCount(), -1);
        m_nodes.push_back(std::move(node));
        return ptr;
    }

    VsNode* Node(int id) const {
        return (id >= 0 && id < (int)m_nodes.size()) ? m_nodes[id].get() : nullptr;
    }
    int NodeCount() const { return static_cast<int>(m_nodes.size()); }

    /// Wire exec out pin of `from` to `to`.
    void ConnectExec(int from, int outPin, int to);
    /// Wire data out pin of `producer` into input pin of `consumer`.
    void ConnectData(int consumer, int inPin, int producer, int outPin);

    /// Evaluate a data node's output pin.
    VsValue Pull(int nodeId, int outPin, VsContext& ctx);
    /// Execute an exec chain starting at `entryNode`.
    void Run(int entryNode, VsContext& ctx);

    /// Name an entry node (e.g. "OnStart", "OnUpdate") for later lookup.
    void SetEntry(const std::string& name, int nodeId) { m_entries[name] = nodeId; }
    int  Entry(const std::string& name) const {
        auto it = m_entries.find(name);
        return it != m_entries.end() ? it->second : -1;
    }

    /// Parse a graph from the simple OkayVS text format (see docs/visual_scripting.md).
    static std::unique_ptr<NodeGraph> Parse(const std::string& text, std::string* error = nullptr);

private:
    std::vector<std::unique_ptr<VsNode>> m_nodes;
    std::unordered_map<std::string, int> m_entries;
    int m_execGuard = 0; // guards against runaway loops
};

} // namespace okay::vs
