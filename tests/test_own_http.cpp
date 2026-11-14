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


// One-reply connector that remembers the request bytes (the shared ScriptConn above routes by URL).
struct ScriptConnForGzip : Connector {
    std::string reply, wire; size_t chunk = 1 << 20;
    struct S : MemStream { std::string* w; bool Write(const void* b, size_t n, Err&) override { w->append((const char*)b, n); return true; } };
    std::unique_ptr<Stream> Open(const Url&, Clock::time_point, Err&, std::string&) override {
        auto s = std::make_unique<S>(); s->in = reply; s->chunk = chunk; s->w = &wire; return s;
    }
};

// ---- gzip / deflate / Content-Range / progress --------------------------------
static const std::string kGz = std::string(
    "\x1f\x8b\x08\x00\x00\x00\x00\x00\x02\x03\x0b\xc9\x48\x55\x28\x2c\xcd\x4c\xce\x56\x48\x2a\xca\x2f\xcf\x53\x48\xcb\xaf\x50\xc8\x2a\xcd\x2d\x28\x56\xc8\x2f\x4b\x2d"
    "\x52\x28\x01\x4a\xe7\x24\x56\x55\x2a\xa4\xe4\xa7\xeb\x29\x84\x8c\x2a\x1e\x55\x3c\xaa\x78\x54\xf1\xa8\xe2\x51\xc5\xc3\x4b\x31\x03\x23\x13\x33\x0b\x2b\x1b\x3b\x07"
    "\x27\x17\x37\x0f\x2f\x1f\xbf\x80\xa0\x90\xb0\x88\xa8\x98\xb8\x84\xa4\x94\xb4\x8c\xac\x9c\xbc\x82\xa2\x92\xb2\x8a\xaa\x9a\xba\x86\xa6\x96\xb6\x8e\xae\x9e\xbe\x81"
    "\xa1\x91\xb1\x89\xa9\x99\xb9\x85\xa5\x95\xb5\x8d\xad\x9d\xbd\x83\xa3\x93\xb3\x8b\xab\x9b\xbb\x87\xa7\x97\xb7\x8f\xaf\x9f\x7f\x40\x60\x50\x70\x48\x68\x58\x78\x44"
    "\x64\x54\x74\x4c\x6c\x5c\x7c\x42\x62\x52\x72\x4a\x6a\x5a\x7a\x46\x66\x56\x76\x4e\x6e\x5e\x7e\x41\x61\x51\x71\x49\x69\x59\x79\x45\x65\x55\x75\x4d\x6d\x5d\x7d\x43"
    "\x63\x53\x73\x4b\x6b\x5b\x7b\x47\x67\x57\x77\x4f\x6f\x5f\xff\x84\x89\x93\x26\x4f\x99\x3a\x6d\xfa\x8c\x99\xb3\x66\xcf\x99\x3b\x6f\xfe\x82\x85\x8b\x16\x2f\x59\xba"
    "\x6c\xf9\x8a\x95\xab\x56\xaf\x59\xbb\x6e\xfd\x86\x8d\x9b\x36\x6f\xd9\xba\x6d\xfb\x8e\x9d\xbb\x76\xef\xd9\xbb\x6f\xff\x81\x83\x87\x0e\x1f\x39\x7a\xec\xf8\x89\x93"
    "\xa7\x4e\x9f\x39\x7b\xee\xfc\x85\x8b\x97\x2e\x5f\xb9\x7a\xed\xfa\x8d\x9b\xb7\x6e\xdf\xb9\x7b\xef\xfe\x83\x87\x8f\x1e\x3f\x79\xfa\xec\xf9\x8b\x97\xaf\x5e\xbf\x79"
    "\xfb\xee\xfd\x87\x8f\x9f\x3e\x7f\xf9\xfa\xed\xfb\x8f\x9f\xbf\x7e\xff\xf9\xfb\xef\xff\xa8\xff\x47\xb6\xff\x4b\x12\x33\x73\x00\x66\xcb\xd5\x6f\x0c\x0a\x00\x00", 359);
