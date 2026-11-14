// The launcher's Versions download (launcher/VersionsNet.cpp DownloadFile) through our own HTTP client, against a loopback
// server: success, 404, a body over the cap, cancel while the server is silent, and the curl fallback when the answer is
// not HTTP. Built only with -DOKAY_OWN_HTTP=ON (Linux); needs OKAY_OWN_HTTP=1 in the environment, which main sets.
// No stub curl is installed for the cases that must NOT reach curl: PATH is emptied, so a fallback shows up as Failed.
#include "VersionsNet.hpp"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace okayversions;
using okayversions::net::DownloadFile;
namespace fs = std::filesystem;
static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void SendAll(int fd, const std::string& s) { size_t o = 0; while (o < s.size()) { ssize_t n = ::send(fd, s.data() + o, s.size() - o, MSG_NOSIGNAL); if (n <= 0) return; o += size_t(n); } }

int main() {
    ::setenv("OKAY_OWN_HTTP", "1", 1); ::unsetenv("OKAY_HTTP_CURL");
    ::setenv("NO_PROXY", "127.0.0.1", 1); ::unsetenv("HTTP_PROXY"); ::unsetenv("http_proxy");
    ::setenv("PATH", "/nonexistent", 1);   // no curl: any fallback is a visible failure
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(fd, (sockaddr*)&a, sizeof a) != 0 || ::listen(fd, 8) != 0) return 2;
    socklen_t l = sizeof a; ::getsockname(fd, (sockaddr*)&a, &l);
    const std::string base = "http://127.0.0.1:" + std::to_string(ntohs(a.sin_port));
    std::atomic<bool> stop{false};
    std::string body(2000, 'd');
    std::thread srv([&] {
        std::vector<std::thread> w;
        while (!stop) {
            pollfd p{fd, POLLIN, 0}; if (::poll(&p, 1, 50) <= 0) continue;
            int c = ::accept(fd, nullptr, nullptr); if (c < 0) continue;
            w.emplace_back([&, c] {
                std::string in; char b[2048];
                while (in.find("\r\n\r\n") == std::string::npos) { pollfd q{c, POLLIN, 0}; if (::poll(&q, 1, 2000) <= 0) { ::close(c); return; } ssize_t n = ::recv(c, b, sizeof b, 0); if (n <= 0) { ::close(c); return; } in.append(b, size_t(n)); }
                const std::string path = in.substr(4, in.find(' ', 4) - 4);
                if (path == "/ok") SendAll(c, "HTTP/1.1 200 OK\r\nContent-Length: 2000\r\n\r\n" + body);
                else if (path == "/big") SendAll(c, "HTTP/1.1 200 OK\r\nContent-Length: 2000\r\n\r\n" + body);
                else if (path == "/redir") SendAll(c, "HTTP/1.1 302 Found\r\nLocation: /ok\r\nContent-Length: 0\r\n\r\n");
                else if (path == "/missing") SendAll(c, "HTTP/1.1 404 Not Found\r\nContent-Length: 4\r\n\r\nnope");
                else if (path == "/garbage") SendAll(c, "not http\r\n\r\n");
                else if (path == "/hang") { while (!stop) std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
                ::close(c);
            });
        }
        for (auto& t : w) t.join();
    });
    char tmpl[] = "/tmp/okay_lvdl_XXXXXX"; if (!::mkdtemp(tmpl)) return 2;
    const fs::path dest = fs::path(tmpl) / "sub" / "file.part";
    std::vector<std::uint64_t> bytes;
    auto onBytes = [&](std::uint64_t n) { bytes.push_back(n); };
    auto never = [] { return false; };
    DownloadResult r = DownloadFile(base + "/ok", dest, 100000, onBytes, never);
    CHECK(r.status == DownloadStatus::Ok && fs::file_size(dest) == 2000 && !bytes.empty() && bytes.back() == 2000);
    r = DownloadFile(base + "/redir", dest, 100000, onBytes, never);
    CHECK(r.status == DownloadStatus::Ok && fs::file_size(dest) == 2000);
    r = DownloadFile(base + "/missing", dest, 100000, onBytes, never);
    CHECK(r.status == DownloadStatus::Failed && r.error.find("does not have") != std::string::npos && !fs::exists(dest));
    r = DownloadFile(base + "/big", dest, 1000, onBytes, never);   // 2000 bytes against a 1000 cap
    CHECK(r.status == DownloadStatus::Failed && r.error.find("more data") != std::string::npos && !fs::exists(dest));
    r = DownloadFile(base + "/garbage", dest, 100000, onBytes, never);   // no usable answer: falls to curl, which is not there
    CHECK(r.status == DownloadStatus::Failed && !fs::exists(dest));
    {   std::atomic<int> polls{0};
        auto t0 = std::chrono::steady_clock::now();
        r = DownloadFile(base + "/hang", dest, 100000, onBytes, [&] { return ++polls > 5; });
        double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(r.status == DownloadStatus::Cancelled && !fs::exists(dest) && secs < 5.0); }
    stop = true; srv.join(); ::close(fd);
    std::error_code ec; fs::remove_all(tmpl, ec);
    std::printf("launcher_download_own: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
