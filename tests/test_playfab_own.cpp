// PlayFab's HTTP calls (engine/include/okay/Platform/PlayFab/PlayFab.hpp, Post) through our own HTTP client, against a
// loopback server that plays the PlayFab API: login without the session header, calls with it, an HTTP 400 body that
// carries the error message, a server that answers with nonsense. PATH is emptied, so the curl fallback cannot succeed:
// a pass means the request really went through the own client. Built only with -DOKAY_OWN_HTTP=ON (Linux).
#define OKAY_PLAYFAB_TESTING 1
#include "okay/Platform/PlayFab/PlayFab.hpp"

#include <arpa/inet.h>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
static void SendAll(int fd, const std::string& s) { size_t o = 0; while (o < s.size()) { ssize_t n = ::send(fd, s.data() + o, s.size() - o, MSG_NOSIGNAL); if (n <= 0) return; o += size_t(n); } }
static std::string Json(int status, const std::string& body) {
    return "HTTP/1.1 " + std::to_string(status) + " X\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

int main() {
    std::signal(SIGPIPE, SIG_IGN);
    ::setenv("OKAY_OWN_HTTP", "1", 1); ::unsetenv("OKAY_HTTP_CURL");
    ::setenv("NO_PROXY", "127.0.0.1", 1); ::unsetenv("HTTP_PROXY"); ::unsetenv("http_proxy");
    ::setenv("PATH", "/nonexistent", 1);   // no curl: a silent fallback is a visible failure
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(fd, (sockaddr*)&a, sizeof a) != 0 || ::listen(fd, 8) != 0) return 2;
    socklen_t l = sizeof a; ::getsockname(fd, (sockaddr*)&a, &l);
    const std::string base = "http://127.0.0.1:" + std::to_string(ntohs(a.sin_port));
    std::atomic<bool> stop{false}; std::mutex mu; std::vector<std::string> heads, bodies; std::atomic<int> mode{0};
    std::thread srv([&] {
        std::vector<std::thread> w;
        while (!stop) {
            pollfd p{fd, POLLIN, 0}; if (::poll(&p, 1, 50) <= 0) continue;
            int c = ::accept(fd, nullptr, nullptr); if (c < 0) continue;
            w.emplace_back([&, c] {
                std::string in; char b[4096];
                for (;;) {   // keep-alive: several requests per connection
                    while (in.find("\r\n\r\n") == std::string::npos) { pollfd q{c, POLLIN, 0}; if (::poll(&q, 1, 100) <= 0) { if (stop) { ::close(c); return; } continue; } ssize_t n = ::recv(c, b, sizeof b, 0); if (n <= 0) { ::close(c); return; } in.append(b, size_t(n)); }
                    size_t he = in.find("\r\n\r\n"); std::string head = in.substr(0, he + 4); in.erase(0, he + 4);
                    size_t cl = 0, pos = head.find("Content-Length: "); if (pos != std::string::npos) cl = size_t(std::atoll(head.c_str() + pos + 16));
                    while (in.size() < cl) { pollfd q{c, POLLIN, 0}; if (::poll(&q, 1, 100) <= 0) { if (stop) { ::close(c); return; } continue; } ssize_t n = ::recv(c, b, sizeof b, 0); if (n <= 0) { ::close(c); return; } in.append(b, size_t(n)); }
                    std::string body = in.substr(0, cl); in.erase(0, cl);
                    { std::lock_guard<std::mutex> lk(mu); heads.push_back(head); bodies.push_back(body); }
                    const std::string path = head.substr(5, head.find(' ', 5) - 5);
                    if (mode == 1) { SendAll(c, "garbage, not http\r\n\r\n"); ::close(c); return; }
                    if (mode == 2) { SendAll(c, "HTTP/1.1 200 OK\r\nContent-Length: 500\r\n\r\n{\"half"); ::close(c); return; }   // truncated
                    if (path == "/Client/LoginWithCustomID") SendAll(c, Json(200, "{\"code\":200,\"data\":{\"SessionTicket\":\"TICKET-1\",\"PlayFabId\":\"PF-9\"}}"));
                    else if (path == "/Client/UpdatePlayerStatistics") {
                        if (head.find("X-Authorization: TICKET-1") != std::string::npos) SendAll(c, Json(200, "{\"code\":200,\"data\":{}}"));
                        else SendAll(c, Json(401, "{\"error\":\"NotAuthenticated\",\"errorMessage\":\"no ticket\"}"));
                    }
                    else if (path == "/Client/UpdateUserTitleDisplayName") SendAll(c, Json(400, "{\"code\":400,\"error\":\"NameNotAvailable\",\"errorMessage\":\"Name not available\"}"));
                    else SendAll(c, Json(200, "{\"data\":{}}"));
                }
            });
        }
        for (auto& t : w) t.join();
    });
    auto& pf = okay::PlayFab::Get();
    pf.Configure("TESTTITLE");
    pf.SetBaseUrlForTests(base);
    CHECK(pf.LoginWithCustomID("device-123"));
    CHECK(pf.IsLoggedIn() && pf.SessionTicket() == "TICKET-1" && pf.PlayFabId() == "PF-9");
    {   std::lock_guard<std::mutex> lk(mu);
        CHECK(heads.size() == 1 && heads[0].rfind("POST /Client/LoginWithCustomID ", 0) == 0);
        CHECK(heads[0].find("Content-Type: application/json") != std::string::npos);
        CHECK(heads[0].find("X-Authorization") == std::string::npos);   // login is not authenticated
        CHECK(bodies[0].find("\"CustomId\":\"device-123\"") != std::string::npos); }
    CHECK(pf.SetStat("score", 42));                                      // the session header goes out on later calls
    CHECK(!pf.SetDisplayName("x"));                                      // a 400 is an answer: its message reaches the caller
    CHECK(pf.LastError().find("Name not available") != std::string::npos && pf.LastError().find("HTTP 400") != std::string::npos);
    { std::lock_guard<std::mutex> lk(mu); CHECK(heads.size() == 3); }    // one connection served all three (keep-alive), nothing was replayed
    mode = 1;                                                            // nonsense answer: not reached, curl is missing: a plain failure
    CHECK(!pf.SetStat("score", 43));
    CHECK(pf.LastError().find("HTTP 0") != std::string::npos);
    mode = 2;                                                            // truncated answer
    CHECK(!pf.SetStat("score", 44));
    mode = 0;
    pf.Logout();
    CHECK(!pf.SetStat("score", 1));                                      // no ticket: the server says 401, and we report it
    CHECK(pf.LastError().find("no ticket") != std::string::npos);
    stop = true; srv.join(); ::close(fd);
    std::printf("playfab_own: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
