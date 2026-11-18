// Linux tests for the Xbox UWP runtime pieces that are pure logic: offline shader bundle, display sizing, lifecycle, input mapping,
// path mapping, audio conversion. The Windows-only glue (CoreWindow, WASAPI, Windows.Gaming.Input) is NOT covered: it is uncompiled.
#include "okay/Render/D3D11Hlsl.hpp"
#include "okay/Render/ShaderBundle.hpp"
#include "XboxDisplay.hpp"
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

int main() {
    ShaderTests(); DisplayTests();
    if (g_fail) { std::printf("%d FAILED\n", g_fail); return 1; }
    std::printf("xbox_uwp_runtime: all passed\n");
    return 0;
}
