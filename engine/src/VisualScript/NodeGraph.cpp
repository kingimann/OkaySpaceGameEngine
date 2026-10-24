#include "okay/VisualScript/NodeGraph.hpp"
#include "okay/VisualScript/VsTypes.hpp"
#include "okay/VisualScript/VsApi.hpp"
#include "okay/VisualScript/NodesFunctions.hpp"
#include "okay/VisualScript/NodesLoops.hpp"
#include "okay/VisualScript/Nodes.hpp"
#include "okay/VisualScript/NodesWave4.hpp"
#include "okay/VisualScript/NodesActions.hpp"
#include "okay/VisualScript/NodesPhysics.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Scene/GameObject.hpp"

#include <cctype>
#include <sstream>
#include <unordered_map>

namespace okay::vs {

// ---- VsNode helpers ---------------------------------------------------
VsValue VsNode::In(int inPin, NodeGraph& g, VsContext& ctx, const VsValue& fallback) const {
    if (inPin >= 0 && inPin < (int)dataIn.size()) {
        const DataLink& l = dataIn[inPin];
        if (l.node >= 0) return g.Pull(l.node, l.pin, ctx);
    }
    if (inPin >= 0 && inPin < (int)hasDefault.size() && hasDefault[inPin]) return inDefault[inPin];
    return fallback;
}

// ---- NodeGraph wiring + execution -------------------------------------
void NodeGraph::ConnectExec(int from, int outPin, int to) {
    VsNode* n = Node(from);
    if (!n || outPin < 0) return;
    if (outPin >= (int)n->execOut.size()) n->execOut.resize(outPin + 1, -1);
    n->execOut[outPin] = to;
}

void NodeGraph::ConnectData(int consumer, int inPin, int producer, int outPin) {
    VsNode* n = Node(consumer);
    if (!n || inPin < 0) return;
    if (inPin >= (int)n->dataIn.size()) n->dataIn.resize(inPin + 1);
    n->dataIn[inPin] = {producer, outPin};
}

VsValue NodeGraph::Pull(int nodeId, int outPin, VsContext& ctx) {
    VsNode* n = Node(nodeId);
    if (m_abort) return {};
    // A data wire that loops back into itself would recurse forever.
    if (m_execGuard >= kMaxPullDepth) {
        ReportError(n, ctx, "a data wire loops back into itself (a node's input depends on its own output); "
                            "the value was treated as 0 and the event stopped");
        m_abort = true;
        return {};
    }
    // Inputs of a targeted node are evaluated for the graph's own object.
    GameObject* const curGo = ctx.gameObject;
    Transform* const curTr = ctx.transform;
    if (!m_selfStack.empty()) { ctx.gameObject = m_selfStack.back().go; ctx.transform = m_selfStack.back().tr; }
    ++m_execGuard;
    VsValue v;
    if (n) {
        GameObject* target = nullptr;
        if (!ResolveTarget(n, ctx, target)) {
            v = {};
        } else if (target) {
            m_selfStack.push_back({ctx.gameObject, ctx.transform});
            ctx.gameObject = target; ctx.transform = target->transform;
            v = n->Eval(outPin, *this, ctx);
            ctx.gameObject = m_selfStack.back().go; ctx.transform = m_selfStack.back().tr;
            m_selfStack.pop_back();
        } else {
            v = n->Eval(outPin, *this, ctx);
        }
    }
    --m_execGuard;
    ctx.gameObject = curGo; ctx.transform = curTr;
    if (n && Tracing() && outPin >= 0 && outPin < 16) {
        if ((int)n->lastOut.size() <= outPin) n->lastOut.resize(outPin + 1);
        n->lastOut[outPin] = v;
    }
    return v;
}

bool NodeGraph::CallFunction(const std::string& name, std::vector<VsValue> args, std::vector<VsValue>& rets,
                             const VsNode* caller, VsContext& ctx) {
    const int start = FunctionNode(name);
    if (start < 0) {
        ReportError(caller, ctx, name.empty() ? std::string("no function is picked to call")
                                              : "there's no function named '" + name + "' in this graph");
        return false;
    }
    if ((int)m_frames.size() >= kMaxCallDepth) {
        ReportError(caller, ctx, "function '" + name + "' was called more than " + std::to_string(kMaxCallDepth) +
                                 " deep (a function calling itself with no way to stop?); the call was skipped");
        return false;
    }
    Frame f; f.name = name; f.args = std::move(args);
    m_frames.push_back(std::move(f));
    // The body runs as a nested chain: it shares the event's step budget, so a
    // function looping forever is stopped like any other runaway loop.
    Run(start, ctx);
    rets = std::move(m_frames.back().rets);
    m_frames.pop_back();
    return !m_abort;
}

bool NodeGraph::ResolveTarget(VsNode* n, VsContext& ctx, GameObject*& out) {
    out = nullptr;
    if (!n || n->targetPin < 0 || !n->HasInput(n->targetPin)) return true;
    const VsValue tv = n->InputValue(n->targetPin, *this, ctx);
    out = VsResolveObject(ctx, tv);
    if (out && out->transform) return true;
    const std::string what = tv.IsObject() ? std::string("its Target object no longer exists (it was destroyed)")
                                           : "no object named '" + tv.AsString() + "' was found for its Target";
    ReportError(n, ctx, what + "; the node was skipped");
    out = nullptr;
    return false;
}

std::string VsObjectName(std::uint64_t id) {
    GameObject* o = id ? GameObject::FromInstanceId(id) : nullptr;
    return o ? o->name : std::string();
}
bool VsObjectAlive(std::uint64_t id) { return id && GameObject::FromInstanceId(id) != nullptr; }

void NodeGraph::ReportError(const VsNode* n, VsContext& ctx, const std::string& what) {
    const int nodeId = n ? n->fileId : -1;
    ++m_errorCount;
    if (what == m_lastError && nodeId == m_lastErrorNode) return;   // already reported
    m_lastError = what;
    m_lastErrorNode = nodeId;
    std::string where;
    if (n && !n->type.empty()) where = " at node '" + n->type + "' (#" + std::to_string(n->fileId) + ")";
    const std::string obj = ctx.gameObject ? ctx.gameObject->name : std::string("?");
    Log::Error("Visual Script on '", obj, "'", where, ": ", what);
}

void NodeGraph::Run(int entryNode, VsContext& ctx, bool resume) {
    // One budget per event: a top-level Run starts it, loop bodies and sequence
    // outputs (nested Runs) share it, so nested loops can't freeze the game.
    if (m_runDepth == 0) { m_steps = 0; m_abort = false; m_loops.clear(); m_loopSignal = kLoopNone; }
    ++m_runDepth;
    int cur = entryNode;
    bool first = true;
    while (cur >= 0 && !Aborted()) {
        VsNode* n = Node(cur);
        if (!n) break;
        if (++m_steps > kMaxStepsPerEvent) {
            ReportError(n, ctx, "stopped a runaway loop: more than " + std::to_string(kMaxStepsPerEvent) +
                                " node steps in one event. The rest of this event was skipped.");
            m_abort = true;
            break;
        }
        if (Tracing() && !(resume && first)) ++n->execCount;
        // A node aimed at another object (its Target input) runs with that object
        // as ctx.gameObject / transform; a target that no longer exists skips it.
        GameObject* target = nullptr;
        int outPin;
        if (!ResolveTarget(n, ctx, target)) {
            outPin = n->ExecOutCount() > 0 ? 0 : -1;
        } else if (target) {
            m_selfStack.push_back({ctx.gameObject, ctx.transform});
            ctx.gameObject = target; ctx.transform = target->transform;
            m_resuming = resume && first;
            outPin = n->Exec(*this, ctx);
            m_resuming = false;
            ctx.gameObject = m_selfStack.back().go; ctx.transform = m_selfStack.back().tr;
            m_selfStack.pop_back();
        } else {
            m_resuming = resume && first;
            outPin = n->Exec(*this, ctx);
            m_resuming = false;
        }
        first = false;
        if (outPin == kSuspend && !m_frames.empty()) {
            // A function has to finish before its caller carries on: there's no
            // caller to come back to next frame.
            ReportError(n, ctx, "Delay / Wait can't pause inside a function (a function runs to the end at once); "
                                "the function stopped here");
            m_frames.back().returned = true;
            break;
        }
        if (outPin == kSuspend) {   // park here; Resume() continues from this node
            if (!IsSuspended(cur)) m_suspended.push_back(cur);
            break;
        }
        if (outPin < 0) break;
        cur = (outPin < (int)n->execOut.size()) ? n->execOut[outPin] : -1;
    }
    --m_runDepth;
}

int TargetPinFor(const std::string& type) {
    static const std::unordered_map<std::string, int> kPins = {
        {"Translate", 3}, {"SetPosition", 3}, {"Rotate", 1}, {"SetScale", 3}, {"SetRotation", 1},
        {"SetPositionVec", 1}, {"MoveBy", 1}, {"Rotate3", 3}, {"SetRotation3", 3},
        {"GetPosition", 0}, {"GetX", 0}, {"GetY", 0}, {"GetZ", 0}, {"GetScale", 0}, {"GetRotation", 0},
        {"ForwardVector", 0}, {"RightVector", 0}, {"UpVector", 0},
        {"AddForce", 3}, {"AddImpulse", 3}, {"SetVelocity", 3}, {"VelX", 0}, {"VelY", 0}, {"VelZ", 0},
        {"SetColor", 4}, {"SetVisible", 1}, {"PlaySound", 0}, {"PlayClip", 0},
        {"AnimTrigger", 0}, {"AnimSetFloat", 1}, {"AnimSetBool", 1}, {"AnimState", 0},
        {"SetBlendShape", 1}, {"GetBlendShape", 0}, {"IkLook", 2}, {"IkHand", 2},
        {"GetName", 0}, {"GetTag", 0}};
    auto it = kPins.find(type);
    if (it != kPins.end()) return it->second;
    // Engine-API nodes of an object type take their Target last, like the rest.
    if (const VsApiEntry* e = VsApiFind(type)) return e->hasTarget ? (int)e->ins.size() - 1 : -1;
    return -1;
}

// ---- Node factory -----------------------------------------------------
namespace {
VsValue ParseValue(const std::string& s) {
    if (s == "true")  return true;
    if (s == "false") return false;
    if (!s.empty() && s.front() == '"') {
        std::string inner = s.substr(1);
        if (!inner.empty() && inner.back() == '"') inner.pop_back();
        return inner;
    }
    try { size_t idx = 0; float f = std::stof(s, &idx); if (idx == s.size()) return f; }
    catch (...) {}
    return s; // fall back to a string token
}
} // namespace

std::unique_ptr<VsNode> MakeNode(const std::string& type,
                                 const std::vector<std::string>& argsIn,
                                 std::string* err) {
    // A node whose text field was left blank gets a sensible default instead of
    // failing — one empty "Set Var" used to stop the whole graph from loading.
    std::vector<std::string> args = argsIn;
    auto need = [&](size_t n) {
        static const std::unordered_map<std::string, std::string> kDefaults = {
            {"Const", "0"}, {"GetVar", "x"}, {"SetVar", "x"}, {"AddVar", "x"}, {"HasVar", "x"},
            {"Key", "space"}, {"KeyDown", "space"}, {"KeyUp", "space"},
            {"Timer", "1"}, {"Wait", "1"}, {"Repeat", "1"}, {"DoN", "1"}, {"Chance", "0.5"}, {"Toggle", "x"}, {"Compare", ">"},
            {"NetGetVar", "x"}, {"NetSetVar", "x"}, {"SteamGetStat", "stat"}, {"SteamSetStat", "stat"}};
        auto it = kDefaults.find(type);
        while (args.size() < n) args.push_back(it != kDefaults.end() ? it->second : std::string());
        (void)err;
        return true;
    };
    // "OnKey:space" / "OnTimer:2" / "OnEvent:Hit" (the entry-name spelling) is the
    // same node as "OnKey space": accept both.
    if (const auto colon = type.find(':'); colon != std::string::npos && colon + 1 < type.size()) {
        const std::string base = type.substr(0, colon);
        if (base == "OnKey" || base == "OnKeyUp" || base == "OnTimer" || base == "OnEvent" || base == "OnAnimEvent" ||
            base == "OnVarChanged" || base == "OnSceneVarChanged" || base == "OnGlobalVarChanged") {
            std::vector<std::string> a{type.substr(colon + 1)};
            a.insert(a.end(), args.begin(), args.end());
            return MakeNode(base, a, err);
        }
    }
    if (type == "OnFixedUpdate" || type == "OnDestroy" || type == "OnEnable" || type == "OnDisable")
        return std::make_unique<EventNode>();
    // Engine-API nodes ("Transform.Translate/3"): generated from the script bindings.
    if (auto api = MakeApiNode(type)) return api;
    if (type == "OnStart" || type == "OnUpdate" || type == "OnCollision" || type == "OnTrigger" || type == "OnClick" || type == "OnAnimEvent" ||
        type == "OnKey" || type == "OnKeyUp" || type == "OnTimer" || type == "OnCollisionExit" ||
        type == "OnTriggerExit" || type == "OnMouseEnter" || type == "OnMouseExit" ||
        type == "OnMouseDown" || type == "OnMouseUp" || type == "OnMouseOver" ||
        type == "OnCollisionStay" || type == "OnLateUpdate" || type == "OnTriggerStay")
        return std::make_unique<EventNode>();
    if (type == "OnEvent") return std::make_unique<CustomEventNode>();
    // ---- Actions steps (the no-code library) ----
    // The editor writes the encoded steps as one quoted arg; hand-written files may
    // split them into tokens, so join everything back up first.
    if (type == "Action" || type == "Actions" || type == "Condition") {
        std::string enc;
        for (const auto& a : args) { if (!enc.empty()) enc += ' '; enc += a; }
        if (type == "Condition") {
            ActionList::Item c = ActionList::DecodeItem(enc);
            if (c.op.empty()) c.op = "always";
            return std::make_unique<ActionConditionNode>(std::vector<ActionList::Item>{c});
        }
        return std::make_unique<ActionStepsNode>(ActionList::DecodeItems(enc));
    }
    if (type == "Delay") return std::make_unique<DelayNode>(args.empty() ? 1.0f : ParseValue(args[0]).AsFloat());
    // ---- Fourth wave ----
    {
        auto a0 = [&](const char* d) { return args.empty() || args[0].empty() ? std::string(d) : args[0]; };
        if (type == "SendEvent")      return std::make_unique<SendEventNode>(a0("Event"));
        if (type == "BroadcastEvent") return std::make_unique<BroadcastEventNode>(a0("Event"));
        if (type == "EventValue")     return std::make_unique<EventValueNode>(false);
        if (type == "EventName")      return std::make_unique<EventValueNode>(true);
        if (type == "SetObjectVar")   return std::make_unique<SetObjectVarNode>(a0("x"));
        if (type == "GetObjectVar")   return std::make_unique<GetObjectVarNode>(a0("x"));
        if (type == "SaveValue")      return std::make_unique<SaveValueNode>(a0("score"));
        if (type == "LoadValue")      return std::make_unique<LoadValueNode>(a0("score"));
        if (type == "Format")         return std::make_unique<FormatNode>(args.empty() ? std::string("{0}") : args[0]);
        if (type == "Substring")      return std::make_unique<TextFnNode>(TextFnNode::Op::Substring);
        if (type == "Replace")        return std::make_unique<TextFnNode>(TextFnNode::Op::Replace);
        if (type == "ToNumber")       return std::make_unique<TextFnNode>(TextFnNode::Op::ToNumber);
        if (type == "StartsWith")     return std::make_unique<TextFnNode>(TextFnNode::Op::StartsWith);
        if (type == "EndsWith")       return std::make_unique<TextFnNode>(TextFnNode::Op::EndsWith);
        if (type == "Trim")           return std::make_unique<TextFnNode>(TextFnNode::Op::Trim);
        if (type == "IndexOf")        return std::make_unique<TextFnNode>(TextFnNode::Op::IndexOf);
        if (type == "Noise")          return std::make_unique<MathFn4Node>(MathFn4Node::Op::Noise);
        if (type == "Snap")           return std::make_unique<MathFn4Node>(MathFn4Node::Op::Snap);
        if (type == "Wrap")           return std::make_unique<MathFn4Node>(MathFn4Node::Op::Wrap);
        if (type == "Average")        return std::make_unique<MathFn4Node>(MathFn4Node::Op::Average);
        if (type == "Distance2D")     return std::make_unique<MathFn4Node>(MathFn4Node::Op::Distance2D);
        if (type == "FindWithTag")    return std::make_unique<TagQueryNode>(TagQueryNode::Op::FindWithTag, a0("Player"));
        if (type == "CountWithTag")   return std::make_unique<TagQueryNode>(TagQueryNode::Op::CountWithTag, a0("Enemy"));
        if (type == "NearestWithTag") return std::make_unique<TagQueryNode>(TagQueryNode::Op::NearestWithTag, a0("Enemy"));
        if (type == "Teleport")       return std::make_unique<TeleportNode>(a0("self"));
        if (type == "SetVisible")     return std::make_unique<SetVisibleNode>();
        if (type == "QuitGame")       return std::make_unique<GameControlNode>(GameControlNode::Op::Quit);
        if (type == "PauseGame")      return std::make_unique<GameControlNode>(GameControlNode::Op::Pause);
        if (type == "ShowCursor")     return std::make_unique<GameControlNode>(GameControlNode::Op::ShowCursor);
        if (type == "ChanceBranch")   return std::make_unique<ChanceBranchNode>();
        if (type == "Counter")        return std::make_unique<CounterNode>();
        if (type == "EveryN")         return std::make_unique<EveryNNode>();
        if (type == "ShowUI")         return std::make_unique<ShowHideUINode>(true, a0("self"));
        if (type == "HideUI")         return std::make_unique<ShowHideUINode>(false, a0("self"));
    }
    if (type == "Const")   { if (!need(1)) return nullptr; return std::make_unique<ConstNode>(ParseValue(args[0])); }
    if (type == "GetVar")  { if (!need(1)) return nullptr; return std::make_unique<GetVarNode>(args[0]); }
    if (type == "SetVar")  { if (!need(1)) return nullptr; return std::make_unique<SetVarNode>(args[0]); }
    if (type == "Time")      return std::make_unique<TimeNode>();
    if (type == "DeltaTime") return std::make_unique<DeltaTimeNode>();
    if (type == "AxisX")     return std::make_unique<AxisXNode>();
    if (type == "AxisY")     return std::make_unique<AxisYNode>();
    if (type == "Add") return std::make_unique<BinaryOpNode>(BinaryOpNode::Op::Add);
    if (type == "Sub") return std::make_unique<BinaryOpNode>(BinaryOpNode::Op::Sub);
    if (type == "Mul") return std::make_unique<BinaryOpNode>(BinaryOpNode::Op::Mul);
    if (type == "Div") return std::make_unique<BinaryOpNode>(BinaryOpNode::Op::Div);
    if (type == "Compare") {
        if (!need(1)) return nullptr;
        const std::string& o = args[0];
        CompareNode::Op op = CompareNode::Op::Gt;
        if      (o == ">")  op = CompareNode::Op::Gt;
        else if (o == "<")  op = CompareNode::Op::Lt;
        else if (o == ">=") op = CompareNode::Op::Ge;
        else if (o == "<=") op = CompareNode::Op::Le;
        else if (o == "==") op = CompareNode::Op::Eq;
        else if (o == "!=") op = CompareNode::Op::Ne;
        else { if (err) *err = "unknown compare op '" + o + "'"; return nullptr; }
        return std::make_unique<CompareNode>(op);
    }
    // Unary math.
    if (type == "Abs")   return std::make_unique<UnaryMathNode>(UnaryMathNode::Op::Abs);
    if (type == "Neg")   return std::make_unique<UnaryMathNode>(UnaryMathNode::Op::Neg);
    if (type == "Sqrt")  return std::make_unique<UnaryMathNode>(UnaryMathNode::Op::Sqrt);
    if (type == "Sin")   return std::make_unique<UnaryMathNode>(UnaryMathNode::Op::Sin);
    if (type == "Cos")   return std::make_unique<UnaryMathNode>(UnaryMathNode::Op::Cos);
    if (type == "Floor") return std::make_unique<UnaryMathNode>(UnaryMathNode::Op::Floor);
    if (type == "Round") return std::make_unique<UnaryMathNode>(UnaryMathNode::Op::Round);
    if (type == "Sign")  return std::make_unique<UnaryMathNode>(UnaryMathNode::Op::Sign);
    // Binary math.
    if (type == "Mod")   return std::make_unique<MathFnNode>(MathFnNode::Op::Mod);
    if (type == "Min")   return std::make_unique<MathFnNode>(MathFnNode::Op::Min);
    if (type == "Max")   return std::make_unique<MathFnNode>(MathFnNode::Op::Max);
    if (type == "Pow")   return std::make_unique<MathFnNode>(MathFnNode::Op::Pow);
    if (type == "Clamp") return std::make_unique<ClampNode>();
    if (type == "Lerp")  return std::make_unique<LerpNode>();
    if (type == "Random")      return std::make_unique<RandomNode>();
    if (type == "RandomRange") return std::make_unique<RandomRangeNode>();
    // Logic.
    if (type == "And") return std::make_unique<LogicNode>(LogicNode::Op::And);
    if (type == "Or")  return std::make_unique<LogicNode>(LogicNode::Op::Or);
    if (type == "Xor") return std::make_unique<LogicNode>(LogicNode::Op::Xor);
    if (type == "Not") return std::make_unique<NotNode>();
    // Conditions.
    if (type == "Equals")    return std::make_unique<EqualsNode>(false);
    if (type == "NotEquals") return std::make_unique<EqualsNode>(true);
    if (type == "Approx")    return std::make_unique<ApproxNode>();
    if (type == "Between")   return std::make_unique<BetweenNode>();
    if (type == "Chance")    { if (!need(1)) return nullptr; return std::make_unique<ChanceNode>(ParseValue(args[0]).AsFloat()); }
    if (type == "HasVar")    { if (!need(1)) return nullptr; return std::make_unique<HasVarNode>(args[0]); }
    if (type == "KeyUp")     { if (!need(1)) return nullptr; return std::make_unique<KeyUpNode>(args[0].empty() ? ' ' : Input::KeyFromName(args[0])); }
    if (type == "Select") return std::make_unique<SelectNode>();
    if (type == "Concat") return std::make_unique<ConcatNode>();
    // Vectors.
    if (type == "MakeVec3") return std::make_unique<MakeVec3Node>();
    if (type == "VecX") return std::make_unique<BreakVec3Node>(0);
    if (type == "VecY") return std::make_unique<BreakVec3Node>(1);
    if (type == "VecZ") return std::make_unique<BreakVec3Node>(2);
    // Input.
    if (type == "Key")     { if (!need(1)) return nullptr; return std::make_unique<KeyNode>(args[0].empty() ? ' ' : Input::KeyFromName(args[0])); }
    if (type == "KeyDown") { if (!need(1)) return nullptr; return std::make_unique<KeyDownNode>(args[0].empty() ? ' ' : Input::KeyFromName(args[0])); }
    if (type == "MouseX") return std::make_unique<MouseXNode>();
    if (type == "MouseY") return std::make_unique<MouseYNode>();
    // Transform getters.
    if (type == "GetPosition") return std::make_unique<GetPositionNode>();
    if (type == "GetX") return std::make_unique<GetAxisNode>(0);
    if (type == "GetY") return std::make_unique<GetAxisNode>(1);
    if (type == "GetZ") return std::make_unique<GetAxisNode>(2);

    if (type == "Print")       return std::make_unique<PrintNode>();
    if (type == "Translate")   return std::make_unique<TranslateNode>();
    if (type == "SetPosition") return std::make_unique<SetPositionNode>();
    if (type == "Rotate")      return std::make_unique<RotateNode>();
    if (type == "Branch")      return std::make_unique<BranchNode>();
    if (type == "AddVar")      { if (!need(1)) return nullptr; return std::make_unique<AddVarNode>(args[0]); }
    if (type == "SetScale")    return std::make_unique<SetScaleNode>();
    if (type == "SetRotation") return std::make_unique<SetRotationNode>();
    if (type == "Destroy")     return std::make_unique<DestroyNode>();
    if (type == "Once")        return std::make_unique<OnceNode>();
    if (type == "FlipFlop")    return std::make_unique<FlipFlopNode>();
    if (type == "Timer")       { if (!need(1)) return nullptr; return std::make_unique<TimerNode>(ParseValue(args[0]).AsFloat()); }
    if (type == "Sequence")    { int c = args.empty() ? 2 : (int)ParseValue(args[0]).AsFloat(); return std::make_unique<SequenceNode>(c); }
    if (type == "Repeat")      { if (!need(1)) return nullptr; return std::make_unique<RepeatNode>((int)ParseValue(args[0]).AsFloat()); }
    if (type == "Toggle")      { if (!need(1)) return nullptr; return std::make_unique<ToggleNode>(args[0]); }
    if (type == "LogWarn")     return std::make_unique<LogNode>(1);
    if (type == "LogError")    return std::make_unique<LogNode>(2);
    if (type == "Spawn")       { if (!need(1)) return nullptr; return std::make_unique<SpawnPrefabNode>(args[0]); }
    if (type == "Wait")        { if (!need(1)) return nullptr; return std::make_unique<WaitNode>(ParseValue(args[0]).AsFloat()); }
    // Multiplayer.
    if (type == "NetConnected") return std::make_unique<NetConnectedNode>();
    if (type == "NetIsServer")  return std::make_unique<NetIsServerNode>();
    if (type == "NetIsClient")  return std::make_unique<NetIsClientNode>();
    if (type == "NetPeers")     return std::make_unique<NetPeersNode>();
    if (type == "NetId")        return std::make_unique<NetIdNode>();
    if (type == "NetGetVar")    { if (!need(1)) return nullptr; return std::make_unique<NetGetVarNode>(args[0]); }
    if (type == "NetHost")      { int p = args.empty() ? 45000 : (int)ParseValue(args[0]).AsFloat(); return std::make_unique<NetHostNode>(p); }
    if (type == "NetJoin") {
        // "host port" / "host:port" in one field (the editor's) or two args.
        std::string h = args.empty() || args[0].empty() ? "127.0.0.1" : args[0];
        std::string ps = args.size() >= 2 ? args[1] : std::string();
        if (ps.empty()) {
            size_t cut = h.find_first_of(" :");
            if (cut != std::string::npos) { ps = h.substr(cut + 1); h = h.substr(0, cut); }
        }
        int p = 45000;
        if (!ps.empty()) { float f = ParseValue(ps).AsFloat(); if (f > 0) p = (int)f; }
        return std::make_unique<NetJoinNode>(h, p);
    }
    if (type == "NetSend")      { if (!need(1)) return nullptr; return std::make_unique<NetSendNode>(args[0]); }
    if (type == "NetSetVar")    { if (!need(1)) return nullptr; return std::make_unique<NetSetVarNode>(args[0]); }
    if (type == "NetDisconnect") return std::make_unique<NetDisconnectNode>();
    if (type == "NetChat")        return std::make_unique<NetChatNode>();
    if (type == "NetRpc")         { if (!need(1)) return nullptr; return std::make_unique<NetRpcNode>(args[0]); }
    if (type == "NetSpawnOwned")  { if (!need(1)) return nullptr; return std::make_unique<NetSpawnOwnedNode>(args[0]); }
    if (type == "NetDespawn")     return std::make_unique<NetDespawnNode>();
    if (type == "NetReady")       return std::make_unique<NetReadyNode>();
    if (type == "NetStartMatch")  return std::make_unique<NetStartMatchNode>();
    if (type == "NetMatchStarted") return std::make_unique<NetMatchStartedNode>();
    if (type == "NetAllReady")    return std::make_unique<NetAllReadyNode>();
    // Steam.
    if (type == "SteamName")        return std::make_unique<SteamNameNode>();
    if (type == "SteamIsUnlocked")  { if (!need(1)) return nullptr; return std::make_unique<SteamUnlockedNode>(args[0]); }
    if (type == "SteamGetStat")     { if (!need(1)) return nullptr; return std::make_unique<SteamGetStatNode>(args[0]); }
    if (type == "SteamUnlock")      { if (!need(1)) return nullptr; return std::make_unique<SteamUnlockNode>(args[0]); }
    if (type == "SteamSetStat")     { if (!need(1)) return nullptr; return std::make_unique<SteamSetStatNode>(args[0]); }
    if (type == "SteamStore")       return std::make_unique<SteamStoreNode>();
    if (type == "SteamLeaderboard") { if (!need(1)) return nullptr; return std::make_unique<SteamLeaderboardNode>(args[0]); }
    // Audio / physics / tween.
    if (type == "PlaySound")   return std::make_unique<PlaySoundNode>();
    if (type == "AddForce")    return std::make_unique<AddForceNode>();
    if (type == "AddImpulse")  return std::make_unique<AddImpulseNode>();
    if (type == "SetVelocity") return std::make_unique<SetVelocityNode>();
    if (type == "VelX")        return std::make_unique<VelAxisNode>(0);
    if (type == "VelY")        return std::make_unique<VelAxisNode>(1);
    if (type == "VelZ")        return std::make_unique<VelAxisNode>(2);
    if (type == "Raycast")     return std::make_unique<RaycastNode>();
    {   // 3D physics queries (arg = optional layer list) and contact details.
        std::string layers;   // "Default, Enemy" may arrive split into tokens
        for (const auto& a : args) { if (!layers.empty()) layers += ' '; layers += a; }
        if (type == "Raycast3D")       return std::make_unique<Raycast3DNode>(layers);
        if (type == "SphereCast3D")    return std::make_unique<SphereCast3DNode>(layers);
        if (type == "RaycastAll3D")    return std::make_unique<RaycastAll3DNode>(layers);
        if (type == "OverlapSphere3D") return std::make_unique<Overlap3DNode>(false, layers);
        if (type == "OverlapBox3D")    return std::make_unique<Overlap3DNode>(true, layers);
        if (type == "ContactPoint")    return std::make_unique<ContactInfoNode>(ContactInfoNode::What::Point);
        if (type == "ContactNormal")   return std::make_unique<ContactInfoNode>(ContactInfoNode::What::Normal);
        if (type == "ImpactSpeed")     return std::make_unique<ContactInfoNode>(ContactInfoNode::What::Speed);
    }
    if (type == "EaseIn")      return std::make_unique<EaseNode>(EaseNode::Mode::In);
    if (type == "EaseOut")     return std::make_unique<EaseNode>(EaseNode::Mode::Out);
    if (type == "EaseInOut")   return std::make_unique<EaseNode>(EaseNode::Mode::InOut);
    // ---- Second wave: math, vectors, text, objects, mouse, actions ----
    using MF = MathFn2Node::Op;
    if (type == "Tan")        return std::make_unique<MathFn2Node>(MF::Tan);
    if (type == "Atan2")      return std::make_unique<MathFn2Node>(MF::Atan2);
    if (type == "Exp")        return std::make_unique<MathFn2Node>(MF::Exp);
    if (type == "Log")        return std::make_unique<MathFn2Node>(MF::Log);
    if (type == "PingPong")   return std::make_unique<MathFn2Node>(MF::PingPong);
    if (type == "Repeat01")   return std::make_unique<MathFn2Node>(MF::Repeat);
    if (type == "DeltaAngle") return std::make_unique<MathFn2Node>(MF::DeltaAngle);
    if (type == "InverseLerp") return std::make_unique<InverseLerpNode>();
    if (type == "Remap")       return std::make_unique<RemapNode>();
    if (type == "SmoothStep")  return std::make_unique<SmoothStepNode>();
    if (type == "MoveTowardsValue") return std::make_unique<MoveTowardsValueNode>();
    using VO = Vec3OpNode::Op;
    if (type == "Vec3Add")       return std::make_unique<Vec3OpNode>(VO::Add);
    if (type == "Vec3Sub")       return std::make_unique<Vec3OpNode>(VO::Sub);
    if (type == "Vec3Scale")     return std::make_unique<Vec3OpNode>(VO::Scale);
    if (type == "Vec3Length")    return std::make_unique<Vec3OpNode>(VO::Length);
    if (type == "Vec3Normalize") return std::make_unique<Vec3OpNode>(VO::Normalize);
    if (type == "Vec3Dot")       return std::make_unique<Vec3OpNode>(VO::Dot);
    if (type == "Vec3Distance")  return std::make_unique<Vec3OpNode>(VO::Distance);
    using TO = TextOpNode::Op;
    if (type == "ToString")   return std::make_unique<TextOpNode>(TO::ToString);
    if (type == "TextLength") return std::make_unique<TextOpNode>(TO::Length);
    if (type == "Contains")   return std::make_unique<TextOpNode>(TO::Contains);
    if (type == "Upper")      return std::make_unique<TextOpNode>(TO::Upper);
    if (type == "Lower")      return std::make_unique<TextOpNode>(TO::Lower);
    if (type == "ToFixed")    return std::make_unique<ToFixedNode>(args.empty() ? 2 : (int)ParseValue(args[0]).AsFloat());
    using OQ = ObjectQueryNode::Op;
    auto argOr = [&](const char* d) { return args.empty() ? std::string(d) : args[0]; };
    if (type == "ObjectExists")   return std::make_unique<ObjectQueryNode>(OQ::Exists, argOr(""));
    if (type == "ObjectPosition") return std::make_unique<ObjectQueryNode>(OQ::Position, argOr("self"));
    if (type == "DistanceTo")     return std::make_unique<ObjectQueryNode>(OQ::DistanceTo, argOr(""));
    if (type == "SelfName")       return std::make_unique<ObjectQueryNode>(OQ::Name, "self");
    if (type == "OtherName")      return std::make_unique<OtherNameNode>();
    if (type == "AnimEventName")  return std::make_unique<AnimEventNameNode>();
    if (type == "OtherTag")       return std::make_unique<OtherTagNode>();
    if (type == "FunctionStart" || type == "Return" || type == "CallFunction") {
        std::string text;   // the signature / function name may contain spaces
        for (const auto& a : args) { if (!text.empty()) text += ' '; text += a; }
        if (type == "FunctionStart") return std::make_unique<FunctionStartNode>(text);
        if (type == "Return")        return std::make_unique<ReturnNode>(FunctionNameOf(text));
        return std::make_unique<CallFunctionNode>(FunctionNameOf(text));
    }
    if (type == "SetLocal")       return std::make_unique<SetLocalNode>(argOr("x"));
    // Loops, lists, scene / global variables (NodesLoops.hpp).
    if (type == "While")          return std::make_unique<WhileNode>();
    if (type == "ForEach")        return std::make_unique<ForEachNode>();
    if (type == "Break")          return std::make_unique<LoopJumpNode>(true);
    if (type == "Continue")       return std::make_unique<LoopJumpNode>(false);
    if (type == "NewList")        return std::make_unique<NewListNode>();
    if (type == "ListAdd")        return std::make_unique<ListAddNode>();
    if (type == "ListRemoveAt")   return std::make_unique<ListRemoveAtNode>();
    if (type == "ListGet")        return std::make_unique<ListGetNode>();
    if (type == "ListLength")     return std::make_unique<ListLengthNode>();
    if (type == "ListContains")   return std::make_unique<ListContainsNode>();
    if (type == "AllWithTag")     return std::make_unique<AllWithTagNode>(argOr("Enemy"));
    {
        using S = ScopedVarNode::Scope; using O = ScopedVarNode::Op;
        if (type == "GetSceneVar")  return std::make_unique<ScopedVarNode>(S::Scene, O::Get, argOr("x"));
        if (type == "SetSceneVar")  return std::make_unique<ScopedVarNode>(S::Scene, O::Set, argOr("x"));
        if (type == "AddSceneVar")  return std::make_unique<ScopedVarNode>(S::Scene, O::Add, argOr("x"));
        if (type == "GetGlobalVar") return std::make_unique<ScopedVarNode>(S::Global, O::Get, argOr("x"));
        if (type == "SetGlobalVar") return std::make_unique<ScopedVarNode>(S::Global, O::Set, argOr("x"));
        if (type == "AddGlobalVar") return std::make_unique<ScopedVarNode>(S::Global, O::Add, argOr("x"));
    }
    {
        using S = VarChangedEventNode::Scope;
        if (type == "OnVarChanged")       return std::make_unique<VarChangedEventNode>(S::Object, argOr("x"));
        if (type == "OnSceneVarChanged")  return std::make_unique<VarChangedEventNode>(S::Scene, argOr("x"));
        if (type == "OnGlobalVarChanged") return std::make_unique<VarChangedEventNode>(S::Global, argOr("x"));
    }
    if (type == "GetLocal")       return std::make_unique<GetLocalNode>(argOr("x"));
    if (type == "Self")           return std::make_unique<SelfNode>();
    if (type == "Other")          return std::make_unique<OtherNode>();
    if (type == "FindObject")     return std::make_unique<FindObjectNode>(argOr(""));
    if (type == "GetName")        return std::make_unique<ObjectInfoNode>(false);
    if (type == "GetTag")         return std::make_unique<ObjectInfoNode>(true);
    if (type == "MouseButton" || type == "MouseButtonDown") {
        // 0/1/2 or left/right/middle (what the editor's button picker writes is 0/1/2).
        int b = 0;
        if (!args.empty()) {
            std::string a = args[0];
            for (auto& ch : a) ch = (char)std::tolower((unsigned char)ch);
            b = a == "left" ? 0 : a == "right" ? 1 : (a == "middle" || a == "wheel") ? 2 : (int)ParseValue(a).AsFloat();
        }
        return std::make_unique<MouseButtonNode>(b, type == "MouseButtonDown");
    }
    if (type == "LookAt")          return std::make_unique<LookAtNode>(argOr(""));
    if (type == "MoveTowards")     return std::make_unique<MoveTowardsObjectNode>(argOr(""));
    if (type == "SetActive")       return std::make_unique<SetActiveNode>(argOr("self"));
    if (type == "SetText")         return std::make_unique<SetTextNode>(argOr("self"));
    if (type == "LoadScene")       return std::make_unique<LoadSceneNode>(argOr(""));
    if (type == "Cooldown")        return std::make_unique<CooldownNode>(args.empty() ? 1.0f : ParseValue(args[0]).AsFloat());
    if (type == "AnimTrigger")     return std::make_unique<AnimParamNode>(AnimParamNode::Op::Trigger, argOr(""));
    if (type == "AnimSetFloat")    return std::make_unique<AnimParamNode>(AnimParamNode::Op::SetFloat, argOr(""));
    if (type == "AnimSetBool")     return std::make_unique<AnimParamNode>(AnimParamNode::Op::SetBool, argOr(""));
    if (type == "AnimState")       return std::make_unique<AnimStateNode>();
    if (type == "IkLook")          return std::make_unique<IkLookNode>();
    if (type == "IkHand") {
        std::string side = argOr("right");
        return std::make_unique<IkHandNode>(!side.empty() && (side[0] == 'l' || side[0] == 'L'));
    }
    // ---- Third wave: loops, switch, 3D transform, colour, clips, shapes ----
    if (type == "ForLoop")    return std::make_unique<ForLoopNode>();
    if (type == "Gate")       return std::make_unique<GateNode>();
    if (type == "DoN")        return std::make_unique<DoNNode>(args.empty() ? 1 : (int)ParseValue(args[0]).AsFloat());
    if (type == "SwitchInt")  return std::make_unique<SwitchIntNode>();
    if (type == "RandomInt")  return std::make_unique<RandomIntNode>();
    using M3 = MathFn3Node::Op;
    if (type == "Ceil")       return std::make_unique<MathFn3Node>(M3::Ceil);
    if (type == "Asin")       return std::make_unique<MathFn3Node>(M3::Asin);
    if (type == "Acos")       return std::make_unique<MathFn3Node>(M3::Acos);
    if (type == "Deg2Rad")    return std::make_unique<MathFn3Node>(M3::Deg2Rad);
    if (type == "Rad2Deg")    return std::make_unique<MathFn3Node>(M3::Rad2Deg);
    if (type == "Frac")       return std::make_unique<MathFn3Node>(M3::Frac);
    if (type == "OneMinus")   return std::make_unique<MathFn3Node>(M3::OneMinus);
    if (type == "Saturate")   return std::make_unique<MathFn3Node>(M3::Saturate);
    using V3 = Vec3MoreNode::Op;
    if (type == "Vec3Cross")  return std::make_unique<Vec3MoreNode>(V3::Cross);
    if (type == "Vec3Lerp")   return std::make_unique<Vec3MoreNode>(V3::Lerp);
    if (type == "Vec3Negate") return std::make_unique<Vec3MoreNode>(V3::Negate);
    if (type == "UpVector")      return std::make_unique<Vec3MoreNode>(V3::Up);
    if (type == "ForwardVector") return std::make_unique<Vec3MoreNode>(V3::Forward);
    if (type == "RightVector")   return std::make_unique<Vec3MoreNode>(V3::Right);
    if (type == "GetScale")    return std::make_unique<TransformReadNode>(false);
    if (type == "GetRotation") return std::make_unique<TransformReadNode>(true);
    using T3 = Transform3Node::Op;
    if (type == "SetPositionVec") return std::make_unique<Transform3Node>(T3::SetPosition);
    if (type == "MoveBy")         return std::make_unique<Transform3Node>(T3::MoveBy);
    if (type == "Rotate3")        return std::make_unique<Transform3Node>(T3::Rotate);
    if (type == "SetRotation3")   return std::make_unique<Transform3Node>(T3::SetRotation);
    if (type == "SetColor")       return std::make_unique<SetColorNode>();
    if (type == "PlayClip")       return std::make_unique<PlayClipNode>(argOr(""));
    if (type == "SetTimeScale")   return std::make_unique<SetTimeScaleNode>();
    if (type == "SetBlendShape")  return std::make_unique<BlendShapeNode>(argOr(""), true);
    if (type == "GetBlendShape")  return std::make_unique<BlendShapeNode>(argOr(""), false);
    if (type == "MouseWheel")  return std::make_unique<EngineReadNode>(EngineReadNode::Op::MouseWheel);
    if (type == "TimeScale")   return std::make_unique<EngineReadNode>(EngineReadNode::Op::TimeScale);
    if (type == "FrameCount")  return std::make_unique<EngineReadNode>(EngineReadNode::Op::FrameCount);
    if (err) *err = type.find('.') != std::string::npos
                        ? "unknown node type '" + type + "': there's no engine binding by that name"
                        : "unknown node type '" + type + "'";
    return nullptr;
}

// ---- Text-format parser ----------------------------------------------
namespace {
std::string JoinArgs(const std::vector<std::string>& a) {
    std::string s;
    for (const auto& x : a) { if (!s.empty()) s += ' '; s += x; }
    return s;
}
// Split a line into tokens, keeping "quoted strings" intact.
std::vector<std::string> Tokenize(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool inQuotes = false;
    for (char c : line) {
        if (c == '"') { inQuotes = !inQuotes; cur += c; }
        else if (std::isspace((unsigned char)c) && !inQuotes) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
} // namespace

std::unique_ptr<NodeGraph> NodeGraph::Parse(const std::string& text, std::string* error) {
    auto graph = std::make_unique<NodeGraph>();
    std::unordered_map<int, int> fileToGraph; // file id -> graph id

    struct PendingExec { int from, pin, to; };
    struct PendingData { int consumer, inPin, producer, outPin; };
    std::vector<PendingExec> execs;
    std::vector<PendingData> datas;
    struct PendingEntry { std::string name; int fileId; };
    std::vector<PendingEntry> entries;
    struct PendingDefault { int node, pin; VsValue v; };
    std::vector<PendingDefault> defaults;

    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    auto fail = [&](const std::string& m) {
        if (error) *error = "line " + std::to_string(lineNo) + ": " + m;
        return std::unique_ptr<NodeGraph>{};
    };

    while (std::getline(in, line)) {
        ++lineNo;
        // Strip comments starting with '#' (a '#' inside "quotes" is text: Format "#{0}").
        {
            bool q = false;
            for (std::size_t i = 0; i < line.size(); ++i) {
                if (line[i] == '"') q = !q;
                else if (line[i] == '#' && !q) { line.erase(i); break; }
            }
        }
        auto tok = Tokenize(line);
        if (tok.empty()) continue;

        const std::string& kind = tok[0];
        if (kind == "node") {
            if (tok.size() < 3) return fail("node needs <id> <type>");
            int fileId = std::stoi(tok[1]);
            std::string type = tok[2];
            std::vector<std::string> args(tok.begin() + 3, tok.end());
            // "quoted args" (names with spaces): Const keeps the quotes (they mark
            // a string value), every other node takes the bare text.
            if (type != "Const")
                for (auto& a : args)
                    if (a.size() >= 2 && a.front() == '"' && a.back() == '"') a = a.substr(1, a.size() - 2);
            std::string e;
            auto node = MakeNode(type, args, &e);
            if (!node) return fail(e);
            VsNode* added = graph->AddNode(std::move(node));
            added->fileId = fileId;
            added->type = type;
            added->targetPin = TargetPinFor(type);
            if (type == "FunctionStart") {
                const std::string fname = FunctionNameOf(args.empty() ? std::string() : JoinArgs(args));
                if (!fname.empty() && !graph->m_functions.count(fname)) graph->m_functions[fname] = added->id;
            }
            fileToGraph[fileId] = added->id;
        } else if (kind == "exec") {
            if (tok.size() < 4) return fail("exec needs <from> <pin> <to>");
            execs.push_back({std::stoi(tok[1]), std::stoi(tok[2]), std::stoi(tok[3])});
        } else if (kind == "data") {
            if (tok.size() < 5) return fail("data needs <consumer> <inPin> <producer> <outPin>");
            datas.push_back({std::stoi(tok[1]), std::stoi(tok[2]),
                             std::stoi(tok[3]), std::stoi(tok[4])});
        } else if (kind == "entry") {
            if (tok.size() < 3) return fail("entry needs <name> <id>");
            entries.push_back({tok[1], std::stoi(tok[2])});
        } else if (kind == "default") {
            // default <node> <inPin> <value>: an unconnected input's typed value.
            if (tok.size() < 4) return fail("default needs <node> <pin> <value>");
            defaults.push_back({std::stoi(tok[1]), std::stoi(tok[2]), ParseValue(tok[3])});
        } else if (kind == "pos" || kind == "comment") {
            // Editor layout (node positions, comment frames): nothing to run.
        } else {
            // A directive from a newer editor: skip it rather than refusing the
            // whole graph.
            OKAY_WARN("VisualScript: ignoring unknown line ", lineNo, " ('", kind, "')");
        }
    }

    auto resolve = [&](int fileId, bool& ok) {
        auto it = fileToGraph.find(fileId);
        ok = it != fileToGraph.end();
        return ok ? it->second : -1;
    };
    bool ok = true;
    for (auto& e : execs) {
        int f = resolve(e.from, ok); int t = resolve(e.to, ok);
        if (!ok) return fail("exec references unknown node");
        graph->ConnectExec(f, e.pin, t);
    }
    for (auto& d : datas) {
        int c = resolve(d.consumer, ok); int p = resolve(d.producer, ok);
        if (!ok) return fail("data references unknown node");
        graph->ConnectData(c, d.inPin, p, d.outPin);
    }
    for (auto& d : defaults) {
        int n = resolve(d.node, ok);
        if (!ok) return fail("default references unknown node");
        if (VsNode* node = graph->Node(n)) node->SetDefault(d.pin, d.v);
    }
    for (auto& en : entries) {
        int n = resolve(en.fileId, ok);
        if (!ok) return fail("entry references unknown node");
        graph->SetEntry(en.name, n);
    }
    return graph;
}

} // namespace okay::vs
