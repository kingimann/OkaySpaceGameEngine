// The launcher's Versions logic (launcher/VersionsManager.cpp, launcher/VersionsNet.cpp): the shared version comparison, reading
// versions.json (garbage, missing fields, unknown schema, huge numbers) and the fallback to VERSION.txt / LTS.txt, where a download
// comes from, installing / removing / pruning on a temp folder with an injected downloader (cancel, checksum mismatch, a full disk
// at every step: nothing is ever left half installed), keeping the current editor when the launcher updates, the Latest / LTS
// channel, project badges, "Open with" command lines, the hand-off for `--versions`, and the cancellable curl runner (with a stub
// curl: no network). No window, no sleeping on a clock, nothing outside temp folders.
#include "test_framework.hpp"
#include "VersionsManager.hpp"
#include "VersionsNet.hpp"
#include "OfficialGame.hpp"   // the bundled SHA-256 (the same one the launcher checks downloads with)

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>

#if !defined(_WIN32)
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace okayversions;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        static int n = 0;
        path = fs::temp_directory_path() / ("okay_lver_" + std::to_string(++n) + "_" +
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
std::string Get(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
std::string Sha(const std::string& s) { return okay::account::detail::Sha256Hex(s); }
bool Has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

/// Every file and folder under `root` with its size (a content hash for files): two snapshots are equal when nothing changed.
std::map<std::string, std::string> Snapshot(const fs::path& root) {
    std::map<std::string, std::string> out;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string rel = fs::relative(it->path(), root, ec).generic_string();
        std::error_code e2;
        out[rel] = it->is_directory(e2) ? "<dir>" : Sha(Get(it->path()));
    }
    return out;
}
bool NoScratch(const fs::path& launcherDir) {
    std::error_code ec;
    for (fs::directory_iterator it(okayver::VersionsDir(launcherDir), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string n = it->path().filename().string();
        if (n.rfind(".partial-", 0) == 0 || n.rfind(".removing-", 0) == 0) return false;
    }
    return true;
}
bool NoPartFiles(const fs::path& root) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
        if (it->path().extension() == ".part") return false;
    return true;
}

// ---- a fake release ------------------------------------------------------------------------------------------------------

std::string Body(const std::string& ver, const std::string& file) { return "MZ fake " + file + " v" + ver + std::string(700 + ver.size() * 3 + file.size(), 'x'); }
std::string EditorOf(const std::string& ver) { return Body(ver, "OkayEngine.exe"); }

std::string FileJson(const std::string& ver, const char* file) {
    const std::string b = Body(ver, file);
    return std::string("\"") + file + "\": {\"size\": " + std::to_string(b.size()) + ", \"sha256\": \"" + Sha(b) + "\"}";
}
std::string EntryJson(const std::string& ver, const char* channel, bool inBucket, bool withSdl = false) {
    std::string s = "{\"version\": \"" + ver + "\", \"channel\": \"" + channel + "\", \"date\": \"2026-11-10\", \"inBucket\": " +
                    (inBucket ? "true" : "false") + ", \"files\": {" + FileJson(ver, "OkayEngine.exe") + ", " + FileJson(ver, "OkaySpacePlayer.exe") + ", " +
                    FileJson(ver, "OkaySpace.exe");
    if (withSdl) s += ", " + FileJson(ver, "SDL2.dll");
    return s + "}}";
}
const char* kBucket = "https://example.test/storage/v1/object/public/releases/";
const char* kArchive = "https://github.example/o/r/releases/download/v{version}/{file}";

std::string IndexJson(const std::vector<std::string>& entries, const std::string& latest, const std::string& lts) {
    std::string s = "{\"schema\": 1, \"latest\": \"" + latest + "\", \"lts\": \"" + lts + "\", \"archive\": \"" + kArchive +
                    "\", \"bucketPath\": \"v{version}/{file}\", \"versions\": [";
    for (std::size_t i = 0; i < entries.size(); ++i) s += (i ? ", " : "") + entries[i];
    return s + "]}";
}

/// What the "server" has: url -> bytes. The downloader writes in two chunks and tells the caller after each.
struct Server {
    std::map<std::string, std::string> files;
    std::vector<std::string> requested;
    int cancelAfterChunks = -1;       // call `cancel` after this many chunks in total
    int chunks = 0;
    bool diskFullOnNth = false;
    int nth = 0, failNth = -1;
    void AddRelease(const std::string& ver, bool bucket, bool archive, bool withSdl = false) {
        for (const char* f : {"OkayEngine.exe", "OkaySpacePlayer.exe", "SDL2.dll"}) {
            if (std::string(f) == "SDL2.dll" && !withSdl) continue;
            if (bucket) files[kBucket + std::string("v") + ver + "/" + f] = Body(ver, f);
            if (archive) files["https://github.example/o/r/releases/download/v" + ver + "/" + f] = Body(ver, f);
        }
    }
    Server Clone() const { Server c; c.files = files; return c; }      // the same files, fresh counters
    DownloadFn Fn(std::function<bool()> cancel = {}) {
        return [this, cancel](const std::string& url, const fs::path& dest, std::uint64_t maxBytes,
                              const std::function<void(std::uint64_t)>& onBytes, const std::function<bool()>& cancelled) {
            DownloadResult r;
            requested.push_back(url);
            if (nth++ == failNth) { r.status = diskFullOnNth ? DownloadStatus::DiskFull : DownloadStatus::Failed; r.error = "boom"; return r; }
            const auto it = files.find(url);
            if (it == files.end()) { r.error = "404"; return r; }
            const std::string& body = it->second;
            if (body.size() > maxBytes) { r.error = "too big"; return r; }
            const std::size_t half = body.size() / 2;
            std::ofstream out(dest, std::ios::binary | std::ios::trunc);
            out.write(body.data(), (std::streamsize)half);
            out.flush();
            onBytes(half);
            ++chunks;
            if ((cancelAfterChunks >= 0 && chunks > cancelAfterChunks) || cancelled()) { r.status = DownloadStatus::Cancelled; return r; }
            out.write(body.data() + half, (std::streamsize)(body.size() - half));
            out.flush();
            onBytes(body.size());
            r.status = DownloadStatus::Ok;
            return r;
        };
    }
};

IO MakeIO(Server& s) {
    IO io;
    io.download = s.Fn();
    io.availableSpace = [](const fs::path&, std::error_code& ec) { ec.clear(); return (std::uintmax_t)64 * 1024 * kMiB; };
    return io;
}

/// A launcher folder with a current install (the running version) in Tools/.
fs::path MakeLauncher(const fs::path& root, const std::string& ver, bool withSdl = true) {
    fs::path dir = root / "Okay Space L\xC3\xA4uncher \xE6\x97\xA5\xE6\x9C\xAC";   // spaces and unicode
    Put(dir / "Tools" / okayver::EditorExeName(), EditorOf(ver));
    Put(dir / "Tools" / "OkaySpacePlayer.exe", Body(ver, "OkaySpacePlayer.exe"));
    if (withSdl) Put(dir / "Tools" / "SDL2.dll", Body("sdl", "SDL2.dll"));
    Put(dir / "OkaySpace.exe", Body(ver, "OkaySpace.exe"));
    return dir;
}

/// The Install plan for `ver` out of `index`, with the launcher's own SDL2.dll as the local copy.
InstallPlan PlanFor(const Index& idx, const std::string& ver, const fs::path& launcher, bool bucket = true) {
    return PlanInstall(idx, ver, bucket ? kBucket : "", launcher / "Tools" / "SDL2.dll", true);
}

bool Complete(const fs::path& launcher, const std::string& ver) {
    const fs::path tools = okayver::VersionsDir(launcher) / ver / "Tools";
    return okayver::IsInstalled(launcher, ver) && fs::is_regular_file(tools / "OkaySpacePlayer.exe") && fs::is_regular_file(tools / "SDL2.dll") &&
           Get(tools / okayver::EditorExeName()) == EditorOf(ver) && Get(tools / "OkaySpacePlayer.exe") == Body(ver, "OkaySpacePlayer.exe");
}

// The old launcher's own comparison, kept here as the reference for "behaviour for every current caller".
int OldCompareVersions(const std::string& a, const std::string& b) {
    auto parse = [](const std::string& s) {
        std::vector<int> v; std::stringstream ss(s); std::string tok;
        while (std::getline(ss, tok, '.')) { int n = 0; try { n = std::stoi(tok); } catch (...) { n = 0; } v.push_back(n); }
        return v;
    };
    std::vector<int> va = parse(a), vb = parse(b);
    std::size_t n = std::max(va.size(), vb.size());
    for (std::size_t i = 0; i < n; ++i) {
        int x = i < va.size() ? va[i] : 0, y = i < vb.size() ? vb[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

}  // namespace

int main() {
    RUN_SUITE("launcher_versions");

    // ---- the shared comparison replaces the launcher's own --------------------------------------------------------------------
    {
        // Every value the launcher compares: published versions (VERSION.txt, LTS.txt: digits and dots), the compiled version, and "dev".
        const std::vector<std::string> shapes = {"2.855.0", "2.855", "2", "2.9.0", "2.10.0", "2.850.1", "2.850.10", "2.850.2", "1.5.0", "1.05", "0.0.0", "10.0.1",
                                                 "2.855.0.0", "2.855.0.1", "2026.10.24.1", "123456789.1", "dev", ""};
        for (const std::string& a : shapes)
            for (const std::string& b : shapes) {
                // One deliberate difference: "0.0.0" used to equal text that is no version ("dev", ""); a real version is now newer than that.
                const bool zeroVsText = (a == "0.0.0" && (b == "dev" || b.empty())) || (b == "0.0.0" && (a == "dev" || a.empty()));
                if (zeroVsText) { CHECK(okayver::CompareVersions(a, b) != 0); continue; }
                CHECK(okayver::CompareVersions(a, b) == OldCompareVersions(a, b));
            }
        CHECK(okayver::CompareVersions("2.855.0", "dev") == 1 && okayver::CompareVersions("dev", "2.855.0") == -1 && okayver::CompareVersions("dev", "dev") == 0);
        // Callers that strip a leading "v" (the What's New history) and callers that compare VERSION.txt text with a trailing newline.
        CHECK(okayver::CompareVersions("2.855.0\r\n", "2.855.0") == 0 && okayver::CompareVersions(" 2.856.0 ", "2.855.0") == 1);
        // A deliberate difference: a number too big for an int was 0 before; now it is just a big number (never an exception, never wrong sign).
        CHECK(okayver::CompareVersions("99999999999.1", "2.0") == 1);
        CHECK(okayver::CompareVersions("2.855.0-rc1", "2.855.0") == 0);
    }

    // ---- versions.json ---------------------------------------------------------------------------------------------------------
    {
        const std::string json = IndexJson({EntryJson("2.850.1", "lts", true), EntryJson("2.856.0", "latest", true), EntryJson("2.855.0", "latest", false)}, "2.856.0", "2.850.1");
        const Index idx = ParseIndex(json);
        CHECK(idx.ok && idx.schema == 1 && idx.error.empty() && !idx.fallback);
        CHECK(idx.latest == "2.856.0" && idx.lts == "2.850.1");
        CHECK(idx.versions.size() == 3 && idx.versions[0].version == "2.856.0" && idx.versions[1].version == "2.855.0" && idx.versions[2].version == "2.850.1");   // sorted, newest first
        CHECK(idx.archive == kArchive && idx.bucketPath == "v{version}/{file}");
        const Entry* e = idx.Find("v2.850.1");
        CHECK(e && e->channel == "lts" && e->inBucket && e->date == "2026-11-10" && e->files.size() == 3);
        CHECK(e && e->files.at("OkayEngine.exe").size == EditorOf("2.850.1").size() && e->files.at("OkayEngine.exe").sha256 == Sha(EditorOf("2.850.1")));
        CHECK(idx.Find("2.855") && !idx.Find("2.855")->inBucket && !idx.Find("9.9.9") && !idx.Find("junk") && !idx.Find(""));
        CHECK(ParseIndex("\xEF\xBB\xBF" + json).ok);                                  // a BOM
        CHECK(ParseIndex(json + "\n\n").ok);

        // Garbage never throws and always says why.
        for (const char* bad : {"", " ", "garbage", "[]", "null", "42", "\"x\"", "{", "{\"schema\": 1", "{\"schema\": 1,}", "{'schema': 1}", "{\"schema\": 1} trailing",
                                "\xFF\xFE\x00\x01", "<html>404 Not Found</html>", "{\"schema\":", "{\"schema\": [1]}"}) {
            const Index g = ParseIndex(std::string_view(bad, std::strlen(bad)));
            CHECK(!g.ok && !g.error.empty() && g.versions.empty());
        }
        CHECK(!ParseIndex(std::string("{\"schema\": 1, \"versions\": [") + std::string(4 * 1024 * 1024 + 1, ' ') + "]}").ok);   // far too big
        std::string deep = "{\"schema\": 1, \"x\": ";
        for (int i = 0; i < 100; ++i) deep += "[";
        CHECK(!ParseIndex(deep).ok);                                                  // nesting is limited, not recursed to the end

        // Schema: only exactly 1.
        CHECK(!ParseIndex("{\"versions\": []}").ok);
        for (const char* s : {"0", "2", "3", "-1", "1.0", "1e0", "\"1\"", "true", "null", "99999999999999999999", "1e30", "18446744073709551616"}) {
            const Index g = ParseIndex(std::string("{\"schema\": ") + s + ", \"latest\": \"2.856.0\", \"versions\": []}");
            CHECK(!g.ok && !g.error.empty());
        }
        CHECK(Has(ParseIndex("{\"schema\": 2, \"versions\": []}").error, "format 2") && Has(ParseIndex("{\"schema\": 2, \"versions\": []}").error, "Update the launcher"));
        CHECK(ParseIndex("{\"schema\": 1}").ok && ParseIndex("{\"schema\": 1, \"versions\": []}").versions.empty());   // no versions yet is fine

        // Unknown extra fields are ignored; what it can't use is skipped, the rest stays.
        const std::string odd = "{\"schema\": 1, \"future\": {\"a\": [1, 2, {\"b\": null}]}, \"latest\": \"2.856.0\", \"lts\": \"dev\", \"versions\": ["
                                "7, null, \"x\", [], {}, {\"version\": 5}, {\"version\": \"nope\"}, {\"version\": \"\"}, {\"version\": \"dev\"}, {\"version\": \"v2.856.0\"}, "
                                "{\"version\": \"2.856.0-evil\"}, {\"version\": \"2.856.0 (x)\"}, "
                                "{\"version\": \"2.856.0\", \"channel\": \"banana\", \"date\": \"2026-11-10\", \"extra\": 1, \"inBucket\": \"yes\", \"files\": {"
                                "\"OkayEngine.exe\": {\"size\": 10, \"sha256\": \"" + std::string(64, 'A') + "\", \"future\": true}, "
                                "\"../evil\": {\"size\": 10}, \"a/b.exe\": {\"size\": 10}, \"\": {\"size\": 10}, \"x y.exe\": {\"size\": 10}, "
                                "\"neg.exe\": {\"size\": -5}, \"huge.exe\": {\"size\": 1e30}, \"big.exe\": {\"size\": 99999999999999}, \"zero.exe\": {\"size\": 0}, "
                                "\"str.exe\": {\"size\": \"10\"}, \"frac.exe\": {\"size\": 10.5}, \"nosize.exe\": {\"sha256\": \"" + std::string(64, 'b') + "\"}, "
                                "\"badsha.exe\": {\"size\": 10, \"sha256\": \"xyz\"}, \"notobj.exe\": 5, "
                                "\"Player.exe\": {\"size\": 20}}}, "
                                "{\"version\": \"2.856.0\", \"channel\": \"lts\"}, "                       // a duplicate: the first one stays
                                "{\"version\": \"2.850.1\", \"channel\": \"lts\", \"files\": 7}]}";
        const Index o = ParseIndex(odd);
        CHECK(o.ok && o.versions.size() == 2 && o.versions[0].version == "2.856.0" && o.versions[1].version == "2.850.1");
        CHECK(o.latest == "2.856.0" && o.lts == "2.850.1");                              // "dev" is no head: the lts head is derived from the entries
        const Entry* oe = o.Find("2.856.0");
        CHECK(oe && oe->channel == "" && !oe->inBucket && oe->date == "2026-11-10");
        CHECK(oe && oe->files.size() == 3 && oe->files.count("OkayEngine.exe") && oe->files.count("badsha.exe") && oe->files.count("Player.exe"));
        CHECK(oe && !oe->files.count("big.exe") && !oe->files.count("frac.exe") && !oe->files.count("nosize.exe") && !oe->files.count("notobj.exe"));
        CHECK(oe && oe->files.at("OkayEngine.exe").sha256 == std::string(64, 'a'));      // upper-case hex is lowered
        CHECK(oe && oe->files.at("Player.exe").sha256.empty() && oe->files.at("Player.exe").size == 20);
        CHECK(oe && !oe->files.count("../evil") && !oe->files.count("neg.exe") && !oe->files.count("huge.exe") && !oe->files.count("zero.exe") && !oe->files.count("str.exe"));
        CHECK(oe && oe->files.count("badsha.exe") == 1 && oe->files.at("badsha.exe").sha256.empty());   // a bad checksum is dropped from the entry, the size alone stays

        // Templates cannot leave their place.
        auto withTemplates = [&](const std::string& archive, const std::string& bucketPath) {
            return ParseIndex("{\"schema\": 1, \"archive\": \"" + archive + "\", \"bucketPath\": \"" + bucketPath + "\", \"versions\": []}");
        };
        CHECK(withTemplates("http://evil.example/{file}", "v{version}/{file}").archive.empty());
        CHECK(withTemplates("https://x.example/v{version}", "v{version}/{file}").archive.empty());                  // no {file}
        CHECK(withTemplates("https://x.example/a b/{file}", "v{version}/{file}").archive.empty());
        CHECK(withTemplates("https://x.example/{file}?x=\\\"", "v{version}/{file}").archive.empty());
        CHECK(withTemplates("https://x.example/{file}", "v{version}/{file}").archive == "https://x.example/{file}");
        for (const char* bp : {"../{file}", "v{version}/../../{file}", "/abs/{file}", "https://evil.example/{file}", "v{version}/nofile", "v{version}/{file} x"})
            CHECK(withTemplates("https://x.example/{file}", bp).bucketPath == "v{version}/{file}");                   // falls back to the default
        CHECK(withTemplates("https://x.example/{file}", "bucket/{version}/{file}").bucketPath == "bucket/{version}/{file}");

        // Many entries are capped, not trusted.
        std::string many = "{\"schema\": 1, \"versions\": [";
        for (int i = 0; i < 700; ++i) many += std::string(i ? "," : "") + "{\"version\": \"2." + std::to_string(i + 1) + ".0\"}";
        CHECK(ParseIndex(many + "]}").versions.size() == kMaxIndexVersions);

        CHECK(ExpandTemplate("v{version}/{file}", "2.850.1", "OkayEngine.exe") == "v2.850.1/OkayEngine.exe");
        CHECK(ExpandTemplate("{file}{file}{version}{x}", "1.0.0", "a") == "aa1.0.0{x}");
        CHECK(IsSafeFileName("OkayEngine.exe") && IsSafeFileName("SDL2.dll") && !IsSafeFileName("") && !IsSafeFileName("..") && !IsSafeFileName(".") &&
              !IsSafeFileName("a/b") && !IsSafeFileName("a\\b") && !IsSafeFileName("a b") && !IsSafeFileName(std::string(65, 'a')));
        CHECK(IsAcceptableUrl("https://x.example/a/b.exe") && IsAcceptableUrl("http://127.0.0.1:8080/x") && IsAcceptableUrl("http://localhost/x"));
        CHECK(!IsAcceptableUrl("http://x.example/a") && !IsAcceptableUrl("ftp://x.example/a") && !IsAcceptableUrl("file:///etc/passwd") &&
              !IsAcceptableUrl("https://x.example/a b") && !IsAcceptableUrl("https://x.example/\"") && !IsAcceptableUrl("https://x.example/\x01") &&
              !IsAcceptableUrl("https://x.example/\xC3\xA4") && !IsAcceptableUrl("https://x.example/$(id)") && !IsAcceptableUrl("https://x.example/a*b") && !IsAcceptableUrl("") && !IsAcceptableUrl("https://") && !IsAcceptableUrl("https://x.example/`x`"));
    }

    // ---- no versions.json: VERSION.txt / LTS.txt ------------------------------------------------------------------------------
    {
        CHECK(ParseVersionTxt("2.856.0\n") == "2.856.0" && ParseVersionTxt("  2.850.1 \r\n") == "2.850.1" && ParseVersionTxt("\n\n2.856\n") == "2.856.0" &&
              ParseVersionTxt("\xEF\xBB\xBF" "2.856.0") == "2.856.0");
        for (const char* bad : {"", "\n", "dev", "garbage", "2", "v2.856.0", "2.856.0-rc1", "2.856.0 extra", "<html>", "2..3", ".5", std::string(100, '1').c_str(), "1.2.x"})
            CHECK(ParseVersionTxt(bad).empty());
        const Index f = FallbackIndex("2.856.0\n", "2.850.1\n", kArchive, true);
        CHECK(f.ok && f.fallback && f.latest == "2.856.0" && f.lts == "2.850.1" && f.versions.size() == 2 && f.versions[0].version == "2.856.0");
        CHECK(f.versions[0].files.empty() && f.versions[0].inBucket && f.versions[0].channel == "latest" && f.versions[1].channel == "lts" && f.archive == kArchive);
        CHECK(FallbackIndex("2.856.0\n", "", "", true).versions.size() == 1 && FallbackIndex("2.856.0\n", "2.856.0", "", true).versions.size() == 1);
        CHECK(!FallbackIndex("", "", kArchive, true).ok && !FallbackIndex("garbage", "<html>", kArchive, true).ok && !FallbackIndex("garbage", "", kArchive, true).error.empty());
        CHECK(FallbackIndex("2.856.0", "", "http://evil.example/{file}", true).archive.empty());
        CHECK(!FallbackIndex("2.856.0", "", kArchive, false).versions[0].inBucket);                 // no bucket to use

        // PickHighlight: what `--versions <ver>` selects.
        const Index idx = ParseIndex(IndexJson({EntryJson("2.856.0", "latest", true), EntryJson("2.850.1", "lts", true), EntryJson("2.850.0", "latest", false)}, "2.856.0", "2.850.1"));
        CHECK(PickHighlight(idx, "2.850.0") == "2.850.0" && PickHighlight(idx, "v2.856.0") == "2.856.0");
        CHECK(PickHighlight(idx, "2.850.7") == "2.850.1");                                           // not listed: the LTS head of that line
        CHECK(PickHighlight(idx, "2.856.3") == "2.856.0");                                           // not listed: the newest of the line
        CHECK(PickHighlight(idx, "2.700.0").empty() && PickHighlight(idx, "").empty() && PickHighlight(idx, "junk").empty() && PickHighlight(Index{}, "2.850.0").empty());
    }

    // ---- finding out what exists (versions.json, then the small text files) ------------------------------------------------------------
    {
        const std::string json = IndexJson({EntryJson("2.856.0", "latest", true), EntryJson("2.850.1", "lts", true)}, "2.856.0", "2.850.1");
        std::map<std::string, std::string> web;
        std::vector<std::string> asked;
        FetchTextFn fetch = [&](const std::string& url, std::string& out) { asked.push_back(url); const auto it = web.find(url); if (it == web.end()) return false; out = it->second; return true; };
        Sources src;
        src.bucketBase = "https://example.test/storage/v1/object/public/releases";      // no trailing slash: added
        src.archiveTemplate = kArchive;
        src.versionTxtUrls = {"https://github.example/o/r/releases/latest/download/VERSION.txt"};
        const std::string B = "https://example.test/storage/v1/object/public/releases/";

        // A good index, in step with the text files: used as it is.
        web[B + "versions.json"] = json;
        web[B + "VERSION.txt"] = "2.856.0\n";
        web[B + "LTS.txt"] = "2.850.1\n";
        Index idx = LoadIndex(src, fetch);
        CHECK(idx.ok && !idx.fallback && idx.note.empty() && idx.latest == "2.856.0" && idx.lts == "2.850.1" && idx.versions.size() == 2 && idx.versions[0].files.size() == 3);
        CHECK(asked[0] == B + "versions.json");
        // The index lags (written last, and a failure there only warns): the newer head from VERSION.txt / LTS.txt is added, unchecked.
        web[B + "VERSION.txt"] = "2.857.0\n";
        web[B + "LTS.txt"] = "2.850.2\n";
        idx = LoadIndex(src, fetch);
        CHECK(idx.ok && idx.latest == "2.857.0" && idx.lts == "2.850.2" && idx.versions.size() == 4 && idx.versions[0].version == "2.857.0" && idx.versions[0].files.empty() &&
              idx.Find("2.850.2") && idx.Find("2.850.2")->channel == "lts" && idx.Find("2.856.0")->files.size() == 3);
        web[B + "VERSION.txt"] = "2.800.0\n";                                           // an older text file never lowers a head
        web.erase(B + "LTS.txt");
        idx = LoadIndex(src, fetch);
        CHECK(idx.ok && idx.latest == "2.856.0" && idx.lts == "2.850.1" && idx.versions.size() == 2);
        // The index has no archive pattern: the launcher's own is used.
        web[B + "versions.json"] = "{\"schema\": 1, \"latest\": \"2.856.0\", \"versions\": []}";
        idx = LoadIndex(src, fetch);
        CHECK(idx.ok && idx.archive == kArchive);
        // No usable index: garbage, a newer schema, a 404 page, nothing at all.
        for (const char* bad : {"garbage", "{\"schema\": 2, \"versions\": []}", "<html>Not Found</html>", ""}) {
            web[B + "versions.json"] = bad;
            if (!*bad) web.erase(B + "versions.json");
            web[B + "VERSION.txt"] = "2.856.0\n";
            web[B + "LTS.txt"] = "2.850.1\n";
            idx = LoadIndex(src, fetch);
            CHECK(idx.ok && idx.fallback && idx.latest == "2.856.0" && idx.lts == "2.850.1" && idx.versions.size() == 2 && idx.versions[0].files.empty() && idx.versions[0].inBucket);
            CHECK(Has(idx.note, "can't be checked automatically") && Has(idx.note, "Only the newest"));
            if (std::string(bad) == "{\"schema\": 2, \"versions\": []}") CHECK(Has(idx.note, "format 2"));
        }
        // GitHub has a newer VERSION.txt than the bucket (the updater takes the newest of the two): so does the fallback.
        web[B + "VERSION.txt"] = "2.856.0\n";
        web["https://github.example/o/r/releases/latest/download/VERSION.txt"] = "2.857.0\n";
        idx = LoadIndex(src, fetch);
        CHECK(idx.ok && idx.fallback && idx.latest == "2.857.0" && idx.Find("2.857.0")->inBucket);
        // No bucket at all (no account server): GitHub only; the entries are not in a bucket.
        Sources noBucket = src;
        noBucket.bucketBase.clear();
        asked.clear();
        idx = LoadIndex(noBucket, fetch);
        CHECK(idx.ok && idx.fallback && idx.latest == "2.857.0" && !idx.versions[0].inBucket && idx.lts.empty() && asked.size() == 1 && Has(idx.note, "not available"));
        noBucket.bucketBase = "http://evil.example/";                                       // a bucket that is not https is not used
        asked.clear();
        idx = LoadIndex(noBucket, fetch);
        CHECK(idx.fallback && !idx.versions[0].inBucket && asked.size() == 1);
        // Nothing reachable: not ok, with the words to show.
        web.clear();
        idx = LoadIndex(src, fetch);
        CHECK(!idx.ok && Has(idx.error, "Couldn't reach") && idx.versions.empty());
        CHECK(!LoadIndex(src, FetchTextFn{}).ok && !LoadIndex(Sources{}, fetch).ok);
        // A body that is far too big is not read.
        web[B + "versions.json"] = std::string(kMaxIndexBytes + 1, ' ');
        CHECK(!LoadIndex(src, fetch).ok);
        // Text files that are no versions are ignored.
        web.clear();
        web[B + "VERSION.txt"] = "<html>oops</html>";
        web["https://github.example/o/r/releases/latest/download/VERSION.txt"] = "2.856.0";
        idx = LoadIndex(src, fetch);
        CHECK(idx.ok && idx.latest == "2.856.0" && idx.versions.size() == 1);
    }

    // ---- where a download comes from ------------------------------------------------------------------------------------------
    {
        TempDir t;
        const fs::path launcher = MakeLauncher(t.path, "2.856.0");
        const Index idx = ParseIndex(IndexJson({EntryJson("2.856.0", "latest", true), EntryJson("2.850.1", "lts", true), EntryJson("2.800.0", "latest", false)}, "2.856.0", "2.850.1"));

        InstallPlan p = PlanFor(idx, "2.850.1", launcher);
        CHECK(p.ok && p.version == "2.850.1" && p.files.size() == 3 && !p.needsConfirmation);
        CHECK(p.files[0].asset == Asset::Editor && p.files[0].indexName == "OkayEngine.exe" && p.files[0].diskName == okayver::EditorExeName());
        CHECK(p.files[0].urls.size() == 2 && p.files[0].urls[0] == std::string(kBucket) + "v2.850.1/OkayEngine.exe" &&
              p.files[0].urls[1] == "https://github.example/o/r/releases/download/v2.850.1/OkayEngine.exe");   // the bucket first, GitHub after it
        CHECK(p.files[0].verified && p.files[0].sha256 == Sha(EditorOf("2.850.1")) && p.files[0].size == EditorOf("2.850.1").size());
        CHECK(p.files[2].asset == Asset::Sdl && p.files[2].localCopy == launcher / "Tools" / "SDL2.dll" && p.files[2].verified && p.files[2].urls.empty());   // not in the index: the launcher's own copy
        CHECK(p.totalBytes() == EditorOf("2.850.1").size() + Body("2.850.1", "OkaySpacePlayer.exe").size() + Body("sdl", "SDL2.dll").size());

        const InstallPlan old = PlanFor(idx, "2.800.0", launcher);                                    // left the bucket: GitHub only
        CHECK(old.ok && old.files[0].urls.size() == 1 && old.files[0].urls[0] == "https://github.example/o/r/releases/download/v2.800.0/OkayEngine.exe");
        const InstallPlan nobucket = PlanFor(idx, "2.850.1", launcher, false);                        // no bucket at all (no account server): GitHub
        CHECK(nobucket.ok && nobucket.files[0].urls.size() == 1 && nobucket.files[0].urls[0].rfind("https://github.example/", 0) == 0);
        const InstallPlan slash = PlanInstall(idx, "2.850.1", "https://example.test/releases", launcher / "Tools" / "SDL2.dll", true);   // base without its slash
        CHECK(slash.ok && slash.files[0].urls[0] == "https://example.test/releases/v2.850.1/OkayEngine.exe");

        Index noArchive = idx;
        noArchive.archive.clear();
        CHECK(!PlanInstall(noArchive, "2.800.0", kBucket, {}, false).ok && Has(PlanInstall(noArchive, "2.800.0", kBucket, {}, false).error, "nowhere"));   // pruned and no GitHub address
        CHECK(PlanInstall(noArchive, "2.850.1", kBucket, {}, false).ok);

        CHECK(!PlanFor(idx, "9.9.9", launcher).ok && Has(PlanFor(idx, "9.9.9", launcher).error, "not in the version list"));
        CHECK(!PlanFor(idx, "junk", launcher).ok && !PlanFor(idx, "", launcher).ok && !PlanFor(idx, "../2.850.1", launcher).ok && !PlanFor(Index{}, "2.850.1", launcher).ok);
        CHECK(!PlanInstall(idx, "2.850.1", kBucket, t.path / "nowhere" / "SDL2.dll", true).ok);        // SDL2.dll needed and nowhere to get it
        const InstallPlan nosdl = PlanInstall(idx, "2.850.1", kBucket, t.path / "nowhere" / "SDL2.dll", false);
        CHECK(nosdl.ok && nosdl.files.size() == 2);                                                     // elsewhere the system's SDL is used

        // A head that the index has no entry for (versions.json is written last and can lag): planned without checksums.
        Index lag = idx;
        lag.latest = "2.857.0";
        const InstallPlan lp = PlanFor(lag, "2.857.0", launcher);
        CHECK(lp.ok && lp.needsConfirmation && !lp.files[0].verified && lp.files[0].size == 0 && !lp.files[0].urls.empty());
        CHECK(!PlanFor(lag, "2.858.0", launcher).ok);

        // The fallback index (no versions.json) has no checksums either.
        const Index fb = FallbackIndex("2.856.0", "2.850.1", kArchive, true);
        const InstallPlan fp = PlanFor(fb, "2.850.1", launcher);
        CHECK(fp.ok && fp.needsConfirmation && !fp.files[0].verified && fp.files[0].sha256.empty());
        std::vector<std::string> asked;
        InstallPlan fp1 = fp;
        CHECK(!FillUnknownSizes(fp1, [&](const std::string& u) { asked.push_back(u); return std::uint64_t(0); }) && !asked.empty());   // the server did not say
        InstallPlan fp2 = fp;
        CHECK(FillUnknownSizes(fp2, [&](const std::string&) { return std::uint64_t(1234); }) && fp2.files[0].size == 1234 && fp2.files[1].size == 1234);
        CHECK(fp2.files[2].size == Body("sdl", "SDL2.dll").size());                                     // the local copy keeps its own size
        InstallPlan fp3 = fp;
        CHECK(!FillUnknownSizes(fp3, [&](const std::string&) { return kMaxFileBytes + 1; }) && fp3.files[0].size == 0);   // an impossible size is no size
    }

    // ---- installing: the good path, and the folder layout ---------------------------------------------------------------------
    {
        TempDir t;
        const fs::path launcher = MakeLauncher(t.path, "2.856.0");
        const Index idx = ParseIndex(IndexJson({EntryJson("2.856.0", "latest", true), EntryJson("2.850.1", "lts", true)}, "2.856.0", "2.850.1"));
        Server srv;
        srv.AddRelease("2.850.1", true, true);
        IO io = MakeIO(srv);
        std::vector<Progress> seen;
        io.progress = [&](const Progress& p) { seen.push_back(p); };
        const auto before = Snapshot(t.path);

        const Result r = Install(launcher, PlanFor(idx, "2.850.1", launcher), {}, io);
        CHECK(r.outcome == Outcome::Installed && r.ok() && r.path == okayver::VersionsDir(launcher) / "2.850.1" && Has(r.message, "2.850.1"));
        CHECK(Complete(launcher, "2.850.1") && NoScratch(launcher) && NoPartFiles(launcher));
        CHECK(Get(okayver::VersionsDir(launcher) / "2.850.1" / "Tools" / "SDL2.dll") == Body("sdl", "SDL2.dll"));
        CHECK(okayver::InstalledVersions(launcher) == std::vector<std::string>{"2.850.1"});
        CHECK(srv.requested.size() == 2 && srv.requested[0].rfind(kBucket, 0) == 0);                    // from the bucket: nothing was fetched from GitHub
        CHECK(!seen.empty() && seen.front().fileCount == 3 && seen.back().doneBytes >= seen.front().totalBytes - 4);
        std::uint64_t last = 0;
        bool monotone = true;
        for (const Progress& p : seen) { if (p.doneBytes < last) monotone = false; last = p.doneBytes; }
        CHECK(monotone);
        const std::vector<Installed> list = ListInstalled(launcher);
        CHECK(list.size() == 1 && list[0].version == "2.850.1" && list[0].source == "installed" && list[0].bytes > 2000 && !list[0].date.empty());
        CHECK(DiskUsage(launcher) >= list[0].bytes);

        // Only the Versions folder changed: the running install, the launcher and everything else are as they were.
        auto after = Snapshot(t.path);
        for (const auto& kv : before) CHECK(after.count(kv.first) && after[kv.first] == kv.second);
        const std::string versionsRel = fs::relative(okayver::VersionsDir(launcher), t.path).generic_string();
        for (const auto& kv : after) CHECK(before.count(kv.first) || kv.first.rfind(versionsRel, 0) == 0);

        // Again: nothing is downloaded twice.
        srv.requested.clear();
        CHECK(Install(launcher, PlanFor(idx, "2.850.1", launcher), {}, io).outcome == Outcome::AlreadyInstalled && srv.requested.empty());

        // An empty folder of that name (no editor in it) is not "installed": it is replaced whole.
        fs::remove_all(okayver::VersionsDir(launcher) / "2.850.1");
        Put(okayver::VersionsDir(launcher) / "2.850.1" / "junk.txt", "leftover");
        CHECK(Install(launcher, PlanFor(idx, "2.850.1", launcher), {}, io).ok() && Complete(launcher, "2.850.1") && !fs::exists(okayver::VersionsDir(launcher) / "2.850.1" / "junk.txt"));

        // A plan that was not made by PlanInstall is checked too.
        InstallPlan evil = PlanFor(idx, "2.850.0", launcher);
        evil.ok = true; evil.version = "2.850.0";
        PlanFile pf; pf.indexName = "../evil"; pf.diskName = "../evil"; pf.urls = {"https://x.example/a"}; pf.size = 3; pf.sha256 = Sha("abc"); pf.verified = true;
        evil.files = {pf};
        CHECK(Install(launcher, evil, {}, io).outcome == Outcome::Failed && !fs::exists(t.path / "evil"));
        InstallPlan badVer = PlanFor(idx, "2.850.1", launcher);
        badVer.version = "../../escape";
        CHECK(Install(launcher, badVer, {}, io).outcome == Outcome::Failed && !fs::exists(launcher / ".." / "escape"));
        badVer.version = "v2.850.1";                                                                    // not the canonical spelling
        CHECK(Install(launcher, badVer, {}, io).outcome == Outcome::Failed);
        CHECK(Install(launcher, InstallPlan{}, {}, io).outcome == Outcome::Failed);
        CHECK(Install(launcher, PlanFor(Index{}, "2.850.1", launcher), {}, io).outcome == Outcome::Failed);
    }

    // ---- installing: checksums, sizes, sources --------------------------------------------------------------------------------
    {
        TempDir t;
        const fs::path launcher = MakeLauncher(t.path, "2.856.0");
        const Index idx = ParseIndex(IndexJson({EntryJson("2.850.1", "lts", true)}, "2.856.0", "2.850.1"));
        const std::string bucketEditor = std::string(kBucket) + "v2.850.1/OkayEngine.exe", gitEditor = "https://github.example/o/r/releases/download/v2.850.1/OkayEngine.exe";

        // The bucket serves a damaged editor, GitHub has the right one: the bad file is never kept, GitHub's is used.
        Server srv;
        srv.AddRelease("2.850.1", true, true);
        srv.files[bucketEditor] = EditorOf("2.850.1").substr(0, 100) + "TAMPERED" + EditorOf("2.850.1").substr(108);   // same size, different bytes
        const Result r = Install(launcher, PlanFor(idx, "2.850.1", launcher), {}, MakeIO(srv));
        CHECK(r.ok() && Complete(launcher, "2.850.1") && std::find(srv.requested.begin(), srv.requested.end(), gitEditor) != srv.requested.end());

        // Both sources damaged: refused, with the words the Versions page shows, and nothing is left behind.
        TempDir t2;
        const fs::path l2 = MakeLauncher(t2.path, "2.856.0");
        Server bad;
        bad.AddRelease("2.850.1", true, true);
        bad.files[bucketEditor] = std::string(EditorOf("2.850.1").size(), 'Z');
        bad.files[gitEditor] = std::string(EditorOf("2.850.1").size(), 'Y');
        const Result rb = Install(l2, PlanFor(idx, "2.850.1", l2), {}, MakeIO(bad));
        CHECK(rb.outcome == Outcome::Failed && Has(rb.message, "did not match its checksum") && Has(rb.message, "OkayEngine.exe") && Has(rb.message, "was not installed"));
        CHECK(!okayver::IsInstalled(l2, "2.850.1") && !fs::exists(okayver::VersionsDir(l2) / "2.850.1") && NoScratch(l2) && NoPartFiles(l2));
        CHECK(ListInstalled(l2).empty());

        // Wrong size (truncated, and too long).
        Server sz;
        sz.AddRelease("2.850.1", true, true);
        sz.files[bucketEditor] = EditorOf("2.850.1").substr(0, 500);
        sz.files[gitEditor] = EditorOf("2.850.1") + "extra";
        const Result rs = Install(l2, PlanFor(idx, "2.850.1", l2), {}, MakeIO(sz));
        CHECK(rs.outcome == Outcome::Failed && Has(rs.message, "bytes") && !fs::exists(okayver::VersionsDir(l2) / "2.850.1") && NoScratch(l2));

        // The bucket serves a damaged editor and GitHub cannot be reached: the damaged file is what is reported (not the later "no connection").
        Server onlyBucket;
        onlyBucket.AddRelease("2.850.1", true, false);
        onlyBucket.files[bucketEditor] = std::string(EditorOf("2.850.1").size(), 'Z');
        const Result ro = Install(l2, PlanFor(idx, "2.850.1", l2), {}, MakeIO(onlyBucket));
        CHECK(ro.outcome == Outcome::Failed && Has(ro.message, "did not match its checksum") && !fs::exists(okayver::VersionsDir(l2) / "2.850.1") && NoScratch(l2));

        // A server that has nothing: the 404 is reported, nothing is made.
        Server none;
        const Result rn = Install(l2, PlanFor(idx, "2.850.1", l2), {}, MakeIO(none));
        CHECK(rn.outcome == Outcome::Failed && Has(rn.message, "404") && NoScratch(l2) && ListInstalled(l2).empty());

        // The second file is the damaged one: the first (good) file does not make a half-installed folder.
        Server second;
        second.AddRelease("2.850.1", true, true);
        second.files[std::string(kBucket) + "v2.850.1/OkaySpacePlayer.exe"] = "wrong";
        second.files["https://github.example/o/r/releases/download/v2.850.1/OkaySpacePlayer.exe"] = "wrong";
        CHECK(!Install(l2, PlanFor(idx, "2.850.1", l2), {}, MakeIO(second)).ok() && !fs::exists(okayver::VersionsDir(l2) / "2.850.1") && NoScratch(l2));
    }

    // ---- installing: cancel -----------------------------------------------------------------------------------------------------
    {
        TempDir t;
        const fs::path launcher = MakeLauncher(t.path, "2.856.0");
        const Index idx = ParseIndex(IndexJson({EntryJson("2.850.1", "lts", true), EntryJson("2.850.0", "latest", true)}, "2.856.0", "2.850.1"));
        Server srv;
        srv.AddRelease("2.850.1", true, true);
        srv.AddRelease("2.850.0", true, true);
        IO good = MakeIO(srv);
        CHECK(Install(launcher, PlanFor(idx, "2.850.0", launcher), {}, good).ok());               // another version already installed: it must survive
        const auto others = Snapshot(okayver::VersionsDir(launcher) / "2.850.0");

        // Cancelled before the first byte, between files, and in the middle of a download.
        for (int when = 0; when < 5; ++when) {
            Server s2 = srv.Clone();
            IO io = MakeIO(s2);
            int polls = 0;
            io.cancelled = [&]() { return when == 0 ? true : (when == 4 ? false : ++polls > when * 2); };
            if (when == 4) s2.cancelAfterChunks = 1;                                                   // the downloader stops by itself
            const Result r = Install(launcher, PlanFor(idx, "2.850.1", launcher), {}, io);
            CHECK(r.outcome == Outcome::Cancelled && Has(r.message, "Nothing was installed") && !r.ok());
            CHECK(!fs::exists(okayver::VersionsDir(launcher) / "2.850.1") && NoScratch(launcher) && NoPartFiles(launcher));
            CHECK(Snapshot(okayver::VersionsDir(launcher) / "2.850.0") == others && okayver::InstalledVersions(launcher) == std::vector<std::string>{"2.850.0"});
            if (when == 0) CHECK(s2.requested.empty());                                                // cancelled at once: nothing was asked for
        }
        // After a cancel the same install goes through.
        CHECK(Install(launcher, PlanFor(idx, "2.850.1", launcher), {}, good).ok() && Complete(launcher, "2.850.1"));
    }

    // ---- installing: a full disk, and a failure at every step ---------------------------------------------------------------------
    {
        TempDir t;
        const fs::path launcher = MakeLauncher(t.path, "2.856.0");
        const Index idx = ParseIndex(IndexJson({EntryJson("2.850.1", "lts", false), EntryJson("2.850.0", "latest", false)}, "2.856.0", "2.850.1"));   // GitHub only: one address per file
        Server srv;
        srv.AddRelease("2.850.1", false, true);
        srv.AddRelease("2.850.0", false, true);
        CHECK(Install(launcher, PlanFor(idx, "2.850.0", launcher), {}, MakeIO(srv)).ok());
        const auto others = Snapshot(okayver::VersionsDir(launcher) / "2.850.0");
        auto Untouched = [&]() {
            const bool a = Snapshot(okayver::VersionsDir(launcher) / "2.850.0") == others, b = NoScratch(launcher), c = NoPartFiles(launcher),
                       d = !fs::exists(okayver::VersionsDir(launcher) / "2.850.1"), e = okayver::InstalledVersions(launcher) == std::vector<std::string>{"2.850.0"};
            if (!(a && b && c && d && e)) std::cerr << "    Untouched: snapshot " << a << " scratch " << b << " parts " << c << " no-new-folder " << d << " list " << e << "\n";
            return a && b && c && d && e; };

        // Not enough space is noticed before anything is downloaded.
        {
            Server s2 = srv.Clone();
            IO io = MakeIO(s2);
            io.availableSpace = [](const fs::path&, std::error_code& ec) { ec.clear(); return (std::uintmax_t)1000; };
            const Result r = Install(launcher, PlanFor(idx, "2.850.1", launcher), {}, io);
            CHECK(r.outcome == Outcome::NotEnoughSpace && Has(r.message, "free") && s2.requested.empty() && Untouched());
            io.availableSpace = [](const fs::path&, std::error_code& ec) { ec = std::make_error_code(std::errc::io_error); return (std::uintmax_t)0; };   // can't even tell
            CHECK(Install(launcher, PlanFor(idx, "2.850.1", launcher), {}, io).outcome == Outcome::NotEnoughSpace && Untouched());
        }
        // The disk fills while a file is being written (the downloader says so), at the first, second and third file.
        for (int at = 0; at < 2; ++at) {
            Server s2 = srv.Clone();
            s2.failNth = at;
            s2.diskFullOnNth = true;
            const Result r = Install(launcher, PlanFor(idx, "2.850.1", launcher), {}, MakeIO(s2));
            CHECK(r.outcome == Outcome::NotEnoughSpace && Has(r.message, "disk filled up") && Untouched());
            Server s3 = srv.Clone();
            s3.failNth = at;                                                                           // a plain failure too
            CHECK(Install(launcher, PlanFor(idx, "2.850.1", launcher), {}, MakeIO(s3)).outcome == Outcome::Failed);
            CHECK(Untouched());
        }
        // The Versions folder cannot be made (a file is in the way): a plain failure, not a crash.
        {
            TempDir t3;
            const fs::path l3 = MakeLauncher(t3.path, "2.856.0");
            Put(okayver::VersionsDir(l3), "not a folder");
            Server s2 = srv.Clone();
            const Result r = Install(l3, PlanFor(idx, "2.850.1", l3), {}, MakeIO(s2));
            CHECK(r.outcome == Outcome::Failed && s2.requested.empty() && Get(okayver::VersionsDir(l3)) == "not a folder");
        }

        // A failure at every single step of an install, in turn: it never leaves anything and never damages what was there.
        std::vector<std::string> steps;
        {
            TempDir t4;
            const fs::path l4 = MakeLauncher(t4.path, "2.856.0");
            Server s2 = srv.Clone();
            IO io = MakeIO(s2);
            io.fail = [&](const std::string& s) { steps.push_back(s); return false; };
            CHECK(Install(l4, PlanFor(idx, "2.850.1", l4), {}, io).ok());
        }
        CHECK(steps.size() == 13);                                                                     // create, space, scratch, 2 x (download/verify/move), copy + move of SDL2.dll, marker, commit
        for (const char* must : {"create-versions", "space", "create-scratch", "download:OkayEngine.exe", "verify:OkayEngine.exe", "move:OkayEngine.exe",
                                 "download:OkaySpacePlayer.exe", "copy:SDL2.dll", "move:SDL2.dll", "marker", "commit"})
            CHECK(std::find(steps.begin(), steps.end(), must) != steps.end());
        for (std::size_t failAt = 0; failAt < steps.size(); ++failAt) {
            Server s2 = srv.Clone();
            IO io = MakeIO(s2);
            std::size_t n = 0;
            io.fail = [&](const std::string&) { return n++ == failAt; };
            const Result r = Install(launcher, PlanFor(idx, "2.850.1", launcher), {}, io);
            CHECK(!r.ok() && !r.message.empty());
            CHECK(Untouched());
            if (!Untouched()) std::cerr << "    (failed at step " << failAt << ": " << steps[failAt] << ")\n";
        }
        CHECK(Install(launcher, PlanFor(idx, "2.850.1", launcher), {}, MakeIO(srv)).ok() && Complete(launcher, "2.850.1"));   // and afterwards it works
    }

    // ---- installing without a checksum -------------------------------------------------------------------------------------------
    {
        TempDir t;
        const fs::path launcher = MakeLauncher(t.path, "2.856.0");
        const Index fb = FallbackIndex("2.856.0", "2.850.1", kArchive, true);                       // no versions.json
        Server srv;
        srv.AddRelease("2.850.1", true, true);
        InstallPlan plan = PlanFor(fb, "2.850.1", launcher);
        CHECK(plan.needsConfirmation);

        // Not confirmed: nothing is downloaded or run.
        Result r = Install(launcher, plan, {}, MakeIO(srv));
        CHECK(r.outcome == Outcome::NeedsConfirmation && srv.requested.empty() && !fs::exists(okayver::VersionsDir(launcher) / "2.850.1"));
        // Confirmed, but the server never told the size: refused (an unchecked file of unknown size is never run).
        InstallOptions yes;
        yes.confirmedUnverified = true;
        r = Install(launcher, plan, yes, MakeIO(srv));
        CHECK(r.outcome == Outcome::Refused && Has(r.message, "can't be checked") && srv.requested.empty() && NoScratch(launcher));
        // Confirmed, size known and matching: installed.
        InstallPlan sized = plan;
        CHECK(FillUnknownSizes(sized, [&](const std::string& url) { const auto it = srv.files.find(url); return it == srv.files.end() ? std::uint64_t(0) : (std::uint64_t)it->second.size(); }));
        r = Install(launcher, sized, yes, MakeIO(srv));
        CHECK(r.outcome == Outcome::Installed && Complete(launcher, "2.850.1"));
        // Confirmed, size known but the file is another size: not kept.
        TempDir t2;
        const fs::path l2 = MakeLauncher(t2.path, "2.856.0");
        InstallPlan wrong = sized;
        for (PlanFile& f : wrong.files) if (f.localCopy.empty()) f.size += 7;
        r = Install(l2, wrong, yes, MakeIO(srv));
        CHECK(r.outcome == Outcome::Failed && !fs::exists(okayver::VersionsDir(l2) / "2.850.1") && NoScratch(l2));
    }

    // ---- removing ----------------------------------------------------------------------------------------------------------------
    {
        TempDir t;
        const fs::path launcher = MakeLauncher(t.path, "2.856.0");
        const Index idx = ParseIndex(IndexJson({EntryJson("2.850.1", "lts", true), EntryJson("2.800.0", "latest", true), EntryJson("2.700.0", "latest", true)}, "2.856.0", "2.850.1"));
        Server srv;
        for (const char* v : {"2.850.1", "2.800.0", "2.700.0"}) srv.AddRelease(v, true, true);
        IO io = MakeIO(srv);
        for (const char* v : {"2.850.1", "2.800.0", "2.700.0"}) CHECK(Install(launcher, PlanFor(idx, v, launcher), {}, io).ok());

        RemoveGuards g;
        g.running = "2.856.0";
        g.launcher = "2.856.0";
        CHECK(Remove(launcher, "2.856.0", g).outcome == Outcome::Refused);                              // not in Versions/ at all
        CHECK(Remove(launcher, "junk", g).outcome == Outcome::Refused && Remove(launcher, "", g).outcome == Outcome::Refused && Remove(launcher, "../x", g).outcome == Outcome::Refused);
        CHECK(Complete(launcher, "2.850.1") && Complete(launcher, "2.800.0"));
        // The version being run, and the version the launcher is, are never removed (a copy of them can be in Versions/ too).
        Put(okayver::VersionsDir(launcher) / "2.856.0" / "Tools" / okayver::EditorExeName(), "copy");
        Result r = Remove(launcher, "2.856.0", g);
        CHECK(r.outcome == Outcome::Refused && Has(r.message, "running") && fs::exists(okayver::VersionsDir(launcher) / "2.856.0"));
        g.running = "2.800.0";
        r = Remove(launcher, "2.856.0", g);
        CHECK(r.outcome == Outcome::Refused && Has(r.message, "this launcher") && fs::exists(okayver::VersionsDir(launcher) / "2.856.0"));
        fs::remove_all(okayver::VersionsDir(launcher) / "2.856.0");
        r = Remove(launcher, "2.800.0", g);
        CHECK(r.outcome == Outcome::Refused && Has(r.message, "running") && Complete(launcher, "2.800.0"));
        g.running = "2.856.0";
        // Protected for a reason (the default editor), and in use (an editor of that version is open).
        g.keep = {{"2.850.1", "it is your default editor"}};
        r = Remove(launcher, "v2.850.1", g);
        CHECK(r.outcome == Outcome::Refused && Has(r.message, "default editor") && Complete(launcher, "2.850.1"));
        g.keep.clear();
        g.inUse = [](const std::string& v) { return v == "2.850.1"; };
        r = Remove(launcher, "2.850.1", g);
        CHECK(r.outcome == Outcome::Refused && Has(r.message, "open") && Complete(launcher, "2.850.1"));
        g.inUse = {};
        // The folder cannot be renamed (a file in use): it stays whole and listed.
        IO fi;
        fi.fail = [](const std::string& s) { return s == "remove-rename"; };
        r = Remove(launcher, "2.700.0", g, fi);
        CHECK(r.outcome == Outcome::Failed && Complete(launcher, "2.700.0") && NoScratch(launcher));
        // Removed: gone at once, and nothing else changes.
        const auto keepSnap = Snapshot(okayver::VersionsDir(launcher) / "2.850.1");
        r = Remove(launcher, "2.700.0", g);
        CHECK(r.outcome == Outcome::Removed && r.ok() && !fs::exists(okayver::VersionsDir(launcher) / "2.700.0") && NoScratch(launcher));
        CHECK(Snapshot(okayver::VersionsDir(launcher) / "2.850.1") == keepSnap && okayver::InstalledVersions(launcher) == (std::vector<std::string>{"2.850.1", "2.800.0"}));
        CHECK(Remove(launcher, "2.700.0", g).outcome == Outcome::Refused);                              // twice
        // The delete after the rename fails: the version is already gone from the list, the scratch folder is kept for the next start.
        fi.fail = [](const std::string& s) { return s == "remove-delete"; };
        r = Remove(launcher, "2.800.0", g, fi);
        CHECK(r.outcome == Outcome::Removed && Has(r.message, "next time") && !okayver::IsInstalled(launcher, "2.800.0") && !NoScratch(launcher));
        CHECK(okayver::InstalledVersions(launcher) == std::vector<std::string>{"2.850.1"});
        CHECK(CleanStale(launcher) == 1 && NoScratch(launcher) && Complete(launcher, "2.850.1"));
        // Stale scratch folders of a crash are cleaned, and nothing else is.
        fs::create_directories(okayver::VersionsDir(launcher) / ".partial-2.9.9-1" / "Tools");
        Put(okayver::VersionsDir(launcher) / ".partial-2.9.9-1" / "Tools" / "x.part", "x");
        fs::create_directories(okayver::VersionsDir(launcher) / "notes");
        CHECK(CleanStale(launcher) == 1 && NoScratch(launcher) && fs::exists(okayver::VersionsDir(launcher) / "notes") && Complete(launcher, "2.850.1"));
        CHECK(CleanStale(t.path / "no such launcher") == 0);
        // Paths with spaces and unicode all the way (the launcher folder has both).
        CHECK(launcher.string().find(' ') != std::string::npos && launcher.string().find("\xC3\xA4") != std::string::npos);
    }

    // ---- keeping the current editor when the launcher updates ---------------------------------------------------------------------
    {
        TempDir t;
        const fs::path launcher = MakeLauncher(t.path, "2.855.0");
        IO io;
        io.availableSpace = [](const fs::path&, std::error_code& ec) { ec.clear(); return (std::uintmax_t)64 * 1024 * kMiB; };
        const CurrentInstall cur = FindCurrentInstall(launcher);
        CHECK(cur.editor == launcher / "Tools" / okayver::EditorExeName() && cur.player == launcher / "Tools" / "OkaySpacePlayer.exe" && cur.sdl == launcher / "Tools" / "SDL2.dll");
        const auto before = Snapshot(launcher);

        Result r = KeepCurrent(launcher, "2.855.0", cur, 1, true, io);
        CHECK(r.outcome == Outcome::Installed && Has(r.message, "2.855.0") && Complete(launcher, "2.855.0") && NoScratch(launcher));
        CHECK(Get(okayver::VersionsDir(launcher) / "2.855.0" / "Tools" / "SDL2.dll") == Body("sdl", "SDL2.dll"));
        const std::vector<Installed> l = ListInstalled(launcher);
        CHECK(l.size() == 1 && l[0].source == "kept");
        // The running install is exactly as it was: the update can now replace it.
        for (const auto& kv : before) CHECK(Snapshot(launcher).count(kv.first) && Snapshot(launcher)[kv.first] == kv.second);
        CHECK(KeepCurrent(launcher, "2.855.0", cur, 1, true, io).outcome == Outcome::AlreadyInstalled);   // the second update: already there

        // Reasons to skip: the update goes on, and nothing is made.
        TempDir t2;
        const fs::path l2 = MakeLauncher(t2.path, "2.855.0");
        const CurrentInstall c2 = FindCurrentInstall(l2);
        CHECK(KeepCurrent(l2, "2.855.0", c2, 0, true, io).outcome == Outcome::Skipped);                  // the setting is 0
        CHECK(KeepCurrent(l2, "dev", c2, 1, true, io).outcome == Outcome::Skipped && Has(KeepCurrent(l2, "dev", c2, 1, true, io).message, "development"));
        CHECK(KeepCurrent(l2, "", c2, 1, true, io).outcome == Outcome::Skipped && KeepCurrent(l2, "garbage", c2, 1, true, io).outcome == Outcome::Skipped);
        CHECK(KeepCurrent(l2, "../x", c2, 1, true, io).outcome == Outcome::Skipped);
        CHECK(KeepCurrent(l2, "2.855.0", CurrentInstall{}, 1, true, io).outcome == Outcome::Skipped);   // no editor found
        CurrentInstall nosdl = c2;
        nosdl.sdl.clear();
        CHECK(KeepCurrent(l2, "2.855.0", nosdl, 1, true, io).outcome == Outcome::Skipped && Has(KeepCurrent(l2, "2.855.0", nosdl, 1, true, io).message, "SDL2.dll"));
        CHECK(!fs::exists(okayver::VersionsDir(l2)) || ListInstalled(l2).empty());
        // A nearly full disk: skipped, and it says so.
        IO full = io;
        full.availableSpace = [](const fs::path&, std::error_code& ec) { ec.clear(); return (std::uintmax_t)(100 * kMiB); };   // less than the 256 MB that stays free
        r = KeepCurrent(l2, "2.855.0", c2, 1, true, full);
        CHECK(r.outcome == Outcome::Skipped && Has(r.message, "free disk space") && Has(r.message, "update goes on") && NoScratch(l2) && ListInstalled(l2).empty());
        full.availableSpace = [](const fs::path&, std::error_code& ec) { ec = std::make_error_code(std::errc::io_error); return (std::uintmax_t)0; };
        CHECK(KeepCurrent(l2, "2.855.0", c2, 1, true, full).outcome == Outcome::Skipped && ListInstalled(l2).empty());
        // No SDL2.dll needed outside Windows: kept without it.
        CHECK(KeepCurrent(l2, "2.855.0", nosdl, 1, false, io).outcome == Outcome::Installed && okayver::IsInstalled(l2, "2.855.0") && !fs::exists(okayver::VersionsDir(l2) / "2.855.0" / "Tools" / "SDL2.dll"));
        // An editor that sits beside the launcher (no Tools folder) is found.
        TempDir t3;
        const fs::path l3 = t3.path / "flat";
        Put(l3 / okayver::EditorExeName(), EditorOf("2.855.0"));
        CHECK(FindCurrentInstall(l3).editor == l3 / okayver::EditorExeName() && FindCurrentInstall(l3).player.empty());
        CHECK(FindCurrentInstall(t3.path / "none").editor.empty());

        // A failure at every step (a full disk in the middle of the copy): nothing is left, the running install is untouched.
        std::vector<std::string> steps;
        {
            TempDir t4;
            const fs::path l4 = MakeLauncher(t4.path, "2.855.0");
            IO rec = io;
            rec.fail = [&](const std::string& s) { steps.push_back(s); return false; };
            CHECK(KeepCurrent(l4, "2.855.0", FindCurrentInstall(l4), 1, true, rec).outcome == Outcome::Installed);
        }
        CHECK(steps.size() == 7);                                                                       // space, create, 3 copies, marker, commit
        for (std::size_t failAt = 0; failAt < steps.size(); ++failAt) {
            TempDir t5;
            const fs::path l5 = MakeLauncher(t5.path, "2.855.0");
            const auto snap = Snapshot(l5);
            IO ff = io;
            std::size_t n = 0;
            ff.fail = [&](const std::string&) { return n++ == failAt; };
            const Result rr = KeepCurrent(l5, "2.855.0", FindCurrentInstall(l5), 1, true, ff);
            CHECK(!rr.ok() && (rr.outcome == Outcome::Failed || rr.outcome == Outcome::Skipped));
            CHECK(!fs::exists(okayver::VersionsDir(l5) / "2.855.0") && NoScratch(l5) && ListInstalled(l5).empty());
            for (const auto& kv : snap) CHECK(Snapshot(l5).count(kv.first) && Snapshot(l5)[kv.first] == kv.second);
        }

        // The update that the copy was made for fails: the copy is dropped (it only duplicates what is running), but only if it is ours.
        DiscardKept(launcher, "2.855.0");
        CHECK(!fs::exists(okayver::VersionsDir(launcher) / "2.855.0"));
        TempDir t6;
        const fs::path l6 = MakeLauncher(t6.path, "2.855.0");
        Put(okayver::VersionsDir(l6) / "2.700.0" / "Tools" / okayver::EditorExeName(), "mine");   // put there by hand: no marker
        DiscardKept(l6, "2.700.0");
        CHECK(okayver::IsInstalled(l6, "2.700.0"));
        DiscardKept(l6, "junk"); DiscardKept(l6, "");
    }

    // ---- pruning: Keep previous versions: N ---------------------------------------------------------------------------------------
    {
        auto Inst = [](const char* v, const char* source) { Installed i; i.version = v; i.source = source; return i; };
        const std::vector<Installed> all = {Inst("2.855.0", "kept"), Inst("2.854.0", "kept"), Inst("2.853.0", "kept"), Inst("2.852.0", "kept"),
                                            Inst("2.800.0", "installed"), Inst("2.790.0", ""), Inst("2.850.1", "kept"), Inst("2.850.0", "kept")};
        CHECK(PlanPrune(all, 1, "", {}) == (std::vector<std::string>{"2.854.0", "2.853.0", "2.852.0", "2.850.1", "2.850.0"}));   // the newest kept copy stays; what the person installed never goes
        CHECK(PlanPrune(all, 2, "", {}) == (std::vector<std::string>{"2.853.0", "2.852.0", "2.850.1", "2.850.0"}));
        CHECK(PlanPrune(all, 9, "", {}).empty());
        CHECK(PlanPrune(all, 0, "", {}).size() == 6);                                                    // 0 keeps none of the automatic copies, still never the person's
        CHECK(PlanPrune(all, -3, "", {}).size() == 6);
        // The newest installed patch of the LTS line is never pruned, even over the limit; older patches of it are.
        CHECK(PlanPrune(all, 1, "2.850.1", {}) == (std::vector<std::string>{"2.854.0", "2.853.0", "2.852.0", "2.850.0"}));
        CHECK(PlanPrune(all, 1, "2.850", {}) == PlanPrune(all, 1, "2.850.9", {}));                       // the LTS head may be a line or a version
        const std::vector<std::string> none = PlanPrune(all, 0, "2.850.1", {});
        CHECK(none.size() == 5 && std::find(none.begin(), none.end(), "2.850.1") == none.end());
        // Nothing of the LTS line installed: nothing to protect. An explicit keep list is honoured.
        CHECK(PlanPrune(all, 1, "2.700.0", {}).size() == 5);
        CHECK(PlanPrune(all, 1, "", {"2.853.0", "v2.852.0", "junk"}) == (std::vector<std::string>{"2.854.0", "2.850.1", "2.850.0"}));
        CHECK(PlanPrune({}, 1, "2.850.1", {}).empty());
        std::vector<Installed> shuffled = all;
        std::reverse(shuffled.begin(), shuffled.end());
        CHECK(PlanPrune(shuffled, 1, "", {}) == PlanPrune(all, 1, "", {}));                              // the order of the list does not matter

        // On disk: the real folders go, the guards still apply.
        TempDir t;
        const fs::path launcher = MakeLauncher(t.path, "2.856.0");
        IO io;
        io.availableSpace = [](const fs::path&, std::error_code& ec) { ec.clear(); return (std::uintmax_t)64 * 1024 * kMiB; };
        for (const char* v : {"2.855.0", "2.854.0", "2.853.0", "2.850.1"}) {
            Put(okayver::VersionsDir(launcher) / v / "Tools" / okayver::EditorExeName(), EditorOf(v));
            Put(okayver::VersionsDir(launcher) / v / "install.txt", std::string("okayspace install 1\nversion=") + v + "\nsource=kept\n");
        }
        Put(okayver::VersionsDir(launcher) / "2.700.0" / "Tools" / okayver::EditorExeName(), EditorOf("2.700.0"));   // no install.txt: by hand
        RemoveGuards g;
        g.running = "2.856.0";
        g.keep = {{"2.853.0", "it is your default editor"}};
        const PruneResult pr = Prune(launcher, 1, "2.850.1", g, io);
        CHECK(pr.removed == std::vector<std::string>{"2.854.0"} && pr.failed.empty());
        CHECK(okayver::InstalledVersions(launcher) == (std::vector<std::string>{"2.855.0", "2.853.0", "2.850.1", "2.700.0"}) && NoScratch(launcher));
        g.inUse = [](const std::string& v) { return v == "2.855.0"; };
        const PruneResult p2 = Prune(launcher, 0, "2.850.1", g, io);                                       // 2.855.0 is open: cannot go; the rest are protected or the person's
        CHECK(p2.removed.empty() && p2.failed == std::vector<std::string>{"2.855.0"} && okayver::InstalledVersions(launcher).size() == 4);
        CHECK(Prune(t.path / "none", 1, "", {}, io).removed.empty());
    }

    // ---- the channel ---------------------------------------------------------------------------------------------------------------
    {
        using K = ChannelDecision;
        CHECK(ParseChannel("lts") == Channel::Lts && ParseChannel(" LTS\n") == Channel::Lts && ParseChannel("latest") == Channel::Latest && ParseChannel("") == Channel::Latest && ParseChannel("banana") == Channel::Latest);
        CHECK(std::string(ChannelName(Channel::Lts)) == "lts" && std::string(ChannelName(Channel::Latest)) == "latest");

        // Latest: today's behaviour (update when the head is newer, a development build always is).
        ChannelDecision d = DecideChannel(Channel::Latest, "2.855.0", "2.856.0", "2.850.1", {});
        CHECK(d.kind == K::UpdateInPlace && d.version == "2.856.0");
        CHECK(DecideChannel(Channel::Latest, "2.856.0", "2.856.0", "2.850.1", {}).kind == K::None);
        CHECK(DecideChannel(Channel::Latest, "2.860.0", "2.856.0", "2.850.1", {}).kind == K::None);        // never a downgrade
        CHECK(DecideChannel(Channel::Latest, "dev", "2.856.0", "", {}).kind == K::UpdateInPlace);
        CHECK(DecideChannel(Channel::Latest, "2.855.0", "", "2.850.1", {}).kind == K::NoInfo);               // offline: nothing changes
        CHECK(DecideChannel(Channel::Latest, "2.855.0", "2.856.0", "", {}).kind == K::UpdateInPlace);        // LTS plays no part

        // LTS, on the line: patches are normal updates.
        d = DecideChannel(Channel::Lts, "2.850.0", "2.856.0", "2.850.2", {});
        CHECK(d.kind == K::UpdateInPlace && d.version == "2.850.2" && Has(d.note, "v2.850.x"));
        CHECK(DecideChannel(Channel::Lts, "2.850.2", "2.856.0", "2.850.2", {}).kind == K::None);
        CHECK(DecideChannel(Channel::Lts, "2.850.3", "2.856.0", "2.850.2", {}).kind == K::None);
        // LTS, no LTS information: never falls back to the latest version (that would move the person off the line they chose).
        d = DecideChannel(Channel::Lts, "2.850.0", "2.856.0", "", {});
        CHECK(d.kind == K::NoInfo && d.version.empty() && Has(d.note, "nothing was changed"));
        // LTS, the launcher is newer than the line: never changed in place; the LTS editor goes next to it.
        d = DecideChannel(Channel::Lts, "2.855.0", "2.856.0", "2.850.1", {});
        CHECK(d.kind == K::InstallSideBySide && d.version == "2.850.1");
        d = DecideChannel(Channel::Lts, "2.855.0", "2.856.0", "2.850.1", {"2.855.0", "2.850.0"});
        CHECK(d.kind == K::UpdateSideBySide && d.version == "2.850.1");                                     // an older patch of the line is installed
        d = DecideChannel(Channel::Lts, "2.855.0", "2.856.0", "2.850.1", {"2.850.1", "2.700.0"});
        CHECK(d.kind == K::None && d.version == "2.850.1" && Has(d.note, "installed"));
        CHECK(DecideChannel(Channel::Lts, "2.855.0", "2.856.0", "2.850.1", {"2.850.2"}).kind == K::None);   // a newer patch than the head: leave it
        CHECK(DecideChannel(Channel::Lts, "dev", "2.856.0", "2.850.1", {}).kind == K::InstallSideBySide);   // a development build is not replaced in place
        // LTS, an older line than the LTS one: moving up to it is an ordinary update.
        d = DecideChannel(Channel::Lts, "2.800.0", "2.856.0", "2.850.1", {});
        CHECK(d.kind == K::UpdateInPlace && d.version == "2.850.1");
        // The rule that matters most, over every combination: an in-place update never goes to a version older than what runs.
        const std::vector<std::string> vs = {"dev", "", "2.700.0", "2.800.0", "2.849.9", "2.850.0", "2.850.1", "2.850.2", "2.851.0", "2.855.0", "2.856.0", "3.0.0"};
        for (Channel c : {Channel::Latest, Channel::Lts})
            for (const std::string& local : vs)
                for (const std::string& lat : vs)
                    for (const std::string& lts : vs)
                        for (const std::vector<std::string>& inst : {std::vector<std::string>{}, std::vector<std::string>{"2.850.0"}, std::vector<std::string>{"2.850.1", "2.855.0"}}) {
                            const ChannelDecision x = DecideChannel(c, local, lat, lts, inst);
                            if (x.kind == K::UpdateInPlace) CHECK(okayver::CompareVersions(x.version, local) > 0 && !x.version.empty());
                            if (c == Channel::Lts && (x.kind == K::UpdateInPlace || x.kind == K::InstallSideBySide || x.kind == K::UpdateSideBySide)) CHECK(x.version == okayver::NormalizeVersion(lts));
                            if (c == Channel::Latest) CHECK(x.kind != K::InstallSideBySide && x.kind != K::UpdateSideBySide);
                            if (x.kind == K::InstallSideBySide || x.kind == K::UpdateSideBySide) CHECK(okayver::CompareVersions(local, x.version) > 0 || okayver::NormalizeVersion(local).empty());
                        }

        // launcher.cfg lines.
        Settings s;
        CHECK(s.channel == Channel::Latest && s.keepPrevious == 1 && s.defaultEditor.empty());              // the defaults
        CHECK(ApplySettingLine(s, "update_channel", "lts") && s.channel == Channel::Lts);
        CHECK(ApplySettingLine(s, "keep_previous", "3") && s.keepPrevious == 3);
        CHECK(ApplySettingLine(s, "keep_previous", "99") && s.keepPrevious == kMaxKeepPrevious && ApplySettingLine(s, "keep_previous", "-4") && s.keepPrevious == 0);
        CHECK(ApplySettingLine(s, "keep_previous", "banana") && s.keepPrevious == kDefaultKeepPrevious && ApplySettingLine(s, "keep_previous", "") && s.keepPrevious == kDefaultKeepPrevious);
        CHECK(ApplySettingLine(s, "keep_previous", "99999999999999999999999") && s.keepPrevious == kDefaultKeepPrevious);
        CHECK(ApplySettingLine(s, "default_editor", "v2.850.1") && s.defaultEditor == "2.850.1" && ApplySettingLine(s, "default_editor", "junk") && s.defaultEditor.empty());
        CHECK(ApplySettingLine(s, "lts_line", "2.850.1") && s.ltsLine == "2.850" && ApplySettingLine(s, "lts_line", "x") && s.ltsLine.empty());
        CHECK(!ApplySettingLine(s, "accent", "3") && !ApplySettingLine(s, "update_on_launch", "1") && !ApplySettingLine(s, "", ""));   // other keys are not ours
        s.channel = Channel::Lts; s.keepPrevious = 4; s.defaultEditor = "2.850.1"; s.ltsLine = "2.850";
        std::ostringstream out;
        WriteSettings(s, out);
        Settings back;
        std::istringstream in(out.str());
        std::string line;
        while (std::getline(in, line)) { const auto eq = line.find('='); CHECK(eq != std::string::npos && ApplySettingLine(back, line.substr(0, eq), line.substr(eq + 1))); }
        CHECK(back.channel == Channel::Lts && back.keepPrevious == 4 && back.defaultEditor == "2.850.1" && back.ltsLine == "2.850");
        std::ostringstream dflt;
        WriteSettings(Settings{}, dflt);
        CHECK(dflt.str() == "update_channel=latest\nkeep_previous=1\n");
        CHECK(&GlobalSettings() == &GlobalSettings());
    }

    // ---- projects: the badge, "Open with", and what a plain Open starts ----------------------------------------------------------------
    {
        ProjectBadge b = BadgeFor("2.850.1", "2.855.0");
        CHECK(b.version == "v2.850.1" && b.text == "older: v2.850.x" && b.tone == ProjectBadge::Older && Has(b.tip, "2.850.1"));
        b = BadgeFor("2.860.0", "2.855.0");
        CHECK(b.version == "v2.860.0" && b.text == "newer" && b.tone == ProjectBadge::Newer && Has(b.tip, "newer"));
        for (const char* same : {"2.855.0", "2.855", "v2.855.0"}) { b = BadgeFor(same, "2.855.0"); CHECK(b.text.empty() && b.tone == ProjectBadge::None && b.tip.empty() && b.version == "v2.855.0"); }
        b = BadgeFor("2.855.0", "2.855.4");
        CHECK(b.text.empty() && b.version == "v2.855.0");                                                  // another patch of this line: no badge
        b = BadgeFor("", "2.855.0");
        CHECK(b.version == "unknown" && b.text.empty() && b.tone == ProjectBadge::None);
        b = BadgeFor("garbage", "2.855.0");
        CHECK(b.version == "unknown" && b.text.empty());
        b = BadgeFor("2.850.1", "dev");                                                                    // a development launcher never badges
        CHECK(b.version == "v2.850.1" && b.text.empty());
        b = BadgeFor("2.850.1", "");
        CHECK(b.text.empty());

        const std::vector<std::string> inst = {"2.800.0", "2.850.1", "2.850.0", "2.700.0", "2.855.0", "2.850.1", "junk"};
        const std::vector<EditorChoice> oc = OpenWithChoices("2.850.0", inst, "2.855.0");
        CHECK(oc.size() == 4 && oc[0].version == "2.850.1" && oc[0].sameLine && oc[1].version == "2.850.0" && oc[1].sameLine &&
              oc[2].version == "2.800.0" && !oc[2].sameLine && oc[3].version == "2.700.0");                // the project's line first; the current editor is not repeated; no duplicates, no junk
        CHECK(OpenWithChoices("", inst, "2.855.0").size() == 4 && !OpenWithChoices("", inst, "2.855.0")[0].sameLine);
        CHECK(OpenWithChoices("2.850.0", {}, "2.855.0").empty() && OpenWithChoices("2.850.0", {"2.855.0"}, "2.855").empty());

        const std::string scene = "/home/u/My Projects/Spa\xC3\x9F-Spiel \"2\"/Assets/main.okayscene";
        const std::vector<std::string> args = OpenWithArgs(4242, scene);
        CHECK(args == (std::vector<std::string>{"--launcher", "4242", "--open", scene, "--keep-version"}));
        // Quoting: spaces, unicode, quotes, `$`, back-slashes and nothing at all each stay one argument.
        CHECK(QuoteArgWindows("plain") == "plain" && QuoteArgWindows("") == "\"\"" && QuoteArgWindows("a b") == "\"a b\"" &&
              QuoteArgWindows("C:\\My Games\\x.okayscene") == "\"C:\\My Games\\x.okayscene\"" && QuoteArgWindows("a\"b") == "\"a\\\"b\"" &&
              QuoteArgWindows("C:\\dir with space\\") == "\"C:\\dir with space\\\\\"" && QuoteArgWindows("Spa\xC3\x9F") == "Spa\xC3\x9F" &&
              QuoteArgWindows("q\\\"x") == "\"q\\\\\\\"x\"");
        CHECK(QuoteArgPosix("plain") == "'plain'" && QuoteArgPosix("") == "''" && QuoteArgPosix("a b") == "'a b'" && QuoteArgPosix("it's") == "'it'\\''s'" &&
              QuoteArgPosix("$HOME `x` \"y\" \\z") == "'$HOME `x` \"y\" \\z'" && QuoteArgPosix("Spa\xC3\x9F") == "'Spa\xC3\x9F'");
        CHECK(JoinArgs({"--open", "a b"}) == QuoteArg("--open") + " " + QuoteArg("a b") && JoinArgs({}).empty());
#if !defined(_WIN32)
        // The POSIX form really is one argument to a shell, whatever is in it.
        {
            TempDir t;
            const std::string tricky = "a b $HOME `echo x` 'q' \"d\" \\ \xC3\xA4\xE6\x97\xA5; echo pwned";
            const fs::path out = t.path / "argv.txt";
            const std::string cmd = "printf '%s' " + QuoteArgPosix(tricky) + " > " + QuoteArgPosix(out.string());
            CHECK(std::system(cmd.c_str()) == 0 && Get(out) == tricky);
        }
#endif

        const std::vector<std::string> inst2 = {"2.850.1", "2.700.0"};
        OpenTarget ot = ChooseEditorForOpen("", "2.855.0", inst2, "2.850.1");                               // no stamp + a default editor: that editor, with --keep-version
        CHECK(ot.version == "2.850.1" && ot.keepFlag);
        ot = ChooseEditorForOpen("2.850.0", "2.855.0", inst2, "2.850.1");                                    // a stamped project: the plain Open (the editor asks its own question)
        CHECK(ot.version.empty() && !ot.keepFlag);
        CHECK(ChooseEditorForOpen("", "2.855.0", inst2, "").version.empty());                                // no default
        CHECK(ChooseEditorForOpen("", "2.855.0", inst2, "2.850.7").version.empty());                          // the default is not installed (any more)
        CHECK(ChooseEditorForOpen("", "2.850.1", inst2, "2.850.1").version.empty());                          // the default is this editor
        CHECK(ChooseEditorForOpen("garbage", "2.855.0", inst2, "2.700.0").version == "2.700.0");              // an unreadable stamp counts as none

        // One editor at a time: another version means the open one has to close first.
        CHECK(!MustCloseEditorFirst(false, "2.700.0", "2.855.0", "2.850.1"));                                // none open
        CHECK(MustCloseEditorFirst(true, "2.855.0", "2.855.0", "2.850.1"));                                   // this version's editor is open, another wanted
        CHECK(!MustCloseEditorFirst(true, "2.850.1", "2.855.0", "2.850.1"));                                  // the wanted version is open: the project is handed to it
        CHECK(!MustCloseEditorFirst(true, "", "2.855.0", ""));                                                // unknown, the plain Open: assumed to be this one
        CHECK(MustCloseEditorFirst(true, "", "2.855.0", "2.850.1"));                                          // unknown, another wanted
        CHECK(MustCloseEditorFirst(true, "2.850.1", "2.855.0", ""));                                          // an older editor open, a plain Open: close it first
        CHECK(!MustCloseEditorFirst(true, "v2.855", "2.855.0", "2.855.0"));
    }

    // ---- the project folder is never written -----------------------------------------------------------------------------------------
    {
        TempDir t;
        const fs::path proj = t.path / "My Project \xC3\xA4";
        Put(proj / "project.okayproj", "engine_version=2.850.1\nname=Test\n");
        Put(proj / "Assets" / "main.okayscene", "okayscene 3\n");
        Put(proj / "notes.txt", "hello");
        const fs::path launcher = MakeLauncher(t.path, "2.855.0");
        const auto before = Snapshot(proj);
        // Reading the version, the badge, the choices, the arguments, keeping and pruning: none of it writes into the project.
        const std::string pv = okayver::ReadProjectVersion(proj.string());
        CHECK(pv == "2.850.1");
        (void)BadgeFor(pv, "2.855.0");
        (void)OpenWithChoices(pv, okayver::InstalledVersions(launcher), "2.855.0");
        (void)OpenWithArgs(1, (proj / "Assets" / "main.okayscene").string());
        (void)ChooseEditorForOpen(pv, "2.855.0", {}, "");
        IO io;
        io.availableSpace = [](const fs::path&, std::error_code& ec) { ec.clear(); return (std::uintmax_t)64 * 1024 * kMiB; };
        CHECK(KeepCurrent(launcher, "2.855.0", FindCurrentInstall(launcher), 1, true, io).ok());
        (void)Prune(launcher, 0, "", RemoveGuards{}, io);
        CHECK(Snapshot(proj) == before);
        CHECK(okayver::ReadProjectVersion(proj.string()) == "2.850.1");
        // A project without a stamp stays without one (the launcher never stamps).
        Put(proj / "project.okayproj", "name=Old\n");
        CHECK(okayver::ReadProjectVersion(proj.string()).empty() && BadgeFor(okayver::ReadProjectVersion(proj.string()), "2.855.0").version == "unknown" &&
              Get(proj / "project.okayproj") == "name=Old\n");
    }

    // ---- `--versions` on the command line, and the hand-off to the running launcher ---------------------------------------------------
    {
        CHECK(!ParseLauncherArgs({}).versions && !ParseLauncherArgs({"--launcher", "123"}).versions);
        LauncherArgs a = ParseLauncherArgs({"--versions", "2.850.1"});
        CHECK(a.versions && a.version == "2.850.1");
        a = ParseLauncherArgs({"--versions", "v2.850"});
        CHECK(a.versions && a.version == "2.850.0");
        a = ParseLauncherArgs({"--versions"});
        CHECK(a.versions && a.version.empty());
        a = ParseLauncherArgs({"--versions", "--other"});
        CHECK(a.versions && a.version.empty());
        a = ParseLauncherArgs({"--versions=2.850.1"});
        CHECK(a.versions && a.version == "2.850.1");
        a = ParseLauncherArgs({"--versions", "garbage"});
        CHECK(a.versions && a.version.empty());                                                             // the page opens, nothing is highlighted
        a = ParseLauncherArgs({"--foo", "--versions", "2.850.1", "--bar"});
        CHECK(a.versions && a.version == "2.850.1");

        TempDir t;
        const fs::path f = t.path / "open.txt";
        CHECK(!HandoffFile().empty() && HandoffFile().filename() == "okayspace_launcher_open.txt");
        LauncherArgs got;
        CHECK(!TakeHandoff(f, 1000, &got));                                                                 // nothing there
        LauncherArgs send;
        send.versions = true; send.version = "2.850.1";
        CHECK(WriteHandoff(f, send, 1000));
        CHECK(TakeHandoff(f, 1010, &got) && got.versions && got.version == "2.850.1" && !fs::exists(f));    // read once, then gone
        CHECK(!TakeHandoff(f, 1010, &got));
        send.version.clear();
        CHECK(WriteHandoff(f, send, 1000) && TakeHandoff(f, 1000, &got) && got.versions && got.version.empty());
        CHECK(WriteHandoff(f, send, 1000) && !TakeHandoff(f, 1000 + 121, &got) && !fs::exists(f));          // a crash left it: ignored and deleted
        CHECK(WriteHandoff(f, send, 5000) && !TakeHandoff(f, 1000, &got) && !fs::exists(f));                 // from the future: ignored too
        for (const char* junk : {"", "\n", "garbage", "versions", "versions 2.850.1", "versions 2.850.1 notanumber", "open 2.850.1 1000"}) {
            Put(f, junk);
            CHECK(!TakeHandoff(f, 1000, &got) && !fs::exists(f));
        }
        Put(f, "versions ../../etc 1000\n");
        CHECK(TakeHandoff(f, 1000, &got) && got.version.empty());                                           // a version that is not one is not highlighted
        Put(f, "versions \x01\x02 1000\n");
        CHECK(TakeHandoff(f, 1000, &got) && got.version.empty());
        CHECK(TakeHandoff(f, 1000, nullptr) == false);
        Put(f, "versions 2.850.1 1000\n");
        CHECK(TakeHandoff(f, 1000, nullptr));                                                               // the caller may not care about the content
        CHECK(!WriteHandoff(t.path / "no" / "such" / "dir" / "x.txt", send, 1000));
    }

    // ---- words -------------------------------------------------------------------------------------------------------------------------
    {
        CHECK(HumanSize(0) == "0 B" && HumanSize(1023) == "1023 B" && HumanSize(1024) == "1 KB" && HumanSize(1536) == "1 KB" && HumanSize(kMiB) == "1.0 MB" &&
              HumanSize(41 * kMiB + 300 * 1024) == "41.3 MB" && HumanSize(2048 * kMiB) == "2.0 GB" && HumanSize(~0ull).find("GB") != std::string::npos);
        CHECK(LtsSentence("2.850.1") == "v2.850.x is the long-term-support version: it only gets fixes." && !LtsSentence("junk").empty());
        CHECK(std::string(OutcomeName(Outcome::Installed)) == "Installed" && std::string(OutcomeName(Outcome::Skipped)) == "Skipped");
        CHECK(std::string(IndexName(Asset::Editor)) == "OkayEngine.exe" && DiskName(Asset::Editor) == okayver::EditorExeName() && DiskName(Asset::Sdl) == "SDL2.dll");
        CHECK(DirectorySize("/no/such/dir/at/all") == 0 && DiskUsage("/no/such/launcher") == 0);
    }

#if !defined(_WIN32)
    // ---- the real download: a cancellable curl child (a stub curl on PATH: no network) ----------------------------------------------------
    {
        using namespace okayversions::net;
        TempDir t;
        const fs::path bin = t.path / "bin dir";
        const auto stub = [&](const std::string& body) {
            Put(bin / "curl", "#!/bin/sh\n" + body);
            fs::permissions(bin / "curl", fs::perms::owner_all);
        };
        const char* oldPath = std::getenv("PATH");
        const std::string saved = oldPath ? oldPath : "";
        setenv("PATH", (bin.string() + ":" + saved).c_str(), 1);
        // The stub finds the argument after -o (the destination) and the last argument (the address), like curl.
        const std::string prelude = "dest=; url=; while [ $# -gt 0 ]; do case \"$1\" in -o) dest=\"$2\"; shift;; *) url=\"$1\";; esac; shift; done\n";
        const fs::path dest = t.path / "dl dir \xC3\xA4" / "file.part";
        std::vector<std::uint64_t> bytes;
        auto onBytes = [&](std::uint64_t b) { bytes.push_back(b); };

        stub(prelude + "printf 'hello %s' \"$url\" > \"$dest\"\nexit 0\n");
        DownloadResult r = DownloadFile("https://example.test/v2.850.1/OkayEngine.exe", dest, 1000, onBytes, [] { return false; });
        CHECK(r.status == DownloadStatus::Ok && Get(dest) == "hello https://example.test/v2.850.1/OkayEngine.exe" && !bytes.empty() && bytes.back() == Get(dest).size());
        stub("echo \"$@\" > \"" + (t.path / "args.txt").string() + "\"\n" + prelude + "printf x > \"$dest\"\n");
        r = DownloadFile("https://example.test/a.exe", dest, 1000, onBytes, [] { return false; });
        std::string args = Get(t.path / "args.txt");
        CHECK(r.status == DownloadStatus::Ok && Has(args, "-L") && Has(args, "-f") && Has(args, "--proto-redir =https") && Has(args, "-o " + dest.string() + " https://example.test/a.exe"));
        r = DownloadFile("http://127.0.0.1:8080/a.exe", dest, 1000, onBytes, [] { return false; });
        args = Get(t.path / "args.txt");
        CHECK(r.status == DownloadStatus::Ok && !Has(args, "--proto-redir") && Has(args, "http://127.0.0.1:8080/a.exe"));

        // Not allowed addresses never reach the program.
        for (const char* url : {"http://example.test/x", "file:///etc/passwd", "https://example.test/a b", "https://example.test/$(id)", ""}) {
            CHECK(DownloadFile(url, dest, 1000, onBytes, [] { return false; }).status == DownloadStatus::Failed);
        }
        // An HTTP error (curl -f exits 22), a full disk (23) and a program that is not there.
        stub("exit 22\n");
        r = DownloadFile("https://example.test/x", dest, 1000, onBytes, [] { return false; });
        CHECK(r.status == DownloadStatus::Failed && Has(r.error, "does not have") && !fs::exists(dest));
        stub("exit 23\n");
        CHECK(DownloadFile("https://example.test/x", dest, 1000, onBytes, [] { return false; }).status == DownloadStatus::DiskFull);
        stub("exit 6\n");
        r = DownloadFile("https://example.test/x", dest, 1000, onBytes, [] { return false; });
        CHECK(r.status == DownloadStatus::Failed && Has(r.error, "6"));
        stub(prelude + "exit 0\n");                                                                         // "succeeded" but wrote nothing
        CHECK(DownloadFile("https://example.test/x", dest, 1000, onBytes, [] { return false; }).status == DownloadStatus::Failed);
        // Cancel: the child is ended within a poll or two even though it would run for a minute, and the partial file is removed.
        stub(prelude + "printf 'partial' > \"$dest\"\nexec sleep 60\n");
        int polls = 0;
        const auto t0 = std::chrono::steady_clock::now();
        r = DownloadFile("https://example.test/x", dest, 1000, onBytes, [&] { return ++polls > 3; });
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(r.status == DownloadStatus::Cancelled && !fs::exists(dest) && secs < 10.0);
        // A body bigger than the file should be is stopped and fails.
        stub(prelude + "head -c 5000 /dev/zero > \"$dest\"\nexec sleep 60\n");
        r = DownloadFile("https://example.test/x", dest, 1000, onBytes, [] { return false; });
        CHECK(r.status == DownloadStatus::Failed && Has(r.error, "more data") && !fs::exists(dest));

        // The real download function inside a real install, end to end: the stub plays the server.
        const fs::path launcher = MakeLauncher(t.path, "2.856.0");
        const Index idx = ParseIndex(IndexJson({EntryJson("2.850.1", "lts", true)}, "2.856.0", "2.850.1"));
        const fs::path serve = t.path / "serve";
        for (const char* f : {"OkayEngine.exe", "OkaySpacePlayer.exe"}) Put(serve / f, Body("2.850.1", f));
        stub(prelude + "cp \"" + serve.string() + "/${url##*/}\" \"$dest\"\n");
        IO io;
        io.download = &DownloadFile;
        CHECK(Install(launcher, PlanFor(idx, "2.850.1", launcher), {}, io).ok() && Complete(launcher, "2.850.1") && NoScratch(launcher));
        // ...and with a tampered file on the "server": refused, nothing kept.
        fs::remove_all(okayver::VersionsDir(launcher));
        Put(serve / "OkayEngine.exe", std::string(Body("2.850.1", "OkayEngine.exe").size(), 'Q'));
        const Result bad = Install(launcher, PlanFor(idx, "2.850.1", launcher), {}, io);
        CHECK(bad.outcome == Outcome::Failed && Has(bad.message, "checksum") && !fs::exists(okayver::VersionsDir(launcher) / "2.850.1") && NoScratch(launcher));

        // RunProcess on its own.
        RunResult rr = RunProcess({"sh", "-c", "exit 3"}, [] { return true; });
        CHECK(rr.started && !rr.stopped && rr.exitCode == 3);
        rr = RunProcess({"no-such-program-okay-versions"}, [] { return true; });
        CHECK(!rr.started);
        rr = RunProcess({}, [] { return true; });
        CHECK(!rr.started);
        rr = RunProcess({"sleep", "60"}, [] { return false; }, 10);
        CHECK(rr.started && rr.stopped);

        setenv("PATH", saved.c_str(), 1);
    }
#endif

    TEST_MAIN_RESULT();
}
