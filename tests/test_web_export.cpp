// The web export's C++ parts: Build Settings > Web's size report and local "Run in browser" server (editor/WebRun.cpp),
// and the web player's save folder and prefs write timing (platform/web/WebPlayer.hpp). The browser side is checked by
// tests/scripts/test_web_player.js and the packager by tests/scripts/test_web_package.py.
#include "test_framework.hpp"

#include "../editor/WebRun.hpp"
#include "../platform/web/WebPlayer.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#endif

namespace fs = std::filesystem;
using namespace oked::webrun;

namespace {
void Write(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << s;
}

#if !defined(_WIN32)
// Ask the server for `request` and return the whole answer, polling the server between reads as the editor's frames do.
std::string Ask(PreviewServer& srv, const std::string& request) {
    const int s = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET; a.sin_port = htons((unsigned short)srv.Port()); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(s, (const sockaddr*)&a, sizeof a) != 0) { ::close(s); return "connect failed"; }
    ::send(s, request.data(), request.size(), 0);
    std::string out;
    for (int i = 0; i < 400; ++i) {
        srv.Poll();
        pollfd p{s, POLLIN, 0};
        if (::poll(&p, 1, 5) > 0) {
            char buf[8192];
            const ssize_t n = ::recv(s, buf, sizeof buf, 0);
            if (n <= 0) break;
            out.append(buf, (std::size_t)n);
        }
    }
    ::close(s);
    return out;
}
#endif
} // namespace