static const std::string kZlib = std::string(
    "\x78\xda\x0b\xc9\x48\x55\x28\x2c\xcd\x4c\xce\x56\x48\x2a\xca\x2f\xcf\x53\x48\xcb\xaf\x50\xc8\x2a\xcd\x2d\x28\x56\xc8\x2f\x4b\x2d\x52\x28\x01\x4a\xe7\x24\x56\x55"
    "\x2a\xa4\xe4\xa7\xeb\x29\x84\x8c\x2a\x1e\x55\x3c\xaa\x78\x54\xf1\xa8\xe2\x51\xc5\xc3\x4b\x31\x03\x23\x13\x33\x0b\x2b\x1b\x3b\x07\x27\x17\x37\x0f\x2f\x1f\xbf\x80"
    "\xa0\x90\xb0\x88\xa8\x98\xb8\x84\xa4\x94\xb4\x8c\xac\x9c\xbc\x82\xa2\x92\xb2\x8a\xaa\x9a\xba\x86\xa6\x96\xb6\x8e\xae\x9e\xbe\x81\xa1\x91\xb1\x89\xa9\x99\xb9\x85"
    "\xa5\x95\xb5\x8d\xad\x9d\xbd\x83\xa3\x93\xb3\x8b\xab\x9b\xbb\x87\xa7\x97\xb7\x8f\xaf\x9f\x7f\x40\x60\x50\x70\x48\x68\x58\x78\x44\x64\x54\x74\x4c\x6c\x5c\x7c\x42"
    "\x62\x52\x72\x4a\x6a\x5a\x7a\x46\x66\x56\x76\x4e\x6e\x5e\x7e\x41\x61\x51\x71\x49\x69\x59\x79\x45\x65\x55\x75\x4d\x6d\x5d\x7d\x43\x63\x53\x73\x4b\x6b\x5b\x7b\x47"
    "\x67\x57\x77\x4f\x6f\x5f\xff\x84\x89\x93\x26\x4f\x99\x3a\x6d\xfa\x8c\x99\xb3\x66\xcf\x99\x3b\x6f\xfe\x82\x85\x8b\x16\x2f\x59\xba\x6c\xf9\x8a\x95\xab\x56\xaf\x59"
    "\xbb\x6e\xfd\x86\x8d\x9b\x36\x6f\xd9\xba\x6d\xfb\x8e\x9d\xbb\x76\xef\xd9\xbb\x6f\xff\x81\x83\x87\x0e\x1f\x39\x7a\xec\xf8\x89\x93\xa7\x4e\x9f\x39\x7b\xee\xfc\x85"
    "\x8b\x97\x2e\x5f\xb9\x7a\xed\xfa\x8d\x9b\xb7\x6e\xdf\xb9\x7b\xef\xfe\x83\x87\x8f\x1e\x3f\x79\xfa\xec\xf9\x8b\x97\xaf\x5e\xbf\x79\xfb\xee\xfd\x87\x8f\x9f\x3e\x7f"
    "\xf9\xfa\xed\xfb\x8f\x9f\xbf\x7e\xff\xf9\xfb\xef\xff\xa8\xff\x47\xb6\xff\x4b\x12\x33\x73\x00\xf9\x8e\x06\x7f", 347);
static const std::string kStoredZ = std::string(
    "\x78\x01\x01\x0c\x00\xf3\xff\x73\x74\x6f\x72\x65\x64\x20\x64\x61\x74\x61\x21\x1f\x1f\x04\x6d", 23);
static const std::string kFixedRaw = std::string(
    "\xcb\x48\xcd\xc9\xc9\x57\xc8\x40\x90\x00", 10);

static std::string GzPlain() {
    std::string t;
    for (int i = 0; i < 40; ++i) t += "The quick brown fox jumps over the lazy dog. ";
    for (int k = 0; k < 3; ++k) for (int i = 0; i < 256; ++i) t += char(i);
    return t + "tail";
}

// Minimal bit writer: a fixed-Huffman block with one literal and N maximum-length matches (a zip bomb shape).
static std::string BombRaw(size_t matches) {
    std::string out; uint32_t acc = 0; int n = 0;
    auto bits = [&](uint32_t v, int c) { for (int i = 0; i < c; ++i) { acc |= ((v >> i) & 1u) << n; if (++n == 8) { out.push_back(char(acc)); acc = 0; n = 0; } } };
    auto huff = [&](uint32_t code, int len) { for (int i = len - 1; i >= 0; --i) bits((code >> i) & 1u, 1); };   // codes go MSB first
    bits(1, 1); bits(1, 2);                         // final block, fixed codes
    huff(0x30 + 'a', 8);                            // literal 'a'
    for (size_t i = 0; i < matches; ++i) { huff(0xC0 + (285 - 280), 8); huff(0, 5); }   // length 258 (code 285), distance 1
    huff(0, 7);                                     // end of block (256)
    if (n) out.push_back(char(acc));
    return out;
}
static std::string GzipOf(const std::string& raw, const std::string& plain) {
    std::string g = std::string("\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\x03", 10) + raw;
    uint32_t c = inflate_detail::Crc32(plain), sz = uint32_t(plain.size());
    for (int i = 0; i < 4; ++i) g.push_back(char(c >> (8 * i)));
    for (int i = 0; i < 4; ++i) g.push_back(char(sz >> (8 * i)));
    return g;
}

