#include "okay/VisualScript/NodeGraph.hpp"
#include "okay/VisualScript/Nodes.hpp"

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
    // Guard against cycles in data evaluation.
    if (++m_execGuard > 100000) { --m_execGuard; return {}; }
    VsNode* n = Node(nodeId);
    VsValue v = n ? n->Eval(outPin, *this, ctx) : VsValue{};
    --m_execGuard;
    return v;
}

void NodeGraph::Run(int entryNode, VsContext& ctx) {
    int cur = entryNode;
    int guard = 0;
    while (cur >= 0 && guard++ < 100000) {
        VsNode* n = Node(cur);
        if (!n) break;
        int outPin = n->Exec(*this, ctx);
        if (outPin < 0) break;
        cur = (outPin < (int)n->execOut.size()) ? n->execOut[outPin] : -1;
    }
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
                                 const std::vector<std::string>& args,
                                 std::string* err) {
    auto need = [&](size_t n) {
        if (args.size() < n) { if (err) *err = type + " needs " + std::to_string(n) + " arg(s)"; return false; }
        return true;
    };
    if (type == "OnStart" || type == "OnUpdate") return std::make_unique<EventNode>();
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
    if (type == "KeyUp")     { if (!need(1)) return nullptr; return std::make_unique<KeyUpNode>(args[0].empty() ? ' ' : args[0][0]); }
    if (type == "Select") return std::make_unique<SelectNode>();
    if (type == "Concat") return std::make_unique<ConcatNode>();
    // Vectors.
    if (type == "MakeVec3") return std::make_unique<MakeVec3Node>();
    if (type == "VecX") return std::make_unique<BreakVec3Node>(0);
    if (type == "VecY") return std::make_unique<BreakVec3Node>(1);
    if (type == "VecZ") return std::make_unique<BreakVec3Node>(2);
    // Input.
    if (type == "Key")     { if (!need(1)) return nullptr; return std::make_unique<KeyNode>(args[0].empty() ? ' ' : args[0][0]); }
    if (type == "KeyDown") { if (!need(1)) return nullptr; return std::make_unique<KeyDownNode>(args[0].empty() ? ' ' : args[0][0]); }
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
    if (type == "NetJoin")      { std::string h = args.empty() ? "127.0.0.1" : args[0]; int p = args.size() < 2 ? 45000 : (int)ParseValue(args[1]).AsFloat(); return std::make_unique<NetJoinNode>(h, p); }
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
    if (type == "Raycast")     return std::make_unique<RaycastNode>();
    if (type == "EaseIn")      return std::make_unique<EaseNode>(EaseNode::Mode::In);
    if (type == "EaseOut")     return std::make_unique<EaseNode>(EaseNode::Mode::Out);
    if (type == "EaseInOut")   return std::make_unique<EaseNode>(EaseNode::Mode::InOut);
    if (err) *err = "unknown node type '" + type + "'";
    return nullptr;
}

// ---- Text-format parser ----------------------------------------------
namespace {
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

    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    auto fail = [&](const std::string& m) {
        if (error) *error = "line " + std::to_string(lineNo) + ": " + m;
        return std::unique_ptr<NodeGraph>{};
    };

    while (std::getline(in, line)) {
        ++lineNo;
        // Strip comments starting with '#'.
        if (auto h = line.find('#'); h != std::string::npos) line.erase(h);
        auto tok = Tokenize(line);
        if (tok.empty()) continue;

        const std::string& kind = tok[0];
        if (kind == "node") {
            if (tok.size() < 3) return fail("node needs <id> <type>");
            int fileId = std::stoi(tok[1]);
            std::string type = tok[2];
            std::vector<std::string> args(tok.begin() + 3, tok.end());
            std::string e;
            auto node = MakeNode(type, args, &e);
            if (!node) return fail(e);
            VsNode* added = graph->AddNode(std::move(node));
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
        } else {
            return fail("unknown directive '" + kind + "'");
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
    for (auto& en : entries) {
        int n = resolve(en.fileId, ok);
        if (!ok) return fail("entry references unknown node");
        graph->SetEntry(en.name, n);
    }
    return graph;
}

} // namespace okay::vs
