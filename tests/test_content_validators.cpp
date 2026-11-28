// The script / graph safety checks content goes through before it is visible (shared/GameContentValidators.hpp), end to end with the real
// engine parsers: a good graph and script are accepted, a broken graph and a script with a syntax error are refused and never become visible,
// and files of other kinds (and programs) are handled by the manifest rules. Docs: docs/release/content_delivery.md "Scripts and graphs".
#include "test_framework.hpp"

#include "GameContentValidators.hpp"
#include "ContentSync.hpp"

#include <chrono>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using namespace okaycontent;

namespace {
struct Server : Transport {
    std::map<std::string, std::string> files;
    GetResult Get(const std::string& name, std::uint64_t from, std::uint64_t, const GetHandlers& h) override {
        GetResult r;
        auto f = files.find(name);
        GetHead head;
        if (f == files.end()) { head.status = 404; r.reached = true; if (h.onHead && !h.onHead(head)) r.aborted = true; return r; }
        head.status = from ? 206 : 200; head.bodyStart = from; head.totalSize = f->second.size();
        if (h.onHead && !h.onHead(head)) { r.aborted = true; return r; }
        const std::string body = f->second.substr(static_cast<std::size_t>(from));
        if (h.onData && !body.empty() && !h.onData(reinterpret_cast<const std::uint8_t*>(body.data()), body.size())) { r.aborted = true; return r; }
        r.reached = true;
        return r;
    }
    const char* Describe() const override { return "test"; }
};
okayac::ed25519::Seed MakeSeed() { okayac::ed25519::Seed s{}; for (std::size_t i = 0; i < s.size(); ++i) s[i] = static_cast<std::uint8_t>(i * 5 + 1); return s; }

std::string Publish(Server& s, const std::map<std::string, std::pair<std::string, Kind>>& content, std::uint64_t version) {
    Manifest m; m.game = "vt"; m.version = version;
    s.files.clear();
    for (const auto& [p, c] : content) {
        Entry e; e.path = p; e.sha256 = Sha256Hex(c.first); e.size = c.first.size(); e.kind = c.second;
        m.entries.push_back(e); s.files[p] = c.first;
    }
    const std::string text = Serialize(m);
    s.files[kManifestName] = text;
    s.files[kSigName] = MakeSignatureFile(text, MakeSeed());
    const auto pk = okayac::ed25519::PublicKeyFromSeed(MakeSeed());
    return okayac::ToHex(pk.data(), pk.size());
}
}  // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / ("okay_content_vt_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ec;
    fs::remove_all(root, ec);

    // A graph that parses and checks clean (an OnStart event that does nothing is a warning at most) and a script that parses.
    const std::string goodGraph = "# a graph\nnode 1 OnStart\nentry OnStart 1\n";
    const std::string badGraph = "this is not a graph at all {{{\n";
    const std::string goodScript = "function start() { }\n";
    const std::string badScript = "function start( { \n";

    {
        RUN_SUITE("validators on single files");
        Entry e; e.kind = Kind::Script;
        const fs::path d = root / "single"; fs::create_directories(d);
        auto write = [&](const char* n, const std::string& t) { std::ofstream(d / n, std::ios::binary) << t; return (d / n).string(); };
        e.path = "s/bad.okayvs";    CHECK(!ValidateContentFile(e, write("bad.okayvs", badGraph)).empty());          // not a graph at all
        e.path = "s/good.okayvs";   CHECK(ValidateContentFile(e, write("good.okayvs", goodGraph)).empty());          // a graph (the checker only warns that nothing is wired)
        e.path = "S/Good.OKAYVS";   CHECK(ValidateContentFile(e, write("good2.okayvs", goodGraph)).empty());         // extension case does not matter
        e.path = "s/type.okayvs";   CHECK(!ValidateContentFile(e, write("type.okayvs", "node 1 NoSuchNodeType\n")).empty());                          // unknown node type
        e.path = "s/wire.okayvs";   CHECK(!ValidateContentFile(e, write("wire.okayvs", "node 1 OnStart\nexec 1 0 9\nentry OnStart 1\n")).empty());   // wire to a missing node
        e.path = "s/mixed.okayvs";  CHECK(!ValidateContentFile(e, write("mixed.okayvs", goodGraph + "run_this_program now\n")).empty());                // a stray line is refused, not ignored
        e.path = "s/nonode.okayvs"; CHECK(!ValidateContentFile(e, write("nonode.okayvs", "# only a comment\n")).empty());
        e.path = "s/bad.okayscript"; CHECK(!ValidateContentFile(e, write("bad.okayscript", badScript)).empty());
        e.path = "s/good.okayscript"; CHECK(ValidateContentFile(e, write("good.okayscript", goodScript)).empty());
        e.path = "s/empty.okayvs";  CHECK(!ValidateContentFile(e, write("empty.okayvs", "")).empty());       // empty is refused
        e.kind = Kind::Config;      // live config (shared/ContentLiveConfig.hpp): a damaged one is refused whole
        e.path = "live/good.okaylive"; CHECK(ValidateContentFile(e, write("good.okaylive", "okay-live 1\nset\tspeed\t10\t-\t-\n")).empty());
        e.path = "live/bad.okaylive";  CHECK(!ValidateContentFile(e, write("bad.okaylive", "okay-live 1\nspeed=10\n")).empty());
        e.kind = Kind::Script;
        e.path = "s/missing.okayvs"; CHECK(!ValidateContentFile(e, (d / "none").string()).empty());          // unreadable is refused
        e.kind = Kind::Data; e.path = "d/x.okaydata"; CHECK(ValidateContentFile(e, write("x.okaydata", "hp=1\n")).empty());   // data has no code
    }
    {
        RUN_SUITE("a refused script never becomes visible");
        Server s; const std::string pub = Publish(s, {{"scripts/ok.okayscript", {goodScript, Kind::Script}}, {"models/m.glb", {"abc", Kind::Model}}}, 1);
        SyncConfig cfg; cfg.root = (root / "c").string(); cfg.game = "vt"; cfg.trustedKeys = pub; cfg.validate = ValidateContentFile;
        CHECK(Sync(cfg, s).Run().status == Status::Updated);
        Publish(s, {{"scripts/ok.okayscript", {badScript, Kind::Script}}, {"models/m.glb", {"abc", Kind::Model}}}, 2);
        const SyncResult r = Sync(cfg, s).Run();
        CHECK(r.status == Status::Failed && r.reason == Reason::Validation);
        Commit c; CHECK(ReadCommit(StorePaths{cfg.root}, c) && c.version == 1);
        CHECK(!fs::exists(fs::path(cfg.root) / "trees" / "2"));
    }
    fs::remove_all(root, ec);
    TEST_MAIN_RESULT();
}
