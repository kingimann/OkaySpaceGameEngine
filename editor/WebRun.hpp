#pragma once
// Build Settings > Web in the editor: what a packaged web game folder holds, and "Run in browser".
//
//   Measure          the download a web game folder makes (index.html, okay-player.js, the program and the game pack),
//                    file by file, so the Build window can show the size before uploading.
//   PreviewServer    a small local web server for one folder, on 127.0.0.1 only, so the browser can open the game
//                    (browsers do not run a WebAssembly game from file://). It never blocks the editor: Poll() is
//                    called once a frame, accepts, reads and answers what is ready, and closes each connection after
//                    one answer. GET and HEAD only; nothing outside the folder is served; no directory listings.
//   The request helpers (ParseRequest, MapTarget, ContentType, ResponseHead) are pure, for tests/test_web_run.cpp.
//
// Written from RFC 9112 (HTTP/1.1 message syntax) and RFC 3986 (percent-encoding); no code from another server.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace oked {
namespace webrun {

struct FileSize { std::string name; std::uint64_t bytes = 0; };
struct Sizes {
    std::vector<FileSize> files;   ///< every file in the folder (sorted by name)
    std::uint64_t total = 0;       ///< what a player downloads: all of them
    bool built = false;            ///< index.html and the program (okay-player.wasm or .wasm.gz) are there
};
/// The web game folder `dir` (scripts/web_package.py output).
Sizes Measure(const std::string& dir);
/// "index.html 3 KB, okay-player.js 120 KB, okay-player.wasm.gz 4.1 MB, game.okaypack.gz 20 KB: 4.2 MB in all".
std::string SizeLine(const Sizes& s);

// ---- the pure request helpers ----
struct Request { std::string method, target; bool ok = false; };
/// The request line of `head` (everything up to the blank line). ok is false for anything that is not "METHOD target HTTP/1.x".
Request ParseRequest(const std::string& head);
/// The file `target` names under `root`: the query and fragment dropped, %XX decoded, "/" is index.html. False (403) for
/// a path that leaves the folder ("..", a backslash, a NUL, a drive letter) or is not a plain absolute path.
bool MapTarget(const std::string& root, const std::string& target, std::string* file);
/// The Content-Type for a file name. .wasm is application/wasm (browsers need it to compile while downloading);
/// a .gz is sent as plain bytes with no Content-Encoding, because okay_web.js unpacks it itself.
std::string ContentType(const std::string& name);
/// The status line and headers of an answer with `length` body bytes.
std::string ResponseHead(int status, const std::string& type, std::uint64_t length);

class PreviewServer {
public:
    PreviewServer();
    ~PreviewServer();
    PreviewServer(const PreviewServer&) = delete;
    PreviewServer& operator=(const PreviewServer&) = delete;

    /// Serve `root` on 127.0.0.1. `port` 0 tries 8080..8099, then lets the system pick. False with `error` in plain words.
    bool Start(const std::string& root, int port, std::string* error);
    void Stop();
    bool Running() const;
    /// Accept, read and answer what is ready. Never waits.
    void Poll();
    /// http://127.0.0.1:<port>/ ("" when stopped).
    std::string Url() const;
    int Port() const;
    const std::string& Root() const;
    /// Answers sent since Start (for the Build window and the tests).
    int Served() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

/// The editor's one preview server (Build Settings > Web > Run in browser), polled by the editor every frame.
PreviewServer& Shared();

} // namespace webrun
} // namespace oked
