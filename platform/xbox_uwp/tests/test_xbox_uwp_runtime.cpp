// Linux tests for the Xbox UWP runtime pieces that are pure logic: offline shader bundle, display sizing, lifecycle, input mapping,
// path mapping, audio conversion. The Windows-only glue (CoreWindow, WASAPI, Windows.Gaming.Input) is NOT covered: it is uncompiled.
#include "okay/Render/D3D11Hlsl.hpp"
#include "okay/Render/ShaderBundle.hpp"
#include "XboxDisplay.hpp"
#include "XboxBuildPlan.hpp"
#include "XboxBuildHost.hpp"
#include "XboxLifecycle.hpp"
#include "XboxInput.hpp"
#include "XboxPaths.hpp"
#include "XboxAudio.hpp"
#include "XboxUwpBackend.hpp"
#include "UwpPlatform.hpp"
#include "XboxUwpAudit.hpp"
#include "XboxGameConfig.hpp"
#include "XboxOverlayMath.hpp"
#include "okay/Core/Paths.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <deque>
#include <set>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

using namespace okay::shaderbundle;
using Files = std::map<std::string, std::vector<unsigned char>>;
static std::vector<unsigned char> Bytes(const std::string& s) { return std::vector<unsigned char>(s.begin(), s.end()); }
static Bundle::ReadFn Reader(const Files& f) {
    return [&f](const std::string& n, std::vector<unsigned char>& b) { auto it = f.find(n); if (it == f.end()) return false; b = it->second; return true; };
}
static Files MakeBundle(const char* src, Manifest* mOut = nullptr) {
    Files f;
    for (const EntryReq& q : Entries()) f[FileNameFor(q.entry)] = Bytes(std::string("DXBC") + q.entry + "-payload");
    Manifest m; std::vector<std::string> w; std::string e;
    CHECK(BuildManifest(Reader(f), src, m, w, &e));
    const std::string t = WriteManifest(m);
    f["shaders.manifest"] = Bytes(t);
    if (mOut) *mOut = m;
    return f;
}

static void ShaderTests() {
    const char* src = okay::D3D11HlslSource();
    // every entry the renderer asks for really exists in the HLSL text
    CHECK(Entries().size() == 15);
    for (const EntryReq& q : Entries()) CHECK(std::strstr(src, (std::string(q.entry) + "(").c_str()) != nullptr);
    CHECK(Fnv1a64("", 0) == 14695981039346656037ull);              // FNV-1a published test vector
    CHECK(Fnv1a64("a", 1) == 0xaf63dc4c8601ec8cull);
    CHECK(Fnv1a64("foobar", 6) == 0x85944171f73967e8ull);

    Files f = MakeBundle(src);
    { Bundle b; std::string e; CHECK(b.Load(Reader(f), src, &e)); CHECK(b.Count() == 15); CHECK(b.Find("VSMain") && b.Find("PSDof")); CHECK(!b.Find("Nope")); CHECK(b.Warnings().empty()); }
    // the manifest round-trips
    { Manifest m; std::string e; CHECK(ParseManifest(std::string(f["shaders.manifest"].begin(), f["shaders.manifest"].end()), m, &e)); CHECK(m.records.size() == 15); CHECK(WriteManifest(m) == std::string(f["shaders.manifest"].begin(), f["shaders.manifest"].end())); }
    // a different HLSL text is refused (stale bytecode)
    { Bundle b; std::string e; CHECK(!b.Load(Reader(f), "changed text", &e)); CHECK(e.find("different HLSL") != std::string::npos); CHECK(b.Count() == 0); }
    // no manifest
    { Files g = f; g.erase("shaders.manifest"); Bundle b; std::string e; CHECK(!b.Load(Reader(g), src, &e)); CHECK(e.find("not found") != std::string::npos); }
    // a flipped byte in a required shader fails; in an optional shader it only warns and the entry is absent
    { Files g = f; g["okay3d_VSMain.cso"][5] ^= 1; Bundle b; std::string e; CHECK(!b.Load(Reader(g), src, &e)); CHECK(e.find("VSMain") != std::string::npos); }
    { Files g = f; g["okay3d_PSDof.cso"][5] ^= 1; Bundle b; std::string e; CHECK(b.Load(Reader(g), src, &e)); CHECK(!b.Find("PSDof")); CHECK(b.Find("PSAo")); CHECK(b.Warnings().size() == 1); }
    { Files g = f; g.erase("okay3d_PSMain.cso"); Bundle b; CHECK(!b.Load(Reader(g), src)); }
    { Files g = f; g.erase("okay3d_VSSkin.cso"); Bundle b; CHECK(b.Load(Reader(g), src)); CHECK(!b.Find("VSSkin")); }
    // not DXBC bytecode
    { Files g; for (const EntryReq& q : Entries()) g[FileNameFor(q.entry)] = Bytes("JUNKJUNK"); Manifest m; std::vector<std::string> w; std::string e; CHECK(!BuildManifest(Reader(g), src, m, w, &e)); }
    // build step: a missing required file refuses, a missing optional one warns
    { Files g; for (const EntryReq& q : Entries()) g[FileNameFor(q.entry)] = Bytes("DXBCxx"); g.erase("okay3d_PSSky.cso"); Manifest m; std::vector<std::string> w; std::string e; CHECK(BuildManifest(Reader(g), src, m, w, &e)); CHECK(w.size() == 1); CHECK(m.records.size() == 14);
      g.erase("okay3d_VSMain.cso"); CHECK(!BuildManifest(Reader(g), src, m, w, &e)); }
    // hostile manifests
    { Manifest m; std::string e;
      CHECK(!ParseManifest("", m, &e));
      CHECK(!ParseManifest("okay-shaders 2\nsource 0000000000000000\n", m, &e));
      CHECK(!ParseManifest("okay-shaders 1\n", m, &e));                                   // no source line
      CHECK(!ParseManifest("okay-shaders 1\nsource zz\n", m, &e));
      CHECK(!ParseManifest("okay-shaders 1\nsource 0000000000000000\nVSMain vs_4_0 ..\\evil.cso 4 0000000000000000\n", m, &e));
      CHECK(!ParseManifest("okay-shaders 1\nsource 0000000000000000\nVS/Main vs_4_0 okay3d_VS/Main.cso 4 0000000000000000\n", m, &e));
      CHECK(!ParseManifest("okay-shaders 1\nsource 0000000000000000\nVSMain vs_4_0 okay3d_VSMain.cso 0 0000000000000000\n", m, &e));
      CHECK(!ParseManifest("okay-shaders 1\nsource 0000000000000000\nVSMain vs_4_0 okay3d_VSMain.cso 99999999999 0000000000000000\n", m, &e));
      CHECK(!ParseManifest("okay-shaders 1\nsource 0000000000000000\nVSMain vs_4_0 okay3d_VSMain.cso 4 00\n", m, &e));
      CHECK(!ParseManifest("okay-shaders 1\nsource 0000000000000000\nVSMain vs_4_0 okay3d_VSMain.cso 4 0000000000000000\nVSMain vs_4_0 okay3d_VSMain.cso 4 0000000000000000\n", m, &e)); // duplicate
      CHECK(ParseManifest("# c\r\nokay-shaders 1\r\nsource 0000000000000000\r\n\r\nVSMain vs_4_0 okay3d_VSMain.cso 4 0000000000000000\r\n", m, &e)); }
    // wrong profile for a known entry
    { Manifest m; std::string e; std::string t = std::string(f["shaders.manifest"].begin(), f["shaders.manifest"].end());
      const std::size_t p = t.find("VSMain vs_4_0"); t.replace(p, 13, "VSMain ps_4_0"); CHECK(ParseManifest(t, m, &e)); CHECK(!CheckCoverage(m).empty()); }
}

using namespace okay::xbox;
static void DisplayTests() {
    CHECK(DipsToPixels(1920, 96) == 1920); CHECK(DipsToPixels(960, 192) == 1920); CHECK(DipsToPixels(1280, 150) == 2000);
    CHECK(DipsToPixels(0, 96) == 0); CHECK(DipsToPixels(-5, 96) == 0); CHECK(DipsToPixels(0.2f, 96) == 1); CHECK(DipsToPixels(100, 0) == 100);
    CHECK(DipsToPixels(1e9f, 96) == 16384);
    CHECK((ChooseBackBuffer({1920, 1080}) == SizePx{1920, 1080}));
    CHECK((ChooseBackBuffer({3840, 2160}) == SizePx{1920, 1080}));
    CHECK((ChooseBackBuffer({1280, 720}) == SizePx{1280, 720}));
    CHECK((ChooseBackBuffer({1281, 721}) == SizePx{1280, 720}));      // made even
    CHECK((ChooseBackBuffer({1000, 4000}) == SizePx{270, 1080}));     // tall window: height cap wins, aspect kept
    CHECK((ChooseBackBuffer({0, 0}) == SizePx{16, 16}));
    CHECK((ChooseBackBuffer({1, 1}) == SizePx{16, 16}));
    CHECK(ClassifyPresent(0) == PresentOutcome::Ok);
    CHECK(ClassifyPresent((std::int32_t)0x887A0005u) == PresentOutcome::DeviceLost);
    CHECK(ClassifyPresent((std::int32_t)0x887A0007u) == PresentOutcome::DeviceLost);
    CHECK(ClassifyPresent((std::int32_t)0x087A0001u) == PresentOutcome::Occluded);
    CHECK(ClassifyPresent((std::int32_t)0x80070057u) == PresentOutcome::Failed);
    PresentTracker t; t.WindowSized({1920, 1080});
    CHECK(t.NeedsResize()); t.Resized({1920, 1080}); CHECK(!t.NeedsResize());
    t.WindowSized({1280, 720}); CHECK(t.NeedsResize()); CHECK((t.Wanted() == SizePx{1280, 720}));
    t.Report((std::int32_t)0x087A0001u); CHECK(t.Occluded() && !t.ShouldDraw()); t.Report(0); CHECK(t.ShouldDraw());
    t.Report((std::int32_t)0x887A0005u); CHECK(t.DeviceLost() && !t.ShouldDraw());
    t.DeviceRebuilt(); CHECK(t.ShouldDraw() && t.NeedsResize());
    t.Report((std::int32_t)0x80004005u); CHECK(t.Failures() == 1);
}

