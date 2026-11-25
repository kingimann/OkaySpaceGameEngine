#include "WebRun.hpp"

#include "okay/Net/UdpSocket.hpp"   // okay::net::Startup / Shutdown: the platform socket library (WSAStartup on Windows)

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <unistd.h>
#  include <cerrno>
#endif

namespace fs = std::filesystem;

namespace oked {
namespace webrun {

// ---- sizes ---------------------------------------------------------------------------------------------------------

namespace {
std::string Human(std::uint64_t b) {
    char buf[32];
    if (b < 1024) std::snprintf(buf, sizeof buf, "%llu bytes", (unsigned long long)b);
    else if (b < 1024ull * 1024) std::snprintf(buf, sizeof buf, "%.0f KB", b / 1024.0);
    else std::snprintf(buf, sizeof buf, "%.1f MB", b / (1024.0 * 1024.0));
    return buf;
}
} // namespace

Sizes Measure(const std::string& dir) {
    Sizes s;
    std::error_code ec;
    const fs::path root = fs::u8path(dir);
    if (!fs::is_directory(root, ec)) return s;
    bool html = false, wasm = false;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        FileSize f;
        f.name = it->path().lexically_relative(root).generic_u8string();
        f.bytes = it->file_size(ec);
        if (ec) { ec.clear(); continue; }
        if (f.name == "index.html") html = true;
        if (f.name == "okay-player.wasm" || f.name == "okay-player.wasm.gz") wasm = true;
        s.total += f.bytes;
        s.files.push_back(std::move(f));
    }
    std::sort(s.files.begin(), s.files.end(), [](const FileSize& a, const FileSize& b) { return a.name < b.name; });
    s.built = html && wasm;
    return s;
}

std::string SizeLine(const Sizes& s) {
    if (s.files.empty()) return "no web build yet";
    std::string out;
    for (const FileSize& f : s.files) {
        if (!out.empty()) out += ", ";
        out += f.name + " " + Human(f.bytes);
    }
    return out + ": " + Human(s.total) + " in all";
}

// ---- request helpers -------------------------------------------------------------------------------------------------

Request ParseRequest(const std::string& head) {
    Request r;
    const std::size_t eol = head.find("\r\n");
    const std::string line = head.substr(0, eol);
    const std::size_t a = line.find(' ');
    if (a == std::string::npos || a == 0) return r;
    const std::size_t b = line.find(' ', a + 1);
    if (b == std::string::npos || b == a + 1) return r;
    const std::string version = line.substr(b + 1);
    if (version != "HTTP/1.1" && version != "HTTP/1.0") return r;
    r.method = line.substr(0, a);
    r.target = line.substr(a + 1, b - a - 1);
    for (char c : r.method) if (c < 'A' || c > 'Z') return Request{};
    r.ok = true;
    return r;
}

namespace {
int Hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
} // namespace

bool MapTarget(const std::string& root, const std::string& target, std::string* file) {
    if (target.empty() || target[0] != '/') return false;
    std::string path = target.substr(0, target.find_first_of("?#"));
    std::string dec;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '%') {
            if (i + 2 >= path.size()) return false;
            const int h = Hex(path[i + 1]), l = Hex(path[i + 2]);
            if (h < 0 || l < 0) return false;
            dec += char(h * 16 + l);
            i += 2;
        } else {
            dec += path[i];
        }
    }
    if (dec.find('\0') != std::string::npos || dec.find('\\') != std::string::npos || dec.find(':') != std::string::npos) return false;
    // Every segment must be a plain name: no "..", no ".", no empty segment inside the path.
    std::string rel;
    std::size_t start = 1;
    while (start <= dec.size()) {
        std::size_t slash = dec.find('/', start);
        if (slash == std::string::npos) slash = dec.size();
        const std::string seg = dec.substr(start, slash - start);
        if (seg == ".." || seg == ".") return false;
        if (seg.empty() && slash != dec.size()) return false;
        if (!seg.empty()) { if (!rel.empty()) rel += '/'; rel += seg; }
        start = slash + 1;
    }
    if (rel.empty() || dec.back() == '/') rel += rel.empty() ? "index.html" : "/index.html";
    if (file) *file = (fs::u8path(root) / fs::u8path(rel)).u8string();
    return true;
}