static void TestInflate() {
    std::string out, why;
    const std::string plain = GzPlain();
    CHECK(InflateBody(kGz, true, 1 << 20, out, why) && out == plain);
    CHECK(InflateBody(kZlib, false, 1 << 20, out, why) && out == plain);
    CHECK(InflateBody(kStoredZ, false, 1 << 20, out, why) && out == "stored data!");
    {   // raw fixed block wrapped by hand
        std::string z = std::string("\x78\x01", 2) + kFixedRaw;
        uint32_t a = inflate_detail::Adler32("hello hello hello");
        for (int i = 3; i >= 0; --i) z.push_back(char(a >> (8 * i)));
        CHECK(InflateBody(z, false, 1 << 20, out, why) && out == "hello hello hello"); }
    // output cap: exactly at the limit passes, one under fails with a "larger than the limit" reason
    CHECK(InflateBody(kGz, true, plain.size(), out, why));
    CHECK(!InflateBody(kGz, true, plain.size() - 1, out, why) && why.find("larger than the limit") != std::string::npos);
    CHECK(!InflateBody(kGz, true, 0, out, why));
    // every truncation fails, nothing crashes
    for (size_t k = 0; k < kGz.size(); ++k) CHECK(!InflateBody(kGz.substr(0, k), true, 1 << 20, out, why));
    for (size_t k = 0; k < kZlib.size(); ++k) CHECK(!InflateBody(kZlib.substr(0, k), false, 1 << 20, out, why));
    // every single byte flip: either refused or, if accepted, still the right bytes (header fields like mtime)
    for (size_t k = 0; k < kGz.size(); ++k) for (int bit = 0; bit < 8; ++bit) {
        std::string m = kGz; m[k] = char(m[k] ^ (1 << bit));
        if (InflateBody(m, true, 1 << 20, out, why)) CHECK(out == plain);
    }
    for (size_t k = 0; k < kZlib.size(); ++k) {
        std::string m = kZlib; m[k] = char(m[k] ^ 0x10);
        if (InflateBody(m, false, 1 << 20, out, why)) CHECK(out == plain);
    }
    CHECK(!InflateBody(kGz + "x", true, 1 << 20, out, why));            // trailing byte
    CHECK(!InflateBody(kGz + kGz, true, 1 << 20, out, why));            // second member
    CHECK(!InflateBody(kZlib + "x", false, 1 << 20, out, why));
    { std::string m = kGz; m[3] = char(0x20); CHECK(!InflateBody(m, true, 1 << 20, out, why)); }   // reserved flag
    { std::string m = kGz; m[2] = 7; CHECK(!InflateBody(m, true, 1 << 20, out, why)); }           // not deflate
    CHECK(!InflateBody("", true, 100, out, why) && !InflateBody("", false, 100, out, why));
    CHECK(!InflateBody(std::string(100, '\xff'), true, 1 << 20, out, why));
    // a distance before the start of the output: fixed block, literal then a match at distance 2
    {   std::string raw; uint32_t acc = 0; int n = 0;
        auto bits = [&](uint32_t v, int c) { for (int i = 0; i < c; ++i) { acc |= ((v >> i) & 1u) << n; if (++n == 8) { raw.push_back(char(acc)); acc = 0; n = 0; } } };
        auto huff = [&](uint32_t code, int len) { for (int i = len - 1; i >= 0; --i) bits((code >> i) & 1u, 1); };
        bits(1, 1); bits(1, 2); huff(0x30 + 'a', 8);
        huff(1, 7 /* length code 257 = 3 */); huff(1, 5); /* distance code 1 = distance 2 */
        huff(0, 7); if (n) raw.push_back(char(acc));
        CHECK(!InflateBody(GzipOf(raw, "aaaa"), true, 1 << 20, out, why) && why.find("too far") != std::string::npos); }
    // zip bomb: ~1 MiB of output from a few KB, stops at the cap without allocating more than the cap
    {   std::string raw = BombRaw(4000);
        std::string plain2(1 + 4000 * 258, 'a');
        CHECK(raw.size() < 8000);
        CHECK(InflateBody(GzipOf(raw, plain2), true, plain2.size(), out, why) && out == plain2);
        CHECK(!InflateBody(GzipOf(raw, plain2), true, 100000, out, why) && why.find("larger than the limit") != std::string::npos && out.size() <= 100000); }
    // an incomplete / over-subscribed dynamic table is refused: type 2 block with HLIT=0 HDIST=0 HCLEN=0 and zero code lengths
    {   std::string raw("\x05\x00\x00\x00\x00\x00\x00\x00", 8);
        CHECK(!InflateBody(GzipOf(raw, ""), true, 1000, out, why)); }
    // mutation fuzz over random bytes: never crashes, output never passes the cap
    Rng rng{0x9e3779b97f4a7c15ull};
    for (int i = 0; i < 3000; ++i) {
        std::string m = (i & 1) ? kGz : kZlib;
        for (int k = 0, c = 1 + int(rng.Below(6)); k < c; ++k) m[rng.Below(m.size())] = char(rng.Next());
        if (rng.Below(4) == 0) m.resize(rng.Below(m.size() + 1));
        InflateBody(m, (i & 1) != 0, 4096, out, why);
        CHECK(out.size() <= 4096);
    }
}

