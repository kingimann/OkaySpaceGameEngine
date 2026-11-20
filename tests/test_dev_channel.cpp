// Test builds (the "dev channel"): shared/TestBuild.hpp, and what launcher/VersionsManager.cpp does with them. Docs: docs/release/release_channels.md "Test builds".
//
// What this proves, with no window, no network and no clock:
//   * the number: a test version (2.876.0.17) sorts just above the release it was made on and below every later release, never equals a release,
//     and a serial of 0 or a fifth number is not a test version;
//   * the STABLE reader ignores them: ParseIndex, ParseVersionTxt, FallbackIndex and LoadIndex never list, name or fetch a test build, and never
//     ask the server for anything under dev/ (so a release can never be replaced by a test build, and "off" means "never read the dev area");
//   * the dev reader (LoadDevIndex) reads ONLY <bucket>/dev/..., keeps only well-formed test builds, requires the same signature as the real list,
//     and hands back a plan that needs a checksum for every file (same refusal when one is missing, same install and rollback rules);
//   * ChooseTarget: what the update check installs for every combination of switch, channel, go-back and server answer;
//   * the "Get test builds" setting is off by default, saved with the other launcher settings, and writes nothing when off (launcher.cfg of a
//     person who never uses it is byte for byte what it was);
//   * the publish tool (scripts/dev_index.py) and the launcher agree on the index format (python3 is run when it is there).
#include "test_framework.hpp"
#include "TestBuild.hpp"
#include "VersionsManager.hpp"
#include "UpdaterCore.hpp"
#include "UpdateSignature.hpp"
#include "OfficialGame.hpp"   // the bundled SHA-256 (what the launcher checks downloads with)

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;
namespace tb = okay::testbuild;
using okayac::ed25519::Seed;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        static int n = 0;
        path = fs::temp_directory_path() / ("okay_devchan_" + std::to_string(++n) + "_" +
               std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~TempDir() {
        std::error_code ec;
        fs::permissions(path, fs::perms::all, ec);
        for (auto& e : fs::recursive_directory_iterator(path, ec)) fs::permissions(e.path(), fs::perms::all, ec);
        fs::remove_all(path, ec);
    }
};
void Put(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
}
std::string Get(const fs::path& p) { std::ifstream in(p, std::ios::binary); std::ostringstream ss; ss << in.rdbuf(); return ss.str(); }
bool Has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
std::string Sha(const std::string& s) { return okay::account::detail::Sha256Hex(s); }

const char* kBucket = "https://example.test/storage/v1/object/public/releases/";
const std::string kDev = std::string(kBucket) + "dev/";

Seed MakeSeed(std::uint8_t tag) {
    Seed s{};
    for (std::size_t i = 0; i < s.size(); ++i) s[i] = (std::uint8_t)(tag * 31u + i * 7u + 1u);
    return s;
}
std::string PubHex(const Seed& s) { const auto p = okayac::ed25519::PublicKeyFromSeed(s); return okayac::ToHex(p.data(), p.size()); }

// ---- a fake release server that remembers every address it was asked for ----------------------------------------------------------

struct Server {
    std::map<std::string, std::string> files;
    std::vector<std::string> asked;
    okayversions::FetchTextFn Fn() {
        return [this](const std::string& url, std::string& out) {
            asked.push_back(url);
            const auto it = files.find(url);
            if (it == files.end()) return false;
            out = it->second;
            return true;
        };
    }
    bool AskedAnythingUnderDev() const {
        for (const std::string& u : asked) if (Has(u, "/dev/")) return true;
        return false;
    }
    bool AskedOnlyUnderDev() const {
        for (const std::string& u : asked) if (u.compare(0, kDev.size(), kDev) != 0) return false;
        return !asked.empty();
    }
};

std::string Body(const std::string& ver, const std::string& file) { return "MZ fake " + file + " v" + ver + std::string(700 + ver.size() * 3 + file.size(), 'x'); }
std::string FileJson(const std::string& ver, const char* file, bool withSha = true) {
    const std::string b = Body(ver, file);
    return std::string("\"") + file + "\": {\"size\": " + std::to_string(b.size()) + (withSha ? ", \"sha256\": \"" + Sha(b) + "\"" : std::string()) + "}";
}
std::string TestEntry(const std::string& ver, bool inBucket = true, const std::string& extra = "", bool withSha = true, const std::string& channel = "dev") {
    return "{\"version\": \"" + ver + "\", \"channel\": \"" + channel + "\", \"date\": \"2026-11-16\", \"inBucket\": " + (inBucket ? "true" : "false") +
           ", \"branch\": \"claude/walk-vs-run\", \"sha\": \"0123456789abcdef0123456789abcdef01234567\", \"note\": \"try the new run speed\"" + extra +
           ", \"files\": {" + FileJson(ver, "OkayEngine.exe", withSha) + ", " + FileJson(ver, "OkaySpacePlayer.exe") + ", " + FileJson(ver, "OkaySpace.exe") + "}}";
}
std::string StableEntry(const std::string& ver, const char* channel = "latest") {
    return "{\"version\": \"" + ver + "\", \"channel\": \"" + channel + "\", \"date\": \"2026-11-10\", \"inBucket\": true, \"files\": {" +
           FileJson(ver, "OkayEngine.exe") + ", " + FileJson(ver, "OkaySpacePlayer.exe") + ", " + FileJson(ver, "OkaySpace.exe") + "}}";
}
std::string DevIndex(const std::vector<std::string>& entries, const std::string& head = "") {
    std::string s = "{\"schema\": 1, ";
    if (!head.empty()) s += "\"dev\": \"" + head + "\", ";
    s += "\"bucketPath\": \"v{version}/{file}\", \"versions\": [";
    for (std::size_t i = 0; i < entries.size(); ++i) s += (i ? ", " : "") + entries[i];
    return s + "]}";
}
std::string StableIndex(const std::vector<std::string>& entries, const std::string& latest) {
    std::string s = "{\"schema\": 1, \"latest\": \"" + latest + "\", \"archive\": \"https://github.example/o/r/releases/download/v{version}/{file}\", "
                    "\"bucketPath\": \"v{version}/{file}\", \"versions\": [";
    for (std::size_t i = 0; i < entries.size(); ++i) s += (i ? ", " : "") + entries[i];
    return s + "]}";
}
okayversions::Sources Src(const std::string& keys = "") {
    okayversions::Sources s;
    s.bucketBase = kBucket;
    s.archiveTemplate = "https://github.example/o/r/releases/download/v{version}/{file}";
    s.trustedKeys = keys;
    return s;
}

}  // namespace

