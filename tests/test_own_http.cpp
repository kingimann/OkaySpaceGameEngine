// Unit tests and mutation fuzz for okay::ownhttp (engine/include/okay/Platform/OwnHttp.hpp):
// the pure HTTP/1.1 layer, driven over in-memory streams. No sockets, no TLS.
// Standalone: g++ -std=c++17 -I engine/include tests/test_own_http.cpp (also run under ASan/UBSan).
#include "okay/Platform/OwnHttp.hpp"

#include <cstdio>
#include <cstdlib>
#include <map>

using namespace okay::ownhttp;

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

// Scripted stream: serves `in` in pieces of `chunk` bytes, then end of stream (or an error).
struct MemStream : Stream {
    std::string in, written;
    size_t pos = 0, chunk = 1 << 20;
    bool errAtEnd = false;
    long Read(void* b, size_t n, Err& e) override {
        if (pos >= in.size()) { if (errAtEnd) { e = Err::Timeout; return -1; } return 0; }
        size_t k = std::min({n, chunk, in.size() - pos});
        std::memcpy(b, in.data() + pos, k); pos += k; return long(k);
    }
    bool Write(const void* b, size_t n, Err&) override { written.append((const char*)b, n); return true; }
};

struct Parsed { bool ok; Err e; std::string msg; Response r; };
static Parsed Parse(const std::string& wire, const std::string& method = "GET", size_t chunk = 1 << 20,
                    Limits lim = Limits()) {
    MemStream ms; ms.in = wire; ms.chunk = chunk;
    Reader rd(ms);
    Parsed p; p.ok = false; p.e = Err::None;
    if (!ReadHead(rd, lim, p.r, p.e, &p.msg)) return p;
    uint64_t total = 0;
    p.ok = ReadBody(rd, p.r, method, lim, [&](const char* d, size_t n) { p.r.body.append(d, n); return true; }, total, p.e, &p.msg);
    p.r.bodyBytes = total;
    return p;
}

static void TestUrl() {
    Url u; std::string why;
    CHECK(ParseUrl("https://Example.COM/a/b?x=1#frag", u) && u.host == "example.com" && u.port == 443 && u.target == "/a/b?x=1" && u.Https());
    CHECK(ParseUrl("http://h:8080", u) && u.port == 8080 && u.target == "/");
    CHECK(ParseUrl("http://h?q=1", u) && u.target == "/?q=1");
    CHECK(ParseUrl("http://[::1]:99/x", u) && u.host == "::1" && u.port == 99 && u.HostPort() == "[::1]:99");
    CHECK(ParseUrl("HTTP://h/", u) && u.scheme == "http");
    CHECK(!ParseUrl("ftp://h/", u));
    CHECK(!ParseUrl("http://", u));
    CHECK(!ParseUrl("http://u:p@h/", u, &why));
    CHECK(!ParseUrl("http://h:0/", u));
    CHECK(!ParseUrl("http://h:65536/", u));
    CHECK(!ParseUrl("http://h:12a/", u));
    CHECK(!ParseUrl("http://h/a b", u));
    CHECK(!ParseUrl("http://h/a\r\nHost: evil", u));
    CHECK(!ParseUrl(std::string("http://h/a") + '\0' + "b", u));
    CHECK(!ParseUrl("http://h\\@evil/", u));
    CHECK(!ParseUrl("http://ex ample/", u));
    CHECK(!ParseUrl("http://h/\xc3\xa9", u));
    CHECK(!ParseUrl("http://[::1/", u));
    CHECK(!ParseUrl("http://[zz]/", u));
    CHECK(!ParseUrl("http://a:b:c/", u));
    CHECK(!ParseUrl(std::string("http://h/") + std::string(9000, 'a'), u));
    CHECK(!ParseUrl(std::string("http://") + std::string(300, 'a') + "/", u));

    Url b; ParseUrl("https://h/a/b/c?q=1", b);
    auto R = [&](const char* l) { Url o; return ResolveLocation(b, l, o) ? o.scheme + "://" + o.HostPort() + o.target : std::string("ERR"); };
    CHECK(R("/x") == "https://h/x");
    CHECK(R("d") == "https://h/a/b/d");
    CHECK(R("../d") == "https://h/a/d");
    CHECK(R("../../../../d") == "https://h/d");
    CHECK(R("./d?z") == "https://h/a/b/d?z");
    CHECK(R("?z=2") == "https://h/a/b/c?z=2");
    CHECK(R("//o.example/p") == "https://o.example/p");
    CHECK(R("http://o.example:81/p") == "http://o.example:81/p");
    CHECK(R("ftp://x/") == "ERR");
    CHECK(R("javascript:alert(1)") == "ERR");
    CHECK(R("") == "ERR");
    CHECK(R("/a\r\nSet-Cookie: x") == "ERR");
    CHECK(R("/a b") == "ERR");
    CHECK(R("..") == "https://h/a/");
}

