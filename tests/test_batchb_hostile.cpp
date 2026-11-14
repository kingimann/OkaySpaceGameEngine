// Hostile input and mutation fuzz for the batch-B own readers: 3MF, AMF, X3D, DXF, LWO. Needs only the reader sources,
// okay/Render/ModelData.hpp and the engine's Core/ImageDecode*.cpp (the inflate used by the zip reader), so it builds
// on its own, e.g.
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all -DOKAY_STANDALONE_TEST
//       -Iengine/include -Iengine/src/Core -Itests tests/test_batchb_hostile.cpp engine/src/Render/<readers>.cpp
//       engine/src/Core/ImageDecode.cpp engine/src/Core/ImageDecodeJpeg.cpp -o hostile
// OKAY_BATCHB_FUZZ_ITERS=200000 for a long run (per reader). All fixtures here are SYNTHETIC.
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "okay/Render/ThreeMfReader.hpp"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace okay;

static long Iters() { const char* e = std::getenv("OKAY_BATCHB_FUZZ_ITERS"); return e ? std::atol(e) : 3000; }
static double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

// A read must either give a sane model or fail with a message and leave nothing behind.
template <class F> static void Try(F&& read, const std::string& bytes) {
    ImportedModel m; std::string err;
    const bool ok = read(bytes, m, err);
    if (ok) { CHECK(fx::Sane(m)); }
    else { CHECK(!err.empty() && m.parts.empty() && m.nodes.empty()); }
}
static std::string Mutate(const std::string& src) {
    std::string s = src;
    const int kind = (int)(fx::Rnd() % 6);
    if (s.empty()) return s;
    const std::size_t p = (std::size_t)(fx::Rnd() % s.size());
    switch (kind) {
    case 0: s[p] = (char)(fx::Rnd() & 255); break;
    case 1: s[p] ^= (char)(1u << (fx::Rnd() % 8)); break;
    case 2: s.erase(p, (std::size_t)(fx::Rnd() % 16) + 1); break;
    case 3: { std::string ins; for (int i = (int)(fx::Rnd() % 8) + 1; i > 0; --i) ins.push_back((char)(fx::Rnd() & 255)); s.insert(p, ins); break; }
    case 4: { const char* t[] = {"0", "-1", "99999999999", "1e308", "nan", "-0", "4294967295", "2147483648", " ", ",", "-", "e", "."}; s.replace(p, std::min<std::size_t>(3, s.size() - p), t[fx::Rnd() % 13]); break; }
    default: s.resize(p); break;
    }
    return s;
}

// ======================= 3MF =======================
static std::string Pkg3mf(const std::string& model) {
    fx::ZipEntry r; r.name = "_rels/.rels"; r.data = "<Relationships><Relationship Target=\"/3D/3dmodel.model\" Type=\"x/3dmodel\"/></Relationships>";
    fx::ZipEntry m; m.name = "3D/3dmodel.model"; m.data = model;
    return fx::MakeZip({r, m});
}
static bool Read3mf(const std::string& b, ImportedModel& m, std::string& e) { return Read3mfBuffer(b, m, &e); }
static const char* kModel3mf =
    "<model unit=\"millimeter\"><resources><basematerials id=\"1\"><base name=\"a\" displaycolor=\"#FF8000\"/></basematerials>"
    "<object id=\"1\" pid=\"1\" pindex=\"0\"><mesh><vertices><vertex x=\"0\" y=\"0\" z=\"0\"/><vertex x=\"1\" y=\"0\" z=\"0\"/><vertex x=\"0\" y=\"1\" z=\"0\"/><vertex x=\"0\" y=\"0\" z=\"1\"/></vertices>"
    "<triangles><triangle v1=\"0\" v2=\"2\" v3=\"1\" p1=\"0\"/><triangle v1=\"0\" v2=\"1\" v3=\"3\"/><triangle v1=\"1\" v2=\"2\" v3=\"3\"/><triangle v1=\"2\" v2=\"0\" v3=\"3\"/></triangles></mesh></object>"
    "<object id=\"2\"><components><component objectid=\"1\" transform=\"1 0 0 0 1 0 0 0 1 2 0 0\"/></components></object></resources>"
    "<build><item objectid=\"2\" transform=\"0 1 0 -1 0 0 0 0 1 0 0 0\"/><item objectid=\"1\"/></build></model>";

