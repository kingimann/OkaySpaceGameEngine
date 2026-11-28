#pragma once
// The storage provider of Live Updates: the one small interface the content downloader and the publisher talk to. Reading is "GET this
// name, optionally from a byte offset" (whole files and byte ranges); a provider that can be written also lists its names and publishes
// (puts) files. The sync engine (ContentSync.hpp) and the publisher (ContentPublish.hpp) are plain code over it, so tests use a fake
// server or a folder and nothing here touches the network. Nothing in this core names a particular storage service.
// Providers: FileTransport (a local folder, below: read and write; for tests, offline and LAN publishing), the HTTP transport over the
// engine's HTTP client and the Xbox/UWP stub (ContentHttpTransport.hpp, read only), and StorageTransport over OkayBackend Storage
// (ContentBackendStorage.hpp: read, list and publish). Header only, no threads, no engine code.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "okay/Core/FileCopy.hpp"   // CopyFileOver (Windows-safe overwrite)

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

/// One name a provider holds (List).
struct Listed {
    std::string name;            ///< relative, '/' separators
    std::uint64_t size = 0;      ///< bytes (0 when the provider does not say)
};

/// The answer of Put and List.
struct StoreResult {
    bool ok = false;
    std::string error;           ///< plain words when !ok
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
    /// Every name under `prefix` ("" = all; a folder prefix ends in '/'), recursively, sorted by name. A read-only provider says so.
    virtual StoreResult List(const std::string& prefix, std::vector<Listed>& out) {
        (void)prefix;
        out.clear();
        StoreResult r; r.error = std::string("the ") + Describe() + " storage cannot list its files";
        return r;
    }
    /// Publish the local file `localFile` as `name`, replacing what is there. A reader sees the old bytes or the new bytes, never a mix,
    /// where the provider can promise it (FileTransport: temp file + rename). A read-only provider says so.
    virtual StoreResult Put(const std::string& name, const std::string& localFile) {
        (void)name; (void)localFile;
        StoreResult r; r.error = std::string("the ") + Describe() + " storage is read only";
        return r;
    }
};

/// One whole-body GET into memory (a manifest, a signature, a config). `status` is the reply's status (0 = no reply); `tooBig` is set
/// when the body passed `maxBytes` (the GET is then stopped). Success is `result.reached && status == 200`.
struct WholeGet {
    GetResult result;
    int status = 0;
    bool tooBig = false;
    bool Ok() const { return result.reached && status == 200; }
};
inline WholeGet GetWhole(Transport& t, const std::string& name, std::uint64_t maxBytes, std::string& out) {
    WholeGet w;
    out.clear();
    GetHandlers h;
    h.onHead = [&](const GetHead& hd) {
        w.status = hd.status;
        if (hd.status == 200 && maxBytes && hd.totalSize > maxBytes) { w.tooBig = true; return false; }   // the server said the size: no need to fetch it
        return hd.status == 200;
    };
    h.onData = [&](const std::uint8_t* d, std::size_t n) {
        if (maxBytes && out.size() + n > maxBytes) { w.tooBig = true; return false; }
        out.append(reinterpret_cast<const char*>(d), n);
        return true;
    };
    w.result = t.Get(name, 0, maxBytes, h);
    return w;
}

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
        if (!SafeName(name)) { r.error = "refused an unsafe name"; return r; }
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

    StoreResult List(const std::string& prefix, std::vector<Listed>& out) override {
        namespace fs = std::filesystem;
        out.clear();
        StoreResult r;
        std::error_code ec;
        const fs::path root = fs::u8path(m_dir);
        if (!fs::is_directory(root, ec)) { r.ok = true; return r; }   // nothing published yet: an empty list
        for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_symlink(ec) || !it->is_regular_file(ec)) continue;
            const std::string rel = fs::relative(it->path(), root, ec).generic_u8string();
            if (ec || rel.empty() || rel.compare(0, prefix.size(), prefix) != 0) continue;
            if (rel.compare(0, 5, ".put-") == 0 || rel.find("/.put-") != std::string::npos) continue;   // an unfinished Put
            Listed l; l.name = rel; l.size = static_cast<std::uint64_t>(it->file_size(ec));
            out.push_back(l);
        }
        if (ec) { r.error = "cannot read the folder: " + ec.message(); return r; }
        std::sort(out.begin(), out.end(), [](const Listed& a, const Listed& b) { return a.name < b.name; });
        r.ok = true;
        return r;
    }

    StoreResult Put(const std::string& name, const std::string& localFile) override {
        namespace fs = std::filesystem;
        StoreResult r;
        if (!SafeName(name)) { r.error = "refused an unsafe name"; return r; }
        std::error_code ec;
        const fs::path dest = fs::u8path(m_dir) / fs::u8path(name);
        fs::create_directories(dest.parent_path(), ec);
        const fs::path tmp = dest.parent_path() / (".put-" + dest.filename().u8string());
        if (!okay::CopyFileOver(fs::u8path(localFile), tmp, ec)) { r.error = "cannot copy '" + localFile + "': " + ec.message(); fs::remove(tmp, ec); return r; }
        fs::rename(tmp, dest, ec);   // the switch: readers see the old file or the new one
        if (ec) { r.error = "cannot put '" + name + "' in place: " + ec.message(); std::error_code e2; fs::remove(tmp, e2); return r; }
        r.ok = true;
        return r;
    }

private:
    static bool SafeName(const std::string& name) {
        return !(name.empty() || name.find("..") != std::string::npos || name[0] == '/' || name.find('\\') != std::string::npos || name.find(':') != std::string::npos);
    }
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