static void TestContentCoding() {
    Limits lim; lim.allowGzip = true;
    const std::string plain = GzPlain();
    auto resp = [](const std::string& enc, const std::string& body, bool chunked = false) {
        std::string h = "HTTP/1.1 200 OK\r\nContent-Encoding: " + enc + "\r\n";
        if (chunked) {
            std::string b; for (size_t i = 0; i < body.size(); i += 7) { std::string part = body.substr(i, 7); char hx[16]; std::snprintf(hx, sizeof hx, "%zx", part.size()); b += std::string(hx) + "\r\n" + part + "\r\n"; }
            return h + "Transfer-Encoding: chunked\r\n\r\n" + b + "0\r\n\r\n";
        }
        return h + "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    };
    Parsed p = Parse(resp("gzip", kGz), "GET", 1 << 20, lim);
    CHECK(p.ok && p.r.body == plain && p.r.bodyBytes == plain.size());
    p = Parse(resp("GZIP", kGz), "GET", 3, lim); CHECK(p.ok && p.r.body == plain);                 // tiny pieces
    p = Parse(resp("x-gzip", kGz, true), "GET", 1 << 20, lim); CHECK(p.ok && p.r.body == plain);   // gzip inside chunked
    p = Parse(resp("deflate", kZlib), "GET", 1 << 20, lim); CHECK(p.ok && p.r.body == plain);
    p = Parse(resp("gzip", kGz)); CHECK(!p.ok && p.e == Err::Unsupported);                         // not asked for: still refused
    p = Parse(resp("br", kGz), "GET", 1 << 20, lim); CHECK(!p.ok && p.e == Err::Unsupported);
    p = Parse(resp("gzip, gzip", kGz), "GET", 1 << 20, lim); CHECK(!p.ok && p.e == Err::Unsupported);   // stacked codings
    p = Parse(resp("gzip", kGz.substr(0, 50)), "GET", 1 << 20, lim); CHECK(!p.ok);                 // truncated stream
    p = Parse(resp("gzip", "not gzip at all, really not"), "GET", 1 << 20, lim); CHECK(!p.ok && p.e == Err::BadResponse);
    { Limits small = lim; small.maxBodyBytes = 1000;                                                // decoded size capped
      p = Parse(resp("gzip", kGz), "GET", 1 << 20, small); CHECK(!p.ok && p.e == Err::TooLarge); }
    { Limits small = lim; small.maxEncodedBytes = 100;                                              // encoded size capped
      p = Parse(resp("gzip", kGz), "GET", 1 << 20, small); CHECK(!p.ok && p.e == Err::TooLarge); }
    { std::string bomb = GzipOf(BombRaw(4000), std::string(1 + 4000 * 258, 'a'));                  // bomb over a Content-Length
      Limits small = lim; small.maxBodyBytes = 64 * 1024;
      p = Parse(resp("gzip", bomb), "GET", 1 << 20, small); CHECK(!p.ok && p.e == Err::TooLarge); }
    p = Parse("HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\n\r\n" + kGz, "GET", 1 << 20, lim);   // close-delimited
    CHECK(p.ok && p.r.body == plain);
    p = Parse("HTTP/1.1 304 Not Modified\r\nContent-Encoding: gzip\r\n\r\n", "GET", 1 << 20, lim); CHECK(p.ok && p.r.body.empty());
    // the request side
    { ScriptConnForGzip c; c.reply = resp("gzip", kGz);
      FetchRequest rq; rq.url = "https://h/x"; rq.acceptGzip = true;
      FetchResult r = Fetch(c, rq, Limits(), 5000);
      CHECK(r.ok && r.response.body == plain && c.wire.find("Accept-Encoding: gzip, deflate") != std::string::npos);
      ScriptConnForGzip c2; c2.reply = resp("gzip", kGz);
      rq.acceptGzip = false; r = Fetch(c2, rq, Limits(), 5000);
      CHECK(!r.ok && r.err == Err::Unsupported && c2.wire.find("Accept-Encoding: identity") != std::string::npos); }
}

static void TestRange() {
    uint64_t a = 0, b = 0, t = 0;
    CHECK(ParseContentRange("bytes 5-9/20", a, b, t) && a == 5 && b == 9 && t == 20);
    CHECK(ParseContentRange("bytes 0-0/*", a, b, t) && t == 0);
    CHECK(!ParseContentRange("bytes 9-5/20", a, b, t));
    CHECK(!ParseContentRange("bytes 5-20/20", a, b, t));
    CHECK(!ParseContentRange("bytes */20", a, b, t));
    CHECK(!ParseContentRange("items 1-2/3", a, b, t));
    CHECK(!ParseContentRange("bytes 1-2", a, b, t));
    CHECK(!ParseContentRange("bytes -1-2/9", a, b, t));
    CHECK(!ParseContentRange("bytes 1-2/9x", a, b, t));
    CHECK(!ParseContentRange("bytes 1-99999999999999999999/9", a, b, t));
    uint64_t st = 0; bool any = false;
    std::vector<Header> h = {{"Range", "bytes=100-"}};
    CHECK(RequestedRangeStart(h, st, any) && any && st == 100);
    h = {{"range", "bytes=7-9"}}; CHECK(RequestedRangeStart(h, st, any) && st == 7);
    h = {{"Range", "bytes=-5"}}; CHECK(!RequestedRangeStart(h, st, any) && any);
    h = {{"Range", "bytes=1-2,5-6"}}; CHECK(!RequestedRangeStart(h, st, any) && any);
    h = {}; CHECK(!RequestedRangeStart(h, st, any) && !any);
    Limits lim;
    auto fetch = [&](const std::string& reply, const std::string& range, FetchResult* out = nullptr) {
        ScriptConnForGzip c; c.reply = reply;
        FetchRequest rq; rq.url = "https://h/x"; if (!range.empty()) rq.headerLines = {"Range: " + range};
        FetchResult r = Fetch(c, rq, lim, 5000); if (out) *out = r; return r; };
    FetchResult r;
    fetch("HTTP/1.1 206 Partial\r\nContent-Range: bytes 5-9/20\r\nContent-Length: 5\r\n\r\n56789", "bytes=5-", &r);
    CHECK(r.ok && r.partial && r.rangeStart == 5 && r.rangeTotal == 20 && r.response.body == "56789");
    fetch("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc", "bytes=5-", &r);                           // server ignored the range
    CHECK(r.ok && !r.partial && r.response.body == "abc");
    CHECK(fetch("HTTP/1.1 206 P\r\nContent-Range: bytes 6-9/20\r\nContent-Length: 4\r\n\r\n6789", "bytes=5-").err == Err::BadResponse);   // wrong start
    CHECK(fetch("HTTP/1.1 206 P\r\nContent-Length: 4\r\n\r\n6789", "bytes=5-").err == Err::BadResponse);                                    // no Content-Range
    CHECK(fetch("HTTP/1.1 206 P\r\nContent-Range: bytes 5-9/20\r\nContent-Length: 4\r\n\r\n5678", "bytes=5-").err == Err::BadResponse);   // length mismatch
    CHECK(fetch("HTTP/1.1 206 P\r\nContent-Range: bytes 5-9/20\r\nContent-Length: 5\r\n\r\n56789", "").err == Err::BadResponse);          // unasked 206
    fetch("HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */20\r\nContent-Length: 0\r\n\r\n", "bytes=99-", &r);
    CHECK(r.ok && r.response.status == 416 && !r.partial);
    // progress and abort from the progress callback
    {   ScriptConnForGzip c; c.reply = "HTTP/1.1 200 OK\r\nContent-Length: 40000\r\n\r\n" + std::string(40000, 'q'); c.chunk = 5000;
        FetchRequest rq; rq.url = "https://h/x"; FetchHooks hk; uint64_t lastDone = 0, lastTotal = 0; int calls = 0;
        hk.onProgress = [&](uint64_t d, uint64_t tot) { lastDone = d; lastTotal = tot; ++calls; return true; };
        FetchResult fr = Fetch(c, rq, lim, 5000, hk);
        CHECK(fr.ok && lastDone == 40000 && lastTotal == 40000 && calls >= 2 && fr.response.body.size() == 40000);
        ScriptConnForGzip c2; c2.reply = c.reply; c2.chunk = 5000; int n2 = 0;
        hk.onProgress = [&](uint64_t, uint64_t) { return ++n2 < 3; };
        fr = Fetch(c2, rq, lim, 5000, hk);
        CHECK(!fr.ok && fr.err == Err::Aborted && n2 == 3); }
}

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


// ---- keep-alive, conditional requests, cookies (in-memory) ------------------------------------------------------
// A pool-aware connector: every stream has a number, `serve(conn, nth request on it, wire)` returns the response bytes;
// "" = the peer closed the connection without answering, "\x01" = the write fails (reset).
struct PoolConn : Connector {
    std::function<std::string(int, int, const std::string&)> serve;
    std::vector<std::string> wires;
    std::vector<std::unique_ptr<Stream>> idle;
    std::vector<std::string> idleHost;
    int opened = 0, taken = 0, given = 0;
    struct PS : Stream {
        PoolConn* c; int no; int reqs = 0; std::string in; size_t pos = 0; bool dead = false;
        long Read(void* b, size_t n, Err&) override {
            if (pos >= in.size()) return 0;
            size_t k = std::min(n, in.size() - pos); std::memcpy(b, in.data() + pos, k); pos += k; return long(k);
        }
        bool Write(const void* b, size_t n, Err& e) override {
            std::string w((const char*)b, n);
            c->wires.push_back(w);
            std::string r = c->serve(no, reqs++, w);
            if (r == "\x01") { e = Err::Io; dead = true; return false; }
            in = r; pos = 0; if (r.empty()) dead = true;
            return true;
        }
        bool StillUsable() override { return !dead; }
    };
    std::unique_ptr<Stream> Open(const Url& u, Clock::time_point, Err&, std::string&) override {
        auto s = std::make_unique<PS>(); s->c = this; s->no = ++opened; (void)u; return s;
    }
    std::unique_ptr<Stream> TakeIdle(const Url& u, Clock::time_point) override {
        for (size_t i = 0; i < idle.size(); ++i)
            if (idleHost[i] == u.HostPort()) {
                auto s = std::move(idle[i]); idle.erase(idle.begin() + long(i)); idleHost.erase(idleHost.begin() + long(i));
                if (!s->StillUsable()) return nullptr;
                ++taken; return s;
            }
        return nullptr;
    }
    void GiveIdle(const Url& u, std::unique_ptr<Stream> s) override { ++given; idle.push_back(std::move(s)); idleHost.push_back(u.HostPort()); }
};
static std::string OkBody(const std::string& b, const std::string& extra = "") {
    return "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(b.size()) + "\r\n" + extra + "\r\n" + b;
}
static FetchRequest Rq(const std::string& url, const std::string& method = "GET", bool ka = true) {
    FetchRequest r; r.method = method; r.url = url; r.keepAlive = ka; return r;
}

static void TestKeepAlive() {
    Limits lim;
    {   // two sequential GETs: one connection, no "Connection: close" sent, the second is counted as reused
        PoolConn c; c.serve = [](int, int, const std::string&) { return OkBody("hello"); };
        FetchResult a = Fetch(c, Rq("http://h.example/a"), lim, 5000);
        FetchResult b = Fetch(c, Rq("http://h.example/b"), lim, 5000);
        CHECK(a.ok && b.ok && b.response.body == "hello");
        CHECK(c.opened == 1 && c.taken == 1 && a.reusedConnections == 0 && b.reusedConnections == 1);
        CHECK(c.wires.size() == 2 && c.wires[0].find("Connection: close") == std::string::npos);
    }
    {   // without the flag: close is sent and nothing is kept
        PoolConn c; c.serve = [](int, int, const std::string&) { return OkBody("x"); };
        FetchResult a = Fetch(c, Rq("http://h.example/a", "GET", false), lim, 5000);
        FetchResult b = Fetch(c, Rq("http://h.example/b", "GET", false), lim, 5000);
        CHECK(a.ok && b.ok && c.opened == 2 && c.given == 0);
        CHECK(c.wires[0].find("Connection: close\r\n") != std::string::npos);
    }
    {   // the server says close, speaks HTTP/1.0, or has no length: the connection is not kept
        for (int variant = 0; variant < 4; ++variant) {
            PoolConn c;
            c.serve = [variant](int, int, const std::string&) -> std::string {
                if (variant == 0) return OkBody("x", "Connection: close\r\n");
                if (variant == 1) return "HTTP/1.0 200 OK\r\nContent-Length: 1\r\n\r\nx";
                if (variant == 2) return "HTTP/1.1 200 OK\r\n\r\nuntil-close";
                return "HTTP/1.1 200 OK\r\nConnection: keep-alive, Close\r\nContent-Length: 1\r\n\r\nx";
            };
            FetchResult a = Fetch(c, Rq("http://h.example/a"), lim, 5000);
            CHECK(a.ok && c.given == 0);
            FetchResult b = Fetch(c, Rq("http://h.example/b"), lim, 5000);
            CHECK(b.ok && c.opened == 2);
        }
    }
    {   // a stale connection (closed before one byte of the answer): one fresh connection, the call still works
        PoolConn c;
        c.serve = [](int conn, int nth, const std::string&) -> std::string { return (conn == 1 && nth == 1) ? std::string() : OkBody("ok"); };
        FetchResult a = Fetch(c, Rq("http://h.example/a"), lim, 5000);
        // the pooled stream reports itself usable until it is asked, as a real one can (the close arrives later)
        FetchResult b = Fetch(c, Rq("http://h.example/b"), lim, 5000);
        CHECK(a.ok && b.ok && b.response.body == "ok" && c.opened == 2 && b.staleRetries == 1);
    }
    {   // the same, but the write itself fails (connection reset)
        PoolConn c;
        c.serve = [](int conn, int nth, const std::string&) -> std::string { return (conn == 1 && nth == 1) ? std::string("\x01") : OkBody("ok"); };
        Fetch(c, Rq("http://h.example/a"), lim, 5000);
        FetchResult b = Fetch(c, Rq("http://h.example/b"), lim, 5000);
        CHECK(b.ok && c.opened == 2 && b.staleRetries == 1);
    }
    {   // a FRESH connection that is closed without an answer is an error, never retried in a loop
        PoolConn c; c.serve = [](int, int, const std::string&) { return std::string(); };
        FetchResult a = Fetch(c, Rq("http://h.example/a"), lim, 5000);
        CHECK(!a.ok && c.opened == 1 && a.staleRetries == 0);
    }
    {   // POST never rides on a kept connection (a replay could run twice), but it can be offered back afterwards
        PoolConn c; c.serve = [](int, int, const std::string&) { return OkBody("ok"); };
        Fetch(c, Rq("http://h.example/a"), lim, 5000);
        FetchRequest p = Rq("http://h.example/p", "POST"); p.body = "data";
        FetchResult r = Fetch(c, p, lim, 5000);
        CHECK(r.ok && c.opened == 2 && r.reusedConnections == 0 && c.taken == 0);
        FetchResult g = Fetch(c, Rq("http://h.example/b"), lim, 5000);
        CHECK(g.ok && g.reusedConnections == 1);
    }
    {   // a POST on a fresh connection that dies is reported, not replayed
        PoolConn c; int posts = 0;
        c.serve = [&](int, int, const std::string& w) -> std::string { if (w.compare(0, 4, "POST") == 0) { ++posts; return std::string(); } return OkBody("ok"); };
        Fetch(c, Rq("http://h.example/a"), lim, 5000);
        FetchRequest p = Rq("http://h.example/p", "POST"); p.body = "data";
        FetchResult r = Fetch(c, p, lim, 5000);
        CHECK(!r.ok && posts == 1);
    }
    {   // a same-origin redirect reuses the connection; another host does not
        PoolConn c;
        c.serve = [](int, int, const std::string& w) -> std::string {
            if (w.compare(0, 11, "GET /start ") == 0) return "HTTP/1.1 302 Found\r\nLocation: /next\r\nContent-Length: 0\r\n\r\n";
            if (w.compare(0, 11, "GET /other ") == 0) return "HTTP/1.1 302 Found\r\nLocation: http://b.example/z\r\nContent-Length: 0\r\n\r\n";
            return OkBody("end");
        };
        FetchResult r = Fetch(c, Rq("http://h.example/start"), lim, 5000);
        CHECK(r.ok && r.redirects == 1 && c.opened == 1 && r.reusedConnections == 1);
        FetchResult r2 = Fetch(c, Rq("http://h.example/other"), lim, 5000);
        CHECK(r2.ok && c.opened == 2 && r2.reusedConnections == 1);   // h.example's idle connection serves /other, only b.example is new
    }
    {   // chunked, HEAD and 304 answers keep the connection, and the bytes after them are not mistaken for the next answer
        PoolConn c;
        c.serve = [](int, int, const std::string& w) -> std::string {
            if (w.compare(0, 4, "HEAD") == 0) return "HTTP/1.1 200 OK\r\nContent-Length: 12345\r\nETag: \"v1\"\r\n\r\n";
            if (w.find("If-None-Match: \"v1\"") != std::string::npos) return "HTTP/1.1 304 Not Modified\r\nETag: \"v1\"\r\n\r\n";
            return "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n";
        };
        FetchResult a = Fetch(c, Rq("http://h.example/a"), lim, 5000);
        FetchResult h = Fetch(c, Rq("http://h.example/a", "HEAD"), lim, 5000);
        FetchRequest cond = Rq("http://h.example/a"); cond.headerLines = {"If-None-Match: \"v1\""};
        FetchResult n = Fetch(c, cond, lim, 5000);
        FetchResult g = Fetch(c, Rq("http://h.example/a"), lim, 5000);
        CHECK(a.ok && a.response.body == "hello");
        CHECK(h.ok && h.response.body.empty() && h.response.Get("content-length") && *h.response.Get("content-length") == "12345");
        CHECK(n.ok && n.response.status == 304 && n.response.body.empty());
        CHECK(g.ok && g.response.body == "hello" && c.opened == 1);
    }
    {   // a server that sends more than the answer (a second response glued on, or a body longer than its length) is not reused
        for (int variant = 0; variant < 2; ++variant) {
            PoolConn c;
            c.serve = [variant](int, int, const std::string&) -> std::string {
                if (variant == 0) return OkBody("one") + OkBody("TWO");   // pipelined leftover
                return "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabcdefgh";   // longer than the stated length
            };
            FetchResult a = Fetch(c, Rq("http://h.example/a"), lim, 5000);
            CHECK(a.ok && a.response.body.size() == 3 && c.given == 0);
            FetchResult b = Fetch(c, Rq("http://h.example/b"), lim, 5000);
            CHECK(b.ok && b.response.body.size() == 3 && c.opened == 2);   // a clean new connection; "TWO" was never taken as an answer
        }
    }
    {   // truncated bodies and a Content-Length longer than what arrives: errors, and nothing is kept
        for (int variant = 0; variant < 3; ++variant) {
            PoolConn c;
            c.serve = [variant](int, int, const std::string&) -> std::string {
                if (variant == 0) return "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\nshort";
                if (variant == 1) return "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhel";
                return "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n";   // the head never ends
            };
            FetchResult a = Fetch(c, Rq("http://h.example/a"), lim, 5000);
            CHECK(!a.ok && c.given == 0 && c.idle.empty());
        }
    }
    {   // a redirect loop on one kept connection still ends at the cap
        PoolConn c; c.serve = [](int, int, const std::string&) { return std::string("HTTP/1.1 302 Found\r\nLocation: /loop\r\nContent-Length: 0\r\n\r\n"); };
        Limits l2; l2.maxRedirects = 5;
        FetchResult r = Fetch(c, Rq("http://h.example/loop"), l2, 5000);
        CHECK(!r.ok && r.err == Err::TooManyRedirects && c.opened == 1);
    }
    {   // an idle connection that is no longer usable is skipped by the connector
        PoolConn c; c.serve = [](int, int, const std::string&) { return OkBody("ok"); };
        Fetch(c, Rq("http://h.example/a"), lim, 5000);
        static_cast<PoolConn::PS*>(c.idle[0].get())->dead = true;
        FetchResult b = Fetch(c, Rq("http://h.example/b"), lim, 5000);
        CHECK(b.ok && c.opened == 2 && b.reusedConnections == 0);
    }
}

static void TestCookies() {
    Limits lim;
    auto find = [](const std::string& wire, const char* name) { size_t p = wire.find(name); return p == std::string::npos ? std::string() : wire.substr(p, wire.find("\r\n", p) - p); };
    {   // off by default: Set-Cookie is ignored and nothing is sent
        PoolConn c;
        c.serve = [](int, int, const std::string& w) -> std::string {
            if (w.compare(0, 11, "GET /start ") == 0) return "HTTP/1.1 302 Found\r\nSet-Cookie: s=1\r\nLocation: /next\r\nContent-Length: 0\r\n\r\n";
            return OkBody("end");
        };
        FetchResult r = Fetch(c, Rq("http://h.example/start", "GET", false), lim, 5000);
        CHECK(r.ok && c.wires[1].find("Cookie:") == std::string::npos);
    }
    {   // on: the cookie from the redirect is sent to the next hop (same host), a caller Cookie header is merged
        PoolConn c;
        c.serve = [](int, int, const std::string& w) -> std::string {
            if (w.compare(0, 11, "GET /start ") == 0) return "HTTP/1.1 302 Found\r\nSet-Cookie: sid=abc; Path=/; HttpOnly\r\nSet-Cookie: t=2\r\nLocation: /next\r\nContent-Length: 0\r\n\r\n";
            return OkBody("end", "Set-Cookie: later=3\r\n");
        };
        FetchRequest q = Rq("http://h.example/start", "GET", false); q.jar = std::make_shared<CookieJar>();
        q.headerLines = {"Cookie: mine=0"};
        FetchResult r = Fetch(c, q, lim, 5000);
        CHECK(r.ok && find(c.wires[0], "Cookie:") == "Cookie: mine=0");
        const std::string ck = find(c.wires[1], "Cookie:");
        CHECK(ck.find("mine=0") != std::string::npos && ck.find("sid=abc") != std::string::npos && ck.find("t=2") != std::string::npos);
        CHECK(q.jar->Size() == 3);   // sid, t and the one from the final answer
    }
    {   // never sent to another host, nor (Secure) over http; a Secure cookie from http is refused outright
        CookieJar j; Url a, b, hs;
        ParseUrl("http://a.example/x/y", a); ParseUrl("http://b.example/x/y", b); ParseUrl("https://a.example/x/y", hs);
        CHECK(j.Store(a, "k=v"));
        CHECK(j.HeaderFor(a) == "k=v" && j.HeaderFor(b).empty());
        CHECK(!j.Store(a, "sec=1; Secure"));
        CHECK(j.Store(hs, "sec=1; Secure"));
        CHECK(j.HeaderFor(hs).find("sec=1") != std::string::npos && j.HeaderFor(a).find("sec=1") == std::string::npos);
    }
    {   // Domain: only the same host is accepted; other domains drop the cookie. Path scoping and default path.
        CookieJar j; Url a, p1, p2;
        ParseUrl("http://www.a.example/dir/page", a);
        CHECK(!j.Store(a, "d=1; Domain=a.example"));
        CHECK(!j.Store(a, "d=1; Domain=evil.example"));
        CHECK(j.Store(a, "d=1; Domain=.www.a.example"));
        CHECK(j.Store(a, "p=1; Path=/dir/sub"));
        ParseUrl("http://www.a.example/dir/other", p1); ParseUrl("http://www.a.example/dir/sub/x", p2);
        CHECK(j.HeaderFor(p1) == "d=1");                    // default path /dir covers /dir/other, p's path /dir/sub does not
        CHECK(j.HeaderFor(p2).find("p=1") != std::string::npos && j.HeaderFor(p2).find("d=1") != std::string::npos);
        Url p3; ParseUrl("http://www.a.example/dirx/y", p3);
        CHECK(j.HeaderFor(p3).empty());                      // "/dir" does not match "/dirx"
    }
    {   // Max-Age=0 and a negative one delete; same name+path replaces; junk is refused
        CookieJar j; Url a; ParseUrl("http://a.example/", a);
        CHECK(j.Store(a, "k=1")); CHECK(j.Store(a, "k=2")); CHECK(j.Size() == 1 && j.HeaderFor(a) == "k=2");
        CHECK(j.Store(a, "k=x; Max-Age=0")); CHECK(j.Size() == 0);
        CHECK(j.Store(a, "k=1")); CHECK(j.Store(a, "k=x; Max-Age=-5")); CHECK(j.Size() == 0);
        CHECK(!j.Store(a, "no-equals")); CHECK(!j.Store(a, "bad name=1")); CHECK(!j.Store(a, "=v"));
        CHECK(!j.Store(a, std::string("k=v\r\nSet-Cookie: x=y")));
        CHECK(!j.Store(a, "k=" + std::string(5000, 'a')));
        CHECK(j.Store(a, "e=1; Max-Age=3600") && j.HeaderFor(a) == "e=1");
        for (int i = 0; i < 100; ++i) j.Store(a, "n" + std::to_string(i) + "=1");
        CHECK(j.Size() <= CookieJar::kMaxCookies);
        CHECK(j.HeaderFor(a).size() <= CookieJar::kMaxHeaderBytes);
    }
    {   // a cookie set on one origin is not carried across a cross-origin redirect to a host that did not set it,
        // and the caller's own Cookie header is dropped there as before
        PoolConn c;
        c.serve = [](int, int, const std::string& w) -> std::string {
            if (w.compare(0, 11, "GET /start ") == 0) return "HTTP/1.1 302 Found\r\nSet-Cookie: a=1\r\nLocation: http://other.example/\r\nContent-Length: 0\r\n\r\n";
            return OkBody("end");
        };
        FetchRequest q = Rq("http://h.example/start", "GET", false); q.jar = std::make_shared<CookieJar>(); q.headerLines = {"Cookie: mine=0"};
        FetchResult r = Fetch(c, q, lim, 5000);
        CHECK(r.ok && c.wires[1].find("Cookie:") == std::string::npos);
    }
}

int main(int argc, char** argv) {
    int iters = 20000;
    if (const char* e = std::getenv("OKAY_FUZZ_ITERS")) iters = std::atoi(e);
    if (argc > 1) iters = std::atoi(argv[1]);
    TestUrl(); TestBuild(); TestParse(); TestFetch(); TestInflate(); TestContentCoding(); TestRange(); TestKeepAlive(); TestCookies(); Fuzz(iters);
    std::printf("own_http: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
