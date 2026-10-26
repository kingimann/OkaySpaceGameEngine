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

// ---- .okayvs text quoting (shared by the runtime, the checker and the editor) ----
// A quoted field is "..." with \" for a quote and \\ for a backslash; '#' starts a
// comment outside quotes. Old files (no escapes) still read the same: a quote that
// never closes under the escape rules is read the old way.
/// The line's tokens, comment removed. A quoted token comes back as `"` + its
/// unescaped text + `"`.
std::vector<std::string> TokenizeLine(const std::string& line);
/// `s` as one quoted field.
std::string QuoteText(const std::string& s);
/// `s` as one field: bare when it can be, quoted when it holds a space, tab,
/// '#' or '"' (or is empty).
std::string QuoteIfNeeded(const std::string& s);
/// True if `field` is exactly one quoted field; `out` gets its text.
bool ParseQuoted(const std::string& field, std::string& out);

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
    NodeGraph() = default;
    NodeGraph(const NodeGraph&) = delete;
    NodeGraph& operator=(const NodeGraph&) = delete;
    ~NodeGraph();

    /// Which graph this is to the debugger (the .okayvs file it came from; "" = no
    /// breakpoints). Set by the Visual Script component.
    std::string debugKey;
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
        const std::string prevEvent = m_event;
        m_event = "(continuing after a wait)";
        for (int id : ids) Run(id, ctx, true);
        m_event = prevEvent;
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
        const std::string prevEvent = m_event;
        m_event = name;
        for (int id : ids) Run(id, ctx);
        m_event = prevEvent;
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
    /// Most data reads (input pulls) one event may make: a separate, larger
    /// budget, so heavy but finite graphs keep the node-run headroom they had.
    static constexpr long kMaxReadsPerEvent = 4 * kMaxStepsPerEvent;
    /// Deepest loops / sequences may nest (each Body or output runs as a nested Run).
    static constexpr int kMaxRunDepth = 256;
    /// Deepest a data pull may nest before it is treated as a wiring cycle.
    static constexpr int kMaxPullDepth = 512;
    /// True once the event being run was stopped, or the function being run has
    /// returned (loop nodes check it so they stop too).
    bool Aborted() const { return m_abort || m_loopSignal != 0 || (!m_frames.empty() && m_frames.back().returned); }

    // ---- Loops: Break / Continue ----------------------------------------
    /// Loop nodes bracket their iterations with these, and after each run of the
    /// body take the signal Break / Continue left (0 none, 1 break, 2 continue).
    enum LoopSignal { kLoopNone = 0, kLoopBreak = 1, kLoopContinue = 2 };
    void EnterLoop() { m_loops.push_back((int)m_frames.size()); }
    void LeaveLoop() { if (!m_loops.empty()) m_loops.pop_back(); m_loopSignal = kLoopNone; }
    int TakeLoopSignal() { const int s = m_loopSignal; m_loopSignal = kLoopNone; return s; }
    /// True when a loop of the current function (or event) is running, so
    /// Break / Continue have a loop to act on.
    bool InLoop() const { return !m_loops.empty() && m_loops.back() == (int)m_frames.size(); }
    void SignalLoop(LoopSignal s) { m_loopSignal = s; }

    // ---- Functions -------------------------------------------------------
    /// Deepest functions may call each other (or themselves) before a call is refused.
    static constexpr int kMaxCallDepth = 64;
    /// Highest pin index + 1 a .okayvs line may name (exec / data / default), and
    /// the most outputs a Sequence can have.
    static constexpr int kMaxPins = 64;
    /// The Function node (graph id) named `name`, or -1.
    int FunctionNode(const std::string& name) const {
        auto it = m_functions.find(name);
        return it != m_functions.end() ? it->second : -1;
    }
    /// Run function `name` with `args`; its Return values land in `rets` (one per
    /// return value; unset ones stay empty). False (and reported) when it can't run.
    bool CallFunction(const std::string& name, std::vector<VsValue> args, std::vector<VsValue>& rets,
                      const VsNode* caller, VsContext& ctx);
    /// Inside a running function: its arguments / where Return writes / its locals.
    bool InFunction() const { return !m_frames.empty(); }
    const VsValue* Arg(int i) const {
        if (m_frames.empty() || i < 0 || i >= (int)m_frames.back().args.size()) return nullptr;
        return &m_frames.back().args[(std::size_t)i];
    }
    /// The function a Return / Set Local is running in ("" outside any function).
    const std::string& CurrentFunction() const { static const std::string none; return m_frames.empty() ? none : m_frames.back().name; }
    void SetReturn(std::vector<VsValue> values) {
        if (m_frames.empty()) return;
        m_frames.back().rets = std::move(values);
        m_frames.back().returned = true;
    }
    std::unordered_map<std::string, VsValue>* Locals() { return m_frames.empty() ? nullptr : &m_frames.back().locals; }

    // ---- Debugger view -----------------------------------------------------
    /// True while one of this graph's chains is running.
    bool Running() const { return m_runDepth > 0; }
    /// How many functions deep the running chain is (0 = in an event).
    int CallDepth() const { return (int)m_frames.size(); }
    /// The event whose chain is running ("OnUpdate", "OnKey:space"...; "" when idle).
    const std::string& CurrentEvent() const { return m_event; }
    /// The running function calls, outermost first: name, arguments, locals.
    struct DebugFrame {
        std::string name;
        std::vector<VsValue> args;
        std::unordered_map<std::string, VsValue> locals;
    };
    std::vector<DebugFrame> DebugFrames() const {
        std::vector<DebugFrame> out;
        for (const Frame& f : m_frames) out.push_back({f.name, f.args, f.locals});
        return out;
    }
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
    long m_reads = 0;        // data reads in the current event
    bool m_abort = false;    // the current event was stopped
    std::string m_event;        // the event being run (debugger view)
    std::vector<int> m_loops;   // running loops: the function-frame depth each started at
    int m_loopSignal = 0;       // a Break / Continue waiting for its loop
    struct Frame {
        std::string name;
        std::vector<VsValue> args, rets;
        std::unordered_map<std::string, VsValue> locals;
        bool returned = false;
    };
    std::vector<Frame> m_frames;                        // running function calls, innermost last
    std::unordered_map<std::string, int> m_functions;   // function name -> its Function node
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