static void TestBuild() {
    Url u; ParseUrl("https://h:8443/p?q", u);
    std::string w; Err e; std::string m;
    CHECK(BuildRequest("GET", u, {{"apikey", "k"}}, "", false, w, e, &m));
    CHECK(w.rfind("GET /p?q HTTP/1.1\r\nHost: h:8443\r\n", 0) == 0);
    CHECK(w.find("Connection: close\r\n") != std::string::npos && w.find("Accept-Encoding: identity\r\n") != std::string::npos);
    CHECK(w.find("apikey: k\r\n") != std::string::npos && w.size() > 4 && w.substr(w.size() - 4) == "\r\n\r\n");
    CHECK(BuildRequest("POST", u, {}, "abc", false, w, e) && w.find("Content-Length: 3\r\n") != std::string::npos && w.substr(w.size() - 3) == "abc");
    CHECK(BuildRequest("POST", u, {}, "", false, w, e) && w.find("Content-Length: 0\r\n") != std::string::npos);
    CHECK(BuildRequest("GET", u, {}, "", true, w, e) && w.rfind("GET https://h:8443/p?q HTTP/1.1", 0) == 0);
    CHECK(!BuildRequest("GET", u, {{"X", "a\r\nInjected: 1"}}, "", false, w, e) && e == Err::BadHeader);
    CHECK(!BuildRequest("GET", u, {{"X", std::string("a\0b", 3)}}, "", false, w, e));
    CHECK(!BuildRequest("GET", u, {{"X\r\nY", "a"}}, "", false, w, e));
    CHECK(!BuildRequest("GET", u, {{"X Y", "a"}}, "", false, w, e));
    CHECK(!BuildRequest("GET", u, {{"", "a"}}, "", false, w, e));
    CHECK(!BuildRequest("GET /x HTTP/1.0\r\n", u, {}, "", false, w, e));
    CHECK(!BuildRequest("GET", u, {{"Host", "evil"}}, "", false, w, e));
    CHECK(!BuildRequest("GET", u, {{"content-length", "5"}}, "", false, w, e));
    CHECK(!BuildRequest("GET", u, {{"Transfer-Encoding", "chunked"}}, "", false, w, e));
    CHECK(!BuildRequest("GET", u, {{"Accept-Encoding", "gzip"}}, "", false, w, e));
    Url bad = u; bad.target = "/a\r\nX: y";
    CHECK(!BuildRequest("GET", bad, {}, "", false, w, e) && e == Err::BadUrl);
    Header h;
    CHECK(ParseHeaderLine("Name:  v a l \t", h) && h.name == "Name" && h.value == "v a l");
    CHECK(!ParseHeaderLine("NoColon", h));
    CHECK(!ParseHeaderLine("A: b\nC: d", h));
    CHECK(!ParseHeaderLine("A: b\rC: d", h));
    CHECK(!ParseHeaderLine(": v", h));
}

static const char* kOk = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nX-A: b\r\n\r\nhello";

