#pragma once
// Automation over the Model Context Protocol: the automation command table (Automation.hpp) served as MCP tools, so an
// AI agent (or any MCP client) can list and run every command. Transport-free: Handle() takes one JSON-RPC message and
// gives the answer; ServeStdio() is the stdio transport (one message per line, as MCP's stdio transport says), which
// `okay mcp` runs. The running editor answers the same messages on its local automation link (editor/AutomationEditor).
//
// Written from the public specifications only: JSON-RPC 2.0 (request, notification, response, error object, batch) and
// the Model Context Protocol (lifecycle: initialize / notifications/initialized / ping; tools: tools/list, tools/call with
// text, image and structured content, isError for a tool's own failure). No MCP SDK and no JSON-RPC library: the JSON is
// okay::json.
#include "okay/Automation/Automation.hpp"

#include <iosfwd>
#include <string>

namespace okay::automation {

struct McpOptions {
    std::string serverName = "okay-engine";
    std::string serverVersion;   ///< the engine version ("" = "dev")
    std::string instructions;    ///< shown to the agent at initialize ("" = a short default)
};

class McpServer {
public:
    McpServer(const Registry& registry, Host& host, McpOptions options = {});

    /// One JSON-RPC message (a request, a notification or a batch). Returns the answer as one line of JSON text, or ""
    /// when nothing is to be answered (a notification). Never throws.
    std::string Handle(const std::string& message);

    /// True once the client sent initialize.
    bool Initialized() const { return m_initialized; }
    /// The protocol version agreed at initialize.
    const std::string& ProtocolVersion() const { return m_version; }

    /// The MCP tool name of a command: "scene.create_object" -> "scene_create_object" (tool names are kept to letters,
    /// digits, '_' and '-', which every MCP client accepts). Registry::Add refuses a name whose tool name another command
    /// already has, so the mapping is one to one.
    static std::string ToolName(const std::string& command);
    /// The command a tool name stands for (null when none).
    const Command* FindTool(const std::string& tool) const;

    /// The protocol versions this server speaks, newest first.
    static const char* const* SupportedVersions(int* count);

private:
    json::Value HandleOne(const json::Value& msg, bool& reply);
    json::Value ToolsList() const;
    json::Value ToolsCall(const json::Value& params, std::string& rpcError, int& rpcCode) const;

    const Registry& m_registry;
    Host& m_host;
    McpOptions m_opt;
    bool m_initialized = false;
    std::string m_version;
};

/// Serve `server` over a stdio-style pair of streams: read one message per line from `in` until it ends, write each
/// answer as one line to `out` (flushed). Lines that are only white space are skipped. Returns 0.
int ServeStdio(McpServer& server, std::istream& in, std::ostream& out);

} // namespace okay::automation
