// Automation over MCP (okay/Automation/Mcp.hpp). Written from the JSON-RPC 2.0 specification (message objects, error
// codes -32700 / -32600 / -32601 / -32602 / -32603, batches) and the Model Context Protocol specification (initialize and
// version negotiation, ping, tools/list, tools/call results with content, structuredContent and isError). Our own code.
#include "okay/Automation/Mcp.hpp"

#include <exception>
#include <istream>
#include <ostream>
#include <utility>

namespace okay::automation {

namespace {

constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;
constexpr int kInternalError = -32603;

const char* const kVersions[] = {"2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"};
constexpr int kVersionCount = (int)(sizeof kVersions / sizeof kVersions[0]);

json::Value RpcError(const json::Value& id, int code, const std::string& message) {
    json::Value e = json::Value::Obj();
    e.Set("code", json::Value::Num(code));
    e.Set("message", json::Value::Str(message));
    json::Value r = json::Value::Obj();
    r.Set("jsonrpc", json::Value::Str("2.0"));
    r.Set("id", id);
    r.Set("error", std::move(e));
    return r;
}

json::Value RpcResult(const json::Value& id, json::Value result) {
    json::Value r = json::Value::Obj();
    r.Set("jsonrpc", json::Value::Str("2.0"));
    r.Set("id", id);
    r.Set("result", std::move(result));
    return r;
}

} // namespace

McpServer::McpServer(const Registry& registry, Host& host, McpOptions options)
    : m_registry(registry), m_host(host), m_opt(std::move(options)) {
    if (m_opt.serverVersion.empty()) m_opt.serverVersion = "dev";
}

const char* const* McpServer::SupportedVersions(int* count) {
    if (count) *count = kVersionCount;
    return kVersions;
}

std::string McpServer::ToolName(const std::string& command) {
    std::string n = command;
    for (char& c : n) if (c == '.') c = '_';
    return n;
}

const Command* McpServer::FindTool(const std::string& tool) const {
    for (const Command* c : m_registry.List()) if (ToolName(c->name) == tool) return c;
    return nullptr;
}

json::Value McpServer::ToolsList() const {
    json::Value tools = json::Value::Arr();
    for (const Command* c : m_registry.List()) {
        json::Value t = json::Value::Obj();
        t.Set("name", json::Value::Str(ToolName(c->name)));
        t.Set("title", json::Value::Str(c->name));
        std::string desc = c->summary;
        if (!c->result.empty()) desc += " Returns " + c->result + ".";
        if (c->flags & flag::Undoable) desc += " One undo step (undo.undo takes it back).";
        if (c->flags & flag::NeedsEditor) desc += " Needs the editor's state.";
        if (c->flags & flag::NeedsPlay) desc += " Only while playing.";
        t.Set("description", json::Value::Str(desc));
        t.Set("inputSchema", Registry::InputSchema(*c));
        json::Value ann = json::Value::Obj();
        ann.Set("title", json::Value::Str(c->name));
        ann.Set("readOnlyHint", json::Value::Boolean(!(c->flags & flag::Mutates)));
        ann.Set("destructiveHint", json::Value::Boolean((c->flags & flag::Mutates) && !(c->flags & flag::Undoable)));
        ann.Set("openWorldHint", json::Value::Boolean(false));
        t.Set("annotations", std::move(ann));
        tools.Push(std::move(t));
    }
    return json::Value::Obj().Set("tools", std::move(tools));
}

json::Value McpServer::ToolsCall(const json::Value& params, std::string& rpcError, int& rpcCode) const {
    const json::Value* name = params.Get("name");
    if (!name || !name->IsString()) { rpcCode = kInvalidParams; rpcError = "tools/call needs a tool name"; return {}; }
    const Command* c = FindTool(name->string);
    if (!c) { rpcCode = kInvalidParams; rpcError = "Unknown tool: " + name->string; return {}; }
    json::Value args;
    if (const json::Value* a = params.Get("arguments")) args = *a;
    const Result r = m_registry.Run(m_host, c->name, args);

    // The result twice, as MCP asks of a tool with structured output: as structuredContent (an object) and as text
    // for clients that only read content. A command's own failure is a tool result with isError, not a JSON-RPC error,
    // so the agent sees the message and can correct itself.
    const json::Value structured = r.ToJson();
    json::Value content = json::Value::Arr();
    content.Push(json::Value::Obj().Set("type", json::Value::Str("text")).Set("text", json::Value::Str(json::Write(structured))));
    for (const Attachment& a : r.attachments)
        content.Push(json::Value::Obj().Set("type", json::Value::Str("image"))
                         .Set("data", json::Value::Str(a.base64)).Set("mimeType", json::Value::Str(a.mimeType)));
    json::Value out = json::Value::Obj();
    out.Set("content", std::move(content));
    out.Set("structuredContent", structured);
    out.Set("isError", json::Value::Boolean(!r.ok));
    return out;
}

json::Value McpServer::HandleOne(const json::Value& msg, bool& reply) {
    reply = true;
    const json::Value kNullId;
    if (!msg.IsObject()) return RpcError(kNullId, kInvalidRequest, "A JSON-RPC message must be an object");
    const json::Value* id = msg.Get("id");
    const bool hasId = id != nullptr;
    const json::Value idv = hasId ? *id : json::Value();
    if (hasId && !(id->IsString() || id->IsNumber() || id->IsNull())) return RpcError(kNullId, kInvalidRequest, "The id must be a string or a number");
    const json::Value* ver = msg.Get("jsonrpc");
    const json::Value* method = msg.Get("method");
    if (!method) {
        // A response from the client (to a request this server never sends): nothing to answer.
        if (msg.Get("result") || msg.Get("error")) { reply = false; return {}; }
        return RpcError(idv, kInvalidRequest, "The message has no method");
    }
    if (!ver || !ver->IsString() || ver->string != "2.0") return RpcError(idv, kInvalidRequest, "jsonrpc must be \"2.0\"");
    if (!method->IsString()) return RpcError(idv, kInvalidRequest, "The method must be a string");
    const json::Value* pp = msg.Get("params");
    if (pp && !pp->IsObject() && !pp->IsArray()) return RpcError(idv, kInvalidRequest, "params must be an object or an array");
    static const json::Value kEmpty = json::Value::Obj();
    const json::Value& params = (pp && pp->IsObject()) ? *pp : kEmpty;
    const std::string& m = method->string;

    if (!hasId) {   // a notification: never answered (initialized, cancelled, progress ...)
        reply = false;
        return {};
    }
    if (m == "initialize") {
        std::string want;
        if (const json::Value* v = params.Get("protocolVersion")) if (v->IsString()) want = v->string;
        m_version = kVersions[1];   // the version most clients speak today, when the client asks for one we do not know
        for (int i = 0; i < kVersionCount; ++i) if (want == kVersions[i]) m_version = want;
        m_initialized = true;
        json::Value caps = json::Value::Obj();
        caps.Set("tools", json::Value::Obj().Set("listChanged", json::Value::Boolean(false)));
        json::Value info = json::Value::Obj();
        info.Set("name", json::Value::Str(m_opt.serverName));
        info.Set("title", json::Value::Str("OkayEngine automation"));
        info.Set("version", json::Value::Str(m_opt.serverVersion));
        json::Value res = json::Value::Obj();
        res.Set("protocolVersion", json::Value::Str(m_version));
        res.Set("capabilities", std::move(caps));
        res.Set("serverInfo", std::move(info));
        res.Set("instructions", json::Value::Str(m_opt.instructions.empty()
            ? std::string("Every engine and editor command of OkayEngine is a tool. Objects are named by their id (a GUID "
                          "from scene_hierarchy), a path such as Parent/Child, or a unique name. Edits are one undo step "
                          "each (undo_undo / undo_redo). automation_describe gives any command's full description.")
            : m_opt.instructions));
        return RpcResult(idv, std::move(res));
    }
    if (m == "ping") return RpcResult(idv, json::Value::Obj());
    if (m == "tools/list") return RpcResult(idv, ToolsList());
    if (m == "tools/call") {
        std::string err;
        int code = 0;
        json::Value r = ToolsCall(params, err, code);
        if (code) return RpcError(idv, code, err);
        return RpcResult(idv, std::move(r));
    }
    return RpcError(idv, kMethodNotFound, "Unknown method: " + m);
}

std::string McpServer::Handle(const std::string& message) {
    json::Value msg;
    std::string why;
    if (!json::Parse(message, msg, &why)) return json::Write(RpcError(json::Value(), kParseError, "Parse error: " + why));
    try {
        if (msg.IsArray()) {   // a batch: every request answered in one array; an empty batch is an invalid request
            if (msg.array.empty()) return json::Write(RpcError(json::Value(), kInvalidRequest, "An empty batch"));
            json::Value all = json::Value::Arr();
            for (const json::Value& one : msg.array) {
                bool reply = false;
                json::Value r = HandleOne(one, reply);
                if (reply) all.Push(std::move(r));
            }
            return all.array.empty() ? std::string() : json::Write(all);
        }
        bool reply = false;
        json::Value r = HandleOne(msg, reply);
        return reply ? json::Write(r) : std::string();
    } catch (const std::exception& ex) {
        const json::Value* id = msg.IsObject() ? msg.Get("id") : nullptr;
        return json::Write(RpcError(id ? *id : json::Value(), kInternalError, std::string("Internal error: ") + ex.what()));
    }
}

int ServeStdio(McpServer& server, std::istream& in, std::ostream& out) {
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        bool blank = true;
        for (char c : line) if (c != ' ' && c != '\t') { blank = false; break; }
        if (blank) continue;
        const std::string answer = server.Handle(line);
        if (!answer.empty()) { out << answer << '\n'; out.flush(); }
    }
    return 0;
}

} // namespace okay::automation
