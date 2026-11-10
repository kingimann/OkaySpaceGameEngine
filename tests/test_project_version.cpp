// The project's engine version (shared/ProjectVersion.hpp): parsing and comparing version text, the Decide table the
// editor's upgrade question follows, the `engine_version` line in project.okayproj (read, written, everything else in
// the file kept), and the folder layout the launcher keeps other versions in. Header-only and engine-free: this test
// links nothing, which is the proof the launcher can use it. Temp folders only; nothing sleeps or reads the clock.
#include "test_framework.hpp"
#include "../shared/ProjectVersion.hpp"

#include <chrono>
#include <fstream>
#include <set>

namespace fs = std::filesystem;
using namespace okayver;

namespace {
struct TempDir {
    fs::path path;
    TempDir() {
        static int n = 0;
        path = fs::temp_directory_path() / ("okay_projver_" + std::to_string(++n) + "_" +
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
    std::string str() const { return path.string(); }
};
void Write(const fs::path& p, const std::string& s) { std::ofstream(p, std::ios::binary | std::ios::trunc) << s; }
std::string Read(const fs::path& p) { std::string s; okayproj::ReadText(p.string(), s); return s; }
bool Has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
std::string Parts(const std::string& text) {
    const Version v = ParseVersion(text);
    std::string s = v.valid ? "ok:" : (v.dev ? "dev" : "bad");
    for (std::uint64_t p : v.parts) s += " " + std::to_string(p);
    return s;
}
}  // namespace

int main() {
    RUN_SUITE("project_version");

    // ---- parsing -------------------------------------------------------------------------------------------------
    {
        CHECK(Parts("2.855.0") == "ok: 2 855 0");
        CHECK(Parts("v2.855.0") == "ok: 2 855 0");
        CHECK(Parts("V2.855.0") == "ok: 2 855 0");
        CHECK(Parts("2.855") == "ok: 2 855");
        CHECK(Parts("2") == "ok: 2");
        CHECK(Parts("  2.855.0  ") == "ok: 2 855 0");
        CHECK(Parts("2.855.0\r\n") == "ok: 2 855 0");          // a line read from a file
        CHECK(Parts("\t2.855.0\n") == "ok: 2 855 0");
        CHECK(Parts("2.855.0-rc1") == "ok: 2 855 0");          // a suffix is ignored
        CHECK(Parts("2.855.0+build.5") == "ok: 2 855 0");
        CHECK(Parts("2.855.0_beta") == "ok: 2 855 0");
        CHECK(Parts("2.855.0 (kept)") == "ok: 2 855 0");
        CHECK(Parts("1.2.3.4.5") == "ok: 1 2 3 4 5");
        CHECK(Parts("0.0.0") == "ok: 0 0 0");
        CHECK(Parts("007.08") == "ok: 7 8");                   // leading zeros are just zeros

        CHECK(Parts("dev") == "dev");
        CHECK(Parts("Dev") == "dev");
        CHECK(Parts("DEV") == "dev");
        CHECK(Parts("dev-abc123") == "dev");
        CHECK(Parts(" dev\r\n") == "dev");
        CHECK(IsDevVersion("dev") && !IsDevVersion("2.855.0") && !IsDevVersion("") && !IsDevVersion("devil") && !IsDevVersion("dev1"));

        for (const char* bad : {"", " ", "\n", "\r\n", "garbage", "abc.def", "2.x", "2.x.3", "2..3", "2.855.", ".2", ".", "..", "-1", "+2",
                                "2.855.0banana", "2.855.0)", "v", "V", "vv2", "version 2", "2,855,0", "2:855",
                                "\xEF\xBB\xBF" "2.855.0", "devil", "dev1", "d", "de", "null", "NaN", "0x10"})
            CHECK(Parts(bad) == "bad");                        // including a BOM: a line reader strips it before it gets here
        CHECK(Parts(std::string("2.8\0.5", 6)) == "bad");      // an embedded NUL is no separator
        CHECK(Parts("\xEF\xBC\x92.\xEF\xBC\x98") == "bad");    // full-width digits are not digits
        CHECK(Parts(std::string(kMaxVersionChars + 1, '1')) == "bad");
        CHECK(Parts(std::string(kMaxVersionChars, '1')).rfind("ok:", 0) == 0);
        std::string many;                                      // 16 components are read, 17 are too many
        for (int i = 0; i < 16; ++i) many += (i ? ".1" : "1");
        CHECK(Parts(many).rfind("ok:", 0) == 0);
        CHECK(Parts(many + ".1") == "bad");

        // huge components clamp, never overflow or throw
        CHECK(Parts("18446744073709551615") == "ok: 18446744073709551615");
        CHECK(Parts("18446744073709551616") == "ok: 18446744073709551615");
        CHECK(Parts("99999999999999999999999.1.0") == "ok: 18446744073709551615 1 0");
        CHECK(Parts("2." + std::string(60, '9')) == "ok: 2 18446744073709551615");
        CHECK(CompareVersions("99999999999999999999999", "1") == 1);
        CHECK(CompareVersions("99999999999999999999999", "99999999999999999999998") == 0);   // both clamp: the documented limit
        CHECK(CompareVersions("2.99999999999999999999999.0", "2.855.0") == 1);

        // formatting
        CHECK(FormatVersion(ParseVersion("2.855")) == "2.855.0");
        CHECK(FormatVersion(ParseVersion("v2.855.0")) == "2.855.0");
        CHECK(FormatVersion(ParseVersion("1")) == "1.0.0");
        CHECK(FormatVersion(ParseVersion("1.2.3.4")) == "1.2.3.4");
        CHECK(FormatVersion(ParseVersion("007.08")) == "7.8.0");
        CHECK(FormatVersion(ParseVersion("dev")).empty() && FormatVersion(ParseVersion("")).empty() && FormatVersion(Version{}).empty());
        CHECK(NormalizeVersion(" v2.9-rc1 ") == "2.9.0");
        CHECK(NormalizeVersion("junk").empty() && NormalizeVersion("dev").empty());
        CHECK(DisplayVersion("2.855") == "v2.855.0" && DisplayVersion("dev") == "dev" && DisplayVersion("junk") == "junk" && DisplayVersion("").empty());
        CHECK(NormalizeVersion(NormalizeVersion("v2.9")) == NormalizeVersion("v2.9"));   // idempotent
    }

    // ---- comparing -----------------------------------------------------------------------------------------------
    {
        struct Row { const char* a; const char* b; int want; };
        const Row rows[] = {
            {"2.855.0", "2.855.0", 0}, {"2.855.0", "2.855", 0}, {"2.855", "2.855.0", 0}, {"v2.855.0", "2.855.0", 0}, {"1", "1.0.0", 0},
            {"2.850.0", "2.855.0", -1}, {"2.855.0", "2.850.0", 1}, {"2.9", "2.855", -1}, {"2.855", "2.9", 1},   // numbers, not text
            {"2.100.0", "2.99.0", 1}, {"10.0.0", "9.99.99", 1}, {"1.0.0", "2.0.0", -1},
            {"2.855.0", "2.855.1", -1}, {"2.855.0", "2.855.0.1", -1}, {"2.855.0.1", "2.855.0", 1}, {"2.855.0.0", "2.855.0", 0},
            {"2.855.0-rc1", "2.855.0", 0}, {"2.855.0-rc1", "2.855.1", -1},
            {"dev", "2.855.0", -1}, {"2.855.0", "dev", 1}, {"dev", "dev", 0}, {"", "2.855.0", -1}, {"2.855.0", "", 1}, {"", "", 0},
            {"garbage", "2.855.0", -1}, {"2.855.0", "garbage", 1}, {"garbage", "other", 0}, {"dev", "", 0}, {"dev", "garbage", 0},
            {"0.0.0", "dev", 1}, {"0", "0.0.0", 0},
        };
        for (const Row& r : rows) {
            if (CompareVersions(r.a, r.b) != r.want) std::cerr << "  compare(\"" << r.a << "\", \"" << r.b << "\") wanted " << r.want << "\n";
            CHECK(CompareVersions(r.a, r.b) == r.want);
            CHECK(CompareVersions(r.b, r.a) == -r.want);        // antisymmetric
        }
        // a total order over a mixed list: sorting it any way gives one answer
        std::vector<std::string> vs = {"2.855.0", "dev", "1.0.0", "2.9", "", "2.855.0.1", "v2.10.0", "garbage", "0.1", "3"};
        std::sort(vs.begin(), vs.end(), [](const std::string& a, const std::string& b) { return CompareVersions(a, b) < 0; });
        for (std::size_t i = 1; i < vs.size(); ++i) CHECK(CompareVersions(vs[i - 1], vs[i]) <= 0);
        CHECK(CompareVersions(vs.front(), "0.0.1") < 0 && CompareVersions(vs.back(), "2.855.0.1") > 0);
    }

    // ---- Decide --------------------------------------------------------------------------------------------------
    {
        struct Row { const char* project; const char* running; Decision want; };
        const Row rows[] = {
            {"2.855.0", "2.855.0", Decision::Same},   {"2.855", "2.855.0", Decision::Same},   {"v2.855.0", "2.855.0", Decision::Same},
            // another patch of the same line: nothing to ask, either way round
            {"2.850.0", "2.850.1", Decision::SamePatchLine}, {"2.850.1", "2.850.0", Decision::SamePatchLine}, {"2.850.2", "2.850.1", Decision::SamePatchLine},
            {"v2.850.0", "2.850.9", Decision::SamePatchLine}, {"2.850", "2.850.3", Decision::SamePatchLine}, {"2.850.0.1", "2.850.0", Decision::SamePatchLine},
            {"2.850.0-rc1", "2.850.1", Decision::SamePatchLine},
            {"2.849.9", "2.850.0", Decision::Older}, {"2.850.9", "2.851.0", Decision::Older}, {"2.851.0", "2.850.9", Decision::Newer},   // another line is not a patch
            {"3.850.0", "2.850.1", Decision::Newer}, {"1.850.0", "2.850.1", Decision::Older},
            {"2.850.0", "2.855.0", Decision::Older},  {"2.9", "2.855.0", Decision::Older},    {"1.0.0", "2.855.0", Decision::Older},
            {"2.860.0", "2.855.0", Decision::Newer},  {"3.0.0", "2.855.0", Decision::Newer},  {"2.855.0.1", "2.855.0", Decision::SamePatchLine},
            {"", "2.855.0", Decision::Unknown},       {"garbage", "2.855.0", Decision::Unknown}, {"dev", "2.855.0", Decision::Unknown},
            {"  ", "2.855.0", Decision::Unknown},     {"2.x", "2.855.0", Decision::Unknown},
            {"2.855.0", "dev", Decision::DevBuild},   {"", "dev", Decision::DevBuild},       {"dev", "dev", Decision::DevBuild},
            {"2.850.0", "dev", Decision::DevBuild},   {"2.860.0", "dev", Decision::DevBuild}, {"garbage", "dev", Decision::DevBuild},
            {"2.855.0", "", Decision::DevBuild},      {"", "", Decision::DevBuild},           {"2.850.0", "garbage", Decision::DevBuild},
            {"2.855.0-rc1", "2.855.0", Decision::Same},
        };
        for (const Row& r : rows) {
            if (Decide(r.project, r.running) != r.want)
                std::cerr << "  Decide(\"" << r.project << "\", \"" << r.running << "\") gave " << DecisionName(Decide(r.project, r.running)) << "\n";
            CHECK(Decide(r.project, r.running) == r.want);
        }
        CHECK(std::string(DecisionName(Decision::Older)) == "Older" && std::string(DecisionName(Decision::DevBuild)) == "DevBuild");
        std::set<std::string> names;
        for (Decision d : {Decision::Same, Decision::SamePatchLine, Decision::Older, Decision::Newer, Decision::Unknown, Decision::DevBuild}) names.insert(DecisionName(d));
        CHECK(names.size() == 6 && std::string(DecisionName(Decision::SamePatchLine)) == "SamePatchLine");
        // a dev build or an unstamped project is never mistaken for a patch of something
        CHECK(Decide("2.850.0", "dev") == Decision::DevBuild && Decide("", "2.850.1") == Decision::Unknown && Decide("dev", "2.850.1") == Decision::Unknown);
    }

    // ---- release lines and the keep target -------------------------------------------------------------------------
    {
        CHECK(LineOf("2.850.0") == "2.850" && LineOf("2.850.1") == "2.850" && LineOf("v2.850.17-rc1") == "2.850" && LineOf("2.850") == "2.850");
        CHECK(LineOf("2") == "2.0" && LineOf("v3") == "3.0" && LineOf("2.850.0.1") == "2.850" && LineOf("007.08.1") == "7.8");
        CHECK(LineOf("").empty() && LineOf("dev").empty() && LineOf("junk").empty() && LineOf("2.x").empty());
        CHECK(LineOf("99999999999999999999999.1.0") == "18446744073709551615.1");
        CHECK(SameLine("2.850.0", "2.850.1") && SameLine("2.850.0", "v2.850") && SameLine("2.850", "2.850.0") && SameLine("2", "2.0.5"));
        CHECK(!SameLine("2.850.0", "2.851.0") && !SameLine("2.850.0", "3.850.0") && !SameLine("2.9.0", "2.90.0"));   // numbers, not text prefixes
        CHECK(!SameLine("", "") && !SameLine("dev", "dev") && !SameLine("junk", "junk") && !SameLine("2.850.0", "dev") && !SameLine("", "2.850.0"));
        CHECK(LineLabel("2.850.0") == "v2.850.x" && LineLabel("v2.850.3") == "v2.850.x" && LineLabel("dev").empty() && LineLabel("").empty());

        const std::vector<std::string> none, inst = {"2.850.0", "2.850.1", "2.855.0", "2.849.0"};
        CHECK(ResolveKeepTarget("2.850.0", inst) == "2.850.1");               // the newest of the project's line, not the exact version
        CHECK(ResolveKeepTarget("2.850.1", inst) == "2.850.1");
        CHECK(ResolveKeepTarget("v2.850", inst) == "2.850.1");
        CHECK(ResolveKeepTarget("2.855.0", inst) == "2.855.0");
        CHECK(ResolveKeepTarget("2.849.7", inst) == "2.849.0");               // an older patch of the line is still the line
        CHECK(ResolveKeepTarget("2.851.0", inst).empty());                    // no editor of that line is installed
        CHECK(ResolveKeepTarget("2.850.0", none).empty());
        CHECK(ResolveKeepTarget("", inst).empty() && ResolveKeepTarget("dev", inst).empty() && ResolveKeepTarget("junk", inst).empty());
        CHECK(ResolveKeepTarget("2.850.0", {"2.850.9", "2.850.10", "2.850.2"}) == "2.850.10");   // numerically newest
        CHECK(ResolveKeepTarget("2.850.0", {"2.850.1", "2.850.1", "v2.850.1"}) == "2.850.1");
        CHECK(ResolveKeepTarget("2.850.0", {"junk", "", "dev", "2.850.1"}) == "2.850.1");   // text that is no version is never a target
        CHECK(ResolveKeepTarget("2.850.0", {"junk", "", "dev"}).empty());
        CHECK(ResolveKeepTarget("2.850.0", {"2.850.1"}) != "2.850.0");
    }

    // ---- the stamp in text ---------------------------------------------------------------------------------------
    {
        std::string raw;
        CHECK(!FindStampInText(""));
        CHECK(!FindStampInText("company=Me\nversion=1.0.0\n"));              // `version=` is the game's version, not the engine's
        CHECK(FindStampInText("engine_version=2.855.0\n", &raw) && raw == "2.855.0");
        CHECK(FindStampInText("a=1\nengine_version=2.855.0", &raw) && raw == "2.855.0");              // no final newline
        CHECK(FindStampInText("a=1\r\nengine_version=2.855.0\r\nb=2\r\n", &raw) && raw == "2.855.0"); // CRLF
        CHECK(FindStampInText("  engine_version  =  2.855.0  \n", &raw) && raw == "2.855.0");       // blanks around the key and value
        CHECK(FindStampInText("\xEF\xBB\xBF" "engine_version=2.855.0\n", &raw) && raw == "2.855.0");  // a BOM before the first key
        CHECK(FindStampInText("engine_version=1.0.0\nengine_version=2.0.0\n", &raw) && raw == "2.0.0");   // the last one wins, like every reader
        CHECK(FindStampInText("engine_version=\n", &raw) && raw.empty());
        CHECK(!FindStampInText("engine_versionX=2.0.0\nxengine_version=2.0.0\nengine_version 2.0.0\n#engine_version=2.0.0\n"));

        // SetStampInText: append, replace in place, keep every other byte
        CHECK(SetStampInText("", "2.855.0") == "engine_version=2.855.0\n");
        CHECK(SetStampInText("a=1\n", "2.855.0") == "a=1\nengine_version=2.855.0\n");
        CHECK(SetStampInText("a=1", "2.855.0") == "a=1\nengine_version=2.855.0\n");                  // a final newline is added first
        CHECK(SetStampInText("a=1\r\nb=2\r\n", "2.855.0") == "a=1\r\nb=2\r\nengine_version=2.855.0\r\n");   // CRLF stays CRLF
        CHECK(SetStampInText("a=1\r\nb=2", "2.855.0") == "a=1\r\nb=2\r\nengine_version=2.855.0\r\n");
        CHECK(SetStampInText("a=1\nengine_version=2.850.0\nb=2\n", "2.855.0") == "a=1\nengine_version=2.855.0\nb=2\n");   // in place
        CHECK(SetStampInText("a=1\r\nengine_version=2.850.0\r\nb=2\r\n", "2.855.0") == "a=1\r\nengine_version=2.855.0\r\nb=2\r\n");
        CHECK(SetStampInText("engine_version=2.850.0", "2.855.0") == "engine_version=2.855.0");     // no final newline stays none
        CHECK(SetStampInText("\xEF\xBB\xBF" "engine_version=2.850.0\nb=2\n", "2.855.0") == "\xEF\xBB\xBF" "engine_version=2.855.0\nb=2\n");
        CHECK(SetStampInText("\xEF\xBB\xBF" "a=1\n", "2.855.0") == "\xEF\xBB\xBF" "a=1\nengine_version=2.855.0\n");
        CHECK(SetStampInText("engine_version=1\na=1\nengine_version=2\n", "2.855.0") == "engine_version=2.855.0\na=1\n");   // a second line is dropped
        CHECK(SetStampInText("# my notes\n\n\nfuture_key=abc def\n", "2.855.0") == "# my notes\n\n\nfuture_key=abc def\nengine_version=2.855.0\n");
        const std::string once = SetStampInText("a=1\nb=2\n", "2.855.0");
        CHECK(SetStampInText(once, "2.855.0") == once);                                               // idempotent
        CHECK(SetStampInText(SetStampInText(once, "2.860.0"), "2.855.0") == once);                    // and reversible

        // KeepUnknownLines: what a writer does not own, comments included
        auto owns = [](const std::string& k) { return k == "company" || k == "tag" || k.rfind("layer_name_", 0) == 0; };
        CHECK(KeepUnknownLines("company=Me\ntag=Enemy\nlayer_name_3=Walls\n", owns).empty());
        CHECK(KeepUnknownLines("company=Me\nfuture=1\n# note\nengine_version=2.0.0\n\n  \nother = x y\n", owns) == "future=1\n# note\nother = x y\n");
        CHECK(KeepUnknownLines("a=1\r\nb=2\r\n", owns) == "a=1\nb=2\n");
        CHECK(KeepUnknownLines("\xEF\xBB\xBF" "future=1\n", owns) == "future=1\n");
        CHECK(KeepUnknownLines("", owns).empty() && KeepUnknownLines("company=x", {}) == "company=x\n");

        // ComposeProjectFile: what a full save writes: the stamp, the writer's text, then the lines it does not own
        const std::string gen = "company=Me\ntag=Enemy\n";
        CHECK(ComposeProjectFile(gen, "", "2.855.0", owns) == "engine_version=2.855.0\ncompany=Me\ntag=Enemy\n");              // a new project
        CHECK(ComposeProjectFile(gen, "company=Old\nengine_version=2.850.0\n", "2.855.0", owns) == "engine_version=2.855.0\ncompany=Me\ntag=Enemy\n");
        CHECK(ComposeProjectFile(gen, "company=Old\nfuture=1\n# note\n", "2.855.0", owns) ==
              "engine_version=2.855.0\ncompany=Me\ntag=Enemy\nfuture=1\n# note\n");                                              // unknown lines survive
        CHECK(ComposeProjectFile("company=Me", "future=1\n", "2.855.0", owns) == "engine_version=2.855.0\ncompany=Me\nfuture=1\n");   // no final newline
        CHECK(ComposeProjectFile(gen, "engine_version=2.850.0\nfuture=1\n", "", owns) == "engine_version=2.850.0\ncompany=Me\ntag=Enemy\nfuture=1\n");  // a dev build keeps the stamp
        CHECK(ComposeProjectFile(gen, "company=Old\n", "", owns) == gen);                                                        // ... and adds none
        CHECK(ComposeProjectFile(gen, "engine_version=\n", "", owns) == gen);
        const std::string saved = ComposeProjectFile(gen, "future=1\r\n", "2.855.0", owns);
        CHECK(ComposeProjectFile(gen, saved, "2.855.0", owns) == saved);                                                         // saving twice changes nothing
        CHECK(FindStampInText(saved, &raw) && raw == "2.855.0");
    }

    // ---- the stamp in a project folder -----------------------------------------------------------------------------
    {
        TempDir td;
        const fs::path file = td.path / okayproj::SettingsFileName();
        CHECK(ProjectFile(td.str()) == file.string());

        // no file: nothing known
        Stamp st = ReadProjectStamp(td.str());
        CHECK(!st.fileFound && !st.hasKey && st.version.empty() && st.error.empty());
        CHECK(ReadProjectVersion(td.str()).empty());

        // an old project: a file without the key still reads, as Unknown
        Write(file, "company=Me\nversion=1.0.0\nwidth=1280\n");
        st = ReadProjectStamp(td.str());
        CHECK(st.fileFound && !st.hasKey && st.version.empty() && st.raw.empty());
        CHECK(Decide(st.version, "2.855.0") == Decision::Unknown);

        // writing it changes that one line only, keeps unknown keys, comments and odd spacing
        const std::string newer = "# written by a future editor\ncompany=Me\nversion=1.0.0\nfuture_setting=on  \nweird key = value\ntag=Enemy\n";
        Write(file, newer);
        std::string err;
        CHECK(WriteProjectVersion(td.str(), "2.855.0", &err) && err.empty());
        CHECK(Read(file) == newer + "engine_version=2.855.0\n");
        CHECK(ReadProjectVersion(td.str()) == "2.855.0");
        CHECK(ReadProjectStamp(td.str()).hasKey);
        CHECK(!fs::exists(file.string() + ".tmp"));                                  // the atomic writer cleans up
        CHECK(WriteProjectVersion(td.str(), "v2.860", &err) && err.empty());          // normalized, in place
        CHECK(Read(file) == newer + "engine_version=2.860.0\n");
        CHECK(WriteProjectVersion(td.str(), "2.860.0", &err));                        // unchanged: not written again
        CHECK(Read(file) == newer + "engine_version=2.860.0\n");

        // CRLF and a BOM survive
        const std::string crlf = "\xEF\xBB\xBF" "company=Me\r\nversion=1.0.0\r\nwidth=1280\r\n";
        Write(file, crlf);
        CHECK(WriteProjectVersion(td.str(), "2.855.0", &err));
        CHECK(Read(file) == crlf + "engine_version=2.855.0\r\n");
        CHECK(ReadProjectVersion(td.str()) == "2.855.0");
        CHECK(WriteProjectVersion(td.str(), "2.856.0", &err));
        CHECK(Read(file) == crlf + "engine_version=2.856.0\r\n");

        // a BOM in front of the stamp line itself
        Write(file, "\xEF\xBB\xBF" "engine_version=2.850.0\r\nwidth=1\r\n");
        CHECK(ReadProjectVersion(td.str()) == "2.850.0");
        CHECK(WriteProjectVersion(td.str(), "2.855.0", &err));
        CHECK(Read(file) == "\xEF\xBB\xBF" "engine_version=2.855.0\r\nwidth=1\r\n");

        // a stamp that is not a version reads as no version, and is replaced by a real one
        Write(file, "engine_version=banana\nwidth=1\n");
        st = ReadProjectStamp(td.str());
        CHECK(st.hasKey && st.raw == "banana" && st.version.empty());
        CHECK(Decide(st.version, "2.855.0") == Decision::Unknown);
        CHECK(WriteProjectVersion(td.str(), "2.855.0", &err) && Read(file) == "engine_version=2.855.0\nwidth=1\n");

        // refusals: a version that is not one, a read-only file that has to change, a folder that is not there
        Write(file, "width=1\n");
        CHECK(!WriteProjectVersion(td.str(), "dev", &err) && Has(err, "not a version"));
        CHECK(!WriteProjectVersion(td.str(), "", &err) && !err.empty());
        CHECK(Read(file) == "width=1\n");
        fs::permissions(file, fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read);
        CHECK(!WriteProjectVersion(td.str(), "2.855.0", &err) && Has(err, "read-only"));
        CHECK(Read(file) == "width=1\n");                                              // nothing changed
        CHECK(!fs::exists(file.string() + ".tmp"));
        fs::permissions(file, fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);
        Write(file, "engine_version=2.855.0\nwidth=1\n");
        fs::permissions(file, fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read);
        CHECK(WriteProjectVersion(td.str(), "2.855.0", &err));                         // already stamped: read-only is fine
        CHECK(!WriteProjectVersion(td.str(), "2.856.0", &err) && Has(err, "read-only"));
        CHECK(ReadProjectVersion(td.str()) == "2.855.0");                              // and readable
        fs::permissions(file, fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);
        CHECK(!WriteProjectVersion((td.path / "nowhere").string(), "2.855.0", &err) && !err.empty());

        // a missing file is created holding just the stamp
        fs::remove(file);
        CHECK(WriteProjectVersion(td.str(), "2.855.0", &err));
        CHECK(Read(file) == "engine_version=2.855.0\n");

        // a folder in the way of the file reads as no file
        fs::remove(file);
        fs::create_directory(file);
        CHECK(!ReadProjectStamp(td.str()).fileFound);
        fs::remove(file);

        // an empty folder argument means the working folder's file (the old layout): not touched here, only addressed
        CHECK(ProjectFile("") == okayproj::SettingsFileName());
    }

    // ---- where the launcher keeps other versions -----------------------------------------------------------------
    {
        const fs::path L = fs::path("install") / "OkaySpace";
        CHECK(LauncherDirOf(L / "Tools") == L);
        CHECK(LauncherDirOf(L / "Tools" / "") == L);                                  // a trailing separator
        CHECK(LauncherDirOf(L / "Versions" / "2.850.0" / "Tools") == L);              // a kept copy finds the same launcher
        CHECK(LauncherDirOf(L) == L);                                                  // an editor beside the launcher
        CHECK(LauncherDirOf(fs::path()).empty());
        CHECK(InstalledEditorPath(L, "2.850.0") == L / "Versions" / "2.850.0" / "Tools" / EditorExeName());
        CHECK(InstalledEditorPath(L, "v2.850") == InstalledEditorPath(L, "2.850.0"));   // the folder is named by the normalized version
        CHECK(InstalledEditorPath(L, "dev").empty() && InstalledEditorPath(L, "").empty());
        CHECK(VersionsDir(L) == L / "Versions");
        const std::vector<std::string> args = KeepVersionArgs("C:\\Games\\My Game\\Assets\\Main.okayscene");
        CHECK(args.size() == 3 && args[0] == "--open" && args[1] == "C:\\Games\\My Game\\Assets\\Main.okayscene" && args[2] == KeepVersionFlag());
        CHECK(std::string(KeepVersionFlag()) == "--keep-version" && std::string(StampKey()) == "engine_version");

        TempDir td;
        const fs::path launcher = td.path / "OkaySpace Launcher";                     // a path with a space
        auto install = [&](const std::string& v) {
            const fs::path exe = InstalledEditorPath(launcher, v);
            fs::create_directories(exe.parent_path());
            Write(exe, "x");
        };
        CHECK(InstalledVersions(launcher).empty() && !IsInstalled(launcher, "2.850.0"));
        install("2.850.0"); install("2.9.0"); install("2.855.0");
        fs::create_directories(VersionsDir(launcher) / "notes");                       // not a version
        fs::create_directories(VersionsDir(launcher) / "2.700.0" / "Tools");           // no editor inside
        fs::create_directories(VersionsDir(launcher) / "v2.800.0" / "Tools");          // not the canonical name
        Write(VersionsDir(launcher) / "2.600.0", "a file, not a folder");
        CHECK(IsInstalled(launcher, "2.850.0") && IsInstalled(launcher, "v2.850") && IsInstalled(launcher, "2.855.0"));
        CHECK(!IsInstalled(launcher, "2.700.0") && !IsInstalled(launcher, "2.851.0") && !IsInstalled(launcher, "dev") && !IsInstalled(launcher, ""));
        const std::vector<std::string> got = InstalledVersions(launcher);
        CHECK(got == (std::vector<std::string>{"2.855.0", "2.850.0", "2.9.0"}));       // newest first, numerically
        CHECK(ResolveKeepTarget("2.850.0", got) == "2.850.0");                         // what the launcher folder holds feeds the resolution
        install("2.850.1");
        CHECK(ResolveKeepTarget("2.850.0", InstalledVersions(launcher)) == "2.850.1");
    }

    TEST_MAIN_RESULT();
}