static void Hostile3mf() {
    RUN_SUITE("3mf hostile: zip");
    const std::string good = Pkg3mf(kModel3mf);
    { ImportedModel m; std::string e; CHECK(Read3mf(good, m, e) && m.parts.size() == 2); }
    for (std::size_t n = 0; n < good.size(); ++n) Try(Read3mf, good.substr(0, n));          // every truncation
    // zip bomb: the directory declares a size over the cap -> refused before inflating
    { fx::ZipEntry b; b.name = "3D/3dmodel.model"; b.rawSet = true; b.deflate = true; b.raw = fx::DeflateRepeat('a', 1000); b.usize = 0x7FFFFFFFu; b.crc = 0;
      ImportedModel m; std::string e; CHECK(!Read3mf(fx::MakeZip({b}), m, e) && e.find("zip bomb") != std::string::npos); }
    // declares a small size but the stream inflates to far more: the inflater stops at the declared size
    { fx::ZipEntry b; b.name = "3D/3dmodel.model"; b.rawSet = true; b.deflate = true; b.raw = fx::DeflateRepeat('a', 2000000); b.usize = 1000; b.crc = 0;
      const double t0 = Now(); ImportedModel m; std::string e; CHECK(!Read3mf(fx::MakeZip({b}), m, e) && !e.empty()); CHECK(Now() - t0 < 5.0); }
    // honest big entry: 200 MB of 'a' (a few hundred KB compressed) is under the per-entry cap, fails as XML, in bounded time
    { fx::ZipEntry b; b.name = "3D/3dmodel.model"; b.rawSet = true; b.deflate = true; const std::size_t matches = 400000; b.raw = fx::DeflateRepeat('a', matches); b.usize = (std::uint32_t)(1 + matches * 258); b.crc = 0;
      const double t0 = Now(); ImportedModel m; std::string e; CHECK(!Read3mf(fx::MakeZip({b}), m, e) && !e.empty()); CHECK(Now() - t0 < 20.0); }
    // bad directory: entry count over the cap, offsets outside the file, no end record, ZIP64 markers
    { std::string z = good; for (std::size_t i = z.size() - 22; i + 4 < z.size(); ++i) if (z.compare(i, 4, "PK\x05\x06") == 0) { z[i + 10] = (char)0xFF; z[i + 11] = (char)0xFF; }
      ImportedModel m; std::string e; CHECK(!Read3mf(z, m, e) && !e.empty()); }
    { std::string z = good; for (std::size_t i = 0; i + 4 < z.size(); ++i) if (z.compare(i, 4, "PK\x01\x02") == 0) { z[i + 42] = (char)0xFF; z[i + 43] = (char)0xFF; z[i + 44] = (char)0x7F; }
      ImportedModel m; std::string e; CHECK(!Read3mf(z, m, e) && !e.empty()); }
    { std::string z = good; z.resize(z.size() - 22); ImportedModel m; std::string e; CHECK(!Read3mf(z, m, e) && !e.empty()); }
    // a wrong checksum is refused
    { std::string z = good; for (std::size_t i = 0; i + 4 < z.size(); ++i) if (z.compare(i, 4, "PK\x01\x02") == 0) { z[i + 16] ^= 0x55; }
      ImportedModel m; std::string e; CHECK(!Read3mf(z, m, e) && e.find("checksum") != std::string::npos); }
    // an unsupported method
    { std::string z = good; for (std::size_t i = 0; i + 4 < z.size(); ++i) if (z.compare(i, 4, "PK\x01\x02") == 0) z[i + 10] = 12;
      ImportedModel m; std::string e; CHECK(!Read3mf(z, m, e) && e.find("method") != std::string::npos); }

    RUN_SUITE("3mf hostile: xml and model");
    auto model = [&](const std::string& s) { ImportedModel m; std::string e; const bool ok = Read3mf(Pkg3mf(s), m, e); if (ok) { CHECK(fx::Sane(m)); } else { CHECK(!e.empty() && m.parts.empty()); } return ok; };
    { std::string deep; for (int i = 0; i < 20000; ++i) deep += "<a>"; CHECK(!model(deep)); }
    { std::string deep = "<model>"; for (int i = 0; i < 300; ++i) deep += "<a>"; CHECK(!model(deep)); }
    { std::string at = "<model "; for (int i = 0; i < 5000; ++i) at += "a" + std::to_string(i) + "=\"1\" "; at += "/>"; CHECK(!model(at)); }
    { std::string lol = "<!DOCTYPE lolz [<!ENTITY lol \"lol\"><!ENTITY lol2 \"&lol;&lol;&lol;&lol;&lol;&lol;&lol;&lol;&lol;&lol;\">]><model>&lol2;</model>"; CHECK(!model(lol)); }
    { CHECK(!model("<!DOCTYPE m SYSTEM \"file:///etc/passwd\"><model>&xxe;</model>")); }
    CHECK(!model("<model>&unknown;</model>"));
    CHECK(!model("\xFF\xFE<\0m\0>\0"));
    CHECK(!model("<model><resources></model>"));
    CHECK(!model("<model></model><model></model>"));
    // numbers
    for (const char* bad : {"1e999", "inf", "-inf", "nan", "0x10", "1e400", "99999999999999999999999999", "1e16", "--1", "1..2", ""}) {
        std::string s = kModel3mf; s.replace(s.find("x=\"1\""), 5, std::string("x=\"") + bad + "\"");
        CHECK(!model(s));
    }
    // indices
    for (const char* bad : {"-1", "4294967296", "99999999999", "1.5", "", "abc", "0x1"}) {
        std::string s = kModel3mf; s.replace(s.find("v1=\"0\""), 6, std::string("v1=\"") + bad + "\"");
        CHECK(!model(s));
    }
    // a component chain 100 deep, and one that fans out 20 x 20 x 20 x 20 (instance cap, not a hang)
    { std::string r = "<model><resources><object id=\"1\"><mesh><vertices><vertex x=\"0\" y=\"0\" z=\"0\"/><vertex x=\"1\" y=\"0\" z=\"0\"/><vertex x=\"0\" y=\"1\" z=\"0\"/></vertices><triangles><triangle v1=\"0\" v2=\"1\" v3=\"2\"/></triangles></mesh></object>";
      for (int i = 2; i <= 100; ++i) r += "<object id=\"" + std::to_string(i) + "\"><components><component objectid=\"" + std::to_string(i - 1) + "\"/></components></object>";
      r += "</resources><build><item objectid=\"100\"/></build></model>"; CHECK(!model(r)); }
    { std::string r = "<model><resources><object id=\"1\"><mesh><vertices><vertex x=\"0\" y=\"0\" z=\"0\"/><vertex x=\"1\" y=\"0\" z=\"0\"/><vertex x=\"0\" y=\"1\" z=\"0\"/></vertices><triangles><triangle v1=\"0\" v2=\"1\" v3=\"2\"/></triangles></mesh></object>";
      for (int i = 2; i <= 6; ++i) { r += "<object id=\"" + std::to_string(i) + "\"><components>"; for (int k = 0; k < 30; ++k) r += "<component objectid=\"" + std::to_string(i - 1) + "\"/>"; r += "</components></object>"; }
      r += "</resources><build><item objectid=\"6\"/></build></model>";
      const double t0 = Now(); CHECK(!model(r)); CHECK(Now() - t0 < 10.0); }
    // 2000 materials, many build items
    { std::string r = "<model><resources><basematerials id=\"1\">"; for (int i = 0; i < 2000; ++i) r += "<base name=\"m\" displaycolor=\"#" + std::string(i % 2 ? "FF0000" : "00FF00") + "\"/>"; r += "</basematerials>";
      r += "<object id=\"2\" pid=\"1\"><mesh><vertices><vertex x=\"0\" y=\"0\" z=\"0\"/><vertex x=\"1\" y=\"0\" z=\"0\"/><vertex x=\"0\" y=\"1\" z=\"0\"/></vertices><triangles>";
      for (int i = 0; i < 2000; ++i) r += "<triangle v1=\"0\" v2=\"1\" v3=\"2\" p1=\"" + std::to_string(i) + "\"/>";
      r += "</triangles></mesh></object></resources><build><item objectid=\"2\"/></build></model>"; CHECK(model(r)); }

    RUN_SUITE("3mf fuzz");
    const std::string base = kModel3mf;
    const long n = Iters();
    for (long i = 0; i < n; ++i) {
        Try(Read3mf, Pkg3mf(Mutate(base)));                 // mutated XML, checksum valid
        if (i % 4 == 0) Try(Read3mf, Mutate(good));         // mutated container
    }
}






int main() {
    Hostile3mf();
    TEST_MAIN_RESULT();
}
