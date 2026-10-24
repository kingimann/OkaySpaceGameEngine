#pragma once
#include <algorithm>
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
    GameObject* other      = nullptr;   ///< the other object of a collision / trigger / click event
    std::unordered_map<std::string, VsValue> variables;
};

class NodeGraph;

/// Which data input of a node type is its optional "Target" object input (the
/// object it acts on instead of the graph's own), or -1 when it has none. The
/// editor appends a "Target" pin to these node types from this same table.
int TargetPinFor(const std::string& type);

/// Base class for a node in a visual script. Nodes come in two flavors:
/// *data* nodes (compute a value via Eval) and *exec* nodes (perform side
/// effects via Exec and pass control to the next node).
class VsNode {
public:
    virtual ~VsNode() = default;

    int id = -1;
    int fileId = -1;            ///< the node's id in the .okayvs file / editor (-1 = built in code)
    std::string type;           ///< the node's type name from the file ("ForLoop"); empty when built in code
    /// The optional "Target" data input of a node that acts on an object
    /// (Translate, Get Position, Add Force...): when something feeds it, the node
    /// acts on that object instead of the graph's own (-1 = no such input).
    int targetPin = -1;

    // Debug trace (only recorded while NodeGraph::Tracing() is on — the editor
    // turns it on so a running graph can light up and show live values).
    unsigned execCount = 0;                  ///< times this node has run
    std::vector<VsValue> lastOut;            ///< last value produced per data output pin

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
    /// Values typed into unconnected data inputs (the editor's inline boxes):
    /// used when nothing is wired into that pin.
    std::vector<VsValue>  inDefault;
    std::vector<char>     hasDefault;
    void SetDefault(int inPin, const VsValue& v) {
        if (inPin < 0) return;
        if (inPin >= (int)inDefault.size()) { inDefault.resize(inPin + 1); hasDefault.resize(inPin + 1, 0); }
        inDefault[inPin] = v; hasDefault[inPin] = 1;
    }

    /// True when something feeds data input `inPin` (a wire or a typed value).
    bool HasInput(int inPin) const {
        if (inPin >= 0 && inPin < (int)dataIn.size() && dataIn[inPin].node >= 0) return true;
        return inPin >= 0 && inPin < (int)hasDefault.size() && hasDefault[inPin];
    }

    /// The value feeding data input `inPin` (wire, typed value, or `fallback`).
    VsValue InputValue(int inPin, NodeGraph& g, VsContext& ctx, const VsValue& fallback = {}) const {
        return In(inPin, g, ctx, fallback);
    }

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
    /// Execute an exec chain starting at `entryNode`. With `resume`, the first node
    /// is a suspended one being picked up again (it sees Resuming() == true).
    void Run(int entryNode, VsContext& ctx, bool resume = false);

    /// An Exec() result meaning "not done yet - park this chain and call me again
    /// next frame" (Delay, Action blocks with a Wait inside). Resume() picks every
    /// parked chain up again; the chain continues from that node once it returns a pin.
    static constexpr int kSuspend = -2;
    /// Continue parked chains (called once per frame by the owning component).
    void Resume(VsContext& ctx) {
        if (m_suspended.empty()) return;
        std::vector<int> ids; ids.swap(m_suspended);
        for (int id : ids) Run(id, ctx, true);
    }
    /// True while a parked node is being resumed (only for that node's own Exec).
    bool Resuming() const { return m_resuming; }
    /// True when node `id` is parked waiting to resume.
    bool IsSuspended(int id) const { return std::find(m_suspended.begin(), m_suspended.end(), id) != m_suspended.end(); }

    /// Register an entry node under an event name ("OnStart", "OnUpdate",
    /// "OnKey:space", ...). A graph may hold several nodes for the same event —
    /// all of them run, in the order they were added.
    void SetEntry(const std::string& name, int nodeId) {
        auto& v = m_entries[name];
        if (std::find(v.begin(), v.end(), nodeId) == v.end()) v.push_back(nodeId);
    }
    /// The first entry node for `name` (-1 if none).
    int  Entry(const std::string& name) const {
        auto it = m_entries.find(name);
        return (it != m_entries.end() && !it->second.empty()) ? it->second.front() : -1;
    }
    /// Every entry node registered for `name`.
    const std::vector<int>& Entries(const std::string& name) const {
        static const std::vector<int> none;
        auto it = m_entries.find(name);
        return it != m_entries.end() ? it->second : none;
    }
    /// Run every entry registered for `name`. Returns how many ran.
    int RunEntries(const std::string& name, VsContext& ctx) {
        auto it = m_entries.find(name);
        if (it == m_entries.end()) return 0;
        const std::vector<int> ids = it->second;   // copy: a chain could reload the graph
        for (int id : ids) Run(id, ctx);
        return (int)ids.size();
    }
    /// All registered event names (for timers / key events that need polling).
    const std::unordered_map<std::string, std::vector<int>>& AllEntries() const { return m_entries; }

    /// Record execution counts + last data values on the nodes (editor debugging).
    static bool& Tracing() { static bool on = false; return on; }
    // ---- Runaway protection ---------------------------------------------
    /// Most node executions one event may run (including loop bodies and chains
    /// started from it) before the event is stopped and an error reported.
    static constexpr long kMaxStepsPerEvent = 1000000;
    /// Deepest a data pull may nest before it is treated as a wiring cycle.
    static constexpr int kMaxPullDepth = 512;
    /// True once the event being run was stopped (loop nodes check it to stop too).
    bool Aborted() const { return m_abort; }
    /// The last runtime error ("" if none) and the file id of the node it was
    /// reported on (-1 if unknown). Each distinct error is logged once; repeats count up.
    const std::string& LastError() const { return m_lastError; }
    /// Report a runtime problem at node `n` (logged once per distinct message).
    void Error(const VsNode* n, VsContext& ctx, const std::string& what) { ReportError(n, ctx, what); }
    int LastErrorNode() const { return m_lastErrorNode; }
    int ErrorCount() const { return m_errorCount; }

    /// Look a node up by its file / editor id (-1 when absent).
    int NodeByFileId(int fileId) const {
        for (const auto& n : m_nodes) if (n && n->fileId == fileId) return n->id;
        return -1;
    }

    /// Parse a graph from the simple OkayVS text format (see docs/visual_scripting.md).
    static std::unique_ptr<NodeGraph> Parse(const std::string& text, std::string* error = nullptr);

private:
    std::vector<std::unique_ptr<VsNode>> m_nodes;
    std::unordered_map<std::string, std::vector<int>> m_entries;
    int m_execGuard = 0;     // current data-pull depth (cycle guard)
    int m_runDepth = 0;      // nested Run() calls (loop bodies, sequences)
    long m_steps = 0;        // node executions in the current event
    bool m_abort = false;    // the current event was stopped
    std::string m_lastError;
    int m_lastErrorNode = -1;
    int m_errorCount = 0;
    void ReportError(const VsNode* n, VsContext& ctx, const std::string& what);
    // Target swapping: while a targeted node runs, ctx.gameObject/transform are
    // its target; the graph's own object is kept here so the node's other inputs
    // are still evaluated for the graph's own object.
    struct SelfCtx { GameObject* go; Transform* tr; };
    std::vector<SelfCtx> m_selfStack;
    /// Resolve `n`'s Target input. Returns false when a target was given but no
    /// longer exists (reported); `out` is null when no target was given.
    bool ResolveTarget(VsNode* n, VsContext& ctx, GameObject*& out);
    std::vector<int> m_suspended;   // parked chains (node ids), resumed next frame
    bool m_resuming = false;
};

} // namespace okay::vs
