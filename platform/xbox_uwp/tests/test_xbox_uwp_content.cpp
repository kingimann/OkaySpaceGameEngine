// The Xbox transport for downloadable game content: the pure HTTP driver's timeout / cancel / progress / resume, the okaycontent::Transport
// adapter over a fake client, and the factory hook in shared/ContentHttpTransport.hpp. No Windows, no network, no threads.
#include <cstdio>
#include "XboxContentTransport.hpp"
#include "ContentHttpTransport.hpp"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)
namespace h = okay::xbox::http;

struct Rsp { int status; std::vector<h::Header> headers; std::string body; };
struct Fake : h::Client {
    std::vector<Rsp> script; size_t next = 0; std::vector<h::Request> seen; size_t chunk = 4; std::uint64_t* clock = nullptr; std::uint64_t tickPerChunk = 0;
    h::ClientResult Send(const h::Request& r, const h::Sink& s) override {
        seen.push_back(r); h::ClientResult cr;
        if (next >= script.size()) { cr.error = "no network"; return cr; }
        const Rsp& p = script[next++]; h::Head hd; hd.status = p.status; hd.headers = p.headers;
        if (!s.onHead(hd)) { cr.aborted = true; return cr; }
        for (size_t i = 0; i < p.body.size(); i += chunk) { if (clock) *clock += tickPerChunk; const size_t n = std::min(chunk, p.body.size() - i); if (!s.onData((const std::uint8_t*)p.body.data() + i, n)) { cr.aborted = true; return cr; } }
        cr.reached = true; return cr;
    }
};

