// Content delivery (shared/ContentManifest.hpp, ContentTransport.hpp, ContentSync.hpp, ContentOverlay.hpp). Docs: docs/release/content_delivery.md.
// Everything runs against a FAKE server (a map of names to bytes behind the Transport interface) and temp folders: no network, no real
// time (the retry wait is a recorded number), throw-away keys made here. Deterministic.
#include "test_framework.hpp"

#include "GameContent.hpp"
#include "ContentBackendStorage.hpp"
#include "ContentLiveConfig.hpp"
#include "ContentPolicy.hpp"
#include "ContentPublish.hpp"
#include "ContentOverlay.hpp"
#include "ContentSync.hpp"

#include <cstdio>
#include <map>
#include <set>
#include <sstream>
#include <chrono>

namespace fs = std::filesystem;
using namespace okaycontent;
using okayac::ed25519::Seed;

namespace {

Seed MakeSeed(std::uint8_t tag) {
    Seed s{};
    for (std::size_t i = 0; i < s.size(); ++i) s[i] = static_cast<std::uint8_t>(tag * 29u + i * 11u + 3u);
    return s;
}
std::string PubHex(const Seed& s) { const auto p = okayac::ed25519::PublicKeyFromSeed(s); return okayac::ToHex(p.data(), p.size()); }

std::string Bytes(char fill, std::size_t n) { std::string s(n, fill); for (std::size_t i = 0; i < n; i += 7) s[i] = static_cast<char>('a' + (i % 23)); return s; }

// ---- fake server ----------------------------------------------------------------------------------------------------------------
struct FakeServer : Transport {
    std::map<std::string, std::string> files;
    struct Req { std::string name; std::uint64_t from; };
    std::vector<Req> log;
    // fault injection (per file name)
    std::map<std::string, int> fail503Times;          // answer 503 this many times, then behave
    std::map<std::string, std::size_t> cutAfter;      // first time: send only this many bytes then drop the connection
    std::map<std::string, bool> ignoreRange;          // always answer 200 with the whole file
    std::map<std::string, std::string> replaceBody;   // serve these bytes instead (same or other size)
    std::map<std::string, bool> wrongContentRange;    // 206 whose body starts at the wrong place
    std::function<void(const std::string&)> onGet;
    int dropAll = 0;                                  // number of calls that fail with "no network" first

    GetResult Get(const std::string& name, std::uint64_t from, std::uint64_t maxBytes, const GetHandlers& h) override {
        log.push_back({name, from});
        if (onGet) onGet(name);
        GetResult r;
        if (dropAll > 0) { --dropAll; r.error = "no network"; return r; }
        auto f = files.find(name);
        GetHead head;
        if (fail503Times[name] > 0) { --fail503Times[name]; head.status = 503; r.reached = true; if (h.onHead && !h.onHead(head)) { r.aborted = true; r.reached = false; } return r; }
        if (f == files.end()) { head.status = 404; r.reached = true; if (h.onHead && !h.onHead(head)) { r.aborted = true; r.reached = false; } return r; }
        std::string body = replaceBody.count(name) ? replaceBody[name] : f->second;
        head.totalSize = body.size();
        std::uint64_t start = 0;
        if (from > 0 && !ignoreRange[name]) {
            if (from >= body.size()) { head.status = 416; r.reached = true; if (h.onHead && !h.onHead(head)) { r.aborted = true; r.reached = false; } return r; }
            head.status = 206; head.bodyStart = wrongContentRange[name] ? from + 1 : from; start = from;
        } else head.status = 200;
        if (h.onHead && !h.onHead(head)) { r.aborted = true; return r; }
        std::string out = body.substr(static_cast<std::size_t>(start));
        if (cutAfter.count(name)) { const std::size_t n = cutAfter[name]; cutAfter.erase(name); out.resize(std::min(out.size(), n)); r.error = "connection dropped"; if (h.onData) h.onData(reinterpret_cast<const std::uint8_t*>(out.data()), out.size()); return r; }
        (void)maxBytes;
        // deliver in 100-byte pieces
        for (std::size_t i = 0; i < out.size(); i += 100) {
            const std::size_t n = std::min<std::size_t>(100, out.size() - i);
            if (h.onData && !h.onData(reinterpret_cast<const std::uint8_t*>(out.data() + i), n)) { r.aborted = true; return r; }
        }
        r.reached = true;
        return r;
    }
    const char* Describe() const override { return "fake"; }
    int Count(const std::string& name) const { int n = 0; for (const Req& r : log) if (r.name == name) ++n; return n; }
    int FileGets() const { int n = 0; for (const Req& r : log) if (r.name != kManifestName && r.name != kSigName) ++n; return n; }
};

struct Publisher {
    Seed seed;
    std::string game = "testgame";
    std::map<std::string, std::pair<std::string, Kind>> content;   // path -> bytes, kind
    std::map<std::string, std::string> groups, minApps;
    std::string manifestMinApp;
    Publisher() : seed(MakeSeed(1)) {}
    void Publish(FakeServer& s, std::uint64_t version) {
        Manifest m; m.game = game; m.version = version; m.minApp = manifestMinApp;
        s.files.clear();
        for (auto& [p, c] : content) {
            Entry e; e.path = p; e.sha256 = Sha256Hex(c.first); e.size = c.first.size(); e.kind = c.second;
            if (groups.count(p)) e.group = groups[p];
            if (minApps.count(p)) e.minApp = minApps[p];
            m.entries.push_back(e);
            s.files[p] = c.first;
        }
        const std::string text = Serialize(m);
        s.files[kManifestName] = text;
        s.files[kSigName] = MakeSignatureFile(text, seed);
    }
};

std::string Slurp(const fs::path& p) { std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }

struct Env {
    fs::path dir;
    explicit Env(const char* tag) {
        dir = fs::temp_directory_path() / ("okay_content_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" + tag);
        std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir, ec);
    }
    ~Env() { std::error_code ec; fs::remove_all(dir, ec); }
    std::string Root() const { return (dir / "content").string(); }
    std::string Install() const { return (dir / "install").string(); }
};

SyncConfig Cfg(const Env& e, const Publisher& p) {
    SyncConfig c;
    c.root = e.Root(); c.game = p.game; c.appVersion = "2.900.0"; c.trustedKeys = PubHex(p.seed);
    c.retry.maxAttempts = 4;
    return c;
}

// ---- Live Updates helpers (local folder provider, a cut connection, an in-memory OkayBackend Storage) ----------------------------
void WriteFile(const fs::path& p, const std::string& bytes) { fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary | std::ios::trunc) << bytes; }

// A publisher's work folder: files on disk, a manifest made by ScanFolder, signed with the throw-away seed.
struct LiveFolder {
    fs::path dir;
    Seed seed = MakeSeed(1);
    std::string game = "testgame";
    Manifest Make(std::uint64_t version, std::string* sig) {
        std::vector<std::string> problems;
        Manifest m; m.game = game; m.version = version;
        m.entries = ScanFolder(dir.u8string(), {}, problems);
        if (sig) *sig = MakeSignatureFile(Serialize(m), seed);
        return m;
    }
};

// Wraps any provider; the first GET of `name` delivers only `cut` bytes, then the connection "drops".
struct CutOnce : Transport {
    Transport& inner; std::string name; std::size_t cut; bool done = false;
    std::vector<std::pair<std::string, std::uint64_t>> gets;
    CutOnce(Transport& t, std::string n, std::size_t c) : inner(t), name(std::move(n)), cut(c) {}
    GetResult Get(const std::string& n, std::uint64_t from, std::uint64_t maxBytes, const GetHandlers& h) override {
        gets.emplace_back(n, from);
        if (n != name || done) return inner.Get(n, from, maxBytes, h);
        done = true;
        std::size_t sent = 0;
        GetHandlers w = h;
        w.onData = [&](const std::uint8_t* d, std::size_t k) { const std::size_t take = std::min(k, cut - sent); sent += take; if (take && h.onData) h.onData(d, take); return sent < cut; };
        GetResult r = inner.Get(n, from, maxBytes, w);
        r.reached = false; r.aborted = false; r.error = "connection dropped";
        return r;
    }
    const char* Describe() const override { return "cut-once"; }
};

// OkayBackend Storage in memory: what a bucket holds, answered the way the Storage interface describes (a folder has no "id").
struct MemStorage : okay::backend::Storage {
    std::map<std::string, std::string> objects;   // "bucket/path" -> bytes
    int uploads = 0;
    static std::string Decode(const std::string& p) {   // the server stores the name, not its URL spelling
        std::string o;
        for (std::size_t i = 0; i < p.size(); ++i) {
            if (p[i] == '%' && i + 2 < p.size()) { o += static_cast<char>(std::stoi(p.substr(i + 1, 2), nullptr, 16)); i += 2; } else o += p[i];
        }
        return o;
    }
    okay::backend::Response Ok(std::string body) { okay::backend::Response r; r.ok = r.reached = true; r.status = 200; r.body = std::move(body); return r; }
    okay::backend::Response Upload(const std::string& b, const std::string& p, const std::string& f, const std::string&, const okay::backend::UploadOptions&) override {
        ++uploads; objects[b + "/" + Decode(p)] = Slurp(f); return Ok("{}");
    }
    okay::backend::Response ReadPublic(const std::string& b, const std::string& p, const std::string&) override {
        auto it = objects.find(b + "/" + Decode(p));
        if (it == objects.end()) { okay::backend::Response r; r.reached = true; r.status = 404; return r; }
        return Ok(it->second);
    }
    okay::backend::Response List(const std::string& b, const okay::backend::ListOptions& o) override {
        std::set<std::string> folders; std::vector<std::pair<std::string, std::size_t>> files;
        const std::string pre = b + "/" + o.prefix;
        for (auto& [k, v] : objects) {
            if (k.compare(0, pre.size(), pre) != 0) continue;
            const std::string rest = k.substr(pre.size());
            const std::size_t sl = rest.find('/');
            if (sl == std::string::npos) files.emplace_back(rest, v.size()); else folders.insert(rest.substr(0, sl));
        }
        std::vector<std::string> items;
        for (const std::string& f : folders) items.push_back("{\"name\":\"" + f + "\",\"id\":null}");
        for (auto& [n, sz] : files) items.push_back("{\"name\":\"" + n + "\",\"id\":\"x\",\"metadata\":{\"size\":" + std::to_string(sz) + "}}");
        std::string body = "[";
        for (std::size_t i = static_cast<std::size_t>(o.offset); i < items.size() && i < static_cast<std::size_t>(o.offset + o.limit); ++i) body += (body.size() > 1 ? "," : "") + items[i];
        return Ok(body + "]");
    }
    okay::backend::Response Remove(const std::string&, const std::vector<std::string>&) override { return Ok("[]"); }
    okay::backend::Response RemoveObject(const std::string&, const std::string&) override { return Ok("{}"); }
    std::string PublicUrl(const std::string& b, const std::string& p) const override { return "mem://" + b + "/" + p; }
};

}  // namespace

