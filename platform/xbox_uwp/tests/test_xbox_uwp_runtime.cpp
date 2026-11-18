// Linux tests for the Xbox UWP runtime pieces that are pure logic: offline shader bundle, display sizing, lifecycle, input mapping,
// path mapping, audio conversion. The Windows-only glue (CoreWindow, WASAPI, Windows.Gaming.Input) is NOT covered: it is uncompiled.
#include "okay/Render/D3D11Hlsl.hpp"
#include "okay/Render/ShaderBundle.hpp"
#include "XboxDisplay.hpp"
#include "XboxBuildPlan.hpp"
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
    in.sdk.visualStudioPath = "C:/VS/2022/Community"; in.sdk.sdkVersion = "10.0.19041.0"; in.sdk.signToolPath = "C:/Kits/signtool.exe";
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
    CHECK(p[2].cmd.Line().find("\"C:/out/My Game/build/app\"") != std::string::npos);   // spaces quoted
    CHECK(p[3].cmd.Line().find("--config Release") != std::string::npos);
    CHECK(p[5].cmd.Line().find("-OutDir \"C:/out/My Game/package/Shaders\"") != std::string::npos);
    CHECK(p[7].cmd.Line().find("-Subject \"CN=Me, O=Studio\"") != std::string::npos);
    for (auto& s : p) CHECK(!s.cmd.elevated);
    { PlanInputs b = in; b.sdk.visualStudioPath = ""; CHECK(PlanBuild(b)[2].cmd.exe == "cmake"); }
    { PlanInputs b = in; b.fs.list = nullptr; CHECK(PlanBuild(b)[2].cmd.exe == "cmake"); }
    { PlanInputs b = in; b.signPackage = false; b.installOnThisPc = true; CHECK(PlanBuild(b).back().id == "pack"); }   // no install of an unsigned package
    { PlanInputs b = in; b.installOnThisPc = true; auto q = PlanBuild(b); CHECK(q.back().id == "install" && q.back().cmd.elevated);
      const std::string l = q.back().cmd.Line(); CHECK(l.find("-Verb RunAs") != std::string::npos); CHECK(l.find("Import-Certificate") == std::string::npos);   // the import is inside the script, run elevated
      CHECK(l.find("xbox-install-local.ps1") != std::string::npos); CHECK(l.find("install-result.txt") != std::string::npos);
      int uac = 0; for (auto& s : q) if (s.cmd.elevated) ++uac; CHECK(uac == 1); }
    { PlanInputs b = in; b.signer = Signer::OwnSigning; auto q = PlanBuild(b); CHECK(q.back().id == "sign" && q.back().kind == StepKind::Internal); CHECK(std::string(SignerLabel(Signer::OwnSigning)).find("not independently reviewed") != std::string::npos); }
    { PlanInputs b = in; b.installOnThisPc = true; b.appxPath = "C:/o/it's.appx"; CHECK(PlanBuild(b).back().cmd.Line().find("it''''s") != std::string::npos); }
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
    CHECK(Redact("-----BEGIN PRIVATE KEY-----").find("hidden") != std::string::npos);
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

int main() {
    ShaderTests(); DisplayTests(); BuildPlanTests(); LogTests(); RunnerTests();
    if (g_fail) { std::printf("%d FAILED\n", g_fail); return 1; }
    std::printf("xbox_uwp_runtime: all passed\n");
    return 0;
}