std::string ContentType(const std::string& name) {
    const std::size_t dot = name.rfind('.');
    std::string ext = dot == std::string::npos ? std::string() : name.substr(dot);
    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".js") return "text/javascript; charset=utf-8";
    if (ext == ".wasm") return "application/wasm";
    if (ext == ".json") return "application/json";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".ico") return "image/x-icon";
    if (ext == ".txt") return "text/plain; charset=utf-8";
    return "application/octet-stream";   // .gz, .okaypack, .data: bytes the page reads itself
}

std::string ResponseHead(int status, const std::string& type, std::uint64_t length) {
    const char* text = status == 200 ? "OK" : status == 400 ? "Bad Request" : status == 403 ? "Forbidden"
                     : status == 404 ? "Not Found" : status == 405 ? "Method Not Allowed" : "Error";
    std::string h = "HTTP/1.1 " + std::to_string(status) + " " + text + "\r\n";
    h += "Content-Type: " + type + "\r\n";
    h += "Content-Length: " + std::to_string(length) + "\r\n";
    h += "Cache-Control: no-store\r\n";   // a rebuilt game is always the one that runs
    h += "X-Content-Type-Options: nosniff\r\n";
    h += "Connection: close\r\n\r\n";
    return h;
}

// ---- the server ----------------------------------------------------------------------------------------------------

namespace {
#if defined(_WIN32)
using Sock = SOCKET;
const Sock kNoSock = INVALID_SOCKET;
void CloseSock(Sock s) { closesocket(s); }
bool SetNonBlocking(Sock s, bool on) { u_long m = on ? 1 : 0; return ioctlsocket(s, FIONBIO, &m) == 0; }
bool WouldBlock() { return WSAGetLastError() == WSAEWOULDBLOCK; }
const int kSendFlags = 0;
#else
using Sock = int;
const Sock kNoSock = -1;
void CloseSock(Sock s) { ::close(s); }
bool SetNonBlocking(Sock s, bool on) {
    const int f = fcntl(s, F_GETFL, 0);
    return f >= 0 && fcntl(s, F_SETFL, on ? (f | O_NONBLOCK) : (f & ~O_NONBLOCK)) == 0;
}
bool WouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK; }
#  if defined(MSG_NOSIGNAL)
const int kSendFlags = MSG_NOSIGNAL;   // a closed browser tab must not end the editor with SIGPIPE
#  else
const int kSendFlags = 0;
#  endif
#endif

using Clock = std::chrono::steady_clock;

struct Conn {
    Sock s = kNoSock;
    std::string in;
    Clock::time_point since;
};

bool SendAll(Sock s, const char* p, std::size_t n) {
    while (n > 0) {
        const int got = (int)::send(s, p, (int)std::min<std::size_t>(n, 1 << 20), kSendFlags);
        if (got <= 0) return false;
        p += got; n -= (std::size_t)got;
    }
    return true;
}
} // namespace

struct PreviewServer::Impl {
    Sock listen = kNoSock;
    int port = 0;
    std::string root;
    std::vector<Conn> conns;
    int served = 0;
    bool netUp = false;

    void Answer(Conn& c) {
        const Request r = ParseRequest(c.in);
        auto reply = [&](int status, const std::string& body) {
            const std::string h = ResponseHead(status, "text/plain; charset=utf-8", body.size());
            SendAll(c.s, h.data(), h.size());
            if (r.method != "HEAD") SendAll(c.s, body.data(), body.size());
        };
        // The answer is sent with the socket blocking, with a time limit, so a slow reader cannot hold the editor long.
        SetNonBlocking(c.s, false);
#if defined(_WIN32)
        DWORD tmo = 3000;
        setsockopt(c.s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tmo, sizeof tmo);
#else
        timeval tmo{3, 0};
        setsockopt(c.s, SOL_SOCKET, SO_SNDTIMEO, &tmo, sizeof tmo);
#endif
        ++served;
        if (!r.ok) return reply(400, "bad request\n");
        if (r.method != "GET" && r.method != "HEAD") return reply(405, "only GET and HEAD\n");
        std::string file;
        if (!MapTarget(root, r.target, &file)) return reply(403, "forbidden\n");
        std::error_code ec;
        const fs::path fp = fs::u8path(file);
        if (!fs::is_regular_file(fp, ec)) return reply(404, "not found\n");
        const std::uint64_t size = fs::file_size(fp, ec);
        std::ifstream f(fp, std::ios::binary);
        if (ec || !f) return reply(404, "not found\n");
        const std::string h = ResponseHead(200, ContentType(file), size);
        if (!SendAll(c.s, h.data(), h.size()) || r.method == "HEAD") return;
        std::vector<char> buf(1 << 16);
        while (f) {
            f.read(buf.data(), (std::streamsize)buf.size());
            const std::streamsize n = f.gcount();
            if (n <= 0 || !SendAll(c.s, buf.data(), (std::size_t)n)) break;
        }
    }
};