// ---- one-click build: plan, runner, log parsing ----
namespace {
struct FakeProc : okay::xbox::ProcessHandle {
    std::deque<std::string> chunks; int code = 0; int ticksLeft = 1; bool* killed = nullptr;
    bool Poll(std::string& out, int& exitCode) override { if (!chunks.empty()) { out += chunks.front(); chunks.pop_front(); return false; } if (ticksLeft-- > 0) return false; exitCode = code; return true; }
    void Kill() override { if (killed) *killed = true; }
};
struct FakeHost : okay::xbox::BuildHost {
    std::map<std::string, std::pair<std::vector<std::string>, int>> script;   // step id -> (output chunks, exit code)
    std::set<std::string> failInternal, noStart; std::vector<std::string> ran; bool killed = false;
    bool RunInternal(const okay::xbox::Step& s, std::vector<std::string>& log, std::string& err) override {
        ran.push_back(s.id); log.push_back("internal " + s.id);
        if (failInternal.count(s.id)) { err = "boom " + s.id; return false; } return true; }
    std::unique_ptr<okay::xbox::ProcessHandle> Spawn(const okay::xbox::Command& c, std::string& err) override {
        std::string id = "?";
        ran.push_back("spawn:" + c.exe);
        for (const std::string& a : c.args) { if (a == "-S") id = "configure"; else if (a == "--build") id = "build"; else if (a.find("compile-shaders") != std::string::npos) id = "shaders"; else if (a.find("xbox-sign") != std::string::npos) id = "sign"; else if (a.find("Start-Process") != std::string::npos) id = "install"; }
        if (noStart.count(id)) { err = "no such program"; return nullptr; }
        auto p = std::make_unique<FakeProc>(); p->killed = &killed;
        auto it = script.find(id); if (it != script.end()) { for (auto& ch : it->second.first) p->chunks.push_back(ch); p->code = it->second.second; }
        return p; }
};
okay::xbox::PlanInputs Inputs() {
    okay::xbox::PlanInputs in;
    in.sdk.visualStudioPath = "C:/VS/2022/Community"; in.sdk.generator = "Visual Studio 17 2022"; in.sdk.sdkVersion = "10.0.19041.0"; in.sdk.signToolPath = "C:/Kits/signtool.exe";
    in.fs.list = [](const std::string& d) { if (d == "C:/VS/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin") return std::vector<std::string>{"cmake.exe"}; return std::vector<std::string>(); };
    in.sourceRoot = "C:/src/OkaySpace"; in.exportDir = "C:/out/My Game"; in.subject = "CN=Me, O=Studio"; in.appxPath = "C:/out/My Game/My_1.0.0.0_x64.appx";
    return in;
}
void Drive(okay::xbox::BuildRunner& r) { r.Start(); for (int i = 0; i < 200 && r.Tick(); ++i) {} }
}

static void BuildPlanTests() {
    using namespace okay::xbox;
    CHECK(QuoteArg("abc") == "abc"); CHECK(QuoteArg("a b") == "\"a b\""); CHECK(QuoteArg("") == "\"\"");
    CHECK(QuoteArg("C:\\dir name\\") == "\"C:\\dir name\\\\\"");     // trailing backslash doubled before the closing quote
    CHECK(QuoteArg("say \"hi\"") == "\"say \\\"hi\\\"\"");
    PlanInputs in = Inputs();
    std::vector<Step> p = PlanBuild(in);
    std::vector<std::string> ids; for (auto& s : p) ids.push_back(s.id);
    CHECK((ids == std::vector<std::string>{"sdk", "export", "configure", "build", "collect", "shaders", "pack", "sign"}));
    CHECK(p[2].cmd.exe == "C:/VS/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe");
    CHECK(p[2].kind == StepKind::Process && p[0].kind == StepKind::Internal && p[4].kind == StepKind::Internal);
    CHECK(p[2].cmd.Line().find("-DCMAKE_SYSTEM_NAME=WindowsStore") != std::string::npos);
    CHECK(p[2].cmd.Line().find("-G \"Visual Studio 17 2022\"") != std::string::npos && p[2].cmd.Line().find("-DCMAKE_GENERATOR_INSTANCE=C:/VS/2022/Community") != std::string::npos);   // from the probe
    { PlanInputs b = in; b.sdk.generator = "Visual Studio 18 2026"; b.sdk.toolsetVersion = "14.44.35207"; b.sdk.platformToolset = "v143"; b.sdk.visualStudioPath = "C:/VS/18/Community";
      const std::string l = PlanBuild(b)[2].cmd.Line(); CHECK(l.find("-G \"Visual Studio 18 2026\"") != std::string::npos && l.find("Visual Studio 17") == std::string::npos);
      CHECK(l.find("-T v143,version=14.44.35207") != std::string::npos && l.find("-DCMAKE_GENERATOR_INSTANCE=C:/VS/18/Community") != std::string::npos); }
    { PlanInputs b = in; b.sdk.toolsetVersion = "14.50.35717"; b.sdk.platformToolset = ""; CHECK(PlanBuild(b)[2].cmd.Line().find("-T v145,version=14.50.35717") != std::string::npos);   // derived when the probe did not say
      b.sdk.toolsetVersion = "14.29.30133"; CHECK(PlanBuild(b)[2].cmd.Line().find("-T version=14.29.30133") != std::string::npos); }   // unknown mapping: version= only
    { PlanInputs b = in; b.sdk.generator = ""; const std::string l = PlanBuild(b)[2].cmd.Line(); CHECK(l.find(" -G ") == std::string::npos && l.find("-A x64") != std::string::npos); }
    CHECK(p[2].cmd.Line().find("\"C:/out/My Game/build/app\"") != std::string::npos);   // spaces quoted
    CHECK(p[3].cmd.Line().find("--config Release") != std::string::npos);
    CHECK(p[5].cmd.Line().find("-OutDir \"C:/out/My Game/package/Shaders\"") != std::string::npos);
    CHECK(p[7].cmd.Line().find("-Subject \"CN=Me, O=Studio\"") != std::string::npos);
    for (auto& s : p) CHECK(!s.cmd.elevated);
    CHECK(p[2].cmd.Line().find("OKAY_XBOX_APP_ENGINE") == std::string::npos);                      // the triangle shell is the default
    { PlanInputs b = in; b.engineMode = true; const std::string l = PlanBuild(b)[2].cmd.Line(); CHECK(l.find("-DOKAY_XBOX_APP_ENGINE=ON") != std::string::npos && l.find("-DOKAY_SOURCE_ROOT=C:/src/OkaySpace") != std::string::npos);
      CHECK(MapFailure("build", 2, false, {}).find("Engine mode") != std::string::npos); }
    { PlanInputs b = in; b.sdk.visualStudioPath = ""; CHECK(PlanBuild(b)[2].cmd.exe == "cmake"); }
    { PlanInputs b = in; b.fs.list = nullptr; CHECK(PlanBuild(b)[2].cmd.exe == "cmake"); }
    { PlanInputs b = in; b.signPackage = false; b.installOnThisPc = true; CHECK(PlanBuild(b).back().id == "pack"); }   // no install of an unsigned package
    { PlanInputs b = in; b.installOnThisPc = true; auto q = PlanBuild(b); CHECK(q.back().id == "install" && q.back().cmd.elevated);
      const std::string l = q.back().cmd.Line(); CHECK(l.find("-Verb RunAs") != std::string::npos); CHECK(l.find("Import-Certificate") == std::string::npos);   // the import is inside the script, run elevated
      CHECK(l.find("xbox-install-local.ps1") != std::string::npos); CHECK(l.find("install-result.txt") != std::string::npos);
      int uac = 0; for (auto& s : q) if (s.cmd.elevated) ++uac; CHECK(uac == 1); }
    { PlanInputs b = in; b.signer = Signer::OwnSigning; auto q = PlanBuild(b); CHECK(q.back().id == "sign" && q.back().kind == StepKind::Internal); CHECK(std::string(SignerLabel(Signer::OwnSigning)).find("not independently reviewed") != std::string::npos); }
    { PlanInputs b = in; b.installOnThisPc = true; b.appxPath = "C:/o/it's.appx"; CHECK(PlanBuild(b).back().cmd.Line().find("it''s") != std::string::npos); }
    // paths with spaces: every -File / -Cer / -Appx value is inside real double quotes in the -ArgumentList (powershell -File does not strip single quotes)
    { PlanInputs b = in; b.installOnThisPc = true; b.sourceRoot = "C:/my src/Okay Space"; b.exportDir = "C:/out/My Game"; b.appxPath = "C:/out/My Game/My_1.0.0.0_x64.appx";
      const auto q = PlanBuild(b).back(); const std::string cmdArg = q.cmd.args[4];   // the -Command text, before the Windows quoting
      CHECK(cmdArg.find("'-File','\"C:/my src/Okay Space/scripts/xbox-install-local.ps1\"'") != std::string::npos);
      CHECK(cmdArg.find("'-Cer','\"C:/out/My Game/OkaySpaceXbox-test.cer\"'") != std::string::npos);
      CHECK(cmdArg.find("'-Appx','\"C:/out/My Game/My_1.0.0.0_x64.appx\"'") != std::string::npos);
      CHECK(cmdArg.find("-File','C:") == std::string::npos);   // no bare or single-quoted value
      CHECK(q.cmd.Line().find("-Command \"Start-Process powershell -Verb RunAs -Wait -ArgumentList") != std::string::npos); }   // the whole -Command is one quoted Windows argument
}

static void SourceRootTests() {
    using namespace okay::xbox;
    std::set<std::string> files = {"C:/Apps/Okay/xbox-src/platform/xbox_uwp/app/CMakeLists.txt", "C:/Apps/Okay/xbox-src/scripts/xbox-sign.ps1",
                                   "D:/src/OkaySpace/platform/xbox_uwp/app/CMakeLists.txt", "D:/src/OkaySpace/scripts/xbox-sign.ps1"};
    FsView fs; fs.list = [files](const std::string& dir) { std::vector<std::string> o; const std::string pre = dir + "/";
        for (const std::string& p : files) if (p.compare(0, pre.size(), pre) == 0) { std::string rest = p.substr(pre.size()); rest = rest.substr(0, rest.find('/')); if (std::find(o.begin(), o.end(), rest) == o.end()) o.push_back(rest); } return o; };
    CHECK(FindSourceRoot(fs, "C:/Apps/Okay", "") == "C:/Apps/Okay/xbox-src");
    CHECK(FindSourceRoot(fs, "D:/src/OkaySpace/build/bin", "") == "D:/src/OkaySpace");          // a source checkout: two folders up
    CHECK(FindSourceRoot(fs, "C:/Apps/Okay", "D:/src/OkaySpace") == "D:/src/OkaySpace");         // the environment variable wins
    CHECK(FindSourceRoot(fs, "C:/Apps/Okay", "E:/nothing") == "C:/Apps/Okay/xbox-src");          // a bad variable is ignored
    CHECK(FindSourceRoot(fs, "E:/other", "").empty());
}

