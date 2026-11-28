#pragma once
// Automation link: how `okay mcp` (an MCP server an agent starts over stdio) reaches a RUNNING editor, so the agent
// drives the scene, selection, undo history and Play the person sees. Off unless the person turns it on in the editor
// (View > Automation Link).
//
// Safety, by design:
//   - The server listens on 127.0.0.1 only (never another interface), on a port the system picks.
//   - Every connection must first send the session's token: 32 random bytes from the operating system, made fresh each
//     time the link starts, written with the port to a file only this user can read (LinkInfoPath(); 0600 on POSIX, the
//     per-user profile folder on Windows). A wrong or missing token closes the connection; the comparison takes the same
//     time whatever the token.
//   - The file is deleted when the link stops. Few connections at once, lines capped in size.
// Nothing here blocks the editor: LinkServer::Poll() accepts, reads and answers what is ready, once a frame, on the
// editor's main thread (so commands run between frames, with no locks). No <thread> or <mutex> (CLAUDE.md rule 21a).
//
// Wire format, written for this engine: one JSON text per line (UTF-8, '\n'). The client's first line is
// {"okay_link":1,"token":"<hex>"}; the server answers {"okay_link":1,"ok":true} and from then on every line is a
// JSON-RPC message for the editor's McpServer, answered with one line (none for a notification).
#include <functional>
#include <memory>
#include <string>

namespace okay::automation {

/// Where a running editor writes its link details: OKAY_AUTOMATION_LINK_FILE when set (tests), else
/// <per-user config folder>/automation/editor_link.json.
std::string LinkInfoPath();

struct LinkInfo {
    int port = 0;
    std::string token;
    std::string project;
    long long pid = 0;
};
/// Read the link file. False, with `error` in plain words, when it is missing or damaged.
bool ReadLinkInfo(const std::string& path, LinkInfo& out, std::string* error = nullptr);

class LinkServer {
public:
    /// `connection` numbers a client from 1; the answer to `line` is sent back unless it is empty.
    using Answer = std::function<std::string(int connection, const std::string& line)>;

    LinkServer();
    ~LinkServer();
    LinkServer(const LinkServer&) = delete;
    LinkServer& operator=(const LinkServer&) = delete;

    /// Listen on 127.0.0.1 (a free port), make a token and write the link file at `infoPath`. False with `error`.
    bool Start(const std::string& infoPath, const std::string& project, std::string* error = nullptr);
    /// Close every connection and delete the link file.
    void Stop();
    bool Running() const;
    int Port() const;
    const std::string& Token() const;
    const std::string& InfoPath() const;
    /// Accept, read and answer what is ready; never waits. Returns how many lines were answered.
    int Poll(const Answer& answer);
    /// Clients connected and past the token.
    int Clients() const;
    /// Told when a client's connection closes (its per-connection state can go).
    std::function<void(int connection)> onClose;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

/// The other end, for okay mcp's bridge and the tests: one blocking connection to a LinkServer.
class LinkClient {
public:
    LinkClient();
    ~LinkClient();
    LinkClient(const LinkClient&) = delete;
    LinkClient& operator=(const LinkClient&) = delete;

    /// Connect to 127.0.0.1:`port` and pass the token. `pump`, when set, is called while waiting for the answer (a test
    /// that runs the server in the same thread polls it there). False with `error`.
    bool Connect(int port, const std::string& token, std::string* error = nullptr, int timeoutMs = 3000,
                 const std::function<void()>& pump = {});
    bool Connected() const;
    /// Send one line (a '\n' is added). False when the connection is gone.
    bool Send(const std::string& line);
    /// Wait up to `timeoutMs` (-1 = no limit) for one line, calling `pump` between waits. False on a timeout or a closed
    /// connection (`closed` says which).
    bool Receive(std::string& line, int timeoutMs = -1, const std::function<void()>& pump = {}, bool* closed = nullptr);
    void Close();

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

} // namespace okay::automation