PreviewServer::PreviewServer() : m(new Impl) {}
PreviewServer::~PreviewServer() { Stop(); }

bool PreviewServer::Start(const std::string& root, int port, std::string* error) {
    Stop();
    auto fail = [&](const std::string& why) { if (error) *error = why; Stop(); return false; };
    std::error_code ec;
    if (!fs::is_directory(fs::u8path(root), ec)) return fail("The web build folder is not there: " + root);
    if (!okay::net::Startup()) return fail("The network could not be started on this computer.");
    m->netUp = true;
    std::vector<int> tries;
    if (port > 0) tries.push_back(port);
    else { for (int p = 8080; p < 8100; ++p) tries.push_back(p); tries.push_back(0); }
    for (int p : tries) {
        Sock s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == kNoSock) return fail("A network socket could not be made.");
#if !defined(_WIN32)
        const int yes = 1;   // a port the last Stop() left in TIME_WAIT can be bound again (Windows' SO_REUSEADDR means something else)
        ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
#endif
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // this computer only: nobody else on the network can reach it
        a.sin_port = htons((unsigned short)p);
        if (::bind(s, (const sockaddr*)&a, sizeof a) == 0 && ::listen(s, 16) == 0 && SetNonBlocking(s, true)) {
            sockaddr_in got{};
            socklen_t gl = sizeof got;
            ::getsockname(s, (sockaddr*)&got, &gl);
            m->listen = s;
            m->port = ntohs(got.sin_port);
            m->root = root;
            m->served = 0;
            return true;
        }
        CloseSock(s);
    }
    return fail(port > 0 ? "Port " + std::to_string(port) + " is in use." : "No free port was found for the local web server.");
}

void PreviewServer::Stop() {
    for (Conn& c : m->conns) CloseSock(c.s);
    m->conns.clear();
    if (m->listen != kNoSock) { CloseSock(m->listen); m->listen = kNoSock; }
    m->port = 0;
    if (m->netUp) { okay::net::Shutdown(); m->netUp = false; }
}

bool PreviewServer::Running() const { return m->listen != kNoSock; }
int PreviewServer::Port() const { return m->port; }
const std::string& PreviewServer::Root() const { return m->root; }
int PreviewServer::Served() const { return m->served; }
std::string PreviewServer::Url() const { return Running() ? "http://127.0.0.1:" + std::to_string(m->port) + "/" : std::string(); }

void PreviewServer::Poll() {
    if (!Running()) return;
    for (int i = 0; i < 16; ++i) {   // new connections
        const Sock s = ::accept(m->listen, nullptr, nullptr);
        if (s == kNoSock) break;
        if (!SetNonBlocking(s, true)) { CloseSock(s); continue; }
        Conn c; c.s = s; c.since = Clock::now();
        m->conns.push_back(std::move(c));
    }
    const auto now = Clock::now();
    for (std::size_t i = 0; i < m->conns.size();) {
        Conn& c = m->conns[i];
        bool done = false;
        char buf[4096];
        for (;;) {
            const int n = (int)::recv(c.s, buf, sizeof buf, 0);
            if (n > 0) { c.in.append(buf, (std::size_t)n); if (c.in.size() > 16384) { done = true; break; } continue; }
            if (n == 0 || !WouldBlock()) done = true;   // closed or failed
            break;
        }
        if (c.in.find("\r\n\r\n") != std::string::npos) { m->Answer(c); done = true; }
        // A browser may open a connection ahead of time and send nothing: drop it after a while.
        if (now - c.since > std::chrono::seconds(10)) done = true;
        if (done) { CloseSock(c.s); m->conns.erase(m->conns.begin() + (std::ptrdiff_t)i); }
        else ++i;
    }
}

PreviewServer& Shared() {
    static PreviewServer s;
    return s;
}

} // namespace webrun
} // namespace oked