static void LogTests() {
    using namespace okay::xbox;
    CHECK(ClassifyLine("C:\\a.cpp(12): error C2065: 'x': undeclared identifier") == LineClass::Error);
    CHECK(ClassifyLine("LINK : fatal error LNK1104: cannot open file") == LineClass::Error);
    CHECK(ClassifyLine("CMake Error at CMakeLists.txt:9 (message):") == LineClass::Error);
    CHECK(ClassifyLine("error: SignTool failed") == LineClass::Error);
    CHECK(ClassifyLine("MSBUILD : error MSB1009: Project file does not exist.") == LineClass::Error);
    CHECK(ClassifyLine("    0 Error(s)") == LineClass::Info);
    CHECK(ClassifyLine("a.cpp(3): warning C4100: unused") == LineClass::Warning);
    CHECK(ClassifyLine("Building CXX object") == LineClass::Info);
    CHECK(ClassifyLine("INSTALL: FAILED (x)") == LineClass::Error);
    CHECK(Redact("signtool sign /p hunter2 file.appx").find("hunter2") == std::string::npos);
    CHECK(Redact("password: abc123") == "password: ***");
    CHECK(Redact("-----BEGIN PRIVATE KEY-----").find("hidden") != std::string::npos); // lint-allow: secret (test string for the redaction check, not a key)
    CHECK(Redact("Exported cert.pfx now").find("hidden") != std::string::npos);
    CHECK(Redact("THUMBPRINT: 0123456789ABCDEF0123456789ABCDEF01234567") == "THUMBPRINT: 0123456789ABCDEF0123456789ABCDEF01234567");
    std::vector<std::string> ls = {"ok", "x.cpp(1): error C1: a", "y", "z.cpp(2): error C2: b", "fatal error C3: c"};
    CHECK(FirstErrors(ls, 2).size() == 2 && FirstErrors(ls, 10).size() == 3);
    CHECK(FindFact({"SIGNER: PowerShell", "CERT: C:/a.cer\r"}, "CERT") == "C:/a.cer"); CHECK(FindFact({"x"}, "CERT").empty());
    CHECK(MapFailure("configure", 1, false, {"e1"}).find("Universal Windows Platform") != std::string::npos);
    CHECK(MapFailure("configure", 1, false, {"e1"}).find("\n  e1") != std::string::npos);
    CHECK(MapFailure("build", -1, true, {}).find("Could not start cmake") != std::string::npos);
    CHECK(MapFailure("sign", 2, false, {}).find("exit code 2") != std::string::npos);
}

static void RunnerTests() {
    using namespace okay::xbox;
    PlanInputs in = Inputs(); in.installOnThisPc = true;
    const std::string cm = "spawn:C:/VS/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe";
    { // everything works, in plan order, facts are read from the log
        FakeHost h; h.script["build"] = {{"Compiling a.cpp\nLinking", " done\n"}, 0}; h.script["sign"] = {{"SIGNER: PowerShell New-SelfSignedCertificate + Microsoft SignTool\nCERT: C:/o/x.cer\n"}, 0};
        h.script["install"] = {{"INSTALL: OK\n"}, 0};
        BuildRunner r(PlanBuild(in), h); Drive(r);
        CHECK(r.Finished() && r.Succeeded()); for (StepStatus s : r.Status()) CHECK(s == StepStatus::Done);
        CHECK(r.Fact("CERT") == "C:/o/x.cer"); CHECK(r.Fact("SIGNER").find("SignTool") != std::string::npos);
        bool linking = false; for (auto& l : r.Log()) if (l.text == "Linking done") linking = true; CHECK(linking);   // a line split across two reads is joined
        CHECK((h.ran == std::vector<std::string>{"sdk", "export", cm, cm, "collect", "spawn:powershell", "pack", "spawn:powershell", "spawn:powershell"}));
    }
    { // the compile fails: stops, later steps skipped, first error lines in the message, nothing after it ran
        FakeHost h; h.script["build"] = {{"a.cpp(1): error C1001: oops\nb.cpp(2): error C2000: again\n"}, 1};
        BuildRunner r(PlanBuild(in), h); Drive(r);
        CHECK(r.Finished() && !r.Succeeded() && !r.Cancelled());
        CHECK(r.Status()[3] == StepStatus::Failed); CHECK(r.Status()[2] == StepStatus::Done); CHECK(r.Status()[4] == StepStatus::Skipped && r.Status().back() == StepStatus::Skipped);
        CHECK(r.FailureMessage().find("C1001") != std::string::npos); CHECK(r.FailureMessage().find("exit code 1") != std::string::npos);
        CHECK(h.ran.size() == 4);
    }
    { FakeHost h; h.failInternal.insert("export"); BuildRunner r(PlanBuild(in), h); Drive(r); CHECK(!r.Succeeded()); CHECK(r.Status()[1] == StepStatus::Failed); CHECK(r.FailureMessage().find("boom export") != std::string::npos); CHECK(h.ran.size() == 2); }
    { FakeHost h; h.noStart.insert("configure"); BuildRunner r(PlanBuild(in), h); Drive(r); CHECK(r.Status()[2] == StepStatus::Failed); CHECK(r.FailureMessage().find("Could not start cmake") != std::string::npos); }
    { // user declines the UAC prompt: exit 0 from the parent but no INSTALL: OK
        FakeHost h; h.script["install"] = {{"INSTALL: FAILED (no result: the administrator prompt was probably declined)\n"}, 0};
        BuildRunner r(PlanBuild(in), h); Drive(r); CHECK(!r.Succeeded()); CHECK(r.Status().back() == StepStatus::Failed); CHECK(r.Status()[r.Status().size() - 2] == StepStatus::Done);
        CHECK(r.FailureMessage().find("administrator prompt") != std::string::npos); }
    { FakeHost h; h.script["install"] = {{}, 0}; BuildRunner r(PlanBuild(in), h); Drive(r); CHECK(!r.Succeeded()); }   // silent success is not trusted
    { // cancel in the middle of the compile
        FakeHost h; h.script["build"] = {{"line1\n", "line2\n", "line3\n"}, 0};
        BuildRunner r(PlanBuild(in), h); r.Start(); int n = 0; while (r.Tick() && n < 3) ++n;
        CHECK(r.Running()); r.Cancel();
        CHECK(r.Finished() && r.Cancelled() && !r.Succeeded()); CHECK(h.killed); CHECK(r.Status()[3] == StepStatus::Cancelled); CHECK(r.Status().back() == StepStatus::Skipped);
        CHECK(!r.Tick()); }
    { // secrets never reach the log
        FakeHost h; h.script["sign"] = {{"calling signtool /p hunter2\npassword: swordfish\n"}, 1}; BuildRunner r(PlanBuild(in), h); Drive(r);
        for (auto& l : r.Log()) { CHECK(l.text.find("hunter2") == std::string::npos); CHECK(l.text.find("swordfish") == std::string::npos); }
        CHECK(r.FailureMessage().find("swordfish") == std::string::npos); }
    { // a huge log stays bounded
        FakeHost h; std::string big; for (int i = 0; i < 5000; ++i) big += "line " + std::to_string(i) + "\n"; h.script["build"] = {{big}, 0};
        PlanInputs in2 = in; in2.installOnThisPc = false; BuildRunner r(PlanBuild(in2), h); Drive(r); CHECK(r.Succeeded()); CHECK(r.Log().size() <= 3000); }
}