int main() {
    const std::string base = "https://cdn.example.com/game";
    // progress counts the whole file (resume offset + received) and ends at the total
    { Fake c; c.script = {{206, {{"Content-Range", "bytes 4-9/10"}}, "456789"}}; std::vector<std::pair<std::uint64_t, std::uint64_t>> p; h::Options o; o.onProgress = [&](std::uint64_t d, std::uint64_t t) { p.push_back({d, t}); };
      h::GetHandlers hh; CHECK(h::Get(c, base, "a.bin", 4, 0, hh, o).reached); CHECK(p.size() == 2 && p[0].first == 8 && p[1].first == 10 && p[1].second == 10); }
    // cancel: before the request, and in the middle of the body
    { Fake c; h::Options o; o.cancel = [] { return true; }; h::GetHandlers hh; h::GetResult r = h::Get(c, base, "a.bin", 0, 0, hh, o); CHECK(r.aborted && r.cancelled && !r.reached && c.seen.empty()); }
    { Fake c; c.script = {{200, {}, "0123456789"}}; int n = 0; h::Options o; o.cancel = [&] { return ++n > 3; }; std::string got; h::GetHandlers hh; hh.onData = [&](const std::uint8_t* d, std::size_t k) { got.append((const char*)d, k); return true; };
      h::GetResult r = h::Get(c, base, "a.bin", 0, 0, hh, o); CHECK(r.aborted && r.cancelled && !r.reached && got.size() < 10 && got.size() >= 4); }
    // timeout: the budget is handed to the client, runs out during the body, and during retry waiting
    { std::uint64_t now = 1000; Fake c; c.clock = &now; c.tickPerChunk = 100; c.script = {{200, {}, "0123456789ab"}}; h::Options o; o.timeoutMs = 250; o.nowMs = [&] { return now; };
      h::GetHandlers hh; h::GetResult r = h::Get(c, base, "a.bin", 0, 0, hh, o); CHECK(!r.reached && r.timedOut && !r.aborted && r.error.find("timed out") != std::string::npos && c.seen[0].timeoutMs == 250); }
    { std::uint64_t now = 0; Fake c; c.script = {{503, {{"Retry-After", "5"}}, ""}, {200, {}, "ok"}}; h::Options o; o.timeoutMs = 3000; o.nowMs = [&] { return now; }; o.sleepMs = [&](std::uint64_t ms) { now += ms; };
      h::GetHandlers hh; h::GetResult r = h::Get(c, base, "a.bin", 0, 0, hh, o); CHECK(!r.reached && r.timedOut && c.seen.size() == 1); }
    { std::uint64_t now = 500; Fake c; c.script = {{302, {{"Location", "/b"}}, ""}, {200, {}, "ok"}}; h::Options o; o.timeoutMs = 3000; o.nowMs = [&] { now += 10; return now; };
      h::GetHandlers hh; CHECK(h::Get(c, base, "a.bin", 0, 0, hh, o).reached && c.seen.size() == 2 && c.seen[1].timeoutMs > 0 && c.seen[1].timeoutMs < 3000); }
    // a client that reports its own timeout ends as a timeout once the budget is gone, a plain error otherwise
    { std::uint64_t now = 0; struct Slow : h::Client { std::uint64_t* t; h::ClientResult Send(const h::Request&, const h::Sink&) override { *t += 9999; h::ClientResult r; r.error = "timeout"; return r; } } c; c.t = &now;
      h::Options o; o.timeoutMs = 1000; o.nowMs = [&] { return now; }; h::GetHandlers hh; h::GetResult r = h::Get(c, base, "a.bin", 0, 0, hh, o); CHECK(r.timedOut && !r.reached); }
    // no options: no timeout field, no cancel, behaves as before
    { Fake c; c.script = {{200, {}, "abc"}}; h::GetHandlers hh; h::GetResult r = h::Get(c, base, "a.bin", 0, 0, hh); CHECK(r.reached && !r.timedOut && !r.cancelled && c.seen[0].timeoutMs == 0); }

    // the okaycontent::Transport adapter, like the content downloader uses it
#if defined(OKAY_XBOX_HAVE_CONTENT_TRANSPORT)
    { Fake c; c.script = {{206, {{"Content-Range", "bytes 2-5/6"}}, "2345"}}; okay::xbox::UwpHttpTransport t(c, base); std::string got; okaycontent::GetHead hd; okaycontent::GetHandlers hh;
      hh.onHead = [&](const okaycontent::GetHead& x) { hd = x; return true; }; hh.onData = [&](const std::uint8_t* d, std::size_t n) { got.append((const char*)d, n); return true; };
      okaycontent::GetResult r = t.Get("m/a b.bin", 2, 0, hh); CHECK(r.reached && got == "2345" && hd.status == 206 && hd.bodyStart == 2 && hd.totalSize == 6 && std::string(t.Describe()) == "uwp-http");
      CHECK(c.seen[0].url == "https://cdn.example.com/game/m/a%20b.bin" && *h::FindHeader(c.seen[0].headers, "Range") == "bytes=2-"); }
    { Fake c; okay::xbox::UwpHttpTransport t(c, base); okaycontent::GetHandlers hh; CHECK(!t.Get("../x", 0, 0, hh).reached && c.seen.empty()); }
    { Fake c; c.script = {{200, {}, "abc"}}; okay::xbox::UwpHttpTransport t(c, base); okaycontent::GetHandlers hh; hh.onData = [](const std::uint8_t*, std::size_t) { return false; }; CHECK(t.Get("a", 0, 0, hh).aborted); }
    // MakeTransport: the stub until a factory is set, the real transport after (OKAY_XBOX_UWP is defined for this test)
    { auto t = okaycontent::MakeTransport("https://cdn.example.com/g", ""); CHECK(t && std::string(t->Describe()) == "uwp-stub"); }
    { Fake c; c.script = {{200, {}, "hi"}}; okaycontent::UwpTransportFactory() = [&](const std::string& url) { return std::unique_ptr<okaycontent::Transport>(new okay::xbox::UwpHttpTransport(c, url)); };
      auto t = okaycontent::MakeTransport("https://cdn.example.com/g", ""); std::string got; okaycontent::GetHandlers hh; hh.onData = [&](const std::uint8_t* d, std::size_t n) { got.append((const char*)d, n); return true; };
      CHECK(t && std::string(t->Describe()) == "uwp-http" && t->Get("x", 0, 0, hh).reached && got == "hi" && c.seen[0].url == "https://cdn.example.com/g/x");
      okaycontent::UwpTransportFactory() = nullptr; }
#else
    CHECK(!"ContentTransport.hpp not found on the include path");
#endif
    if (g_fail) { std::printf("%d failed\n", g_fail); return 1; }
    std::printf("xbox_uwp_content: all passed\n"); return 0;
}