static void TestParse() {
    Parsed p = Parse(kOk);
    CHECK(p.ok && p.r.status == 200 && p.r.body == "hello" && *p.r.Get("x-a") == "b" && p.r.reason == "OK");
    for (size_t c : {size_t(1), size_t(2), size_t(3), size_t(7)}) { p = Parse(kOk, "GET", c); CHECK(p.ok && p.r.body == "hello"); }
    p = Parse("HTTP/1.0 200 OK\r\n\r\nuntil close");
    CHECK(p.ok && p.r.body == "until close" && p.r.minor == 0);
    p = Parse("HTTP/1.1 200\r\nContent-Length: 0\r\n\r\n"); CHECK(p.ok && p.r.reason.empty());
    p = Parse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n6;ext=1\r\n world\r\n0\r\nTrailer: x\r\n\r\n");
    CHECK(p.ok && p.r.body == "hello world");
    for (size_t c : {size_t(1), size_t(2), size_t(5)}) {
        p = Parse("HTTP/1.1 200 OK\r\nTransfer-Encoding: Chunked\r\n\r\nA\r\n0123456789\r\n0\r\n\r\n", "GET", c);
        CHECK(p.ok && p.r.body == "0123456789");
    }
    p = Parse("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello", "HEAD"); CHECK(p.ok && p.r.body.empty());
    p = Parse("HTTP/1.1 204 No Content\r\nContent-Length: 5\r\n\r\n"); CHECK(p.ok && p.r.body.empty());
    p = Parse("HTTP/1.1 304 Not Modified\r\n\r\n"); CHECK(p.ok && p.r.body.empty());
    p = Parse("HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nhi"); CHECK(p.ok && p.r.status == 200 && p.r.body == "hi");
    p = Parse("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 5\r\n\r\nhello"); CHECK(p.ok);
    p = Parse("HTTP/1.1 200 OK\r\nContent-Encoding: identity\r\nContent-Length: 1\r\n\r\nx"); CHECK(p.ok);

    // hostile
    auto Bad = [&](const std::string& w, Err want, const char* tag) {
        Parsed q = Parse(w);
        ++g_checks;
        if (q.ok || q.e != want) { ++g_fail; std::printf("FAIL hostile %s: ok=%d err=%s\n", tag, q.ok, ErrName(q.e)); }
    };
    Bad("HTTP/1.1 101 Switching\r\nUpgrade: websocket\r\n\r\n", Err::BadResponse, "101");
    Bad("HTTP/1.1 200 OK\nContent-Length: 0\n\n", Err::BadResponse, "bare LF");
    Bad("HTTP/1.1 200 OK\r\nX: a\nY: b\r\n\r\n", Err::BadResponse, "bare LF in header");
    Bad("HTTP/1.1 200 OK\r\nX: a\rb\r\n\r\n", Err::BadResponse, "bare CR");
    Bad("HTTP/1.1 200 OK\r\nX: a\r\n b\r\n\r\n", Err::BadResponse, "obs-fold");
    Bad("HTTP/1.1 200 OK\r\nX a: b\r\n\r\n", Err::BadResponse, "space in name");
    Bad("HTTP/1.1 200 OK\r\nNoColon\r\n\r\n", Err::BadResponse, "no colon");
    Bad(std::string("HTTP/1.1 200 OK\r\nX: a") + '\0' + "b\r\n\r\n", Err::BadResponse, "NUL");
    Bad("HTTP/2 200\r\n\r\n", Err::BadResponse, "h2 line");
    Bad("HTTP/1.1 20 OK\r\n\r\n", Err::BadResponse, "short code");
    Bad("HTTP/1.1 2000 OK\r\n\r\n", Err::BadResponse, "long code");
    Bad("HTTP/1.1 099 OK\r\n\r\n", Err::BadResponse, "code <100");
    Bad("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\nhello!", Err::BadResponse, "CL conflict");
    Bad("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n", Err::BadResponse, "CL+TE");
    Bad("HTTP/1.1 200 OK\r\nContent-Length: +5\r\n\r\nhello", Err::BadResponse, "CL plus");
    Bad("HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n", Err::BadResponse, "CL negative");
    Bad("HTTP/1.1 200 OK\r\nContent-Length: 5, 5\r\n\r\nhello", Err::BadResponse, "CL list");
    Bad("HTTP/1.1 200 OK\r\nContent-Length: \r\n\r\n", Err::BadResponse, "CL empty");
    Bad("HTTP/1.1 200 OK\r\nContent-Length: 99999999999999999999\r\n\r\n", Err::BadResponse, "CL overflow");
    Bad("HTTP/1.1 200 OK\r\nContent-Length: 9999999999999999999\r\n\r\n", Err::TooLarge, "CL huge");
    Bad("HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\nshort", Err::BadResponse, "truncated body");
    Bad("HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, chunked\r\n\r\n", Err::Unsupported, "TE list");
    Bad("HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\n", Err::Unsupported, "TE gzip");
    Bad("HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: 0\r\n\r\n", Err::Unsupported, "CE gzip");
    Bad("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nFFFFFFFFFFFFFFFFF\r\n", Err::BadResponse, "chunk 17 digits");
    Bad("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nFFFFFFFFFFFFFFFF\r\n", Err::TooLarge, "chunk max");
    Bad("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n-5\r\n", Err::BadResponse, "chunk negative");
    Bad("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0x5\r\n", Err::BadResponse, "chunk 0x");
    Bad("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n\r\n", Err::BadResponse, "chunk empty size");
    Bad("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhelloXX0\r\n\r\n", Err::BadResponse, "chunk no CRLF");
    Bad("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n", Err::BadResponse, "chunked truncated");
    Bad("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\nbad trailer\r\n\r\n", Err::BadResponse, "bad trailer");
    Bad("", Err::BadResponse, "empty");
    Bad("HTTP/1.1 200 OK\r\nX: y\r\n", Err::BadResponse, "head truncated");
    Bad("HTTP/1.1 100 Continue\r\n\r\n", Err::BadResponse, "only interim");
    {
        std::string many; for (int i = 0; i < 9; ++i) many += "HTTP/1.1 102 Processing\r\n\r\n";
        Bad(many + "HTTP/1.1 200 OK\r\n\r\n", Err::BadResponse, "interim flood");
    }
    {   // head too large, too many headers, huge line
        std::string big = "HTTP/1.1 200 OK\r\n";
        for (int i = 0; i < 101; ++i) big += "X" + std::to_string(i) + ": v\r\n";
        Bad(big + "\r\n", Err::TooLarge, "101 headers");
        std::string huge = "HTTP/1.1 200 OK\r\n";
        for (int i = 0; i < 100; ++i) huge += "X" + std::to_string(i) + ": " + std::string(1000, 'a') + "\r\n";
        huge += "\r\n";
        Limits lim; lim.maxHeaderBytes = 8192;
        Parsed q = Parse(huge, "GET", 1 << 20, lim); CHECK(!q.ok && q.e == Err::TooLarge);
        Bad("HTTP/1.1 200 OK\r\nX: " + std::string(100000, 'a'), Err::BadResponse, "endless line");
        Bad("HTTP/1.1 200 " + std::string(5000, 'a') + "\r\n\r\n", Err::BadResponse, "long status line");
    }
    {   // body cap, all three framings
        Limits lim; lim.maxBodyBytes = 10;
        Parsed q = Parse("HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\n01234567890", "GET", 1 << 20, lim); CHECK(!q.ok && q.e == Err::TooLarge);
        q = Parse("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\n0123456789", "GET", 1 << 20, lim); CHECK(q.ok);
        q = Parse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n6\r\n012345\r\n5\r\n67890\r\n0\r\n\r\n", "GET", 1 << 20, lim); CHECK(!q.ok && q.e == Err::TooLarge);
        q = Parse("HTTP/1.1 200 OK\r\n\r\n01234567890", "GET", 3, lim); CHECK(!q.ok && q.e == Err::TooLarge);
        q = Parse("HTTP/1.1 200 OK\r\n\r\n0123456789", "GET", 3, lim); CHECK(q.ok);
    }
    {   // stream error mid body
        MemStream ms; ms.in = "HTTP/1.1 200 OK\r\nContent-Length: 50\r\n\r\nabc"; ms.errAtEnd = true;
        Reader rd(ms); Response r; Err e = Err::None; std::string m;
        CHECK(ReadHead(rd, Limits(), r, e, &m));
        uint64_t t; CHECK(!ReadBody(rd, r, "GET", Limits(), DataFn(), t, e, &m) && e == Err::Timeout);
    }
    {   // abort from the data callback
        MemStream ms; ms.in = kOk; Reader rd(ms); Response r; Err e; std::string m; uint64_t t;
        CHECK(ReadHead(rd, Limits(), r, e, &m));
        CHECK(!ReadBody(rd, r, "GET", Limits(), [](const char*, size_t) { return false; }, t, e, &m) && e == Err::Aborted);
    }
}

