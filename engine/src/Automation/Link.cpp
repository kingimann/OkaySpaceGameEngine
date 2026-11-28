// Automation link (okay/Automation/Link.hpp): a loopback TCP line link with a per-session token. Written for this
// engine from the BSD sockets interface as POSIX and Winsock 2 document it (socket, bind, listen, accept, recv, send,
// select, non-blocking mode); the token comes from the operating system's random source (okay::account::detail::
// OsRandom). No code from another project.
#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#else
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
#  include <cerrno>
#endif

#include "okay/Automation/Link.hpp"

#include "okay/Core/Json.hpp"
#include "okay/Core/Paths.hpp"
#include "okay/Net/UdpSocket.hpp"                   // okay::net::Startup: WSAStartup on Windows
#include "okay/Platform/Account/AccountDetail.hpp"  // OsRandom, ToHex: the operating system's random bytes

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

namespace okay::automation {

namespace {

#if defined(_WIN32)
using Sock = SOCKET;
const Sock kNone = INVALID_SOCKET;
void CloseIt(Sock s) { closesocket(s); }
bool NonBlocking(Sock s) { u_long on = 1; return ioctlsocket(s, FIONBIO, &on) == 0; }
bool WouldBlock() { const int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
int SendFlags() { return 0; }
long long Pid() { return (long long)GetCurrentProcessId(); }
#else
using Sock = int;
const Sock kNone = -1;
void CloseIt(Sock s) { ::close(s); }
bool NonBlocking(Sock s) { const int f = fcntl(s, F_GETFL, 0); return f >= 0 && fcntl(s, F_SETFL, f | O_NONBLOCK) == 0; }
bool WouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EINTR; }
#  if defined(MSG_NOSIGNAL)
int SendFlags() { return MSG_NOSIGNAL; }   // a closed peer must not kill the editor with SIGPIPE
#  else
int SendFlags() { return 0; }
#  endif
long long Pid() { return (long long)getpid(); }
#endif

constexpr std::size_t kMaxLine = 16u << 20;   // one message at most 16 MB (a screenshot inline is far less)
constexpr int kMaxClients = 4;

/// Same length and same bytes, in a time that does not depend on where they first differ.
bool SameToken(const std::string& a, const std::string& b) {
    if (a.size() != b.size() || a.empty()) return false;
    unsigned char d = 0;
    for (std::size_t i = 0; i < a.size(); ++i) d |= (unsigned char)(a[i] ^ b[i]);
    return d == 0;
}

/// Send all of `data`, waiting (select) while the socket's buffer is full. False when the peer is gone.
bool SendAll(Sock s, const std::string& data) {
    std::size_t at = 0;
    while (at < data.size()) {
        const int n = (int)::send(s, data.data() + at, (int)std::min<std::size_t>(data.size() - at, 1u << 20), SendFlags());
        if (n > 0) { at += (std::size_t)n; continue; }
        if (n < 0 && WouldBlock()) {
            fd_set w;
            FD_ZERO(&w);
            FD_SET(s, &w);
            timeval tv{1, 0};
            if (::select((int)s + 1, nullptr, &w, nullptr, &tv) < 0) return false;
            continue;
        }
        return false;
    }
    return true;
}

/// Take every complete line out of `buf` (without its '\n' or a '\r' before it).
std::vector<std::string> TakeLines(std::string& buf) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (std::size_t nl; (nl = buf.find('\n', start)) != std::string::npos; start = nl + 1) {
        std::string line = buf.substr(start, nl - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(std::move(line));
    }
    buf.erase(0, start);
    return out;
}

bool WriteUserOnly(const std::string& path, const std::string& text, std::string* error) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(path).parent_path(), ec);
    const std::string tmp = path + ".tmp";
#if defined(_WIN32)
    {
        std::ofstream f(std::filesystem::u8path(tmp), std::ios::binary | std::ios::trunc);
        f << text;
        if (!f) { if (error) *error = "The link file could not be written: " + path; return false; }
    }
#else
    ::unlink(tmp.c_str());
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_TRUNC, 0600);   // this user only, from the start
    if (fd < 0) { if (error) *error = "The link file could not be written: " + path; return false; }
    const bool ok = ::write(fd, text.data(), text.size()) == (ssize_t)text.size();
    ::close(fd);
    if (!ok) { ::unlink(tmp.c_str()); if (error) *error = "The link file could not be written: " + path; return false; }
#endif
    std::filesystem::rename(std::filesystem::u8path(tmp), std::filesystem::u8path(path), ec);
    if (ec) {
        std::filesystem::remove(std::filesystem::u8path(path), ec);
        ec.clear();
        std::filesystem::rename(std::filesystem::u8path(tmp), std::filesystem::u8path(path), ec);
    }
    if (ec) { if (error) *error = "The link file could not be put in place: " + path; return false; }
    return true;
}

const char* kHelloOk = "{\"okay_link\":1,\"ok\":true}";
const char* kHelloNo = "{\"okay_link\":1,\"ok\":false,\"error\":\"wrong token\"}";

} // namespace