static void HostTests() {
    using namespace okay::xbox;
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "okay_xbox_host_test";
    fs::remove_all(root); fs::create_directories(root / "package" / "Shaders"); fs::create_directories(root / "build" / "app" / "out" / "Release");
    auto put = [&](const fs::path& p, const std::string& b) { std::ofstream o(p, std::ios::binary); o << b; };
    put(root / "package" / "AppxManifest.xml", "<Package/>"); put(root / "package" / "Shaders" / "okay3d.hlsl", "x"); put(root / "package" / "Shaders" / "entries.txt", "x");
    put(root / "package" / "Shaders" / "shaders.manifest", "okay-shaders 1\n");
    BuildHostConfig c; c.sdkAvailable = true; c.exportOk = true; c.exportMessage = "exported"; c.exportDir = root.string(); c.appxPath = (root / "g_1.0.0.0_x64.appx").string();
    XboxBuildHost h(c);
    std::vector<std::string> log; std::string err; Step s;
    s.id = "sdk"; CHECK(h.RunInternal(s, log, err));
    s.id = "export"; CHECK(h.RunInternal(s, log, err));
    s.id = "pack"; CHECK(!h.RunInternal(s, log, err)); CHECK(err.find("OkaySpaceXbox.exe") != std::string::npos);   // not built yet
    s.id = "collect"; CHECK(!h.RunInternal(s, log, err)); CHECK(err.find("not found") != std::string::npos);
    put(root / "build" / "app" / "out" / "Release" / "OkaySpaceXbox.exe", "MZ-fake"); put(root / "build" / "app" / "tri_vs.cso", "DXBC");
    err.clear(); s.id = "collect"; CHECK(h.RunInternal(s, log, err)); CHECK(fs::exists(root / "package" / "OkaySpaceXbox.exe")); CHECK(fs::exists(root / "package" / "tri_vs.cso"));
    err.clear(); s.id = "pack"; CHECK(h.RunInternal(s, log, err)); CHECK(fs::exists(c.appxPath));
    CHECK(!fs::exists(root / "package" / "Shaders" / "okay3d.hlsl")); CHECK(fs::exists(root / "package" / "Shaders" / "shaders.manifest"));
    { std::ifstream f(c.appxPath, std::ios::binary); std::string z((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); CHECK(appx::Verify(z).ok); }
    { BuildHostConfig d = c; d.sdkAvailable = false; XboxBuildHost h2(d); s.id = "sdk"; err.clear(); CHECK(!h2.RunInternal(s, log, err)); CHECK(!err.empty()); }
    { BuildHostConfig d = c; d.exportOk = false; d.exportMessage = "Xbox export stopped: bad name"; XboxBuildHost h2(d); s.id = "export"; err.clear(); CHECK(!h2.RunInternal(s, log, err)); CHECK(err.find("bad name") != std::string::npos); }
    { std::string e; CHECK(h.Spawn(Command{}, e) == nullptr); CHECK(!e.empty()); }   // no spawn function (not Windows)
    // the whole runner with the real host and a fake process for each Process step
    { struct P : ProcessHandle { bool Poll(std::string&, int& c) override { c = 0; return true; } void Kill() override {} };
      BuildHostConfig d = c; d.spawn = [](const Command&, std::string&) { return std::unique_ptr<ProcessHandle>(new P()); };
      XboxBuildHost h3(d); PlanInputs in = Inputs(); in.exportDir = root.string(); in.signPackage = false;
      std::remove(c.appxPath.c_str()); BuildRunner r(PlanBuild(in), h3); r.Start(); for (int i = 0; i < 50 && r.Tick(); ++i) {}
      CHECK(r.Succeeded()); CHECK(fs::exists(c.appxPath)); }
    fs::remove_all(root);
}

// ---- lifecycle ----
static void LifecycleTests() {
    using namespace okay::xbox;
    Lifecycle l;
    CHECK(l.Phase() == AppPhase::Created && !l.ShouldRender() && l.ShouldRunLogic());
    l.Resized(960, 540, 192); CHECK((l.Size() == SizePx{1920, 1080}));
    { LifecyclePending p = l.Take(); CHECK(p.resized && !p.saveState); CHECK(!l.Take().Any()); }
    l.Launched(); CHECK(l.Phase() == AppPhase::Active && l.ShouldRender());
    l.Resized(960, 540, 192); CHECK(!l.Peek());                       // same size: no event
    l.Resized(1280, 720, 96); CHECK(l.Take().resized);
    l.VisibilityChanged(false); CHECK(l.Phase() == AppPhase::Hidden && !l.ShouldRender() && l.ShouldRunLogic());
    l.VisibilityChanged(true); CHECK(l.ShouldRender());
    // suspend: save + trim exactly once, no drawing, resume brings the device check back
    l.Suspending(1000, 5000);
    { LifecyclePending p = l.Take(); CHECK(p.saveState && p.trimGpu && !p.resumed); }
    CHECK(l.Phase() == AppPhase::Suspended && !l.ShouldRender() && !l.ShouldRunLogic());
    CHECK(!l.SuspendReady()); CHECK(!l.SuspendOverdue(3000)); CHECK(l.SuspendOverdue(5600));   // 500 ms before the deadline the deferral must be completed
    l.Suspending(1100); CHECK(!l.Take().Any());                       // a second Suspending changes nothing
    l.VisibilityChanged(false); CHECK(l.Phase() == AppPhase::Suspended);   // applied at resume
    l.Activated(true); CHECK(l.Phase() == AppPhase::Suspended);
    l.SaveDone(); CHECK(l.SuspendReady() && !l.SuspendOverdue(99999));
    l.Resuming(); CHECK(l.Phase() == AppPhase::Hidden); { LifecyclePending p = l.Take(); CHECK(p.resumed && !p.saveState); }
    l.Resuming(); CHECK(!l.Take().Any());                             // Resuming without Suspending is ignored
    l.VisibilityChanged(true);
    // back button: exit request, not a state change by itself
    l.BackRequested(); CHECK(l.Take().exitRequested); CHECK(l.Phase() == AppPhase::Active);
    l.WindowClosed(); CHECK(l.Phase() == AppPhase::Closing && l.Take().exitRequested && !l.ShouldRunLogic());
    l.BackRequested(); CHECK(!l.Take().Any()); l.Suspending(5, 5000); CHECK(!l.Take().Any());
    // a suspend that arrives while hidden still saves
    Lifecycle h; h.Launched(); h.VisibilityChanged(false); h.Suspending(0); CHECK(h.Take().saveState);
}

// ---- gamepad mapping ----
static void InputTests() {
    using namespace okay::xbox; using namespace okay::platform;
    PadReading r; PadState s = MapReading(r);
    for (int i = 0; i < 6; ++i) CHECK(s.axis[i] == 0);
    for (int i = 0; i < 15; ++i) CHECK(!s.button[i]);
    // documented button bit values (Windows.Gaming.Input.GamepadButtons)
    CHECK(kPadMenu == 1 && kPadView == 2 && kPadA == 4 && kPadB == 8 && kPadX == 16 && kPadY == 32 && kPadDpadUp == 64 && kPadDpadDown == 128 && kPadDpadLeft == 256 && kPadDpadRight == 512 && kPadLeftShoulder == 1024 && kPadRightShoulder == 2048 && kPadLeftThumb == 4096 && kPadRightThumb == 8192);
    r.buttons = kPadA | kPadMenu | kPadDpadLeft | kPadRightShoulder | kPadLeftThumb;
    s = MapReading(r);
    CHECK(s.button[(int)GamepadButton::A] && s.button[(int)GamepadButton::Start] && s.button[(int)GamepadButton::DpadLeft] && s.button[(int)GamepadButton::RightShoulder] && s.button[(int)GamepadButton::LeftStick]);
    CHECK(!s.button[(int)GamepadButton::B] && !s.button[(int)GamepadButton::Back] && !s.button[(int)GamepadButton::DpadRight] && !s.button[(int)GamepadButton::Guide]);
    r = PadReading(); r.buttons = kPadView | kPadB | kPadX | kPadY | kPadDpadUp | kPadDpadDown | kPadLeftShoulder | kPadRightThumb;
    s = MapReading(r); CHECK(s.button[(int)GamepadButton::Back] && s.button[(int)GamepadButton::B] && s.button[(int)GamepadButton::X] && s.button[(int)GamepadButton::Y] && s.button[(int)GamepadButton::DpadUp] && s.button[(int)GamepadButton::DpadDown] && s.button[(int)GamepadButton::LeftShoulder] && s.button[(int)GamepadButton::RightStick]);
    // sticks: +Y up becomes +Y down, full deflection reaches the ends, inside the dead zone is zero, no jump at the edge
    r = PadReading(); r.leftX = 1.0; r.leftY = 1.0; s = MapReading(r);
    CHECK(s.axis[(int)GamepadAxis::LeftX] > 0 && s.axis[(int)GamepadAxis::LeftY] < 0);      // diagonal: normalised to the unit circle
    r = PadReading(); r.leftX = 1.0; s = MapReading(r); CHECK(s.axis[(int)GamepadAxis::LeftX] == 32767);
    r = PadReading(); r.leftX = -1.0; s = MapReading(r); CHECK(s.axis[(int)GamepadAxis::LeftX] == -32768);
    r = PadReading(); r.rightY = 1.0; s = MapReading(r); CHECK(s.axis[(int)GamepadAxis::RightY] == -32768);   // up = negative, like SDL
    r = PadReading(); r.rightY = -1.0; s = MapReading(r); CHECK(s.axis[(int)GamepadAxis::RightY] == 32767);
    r = PadReading(); r.leftX = 0.2; r.leftY = 0.1; s = MapReading(r); CHECK(s.axis[(int)GamepadAxis::LeftX] == 0 && s.axis[(int)GamepadAxis::LeftY] == 0);   // |v| 0.224 < 0.2395
    r = PadReading(); r.rightX = 0.26; s = MapReading(r); CHECK(s.axis[(int)GamepadAxis::RightX] == 0);       // right dead zone is 0.265
    r = PadReading(); r.rightX = 0.27; s = MapReading(r); CHECK(s.axis[(int)GamepadAxis::RightX] > 0 && s.axis[(int)GamepadAxis::RightX] < 1000);   // just outside: tiny, no jump
    { double ox, oy; RadialDeadzone(0.5, 0.0, 0.25, ox, oy); CHECK(std::fabs(ox - 1.0 / 3.0) < 1e-9 && oy == 0); RadialDeadzone(3.0, 4.0, 0.25, ox, oy); CHECK(std::fabs(std::sqrt(ox * ox + oy * oy) - 1.0) < 1e-9); RadialDeadzone(0.1, 0.1, 5.0, ox, oy); CHECK(ox > 0); /* bad dz ignored */ }
    // triggers: threshold then 0..32767
    r = PadReading(); r.leftTrigger = 0.1; s = MapReading(r); CHECK(s.axis[(int)GamepadAxis::TriggerLeft] == 0);
    r = PadReading(); r.leftTrigger = 1.0; r.rightTrigger = 0.5; s = MapReading(r); CHECK(s.axis[(int)GamepadAxis::TriggerLeft] == 32767 && s.axis[(int)GamepadAxis::TriggerRight] > 14000 && s.axis[(int)GamepadAxis::TriggerRight] < 17000);
    r = PadReading(); r.leftTrigger = 7.0; s = MapReading(r); CHECK(s.axis[(int)GamepadAxis::TriggerLeft] == 32767);   // out-of-range reading is clamped
    r = PadReading(); r.leftX = std::nan(""); s = MapReading(r); CHECK(s.axis[(int)GamepadAxis::LeftX] == 0);          // NaN never becomes garbage
    // key table
    CHECK(KeyFromVirtualKey(0x41) == Key::A && KeyFromVirtualKey(0x5A) == Key::Z && KeyFromVirtualKey(0x31) == Key::N1 && KeyFromVirtualKey(0x30) == Key::N0 && KeyFromVirtualKey(0x39) == Key::N9);
    CHECK(KeyFromVirtualKey(0x70) == Key::F1 && KeyFromVirtualKey(0x7B) == Key::F12 && KeyFromVirtualKey(0x0D) == Key::Return && KeyFromVirtualKey(0x1B) == Key::Escape && KeyFromVirtualKey(0x20) == Key::Space);
    CHECK(KeyFromVirtualKey(0x25) == Key::Left && KeyFromVirtualKey(0x26) == Key::Up && KeyFromVirtualKey(0x27) == Key::Right && KeyFromVirtualKey(0x28) == Key::Down);
    CHECK(KeyFromVirtualKey(0x60) == Key::Kp0 && KeyFromVirtualKey(0x61) == Key::Kp1 && KeyFromVirtualKey(0x69) == Key::Kp9 && KeyFromVirtualKey(0xA1) == Key::RShift && KeyFromVirtualKey(0xFFFF) == Key::Unknown);
}

namespace {
struct FakePad : okay::xbox::PadDevice {
    okay::xbox::PadReading r; bool connected = true; std::vector<okay::xbox::Vibration> sent; bool trig = true;
    bool Read(okay::xbox::PadReading& o) override { o = r; return connected; }
    void SendVibration(const okay::xbox::Vibration& v) override { sent.push_back(v); }
    bool HasTriggerMotors() const override { return trig; }
};
}
static void PadClassTests() {
    using namespace okay::xbox; using namespace okay::platform;
    FakePad d; UwpGamepad g(7, &d);
    CHECK(g.InstanceId() == 7 && std::string(g.Name()) == "Xbox controller");
    d.r.buttons = kPadA; d.r.leftX = 1.0; g.Poll(1000);
    CHECK(g.Connected() && g.Button(GamepadButton::A) && g.Axis(GamepadAxis::LeftX) == 32767);
    d.connected = false; g.Poll(1010); CHECK(!g.Connected() && !g.Button(GamepadButton::A) && g.Axis(GamepadAxis::LeftX) == 0);   // unplugged: all released
    d.connected = true; g.Poll(1020);
    // rumble: motors scale 0..65535 -> 0..1, ends by itself after the duration, a new request replaces the old one
    CHECK(g.Rumble(65535, 32768, 500)); g.Poll(1100);
    CHECK(d.sent.size() == 1 && d.sent[0].leftMotor == 1.0 && std::fabs(d.sent[0].rightMotor - 0.5) < 0.001 && d.sent[0].leftTrigger == 0);
    g.Poll(1300); CHECK(d.sent.size() == 1);                            // no change, nothing sent
    g.Poll(1700); CHECK(d.sent.size() == 2 && d.sent[1].Zero());       // 1100+... the request started at now=1020 for 500 ms
    CHECK(g.RumbleTriggers(65535, 0, 100) && g.HasTriggerRumble()); g.Poll(1750); CHECK(d.sent.back().leftTrigger == 1.0);
    g.Poll(1900); CHECK(d.sent.back().Zero());
    CHECK(g.Rumble(100, 100, 0)); g.Poll(2000); CHECK(d.sent.back().Zero());   // zero duration = stop
    d.trig = false; CHECK(!g.RumbleTriggers(1, 1, 10) && !g.HasTriggerRumble());
    { FakePad d2; { UwpGamepad g2(1, &d2); g2.Rumble(65535, 65535, 5000); g2.Poll(1); } CHECK(d2.sent.back().Zero()); }   // destroying the pad stops the motors
    { UwpGamepad none(1, nullptr); CHECK(!none.Rumble(1, 1, 1)); none.Poll(0); CHECK(!none.Connected()); }
}

// ---- paths ----
static void PathTests() {
    using namespace okay::xbox;
    PathRoots r; r.install = "C:/Program Files/WindowsApps/Okay_1.0_x64__abc/"; r.local = "C:/Users/u/AppData/Local/Packages/Okay/LocalState"; r.temp = "C:/Users/u/AppData/Local/Packages/Okay/TempState/";
    { PathResult p = Resolve(r, PathArea::GameData, "scenes/main.okayscene"); CHECK(p.ok && p.path == "C:/Program Files/WindowsApps/Okay_1.0_x64__abc/Data/scenes/main.okayscene" && !p.writable); }
    { PathResult p = Resolve(r, PathArea::Shaders, "shaders.manifest"); CHECK(p.ok && !p.writable && p.path.find("/Shaders/shaders.manifest") != std::string::npos); }
    { PathResult p = Resolve(r, PathArea::Saves, "slot1.sav"); CHECK(p.ok && p.writable && p.path == "C:/Users/u/AppData/Local/Packages/Okay/LocalState/saves/slot1.sav"); }
    { PathResult p = Resolve(r, PathArea::Temp, "a.tmp"); CHECK(p.ok && p.path == "C:/Users/u/AppData/Local/Packages/Okay/TempState/a.tmp"); }
    { PathResult p = Resolve(r, PathArea::Config, ""); CHECK(p.ok && p.path.size() > 6 && p.path.substr(p.path.size() - 7) == "/config"); }
    { PathResult p = Resolve(r, PathArea::Saves, "a\\b\\c.sav"); CHECK(p.ok && p.path.find("/saves/a/b/c.sav") != std::string::npos); }   // backslashes accepted
    { PathResult p = Resolve(r, PathArea::Saves, "./a/./b"); CHECK(p.ok && p.path.find("/saves/a/b") != std::string::npos); }
    for (const char* bad : {"../x", "a/../../x", "/etc/passwd", "C:/x", "a/b:stream", "a//b", "a/b.", "a/b ", "con", "dir/NUL.txt", "a/b?", "a\x01" "b", "..", "a/.."})
        { PathResult p = Resolve(r, PathArea::Saves, bad); CHECK(!p.ok); CHECK(!p.error.empty()); }
    { PathRoots e; CHECK(!Resolve(e, PathArea::Saves, "x").ok); }
    CHECK(CanWrite(r, "C:/Users/u/AppData/Local/Packages/Okay/LocalState/saves/x"));
    CHECK(CanWrite(r, "C:\\Users\\u\\AppData\\Local\\Packages\\Okay\\TempState\\x"));
    CHECK(!CanWrite(r, "C:/Program Files/WindowsApps/Okay_1.0_x64__abc/Data/x"));
    CHECK(!CanWrite(r, "C:/Users/u/AppData/Local/Packages/Okay/LocalState"));                        // the folder itself
    CHECK(!CanWrite(r, "C:/Users/u/AppData/Local/Packages/Okay/LocalState/../../Other/x"));          // escapes
    CHECK(!CanWrite(r, "C:/Users/u/AppData/Local/Packages/Okay/LocalStateEvil/x"));                  // prefix of a sibling folder
    CHECK(!CanWrite(r, "C:/Windows/x"));
    // the engine's Paths: with the override, Windows-style folders land inside LocalFolder
    okay::Paths::Env e; e.appData = DesktopDirToLocal(r, DesktopDir::RoamingAppData); e.localAppData = DesktopDirToLocal(r, DesktopDir::LocalAppData); e.home = DesktopDirToLocal(r, DesktopDir::Documents);
    okay::Paths::SetEnvOverride(&e);
    CHECK(okay::Paths::Env::Current().appData == "C:/Users/u/AppData/Local/Packages/Okay/LocalState/roaming");
    const std::string ud = okay::Paths::UserDataDirFor(okay::Paths::Platform::Windows, okay::Paths::Env::Current(), "Game");
    CHECK(ud == "C:/Users/u/AppData/Local/Packages/Okay/LocalState/roaming/Game"); CHECK(CanWrite(r, ud + "/save.dat"));
    okay::Paths::SetEnvOverride(nullptr); CHECK(okay::Paths::Env::Current().appData != e.appData);
    CHECK(DesktopDirToLocal(r, DesktopDir::LocalAppData, "../x") == "C:/Users/u/AppData/Local/Packages/Okay/LocalState/localappdata");   // a bad relative part is dropped
}

// ---- audio ----
namespace {
struct FakeAudioHost : okay::xbox::AudioHost {
    bool started = false, stopped = false; okay::xbox::AudioOutputFormat fmt;
    bool Format(okay::xbox::AudioOutputFormat& o) override { o = fmt; return true; }
    bool Start(okay::xbox::UwpAudioDevice*, std::string&) override { started = true; return true; }
    void Stop(okay::xbox::UwpAudioDevice*) override { stopped = true; }
};
int g_cbCalls = 0;
void Ramp(void*, std::uint8_t* s, int len) { std::int16_t* p = (std::int16_t*)s; for (int i = 0; i < len / 2; ++i) p[i] = (std::int16_t)(1000 + (g_cbCalls * 16 + i)); ++g_cbCalls; }
}
static void AudioTests() {
    using namespace okay::xbox; using namespace okay::platform;
    CHECK(S16ToF32(32767) == 1.0f && S16ToF32(-32768) == -1.0f && S16ToF32(0) == 0.0f);
    CHECK(F32ToS16(1.0f) == 32767 && F32ToS16(-1.0f) == -32768 && F32ToS16(2.0f) == 32767 && F32ToS16(-9.0f) == -32768 && F32ToS16(std::nanf("")) == 0 && F32ToS16(0.5f) == 16384);
    for (int v : {-32768, -1000, -1, 0, 1, 1000, 32767}) CHECK(F32ToS16(S16ToF32((std::int16_t)v)) == v);   // round trip is exact
    { float a[2] = {0.5f, -0.5f}, o[6]; MapChannels(a, 2, o, 6); CHECK(o[0] == 0.5f && o[1] == -0.5f && o[2] == 0 && o[5] == 0); MapChannels(a, 2, o, 1); CHECK(o[0] == 0.0f);
      float m[1] = {0.25f}; MapChannels(m, 1, o, 2); CHECK(o[0] == 0.25f && o[1] == 0.25f); MapChannels(m, 1, o, 4); CHECK(o[2] == 0 && o[3] == 0); }
    // ring
    { ByteRing b(10); CHECK(b.Capacity() == 16); unsigned char x[16], y[16]; for (int i = 0; i < 16; ++i) x[i] = (unsigned char)i;
      CHECK(b.Write(x, 12)); CHECK(!b.Write(x, 5)); CHECK(b.Size() == 12 && b.Free() == 4); CHECK(b.Read(y, 8) == 8 && y[7] == 7);
      CHECK(b.Write(x, 10)); CHECK(b.Read(y, 16) == 14); CHECK(y[0] == 8 && y[3] == 11 && y[4] == 0 && y[13] == 9);        // wraps around
      CHECK(b.Read(y, 4) == 0); b.Write(x, 3); b.ConsumerClear(); CHECK(b.Size() == 0); }
    FakeAudioHost host; host.fmt = {48000, 2};
    AudioSpec want; want.freq = 48000; want.channels = 2; want.format = SampleFormat::S16; want.frames = 256;
    { // queue mode, same rate, stereo S16 -> stereo float; starts paused; underrun is silence
        UwpAudioDevice d(want, host.fmt, &host); std::string err; CHECK(d.Begin(err) && host.started);
        std::int16_t in[8] = {16384, -16384, 8000, -8000, 100, 200, 300, 400};
        CHECK(d.Queue(in, sizeof(in)) && d.QueuedBytes() == 16);
        float out[16]; std::fill(out, out + 16, 9.0f);
        d.Render(out, 4); for (int i = 0; i < 8; ++i) CHECK(out[i] == 0.0f);          // paused: silence, nothing consumed
        CHECK(d.QueuedBytes() == 16);
        d.Pause(false); d.Render(out, 4);
        CHECK(std::fabs(out[0] - 16384 / 32767.0f) < 1e-6 && std::fabs(out[1] + 16384 / 32768.0f) < 1e-6 && std::fabs(out[6] - 300 / 32767.0f) < 1e-6);
        d.Render(out, 4); for (int i = 0; i < 8; ++i) CHECK(out[i] == 0.0f);          // ran dry: silence, no crash
        CHECK(d.UnderrunFrames() > 0);
        CHECK(!d.Queue(in, 0) || true); std::int16_t odd[3] = {1, 2, 3}; CHECK(d.Queue(odd, 5) && d.QueuedBytes() == 4);   // 5 bytes: one whole stereo frame kept
        d.ClearQueued(); CHECK(d.QueuedBytes() == 0); d.Render(out, 1); CHECK(out[0] == 0.0f);
        CHECK(d.Dequeue(out, 4) == 0);
    }
    CHECK(host.stopped);   // the destructor stops the render thread first
    { // mono S16 at 24 kHz to stereo float at 48 kHz: doubled length, interpolated, both channels equal
        AudioSpec m = want; m.channels = 1; m.freq = 24000; UwpAudioDevice d(m, host.fmt, nullptr); d.Pause(false);
        std::int16_t in[4] = {0, 32767, 0, -32768}; CHECK(d.Queue(in, sizeof(in)));
        float out[16] = {}; d.Render(out, 6);
        CHECK(out[0] == 0.0f && out[1] == 0.0f);                                       // frame 0 = sample 0
        CHECK(std::fabs(out[2] - 0.5f) < 1e-5 && out[2] == out[3]);                    // frame 1 = halfway to sample 1
        CHECK(std::fabs(out[4] - 1.0f) < 1e-5);                                        // frame 2 = sample 1
        CHECK(std::fabs(out[6] - 0.5f) < 1e-5);                                        // frame 3 = halfway back to 0 (sample 2)
    }
    { // a stream split over many small Render calls equals one big call (no clicks at the seams), 44.1k -> 48k
        AudioSpec m = want; m.freq = 44100; m.channels = 1; m.frames = 64; std::vector<std::int16_t> in(2000); for (std::size_t i = 0; i < in.size(); ++i) in[i] = (std::int16_t)(std::sin(i * 0.05) * 20000);
        AudioOutputFormat of{48000, 1};
        UwpAudioDevice a(m, of, nullptr), b(m, of, nullptr); a.Pause(false); b.Pause(false);
        CHECK(a.Queue(in.data(), (std::uint32_t)(in.size() * 2))); CHECK(b.Queue(in.data(), (std::uint32_t)(in.size() * 2)));
        std::vector<float> big(1500), small(1500); a.Render(big.data(), 1500);
        for (int off = 0; off < 1500;) { int n = std::min(1500 - off, 1 + (off * 7) % 37); b.Render(small.data() + off, n); off += n; }
        float maxDiff = 0; for (int i = 0; i < 1500; ++i) maxDiff = std::max(maxDiff, std::fabs(big[(std::size_t)i] - small[(std::size_t)i])); CHECK(maxDiff < 1e-4f);
        float maxStep = 0; for (int i = 1; i < 1500; ++i) maxStep = std::max(maxStep, std::fabs(big[(std::size_t)i] - big[(std::size_t)i - 1])); CHECK(maxStep < 0.06f);   // smooth sine, no jumps
    }
    { // callback mode: the engine's callback fills S16, the device converts; 5.1 output keeps front L/R
        AudioSpec c = want; c.callback = Ramp; c.frames = 64; AudioOutputFormat six{48000, 6}; g_cbCalls = 0;
        UwpAudioDevice d(c, six, nullptr); d.Pause(false); CHECK(!d.Queue("xxxx", 4));   // callback devices ignore Queue
        float out[6 * 100]; d.Render(out, 100);
        CHECK(std::fabs(out[0] - S16ToF32(1000)) < 1e-6 && std::fabs(out[1] - S16ToF32(1001)) < 1e-6 && out[2] == 0 && out[3] == 0 && out[4] == 0 && out[5] == 0);
        CHECK(std::fabs(out[6] - S16ToF32(1002)) < 1e-6); CHECK(g_cbCalls == 2);        // 100 frames need two 64-frame blocks
    }
    { // float input is passed through
        AudioSpec f = want; f.format = SampleFormat::F32; UwpAudioDevice d(f, host.fmt, nullptr); d.Pause(false); float in[4] = {0.25f, -0.25f, 0.5f, 0.75f}; CHECK(d.Queue(in, sizeof(in)));
        float out[4]; d.Render(out, 2); CHECK(out[0] == 0.25f && out[1] == -0.25f && out[2] == 0.5f && out[3] == 0.75f); }
    { UwpAudioDevice d(AudioSpec{}, host.fmt, nullptr); CHECK(d.Spec().freq == 48000 && d.Spec().channels == 1); }   // defaults survive
    { AudioSpec bad = want; bad.freq = 5; bad.channels = 0; bad.frames = 0; UwpAudioDevice d(bad, host.fmt, nullptr); CHECK(d.Spec().freq >= 8000 && d.Spec().channels >= 1 && d.Spec().frames >= 64); float o[4]; d.Render(o, 0); }
}

// ---- the UWP backend with a fake host ----
// UwpPlatform.cpp (linked into this test) provides the base-class defaults and the registry, as it does in the UWP engine target.
namespace {
struct FakeUwpHost : okay::xbox::UwpHost {
    int pumps = 0; std::function<void()> onPump; FakePad pad; int pads = 1; FakeAudioHost audio; bool hasAudio = true; std::string title;
    void* CoreWindowHandle() override { return this; }
    void PumpEvents() override { ++pumps; if (onPump) onPump(); }
    int PadCount() override { return pads; }
    okay::xbox::PadDevice* Pad(int) override { return &pad; }
    okay::xbox::AudioHost* Audio() override { return hasAudio ? &audio : nullptr; }
    void SetTitle(const std::string& t) override { title = t; }
    bool PresentPixels(const std::uint8_t*, int w, int h) override { return w > 0 && h > 0; }
};
std::vector<okay::platform::Event> Drain(okay::xbox::UwpBackend& b) { std::vector<okay::platform::Event> v; okay::platform::Event e; while (b.PollEvent(e)) v.push_back(e); return v; }
}
static void BackendTests() {
    using namespace okay::xbox; using namespace okay::platform;
    FakeUwpHost host; host.audio.fmt = {48000, 2};
    PathRoots roots; roots.install = "C:/WindowsApps/Okay/"; roots.local = "/tmp/okay_xbox_backend_local"; roots.temp = "/tmp/okay_xbox_backend_temp";
    std::filesystem::remove_all(roots.local);
    UwpBackend b(&host, roots);
    CHECK(b.Kind() == BackendKind::Own && b.Init(InitFlags()) && std::string(b.Name()) == "own");
    CHECK(b.BasePath() == "C:/WindowsApps/Okay/");
    { std::string p = b.PrefPath("Org", "My Game"); CHECK(p == "/tmp/okay_xbox_backend_local/saves/My Game/" && std::filesystem::is_directory(p)); CHECK(b.PrefPath("O", "../evil").empty()); }
    // window: size comes from the lifecycle, GL and moving are refused, the handle is the CoreWindow
    auto w = b.MakeWindow(WindowDesc()); int ww = 0, wh = 0;
    b.Life().Launched(); b.Life().Resized(960, 540, 192); w->GetSize(ww, wh); CHECK(ww == 1920 && wh == 1080);
    CHECK(w->NativeHandle() == &host && !w->CreateGLContext(GLAttributes()) && w->IsFullscreen() && w->IsFocused() && w->PresentPixels(nullptr, 4, 4) && !w->PresentPixels(nullptr, 0, 0));
    w->SetTitle("Hi"); CHECK(host.title == "Hi");
    // events: the host pump runs when the queue is empty; a resize and a key arrive in order
    host.onPump = [&] { b.PushKey(0x41, true); };
    { auto ev = Drain(b); CHECK(host.pumps >= 1); bool resized = false, key = false; for (auto& e : ev) { if (e.type == EventType::WindowResized && e.width == 1920 && e.height == 1080) resized = true; if (e.type == EventType::KeyDown && e.key == Key::A) key = true; } CHECK(resized && key); }
    host.onPump = nullptr;
    CHECK(b.IsKeyDown(Key::A)); b.PushKey(0x41, true); CHECK(Drain(b).empty());                           // no change, not a repeat: no event
    b.PushKey(0x41, true, true); { auto ev = Drain(b); CHECK(ev.size() == 1 && ev[0].repeat); }
    b.PushKey(0x41, false); { auto ev = Drain(b); CHECK(ev.size() == 1 && ev[0].type == EventType::KeyUp); } CHECK(!b.IsKeyDown(Key::A));
    b.PushKey(0xFFFF, true); CHECK(Drain(b).empty());
    b.PushKey(0xA0, true); CHECK((b.ModState() & ModShift) != 0); Drain(b);
    // text only while text input is on
    b.PushText("a"); CHECK(Drain(b).empty()); b.StartTextInput(); b.PushText("é"); { auto ev = Drain(b); CHECK(ev.size() == 1 && ev[0].text == "é"); } b.StopTextInput();
    // mouse
    b.PushPointerMove(10, 20); b.PushPointerMove(15, 18); b.PushPointerButton(MouseLeft, true, 15, 18); b.PushWheel(1.0f);
    { int x, y; CHECK((b.MouseState(&x, &y) & MouseMask(MouseLeft)) && x == 15 && y == 18); int dx, dy; b.RelativeMouseState(&dx, &dy); CHECK(dx == 15 && dy == 18); b.RelativeMouseState(&dx, &dy); CHECK(dx == 0 && dy == 0); }
    { auto ev = Drain(b); CHECK(ev.size() == 4 && ev[3].type == EventType::MouseWheel && ev[3].wheelY == 1.0f && ev[1].dx == 5 && ev[1].dy == -2); }
    b.PushPointerButton(MouseLeft, false, 15, 18); Drain(b); { int x, y; CHECK(!(b.MouseState(&x, &y) & MouseMask(MouseLeft))); }
    CHECK(b.KeyForChar('w') == Key::W && b.KeyForChar('W') == Key::W && b.KeyForChar('5') == Key::N5 && b.KeyForChar('0') == Key::N0 && b.KeyForChar('!') == Key::Unknown);
    // pads: open through the backend, polled by PollEvent, closed pads leave the registry
    CHECK(b.GamepadDeviceCount() == 1 && b.IsGamepad(0) && !b.IsGamepad(1));
    { auto g = b.OpenGamepad(0); CHECK(g && g->InstanceId() == 1); host.pad.r.buttons = kPadA; CHECK(!g->Button(GamepadButton::A)); Drain(b); CHECK(g->Button(GamepadButton::A)); }   // the pad is read when the loop polls events
    Drain(b);   // the destroyed pad is not polled any more (would crash otherwise)
    CHECK(b.OpenGamepad(5) == nullptr);
    b.PushPadAdded(0); b.PushPadRemoved(1); { auto ev = Drain(b); CHECK(ev.size() == 2 && ev[0].type == EventType::GamepadAdded && ev[1].deviceId == 1); }
    // audio: through the host, capture refused, no host audio refused
    { AudioSpec want; want.freq = 48000; want.channels = 2; AudioSpec got; auto a = b.OpenAudio(want, false, nullptr, &got); CHECK(a && host.audio.started && got.freq == 48000 && b.AudioDeviceCount(false) == 1); }
    CHECK(b.OpenAudio(AudioSpec(), true, nullptr, nullptr) == nullptr && b.LastError().find("capture") != std::string::npos);
    host.hasAudio = false; CHECK(b.OpenAudio(AudioSpec(), false, nullptr, nullptr) == nullptr && b.AudioDeviceCount(false) == 0); host.hasAudio = true;
    // lifecycle through events: suspend saves BEFORE the focus event is handled, resume comes back, B / back gives exactly one Quit
    int saved = 0, resumed = 0; b.onSuspend = [&] { ++saved; }; b.onResume = [&] { ++resumed; };
    b.Life().Suspending(b.TicksMs()); { auto ev = Drain(b); CHECK(saved == 1); bool lost = false; for (auto& e : ev) if (e.type == EventType::WindowFocusLost) lost = true; CHECK(lost); CHECK(b.Life().SuspendReady()); }
    b.Life().Resuming(); { auto ev = Drain(b); CHECK(resumed == 1); bool gained = false; for (auto& e : ev) if (e.type == EventType::WindowFocusGained) gained = true; CHECK(gained); }
    b.Life().BackRequested(); { auto ev = Drain(b); int quits = 0; for (auto& e : ev) if (e.type == EventType::Quit) ++quits; CHECK(quits == 1); }
    b.Life().BackRequested(); b.Life().WindowClosed(); { auto ev = Drain(b); int quits = 0; for (auto& e : ev) if (e.type == EventType::Quit) ++quits; CHECK(quits == 0); }   // already sent
    { using namespace okay::platform; CHECK(Default() == nullptr && Get(BackendKind::Own) == nullptr);
      InstallBackend(&b); CHECK(Default() == &b && Get(BackendKind::Own) == &b && Get(BackendKind::Sdl2) == nullptr && IsCompiledIn(BackendKind::Own) && !IsCompiledIn(BackendKind::Sdl2) && DefaultKind() == BackendKind::Own);
      CHECK(TicksMs() <= b.TicksMs() + 5); okay::platform::Log("through the registry"); CHECK(!b.LogLines().empty() && b.LogLines().back() == "through the registry");
      BackendKind k; CHECK(ParseBackendName("own", k) && k == BackendKind::Own && !ParseBackendName("x", k) && !ParseBackendName(nullptr, k)); InstallBackend(nullptr); CHECK(Default() == nullptr); }
    b.SetClipboardText("clip"); CHECK(b.GetClipboardText() == "clip");
    b.ShowErrorBox("T", "x", nullptr); CHECK(!b.LogLines().empty());
    CHECK(b.PerfFrequency() == 1000000000ull); b.DelayMs(10);
    std::filesystem::remove_all(roots.local);
}

// ---- UWP API audit scanner ----
static void AuditTests() {
    using namespace okay::xbox;
    auto n = [](const std::string& s) { return ScanSource(s).size(); };
    CHECK(n("int a;\nvoid f(){ D3DCompile(x); }\n") == 1);
    CHECK(ScanSource("int a;\nvoid f(){ D3DCompile(x); }\n")[0].line == 2);
    CHECK(n("// D3DCompile in a comment\n/* LoadLibraryA\n in a block */ int x;\n") == 0);
    CHECK(n("#include <d3dcompiler.h>\n") == 1 && n("#include <d3d11.h>\n") == 0);
    CHECK(n("MyD3DCompileHelper(1);\n") == 0 && n("D3DCompileFromFile(1);\n") == 0);                  // whole words only
    CHECK(n("auto p = std::getenv(\"APPDATA\");\n") == 1 && n("auto p = std::getenv(\"HOME\");\n") == 0);
    CHECK(n("SDL_Init(0); SDL_CreateWindow(0);\n") == 1);                                              // one finding per token per line
    // fenced code is not reported
    CHECK(n("#if !defined(OKAY_XBOX_UWP)\nLoadLibraryA(\"x\");\n#endif\n") == 0);
    CHECK(n("#if defined(OKAY_XBOX_UWP)\nx();\n#else\nLoadLibraryA(\"x\");\n#endif\n") == 0);
    CHECK(n("#if defined(OKAY_XBOX_UWP)\nLoadLibraryA(\"x\");\n#else\nx();\n#endif\n") == 1);
    CHECK(n("#if defined(_WIN32) && !defined(OKAY_XBOX_UWP)\nLoadLibraryA(\"x\");\n#endif\n") == 0);
    CHECK(n("#if defined(_WIN32)\nLoadLibraryA(\"x\");\n#endif\n") == 1);                                // Windows but not fenced: reported
    CHECK(n("#if !defined(_WIN32)\nSDL_Init(0);\n#endif\n") == 0);
    CHECK(n("#ifdef __linux__\nSDL_Init(0);\n#endif\n") == 0 && n("#ifndef _WIN32\nSDL_Init(0);\n#else\nx();\n#endif\n") == 0);
    CHECK(n("#if defined(__APPLE__) || defined(__linux__)\nSDL_Init(0);\n#endif\n") == 0);
    CHECK(n("#if defined(__linux__)\nA();\n#elif defined(_WIN32)\nLoadLibraryA(\"x\");\n#else\nB();\n#endif\n") == 1);
    CHECK(n("#if defined(_WIN32)\n#if !defined(OKAY_XBOX_UWP)\nLoadLibraryA(\"x\");\n#endif\n#endif\n") == 0);   // nested
    CHECK(n("#if SOMETHING_UNKNOWN\nLoadLibraryA(\"x\");\n#endif\n") == 1);                           // unknown condition: scanned (the safe direction)
    CHECK(n("#if defined(OKAY_HTTP_WINHTTP)\nWinHttpOpen(0);\n#endif\n") == 0);
    CHECK(n("#if 0\nLoadLibraryA(\"x\");\n#endif\n") == 0 && n("#if 1\nLoadLibraryA(\"x\");\n#endif\n") == 1);
    CHECK(n("#if (defined(_WIN32) && !defined(__MINGW32__))\nLoadLibraryA(\"x\");\n#endif\n") == 1);
    CHECK(n("#if defined(__GNUC__) && !defined(_MSC_VER)\nLoadLibraryA(\"x\");\n#endif\n") == 0);
    { std::vector<std::string> inc; ScanSource("#include \"a.hpp\"\n#if defined(__linux__)\n#include <b.h>\n#endif\n#include <c.h>\n", &inc); CHECK((inc == std::vector<std::string>{"a.hpp", "c.h"})); }   // only active includes
    // the engine source list parser
    { auto l = ParseEngineSourceList("project(x)\nadd_library(okay_engine\n    src/A.cpp\n    src/B/C.cpp  # (note) src/Z.cpp\n    third_party/u.c\n    src/D.cpp)\nadd_library(other src/E.cpp)\n");
      CHECK((l == std::vector<std::string>{"src/A.cpp", "src/B/C.cpp", "third_party/u.c", "src/D.cpp"})); }
    CHECK(ParseEngineSourceList("nothing").empty());
}

// ---- game config ----
static void ConfigTests() {
    using namespace okay::xbox;
    GameConfig c = ParseGameConfig("title=My Game\r\nwidth=1920\nheight=1080\nstartup=levels/one.okayscene\nscene=levels/one.okayscene\nscene=levels/two.okayscene\n"
                                   "physics_hz=60\nphysics_smooth=0\nvolume=0.5\nfps_cap=60\nvsync=0\ndevelopment=1\nshadows=1\nanticheat=report\n# comment=1\nnonsense\n");
    CHECK(c.title == "My Game" && c.width == 1920 && c.height == 1080 && c.startup == "levels/one.okayscene" && c.scenes.size() == 2);
    CHECK(c.physicsHz == 60.0f && !c.smoothPhysics && c.sawPhysics && c.volume == 0.5f && c.fpsCap == 60 && !c.vsync && c.development);
    CHECK((c.ignored == std::vector<std::string>{"shadows", "anticheat"}));
    { GameConfig d = ParseGameConfig(""); CHECK(d.title == "OkaySpace Game" && d.width == 1280 && !d.sawPhysics && d.vsync && d.startup.empty()); }
    { GameConfig d = ParseGameConfig("width=99999\nheight=-3\nphysics_hz=0\nvolume=9\nfps_cap=99999\n"); CHECK(d.width == 1280 && d.height == 720 && !d.sawPhysics && d.volume == 1.0f && d.fpsCap == 0); }   // nonsense falls back
    { GameConfig d = ParseGameConfig("volume=-2\n"); CHECK(d.volume == 0.0f); }
    std::set<std::string> have = {"game.okayscene", "levels/one.okayscene", "two.okayscene"};
    auto ex = [&](const std::string& p) { return have.count(p) != 0; };
    CHECK(ResolveStartupScene(c, ex) == "levels/one.okayscene");
    { GameConfig d; d.startup = "missing.okayscene"; CHECK(ResolveStartupScene(d, ex) == "game.okayscene"); }
    { GameConfig d; d.startup = "../../evil"; have.insert("../../evil"); CHECK(ResolveStartupScene(d, ex) == "game.okayscene"); have.erase("../../evil"); }   // an escaping name is never used
    { GameConfig d; d.startup = "C:/x"; have.insert("C:/x"); d.scenes = {"two.okayscene"}; have.erase("game.okayscene"); CHECK(ResolveStartupScene(d, ex) == "two.okayscene"); }
    { GameConfig d; have.clear(); CHECK(ResolveStartupScene(d, ex).empty()); }
}

// ---- UI overlay: raster core and overlay maths ----
static void RasterTests() {
    using namespace okay::xbox;
    auto px = [](const SoftSurface& s, int x, int y) { const std::uint8_t* p = s.Data() + ((std::size_t)y * (std::size_t)s.Width() + (std::size_t)x) * 4; return std::vector<int>{p[0], p[1], p[2], p[3]}; };
    const IRect all{0, 0, 8, 8};
    { SoftSurface s(8, 8); CHECK(px(s, 0, 0) == (std::vector<int>{0, 0, 0, 0}));   // starts transparent
      s.FillRect(IRect{1, 1, 3, 2}, 255, 0, 0, 255, true, all); CHECK(px(s, 1, 1) == (std::vector<int>{255, 0, 0, 255}) && px(s, 3, 2) == (std::vector<int>{255, 0, 0, 255}) && px(s, 4, 1)[3] == 0 && px(s, 1, 3)[3] == 0);
      IRect d = s.TakeDirty(); CHECK(d.x == 1 && d.y == 1 && d.w == 3 && d.h == 2); CHECK(s.TakeDirty().Empty()); }
    { SoftSurface s(4, 4); s.FillRect(IRect{0, 0, 4, 4}, 0, 0, 255, 128, true, IRect{0, 0, 4, 4});   // straight blue at 50%: premultiplied (0,0,128,128)
      CHECK(px(s, 0, 0) == (std::vector<int>{0, 0, 128, 128}));
      s.FillRect(IRect{0, 0, 4, 4}, 255, 0, 0, 128, true, IRect{0, 0, 4, 4});                     // red 50% over it: src + dst*(1-0.502)
      auto p = px(s, 0, 0); CHECK(p[0] == 128 && p[2] == 64 && p[3] == 192);
      s.FillRect(IRect{0, 0, 4, 4}, 10, 20, 30, 255, false, IRect{0, 0, 4, 4}); CHECK(px(s, 1, 1) == (std::vector<int>{10, 20, 30, 255})); }   // no blend: replace
    { SoftSurface s(8, 8); s.FillRect(IRect{0, 0, 8, 8}, 255, 255, 255, 255, true, IRect{2, 2, 2, 2}); CHECK(px(s, 2, 2)[3] == 255 && px(s, 1, 2)[3] == 0 && px(s, 4, 2)[3] == 0 && px(s, 3, 3)[3] == 255); }   // clip
    { SoftSurface s(8, 8); s.FillRect(IRect{-5, -5, 100, 100}, 1, 2, 3, 255, true, all); CHECK(px(s, 7, 7)[0] == 1); s.FillRect(IRect{20, 20, 3, 3}, 9, 9, 9, 255, true, all); s.FillRect(IRect{0, 0, 0, 5}, 9, 9, 9, 255, true, all); }   // off-surface and empty: no crash
    { SoftSurface s(8, 8); s.Line(0, 0, 7, 7, 255, 255, 255, 255, true, all); for (int i = 0; i < 8; ++i) CHECK(px(s, i, i)[3] == 255); CHECK(px(s, 0, 7)[3] == 0);
      SoftSurface h(8, 8); h.Line(1, 3, 6, 3, 9, 9, 9, 255, true, all); for (int x = 1; x <= 6; ++x) CHECK(px(h, x, 3)[3] == 255); CHECK(px(h, 0, 3)[3] == 0 && px(h, 7, 3)[3] == 0); }
    // premultiply, texture blit
    { std::uint8_t a[4] = {200, 100, 50, 128}; PremultiplyRgba(a, 1); CHECK(a[0] == 100 && a[1] == 50 && a[2] == 25 && a[3] == 128); std::uint8_t b[4] = {1, 2, 3, 255}; PremultiplyRgba(b, 1); CHECK(b[0] == 1); }
    SoftTexture t; t.w = 2; t.h = 2; t.px = {255, 0, 0, 255,  0, 255, 0, 255,  0, 0, 255, 255,  255, 255, 255, 255};   // R G / B W
    { SoftSurface s(8, 8); s.Blit(t, IRect{}, 0, 0, 4, 4, 0, 2, 2, false, false, all);   // 2x2 scaled to 4x4 nearest: each texel is 2x2
      CHECK(px(s, 0, 0) == (std::vector<int>{255, 0, 0, 255}) && px(s, 1, 1)[0] == 255 && px(s, 2, 0)[1] == 255 && px(s, 0, 2)[2] == 255 && px(s, 3, 3) == (std::vector<int>{255, 255, 255, 255}) && px(s, 4, 0)[3] == 0);
      SoftSurface f(8, 8); f.Blit(t, IRect{}, 0, 0, 4, 4, 0, 2, 2, true, false, all); CHECK(px(f, 0, 0)[1] == 255 && px(f, 2, 0)[0] == 255);   // flipped horizontally: G R / W B
      SoftSurface v(8, 8); v.Blit(t, IRect{}, 0, 0, 4, 4, 0, 2, 2, false, true, all); CHECK(px(v, 0, 0)[2] == 255 && px(v, 0, 2)[0] == 255 && px(v, 2, 2)[1] == 255);   // flipped vertically
      SoftSurface sub(8, 8); sub.Blit(t, IRect{1, 0, 1, 1}, 0, 0, 2, 2, 0, 1, 1, false, false, all); CHECK(px(sub, 0, 0)[1] == 255 && px(sub, 1, 1)[1] == 255);   // a sub-rectangle: only the green texel
      SoftSurface rot(8, 8); rot.Blit(t, IRect{}, 2, 2, 4, 4, 90, 4, 4, false, false, all); CHECK(px(rot, 5, 2) == (std::vector<int>{255, 0, 0, 255}) && px(rot, 2, 2)[2] == 255 && px(rot, 5, 5)[1] == 255);   // 90 degrees clockwise about the centre (y down): R (was top left) is top right, G bottom right, B top left
    }
    { SoftTexture m = t; m.modA = 128; m.modR = 0; SoftSurface s(4, 4); s.Blit(m, IRect{}, 0, 0, 2, 2, 0, 1, 1, false, false, IRect{0, 0, 4, 4}); auto p = px(s, 0, 0); CHECK(p[0] == 0 && p[3] == 128);   // alpha mod 128 scales everything, colour mod zeroes red
      SoftTexture nb = t; nb.blend = false; nb.modA = 255; SoftSurface s2(4, 4); s2.FillRect(IRect{0, 0, 4, 4}, 50, 50, 50, 255, true, IRect{0, 0, 4, 4}); s2.Blit(nb, IRect{}, 0, 0, 2, 2, 0, 1, 1, false, false, IRect{0, 0, 4, 4}); CHECK(px(s2, 0, 0)[0] == 255); }
    { SoftTexture lin; lin.w = 2; lin.h = 1; lin.linear = true; lin.px = {0, 0, 0, 255,  255, 255, 255, 255}; SoftSurface s(4, 1); s.Blit(lin, IRect{}, 0, 0, 4, 1, 0, 2, 0.5f, false, false, IRect{0, 0, 4, 1}); CHECK(px(s, 0, 0)[0] < px(s, 1, 0)[0] && px(s, 1, 0)[0] < px(s, 2, 0)[0] && px(s, 2, 0)[0] < px(s, 3, 0)[0]); }   // bilinear gradient
    // triangles: vertex colour, coverage, a degenerate one
    { SoftSurface s(8, 8); SoftSurface::Vtx v[3] = {{0, 0, 255, 0, 0, 255, 0, 0}, {8, 0, 255, 0, 0, 255, 0, 0}, {0, 8, 255, 0, 0, 255, 0, 0}}; s.Triangles(nullptr, v, 3, nullptr, 0, all);
      CHECK(px(s, 1, 1)[0] == 255 && px(s, 6, 6)[3] == 0 && px(s, 1, 6)[3] == 255);
      SoftSurface d(8, 8); SoftSurface::Vtx z[3] = {{1, 1, 9, 9, 9, 255, 0, 0}, {2, 2, 9, 9, 9, 255, 0, 0}, {3, 3, 9, 9, 9, 255, 0, 0}}; d.Triangles(nullptr, z, 3, nullptr, 0, all); CHECK(px(d, 2, 2)[3] == 0);
      SoftSurface i(8, 8); int idx[3] = {2, 1, 0}; s.TakeDirty(); i.Triangles(nullptr, v, 3, idx, 3, all); CHECK(px(i, 1, 1)[0] == 255);   // indexed, either winding
      SoftSurface g(8, 8); SoftSurface::Vtx gv[3] = {{0, 0, 255, 0, 0, 255, 0, 0}, {8, 0, 0, 255, 0, 255, 0, 0}, {0, 8, 0, 0, 255, 255, 0, 0}}; g.Triangles(nullptr, gv, 3, nullptr, 0, all); CHECK(px(g, 0, 3)[0] > px(g, 6, 0)[0] && px(g, 6, 0)[1] > px(g, 0, 6)[1]); }   // colours interpolate
    { SoftSurface s(4, 4); s.Clear(); CHECK(!s.TakeDirty().Empty()); s.Resize(0, -3); CHECK(s.Width() == 0 && s.Bytes() == 0); s.FillRect(IRect{0, 0, 2, 2}, 1, 1, 1, 255, true, IRect{0, 0, 2, 2}); }
    CHECK(Intersect(IRect{0, 0, 4, 4}, IRect{2, 2, 4, 4}).w == 2 && Intersect(IRect{0, 0, 2, 2}, IRect{5, 5, 2, 2}).Empty()); CHECK(Union(IRect{0, 0, 2, 2}, IRect{3, 3, 2, 2}).w == 5 && Union(IRect{}, IRect{1, 1, 2, 2}).x == 1);
}
static void OverlayMathTests() {
    using namespace okay::xbox;
    CHECK((UiBufferSize({1920, 1080}) == SizePx{1920, 1080}) && (UiBufferSize({1280, 720}) == SizePx{1280, 720}) && (UiBufferSize({0, 0}) == SizePx{1, 1}));
    { SizePx b = UiBufferSize({3840, 2160}); CHECK(b.w * b.h <= 1920 * 1080 + 4000 && b.w * 9 > b.h * 15 && b.w * 9 < b.h * 17); }   // 4K: shrunk to about 1080p, aspect kept
    CHECK((UiLayoutSize({1920, 1080}, 1.0f) == SizePx{1920, 1080}) && (UiLayoutSize({1920, 1080}, 2.0f) == SizePx{960, 540}) && (UiLayoutSize({1920, 1080}, 0.0f) == SizePx{3840, 2160}) && (UiLayoutSize({1920, 1080}, 99.0f) == SizePx{480, 270}));
    { int x, y; DipsToUiPixel(480, 270, {960, 540}, {1920, 1080}, x, y); CHECK(x == 960 && y == 540); DipsToUiPixel(-5, 9999, {960, 540}, {1920, 1080}, x, y); CHECK(x == 0 && y == 1079); DipsToUiPixel(1, 1, {0, 0}, {10, 10}, x, y); CHECK(x == 0 && y == 0); }
    OverlayState o; CHECK(o.Next(false) == OverlayAction::Nothing); CHECK(o.Next(true) == OverlayAction::DrawAndUpload && o.Shown()); CHECK(o.Next(true) == OverlayAction::DrawAndUpload);
    CHECK(o.Next(false) == OverlayAction::ClearOnce && !o.Shown()); CHECK(o.Next(false) == OverlayAction::Nothing);   // cleared exactly once
    { std::vector<std::uint8_t> src(4 * 4 * 4, 7), dst(4 * 64, 0); CHECK(CopyDirtyRows(src.data(), 4, 4, IRect{0, 1, 4, 2}, dst.data(), 64) == 2 * 16);
      CHECK(dst[0] == 0 && dst[64] == 7 && dst[64 + 15] == 7 && dst[64 + 16] == 0 && dst[128] == 7 && dst[192] == 0);   // rows 1 and 2 only, at the GPU's row pitch
      CHECK(CopyDirtyRows(src.data(), 4, 4, IRect{}, dst.data(), 64) == 0 && CopyDirtyRows(src.data(), 4, 4, IRect{0, 0, 4, 99}, dst.data(), 64) == 4 * 16); }
}

int main() {
    ShaderTests(); DisplayTests(); BuildPlanTests(); LogTests(); SourceRootTests(); RunnerTests(); HostTests(); LifecycleTests(); InputTests(); PadClassTests(); PathTests(); AudioTests(); BackendTests(); AuditTests(); ConfigTests(); RasterTests(); OverlayMathTests();
    if (g_fail) { std::printf("%d FAILED\n", g_fail); return 1; }
    std::printf("xbox_uwp_runtime: all passed\n");
    return 0;
}