int main() {
    // ---------------------------------------------------------------------------------------------------------------- manifest
    {
        RUN_SUITE("manifest format");
        Manifest m; m.game = "g1"; m.version = 7; m.minApp = "2.886.0";
        for (const char* p : {"models/ship.glb", "textures/a b.png", "scripts/boss.okayvs"}) {
            Entry e; e.path = p; e.sha256 = Sha256Hex(p); e.size = 5; e.kind = Kind::Data; m.entries.push_back(e);
        }
        m.entries[0].kind = Kind::Model; m.entries[2].kind = Kind::Script; m.entries[1].group = "hd"; m.entries[1].minApp = "2.890.0";
        const std::string text = Serialize(m);
        Manifest back; std::string why;
        CHECK(Parse(text, back, &why));
        CHECK(Serialize(back) == text);                      // one canonical spelling
        CHECK(back.entries.size() == 3 && back.entries[0].path == "models/ship.glb" && back.version == 7 && back.minApp == "2.886.0");
        CHECK(back.entries[2].group == "hd" && back.entries[2].minApp == "2.890.0" && back.entries[1].group.empty());

        auto bad = [&](const std::string& t) { Manifest x; std::string w; const bool ok = Parse(t, x, &w); return !ok && !w.empty() && x.entries.empty(); };
        const std::string sha(64, 'a');
        const std::string head = "okay-content 1\ngame g\nversion 1\n";
        auto line = [&](const std::string& path, const std::string& kind = "data", const std::string& size = "1", const std::string& s = std::string(64, 'a')) {
            return "file\t" + s + "\t" + size + "\t" + kind + "\t-\t-\t" + path + "\n";
        };
        CHECK(!bad(head + line("a.txt")));
        CHECK(bad(head + line("../a.txt")));
        CHECK(bad(head + line("a/../b.txt")));
        CHECK(bad(head + line("/abs.txt")));
        CHECK(bad(head + line("C:/x.txt")));
        CHECK(bad(head + line("C:x.txt")));
        CHECK(bad(head + line("a.txt:stream")));
        CHECK(bad(head + line("nul.txt")));
        CHECK(bad(head + line("sub/CON")));
        CHECK(bad(head + line("a\\b.txt")));
        CHECK(bad(head + line("a//b.txt")));
        CHECK(bad(head + line("a/./b.txt")));
        CHECK(bad(head + line(".hidden")));
        CHECK(bad(head + line("dir/")));
        CHECK(bad(head + line("trail.")));
        CHECK(bad(head + line("a.txt", "exe")));            // programs are not a kind
        CHECK(bad(head + line("a.txt", "dll")));
        for (const char* n : {"tools/run.exe", "x/Lib.DLL", "a.so", "a.dylib", "run.bat", "go.cmd", "s.ps1", "p.msi", "x.appx", "run.sh", "m.wasm", "a.jar", "x.lnk"}) CHECK(bad(head + line(n)));   // native code is never content, even with kind data
        CHECK(!bad(head + line("scripts/boss.okayvs", "script")));
        CHECK(bad(head + line("a.txt", "data", "01")));     // leading zero
        CHECK(bad(head + line("a.txt", "data", "-1")));
        CHECK(bad(head + line("a.txt", "data", "1", std::string(64, 'A'))));   // uppercase hex
        CHECK(bad(head + line("a.txt", "data", "1", std::string(63, 'a'))));
        CHECK(bad(head + line("a.txt") + line("a.txt")));  // duplicate
        CHECK(bad(head + line("b.txt") + line("a.txt")));  // not in order
        CHECK(bad(head + line("A.txt") + line("a.txt")));  // differ only by case ('A' < 'a' so the order is fine; the case rule fires)
        CHECK(bad(head + line("a") + line("a/b")));        // a file and a folder
        CHECK(bad(head + "file\tonly\tthree\n"));
        CHECK(bad(head + line("a.txt").substr(0, line("a.txt").size() - 1)));   // no final newline
        CHECK(bad("okay-content 2\ngame g\nversion 1\n"));
        CHECK(bad("okay-content 1\ngame G!\nversion 1\n"));
        CHECK(bad("okay-content 1\nversion 1\n"));
        CHECK(bad("okay-content 1\ngame g\nversion 0\n"));
        CHECK(bad("okay-content 1\ngame g\nversion 1\nversion 2\n"));
        CHECK(bad("okay-content 1\ngame g\nversion 1\nmin-app dev\n"));
        CHECK(bad("okay-content 1\r\ngame g\nversion 1\n"));
        CHECK(bad(std::string("okay-content 1\ngame g\nversion 1\n") + "file\t" + sha + "\t1\tdata\t-\t-\tz\x01.txt\n"));
        CHECK(bad(head + "\n"));                            // blank line
        CHECK(bad(head + line("a.txt", "data", "1", sha) + "junk\n"));
        // limits
        Limits lim; lim.maxEntries = 2;
        { Manifest x; std::string w; CHECK(!Parse(head + line("a") + line("b") + line("c"), x, &w, lim)); }
        lim = Limits(); lim.maxFileBytes = 10; lim.maxTotalBytes = 15;
        { Manifest x; std::string w; CHECK(!Parse(head + line("a", "data", "11"), x, &w, lim)); }
        { Manifest x; std::string w; CHECK(!Parse(head + line("a", "data", "8") + line("b", "data", "8"), x, &w, lim)); }
        { Manifest x; std::string w; CHECK(Parse(head + line("a", "data", "8") + line("b", "data", "7"), x, &w, lim)); }
        lim = Limits(); lim.maxManifestBytes = 20;
        { Manifest x; std::string w; CHECK(!Parse(head + line("a"), x, &w, lim)); }
        { Manifest x; std::string w; CHECK(!Parse(std::string(), x, &w)); }
        // Windows-hostile names stay refused even though Linux would store them
        for (const char* n : {"COM1", "lpt9.txt", "aux.dat", "a/prn", "x/NUL.tga", "name ", "q?.png", "q*.png", "p|q"}) CHECK(!PathProblem(n).empty());
        CHECK(PathProblem("models/ok name.glb").empty());
    }

    // ------------------------------------------------------------------------------------------------------------------ signature
    {
        RUN_SUITE("manifest signature");
        const Seed a = MakeSeed(1), b = MakeSeed(2);
        const std::string body = "okay-content 1\ngame g\nversion 1\n";
        const std::string sigA = MakeSignatureFile(body, a);
        CHECK(CheckSignature(PubHex(a), body, sigA).kind == SigKind::Valid);
        CHECK(CheckSignature(PubHex(b), body, sigA).kind == SigKind::Bad);                      // another key
        CHECK(CheckSignature(PubHex(b) + "," + PubHex(a), body, sigA).keyIndex == 1);           // rotation: either key
        CHECK(CheckSignature(PubHex(a), body + "x", sigA).kind == SigKind::Bad);                // one byte changed
        CHECK(CheckSignature(PubHex(a), body, "").kind == SigKind::Missing);
        CHECK(CheckSignature(PubHex(a), body, "okay-sig1 zz").kind == SigKind::Bad);
        CHECK(CheckSignature("", body, sigA).kind == SigKind::NoKeys && !CheckSignature("", body, sigA).Accepts());   // no key: REFUSED, no transition mode
        CHECK(CheckSignature("nothex", body, sigA).kind == SigKind::KeysInvalid);
        CHECK(CheckSignature("", body, "", true).Accepts());                                    // the explicit developer switch only
        // domain separation: a signature over the same bytes made for the release index does not verify here
        const std::string idx = okayupdate::MakeSignatureFile(body, a);
        CHECK(CheckSignature(PubHex(a), body, idx).kind == SigKind::Bad);
        CHECK(okayupdate::Check(PubHex(a), body, sigA).kind == okayupdate::Kind::Bad);          // and the other way round
    }

    // ------------------------------------------------------------------------------------------------------------ transport helpers
    {
        RUN_SUITE("transport helpers");
        CHECK(JoinUrl("https://h/c/", "models/my ship#1.glb") == "https://h/c/models/my%20ship%231.glb");
        CHECK(FileUrlFolder("file:///C:/x/y") == "C:/x/y");
        CHECK(FileUrlFolder("file:///home/u/c") == "/home/u/c");
        CHECK(FileUrlFolder("https://x").empty());
        RetryPolicy rp; rp.baseDelayMs = 500; rp.maxDelayMs = 3000;
        CHECK(rp.DelayBeforeRetry(1) == 500 && rp.DelayBeforeRetry(2) == 1000 && rp.DelayBeforeRetry(3) == 2000 && rp.DelayBeforeRetry(4) == 3000 && rp.DelayBeforeRetry(40) == 3000);
        Env env("ft");
        fs::create_directories(env.dir / "srv" / "a");
        { std::ofstream(env.dir / "srv" / "a" / "f.bin", std::ios::binary) << "0123456789"; }
        FileTransport ft((env.dir / "srv").string(), 4);
        std::string got; GetHead hd;
        GetHandlers h; h.onHead = [&](const GetHead& x) { hd = x; return true; }; h.onData = [&](const std::uint8_t* d, std::size_t n) { got.append((const char*)d, n); return true; };
        CHECK(ft.Get("a/f.bin", 0, 0, h).reached && got == "0123456789" && hd.status == 200 && hd.totalSize == 10);
        got.clear();
        CHECK(ft.Get("a/f.bin", 6, 0, h).reached && got == "6789" && hd.status == 206 && hd.bodyStart == 6);
        got.clear();
        CHECK(ft.Get("a/f.bin", 10, 0, h).reached && hd.status == 416 && got.empty());
        CHECK(ft.Get("a/none.bin", 0, 0, h).reached && hd.status == 404);
        CHECK(!ft.Get("../x", 0, 0, h).reached);
        CHECK(!ft.Get("a/f.bin", 0, 5, h).reached);   // body bigger than the cap
    }

    // ------------------------------------------------------------------------------------------------------------------- sync basics
    {
        RUN_SUITE("sync: fresh, up to date, changed files only");
        Env env("sync1");
        FakeServer srv; Publisher pub;
        pub.content["models/ship.glb"] = {Bytes('m', 1000), Kind::Model};
        pub.content["textures/ship.png"] = {Bytes('t', 2500), Kind::Texture};
        pub.content["scripts/boss.okayvs"] = {"# graph\n", Kind::Script};
        pub.content["audio/hit.wav"] = {Bytes('w', 333), Kind::Audio};
        pub.Publish(srv, 1);
        std::vector<std::string> lines;
        SyncConfig cfg = Cfg(env, pub); cfg.log = [&](const std::string& l) { lines.push_back(l); };
        std::vector<Phase> phases;
        cfg.progress = [&](const Progress& p) { if (phases.empty() || phases.back() != p.phase) phases.push_back(p.phase); return true; };
        SyncResult r = Sync(cfg, srv).Run();
        CHECK(r.status == Status::Updated && r.version == 1 && r.filesDownloaded == 4 && r.filesReused == 0 && r.bytesDownloaded == 1000 + 2500 + 8 + 333);
        CHECK(Slurp(fs::path(env.Root()) / "trees" / "1" / "models" / "ship.glb") == Bytes('m', 1000));
        CHECK(Slurp(fs::path(env.Root()) / "trees" / "1" / "scripts" / "boss.okayvs") == "# graph\n");
        CHECK(!fs::exists(fs::path(env.Root()) / "staging"));        // nothing left behind
        CHECK(!fs::exists(fs::path(env.Root()) / "trees" / "1.tmp"));
        CHECK(phases.front() == Phase::Manifest && phases.back() == Phase::Done);
        CHECK(std::find(phases.begin(), phases.end(), Phase::Downloading) != phases.end());
        Commit c; CHECK(ReadCommit(StorePaths{env.Root()}, c) && c.version == 1 && c.floor == 1 && c.previous == 0);

        // second run: nothing to do, no file requests at all
        srv.log.clear();
        r = Sync(cfg, srv).Run();
        CHECK(r.status == Status::UpToDate && srv.FileGets() == 0);

        // version 2: one file changed, one added, one removed
        pub.content["textures/ship.png"] = {Bytes('T', 2600), Kind::Texture};
        pub.content["data/items.okaydata"] = {"hp=5\n", Kind::Data};
        pub.content.erase("audio/hit.wav");
        pub.Publish(srv, 2); srv.log.clear();
        r = Sync(cfg, srv).Run();
        CHECK(r.status == Status::Updated && r.version == 2 && r.filesDownloaded == 2 && r.filesReused == 2);
        CHECK(srv.Count("models/ship.glb") == 0 && srv.Count("scripts/boss.okayvs") == 0 && srv.Count("textures/ship.png") == 1);
        CHECK(Slurp(fs::path(env.Root()) / "trees" / "2" / "textures" / "ship.png") == Bytes('T', 2600));
        CHECK(!fs::exists(fs::path(env.Root()) / "trees" / "2" / "audio" / "hit.wav"));
        CHECK(fs::exists(fs::path(env.Root()) / "trees" / "1"));       // previous version kept for rollback
        CHECK(ReadCommit(StorePaths{env.Root()}, c) && c.version == 2 && c.previous == 1 && c.floor == 2);

        // version 3: the version before the previous one is removed, and the result SAYS so
        pub.content["data/items.okaydata"] = {"hp=6\n", Kind::Data};
        pub.Publish(srv, 3);
        r = Sync(cfg, srv).Run();
        CHECK(r.status == Status::Updated && !fs::exists(fs::path(env.Root()) / "trees" / "1") && fs::exists(fs::path(env.Root()) / "trees" / "2"));
        CHECK(r.removed.size() == 1 && r.removed[0].find("version 1") != std::string::npos);

        // rollback: version 2 is visible again, floor stays 3 (so the server cannot replay 2 as an upgrade path to nothing)
        std::string why;
        CHECK(Rollback(StorePaths{env.Root()}, &why));
        CHECK(ReadCommit(StorePaths{env.Root()}, c) && c.version == 2 && c.floor == 3 && c.previous == 0);
        CHECK(!Rollback(StorePaths{env.Root()}, &why) && !why.empty());
        Overlay ov; CHECK(ov.Load(env.Root(), pub.game, PubHex(pub.seed)) && ov.Version() == 2);
        CHECK(Slurp(ov.Find("data/items.okaydata")) == "hp=5\n");
    }

    {
        RUN_SUITE("sync: same bytes under two names download once");
        Env env("dup");
        FakeServer srv; Publisher pub;
        pub.content["a/one.png"] = {Bytes('x', 700), Kind::Texture};
        pub.content["b/two.png"] = {Bytes('x', 700), Kind::Texture};
        pub.Publish(srv, 1);
        SyncResult r = Sync(Cfg(env, pub), srv).Run();
        CHECK(r.status == Status::Updated && r.filesDownloaded == 1 && r.filesReused == 1);
        CHECK(Slurp(fs::path(env.Root()) / "trees" / "1" / "b" / "two.png") == Bytes('x', 700));
    }

    // ---------------------------------------------------------------------------------------------------- resume, retry, backoff
    {
        RUN_SUITE("sync: resume, retry, backoff");
        Env env("resume");
        FakeServer srv; Publisher pub;
        pub.content["models/big.glb"] = {Bytes('b', 5000), Kind::Model};
        pub.Publish(srv, 1);
        srv.cutAfter["models/big.glb"] = 1800;
        SyncConfig cfg = Cfg(env, pub);
        std::vector<int> waits; cfg.sleepMs = [&](int ms) { waits.push_back(ms); };
        SyncResult r = Sync(cfg, srv).Run();
        CHECK(r.status == Status::Updated && r.retries == 1 && waits.size() == 1 && waits[0] == 500);
        // the second GET asked for the rest of the file (Range), it did not start over
        CHECK(srv.log.size() >= 4 && srv.log[srv.log.size() - 2].name == "models/big.glb" && srv.log.back().from == 1800);
        CHECK(Slurp(fs::path(env.Root()) / "trees" / "1" / "models" / "big.glb") == Bytes('b', 5000));

        // server errors: 503 twice then fine -> waits 500, 1000 (doubling)
        Env env2("retry");
        FakeServer s2; pub.Publish(s2, 1);
        s2.fail503Times["models/big.glb"] = 2;
        cfg = Cfg(env2, pub); waits.clear(); cfg.sleepMs = [&](int ms) { waits.push_back(ms); };
        r = Sync(cfg, s2).Run();
        CHECK(r.status == Status::Updated && r.retries == 2 && waits == std::vector<int>({500, 1000}));

        // permanent 503: gives up after maxAttempts with a network reason, nothing visible
        Env env3("giveup");
        FakeServer s3; pub.Publish(s3, 1);
        s3.fail503Times["models/big.glb"] = 99;
        cfg = Cfg(env3, pub); waits.clear(); cfg.sleepMs = [&](int ms) { waits.push_back(ms); };
        r = Sync(cfg, s3).Run();
        CHECK(r.status == Status::Failed && r.reason == Reason::Network && waits == std::vector<int>({500, 1000, 2000}));
        Commit c; CHECK(!ReadCommit(StorePaths{env3.Root()}, c));
        { Overlay ov; CHECK(!ov.Load(env3.Root(), pub.game, PubHex(pub.seed)) && !ov.Active()); }

        // no network at all for the manifest
        Env env4("nonet");
        FakeServer s4; pub.Publish(s4, 1); s4.dropAll = 99;
        r = Sync(Cfg(env4, pub), s4).Run();
        CHECK(r.status == Status::Failed && r.reason == Reason::Network);

        // the server ignores Range and sends 200: the partial file is restarted, never spliced
        Env env5("norange");
        FakeServer s5; pub.Publish(s5, 1);
        s5.cutAfter["models/big.glb"] = 900; s5.ignoreRange["models/big.glb"] = true;
        r = Sync(Cfg(env5, pub), s5).Run();
        CHECK(r.status == Status::Updated && Slurp(fs::path(env5.Root()) / "trees" / "1" / "models" / "big.glb") == Bytes('b', 5000));

        // a 206 that starts at the wrong place is never appended; the file is fetched again from the start
        Env env6("badrange");
        FakeServer s6; pub.Publish(s6, 1);
        s6.cutAfter["models/big.glb"] = 900; s6.wrongContentRange["models/big.glb"] = true;
        r = Sync(Cfg(env6, pub), s6).Run();
        // the bad 206 is thrown away (never spliced in) and the file is fetched again from the start
        CHECK(r.status == Status::Updated && Slurp(fs::path(env6.Root()) / "trees" / "1" / "models" / "big.glb") == Bytes('b', 5000));
        CHECK(s6.Count("models/big.glb") == 3);
    }

    {
        RUN_SUITE("sync: cancel keeps the partial file; the next run resumes it");
        Env env("cancel");
        FakeServer srv; Publisher pub;
        pub.content["models/big.glb"] = {Bytes('c', 4000), Kind::Model};
        pub.Publish(srv, 1);
        SyncConfig cfg = Cfg(env, pub);
        int pieces = 0;
        cfg.progress = [&](const Progress& p) { if (p.phase == Phase::Downloading && ++pieces == 10) return false; return true; };
        SyncResult r = Sync(cfg, srv).Run();
        CHECK(r.status == Status::Cancelled && r.reason == Reason::Cancelled);
        Commit c; CHECK(!ReadCommit(StorePaths{env.Root()}, c));
        const fs::path part = fs::path(env.Root()) / "staging" / (Sha256Hex(Bytes('c', 4000)) + ".part");
        CHECK(fs::exists(part) && fs::file_size(part) >= 1000 && fs::file_size(part) < 4000);
        const std::uint64_t partSize = fs::file_size(part);
        cfg.progress = nullptr; srv.log.clear();
        r = Sync(cfg, srv).Run();
        CHECK(r.status == Status::Updated && srv.log.back().name == "models/big.glb" && srv.log.back().from == partSize);   // resumed from the partial file
        CHECK(Slurp(fs::path(env.Root()) / "trees" / "1" / "models" / "big.glb") == Bytes('c', 4000));
    }

    // ------------------------------------------------------------------------------------------------------- corrupt and hostile
    {
        RUN_SUITE("sync: corrupt, tampered and hostile servers");
        Env env("corrupt");
        FakeServer srv; Publisher pub;
        pub.content["models/a.glb"] = {Bytes('a', 800), Kind::Model};
        pub.content["models/b.glb"] = {Bytes('b', 800), Kind::Model};
        pub.Publish(srv, 1);
        CHECK(Sync(Cfg(env, pub), srv).Run().status == Status::Updated);   // version 1 is good and visible
        Overlay ov0; CHECK(ov0.Load(env.Root(), pub.game, PubHex(pub.seed)));

        // version 2: b.glb is served with bytes of the right size but wrong content (a tampered CDN)
        pub.content["models/b.glb"] = {Bytes('B', 800), Kind::Model};
        pub.Publish(srv, 2);
        std::string evil = Bytes('B', 800); evil[400] ^= 1;
        srv.replaceBody["models/b.glb"] = evil;
        SyncResult r = Sync(Cfg(env, pub), srv).Run();
        CHECK(r.status == Status::Failed && r.reason == Reason::FileCorrupt && r.corruptRejected == 4);
        Commit c; CHECK(ReadCommit(StorePaths{env.Root()}, c) && c.version == 1);         // old version still the visible one
        CHECK(!fs::exists(fs::path(env.Root()) / "trees" / "2"));                          // the bad file never became visible
        { Overlay ov; CHECK(ov.Load(env.Root(), pub.game, PubHex(pub.seed)) && ov.Version() == 1 && Slurp(ov.Find("models/b.glb")) == Bytes('b', 800)); }
        { std::string leftover; for (auto& f : fs::recursive_directory_iterator(env.Root())) leftover += f.path().string() + "\n";
          CHECK(leftover.find("evil") == std::string::npos); CHECK(!fs::exists(fs::path(env.Root()) / "staging" / (Sha256Hex(Bytes('B', 800)) + ".part"))); }

        // one bad try, then the CDN heals: accepted (the bad bytes were thrown away, not reused)
        srv.replaceBody.clear();
        r = Sync(Cfg(env, pub), srv).Run();
        CHECK(r.status == Status::Updated && r.version == 2);

        // a server that sends MORE bytes than the manifest says is cut off at the size; the disk never fills
        Env env2("overlong");
        FakeServer s2; pub.Publish(s2, 1);
        s2.replaceBody["models/a.glb"] = Bytes('a', 800) + Bytes('z', 100000);
        r = Sync(Cfg(env2, pub), s2).Run();
        CHECK(r.status == Status::Failed && r.reason == Reason::FileCorrupt);
        { std::error_code ec; std::uintmax_t total = 0; for (auto& f : fs::recursive_directory_iterator(env2.Root(), ec)) if (f.is_regular_file()) total += f.file_size(); CHECK(total < 1700); }

        // 404 for a file the manifest lists: plain failure, no retry storm
        Env env3("missing");
        FakeServer s3; pub.Publish(s3, 1); s3.files.erase("models/b.glb");
        r = Sync(Cfg(env3, pub), s3).Run();
        CHECK(r.status == Status::Failed && r.reason == Reason::ServerMissingFile && s3.Count("models/b.glb") == 1);
    }

    // ------------------------------------------------------------------------------------------- manifest-level refusals
    {
        RUN_SUITE("sync: signature, game, version floor, app version, budget, groups");
        Env env("refuse");
        FakeServer srv; Publisher pub;
        pub.content["data/x.okaydata"] = {"x=1\n", Kind::Data};
        pub.content["textures/hd.png"] = {Bytes('h', 600), Kind::Texture};
        pub.groups["textures/hd.png"] = "hd";
        pub.Publish(srv, 5);

        // unsigned / wrongly signed / tampered
        { FakeServer s = srv; s.files.erase(kSigName); SyncResult r = Sync(Cfg(env, pub), s).Run(); CHECK(r.reason == Reason::SignatureBad && r.status == Status::Failed && s.FileGets() == 0); }
        { FakeServer s = srv; s.files[kSigName] = MakeSignatureFile(s.files[kManifestName], MakeSeed(9)); SyncResult r = Sync(Cfg(env, pub), s).Run(); CHECK(r.reason == Reason::SignatureBad && s.FileGets() == 0); }
        { FakeServer s = srv; std::string m = s.files[kManifestName]; const std::size_t p = m.find("version 5"); m[p + 8] = '6'; s.files[kManifestName] = m;
          SyncResult r = Sync(Cfg(env, pub), s).Run(); CHECK(r.reason == Reason::SignatureBad); }
        { SyncConfig c = Cfg(env, pub); c.trustedKeys.clear(); SyncResult r = Sync(c, srv).Run(); CHECK(r.reason == Reason::SignatureBad); }   // no key = refuse
        { SyncConfig c = Cfg(env, pub); c.trustedKeys.clear(); c.allowUnsigned = true; FakeServer s = srv; s.files.erase(kSigName);
          Env e2("unsigned"); c.root = e2.Root(); CHECK(Sync(c, s).Run().status == Status::Updated); }
        // wrong game
        { SyncConfig c = Cfg(env, pub); c.game = "othergame"; SyncResult r = Sync(c, srv).Run(); CHECK(r.reason == Reason::GameMismatch); }
        // app too old for the manifest
        { Publisher p2 = pub; p2.manifestMinApp = "3.0.0"; FakeServer s; p2.Publish(s, 5); SyncResult r = Sync(Cfg(env, p2), s).Run(); CHECK(r.reason == Reason::AppTooOld); }
        // the manifest a dev app (no version) cannot meet min-app
        { Publisher p2 = pub; p2.manifestMinApp = "2.0.0"; FakeServer s; p2.Publish(s, 5); SyncConfig c = Cfg(env, p2); c.appVersion.clear(); CHECK(Sync(c, s).Run().reason == Reason::AppTooOld); }
        CHECK(!fs::exists(fs::path(env.Root()) / "current.txt"));

        // groups: only the wanted group plus ungrouped files
        { Env e2("groups"); SyncConfig c = Cfg(env, pub); c.root = e2.Root(); c.groups = {"sd"};
          SyncResult r = Sync(c, srv).Run(); CHECK(r.status == Status::Updated && r.filesSkipped == 1 && r.filesDownloaded == 1 && !fs::exists(fs::path(e2.Root()) / "trees" / "5" / "textures" / "hd.png")); }
        // per-entry min-app
        { Publisher p2 = pub; p2.minApps["textures/hd.png"] = "2.950.0"; FakeServer s; p2.Publish(s, 5); Env e2("entryminapp"); SyncConfig c = Cfg(e2, p2);
          SyncResult r = Sync(c, s).Run(); CHECK(r.status == Status::Updated && r.filesSkipped == 1); c.appVersion = "2.950.0"; Env e3("entryminapp2"); c.root = e3.Root();
          r = Sync(c, s).Run(); CHECK(r.filesSkipped == 0 && r.filesDownloaded == 2); }

        // good sync, then replay floor
        CHECK(Sync(Cfg(env, pub), srv).Run().status == Status::Updated);
        { FakeServer old = srv; Publisher p3 = pub; p3.content["data/x.okaydata"] = {"x=0\n", Kind::Data}; p3.Publish(old, 4);   // a really signed OLD version
          SyncResult r = Sync(Cfg(env, pub), old).Run(); CHECK(r.status == Status::Failed && r.reason == Reason::Downgrade);
          SyncConfig c = Cfg(env, pub); c.allowDowngrade = true; CHECK(Sync(c, old).Run().status == Status::Updated); }
        // same version number, different bytes: refused
        { Env e2("samever"); FakeServer s; Publisher p3 = pub; p3.Publish(s, 5); CHECK(Sync(Cfg(e2, pub), s).Run().status == Status::Updated);
          p3.content["data/x.okaydata"] = {"x=2\n", Kind::Data}; p3.Publish(s, 5); SyncResult r = Sync(Cfg(e2, pub), s).Run(); CHECK(r.reason == Reason::ManifestBad); }

        // budget: refused before any file request, nothing deleted
        { Env e2("budget"); FakeServer s = srv; s.log.clear(); SyncConfig c = Cfg(e2, pub); c.budgetBytes = 500; SyncResult r = Sync(c, s).Run();
          CHECK(r.reason == Reason::OverBudget && s.FileGets() == 0 && !fs::exists(fs::path(e2.Root()) / "trees")); }
        { // over budget on an update: the old version is untouched
          Publisher p3 = pub; p3.content["textures/hd.png"] = {Bytes('H', 9000), Kind::Texture}; p3.Publish(srv, 6);
          SyncConfig c = Cfg(env, pub); c.budgetBytes = 2000; SyncResult r = Sync(c, srv).Run(); CHECK(r.reason == Reason::OverBudget);
          Commit cm; CHECK(ReadCommit(StorePaths{env.Root()}, cm) && cm.version == 4 && cm.floor == 5); CHECK(fs::exists(fs::path(env.Root()) / "trees" / "4" / "textures" / "hd.png")); }
    }

    // ------------------------------------------------------------------------------------------------------- script validator
    {
        RUN_SUITE("sync: scripts go through the validator before they are visible");
        Env env("validate");
        FakeServer srv; Publisher pub;
        pub.content["scripts/ok.okayvs"] = {"fine\n", Kind::Script};
        pub.content["models/m.glb"] = {Bytes('m', 300), Kind::Model};
        pub.Publish(srv, 1);
        std::vector<std::string> seen;
        SyncConfig cfg = Cfg(env, pub);
        cfg.validate = [&](const Entry& e, const std::string& path) { seen.push_back(e.path); return Slurp(path) == "fine\n" ? std::string() : std::string("graph is not valid"); };
        CHECK(Sync(cfg, srv).Run().status == Status::Updated);
        CHECK(seen.size() == 1 && seen[0] == "scripts/ok.okayvs");           // models are not passed to it
        pub.content["scripts/ok.okayvs"] = {"evil\n", Kind::Script};
        pub.Publish(srv, 2);
        SyncResult r = Sync(cfg, srv).Run();
        CHECK(r.status == Status::Failed && r.reason == Reason::Validation && r.message.find("graph is not valid") != std::string::npos);
        Commit c; CHECK(ReadCommit(StorePaths{env.Root()}, c) && c.version == 1);
        CHECK(!fs::exists(fs::path(env.Root()) / "trees" / "2") && !fs::exists(fs::path(env.Root()) / "trees" / "2.tmp"));
    }

    // ----------------------------------------------------------------------------------------------------------------- overlay
    {
        RUN_SUITE("overlay: precedence, fallback, escapes, case, tampering");
        Env env("overlay");
        FakeServer srv; Publisher pub;
        pub.content["Textures/Ship.PNG"] = {Bytes('p', 200), Kind::Texture};
        pub.content["models/new.glb"] = {Bytes('n', 100), Kind::Model};
        pub.content["dir with space/a.txt"] = {"sp\n", Kind::Data};
        pub.Publish(srv, 1);
        CHECK(Sync(Cfg(env, pub), srv).Run().status == Status::Updated);
        fs::create_directories(fs::path(env.Install()) / "Textures"); fs::create_directories(fs::path(env.Install()) / "models"); fs::create_directories(fs::path(env.Install()) / "audio");
        { std::ofstream(fs::path(env.Install()) / "Textures" / "Ship.PNG", std::ios::binary) << "INSTALLED-OLD"; }
        { std::ofstream(fs::path(env.Install()) / "models" / "only_installed.glb", std::ios::binary) << "INSTALLED-ONLY"; }
        { std::ofstream(fs::path(env.Install()) / "audio" / "x.wav", std::ios::binary) << "WAV"; }
        // a stray file dropped into the tree is not in the manifest: never found
        { std::ofstream(fs::path(env.Root()) / "trees" / "1" / "models" / "stray.glb", std::ios::binary) << "STRAY"; }

        Overlay ov;
        CHECK(!ov.Active() && ov.Resolve("models/only_installed.glb", env.Install()).empty() == false);   // inactive: install files still found
        std::string why;
        CHECK(ov.Load(env.Root(), pub.game, PubHex(pub.seed), false, &why) && ov.Active() && ov.Version() == 1 && ov.FileCount() == 3);
        bool fromContent = false;
        std::string p = ov.Resolve("Textures/Ship.PNG", env.Install(), &fromContent);
        CHECK(fromContent && Slurp(p) == Bytes('p', 200));                                          // downloaded wins over installed
        p = ov.Resolve("models/only_installed.glb", env.Install(), &fromContent);
        CHECK(!fromContent && Slurp(p) == "INSTALLED-ONLY");                                        // falls back
        CHECK(Slurp(ov.Resolve("models/new.glb", env.Install(), &fromContent)) == Bytes('n', 100) && fromContent);
        CHECK(ov.Resolve("models/missing.glb", env.Install()).empty());                              // in neither layer
        CHECK(ov.Resolve("models/stray.glb", env.Install()).empty());                                // not in the manifest
        // case-insensitive content lookup, on every platform
        CHECK(Slurp(ov.Resolve("textures/ship.png", env.Install(), &fromContent)) == Bytes('p', 200) && fromContent);
        CHECK(Slurp(ov.Resolve("TEXTURES/SHIP.PNG", env.Install())) == Bytes('p', 200));
        CHECK(Slurp(ov.Resolve("Textures\\Ship.PNG", env.Install())) == Bytes('p', 200));           // Windows separators
        CHECK(Slurp(ov.Resolve("dir with space/a.txt", env.Install())) == "sp\n");
        // escape attempts find nothing in either layer
        { std::ofstream(env.dir / "secret.txt", std::ios::binary) << "SECRET"; }
        for (const char* bad : {"../secret.txt", "../../secret.txt", "models/../../secret.txt", "..\\secret.txt", "/etc/passwd", "\\\\server\\share\\x", "C:/Windows/win.ini",
                                "C:x", "models/new.glb:hidden", "nul", "NUL.txt", "con", "com1.glb", "", ".", "models//new.glb", "models/new.glb.", "models/new.glb "}) {
            CHECK(ov.Resolve(bad, env.Install()).empty());
            CHECK(ov.Find(bad).empty());
        }
        CHECK(ov.Resolve((env.dir / "secret.txt").string(), env.Install()).empty());                 // an absolute path is not a game file name
        // a link inside the content tree that points outside is refused
        {
            std::error_code ec;
            fs::create_directory_symlink(env.dir, fs::path(env.Root()) / "trees" / "1" / "linkout", ec);
            if (!ec) CHECK(ov.Find("linkout/secret.txt").empty());
            std::ofstream(fs::path(env.Install()) / "audio" / "dummy", std::ios::binary) << "d";
            fs::create_symlink(env.dir / "secret.txt", fs::path(env.Install()) / "audio" / "leak.wav", ec);
            if (!ec) CHECK(ov.Resolve("audio/leak.wav", env.Install()).empty());
        }
        // wrong game, wrong key, no key
        { Overlay o2; CHECK(!o2.Load(env.Root(), "other", PubHex(pub.seed), false, &why) && !o2.Active()); }
        { Overlay o2; CHECK(!o2.Load(env.Root(), pub.game, PubHex(MakeSeed(7)), false, &why) && !o2.Active() && !why.empty()); }
        { Overlay o2; CHECK(!o2.Load(env.Root(), pub.game, "", false, &why)); }
        // local tampering with the saved manifest or the commit record: the overlay refuses to serve anything
        { const fs::path mf = StorePaths{env.Root()}.ManifestFile(1); const std::string orig = Slurp(mf); std::string t = orig;
          const std::size_t i = t.find("\t200\t"); t.replace(i, 5, "\t201\t"); { std::ofstream(mf, std::ios::binary | std::ios::trunc) << t; }
          Overlay o2; CHECK(!o2.Load(env.Root(), pub.game, PubHex(pub.seed), false, &why) && !o2.Active());      // sha256 of the manifest no longer matches the commit
          { std::ofstream(mf, std::ios::binary | std::ios::trunc) << orig; } CHECK(o2.Load(env.Root(), pub.game, PubHex(pub.seed))); }
        { const fs::path cf = StorePaths{env.Root()}.CommitFile(); const std::string orig = Slurp(cf);
          { std::ofstream(cf, std::ios::binary | std::ios::trunc) << "garbage"; } Overlay o2; CHECK(!o2.Load(env.Root(), pub.game, PubHex(pub.seed)));
          { std::ofstream(cf, std::ios::binary | std::ios::trunc) << orig; } }
        // an interrupted commit write (the ".new" file) does not matter
        { std::ofstream(StorePaths{env.Root()}.CommitFile().string() + ".new", std::ios::binary) << "okay-content-commit 1\ngame testgame\nversion 99\n"; Overlay o2; CHECK(o2.Load(env.Root(), pub.game, PubHex(pub.seed)) && o2.Version() == 1); }
        // the shared instance starts empty
        CHECK(!Overlay::Global().Active());
    }

    // ------------------------------------------------------------------------------------ an update that never finishes changes nothing
    {
        RUN_SUITE("rollback safety: interrupted update leaves the old version in use");
        Env env("interrupted");
        FakeServer srv; Publisher pub;
        pub.content["a.dat"] = {Bytes('a', 400), Kind::Data};
        pub.content["b.dat"] = {Bytes('b', 400), Kind::Data};
        pub.Publish(srv, 1);
        CHECK(Sync(Cfg(env, pub), srv).Run().status == Status::Updated);
        pub.content["a.dat"] = {Bytes('A', 400), Kind::Data}; pub.content["b.dat"] = {Bytes('B', 400), Kind::Data};
        pub.Publish(srv, 2);
        srv.fail503Times["b.dat"] = 99;      // a.dat arrives, b.dat never does
        SyncResult r = Sync(Cfg(env, pub), srv).Run();
        CHECK(r.status == Status::Failed);
        Overlay ov; CHECK(ov.Load(env.Root(), pub.game, PubHex(pub.seed)) && ov.Version() == 1 && Slurp(ov.Find("a.dat")) == Bytes('a', 400));   // v1 still wholly visible
        CHECK(!fs::exists(fs::path(env.Root()) / "trees" / "2"));
        // the server recovers: the verified a.dat from the failed run is reused (not downloaded again)
        srv.fail503Times.clear(); srv.log.clear();
        r = Sync(Cfg(env, pub), srv).Run();
        CHECK(r.status == Status::Updated && r.version == 2 && srv.Count("a.dat") == 0 && srv.Count("b.dat") == 1);
        Overlay ov2; CHECK(ov2.Load(env.Root(), pub.game, PubHex(pub.seed)) && Slurp(ov2.Find("b.dat")) == Bytes('B', 400));
    }

    // ------------------------------------------------------------------------------------------------------------ game start-up wiring
    {
        RUN_SUITE("game start-up: config, off by default, offline fallback");
        GameContentConfig c; std::string why;
        CHECK(ParseConfigText("# c\nurl = https://x/y/\ngame=g1\nkeys=" + std::string(64, 'a') + "\ngroups=hd, ,fr\nbudget-mb=64\n", c, &why) && c.url == "https://x/y/" && c.game == "g1" && c.groups.size() == 2 && c.budgetBytes == (64ull << 20));
        { GameContentConfig d; CHECK(!ParseConfigText("urll=x\n", d, &why) && why.find("unknown key") != std::string::npos); }
        { GameContentConfig d; CHECK(!ParseConfigText("budget-mb=lots\n", d, &why)); }
        { GameContentConfig d; CHECK(!ParseConfigText("nonsense\n", d, &why)); }
        EnvFn env = [](const char* n) { std::string k = n; return k == "OKAY_CONTENT_URL" ? std::string("file:///tmp/zz") : k == "OKAY_CONTENT_ALLOW_UNSIGNED" ? std::string("1") : std::string(); };
        GameContentConfig r = ResolveConfig("url=https://file/\ngame=g1\n", env);
        CHECK(r.url == "file:///tmp/zz" && r.game == "g1" && r.allowUnsigned && r.Enabled());      // environment wins over the file
        CHECK(!ResolveConfig("", [](const char*) { return std::string(); }).Enabled());             // nothing configured = off
        CHECK(!ResolveConfig("game=g1\n", [](const char*) { return std::string(); }).allowUnsigned);

        // OFF: no URL -> nothing is created, nothing is requested, the overlay stays empty
        Env env1("start_off");
        FakeServer srv; Publisher pub; pub.content["data/a.okaydata"] = {"a=1\n", Kind::Data}; pub.Publish(srv, 1);
        Overlay ov;
        StartOutcome o = StartGameContent(GameContentConfig(), env1.Root(), "2.900.0", nullptr, nullptr, nullptr, &srv, &ov);
        CHECK(!o.enabled && !o.synced && !ov.Active() && !fs::exists(env1.Root()) && srv.log.empty());

        // ON: sync, overlay serves the download
        GameContentConfig on; on.url = "https://example.invalid/c/"; on.game = pub.game; on.keys = PubHex(pub.seed);
        std::vector<std::string> logs;
        o = StartGameContent(on, env1.Root(), "2.900.0", nullptr, nullptr, [&](const std::string& l) { logs.push_back(l); }, &srv, &ov);
        CHECK(o.enabled && o.synced && o.sync.status == Status::Updated && o.overlayActive && ov.Active() && o.line.find("serving downloaded content version 1") != std::string::npos);
        CHECK(Slurp(ov.Find("data/a.okaydata")) == "a=1\n");
        // offline next time: the sync fails but the last good content is still served
        srv.dropAll = 1000;
        Overlay ov2;
        o = StartGameContent(on, env1.Root(), "2.900.0", nullptr, nullptr, nullptr, &srv, &ov2);
        CHECK(o.sync.status == Status::Failed && o.sync.reason == Reason::Network && o.overlayActive && ov2.Version() == 1 && o.line.find("sync failed") != std::string::npos);
        // no game id configured: skipped with a plain line, nothing created
        GameContentConfig nog = on; nog.game.clear(); Env env2("start_nog");
        o = StartGameContent(nog, env2.Root(), "2.900.0", nullptr, nullptr, nullptr, &srv, &ov2);
        CHECK(o.enabled && !o.synced && !fs::exists(env2.Root()));
        // a file:// URL really works end to end through FileTransport (the folder a publisher uploads)
        Env env3("start_file");
        { fs::create_directories(env3.dir / "srv" / "data"); for (auto& [n, b] : srv.files) { fs::create_directories((env3.dir / "srv" / n).parent_path()); std::ofstream(env3.dir / "srv" / n, std::ios::binary) << b; } }
        GameContentConfig fc = on; fc.url = "file://" + (env3.dir / "srv").string();
        Overlay ov3;
        o = StartGameContent(fc, (env3.dir / "c").string(), "2.900.0", nullptr, nullptr, nullptr, nullptr, &ov3);
        CHECK(o.sync.status == Status::Updated && ov3.Active() && Slurp(ov3.Find("data/a.okaydata")) == "a=1\n");
        // a bad key in the config: refused, installed files only
        GameContentConfig badk = fc; badk.keys = PubHex(MakeSeed(5)); Env env4("start_badkey"); Overlay ov4;
        o = StartGameContent(badk, env4.Root(), "2.900.0", nullptr, nullptr, nullptr, nullptr, &ov4);
        CHECK(o.sync.reason == Reason::SignatureBad && !o.overlayActive && !ov4.Active());
    }

    // ================================================================ Live Updates foundations (docs/design/live_updates.md)
    {
        RUN_SUITE("live: manifest kinds, min-game, apply, delta");
        Manifest m; m.game = "g1"; m.version = 3; m.minGame = "1.4.0"; m.apply = ApplyWhen::BetweenMatches;
        Entry a; a.path = "live/balance.okaylive"; a.sha256 = Sha256Hex("x"); a.size = 1; a.kind = Kind::Config; m.entries.push_back(a);
        Entry b; b.path = "models/ship.glb"; b.sha256 = Sha256Hex("s"); b.size = 1; b.kind = Kind::Model; m.entries.push_back(b);
        const std::string text = Serialize(m);
        CHECK(text.find("min-game 1.4.0\napply between-matches\n") != std::string::npos && text.find("\tconfig\t") != std::string::npos);
        Manifest back; std::string why;
        CHECK(Parse(text, back, &why) && back.minGame == "1.4.0" && back.apply == ApplyWhen::BetweenMatches && back.entries[0].kind == Kind::Config);
        CHECK(Serialize(back) == text);
        std::string t2 = text; t2.replace(t2.find("between-matches"), 15, "whenever");
        CHECK(!Parse(t2, back, &why) && why.find("apply") != std::string::npos);
        std::string t3 = text; t3.replace(t3.find("min-game 1.4.0"), 14, "min-game soon!");
        CHECK(!Parse(t3, back, &why));
        // headers in another order are accepted, a repeated one is not
        std::string t4 = "okay-content 1\napply next-launch\nversion 3\ngame g1\n";
        CHECK(Parse(t4, back, &why) && back.apply == ApplyWhen::NextLaunch && back.version == 3);
        CHECK(!Parse("okay-content 1\ngame g1\nversion 3\napply next-launch\napply immediately\n", back, &why));
        // delta: added, changed, removed, unchanged
        Manifest n = m; n.version = 4;
        n.entries[0].sha256 = Sha256Hex("y");                                                      // changed bytes
        Entry c; c.path = "scenes/new.okayscene"; c.sha256 = Sha256Hex("n"); c.size = 9; c.kind = Kind::Scene; n.entries.push_back(c);   // added
        n.entries.erase(n.entries.begin() + 1);                                                    // models/ship.glb removed
        Delta d = Diff(m, n);
        CHECK(d.added.size() == 1 && d.added[0].path == "scenes/new.okayscene" && d.changed.size() == 1 && d.changed[0].path == "live/balance.okaylive");
        CHECK(d.removed.size() == 1 && d.removed[0] == "models/ship.glb" && d.unchanged == 0 && d.BytesToSend() == 10);
        Manifest k = n; k.entries[0].group = "hd";   // metadata only: no bytes to move
        d = Diff(n, k);
        CHECK(d.Empty() && d.unchanged == 2);
        d = Diff(Manifest(), n);
        CHECK(d.added.size() == 2 && d.removed.empty());
    }
    {
        RUN_SUITE("live: local folder provider (list, get whole, put)");
        Env env("live_provider");
        FileTransport store((env.dir / "store").string());
        std::vector<Listed> ls;
        CHECK(store.List("", ls).ok && ls.empty());   // nothing published yet: an empty list, not an error
        WriteFile(env.dir / "src.bin", "hello");
        CHECK(store.Put("a/b.okaydata", (env.dir / "src.bin").string()).ok);
        CHECK(store.Put("c.okaydata", (env.dir / "src.bin").string()).ok);
        CHECK(!store.Put("../escape.okaydata", (env.dir / "src.bin").string()).ok && !fs::exists(env.dir / "escape.okaydata"));
        CHECK(store.List("", ls).ok && ls.size() == 2 && ls[0].name == "a/b.okaydata" && ls[0].size == 5 && ls[1].name == "c.okaydata");
        CHECK(store.List("a/", ls).ok && ls.size() == 1);
        std::string got;
        CHECK(GetWhole(store, "a/b.okaydata", 100, got).Ok() && got == "hello");
        CHECK(GetWhole(store, "a/b.okaydata", 3, got).tooBig);
        CHECK(GetWhole(store, "missing", 100, got).status == 404);
        // a read-only provider says so instead of pretending
        FakeServer ro;
        CHECK(!ro.Put("x", (env.dir / "src.bin").string()).ok && !ro.List("", ls).ok);
    }
    {
        RUN_SUITE("live: publish sends only the changed files; the client downloads only them");
        Env env("live_publish");
        LiveFolder lf; lf.dir = env.dir / "work";
        WriteFile(lf.dir / "models/ship.glb", Bytes('M', 3000));
        WriteFile(lf.dir / "textures/ship.png", Bytes('T', 2000));
        WriteFile(lf.dir / "live/game.okaylive", "okay-live 1\nset\tspeed\t10\t-\t-\n");
        std::string sig;
        Manifest v1 = lf.Make(1, &sig);
        CHECK(v1.entries.size() == 3 && v1.entries[0].kind == Kind::Config);
        FileTransport store((env.dir / "store").string());
        PublishOptions po; po.verifyKeys = PubHex(lf.seed);
        PublishResult pr = PublishVersion(store, lf.dir.string(), v1, sig, po);
        CHECK(pr.ok && pr.uploaded == 3 && pr.previous == 0 && pr.delta.added.size() == 3);
        CHECK(!fs::exists(lf.dir / ".okay-publish"));
        // the client takes version 1 from the folder
        Publisher who; SyncConfig cfg = Cfg(env, who);
        SyncResult r = Sync(cfg, store).Run();
        CHECK(r.status == Status::Updated && r.version == 1 && r.filesDownloaded == 3);
        // version 2: one texture changed, one scene added, the model unchanged
        WriteFile(lf.dir / "textures/ship.png", Bytes('U', 2100));
        WriteFile(lf.dir / "scenes/dock.okayscene", "scene\n");
        Manifest v2 = lf.Make(2, &sig);
        pr = PublishVersion(store, lf.dir.string(), v2, sig, po);
        CHECK(pr.ok && pr.previous == 1 && pr.uploaded == 2 && pr.delta.changed.size() == 1 && pr.delta.added.size() == 1 && pr.delta.unchanged == 2);
        CHECK(pr.bytesUploaded == 2100 + 6);
        {
            struct Counting : Transport {
                Transport& in; std::vector<std::string> names;
                explicit Counting(Transport& t) : in(t) {}
                GetResult Get(const std::string& n, std::uint64_t f, std::uint64_t m, const GetHandlers& h) override { names.push_back(n); return in.Get(n, f, m, h); }
                const char* Describe() const override { return "counting"; }
            } counting(store);
            r = Sync(cfg, counting).Run();
            CHECK(r.status == Status::Updated && r.version == 2 && r.filesDownloaded == 2 && r.filesReused == 2);
            std::set<std::string> fetched(counting.names.begin(), counting.names.end());
            CHECK(fetched.count("textures/ship.png") && fetched.count("scenes/dock.okayscene") && !fetched.count("models/ship.glb") && !fetched.count("live/game.okaylive"));
        }
        // refusals send nothing: same version again, a wrong key, an unsigned list, a local file that changed after the manifest was made
        const int before = static_cast<int>(fs::file_size(env.dir / "store" / "content.manifest"));
        pr = PublishVersion(store, lf.dir.string(), v2, sig, po);
        CHECK(!pr.ok && pr.message.find("higher") != std::string::npos);
        Manifest v3 = lf.Make(3, nullptr);
        pr = PublishVersion(store, lf.dir.string(), v3, MakeSignatureFile(Serialize(v3), MakeSeed(9)), po);
        CHECK(!pr.ok && pr.message.find("key") != std::string::npos);
        pr = PublishVersion(store, lf.dir.string(), v3, std::string(), po);
        CHECK(!pr.ok && pr.message.find("not signed") != std::string::npos);
        WriteFile(lf.dir / "scenes/dock.okayscene", "scene changed after make\n");
        Manifest v3b = v2; v3b.version = 3; v3b.entries[2].sha256 = Sha256Hex("something else"); v3b.entries[2].size = 14;
        pr = PublishVersion(store, lf.dir.string(), v3b, MakeSignatureFile(Serialize(v3b), lf.seed), po);
        CHECK(!pr.ok && pr.message.find("does not match") != std::string::npos);
        std::string live; GetWhole(store, kManifestName, 1 << 20, live);
        Manifest lm; CHECK(Parse(live, lm) && lm.version == 2 && static_cast<int>(live.size()) == before);
        // the server lost an unchanged file: the next publish sends it again
        fs::remove(env.dir / "store" / "models" / "ship.glb");
        Manifest v4 = lf.Make(4, &sig);
        pr = PublishVersion(store, lf.dir.string(), v4, sig, po);
        CHECK(pr.ok && pr.repaired == 1 && pr.uploaded == 1 && fs::exists(env.dir / "store" / "models" / "ship.glb"));
    }
    {
        RUN_SUITE("live: an interrupted download resumes by byte range (local provider)");
        Env env("live_resume");
        LiveFolder lf; lf.dir = env.dir / "work";
        WriteFile(lf.dir / "models/big.glb", Bytes('B', 50000));
        std::string sig; Manifest v1 = lf.Make(1, &sig);
        FileTransport store((env.dir / "store").string());
        CHECK(PublishVersion(store, lf.dir.string(), v1, sig).ok);
        CutOnce cut(store, "models/big.glb", 20000);
        Publisher who; SyncConfig cfg = Cfg(env, who);
        const SyncResult r = Sync(cfg, cut).Run();
        CHECK(r.status == Status::Updated && r.retries == 1 && r.bytesDownloaded == 50000);
        std::vector<std::uint64_t> starts;
        for (auto& g : cut.gets) if (g.first == "models/big.glb") starts.push_back(g.second);
        CHECK(starts.size() == 2 && starts[0] == 0 && starts[1] == 20000);   // the second GET asked only for the rest
        CHECK(Slurp(fs::path(env.Root()) / "trees" / "1" / "models" / "big.glb") == Bytes('B', 50000));
    }
    {
        RUN_SUITE("live: corrupted file and bad signatures are refused and not applied (local provider)");
        Env env("live_refuse");
        LiveFolder lf; lf.dir = env.dir / "work";
        WriteFile(lf.dir / "data/a.okaydata", "a=1\n");
        std::string sig; Manifest v1 = lf.Make(1, &sig);
        FileTransport store((env.dir / "store").string());
        CHECK(PublishVersion(store, lf.dir.string(), v1, sig).ok);
        Publisher who; SyncConfig cfg = Cfg(env, who);
        CHECK(Sync(cfg, store).Run().status == Status::Updated);
        WriteFile(lf.dir / "data/a.okaydata", "a=2\n");
        Manifest v2 = lf.Make(2, &sig);
        CHECK(PublishVersion(store, lf.dir.string(), v2, sig).ok);
        WriteFile(env.dir / "store" / "data" / "a.okaydata", "a=X\n");   // same size, wrong bytes on the server
        SyncResult r = Sync(cfg, store).Run();
        Commit c;
        CHECK(r.status == Status::Failed && r.reason == Reason::FileCorrupt && r.corruptRejected >= 1);
        CHECK(ReadCommit(StorePaths{env.Root()}, c) && c.version == 1 && Slurp(fs::path(env.Root()) / "trees" / "1" / "data" / "a.okaydata") == "a=1\n");
        WriteFile(env.dir / "store" / "data" / "a.okaydata", "a=2\n");
        fs::remove(env.dir / "store" / kSigName);                         // missing signature
        r = Sync(cfg, store).Run();
        CHECK(r.reason == Reason::SignatureBad && ReadCommit(StorePaths{env.Root()}, c) && c.version == 1);
        WriteFile(env.dir / "store" / kSigName, "okay-sig1 " + std::string(128, '0') + "\n");   // a signature that does not verify
        r = Sync(cfg, store).Run();
        CHECK(r.reason == Reason::SignatureBad && ReadCommit(StorePaths{env.Root()}, c) && c.version == 1);
        WriteFile(env.dir / "store" / kSigName, sig);
        CHECK(Sync(cfg, store).Run().status == Status::Updated);
    }
    {
        RUN_SUITE("live: incompatible versions are refused");
        Env env("live_minversion");
        LiveFolder lf; lf.dir = env.dir / "work";
        WriteFile(lf.dir / "data/a.okaydata", "a=1\n");
        std::string sig; Manifest v1 = lf.Make(1, nullptr); v1.minGame = "2.0.0"; sig = MakeSignatureFile(Serialize(v1), lf.seed);
        FileTransport store((env.dir / "store").string());
        CHECK(PublishVersion(store, lf.dir.string(), v1, sig).ok);
        Publisher who; SyncConfig cfg = Cfg(env, who);
        cfg.gameVersion = "1.5.0";
        SyncResult r = Sync(cfg, store).Run();
        CHECK(r.status == Status::Failed && r.reason == Reason::GameTooOld && !fs::exists(StorePaths{env.Root()}.CommitFile()));
        cfg.gameVersion.clear();   // unknown game version: the requirement is not met either
        CHECK(Sync(cfg, store).Run().reason == Reason::GameTooOld);
        cfg.gameVersion = "2.0.1";
        CHECK(Sync(cfg, store).Run().status == Status::Updated);
        // multiplayer: client and server
        PeerVersions s; s.game = "1.4.2"; s.content = 7; s.protocol = 5; s.minProtocol = 3;
        PeerVersions k = s;
        CHECK(CheckCompat(s, k).Accepts());
        k.protocol = 2; k.minProtocol = 1;
        Compat cc = CheckCompat(s, k);
        CHECK(cc.verdict == CompatVerdict::ProtocolMismatch && cc.update == UpdateSide::Client && !cc.message.empty());
        k = s; k.minProtocol = 6; k.protocol = 7;
        CHECK(CheckCompat(s, k).verdict == CompatVerdict::ProtocolMismatch && CheckCompat(s, k).update == UpdateSide::Server);
        k = s; k.protocol = 4; k.minProtocol = 4;   // ranges [3,5] and [4,4] meet
        CHECK(CheckCompat(s, k).Accepts());
        k = s; k.game = "1.4.1";
        cc = CheckCompat(s, k);
        CHECK(cc.verdict == CompatVerdict::GameMismatch && cc.update == UpdateSide::Client && cc.message.find("1.4.1") != std::string::npos);
        CompatRules mm; mm.game = CompatRules::Game::SameMajorMinor;
        CHECK(CheckCompat(s, k, mm).Accepts());
        k.game = "1.5.0";
        CHECK(!CheckCompat(s, k, mm).Accepts() && CheckCompat(s, k, mm).update == UpdateSide::Server);
        k.game = ""; CHECK(!CheckCompat(s, k, mm).Accepts());   // unknown is never "the same"
        CompatRules any; any.game = CompatRules::Game::Any; CHECK(CheckCompat(s, k, any).Accepts());
        k = s; k.content = 6;
        cc = CheckCompat(s, k);
        CHECK(cc.verdict == CompatVerdict::ClientContentOld && cc.update == UpdateSide::Client);
        CompatRules lag; lag.contentBehind = 1; CHECK(CheckCompat(s, k, lag).Accepts());
        k.content = 5; CHECK(!CheckCompat(s, k, lag).Accepts());
        k.content = 8;
        CHECK(CheckCompat(s, k).verdict == CompatVerdict::ClientContentNew && CheckCompat(s, k).update == UpdateSide::Server);
        CompatRules ahead; ahead.allowClientAhead = true; CHECK(CheckCompat(s, k, ahead).Accepts());
    }
    {
        RUN_SUITE("live: a failed apply or a crashing start-up rolls back, and the bad version is not taken again");
        Env env("live_rollback");
        LiveFolder lf; lf.dir = env.dir / "work";
        WriteFile(lf.dir / "data/a.okaydata", "a=1\n");
        std::string sig; Manifest v1 = lf.Make(1, &sig);
        FileTransport store((env.dir / "store").string());
        CHECK(PublishVersion(store, lf.dir.string(), v1, sig).ok);
        Publisher who; SyncConfig cfg = Cfg(env, who);
        CHECK(Sync(cfg, store).Run().status == Status::Updated);
        const StorePaths sp{env.Root()};
        CHECK(MarkLaunchGood(sp) && LastGoodVersion(sp) == 1);
        WriteFile(lf.dir / "data/a.okaydata", "a=2\n");
        Manifest v2 = lf.Make(2, &sig);
        CHECK(PublishVersion(store, lf.dir.string(), v2, sig).ok);
        CHECK(Sync(cfg, store).Run().version == 2);
        // start-up guard: two starts of version 2 never reach "good"; the third start rolls back to 1
        LaunchCheck lc = BeginLaunch(sp);
        CHECK(!lc.rolledBack && lc.version == 2 && lc.failedStarts == 0);
        lc = BeginLaunch(sp);   // (the game crashed before MarkLaunchGood)
        CHECK(!lc.rolledBack && lc.failedStarts == 1);
        lc = BeginLaunch(sp);
        CHECK(lc.rolledBack && lc.version == 1 && lc.failedStarts == 2 && lc.message.find("rolled back to version 1") != std::string::npos);
        Overlay ov; CHECK(ov.Load(env.Root(), who.game, PubHex(who.seed)) && ov.Version() == 1 && Slurp(ov.Find("data/a.okaydata")) == "a=1\n");
        lc = BeginLaunch(sp);   // version 1 is known good: never blamed
        CHECK(!lc.rolledBack && lc.version == 1 && lc.failedStarts == 0);
        // the rolled-back version is not downloaded again; a newer one is
        SyncResult r = Sync(cfg, store).Run();
        CHECK(r.status == Status::Failed && r.reason == Reason::RolledBack);
        CHECK(ov.Load(env.Root(), who.game, PubHex(who.seed)) && ov.Version() == 1);
        WriteFile(lf.dir / "data/a.okaydata", "a=3\n");
        Manifest v3 = lf.Make(3, &sig);
        CHECK(PublishVersion(store, lf.dir.string(), v3, sig).ok);
        r = Sync(cfg, store).Run();
        CHECK(r.status == Status::Updated && r.version == 3);
        // a version that started fine once is not rolled back by later crashes
        CHECK(MarkLaunchGood(sp));
        for (int i = 0; i < 4; ++i) CHECK(!BeginLaunch(sp).rolledBack);
        // a failed hot-apply in the running game: the game calls Rollback and the kept previous version (1) is visible again
        std::string why;
        Commit c; CHECK(ReadCommit(sp, c) && c.version == 3 && c.previous == 1);
        CHECK(Rollback(sp, &why) && ReadCommit(sp, c) && c.version == 1);
        CHECK(Sync(cfg, store).Run().reason == Reason::RolledBack);
        // a validator refusal (a damaged live config) never becomes visible at all
        WriteFile(lf.dir / "live/game.okaylive", "okay-live 1\nset\tSpeed!\t10\t-\t-\n");
        Manifest v4 = lf.Make(4, &sig);
        CHECK(PublishVersion(store, lf.dir.string(), v4, sig).ok);
        cfg.validate = [](const Entry& e, const std::string& path) { return e.kind == Kind::Config ? LiveConfigProblem(Slurp(path)) : std::string(); };
        r = Sync(cfg, store).Run();
        CHECK(r.reason == Reason::Validation && ReadCommit(sp, c) && c.version == 1 && !fs::exists(sp.TmpTree(4)));
    }
    {
        RUN_SUITE("live: offline keeps the last good version working (local provider)");
        Env env("live_offline");
        LiveFolder lf; lf.dir = env.dir / "work";
        WriteFile(lf.dir / "data/a.okaydata", "a=1\n");
        std::string sig; Manifest v1 = lf.Make(1, &sig);
        FileTransport store((env.dir / "store").string());
        CHECK(PublishVersion(store, lf.dir.string(), v1, sig).ok);
        Publisher who;
        GameContentConfig gc; gc.url = "file://" + (env.dir / "store").string(); gc.game = who.game; gc.keys = PubHex(who.seed);
        Overlay ov;
        StartOutcome o = StartGameContent(gc, env.Root(), "2.900.0", nullptr, nullptr, nullptr, nullptr, &ov);
        CHECK(o.sync.status == Status::Updated && ov.Version() == 1);
        fs::remove_all(env.dir / "store");   // the server is gone
        Overlay ov2;
        o = StartGameContent(gc, env.Root(), "2.900.0", nullptr, nullptr, nullptr, nullptr, &ov2);
        CHECK(o.sync.status == Status::Failed && o.overlayActive && ov2.Version() == 1 && Slurp(ov2.Find("data/a.okaydata")) == "a=1\n");
    }
    {
        RUN_SUITE("live: staged apply (download now, make visible later)");
        Env env("live_staged");
        LiveFolder lf; lf.dir = env.dir / "work";
        WriteFile(lf.dir / "data/a.okaydata", "a=1\n");
        std::string sig; Manifest v1 = lf.Make(1, &sig);
        FileTransport store((env.dir / "store").string());
        CHECK(PublishVersion(store, lf.dir.string(), v1, sig).ok);
        Publisher who; SyncConfig cfg = Cfg(env, who);
        CHECK(Sync(cfg, store).Run().status == Status::Updated);
        WriteFile(lf.dir / "data/a.okaydata", "a=2\n");
        WriteFile(lf.dir / "data/b.okaydata", "b=1\n");
        Manifest v2 = lf.Make(2, nullptr); v2.apply = ApplyWhen::AtLoadingScreen; sig = MakeSignatureFile(Serialize(v2), lf.seed);
        CHECK(PublishVersion(store, lf.dir.string(), v2, sig).ok);
        const StorePaths sp{env.Root()};
        SyncConfig st = cfg; st.stageOnly = true;
        SyncResult r = Sync(st, store).Run();
        Commit c;
        CHECK(r.status == Status::Staged && r.version == 1 && ReadCommit(sp, c) && c.version == 1);   // downloaded, not visible
        Overlay ov; CHECK(ov.Load(env.Root(), who.game, PubHex(who.seed)) && ov.Version() == 1 && ov.Find("data/b.okaydata").empty());
        // the game asks when it may apply: the published policy says "at a loading screen"
        Manifest staged; std::string stext; std::uint64_t sv = 0; std::string sgame, ssha;
        CHECK(Sync::ReadStaged(sp, sgame, sv, ssha) && sv == 2);
        CHECK(detail::ReadAll(sp.StagedManifest(2), stext, 1 << 20) && Parse(stext, staged) && staged.apply == ApplyWhen::AtLoadingScreen);
        GameMoment playing; playing.inMatch = true;
        CHECK(!DecideApply(staged.apply, ApplyWhen::NextLaunch, true, playing).applyNow);
        GameMoment loading; loading.loadingScreen = true;
        CHECK(DecideApply(staged.apply, ApplyWhen::NextLaunch, true, loading).applyNow);
        // offline now: ApplyStaged needs no network
        fs::remove_all(env.dir / "store");
        r = Sync(cfg, store).ApplyStaged();
        CHECK(r.status == Status::Updated && r.version == 2 && ReadCommit(sp, c) && c.version == 2 && c.previous == 1 && !fs::exists(sp.StagedFile()) && !fs::exists(sp.StagedManifest(2)));
        Overlay ov2; CHECK(ov2.Load(env.Root(), who.game, PubHex(who.seed)) && Slurp(ov2.Find("data/b.okaydata")) == "b=1\n");
        CHECK(Sync(cfg, store).ApplyStaged().reason == Reason::NothingStaged);
        // a staged file damaged on disk is refused at apply time, and nothing changes
        FileTransport store2((env.dir / "store2").string());
        WriteFile(lf.dir / "data/b.okaydata", "b=2\n");
        Manifest v3 = lf.Make(3, &sig);
        CHECK(PublishVersion(store2, lf.dir.string(), v3, sig).ok);
        CHECK(Sync(st, store2).Run().status == Status::Staged);
        WriteFile(sp.TmpTree(3) / "data" / "b.okaydata", "b=9\n");
        r = Sync(cfg, store2).ApplyStaged();
        CHECK(r.reason == Reason::FileCorrupt && ReadCommit(sp, c) && c.version == 2 && !fs::exists(sp.StagedFile()));
        // a staged manifest whose signature no longer verifies (key changed) is refused too
        CHECK(Sync(st, store2).Run().status == Status::Staged);
        SyncConfig other = cfg; other.trustedKeys = PubHex(MakeSeed(9));
        CHECK(Sync(other, store2).ApplyStaged().reason == Reason::SignatureBad && ReadCommit(sp, c) && c.version == 2);
    }
    {
        RUN_SUITE("live: apply timing decisions");
        GameMoment start; start.startingUp = true;
        GameMoment match; match.inMatch = true;
        GameMoment menu;
        GameMoment load; load.loadingScreen = true;
        GameMoment loadInMatch; loadInMatch.loadingScreen = true; loadInMatch.inMatch = true;   // a level change inside a match
        const ApplyWhen D = ApplyWhen::GameDefault;
        for (ApplyWhen p : {ApplyWhen::Immediately, ApplyWhen::AtLoadingScreen, ApplyWhen::BetweenMatches, ApplyWhen::NextLaunch, D}) {
            CHECK(!DecideApply(p, D, false, start).applyNow);   // nothing staged: nothing to do
            CHECK(DecideApply(p, D, true, start).applyNow);     // start-up applies every policy
        }
        CHECK(DecideApply(ApplyWhen::Immediately, D, true, match).applyNow);
        CHECK(!DecideApply(ApplyWhen::AtLoadingScreen, D, true, match).applyNow && DecideApply(ApplyWhen::AtLoadingScreen, D, true, load).applyNow);
        CHECK(DecideApply(ApplyWhen::AtLoadingScreen, D, true, loadInMatch).applyNow);
        CHECK(!DecideApply(ApplyWhen::BetweenMatches, D, true, match).applyNow && !DecideApply(ApplyWhen::BetweenMatches, D, true, loadInMatch).applyNow);
        CHECK(DecideApply(ApplyWhen::BetweenMatches, D, true, menu).applyNow && DecideApply(ApplyWhen::BetweenMatches, D, true, load).applyNow);
        CHECK(!DecideApply(ApplyWhen::NextLaunch, D, true, menu).applyNow && !DecideApply(ApplyWhen::NextLaunch, D, true, load).applyNow);
        // no published policy: the game's own; neither: next launch
        CHECK(DecideApply(D, ApplyWhen::Immediately, true, match).applyNow && DecideApply(D, ApplyWhen::Immediately, true, match).policy == ApplyWhen::Immediately);
        CHECK(!DecideApply(D, D, true, menu).applyNow && DecideApply(D, D, true, menu).policy == ApplyWhen::NextLaunch);
        // the publisher's wish beats the game's default
        CHECK(!DecideApply(ApplyWhen::NextLaunch, ApplyWhen::Immediately, true, menu).applyNow);
        CHECK(!DecideApply(ApplyWhen::BetweenMatches, D, true, match).reason.empty());
    }
    {
        RUN_SUITE("live: remote config and scheduled toggles");
        LiveConfig lc; std::string why;
        const std::string text =
            "okay-live 1\n"
            "# balancing\n"
            "set\tspeed\t10\t-\t-\n"
            "set\tdouble_xp\toff\t-\t-\n"
            "set\tdouble_xp\ton\t1000\t2000\n"       // a weekend event
            "set\tspeed\t12.5\t1500\t-\n"            // a balance change that starts later and stays
            "\n"
            "set\tmotd\tWelcome back!\t-\t1800\n";
        CHECK(lc.Parse(text, &why));
        CHECK(!lc.IsOn("double_xp", 999) && lc.IsOn("double_xp", 1000) && lc.IsOn("double_xp", 1999) && !lc.IsOn("double_xp", 2000));
        CHECK(lc.GetInt("speed", 0, -1) == 10 && lc.GetNumber("speed", 1499, -1) == 10.0 && lc.GetNumber("speed", 1500, -1) == 12.5);
        CHECK(lc.GetInt("speed", 1500, -1) == -1);   // "12.5" is not a whole number: the fallback, not 12
        CHECK(lc.Get("motd", 1799) == "Welcome back!" && lc.Get("motd", 1800, "none") == "none");
        CHECK(lc.Get("unknown", 5, "dflt") == "dflt" && lc.IsOn("unknown", 5, true));
        CHECK(lc.NextChange(0) == 1000 && lc.NextChange(1000) == 1500 && lc.NextChange(1500) == 1800 && lc.NextChange(1800) == 2000 && lc.NextChange(2000) == kLiveOpen);
        // into the engine's settings (the game passes the Cvar registry; here a map that refuses one key, as a read-only cvar would)
        std::map<std::string, std::string> cvars;
        const int applied = lc.ApplyTo(1600, [&](const std::string& k, const std::string& v) { if (k == "motd") return false; cvars[k] = v; return true; });
        CHECK(applied == 2 && cvars["speed"] == "12.5" && cvars["double_xp"] == "on" && !cvars.count("motd"));
        // refused whole: the game keeps its shipped values
        CHECK(!lc.Parse("okay-live 1\nset\tspeed\t10\t-\n", &why) && lc.Empty() && lc.GetInt("speed", 0, 7) == 7);
        CHECK(!LiveConfigProblem("okay-live 1\nset\tspeed\t10\t2000\t1000\n").empty());   // ends before it starts
        CHECK(!LiveConfigProblem("okay-live 1\nset\tBad Key\t1\t-\t-\n").empty());
        CHECK(!LiveConfigProblem("okay-live 2\n").empty() && !LiveConfigProblem("okay-live 1").empty());
        CHECK(!LiveConfigProblem("okay-live 1\nset\tk\tv\x01\t-\t-\n").empty());
        CHECK(LiveConfigProblem("okay-live 1\n").empty());
    }
    {
        RUN_SUITE("live: OkayBackend Storage provider (publish and sync through the Storage interface)");
        Env env("live_backend");
        MemStorage mem;
        StorageTransport store(mem, "game-content", "testgame");
        LiveFolder lf; lf.dir = env.dir / "work";
        WriteFile(lf.dir / "models/a b.glb", Bytes('A', 1500));
        WriteFile(lf.dir / "data/x.okaydata", "x=1\n");
        std::string sig; Manifest v1 = lf.Make(1, &sig);
        PublishResult pr = PublishVersion(store, lf.dir.string(), v1, sig);
        CHECK(pr.ok && pr.uploaded == 2 && mem.objects.count("game-content/testgame/models/a b.glb") && mem.objects.count("game-content/testgame/content.manifest"));
        std::vector<Listed> ls;
        CHECK(store.List("", ls).ok && ls.size() == 4 && ls[0].name == "content.manifest" && ls[2].name == "data/x.okaydata" && ls[2].size == 4);
        Publisher who; SyncConfig cfg = Cfg(env, who);
        SyncResult r = Sync(cfg, store).Run();
        CHECK(r.status == Status::Updated && r.filesDownloaded == 2);
        WriteFile(lf.dir / "data/x.okaydata", "x=2\n");
        Manifest v2 = lf.Make(2, &sig);
        const int up = mem.uploads;
        pr = PublishVersion(store, lf.dir.string(), v2, sig);
        CHECK(pr.ok && pr.uploaded == 1 && mem.uploads == up + 3);   // one file, the signature, the manifest
        r = Sync(cfg, store).Run();
        CHECK(r.status == Status::Updated && r.version == 2 && r.filesDownloaded == 1);
    }

    TEST_MAIN_RESULT();
}