int main() {
    RUN_SUITE("web_export");

    // ---- the save folder from the page ----
    CHECK(okay::web::SaveDirFromEnv("/okaysave/okayspace_tests_web_test/") == "/okaysave/okayspace_tests_web_test/");
    CHECK(okay::web::SaveDirFromEnv("/okaysave/game") == "/okaysave/game/");        // the slash is added
    CHECK(okay::web::SaveDirFromEnv(nullptr).empty());
    CHECK(okay::web::SaveDirFromEnv("").empty());
    CHECK(okay::web::SaveDirFromEnv("/okaysave/").empty());                         // no game id
    CHECK(okay::web::SaveDirFromEnv("/okaysave/../etc/").empty());                  // not a plain id
    CHECK(okay::web::SaveDirFromEnv("/okaysave/a/b/").empty());
    CHECK(okay::web::SaveDirFromEnv("/okaysave/Game").empty());                     // the page lower-cases ids
    CHECK(okay::web::SaveDirFromEnv("/tmp/okaysave/game/").empty());                // not under the mount root
    CHECK(okay::web::SaveDirFromEnv(("/okaysave/" + std::string(61, 'a')).c_str()).empty());

    // ---- prefs written after a quiet half second, not on every change ----
    {
        okay::web::PendingWrite w(0.5);
        CHECK(!w.Pending() && !w.Due(10.0));
        w.Touch(1.0);
        CHECK(w.Pending() && !w.Due(1.2));
        w.Touch(1.3);                      // another change moves the write on
        CHECK(!w.Due(1.6));
        CHECK(w.Due(1.8));
        w.Done();
        CHECK(!w.Pending() && !w.Due(5.0));
    }

    // ---- request parsing and the path rules ----
    {
        Request r = ParseRequest("GET /index.html HTTP/1.1\r\nHost: x\r\n\r\n");
        CHECK(r.ok && r.method == "GET" && r.target == "/index.html");
        CHECK(ParseRequest("HEAD / HTTP/1.0\r\n\r\n").ok);
        CHECK(!ParseRequest("GET /index.html\r\n\r\n").ok);              // no version
        CHECK(!ParseRequest("GET  / HTTP/1.1\r\n\r\n").ok);              // empty target
        CHECK(!ParseRequest("get / HTTP/1.1\r\n\r\n").ok);               // methods are upper case
        CHECK(!ParseRequest("GET / SPDY/3\r\n\r\n").ok);
        CHECK(!ParseRequest("").ok);

        std::string f;
        CHECK(MapTarget("/srv", "/", &f) && fs::u8path(f) == fs::u8path("/srv/index.html"));
        CHECK(MapTarget("/srv", "/okay-player.wasm.gz?v=2#x", &f) && fs::u8path(f) == fs::u8path("/srv/okay-player.wasm.gz"));
        CHECK(MapTarget("/srv", "/a%20b/c.js", &f) && fs::u8path(f) == fs::u8path("/srv/a b/c.js"));
        CHECK(MapTarget("/srv", "/sub/", &f) && fs::u8path(f) == fs::u8path("/srv/sub/index.html"));
        CHECK(!MapTarget("/srv", "/../etc/passwd", &f));
        CHECK(!MapTarget("/srv", "/a/../../x", &f));
        CHECK(!MapTarget("/srv", "/%2e%2e/x", &f));                       // ".." written in percent codes
        CHECK(!MapTarget("/srv", "/a%5c..%5cx", &f));                     // a backslash
        CHECK(!MapTarget("/srv", "/a%00.html", &f));                      // a NUL
        CHECK(!MapTarget("/srv", "/C:/Windows/win.ini", &f));             // a drive letter
        CHECK(!MapTarget("/srv", "/a//b", &f));                           // an empty segment
        CHECK(!MapTarget("/srv", "/bad%zz", &f));
        CHECK(!MapTarget("/srv", "/cut%2", &f));
        CHECK(!MapTarget("/srv", "http://x/", &f));                       // not a plain path
        CHECK(!MapTarget("/srv", "", &f));

        CHECK(ContentType("okay-player.wasm") == "application/wasm");
        CHECK(ContentType("INDEX.HTML") == "text/html; charset=utf-8");
        CHECK(ContentType("okay-player.js") == "text/javascript; charset=utf-8");
        CHECK(ContentType("okay-player.wasm.gz") == "application/octet-stream");   // no Content-Encoding: the page unpacks it
        CHECK(ContentType("game.okaypack.gz") == "application/octet-stream");
        CHECK(ContentType("noext") == "application/octet-stream");

        const std::string h = ResponseHead(200, "application/wasm", 1234);
        CHECK(h.rfind("HTTP/1.1 200 OK\r\n", 0) == 0);
        CHECK(h.find("Content-Length: 1234\r\n") != std::string::npos);
        CHECK(h.find("Content-Type: application/wasm\r\n") != std::string::npos);
        CHECK(h.find("Content-Encoding") == std::string::npos);
        CHECK(h.size() >= 4 && h.compare(h.size() - 4, 4, "\r\n\r\n") == 0);
        CHECK(ResponseHead(404, "text/plain", 0).rfind("HTTP/1.1 404 Not Found", 0) == 0);
    }

    // ---- sizes of a web game folder ----
    const fs::path root = fs::temp_directory_path() / "okay_web_export_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    {
        CHECK(!Measure((root / "missing").u8string()).built);
        CHECK(SizeLine(Measure((root / "missing").u8string())) == "no web build yet");
        Write(root / "site" / "index.html", "<html>okay</html>");
        Write(root / "site" / "okay-player.js", std::string(2048, 'j'));
        Sizes s = Measure((root / "site").u8string());
        CHECK(!s.built);                                               // no program yet
        Write(root / "site" / "okay-player.wasm.gz", std::string(3 * 1024 * 1024, 'w'));
        Write(root / "site" / "game.okaypack.gz", std::string(10, 'p'));
        s = Measure((root / "site").u8string());
        CHECK(s.built);
        CHECK(s.files.size() == 4);
        CHECK(s.files[0].name == "game.okaypack.gz" && s.files[0].bytes == 10);
        CHECK(s.total == 17 + 2048 + 3 * 1024 * 1024 + 10);
        const std::string line = SizeLine(s);
        CHECK(line.find("okay-player.wasm.gz 3.0 MB") != std::string::npos);
        CHECK(line.find("okay-player.js 2 KB") != std::string::npos);
        CHECK(line.find("game.okaypack.gz 10 bytes") != std::string::npos);
        CHECK(line.find(": 3.0 MB in all") != std::string::npos);
    }

    // ---- the local server ----
    {
        PreviewServer srv;
        std::string err;
        CHECK(!srv.Start((root / "missing").u8string(), 0, &err) && !err.empty());
        CHECK(!srv.Running() && srv.Url().empty());
        CHECK(srv.Start((root / "site").u8string(), 0, &err));
        CHECK(srv.Running() && srv.Port() > 0);
        CHECK(srv.Url() == "http://127.0.0.1:" + std::to_string(srv.Port()) + "/");
        srv.Poll();   // nothing waiting: returns at once
#if !defined(_WIN32)
        std::string a = Ask(srv, "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
        CHECK(a.rfind("HTTP/1.1 200 OK", 0) == 0);
        CHECK(a.find("Content-Type: text/html") != std::string::npos);
        CHECK(a.size() > 17 && a.compare(a.size() - 17, 17, "<html>okay</html>") == 0);
        a = Ask(srv, "GET /okay-player.wasm.gz HTTP/1.1\r\n\r\n");
        CHECK(a.find("Content-Length: 3145728\r\n") != std::string::npos);
        CHECK(a.size() == a.find("\r\n\r\n") + 4 + 3145728);           // the whole 3 MB body arrives
        a = Ask(srv, "HEAD /okay-player.js HTTP/1.1\r\n\r\n");
        CHECK(a.find("Content-Length: 2048\r\n") != std::string::npos && a.size() == a.find("\r\n\r\n") + 4);
        CHECK(Ask(srv, "GET /nope.txt HTTP/1.1\r\n\r\n").rfind("HTTP/1.1 404", 0) == 0);
        CHECK(Ask(srv, "GET /../okay_web_export_test/site/index.html HTTP/1.1\r\n\r\n").rfind("HTTP/1.1 403", 0) == 0);
        CHECK(Ask(srv, "POST / HTTP/1.1\r\n\r\n").rfind("HTTP/1.1 405", 0) == 0);
        CHECK(Ask(srv, "nonsense\r\n\r\n").rfind("HTTP/1.1 400", 0) == 0);
        CHECK(srv.Served() == 7);
        {   // a connection that sends nothing does not stop others being answered
            const int idle = ::socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in ia{}; ia.sin_family = AF_INET; ia.sin_port = htons((unsigned short)srv.Port()); ia.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            CHECK(::connect(idle, (const sockaddr*)&ia, sizeof ia) == 0);
            CHECK(Ask(srv, "GET /index.html HTTP/1.1\r\n\r\n").rfind("HTTP/1.1 200", 0) == 0);
            ::close(idle);
        }
        // Only this computer: the server is bound to 127.0.0.1.
        {
            sockaddr_in got{}; socklen_t gl = sizeof got;
            const int probe = ::socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in pa{}; pa.sin_family = AF_INET; pa.sin_port = htons((unsigned short)srv.Port()); pa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            CHECK(::connect(probe, (const sockaddr*)&pa, sizeof pa) == 0);
            ::getpeername(probe, (sockaddr*)&got, &gl);
            CHECK(ntohl(got.sin_addr.s_addr) == INADDR_LOOPBACK);
            ::close(probe);
        }
#endif
        const int port = srv.Port();
        srv.Stop();
        CHECK(!srv.Running());
        PreviewServer again;   // the port is free again after Stop
        CHECK(again.Start((root / "site").u8string(), port, &err));
        again.Stop();
    }

    fs::remove_all(root, ec);
    TEST_MAIN_RESULT();
}