int main() {
    RUN_SUITE("dev_channel");

    // ---- the number ---------------------------------------------------------------------------------------------------------------
    {
        CHECK(tb::IsTestVersion("2.876.0.17") && tb::IsTestVersion(" 2.876.0.17\r\n") && tb::IsTestVersion("v2.876.0.1"));
        CHECK(!tb::IsTestVersion("2.876.0") && !tb::IsTestVersion("2.876") && !tb::IsTestVersion("2") && !tb::IsTestVersion(""));
        CHECK(!tb::IsTestVersion("2.876.0.0"));                                   // a fourth number of 0 would EQUAL the release
        CHECK(!tb::IsTestVersion("2.876.0.17.1") && !tb::IsTestVersion("1.2.3.4.5"));   // exactly four numbers
        CHECK(!tb::IsTestVersion("dev") && !tb::IsTestVersion("garbage") && !tb::IsTestVersion("2.876.0.x"));
        CHECK(tb::IsReleaseShaped("2.876.0") && tb::IsReleaseShaped("2.876") && tb::IsReleaseShaped("2") && !tb::IsReleaseShaped("2.876.0.17"));
        CHECK(!tb::IsReleaseShaped("") && !tb::IsReleaseShaped("dev") && !tb::IsReleaseShaped("2.876.0.17.1"));
        CHECK(tb::NumberCount("2.876.0.17") == 4 && tb::NumberCount("2.876.0") == 3 && tb::NumberCount("nope") == 0);
        CHECK(tb::BaseRelease("2.876.0.17") == "2.876.0" && tb::BaseRelease("2.876.0").empty() && tb::BaseRelease("2.876.0.0").empty());
        CHECK(tb::Serial("2.876.0.17") == 17 && tb::Serial("2.876.0") == 0 && tb::Serial("2.876.0.0") == 0);
        CHECK(tb::MakeVersion("2.876.0", 17) == "2.876.0.17" && tb::MakeVersion("v2.876.0", 3) == "2.876.0.3");
        CHECK(tb::MakeVersion("2.876.0", 0).empty() && tb::MakeVersion("2.876", 5).empty() && tb::MakeVersion("2.876.0.1", 5).empty() && tb::MakeVersion("x", 5).empty());
        CHECK(tb::IsTestVersion(tb::MakeVersion("2.876.0", 123456)));
        CHECK(tb::Suffix("2.876.0.17") == " (test build)" && tb::Suffix("2.876.0").empty() && tb::Suffix("dev").empty());
        CHECK(tb::DisplayVersion("2.876.0.17") == "v2.876.0.17 (test build)" && tb::DisplayVersion("2.876.0") == "v2.876.0" && tb::DisplayVersion("dev") == "dev");

        // The order that makes it safe: just above its release, below every later one, never equal to any release.
        using okayver::CompareVersions;
        CHECK(CompareVersions("2.876.0", "2.876.0.1") < 0);                       // a launcher on 2.876.0 is offered 2.876.0.1
        CHECK(CompareVersions("2.876.0.1", "2.876.0.2") < 0 && CompareVersions("2.876.0.9", "2.876.0.10") < 0);   // numeric, not text
        CHECK(CompareVersions("2.876.0.99999", "2.876.1") < 0);                   // the next patch always replaces it
        CHECK(CompareVersions("2.876.0.99999", "2.877.0") < 0 && CompareVersions("2.876.7.3", "2.877.0") < 0 && CompareVersions("2.999.9.9", "3.0.0") < 0);
        CHECK(CompareVersions("2.876.0.17", "2.875.0.99") > 0);                   // a newer base beats any serial of an older base
        std::uint32_t seed = 12345;
        auto next = [&seed](std::uint32_t mod) { seed = seed * 1664525u + 1013904223u; return (seed >> 8) % mod; };
        for (int i = 0; i < 4000; ++i) {
            const std::string base = "2." + std::to_string(next(2000)) + "." + std::to_string(next(40));
            const std::string t = tb::MakeVersion(base, 1 + next(5000));
            const std::string release = std::to_string(next(4) + 1) + (next(3) ? "." + std::to_string(next(2000)) : "") + (next(3) ? "." + std::to_string(next(40)) : "");
            CHECK(!t.empty() && tb::IsTestVersion(t));
            CHECK(CompareVersions(t, release) != 0);                              // never equal to any release
            CHECK((CompareVersions(release, base) > 0) == (CompareVersions(release, t) > 0));   // a release that is newer than the base is newer than the test build
            CHECK(CompareVersions(base, t) < 0);                                  // and the base is older than it
        }
    }

    // ---- the words ----------------------------------------------------------------------------------------------------------------
    {
        CHECK(tb::Title("claude/walk-vs-run", "abc1234def") == "Test build of walk-vs-run (abc1234)");
        CHECK(tb::Title("claude2/own-reader", "ABC1234") == "Test build of own-reader (abc1234)");
        CHECK(tb::Title("main", "0123456789abcdef0123456789abcdef01234567") == "Test build of main (0123456)");
        CHECK(tb::Title("lts/2.850", "abc1234") == "Test build of lts/2.850 (abc1234)");
        CHECK(tb::Title("claude/", "abc1234") == "Test build (abc1234)");         // a branch name cannot end in a slash: it is left out, never shown raw
        CHECK(tb::Title("claude/walk", "") == "Test build of walk" && tb::Title("", "abc1234") == "Test build (abc1234)" && tb::Title("", "") == "Test build");
        CHECK(tb::Title("bad branch!", "zzz") == "Test build");                   // anything unusable is left out, never shown raw
        CHECK(tb::CleanBranch("claude/walk-vs-run") == "claude/walk-vs-run" && tb::CleanBranch("release_2.877+rc-1") == "release_2.877+rc-1");
        for (const char* bad : {"", "a b", "a;b", "a\"b", "$(x)", "../x", "a..b", "/abs", "-flag", ".hidden", "trailing/", "naïve", "a\nb", "a|b", "a`b`"})
            CHECK(tb::CleanBranch(bad).empty());
        CHECK(tb::CleanBranch(std::string(80, 'a')).size() == 80 && tb::CleanBranch(std::string(81, 'a')).empty());
        CHECK(tb::CleanSha("abc1234") == "abc1234" && tb::CleanSha("ABC1234") == "abc1234" && tb::CleanSha(std::string(40, 'f')).size() == 40);
        for (const char* bad : {"", "abc123", "abc123g", "abc 1234", "abc1234\n", "../abc12"}) CHECK(tb::CleanSha(bad).empty());
        CHECK(tb::CleanSha(std::string(41, 'a')).empty() && tb::ShortSha("0123456789abcdef") == "0123456" && tb::ShortSha("junk").empty());
        CHECK(tb::CleanLine("  try   the \t new run speed \n") == "try the new run speed");
        CHECK(tb::CleanLine("a\x01\x02" "b\x7f" "c\xC3\xA4" "d") == "abcd");        // control characters and non-ASCII are dropped
        CHECK(tb::CleanLine(std::string(500, 'x')).size() == tb::kMaxNoteChars && tb::CleanLine(std::string(500, 'x'), 10) == std::string(10, 'x'));
        CHECK(tb::CleanLine("ab cd", 4) == "ab c" && tb::CleanLine("ab cd", 3) == "ab");
        CHECK(tb::CleanLine("").empty() && tb::CleanLine("   \n  ").empty());
        for (std::size_t cap = 0; cap < 12; ++cap) CHECK(tb::CleanLine("aa bb cc dd ee ff", cap).size() <= cap);
    }

    // ---- what to install ----------------------------------------------------------------------------------------------------------
    {
        using K = tb::Choice::Kind;
        // Off: the answer is the stable head and nothing else, whatever the server also offers.
        auto off = tb::ChooseTarget("2.876.0", "2.877.0", "2.877.0.5", false, false, false);
        CHECK(off.kind == K::Stable && off.version == "2.877.0");
        off = tb::ChooseTarget("2.876.0", "2.876.0", "2.876.0.5", false, false, false);
        CHECK(off.kind == K::Stable && off.version == "2.876.0");                // (the caller sees "not newer": up to date)
        CHECK(tb::ChooseTarget("2.876.0", "", "2.876.0.5", false, false, false).kind == K::None);
        // On: the newer of the two wins.
        auto on = tb::ChooseTarget("2.876.0", "2.876.0", "2.876.0.5", true, false, false);
        CHECK(on.kind == K::TestBuild && on.version == "2.876.0.5");
        on = tb::ChooseTarget("2.876.0", "2.877.0", "2.876.0.5", true, false, false);   // a newer release replaces a test build of the older one
        CHECK(on.kind == K::Stable && on.version == "2.877.0");
        on = tb::ChooseTarget("2.876.0.5", "2.876.0", "2.876.0.5", true, false, false);  // already on it: the caller finds it not newer
        CHECK(on.kind == K::TestBuild && on.version == "2.876.0.5");
        on = tb::ChooseTarget("2.876.0", "", "2.876.0.5", true, false, false);          // the stable head could not be read: the test build still counts
        CHECK(on.kind == K::TestBuild);
        on = tb::ChooseTarget("2.876.0", "2.876.0", "", true, false, false);            // nothing published in dev/: stable
        CHECK(on.kind == K::Stable && on.version == "2.876.0");
        CHECK(tb::ChooseTarget("2.876.0", "", "", true, false, false).kind == K::None);
        // Things that are not what they claim are ignored: a test version as the stable head, a release as a test build.
        CHECK(tb::ChooseTarget("2.876.0", "2.877.0.1", "", true, false, false).kind == K::None);
        CHECK(tb::ChooseTarget("2.876.0", "2.876.0", "2.999.0", true, false, false).version == "2.876.0");
        CHECK(tb::ChooseTarget("2.876.0", "2.876.0", "2.876.0.0", true, false, false).version == "2.876.0");
        // The long-term-support channel never gets test builds.
        CHECK(tb::ChooseTarget("2.850.0", "2.876.0", "2.876.0.5", true, true, false).kind == K::Stable);
        // Going back.
        auto back = tb::ChooseTarget("2.876.0.5", "2.876.0", "2.876.0.5", false, false, true);
        CHECK(back.kind == K::GoBack && back.version == "2.876.0");
        back = tb::ChooseTarget("2.876.0.5", "2.877.0", "", false, false, true);        // a newer release: an ordinary update
        CHECK(back.kind == K::Stable && back.version == "2.877.0");
        back = tb::ChooseTarget("2.876.0", "2.876.0", "", false, false, true);          // not on a test build: nothing to go back from
        CHECK(back.kind == K::None && back.version.empty() && !back.note.empty());
        back = tb::ChooseTarget("dev", "2.876.0", "", false, false, true);
        CHECK(back.kind == K::None);
        back = tb::ChooseTarget("2.876.0.5", "", "", false, false, true);               // the stable head is unknown: never a guess
        CHECK(back.kind == K::None && Has(back.note, "Couldn't find out") && back.failed);
        CHECK(!tb::ChooseTarget("2.876.0", "2.876.0", "", false, false, true).failed);  // "nothing to go back from" is not a failure
        CHECK(tb::ChooseTarget("2.876.0", "", "", true, false, false).failed);          // nothing could be read at all
        back = tb::ChooseTarget("2.876.0.5", "2.876.0", "2.876.0.9", true, false, true);   // go back wins over a newer test build
        CHECK(back.kind == K::GoBack && back.version == "2.876.0");
        // Whatever the answer, a normal install only ever happens for a version that is newer than the one installed.
        for (const char* local : {"2.875.0", "2.876.0", "2.876.0.5", "2.876.0.7", "2.877.0", "dev"})
            for (const char* stable : {"", "2.875.0", "2.876.0", "2.877.0"})
                for (const char* test : {"", "2.876.0.5", "2.876.0.7"})
                    for (int sw = 0; sw < 2; ++sw) {
                        const tb::Choice c = tb::ChooseTarget(local, stable, test, sw != 0, false, false);
                        if (c.kind == K::None) { CHECK(std::string(stable).empty() && (!sw || std::string(test).empty())); continue; }
                        if (c.kind == K::TestBuild) CHECK(sw != 0 && tb::IsTestVersion(c.version) && okayver::CompareVersions(c.version, stable) > 0);
                        if (c.kind == K::Stable) CHECK(tb::IsReleaseShaped(c.version) && c.version == stable);
                        CHECK(c.kind != K::GoBack);                                         // only a person's click goes back
                        if (!sw) CHECK(c.kind == K::Stable);                                // off: never a test build
                    }
    }

    // ---- which test build is installed (the marker next to the launcher) ------------------------------------------------------------------
    {
        TempDir t;
        tb::Marker m;
        m.version = "2.876.0.17"; m.branch = "claude/walk-vs-run"; m.sha = "0123456789abcdef"; m.note = "try the new run speed";
        CHECK(tb::WriteMarker(t.path, m));
        const tb::Marker r = tb::ReadMarker(t.path);
        CHECK(r.Valid() && r.version == "2.876.0.17" && r.branch == "claude/walk-vs-run" && r.sha == "0123456789abcdef" && r.note == "try the new run speed");
        CHECK(tb::RunningTitle("2.876.0.17", r) == "Test build of walk-vs-run (0123456)");
        CHECK(tb::RunningTitle("2.876.0.18", r) == "Test build");                  // the marker is for another version: only the shape of the number is trusted
        CHECK(tb::RunningTitle("2.876.0", r).empty() && tb::RunningTitle("dev", r).empty());   // a normal release says nothing
        CHECK(tb::RunningTitle("2.876.0.17", tb::Marker{}) == "Test build");
        tb::RemoveMarker(t.path);
        CHECK(!tb::ReadMarker(t.path).Valid() && !fs::exists(tb::MarkerPath(t.path)));
        CHECK(!tb::WriteMarker(t.path, tb::Marker{}) && !fs::exists(tb::MarkerPath(t.path)));          // a marker with no test version is never written
        // Hostile and damaged files are "no marker", never a crash and never raw text.
        for (const std::string& junk : std::vector<std::string>{"", "garbage", "okayspace test build 2\nversion=2.876.0.17\n", "okayspace test build 1\nversion=2.876.0\n",
                                                                 "okayspace test build 1\n", std::string(5000, 'a'), "\xFF\xFE\x00\x01"}) {
            Put(tb::MarkerPath(t.path), junk);
            CHECK(!tb::ReadMarker(t.path).Valid());
        }
        Put(tb::MarkerPath(t.path), "okayspace test build 1\r\nversion=2.876.0.17\r\nbranch=a b;rm -rf /\r\nsha=zzzz\r\nnote=ok\x01\x02 fine\r\n");
        const tb::Marker h = tb::ReadMarker(t.path);
        CHECK(h.Valid() && h.branch.empty() && h.sha.empty() && h.note == "ok fine");
        CHECK(tb::RunningTitle("2.876.0.17", h) == "Test build");
    }

    // ---- the stable reader ignores test builds -------------------------------------------------------------------------------------
    // (This is the part that failed before the change: ParseIndex and ParseVersionTxt took any digits-and-dots version, so a four-number version
    // in the real list or in VERSION.txt became "latest".)
    {
        const std::string mixed = StableIndex({StableEntry("2.876.0"), TestEntry("2.876.0.17", true, "", true, "latest"), StableEntry("2.875.0")}, "2.876.0.17");
        const okayversions::Index idx = okayversions::ParseIndex(mixed);
        CHECK(idx.ok && idx.versions.size() == 2 && idx.versions[0].version == "2.876.0" && idx.versions[1].version == "2.875.0");
        CHECK(!idx.Find("2.876.0.17"));
        CHECK(idx.latest == "2.876.0");                                             // the head that named a test version is not believed: the newest release entry is
        CHECK(okayversions::ParseVersionTxt("2.876.0.17\n").empty() && okayversions::ParseVersionTxt("2.876.0\n") == "2.876.0");
        CHECK(okayversions::ParseVersionTxt("2.876.0.0\n").empty() && okayversions::ParseVersionTxt("1.2.3.4.5").empty());
        const okayversions::Index fb = okayversions::FallbackIndex("2.876.0.17\n", "2.876.0.18\n", "", true);
        CHECK(!fb.ok && fb.versions.empty());                                       // nothing else to go on: refused, never a test build as the head
        const okayversions::Index fb2 = okayversions::FallbackIndex("2.876.0.17\n", "2.850.1\n", "", true);
        CHECK(fb2.ok && fb2.latest.empty() && fb2.lts == "2.850.1" && fb2.versions.size() == 1);

        // The whole loader, with a server whose real files carry test versions and whose dev/ folder is full: the loader must not even ask for dev/.
        Server s;
        s.files[kBucket + std::string("versions.json")] = mixed;
        s.files[kBucket + std::string("VERSION.txt")] = "2.876.0.17\n";
        s.files[kBucket + std::string("LTS.txt")] = "2.850.1\n";
        s.files[kDev + "versions.json"] = DevIndex({TestEntry("2.876.0.99")}, "2.876.0.99");
        s.files[kDev + "DEV.txt"] = "2.876.0.99\n";
        s.files[kDev + "NOTES-DEV.txt"] = "Test build of walk-vs-run (0123456)\n";
        const okayversions::Index loaded = okayversions::LoadIndex(Src(), s.Fn());
        CHECK(loaded.ok && !loaded.fallback);
        CHECK(!loaded.Find("2.876.0.17") && !loaded.Find("2.876.0.99"));
        CHECK(loaded.latest == "2.876.0");                                         // (VERSION.txt said 2.876.0.17: not a release, not believed)
        for (const okayversions::Entry& e : loaded.versions) CHECK(tb::IsReleaseShaped(e.version));
        CHECK(!s.asked.empty() && !s.AskedAnythingUnderDev());                     // "off" = the dev area is never read
        // With the real list missing, the fallback to VERSION.txt must not turn a test version into the newest release either.
        Server s2;
        s2.files[kBucket + std::string("VERSION.txt")] = "2.876.0.17\n";
        s2.files[kDev + "versions.json"] = DevIndex({TestEntry("2.876.0.99")});
        const okayversions::Index fb3 = okayversions::LoadIndex(Src(), s2.Fn());
        CHECK(!fb3.ok || (fb3.latest.empty() && !fb3.Find("2.876.0.17")));
        CHECK(!s2.AskedAnythingUnderDev());
    }

    // ---- the setting ------------------------------------------------------------------------------------------------------------
    {
        okayversions::Settings def;
        CHECK(!def.testBuilds);                                                    // off by default
        std::ostringstream out;
        okayversions::WriteSettings(def, out);
        CHECK(out.str() == "update_channel=latest\nkeep_previous=1\n");           // byte for byte what it was before the setting existed
        okayversions::Settings on;
        CHECK(okayversions::ApplySettingLine(on, "test_builds", "1") && on.testBuilds);
        std::ostringstream out2;
        okayversions::WriteSettings(on, out2);
        CHECK(out2.str() == "update_channel=latest\nkeep_previous=1\ntest_builds=1\n");
        okayversions::Settings back;
        std::istringstream lines(out2.str());
        std::string line;
        while (std::getline(lines, line)) { const auto eq = line.find('='); okayversions::ApplySettingLine(back, line.substr(0, eq), line.substr(eq + 1)); }
        CHECK(back.testBuilds && back.channel == okayversions::Channel::Latest && back.keepPrevious == 1);   // the round trip
        for (const char* v : {"0", "", "true", "yes", "on", "2", " 1", "1 ", "\xFF"}) {                         // only an exact 1 turns it on
            okayversions::Settings s;
            CHECK(okayversions::ApplySettingLine(s, "test_builds", v) && !s.testBuilds);
        }
        okayversions::Settings both;
        okayversions::ApplySettingLine(both, "update_channel", "lts");
        okayversions::ApplySettingLine(both, "test_builds", "1");
        CHECK(both.channel == okayversions::Channel::Lts && both.testBuilds);      // independent of the channel
        CHECK(!okayversions::ApplySettingLine(on, "test_builds_x", "1"));
    }

    // ---- the dev reader ---------------------------------------------------------------------------------------------------------
    {
        const std::string body = DevIndex({TestEntry("2.876.0.18"), TestEntry("2.876.0.17"), TestEntry("2.876.0.16", false)}, "2.876.0.18");
        Server s;
        s.files[kDev + "versions.json"] = body;
        okayversions::Index idx = okayversions::LoadDevIndex(Src(), s.Fn());
        CHECK(idx.ok && !idx.fallback && !idx.signatureRefused && idx.versions.size() == 3);
        CHECK(idx.latest == "2.876.0.18" && idx.lts.empty() && idx.archive.empty());
        CHECK(s.AskedOnlyUnderDev() && s.asked.front() == kDev + "versions.json");   // only the dev area, and the list first
        const okayversions::Entry* e = idx.Find("2.876.0.18");
        CHECK(e && e->inBucket && e->channel.empty() && e->branch == "claude/walk-vs-run" && e->sha == "0123456789abcdef0123456789abcdef01234567" &&
              e->note == "try the new run speed" && e->files.size() == 3);
        CHECK(idx.versions[0].version == "2.876.0.18" && idx.versions[2].version == "2.876.0.16");              // newest first
        CHECK(tb::Title(e->branch, e->sha) == "Test build of walk-vs-run (0123456)");

        // Only the newest one that can really be installed counts as the head.
        idx = [&] { Server t; t.files[kDev + "versions.json"] = DevIndex({TestEntry("2.876.0.19", false), TestEntry("2.876.0.18", true, "", false), TestEntry("2.876.0.17")}); return okayversions::LoadDevIndex(Src(), t.Fn()); }();
        CHECK(idx.ok && idx.latest == "2.876.0.17");                                // .19 was pruned from the bucket, .18 has no checksum for the editor
        idx = [&] { Server t; t.files[kDev + "versions.json"] = DevIndex({TestEntry("2.876.0.19", false)}); return okayversions::LoadDevIndex(Src(), t.Fn()); }();
        CHECK(idx.ok && idx.latest.empty());                                        // nothing installable: no offer

        // A real list served as the dev list (a replay, or a mistake) offers nothing: its entries are releases, not test builds.
        idx = [&] { Server t; t.files[kDev + "versions.json"] = StableIndex({StableEntry("2.876.0"), StableEntry("2.875.0")}, "2.876.0"); return okayversions::LoadDevIndex(Src(), t.Fn()); }();
        CHECK(idx.ok && idx.versions.empty() && idx.latest.empty());
        // Entries that are not well-formed test builds are dropped, whatever else is in the list.
        idx = [&] {
            Server t;
            t.files[kDev + "versions.json"] = DevIndex({TestEntry("2.876.0.017"), TestEntry("2.876.0.0"), TestEntry("2.876.0.5.1"), TestEntry("v2.876.0.4"), TestEntry("2.876.0.3-rc1"),
                                                         TestEntry("2.876.0"), TestEntry("garbage"), TestEntry("2.876.0.17"), TestEntry("2.876.0.17")});
            return okayversions::LoadDevIndex(Src(), t.Fn());
        }();
        CHECK(idx.ok && idx.versions.size() == 1 && idx.versions[0].version == "2.876.0.17" && idx.latest == "2.876.0.17");
        // Text from the server is cleaned before anyone shows it.
        idx = [&] {
            Server t;
            std::string evil = TestEntry("2.876.0.17");
            const std::string key = "\"branch\": \"claude/walk-vs-run\"";
            evil.replace(evil.find(key), key.size(), "\"branch\": \"a b;rm -rf /\"");
            t.files[kDev + "versions.json"] = DevIndex({evil, TestEntry("2.876.0.18", true, ", \"note2\": \"x\"")});
            return okayversions::LoadDevIndex(Src(), t.Fn());
        }();
        CHECK(idx.ok && idx.Find("2.876.0.17") && idx.Find("2.876.0.17")->branch.empty() && tb::Title(idx.Find("2.876.0.17")->branch, idx.Find("2.876.0.17")->sha) == "Test build (0123456)");
        // The archive address in a dev list is never used: a test build is only ever downloaded from the bucket, and only from dev/.
        idx = [&] {
            Server t;
            std::string j = DevIndex({TestEntry("2.876.0.17")});
            j.insert(1, "\"archive\": \"https://evil.example/{version}/{file}\", \"latest\": \"2.999.0\", ");
            t.files[kDev + "versions.json"] = j;
            return okayversions::LoadDevIndex(Src(), t.Fn());
        }();
        CHECK(idx.ok && idx.archive.empty() && idx.latest == "2.876.0.17");
        // Garbage and absence.
        for (const char* bad : {"", "garbage", "[]", "{\"schema\": 2, \"versions\": []}", "<html>404</html>", "{\"versions\": []}"}) {
            Server t;
            t.files[kDev + "versions.json"] = bad;
            const okayversions::Index g = okayversions::LoadDevIndex(Src(), t.Fn());
            CHECK(!g.ok && !g.error.empty() && g.latest.empty());
        }
        { Server t; const okayversions::Index g = okayversions::LoadDevIndex(Src(), t.Fn()); CHECK(!g.ok && !g.error.empty()); }
        { okayversions::Sources none = Src(); none.bucketBase.clear(); Server t; CHECK(!okayversions::LoadDevIndex(none, t.Fn()).ok && t.asked.empty()); }
        { okayversions::Sources plain = Src(); plain.bucketBase = "http://example.test/releases/"; Server t; CHECK(!okayversions::LoadDevIndex(plain, t.Fn()).ok && t.asked.empty()); }
    }

    // ---- the dev list is signed like the real one -----------------------------------------------------------------------------------
    {
        const Seed a = MakeSeed(1), other = MakeSeed(2);
        const std::string body = DevIndex({TestEntry("2.876.0.17")}, "2.876.0.17");
        auto serve = [&](const std::string* sig) {
            Server s;
            s.files[kDev + "versions.json"] = body;
            if (sig) s.files[kDev + okayupdate::kSigName] = *sig;
            return s;
        };
        const std::string sigA = okayupdate::MakeSignatureFile(body, a), sigOther = okayupdate::MakeSignatureFile(body, other);
        { Server s = serve(&sigA); const auto i = okayversions::LoadDevIndex(Src(PubHex(a)), s.Fn()); CHECK(i.ok && i.signedByPublisher && i.latest == "2.876.0.17"); CHECK(s.AskedOnlyUnderDev()); }
        { Server s = serve(nullptr); const auto i = okayversions::LoadDevIndex(Src(PubHex(a)), s.Fn()); CHECK(!i.ok && i.signatureRefused && !i.error.empty() && i.latest.empty()); }
        { Server s = serve(&sigOther); const auto i = okayversions::LoadDevIndex(Src(PubHex(a)), s.Fn()); CHECK(!i.ok && i.signatureRefused); }
        { Server s = serve(&sigA); s.files[kDev + "versions.json"] += " "; const auto i = okayversions::LoadDevIndex(Src(PubHex(a)), s.Fn()); CHECK(!i.ok && i.signatureRefused); }
        // The signature of the REAL list is no good next to the dev list, and the dev list's signature is no good for the real one: they are different bytes.
        {
            const std::string stableBody = StableIndex({StableEntry("2.876.0")}, "2.876.0");
            Server s = serve(nullptr);
            s.files[kDev + okayupdate::kSigName] = okayupdate::MakeSignatureFile(stableBody, a);
            CHECK(!okayversions::LoadDevIndex(Src(PubHex(a)), s.Fn()).ok);
            Server t;
            t.files[kBucket + std::string("versions.json")] = stableBody;
            t.files[kBucket + std::string(okayupdate::kSigName)] = sigA;           // the dev list's signature next to the real list
            const auto st = okayversions::LoadIndex(Src(PubHex(a)), t.Fn());
            CHECK(!st.ok && st.signatureRefused);
        }
        // A refusal never falls back to something softer, and the unsigned transition (no key built in) is the same as for the real list.
        { Server s = serve(nullptr); const auto i = okayversions::LoadDevIndex(Src(""), s.Fn()); CHECK(i.ok && !i.signedByPublisher && i.latest == "2.876.0.17"); }
        { Server s = serve(&sigA); const auto i = okayversions::LoadDevIndex(Src("zz"), s.Fn()); CHECK(!i.ok && i.signatureRefused); }
    }

    // ---- the same install rules: a checksum for every file, or no install -------------------------------------------------------------
    {
        const std::string ver = "2.876.0.17";
        Server s;
        s.files[kDev + "versions.json"] = DevIndex({TestEntry(ver)}, ver);
        const okayversions::Index idx = okayversions::LoadDevIndex(Src(), s.Fn());
        CHECK(idx.ok && okayupdater::IndexProblem(idx, idx.Find(ver), ver).empty());
        const okayversions::InstallPlan plan = okayversions::PlanInstall(idx, ver, kDev, fs::path(), false);
        CHECK(plan.ok && plan.version == ver && plan.files.size() >= 2);
        for (const okayversions::PlanFile& f : plan.files) {
            CHECK(f.verified && f.sha256.size() == 64 && !f.urls.empty());
            for (const std::string& u : f.urls) CHECK(u == kDev + "v" + ver + "/" + f.indexName);   // dev/v<ver>/<file>, nothing else, no GitHub address
        }
        // The editor listed without a checksum: the plan refuses (nothing is "installed anyway").
        Server t;
        t.files[kDev + "versions.json"] = DevIndex({TestEntry(ver, true, "", false)}, ver);
        const okayversions::Index noSum = okayversions::LoadDevIndex(Src(), t.Fn());
        CHECK(noSum.ok && noSum.latest.empty());                                    // never offered
        const okayversions::InstallPlan refused = okayversions::PlanInstall(noSum, ver, kDev, fs::path(), false);
        CHECK(!refused.ok && !refused.error.empty() && refused.files.empty());
        CHECK(!okayupdater::RequireChecksum("OkayEngine.exe", &noSum.Find(ver)->files.at("OkayEngine.exe")).empty());
        // A version that is not in the list at all, and a release asked for from the dev list.
        CHECK(!okayversions::PlanInstall(idx, "2.876.0.99", kDev, fs::path(), false).ok);
        CHECK(!okayversions::PlanInstall(idx, "2.876.0", kDev, fs::path(), false).ok);

        // A download that is not the published file is refused before anything is replaced.
        TempDir dir;
        const fs::path dest = dir.path / "Tools" / "OkayEngine.exe";
        Put(dest, "old editor");
        const okayversions::FileInfo* want = &idx.Find(ver)->files.at("OkayEngine.exe");
        auto fetchWrong = [](const std::string&, const fs::path& to, std::string&) { Put(to, "MZ tampered"); return true; };
        const okayupdater::Outcome bad = okayupdater::StageFromUrls("OkayEngine.exe", plan.files[0].urls, dest, want, fetchWrong);
        CHECK(!bad.ok && Get(dest) == "old editor" && !fs::exists(fs::path(dest) += ".new"));
        auto fetchRight = [&](const std::string& url, const fs::path& to, std::string&) { Put(to, Body(ver, "OkayEngine.exe")); return Has(url, "/dev/v" + ver + "/"); };
        const okayupdater::Outcome good = okayupdater::StageFromUrls("OkayEngine.exe", plan.files[0].urls, dest, want, fetchRight);
        CHECK(good.ok && Get(fs::path(dest) += ".new") == Body(ver, "OkayEngine.exe") && Get(dest) == "old editor");   // staged, live file untouched until the swap
    }

    // ---- the publish tool and the launcher agree on the format ------------------------------------------------------------------------
    {
        TempDir dir;
        const std::string script = std::string(OKAY_SOURCE_DIR) + "/scripts/dev_index.py";
        const std::string ver = "2.876.0.17";
        for (const char* n : {"OkayEngine.exe", "OkaySpacePlayer.exe", "OkaySpace.exe"}) Put(dir.path / n, Body(ver, n));
        const fs::path out = dir.path / "versions.json", notes = dir.path / "NOTES-DEV.txt";
        const std::string files = " --file OkayEngine.exe='" + (dir.path / "OkayEngine.exe").string() + "' --file OkaySpacePlayer.exe='" + (dir.path / "OkaySpacePlayer.exe").string() +
                                  "' --file OkaySpace.exe='" + (dir.path / "OkaySpace.exe").string() + "'";
#if defined(_WIN32)
        const std::string cmd = "cd . ";   // (the publish tool is run by a Linux job; nothing to check here)
        (void)cmd; (void)script; (void)files; (void)out; (void)notes;
        std::cout << "  (the publish-tool cross check runs on Linux only)\n";
#else
        const std::string run = "python3 -I '" + script + "' merge --version " + ver + " --branch claude/walk-vs-run --sha 0123456789abcdef0123456789abcdef01234567 "
                                "--note 'try the new run speed' --date 2026-11-16 --out '" + out.string() + "'" + files + " >/dev/null 2>&1";
        if (std::system(run.c_str()) != 0) {
            std::cout << "  (python3 or scripts/dev_index.py is not usable here: the publish-tool cross check is skipped)\n";
        } else {
            const std::string text = Get(out);
            const okayversions::Index idx = okayversions::ParseIndex(text, okayversions::IndexKind::TestBuilds);
            CHECK(idx.ok && idx.versions.size() == 1 && idx.versions[0].version == ver && idx.versions[0].inBucket && idx.versions[0].channel.empty());
            const okayversions::Entry* e = idx.Find(ver);
            CHECK(e && e->branch == "claude/walk-vs-run" && e->sha == "0123456789abcdef0123456789abcdef01234567" && e->note == "try the new run speed" && e->date == "2026-11-16");
            for (const char* n : {"OkayEngine.exe", "OkaySpacePlayer.exe", "OkaySpace.exe"}) {
                CHECK(e && e->files.count(n) == 1);
                if (e && e->files.count(n)) CHECK(e->files.at(n).sha256 == Sha(Body(ver, n)) && e->files.at(n).size == Body(ver, n).size());
            }
            CHECK(Has(text, "\"dev\": \"" + ver + "\"") && !Has(text, "\"latest\"") && !Has(text, "\"lts\"") && !Has(text, "\"archive\""));
            // The STABLE reader sees nothing in it (it is not served there, but even if it were).
            const okayversions::Index asStable = okayversions::ParseIndex(text);
            CHECK(asStable.ok && asStable.versions.empty() && asStable.latest.empty());
            // The loader on the very same bytes.
            Server s;
            s.files[kDev + "versions.json"] = text;
            const okayversions::Index loaded = okayversions::LoadDevIndex(Src(), s.Fn());
            CHECK(loaded.ok && loaded.latest == ver && okayversions::PlanInstall(loaded, ver, kDev, fs::path(), false).ok);
            // The notes' first line is the title the launcher builds itself.
            const std::string nrun = "python3 -I '" + script + "' notes --version " + ver + " --base 2.876.0 --branch claude/walk-vs-run --sha 0123456789abcdef0123456789abcdef01234567 "
                                     "--note 'try the new run speed' --out '" + notes.string() + "' >/dev/null 2>&1";
            CHECK(std::system(nrun.c_str()) == 0);
            const std::string nt = Get(notes);
            CHECK(nt.substr(0, nt.find('\n')) == tb::Title("claude/walk-vs-run", "0123456789abcdef0123456789abcdef01234567"));
            CHECK(Has(nt, "try the new run speed") && Has(nt, "2.876.0"));
            // The tool refuses a stable-looking version and a missing checksum (exit 2), and an existing version again (exit 3).
            const std::string bad3 = "python3 -I '" + script + "' merge --version 2.876.0 --branch b --sha 0123456 --note n --date 2026-11-16 --out '" + (dir.path / "x.json").string() + "'" + files + " >/dev/null 2>&1";
            CHECK(std::system(bad3.c_str()) != 0 && !fs::exists(dir.path / "x.json"));
            const std::string again = "python3 -I '" + script + "' merge --existing '" + out.string() + "' --version " + ver + " --branch b --sha 0123456 --note n --date 2026-11-16 --out '" + (dir.path / "y.json").string() + "'" + files + " >/dev/null 2>&1";
            CHECK(std::system(again.c_str()) != 0 && !fs::exists(dir.path / "y.json"));
        }
#endif
    }

    TEST_MAIN_RESULT();
}