std::string LinkInfoPath() {
    if (const char* e = std::getenv("OKAY_AUTOMATION_LINK_FILE")) if (*e) return e;
    return Paths::Join(Paths::Join(Paths::ConfigDir(), "automation"), "editor_link.json");
}

bool ReadLinkInfo(const std::string& path, LinkInfo& out, std::string* error) {
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) { if (error) *error = "No editor has its automation link on (there is no " + path + ")."; return false; }
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    json::Value v;
    std::string why;
    if (!json::Parse(text, v, &why)) { if (error) *error = "The link file is damaged: " + why; return false; }
    const json::Value* port = v.Get("port");
    const json::Value* token = v.Get("token");
    std::uint64_t p = 0;
    if (!port || !port->AsIndex(p) || p == 0 || p > 65535 || !token || !token->IsString() || token->string.size() < 32) {
        if (error) *error = "The link file has no usable port and token.";
        return false;
    }
    out.port = (int)p;
    out.token = token->string;
    const json::Value* proj = v.Get("project");
    out.project = proj && proj->IsString() ? proj->string : std::string();
    std::uint64_t pid = 0;
    const json::Value* pv = v.Get("pid");
    out.pid = pv && pv->AsIndex(pid) ? (long long)pid : 0;
    return true;
}

// ---- LinkServer ------------------------------------------------------------------------------------------------------

struct LinkServer::Impl {
    struct Client { Sock s = kNone; int id = 0; bool passed = false; std::string in; };
    Sock listen = kNone;
    int port = 0;
    std::string token, info;
    std::vector<Client> clients;
    int nextId = 1;
    bool netUp = false;
};

LinkServer::LinkServer() : m(new Impl) {}
LinkServer::~LinkServer() { Stop(); }

bool LinkServer::Start(const std::string& infoPath, const std::string& project, std::string* error) {
    Stop();
    auto fail = [&](const std::string& why) { if (error) *error = why; Stop(); return false; };
    if (!okay::net::Startup()) return fail("The network could not be started on this computer.");
    m->netUp = true;
    std::uint8_t raw[32];
    if (!okay::account::detail::OsRandom(raw, sizeof raw)) return fail("This computer gave no random numbers for the link's token.");
    m->token = okay::account::detail::ToHex(raw, sizeof raw);
    const Sock s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kNone) return fail("A network socket could not be made.");
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // this computer only
    a.sin_port = 0;                                // the system picks a free port
    if (::bind(s, (const sockaddr*)&a, sizeof a) != 0 || ::listen(s, 4) != 0 || !NonBlocking(s)) {
        CloseIt(s);
        return fail("The automation link could not listen on this computer.");
    }
    sockaddr_in got{};
    socklen_t gl = sizeof got;
    ::getsockname(s, (sockaddr*)&got, &gl);
    m->listen = s;
    m->port = ntohs(got.sin_port);
    json::Value v = json::Value::Obj();
    v.Set("okay_link", json::Value::Num(1));
    v.Set("port", json::Value::Num(m->port));
    v.Set("token", json::Value::Str(m->token));
    v.Set("pid", json::Value::Num((double)Pid()));
    v.Set("project", json::Value::Str(project));
    std::string why;
    if (!WriteUserOnly(infoPath, json::Write(v) + "\n", &why)) return fail(why);
    m->info = infoPath;
    return true;
}

void LinkServer::Stop() {
    for (auto& c : m->clients) {
        CloseIt(c.s);
        if (onClose) onClose(c.id);
    }
    m->clients.clear();
    if (m->listen != kNone) { CloseIt(m->listen); m->listen = kNone; }
    if (!m->info.empty()) {
        std::error_code ec;
        std::filesystem::remove(std::filesystem::u8path(m->info), ec);
        m->info.clear();
    }
    m->port = 0;
    m->token.clear();
    if (m->netUp) { okay::net::Shutdown(); m->netUp = false; }
}

bool LinkServer::Running() const { return m->listen != kNone; }
int LinkServer::Port() const { return m->port; }
const std::string& LinkServer::Token() const { return m->token; }
const std::string& LinkServer::InfoPath() const { return m->info; }
int LinkServer::Clients() const {
    int n = 0;
    for (const auto& c : m->clients) if (c.passed) ++n;
    return n;
}

