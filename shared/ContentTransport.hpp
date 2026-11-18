#pragma once
// The one small interface the content downloader talks to: "GET this name from the content server, optionally from a byte offset".
// The sync engine (ContentSync.hpp) is a plain state machine over it, so tests use a fake server and nothing here touches the network.
// Real transports: FileTransport (a local folder, below; for testing and offline), the HTTP transport on top of the engine's HTTP
// client (engine/include/okay/Platform/ContentHttpTransport.hpp) and the Xbox/UWP stub (same file). Header only, no threads, no
// engine code.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace okaycontent {

/// What the server answered, before the body.
struct GetHead {
    int status = 0;                  ///< HTTP status (200, 206, 404, 416, 503 ...)
    std::uint64_t bodyStart = 0;     ///< where in the file the body begins: from Content-Range for 206; 0 for 200
    std::uint64_t totalSize = 0;     ///< complete size of the file when the server said so (0 = unknown)
};

/// Hooks for one GET. onHead is called once, before any data; return false to stop (the transport then reports `aborted`).
/// onData is called for each piece of the body, in order; return false to stop.
struct GetHandlers {
    std::function<bool(const GetHead&)> onHead;
    std::function<bool(const std::uint8_t*, std::size_t)> onData;
};

struct GetResult {
    bool reached = false;       ///< a reply was obtained (any status) and the body, if any, was fully handed to onData
    bool aborted = false;       ///< a hook returned false (the caller already knows why)
    std::string error;          ///< plain words when reached is false and not aborted (no network, timed out, cut short ...)
};

class Transport {
public:
    virtual ~Transport() = default;
    /// GET `name` (a relative name such as "content.manifest" or "models/ship.glb", already checked by the caller; the transport joins it
    /// to its base and percent-encodes what a URL needs). rangeFrom > 0 asks for "bytes=rangeFrom-". `maxBytes` caps the body (0 = no
    /// cap): a transport must stop with an error when more arrives.
    virtual GetResult Get(const std::string& name, std::uint64_t rangeFrom, std::uint64_t maxBytes, const GetHandlers& h) = 0;
    /// Short name for logs ("file", "http", "uwp-stub").
    virtual const char* Describe() const = 0;
};

/// "https://host/base" + "models/my ship.glb" -> "https://host/base/models/my%20ship.glb" (unreserved characters and '/' stay).
inline std::string JoinUrl(std::string base, const std::string& name) {
    while (!base.empty() && base.back() == '/') base.pop_back();
    static const char* hx = "0123456789ABCDEF";
    std::string o = base + "/";
    for (unsigned char c : name) {
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~' || c == '/';
        if (plain) o += static_cast<char>(c);
        else { o += '%'; o += hx[c >> 4]; o += hx[c & 15]; }
    }
    return o;
}

/// Serves a local folder as if it were the server. Honours rangeFrom like a real server (206 with the right start; 416 past the end).
/// For tests, and for a game tested from a folder before anything is uploaded: OKAY_CONTENT_URL=file:///C:/path/to/folder.
class FileTransport : public Transport {
public:
    explicit FileTransport(std::string folder, std::size_t chunk = 64 * 1024) : m_dir(std::move(folder)), m_chunk(chunk ? chunk : 1) {}
    GetResult Get(const std::string& name, std::uint64_t rangeFrom, std::uint64_t maxBytes, const GetHandlers& h) override {
        GetResult r;
        namespace fs = std::filesystem;
        if (name.empty() || name.find("..") != std::string::npos || name[0] == '/' || name.find('\\') != std::string::npos || name.find(':') != std::string::npos) {
            r.error = "refused an unsafe name";
            return r;
        }
        std::error_code ec;
        const fs::path p = fs::u8path(m_dir) / fs::u8path(name);
        if (!fs::is_regular_file(p, ec)) {
            GetHead head; head.status = 404;
            r.reached = true;
            if (h.onHead && !h.onHead(head)) { r.aborted = true; r.reached = false; }
            return r;
        }
        const std::uint64_t size = fs::file_size(p, ec);
        if (ec) { r.error = "cannot read the file"; return r; }
        GetHead head;
        head.totalSize = size;
        if (rangeFrom > 0) {
            if (rangeFrom >= size) head.status = 416; else { head.status = 206; head.bodyStart = rangeFrom; }
        } else head.status = 200;
        if (h.onHead && !h.onHead(head)) { r.aborted = true; return r; }
        if (head.status == 416) { r.reached = true; return r; }
        std::ifstream in(p, std::ios::binary);
        if (!in) { r.error = "cannot open the file"; return r; }
        in.seekg(static_cast<std::streamoff>(head.bodyStart));
        std::vector<char> buf(m_chunk);
        std::uint64_t sent = 0;
        while (in) {
            in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
            const std::streamsize n = in.gcount();
            if (n <= 0) break;
            sent += static_cast<std::uint64_t>(n);
            if (maxBytes && sent > maxBytes) { r.error = "the body is larger than allowed"; return r; }
            if (h.onData && !h.onData(reinterpret_cast<const std::uint8_t*>(buf.data()), static_cast<std::size_t>(n))) { r.aborted = true; return r; }
        }
        r.reached = true;
        return r;
    }
    const char* Describe() const override { return "file"; }
private:
    std::string m_dir;
    std::size_t m_chunk;
};

/// Pick a transport for a content URL: "file://<folder>" -> FileTransport. (http and https are made by the platform layer, see
/// ContentHttpTransport.hpp, because they need the engine's HTTP client.) Returns the folder for a file URL, "" otherwise.
inline std::string FileUrlFolder(const std::string& url) {
    const std::string pfx = "file://";
    if (url.compare(0, pfx.size(), pfx) != 0) return std::string();
    std::string p = url.substr(pfx.size());
    // file:///C:/x -> C:/x on Windows-style paths; file:///home/x -> /home/x
    if (p.size() >= 3 && p[0] == '/' && std::isalpha(static_cast<unsigned char>(p[1])) && p[2] == ':') p.erase(0, 1);
    return p;
}

/// Wait time before attempt number `attempt` (1 = the first retry): base, 2x, 4x ... capped. Pure, so tests can check it.
struct RetryPolicy {
    int maxAttempts = 4;       ///< tries per file (the first one included)
    int baseDelayMs = 500;
    int maxDelayMs = 8000;
    int DelayBeforeRetry(int retryNumber) const {
        long long d = baseDelayMs;
        for (int i = 1; i < retryNumber && d < maxDelayMs; ++i) d *= 2;
        return static_cast<int>(std::min<long long>(d, maxDelayMs));
    }
};

}  // namespace okaycontent