// Connector backed by a script: (request wire, url) -> response bytes. Records every request.
struct ScriptConn : Connector {
    std::function<std::string(const Url&, const std::string&)> serve;
    std::vector<std::string> wires; std::vector<std::string> hosts;
    bool absolute = false;
    struct S : MemStream { ScriptConn* c; Url u;
        bool Write(const void* b, size_t n, Err& e) override { MemStream::Write(b, n, e); if (written.find("\r\n\r\n") != std::string::npos && in.empty()) { c->wires.push_back(written); in = c->serve(u, written); } return true; } };
    std::unique_ptr<Stream> Open(const Url& u, Clock::time_point, Err& e, std::string& m) override {
        if (u.host == "unreachable.example") { e = Err::Connect; m = "no route"; return nullptr; }
        hosts.push_back(u.scheme + "://" + u.HostPort());
        auto s = std::make_unique<S>(); s->c = this; s->u = u; return s;
    }
    bool AbsoluteForm(const Url&) override { return absolute; }
};

static std::string Redir(int code, const std::string& loc) {
    return "HTTP/1.1 " + std::to_string(code) + " X\r\nLocation: " + loc + "\r\nContent-Length: 3\r\n\r\nxyz";
}

static void TestFetch() {
    Limits lim;
    {   ScriptConn c; c.serve = [](const Url&, const std::string&) { return std::string(kOk); };
        FetchRequest rq; rq.url = "https://h/x";
        FetchResult r = Fetch(c, rq, lim, 5000);
        CHECK(r.ok && r.response.body == "hello" && r.redirects == 0 && r.finalUrl.host == "h"); }
    {   // redirect chain, relative + absolute, 302 turns POST into GET and drops the body
        ScriptConn c;
        c.serve = [](const Url& u, const std::string&) {
            if (u.target == "/a") return Redir(302, "/b");
            if (u.target == "/b") return Redir(301, "c");
            return std::string(kOk); };
        FetchRequest rq; rq.method = "POST"; rq.url = "https://h/a"; rq.body = "secret"; rq.headerLines = {"Content-Type: text/plain"};
        FetchResult r = Fetch(c, rq, lim, 5000);
        CHECK(r.ok && r.redirects == 2 && r.finalUrl.target == "/c");
        CHECK(c.wires.size() == 3 && c.wires[0].rfind("POST /a", 0) == 0 && c.wires[1].rfind("GET /b", 0) == 0 && c.wires[2].rfind("GET /c", 0) == 0);
        CHECK(c.wires[1].find("secret") == std::string::npos && c.wires[1].find("Content-Type") == std::string::npos); }
    {   // 307 keeps method and body
        ScriptConn c; c.serve = [](const Url& u, const std::string&) { return u.target == "/a" ? Redir(307, "/b") : std::string(kOk); };
        FetchRequest rq; rq.method = "POST"; rq.url = "https://h/a"; rq.body = "keep";
        FetchResult r = Fetch(c, rq, lim, 5000);
        CHECK(r.ok && c.wires[1].rfind("POST /b", 0) == 0 && c.wires[1].find("keep") != std::string::npos); }
    {   // cross-origin: credentials are not forwarded; same-origin: they are
        ScriptConn c; c.serve = [](const Url& u, const std::string&) {
            if (u.target == "/a") return Redir(302, "https://other.example/b");
            if (u.target == "/s") return Redir(302, "/t");
            return std::string(kOk); };
        FetchRequest rq; rq.url = "https://h/a"; rq.headerLines = {"Authorization: Bearer T", "apikey: K", "Cache-Control: no-cache", "Cookie: a=b"};
        FetchResult r = Fetch(c, rq, lim, 5000);
        CHECK(r.ok && c.wires[0].find("Bearer T") != std::string::npos);
        CHECK(c.wires[1].find("Bearer T") == std::string::npos && c.wires[1].find("apikey") == std::string::npos && c.wires[1].find("Cookie") == std::string::npos);
        CHECK(c.wires[1].find("Cache-Control: no-cache") != std::string::npos && c.hosts[1] == "https://other.example");
        ScriptConn c2; c2.serve = c.serve; rq.url = "https://h/s";
        r = Fetch(c2, rq, lim, 5000);
        CHECK(r.ok && c2.wires[1].find("Bearer T") != std::string::npos); }
    {   // loop cap
        ScriptConn c; c.serve = [](const Url&, const std::string&) { return Redir(302, "/loop"); };
        FetchRequest rq; rq.url = "https://h/loop";
        FetchResult r = Fetch(c, rq, lim, 5000);
        CHECK(!r.ok && r.err == Err::TooManyRedirects && c.wires.size() == size_t(lim.maxRedirects) + 1);
        lim.maxRedirects = 0; r = Fetch(c, rq, lim, 5000); CHECK(r.err == Err::TooManyRedirects); lim.maxRedirects = 10; }
    {   // https -> http refused, http -> https fine, bad Location refused
        ScriptConn c; c.serve = [](const Url&, const std::string&) { return Redir(302, "http://h/x"); };
        FetchRequest rq; rq.url = "https://h/a";
        FetchResult r = Fetch(c, rq, lim, 5000);
        CHECK(!r.ok && r.err == Err::DowngradeRefused && c.wires.size() == 1);
        ScriptConn c2; c2.serve = [](const Url& u, const std::string&) { return u.Https() ? std::string(kOk) : Redir(302, "https://h/x"); };
        rq.url = "http://h/a"; r = Fetch(c2, rq, lim, 5000); CHECK(r.ok && r.finalUrl.Https());
        ScriptConn c3; c3.serve = [](const Url&, const std::string&) { return Redir(302, "ftp://h/x"); };
        r = Fetch(c3, rq, lim, 5000); CHECK(!r.ok && r.err == Err::BadUrl);
        ScriptConn c4; c4.serve = [](const Url&, const std::string&) { return std::string("HTTP/1.1 302 X\r\nLocation: /a\r\nLocation: /b\r\nContent-Length: 0\r\n\r\n"); };
        r = Fetch(c4, rq, lim, 5000); CHECK(r.redirects >= 1); }
    {   // a 3xx without Location is a final response
        ScriptConn c; c.serve = [](const Url&, const std::string&) { return std::string("HTTP/1.1 302 X\r\nContent-Length: 2\r\n\r\nok"); };
        FetchRequest rq; rq.url = "https://h/a"; FetchResult r = Fetch(c, rq, lim, 5000);
        CHECK(r.ok && r.response.status == 302 && r.response.body == "ok"); }
    {   // errors: bad url, bad header, connect failure, absolute form, hooks
        ScriptConn c; c.serve = [](const Url&, const std::string&) { return std::string(kOk); };
        FetchRequest rq; rq.url = "https://h/a\r\nX: y";
        CHECK(Fetch(c, rq, lim, 5000).err == Err::BadUrl);
        rq.url = "https://h/"; rq.headerLines = {"X: a\r\nY: b"};
        CHECK(Fetch(c, rq, lim, 5000).err == Err::BadHeader && c.wires.empty());
        rq.headerLines = {}; rq.url = "https://unreachable.example/";
        FetchResult r = Fetch(c, rq, lim, 5000); CHECK(!r.ok && r.err == Err::Connect);
        rq.url = "http://h/p"; c.absolute = true; Fetch(c, rq, lim, 5000);
        CHECK(c.wires.back().rfind("GET http://h/p HTTP/1.1", 0) == 0);
        FetchHooks hk; std::string got; int heads = 0;
        hk.onHead = [&](const Response& h) { ++heads; return h.status == 200; };
        hk.onData = [&](const char* p, size_t n) { got.append(p, n); return true; };
        r = Fetch(c, rq, lim, 5000, hk); CHECK(r.ok && got == "hello" && heads == 1 && r.response.body.empty());
        hk.onHead = [](const Response&) { return false; };
        r = Fetch(c, rq, lim, 5000, hk); CHECK(!r.ok && r.err == Err::Aborted); }
}