int LinkServer::Poll(const Answer& answer) {
    if (m->listen == kNone) return 0;
    for (;;) {   // new connections
        const Sock s = ::accept(m->listen, nullptr, nullptr);
        if (s == kNone) break;
        if ((int)m->clients.size() >= kMaxClients || !NonBlocking(s)) { CloseIt(s); continue; }
        Impl::Client c;
        c.s = s;
        c.id = m->nextId++;
        m->clients.push_back(std::move(c));
    }
    int answered = 0;
    for (std::size_t i = 0; i < m->clients.size();) {
        Impl::Client& c = m->clients[i];
        bool drop = false;
        char buf[65536];
        for (;;) {
            const int n = (int)::recv(c.s, buf, (int)sizeof buf, 0);
            if (n > 0) { c.in.append(buf, (std::size_t)n); if (c.in.size() > kMaxLine) { drop = true; break; } continue; }
            if (n < 0 && WouldBlock()) break;
            drop = true;   // closed by the other end, or an error
            break;
        }
        for (const std::string& line : TakeLines(c.in)) {
            if (drop) break;
            if (!c.passed) {
                json::Value hello;
                const json::Value* tok = nullptr;
                if (json::Parse(line, hello) && (tok = hello.Get("token")) && tok->IsString() && SameToken(tok->string, m->token)) {
                    c.passed = true;
                    SendAll(c.s, std::string(kHelloOk) + "\n");
                } else {
                    SendAll(c.s, std::string(kHelloNo) + "\n");
                    drop = true;
                }
                continue;
            }
            const std::string reply = answer ? answer(c.id, line) : std::string();
            ++answered;
            if (!reply.empty() && !SendAll(c.s, reply + "\n")) drop = true;
        }
        if (drop) {
            CloseIt(c.s);
            const int id = c.id;
            m->clients.erase(m->clients.begin() + (std::ptrdiff_t)i);
            if (onClose) onClose(id);
            continue;
        }
        ++i;
    }
    return answered;
}

// ---- LinkClient ------------------------------------------------------------------------------------------------------

struct LinkClient::Impl {
    Sock s = kNone;
    std::string in;
    bool netUp = false;
};

LinkClient::LinkClient() : m(new Impl) {}
LinkClient::~LinkClient() { Close(); }

bool LinkClient::Connected() const { return m->s != kNone; }

void LinkClient::Close() {
    if (m->s != kNone) { CloseIt(m->s); m->s = kNone; }
    m->in.clear();
    if (m->netUp) { okay::net::Shutdown(); m->netUp = false; }
}

bool LinkClient::Connect(int port, const std::string& token, std::string* error, int timeoutMs, const std::function<void()>& pump) {
    Close();
    auto fail = [&](const std::string& why) { if (error) *error = why; Close(); return false; };
    if (!okay::net::Startup()) return fail("The network could not be started on this computer.");
    m->netUp = true;
    m->s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m->s == kNone) return fail("A network socket could not be made.");
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((unsigned short)port);
    NonBlocking(m->s);
    if (::connect(m->s, (const sockaddr*)&a, sizeof a) != 0 && !WouldBlock()) return fail("The editor's automation link did not answer on port " + std::to_string(port) + ".");
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs < 0 ? 3000 : timeoutMs);
    for (;;) {   // wait until the connection is made (the server may be polled by `pump` on this very thread)
        fd_set w;
        FD_ZERO(&w);
        FD_SET(m->s, &w);
        timeval tv{0, 10000};
        const int r = ::select((int)m->s + 1, nullptr, &w, nullptr, &tv);
        if (r > 0) {
            int err = 0;
            socklen_t len = sizeof err;
            ::getsockopt(m->s, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
            if (err != 0) return fail("The editor's automation link refused the connection on port " + std::to_string(port) + ".");
            break;
        }
        if (pump) pump();
        if (std::chrono::steady_clock::now() > until) return fail("The editor's automation link did not answer in time.");
    }
    if (!Send(json::Write(json::Value::Obj().Set("okay_link", json::Value::Num(1)).Set("token", json::Value::Str(token)))))
        return fail("The token could not be sent to the editor.");
    std::string hello;
    bool closed = false;
    if (!Receive(hello, timeoutMs, pump, &closed)) return fail(closed ? "The editor closed the link (a wrong token?)." : "The editor did not answer the token in time.");
    json::Value v;
    const json::Value* ok = nullptr;
    if (!json::Parse(hello, v) || !(ok = v.Get("ok")) || !ok->IsBool() || !ok->boolean) return fail("The editor refused the token.");
    return true;
}

bool LinkClient::Send(const std::string& line) {
    if (m->s == kNone) return false;
    if (!SendAll(m->s, line + "\n")) { Close(); return false; }
    return true;
}

bool LinkClient::Receive(std::string& line, int timeoutMs, const std::function<void()>& pump, bool* closed) {
    if (closed) *closed = false;
    const auto start = std::chrono::steady_clock::now();
    for (;;) {
        const std::size_t nl = m->in.find('\n');
        if (nl != std::string::npos) {
            line = m->in.substr(0, nl);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            m->in.erase(0, nl + 1);
            return true;
        }
        if (m->s == kNone) { if (closed) *closed = true; return false; }
        char buf[65536];
        const int n = (int)::recv(m->s, buf, (int)sizeof buf, 0);
        if (n > 0) { m->in.append(buf, (std::size_t)n); if (m->in.size() > kMaxLine) { Close(); if (closed) *closed = true; return false; } continue; }
        if (n == 0 || !WouldBlock()) { Close(); if (closed) *closed = true; return false; }
        if (pump) pump();
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(m->s, &rd);
        timeval tv{0, pump ? 2000 : 50000};
        ::select((int)m->s + 1, &rd, nullptr, nullptr, &tv);
        if (timeoutMs >= 0 && std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timeoutMs)) return false;
    }
}

} // namespace okay::automation