// ---- mutation fuzz -----------------------------------------------------------
struct Rng { uint64_t s; uint64_t Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; } size_t Below(size_t n) { return n ? size_t(Next() % n) : 0; } };

static void Fuzz(int iters) {
    const std::vector<std::string> corpus = {
        kOk,
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n6;a=b\r\n world\r\n0\r\nT: x\r\n\r\n",
        "HTTP/1.0 200 OK\r\nContent-Type: a/b\r\n\r\nbody until close",
        "HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 302 Found\r\nLocation: /x\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 204 No Content\r\n\r\n",
    };
    const char* const dict[] = {"\r\n", "\n", "\r", "\0", "Content-Length: ", "Transfer-Encoding: chunked\r\n", "0\r\n\r\n", "FFFFFFFFFFFFFFFF",
                                "99999999999999999999", ": ", "HTTP/1.1 ", " ", "\t", "-1", "gzip"};
    Rng rng{0x9E3779B97F4A7C15ull};
    Limits lim; lim.maxBodyBytes = 1 << 16; lim.maxHeaderBytes = 4096;
    size_t okCount = 0;
    for (int it = 0; it < iters; ++it) {
        std::string w = corpus[rng.Below(corpus.size())];
        int nm = 1 + int(rng.Below(6));
        for (int k = 0; k < nm; ++k) {
            switch (rng.Below(6)) {
            case 0: if (!w.empty()) w[rng.Below(w.size())] = char(rng.Below(256)); break;
            case 1: if (!w.empty()) w.erase(rng.Below(w.size()), 1 + rng.Below(8)); break;
            case 2: { const char* d = dict[rng.Below(sizeof dict / sizeof dict[0])]; size_t l = std::strlen(d); if (d[0] == 0) l = 1; w.insert(rng.Below(w.size() + 1), d, l); break; }
            case 3: if (!w.empty()) w.resize(rng.Below(w.size() + 1)); break;
            case 4: { size_t a = rng.Below(w.size() + 1), n = rng.Below(w.size() - a + 1); w.insert(rng.Below(w.size() + 1), w.substr(a, n)); break; }
            case 5: if (w.size() > 1) std::swap(w[rng.Below(w.size())], w[rng.Below(w.size())]); break;
            }
        }
        for (const char* method : {"GET", "HEAD"}) {
            Parsed p = Parse(w, method, 1 + rng.Below(40), lim);
            Parsed whole = Parse(w, method, 1 << 20, lim);   // delivery in pieces must not change the verdict
            if (p.ok != whole.ok || (p.ok && (p.r.body != whole.r.body || p.r.status != whole.r.status))) { ++g_fail; std::printf("FAIL fuzz: split delivery differs\n"); }
            if (p.ok) {
                ++okCount;
                if (p.r.bodyBytes > lim.maxBodyBytes || p.r.body.size() != p.r.bodyBytes || p.r.status < 200) { ++g_fail; std::printf("FAIL fuzz invariant\n"); }
                for (const Header& h : p.r.headers) if (!detail::IsToken(h.name) || !detail::CleanValue(h.value)) { ++g_fail; std::printf("FAIL fuzz header invariant\n"); }
            } else if (p.e == Err::None) { ++g_fail; std::printf("FAIL fuzz: failure without an error code\n"); }
        }
        // whole Fetch path with the mutated bytes as the server's answer
        ScriptConn c; c.serve = [&](const Url&, const std::string&) { return w; };
        FetchRequest rq; rq.url = "https://h/x";
        FetchResult r = Fetch(c, rq, lim, 5000);
        if (r.ok == (r.err != Err::None)) { ++g_fail; std::printf("FAIL fuzz: ok/err mismatch\n"); }
        if (r.redirects > lim.maxRedirects) { ++g_fail; std::printf("FAIL fuzz: redirect cap\n"); }
    }
    // random URLs and Locations never crash and never yield control characters
    Url base; ParseUrl("https://h/a/b?q", base);
    const char alpha[] = "abAB09:/?#[]@\\ .%-_\r\n\t\xc3\xa9~&=;";
    for (int it = 0; it < iters; ++it) {
        std::string s = rng.Below(2) ? "http://" : (rng.Below(2) ? "https://" : "");
        for (size_t n = rng.Below(40); n; --n) s += alpha[rng.Below(sizeof alpha - 1)];
        Url u, v;
        if (ParseUrl(s, u)) {
            for (unsigned char ch : u.target) if (ch <= 0x20 || ch == 0x7f) { ++g_fail; std::printf("FAIL url target ctl\n"); break; }
            if (u.host.empty() || u.port < 1 || u.port > 65535 || u.target[0] != '/') { ++g_fail; std::printf("FAIL url invariant\n"); }
        }
        if (ResolveLocation(base, s, v)) {
            if (v.target.empty() || v.target[0] != '/') { ++g_fail; std::printf("FAIL loc invariant\n"); }
            std::string w; Err e;
            if (!BuildRequest("GET", v, {}, "", false, w, e) || w.find("\r\n\r\n") != w.size() - 4) { ++g_fail; std::printf("FAIL request smuggling via Location\n"); }
        }
    }
    std::printf("fuzz: %d iterations, %zu parsed ok\n", iters, okCount);
}

int main(int argc, char** argv) {
    int iters = 20000;
    if (const char* e = std::getenv("OKAY_FUZZ_ITERS")) iters = std::atoi(e);
    if (argc > 1) iters = std::atoi(argv[1]);
    TestUrl(); TestBuild(); TestParse(); TestFetch(); Fuzz(iters);
    std::printf("own_http: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
