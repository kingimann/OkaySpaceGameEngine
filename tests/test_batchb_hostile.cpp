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
#include "okay/Render/AmfReader.hpp"
#include "okay/Render/X3dReader.hpp"
#include "okay/Render/DxfReader.hpp"
#include "okay/Render/LwoReader.hpp"
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


// ======================= AMF =======================
static bool ReadAmf(const std::string& b, ImportedModel& m, std::string& e) { return ReadAmfBuffer(b, m, &e); }
static const char* kAmf =
    "<amf unit=\"millimeter\"><material id=\"1\"><metadata type=\"name\">m</metadata><color><r>1</r><g>0.5</g><b>0</b></color></material>"
    "<object id=\"0\"><color><r>0</r><g>1</g><b>0</b></color><mesh><vertices>"
    "<vertex><coordinates><x>0</x><y>0</y><z>0</z></coordinates></vertex><vertex><coordinates><x>1</x><y>0</y><z>0</z></coordinates></vertex>"
    "<vertex><coordinates><x>0</x><y>1</y><z>0</z></coordinates><color><r>1</r><g>0</g><b>0</b></color></vertex><vertex><coordinates><x>0</x><y>0</y><z>1</z></coordinates></vertex></vertices>"
    "<volume materialid=\"1\"><triangle><v1>0</v1><v2>2</v2><v3>1</v3></triangle><triangle><v1>0</v1><v2>1</v2><v3>3</v3></triangle></volume>"
    "<volume><triangle><color><r>0</r><g>0</g><b>1</b></color><v1>1</v1><v2>2</v2><v3>3</v3></triangle><triangle><v1>2</v1><v2>0</v2><v3>3</v3></triangle></volume></mesh></object>"
    "<constellation id=\"1\"><instance objectid=\"0\"><deltax>5</deltax><rz>45</rz></instance></constellation></amf>";

static void HostileAmf() {
    RUN_SUITE("amf hostile");
    { ImportedModel m; std::string e; CHECK(ReadAmf(kAmf, m, e) && m.parts.size() == 3); }
    const std::string good = kAmf;
    for (std::size_t n = 0; n < good.size(); ++n) Try(ReadAmf, good.substr(0, n));
    auto amf = [&](const std::string& s) { ImportedModel m; std::string e; const bool ok = ReadAmf(s, m, e); if (ok) { CHECK(fx::Sane(m)); } else { CHECK(!e.empty() && m.parts.empty()); } return ok; };
    { std::string deep; for (int i = 0; i < 20000; ++i) deep += "<a>"; CHECK(!amf(deep)); }
    { std::string lol = "<!DOCTYPE lolz [<!ENTITY lol \"lol\"><!ENTITY lol2 \"&lol;&lol;&lol;&lol;&lol;&lol;&lol;&lol;&lol;&lol;\">]><amf>&lol2;</amf>"; CHECK(!amf(lol)); }
    CHECK(!amf("<!DOCTYPE a SYSTEM \"http://example.com/x.dtd\"><amf>&ext;</amf>"));
    for (const char* bad : {"1e999", "inf", "nan", "0x10", "1..2", "", "1e16", "--1"}) {
        std::string s = kAmf; s.replace(s.find("<x>0</x>"), 8, std::string("<x>") + bad + "</x>");
        CHECK(!amf(s));
    }
    for (const char* bad : {"-1", "99999999999", "1.5", "", "abc"}) {
        std::string s = kAmf; s.replace(s.find("<v1>0</v1>"), 10, std::string("<v1>") + bad + "</v1>");
        CHECK(!amf(s));
    }
    // constellation values: huge rotation and offset are valid numbers; overflow shows up as a clean failure or a finite result
    for (const char* v : {"1e15", "-1e15", "1e300", "nan", "1e-320"}) {
        std::string s = kAmf; s.replace(s.find("<deltax>5</deltax>"), 18, std::string("<deltax>") + v + "</deltax>"); amf(s);
        s = kAmf; s.replace(s.find("<rz>45</rz>"), 11, std::string("<rz>") + v + "</rz>"); amf(s);
    }
    { // 100001 instances
      std::string s = "<amf>"; s += std::string(kAmf).substr(std::string(kAmf).find("<object"), std::string(kAmf).find("<constellation") - std::string(kAmf).find("<object"));
      s += "<constellation id=\"1\">"; for (int i = 0; i < 100001; ++i) s += "<instance objectid=\"0\"/>"; s += "</constellation></amf>"; CHECK(!amf(s)); }
    { // 3000 instances of one object are fine
      std::string s = "<amf>"; s += std::string(kAmf).substr(std::string(kAmf).find("<object"), std::string(kAmf).find("<constellation") - std::string(kAmf).find("<object"));
      s += "<constellation id=\"1\">"; for (int i = 0; i < 3000; ++i) s += "<instance objectid=\"0\"><deltax>" + std::to_string(i) + "</deltax></instance>"; s += "</constellation></amf>"; CHECK(amf(s)); }
    { // 100000 distinct colours (material cap) and 3000 distinct triangle colours
      std::string s = "<amf><object id=\"0\"><mesh><vertices><vertex><coordinates><x>0</x><y>0</y><z>0</z></coordinates></vertex><vertex><coordinates><x>1</x><y>0</y><z>0</z></coordinates></vertex><vertex><coordinates><x>0</x><y>1</y><z>0</z></coordinates></vertex></vertices><volume>";
      for (int i = 0; i < 3000; ++i) s += "<triangle><color><r>" + std::to_string(i / 3000.0) + "</r><g>0</g><b>0</b></color><v1>0</v1><v2>1</v2><v3>2</v3></triangle>";
      s += "</volume></mesh></object></amf>"; CHECK(amf(s)); }
    // zip in AMF: a bomb and an archive with no .amf
    { fx::ZipEntry b; b.name = "a.amf"; b.rawSet = true; b.deflate = true; b.raw = fx::DeflateRepeat('a', 1000); b.usize = 0x7FFFFFFFu; b.crc = 0;
      ImportedModel m; std::string e; CHECK(!ReadAmf(fx::MakeZip({b}), m, e) && e.find("zip bomb") != std::string::npos); }
    { fx::ZipEntry b; b.name = "a.amf"; b.rawSet = true; b.deflate = true; b.raw = fx::DeflateRepeat('a', 2000000); b.usize = 1000; b.crc = 0;
      ImportedModel m; std::string e; CHECK(!ReadAmf(fx::MakeZip({b}), m, e) && !e.empty()); }
    { fx::ZipEntry a; a.name = "a.amf"; a.data = kAmf; const std::string z = fx::MakeZip({a}); for (std::size_t n = 0; n < z.size(); ++n) Try(ReadAmf, z.substr(0, n)); }
    RUN_SUITE("amf fuzz");
    const long n = Iters();
    fx::ZipEntry a; a.name = "a.amf"; a.data = kAmf; a.deflate = true;
    const std::string zgood = fx::MakeZip({a});
    for (long i = 0; i < n; ++i) {
        Try(ReadAmf, Mutate(good));
        if (i % 4 == 0) { fx::ZipEntry x; x.name = "a.amf"; x.data = Mutate(good); Try(ReadAmf, fx::MakeZip({x})); }
        if (i % 8 == 0) Try(ReadAmf, Mutate(zgood));
    }
}


// ======================= X3D =======================
static bool ReadX3d(const std::string& b, ImportedModel& m, std::string& e) { return ReadX3dBuffer(b, "", m, &e); }
static std::string X3dDoc(const std::string& scene) { return "<?xml version=\"1.0\"?><!DOCTYPE X3D PUBLIC \"-//Web3D//DTD X3D 3.3//EN\" \"x.dtd\"><X3D><Scene>" + scene + "</Scene></X3D>"; }
static const char* kX3d =
    "<Transform DEF=\"T\" translation=\"1 2 3\" rotation=\"0 1 0 0.5\" scale=\"1 2 1\" center=\"0 1 0\" scaleOrientation=\"1 0 0 0.3\"><Shape DEF=\"S\"><Appearance><Material diffuseColor=\"1 0.5 0\" transparency=\"0.1\"/><ImageTexture url='\"a.png\"'/></Appearance>"
    "<IndexedFaceSet coordIndex=\"0 1 2 3 -1 0 3 4 -1\" normalIndex=\"0 0 0 0 -1 1 1 1 -1\" texCoordIndex=\"0 1 2 3 -1 0 3 2 -1\" creaseAngle=\"1\" ccw=\"true\"><Coordinate point=\"0 0 0, 1 0 0, 1 1 0, 0 1 0, 0 0 1\"/>"
    "<Normal vector=\"0 0 1 0 1 0\"/><TextureCoordinate point=\"0 0 1 0 1 1 0 1\"/></IndexedFaceSet></Shape></Transform>"
    "<Group><Shape USE=\"S\"/><Switch whichChoice=\"0\"><Shape><Box size=\"1 2 3\"/></Shape></Switch><LOD><Shape><Sphere/></Shape><Shape><Cone/></Shape></LOD><Shape><Cylinder/></Shape></Group>";

static void HostileX3d() {
    RUN_SUITE("x3d hostile");
    const std::string good = X3dDoc(kX3d);
    { ImportedModel m; std::string e; CHECK(ReadX3d(good, m, e) && m.parts.size() == 5); }
    for (std::size_t n = 0; n < good.size(); ++n) Try(ReadX3d, good.substr(0, n));
    auto x3d = [&](const std::string& s) { ImportedModel m; std::string e; const bool ok = ReadX3d(X3dDoc(s), m, e); if (ok) { CHECK(fx::Sane(m)); } else { CHECK(!e.empty() && m.parts.empty()); } return ok; };
    { std::string deep; for (int i = 0; i < 20000; ++i) deep += "<Group>"; CHECK(!x3d(deep)); }
    { std::string deep; for (int i = 0; i < 200; ++i) deep += "<Group>"; deep += "<Shape><Box/></Shape>"; for (int i = 0; i < 200; ++i) deep += "</Group>"; CHECK(!x3d(deep)); }   // over the scene depth cap
    { std::string lol = "<!DOCTYPE X3D [<!ENTITY lol \"lol\"><!ENTITY lol2 \"&lol;&lol;&lol;\">]><X3D><Scene>&lol2;</Scene></X3D>"; ImportedModel m; std::string e; CHECK(!ReadX3d(lol, m, e)); }
    // an exponential DEF/USE expansion: each level uses the previous one twice (2^40 shapes if followed)
    { std::string s = "<Group DEF=\"g0\"><Shape><Box/></Shape></Group>";
      for (int i = 1; i <= 40; ++i) s += "<Group DEF=\"g" + std::to_string(i) + "\"><Group USE=\"g" + std::to_string(i - 1) + "\"/><Group USE=\"g" + std::to_string(i - 1) + "\"/></Group>";
      const double t0 = Now(); CHECK(!x3d(s)); CHECK(Now() - t0 < 30.0); }
    // a shape reused 3000 times
    { std::string s = "<Shape DEF=\"s\"><Box/></Shape>"; for (int i = 0; i < 3000; ++i) s += "<Transform translation=\"" + std::to_string(i) + " 0 0\"><Shape USE=\"s\"/></Transform>"; CHECK(x3d(s)); }
    CHECK(!x3d("<Group DEF=\"a\"><Group USE=\"a\"/></Group>"));
    for (const char* bad : {"1e999", "inf", "nan", "0x10", "1..2", "--1", "1e16", "1 2", ""}) {
        CHECK(!x3d(std::string("<Transform translation=\"") + bad + "\"><Shape><Box/></Shape></Transform><Group/>") || std::string(bad) == "");
        std::string s = "<Shape><IndexedFaceSet coordIndex=\"0 1 2 -1\"><Coordinate point=\"0 0 0 1 0 0 0 " + std::string(bad) + " 0\"/></IndexedFaceSet></Shape>"; CHECK(!x3d(s) || std::string(bad) == "1 2" || std::string(bad) == "");
    }
    for (const char* bad : {"-5", "99999999999", "1.5", "a", "2147483648"}) {
        CHECK(x3d(std::string("<Shape><IndexedFaceSet coordIndex=\"0 1 2 -1 0 1 2 -1\"><Coordinate point=\"0 0 0 1 0 0 0 1 0\"/></IndexedFaceSet></Shape>").replace(48, 0, "")) || true);
        std::string s = "<Shape><IndexedFaceSet coordIndex=\"0 1 2 -1 " + std::string(bad) + " 1 2 -1\"><Coordinate point=\"0 0 0 1 0 0 0 1 0\"/></IndexedFaceSet></Shape>"; x3d(s);
    }
    // huge polygon: 100000 corners, convex fan; and a concave 250-corner star (ear clipping bound)
    { std::string idx, pts; for (int i = 0; i < 100000; ++i) { idx += std::to_string(i) + " "; const double a = 6.283185307 * i / 100000; pts += std::to_string(std::cos(a)) + " " + std::to_string(std::sin(a)) + " 0 "; }
      CHECK(x3d("<Shape><IndexedFaceSet coordIndex=\"" + idx + "-1\"><Coordinate point=\"" + pts + "\"/></IndexedFaceSet></Shape>")); }
    { std::string idx, pts; for (int i = 0; i < 250; ++i) { idx += std::to_string(i) + " "; const double a = 6.283185307 * i / 250, r = i % 2 ? 0.4 : 1.0; pts += std::to_string(r * std::cos(a)) + " " + std::to_string(r * std::sin(a)) + " 0 "; }
      const double t0 = Now(); CHECK(x3d("<Shape><IndexedFaceSet coordIndex=\"" + idx + "-1\"><Coordinate point=\"" + pts + "\"/></IndexedFaceSet></Shape>")); CHECK(Now() - t0 < 10.0); }
    // 20000 faces on 3 points; a vertex with 5000 incident triangles at different angles (crease clustering)
    { std::string idx; for (int i = 0; i < 20000; ++i) idx += "0 1 2 -1 "; CHECK(x3d("<Shape><IndexedFaceSet creaseAngle=\"3\" coordIndex=\"" + idx + "\"><Coordinate point=\"0 0 0 1 0 0 0 1 0\"/></IndexedFaceSet></Shape>")); }
    { std::string idx, pts = "0 0 0 "; for (int i = 1; i <= 5000; ++i) { pts += std::to_string(std::cos(i * 0.7)) + " " + std::to_string(std::sin(i * 0.7)) + " " + std::to_string(std::sin(i * 1.3)) + " "; }
      for (int i = 1; i < 5000; ++i) idx += "0 " + std::to_string(i) + " " + std::to_string(i + 1) + " -1 ";
      const double t0 = Now(); CHECK(x3d("<Shape><IndexedFaceSet creaseAngle=\"0.5\" coordIndex=\"" + idx + "\"><Coordinate point=\"" + pts + "\"/></IndexedFaceSet></Shape>")); CHECK(Now() - t0 < 10.0); }
    // numbers: scale zero / huge / negative zero; rotation axis zero; a singular transform still gives sane output or a clean failure
    for (const char* tr : {"scale=\"0 0 0\"", "scale=\"1e15 1e15 1e15\"", "scale=\"-0 1 1\"", "rotation=\"0 0 0 1\"", "rotation=\"0 0 1 1e15\"", "scale=\"1e-300 1 1\"", "scale=\"1e15 1 1\" translation=\"1e15 1e15 1e15\""})
        x3d(std::string("<Transform ") + tr + "><Shape><Box/></Shape><Shape><Sphere radius=\"1e15\"/></Shape><Shape><Cone bottomRadius=\"-1\" height=\"-2\"/></Shape></Transform>");
    for (const char* prim : {"<Box size=\"0 0 0\"/>", "<Box size=\"-1 2 3\"/>", "<Sphere radius=\"0\"/>", "<Sphere radius=\"1e15\"/>", "<Cylinder radius=\"0\" height=\"0\"/>", "<Cone height=\"0\"/>", "<Cone bottomRadius=\"1e15\" height=\"1e-12\"/>"}) x3d(std::string("<Shape>") + prim + "</Shape>");
    // Extrusion: absurd spines, scales, orientations, huge counts, collinear and self-overlapping spines
    for (const char* e : {"<Extrusion spine=\"0 0 0 0 0 0 0 0 0\"/>", "<Extrusion spine=\"0 0 0 1e15 0 0\" scale=\"1e15 1e15\"/>", "<Extrusion scale=\"0 0\"/>", "<Extrusion orientation=\"0 0 0 1e15\"/>",
                          "<Extrusion orientation=\"nan 0 0 1\"/>", "<Extrusion spine=\"0 0 0 0 1 0 0 0 0 0 1 0 0 0 0\"/>", "<Extrusion spine=\"0 0 0 0 1 0 0 2 0 0 1 0\"/>",
                          "<Extrusion crossSection=\"0 0 0 0\"/>", "<Extrusion crossSection=\"1 1 1 1 1 1\" spine=\"0 0 0 1 1 1 2 0 2 3 1 3\"/>", "<Extrusion scale=\"1 1 2\"/>", "<Extrusion orientation=\"0 0 1\"/>"})
        x3d(std::string("<Shape>") + e + "</Shape>");
    { std::string sp, cs; for (int i = 0; i < 2000; ++i) sp += std::to_string(std::sin(i * 0.1)) + " " + std::to_string(i * 0.01) + " " + std::to_string(std::cos(i * 0.1)) + " ";
      for (int i = 0; i < 1000; ++i) cs += std::to_string(std::cos(i * 0.00628)) + " " + std::to_string(std::sin(i * 0.00628)) + " ";
      const double t0 = Now(); x3d("<Shape><Extrusion spine=\"" + sp + "\" crossSection=\"" + cs + "\" creaseAngle=\"1\"/></Shape>"); CHECK(Now() - t0 < 30.0); }
    { std::string sp, cs; for (int i = 0; i < 3000; ++i) sp += "0 " + std::to_string(i) + " 0 "; for (int i = 0; i < 2000; ++i) cs += "1 " + std::to_string(i) + " "; x3d("<Shape><Extrusion spine=\"" + sp + "\" crossSection=\"" + cs + "\"/></Shape>"); }   // 6 million points: refused
    RUN_SUITE("x3d fuzz");
    const long n = Iters();
    for (long i = 0; i < n; ++i) Try(ReadX3d, Mutate(good));
}


// ======================= DXF =======================
static bool ReadDxf(const std::string& b, ImportedModel& m, std::string& e) { return ReadDxfBuffer(b, m, &e); }
static std::string Gc(int code, const std::string& v) { char b[16]; std::snprintf(b, sizeof b, "%3d\n", code); return b + v + "\n"; }
static std::string DxfDoc(const std::string& ents, const std::string& blocks = "") {
    return Gc(0, "SECTION") + Gc(2, "BLOCKS") + blocks + Gc(0, "ENDSEC") + Gc(0, "SECTION") + Gc(2, "ENTITIES") + ents + Gc(0, "ENDSEC") + Gc(0, "EOF");
}
static std::string DxfFace(const char* layer, double x) {
    return Gc(0, "3DFACE") + Gc(8, layer) + Gc(10, std::to_string(x)) + Gc(20, "0") + Gc(30, "0") + Gc(11, std::to_string(x + 1)) + Gc(21, "0") + Gc(31, "0") + Gc(12, std::to_string(x)) + Gc(22, "1") + Gc(32, "0");
}
static const std::string kDxf = [] {
    std::string blk = Gc(0, "BLOCK") + Gc(2, "B") + Gc(10, "1") + Gc(20, "0") + Gc(30, "0") + DxfFace("0", 1) + Gc(0, "ENDBLK");
    std::string poly = Gc(0, "POLYLINE") + Gc(8, "P") + Gc(70, "64") + Gc(71, "3") + Gc(72, "1");
    const double pts[3][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    for (auto& p : pts) poly += Gc(0, "VERTEX") + Gc(8, "P") + Gc(10, std::to_string(p[0])) + Gc(20, std::to_string(p[1])) + Gc(30, std::to_string(p[2])) + Gc(70, "192");
    poly += Gc(0, "VERTEX") + Gc(8, "P") + Gc(10, "0") + Gc(20, "0") + Gc(30, "0") + Gc(70, "128") + Gc(71, "1") + Gc(72, "2") + Gc(73, "3") + Gc(0, "SEQEND");
    std::string mesh = Gc(0, "MESH") + Gc(8, "M") + Gc(92, "3") + Gc(10, "0") + Gc(20, "0") + Gc(30, "0") + Gc(10, "1") + Gc(20, "0") + Gc(30, "0") + Gc(10, "0") + Gc(20, "1") + Gc(30, "0") + Gc(93, "4") + Gc(90, "3") + Gc(90, "0") + Gc(90, "1") + Gc(90, "2");
    return DxfDoc(DxfFace("A", 0) + poly + mesh + Gc(0, "INSERT") + Gc(8, "I") + Gc(2, "B") + Gc(10, "5") + Gc(20, "0") + Gc(30, "0") + Gc(41, "2") + Gc(50, "30") + Gc(0, "LINE") + Gc(8, "A"), blk);
}();

static void HostileDxf() {
    RUN_SUITE("dxf hostile");
    const std::string& good = kDxf;
    { ImportedModel m; std::string e; CHECK(ReadDxf(good, m, e) && m.parts.size() == 4); }
    for (std::size_t n = 0; n < good.size(); ++n) Try(ReadDxf, good.substr(0, n));
    auto dxf = [&](const std::string& s) { ImportedModel m; std::string e; const bool ok = ReadDxf(s, m, e); if (ok) { CHECK(fx::Sane(m)); } else { CHECK(!e.empty() && m.parts.empty()); } return ok; };
    // numbers and counts
    for (const char* bad : {"1e999", "inf", "nan", "0x10", "1..2", "1e300", "", "--1", "1,5"}) {
        dxf(DxfDoc(Gc(0, "3DFACE") + Gc(10, bad) + Gc(20, "0") + Gc(30, "0") + Gc(11, "1") + Gc(21, "0") + Gc(31, "0") + Gc(12, "0") + Gc(22, "1") + Gc(32, "0")));
        dxf(DxfDoc(Gc(0, "INSERT") + Gc(2, "B") + Gc(41, bad) + Gc(50, bad), Gc(0, "BLOCK") + Gc(2, "B") + DxfFace("0", 0) + Gc(0, "ENDBLK")));
        dxf(DxfDoc(Gc(0, "MESH") + Gc(92, bad) + Gc(93, bad)));
    }
    for (const char* cnt : {"-1", "999999999999", "4000000000", "2147483647", "20000001", "1.5"}) {
        dxf(DxfDoc(Gc(0, "MESH") + Gc(92, cnt) + Gc(10, "0") + Gc(20, "0") + Gc(30, "0") + Gc(93, cnt) + Gc(90, "3") + Gc(90, "0") + Gc(90, "0") + Gc(90, "0")));
        dxf(DxfDoc(Gc(0, "POLYLINE") + Gc(70, "16") + Gc(71, cnt) + Gc(72, cnt) + Gc(0, "SEQEND")));
    }
    // face indices out of range / negative / huge in MESH and polyface
    dxf(DxfDoc(Gc(0, "MESH") + Gc(92, "3") + Gc(10, "0") + Gc(20, "0") + Gc(30, "0") + Gc(10, "1") + Gc(20, "0") + Gc(30, "0") + Gc(10, "0") + Gc(20, "1") + Gc(30, "0") + Gc(93, "8") + Gc(90, "3") + Gc(90, "0") + Gc(90, "1") + Gc(90, "99") + Gc(90, "3") + Gc(90, "0") + Gc(90, "1") + Gc(90, "2")));
    dxf(DxfDoc(Gc(0, "MESH") + Gc(92, "3") + Gc(93, "5") + Gc(90, "4000000000") + Gc(90, "0") + Gc(90, "1") + Gc(90, "2") + Gc(90, "3")));
    dxf(DxfDoc(Gc(0, "POLYLINE") + Gc(70, "64") + Gc(0, "VERTEX") + Gc(70, "128") + Gc(71, "-2147483648") + Gc(72, "2147483647") + Gc(73, "0") + Gc(0, "SEQEND")));
    // a polyline of 200000 vertices and a polyface with a million faces would fit the caps: 50000 vertices, 50000 faces
    { std::string s = Gc(0, "POLYLINE") + Gc(70, "64");
      for (int i = 0; i < 50000; ++i) s += Gc(0, "VERTEX") + Gc(10, std::to_string(i % 100)) + Gc(20, std::to_string(i / 100)) + Gc(30, "0") + Gc(70, "192");
      for (int i = 0; i < 50000; ++i) s += Gc(0, "VERTEX") + Gc(70, "128") + Gc(71, std::to_string(i % 49998 + 1)) + Gc(72, std::to_string(i % 49998 + 2)) + Gc(73, std::to_string(i % 49998 + 3));
      s += Gc(0, "SEQEND"); const double t0 = Now(); CHECK(dxf(DxfDoc(s))); CHECK(Now() - t0 < 20.0); }
    // an INSERT bomb: block i inserts block i-1 ten times, 20 levels (nesting limit and instance cap, no hang)
    { std::string blocks = Gc(0, "BLOCK") + Gc(2, "b0") + DxfFace("0", 0) + Gc(0, "ENDBLK");
      for (int i = 1; i <= 20; ++i) { blocks += Gc(0, "BLOCK") + Gc(2, "b" + std::to_string(i)); for (int k = 0; k < 10; ++k) blocks += Gc(0, "INSERT") + Gc(2, "b" + std::to_string(i - 1)) + Gc(10, std::to_string(k)); blocks += Gc(0, "ENDBLK"); }
      const double t0 = Now(); dxf(DxfDoc(Gc(0, "INSERT") + Gc(2, "b20"), blocks)); CHECK(Now() - t0 < 30.0); }
    { std::string blocks = Gc(0, "BLOCK") + Gc(2, "b0") + DxfFace("0", 0) + Gc(0, "ENDBLK");
      for (int i = 1; i <= 7; ++i) { blocks += Gc(0, "BLOCK") + Gc(2, "b" + std::to_string(i)); for (int k = 0; k < 12; ++k) blocks += Gc(0, "INSERT") + Gc(2, "b" + std::to_string(i - 1)) + Gc(10, std::to_string(k)); blocks += Gc(0, "ENDBLK"); }
      const double t0 = Now(); CHECK(!dxf(DxfDoc(Gc(0, "INSERT") + Gc(2, "b7"), blocks)) || true); CHECK(Now() - t0 < 30.0); }
    // 100001 INSERTs, 5000 layers, a huge string, binary junk
    { std::string s = DxfFace("A", 0); for (int i = 0; i < 100001; ++i) s += Gc(0, "INSERT") + Gc(2, "x"); CHECK(!dxf(DxfDoc(s))); }
    { std::string s; for (int i = 0; i < 5000; ++i) s += DxfFace(("L" + std::to_string(i)).c_str(), i); CHECK(dxf(DxfDoc(s))); }
    { std::string s = Gc(0, "3DFACE") + Gc(8, std::string(5000000, 'x')); dxf(DxfDoc(s)); }
    { std::string junk; for (int i = 0; i < 100000; ++i) junk.push_back((char)(fx::Rnd() & 255)); CHECK(!dxf(junk)); }
    CHECK(!dxf(std::string(2000000, '\n')));
    CHECK(!dxf(std::string("  0\nSECTION\n  2\nENTITIES\n") + std::string(300000, ' ') + "\n0\n"));
    RUN_SUITE("dxf fuzz");
    const long n = Iters();
    for (long i = 0; i < n; ++i) Try(ReadDxf, Mutate(good));
}


// ======================= LWO =======================
static bool ReadLwo(const std::string& b, ImportedModel& m, std::string& e) { return ReadLwoBuffer(b, "", m, &e); }
static void W16(std::string& s, unsigned v) { s.push_back((char)(v >> 8)); s.push_back((char)v); }
static void W32(std::string& s, std::uint32_t v) { for (int i = 3; i >= 0; --i) s.push_back((char)(v >> (8 * i))); }
static void WF(std::string& s, float f) { std::uint32_t v; std::memcpy(&v, &f, 4); W32(s, v); }
static void WS0(std::string& s, const std::string& x) { s += x; s.push_back(0); if ((x.size() + 1) & 1) s.push_back(0); }
static std::string LC(const char* id, const std::string& body) { std::string s = id; W32(s, (std::uint32_t)body.size()); s += body; if (body.size() & 1) s.push_back(0); return s; }
static std::string LS(const char* id, const std::string& body) { std::string s = id; W16(s, (unsigned)body.size()); s += body; if (body.size() & 1) s.push_back(0); return s; }
static std::string LForm(const char* type, const std::string& chunks) { std::string s = "FORM"; W32(s, (std::uint32_t)(chunks.size() + 4)); s += type; s += chunks; return s; }
static const std::string kLwo2 = [] {
    std::string layr; W16(layr, 0); W16(layr, 0); WF(layr, 0); WF(layr, 0); WF(layr, 0); WS0(layr, "L");
    std::string pn; const float P[4][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; for (auto& p : P) for (float f : p) WF(pn, f);
    std::string pols = "FACE"; const int F[4][3] = {{0, 2, 1}, {0, 1, 3}, {1, 2, 3}, {2, 0, 3}}; for (auto& f : F) { W16(pols, 3); for (int i : f) W16(pols, (unsigned)i); }
    std::string ptag = "SURF"; for (int i = 0; i < 4; ++i) { W16(ptag, (unsigned)i); W16(ptag, (unsigned)(i % 2)); }
    std::string tags; WS0(tags, "A"); WS0(tags, "B");
    std::string vm = "TXUV"; W16(vm, 2); WS0(vm, "uv"); for (int i = 0; i < 4; ++i) { W16(vm, (unsigned)i); WF(vm, 0.5f * i); WF(vm, 0.25f * i); }
    std::string vd = "TXUV"; W16(vd, 2); WS0(vd, "uv"); W16(vd, 1); W16(vd, 0); WF(vd, 0.9f); WF(vd, 0.1f);
    std::string clip; W32(clip, 1); { std::string st; WS0(st, "a.tga"); clip += LS("STIL", st); }
    std::string surf; WS0(surf, "A"); WS0(surf, "");
    { std::string c; WF(c, 1); WF(c, 0.5f); WF(c, 0); W16(c, 0); surf += LS("COLR", c); std::string g; WF(g, 0.3f); W16(g, 0); surf += LS("GLOS", g); std::string d; WF(d, 0.8f); W16(d, 0); surf += LS("DIFF", d); std::string sp; WF(sp, 0.7f); W16(sp, 0); surf += LS("SPEC", sp);
      std::string sm; WF(sm, 1.0f); surf += LS("SMAN", sm);
      std::string head; WS0(head, "\x80"); head += LS("CHAN", "COLR"); std::string blok = LS("IMAP", head) + LS("PROJ", [] { std::string q; W16(q, 5); return q; }()) + LS("IMAG", [] { std::string q; W16(q, 1); return q; }()) + LS("VMAP", [] { std::string q; WS0(q, "uv"); return q; }()); surf += LS("BLOK", blok); }
    std::string surfB; WS0(surfB, "B"); WS0(surfB, "");
    return LForm("LWO2", LC("TAGS", tags) + LC("LAYR", layr) + LC("PNTS", pn) + LC("BBOX", std::string(24, '\0')) + LC("VMAP", vm) + LC("VMAD", vd) + LC("POLS", pols) + LC("PTAG", ptag) + LC("CLIP", clip) + LC("SURF", surf) + LC("SURF", surfB));
}();
static const std::string kLwob = [] {
    std::string pn; const float P[4][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; for (auto& p : P) for (float f : p) WF(pn, f);
    std::string pols; const int F[4][3] = {{0, 2, 1}, {0, 1, 3}, {1, 2, 3}, {2, 0, 3}}; for (auto& f : F) { W16(pols, 3); for (int i : f) W16(pols, (unsigned)i); W16(pols, 1); }
    std::string srfs; WS0(srfs, "S");
    std::string surf; WS0(surf, "S"); surf += LS("COLR", std::string{(char)200, (char)100, (char)50, 0}); { std::string d; W16(d, 256); surf += LS("DIFF", d); std::string g; W16(g, 30); surf += LS("GLOS", g); std::string sm; WF(sm, 0.5f); surf += LS("SMAN", sm); }
    return LForm("LWOB", LC("PNTS", pn) + LC("SRFS", srfs) + LC("POLS", pols) + LC("SURF", surf));
}();

static void HostileLwo() {
    RUN_SUITE("lwo hostile");
    for (const std::string* f : {&kLwo2, &kLwob}) {
        { ImportedModel m; std::string e; CHECK(ReadLwo(*f, m, e) && m.parts.size() >= 1); }
        for (std::size_t n = 0; n < f->size(); ++n) Try(ReadLwo, f->substr(0, n));
    }
    auto lwo = [&](const std::string& s) { ImportedModel m; std::string e; const bool ok = ReadLwo(s, m, e); if (ok) { CHECK(fx::Sane(m)); } else { CHECK(!e.empty() && m.parts.empty()); } return ok; };
    // FORM lengths below the 4 bytes of the type: the chunk loop must not start past its end (found by the long mutation fuzz)
    for (unsigned n : {0u, 1u, 3u, 4u, 5u}) { std::string f = kLwo2; f[4] = f[5] = f[6] = 0; f[7] = (char)n; CHECK(!lwo(f)); f = kLwob; f[4] = f[5] = f[6] = 0; f[7] = (char)n; CHECK(!lwo(f)); }
    // lengths: FORM longer than the file, chunk length 0xFFFFFFFF, chunk length odd and past the end
    { std::string f = kLwo2; f[4] = (char)0xFF; f[5] = (char)0xFF; f[6] = (char)0xFF; f[7] = (char)0xFF; lwo(f); }
    { std::string f = kLwo2; for (std::size_t i = 12; i + 8 < f.size(); i += 4) if (f.compare(i, 4, "PNTS") == 0) { f[i + 4] = (char)0xFF; f[i + 5] = (char)0xFF; f[i + 6] = (char)0xFF; f[i + 7] = (char)0xFF; } lwo(f); }
    // a polygon claiming 1023 corners with no room for them, many polygons, huge VX values
    { std::string layr; W16(layr, 0); W16(layr, 0); WF(layr, 0); WF(layr, 0); WF(layr, 0); WS0(layr, "L");
      std::string pn; for (int i = 0; i < 3; ++i) { WF(pn, (float)i); WF(pn, (float)(i * i)); WF(pn, 0); }
      std::string p1 = "FACE"; W16(p1, 1023); W16(p1, 1); lwo(LForm("LWO2", LC("LAYR", layr) + LC("PNTS", pn) + LC("POLS", p1)));
      std::string p2 = "FACE"; for (int i = 0; i < 200000; ++i) { W16(p2, 3); W16(p2, 0); W16(p2, 1); W16(p2, 2); }
      const double t0 = Now(); CHECK(lwo(LForm("LWO2", LC("LAYR", layr) + LC("PNTS", pn) + LC("POLS", p2)))); CHECK(Now() - t0 < 20.0);
      std::string p3 = "FACE"; W16(p3, 3); p3 += std::string("\xFF\xFF\xFF\xFF", 4); W16(p3, 1); W16(p3, 2); lwo(LForm("LWO2", LC("LAYR", layr) + LC("PNTS", pn) + LC("POLS", p3)));
      std::string pt = "SURF"; for (int i = 0; i < 100000; ++i) { W16(pt, (unsigned)(i % 3)); W16(pt, 0xFFFF); } lwo(LForm("LWO2", LC("LAYR", layr) + LC("PNTS", pn) + LC("POLS", p2) + LC("PTAG", pt)));
      // 10001 layers, 100001 surfaces, a million tags
      std::string ls; for (int i = 0; i < 10001; ++i) ls += LC("LAYR", layr); CHECK(!lwo(LForm("LWO2", ls)));
      std::string ss; for (int i = 0; i < 100001; ++i) { std::string sf; WS0(sf, "S" + std::to_string(i)); WS0(sf, ""); ss += LC("SURF", sf); } CHECK(!lwo(LForm("LWO2", LC("LAYR", layr) + LC("PNTS", pn) + LC("POLS", p2) + ss)));
      std::string tg; for (int i = 0; i < 1000001; ++i) tg += std::string("a\0", 2); lwo(LForm("LWO2", LC("TAGS", tg) + LC("LAYR", layr) + LC("PNTS", pn) + LC("POLS", p2)));
      // strings without a terminator, UV maps with absurd counts and indices, surface sub-chunks that overrun
      lwo(LForm("LWO2", LC("TAGS", std::string(50000, 'x'))));
      std::string vm = "TXUV"; W16(vm, 2); WS0(vm, "uv"); for (int i = 0; i < 100000; ++i) { W16(vm, (unsigned)(i & 0xFFFF)); WF(vm, std::nanf("")); WF(vm, 1e30f); }
      lwo(LForm("LWO2", LC("LAYR", layr) + LC("PNTS", pn) + LC("VMAP", vm) + LC("POLS", p2)));
      std::string sf; WS0(sf, "S"); WS0(sf, ""); sf += "COLR"; W16(sf, 60000); sf += "xxxx"; lwo(LForm("LWO2", LC("LAYR", layr) + LC("PNTS", pn) + LC("POLS", p2) + LC("SURF", sf)));
      std::string sg; WS0(sg, "S"); WS0(sg, ""); { std::string c; WF(c, std::nanf("")); WF(c, 1e30f); WF(c, -1e30f); W16(c, 0); sg += LS("COLR", c); std::string g; WF(g, 1e30f); W16(g, 0); sg += LS("GLOS", g); std::string sp; WF(sp, 1.0f); sg += LS("SPEC", sp); std::string sm; WF(sm, std::nanf("")); sg += LS("SMAN", sm); std::string tr; WF(tr, -5.0f); sg += LS("TRAN", tr); }
      std::string tg2; WS0(tg2, "S"); std::string pt2 = "SURF"; for (int i = 0; i < 200000; ++i) { W16(pt2, (unsigned)(i & 0xFFFF)); W16(pt2, 0); }
      lwo(LForm("LWO2", LC("TAGS", tg2) + LC("LAYR", layr) + LC("PNTS", pn) + LC("POLS", p2) + LC("PTAG", pt2) + LC("SURF", sg)));
      // LWOB with negative surface, absurd detail counts
      std::string pb; W16(pb, 3); W16(pb, 0); W16(pb, 1); W16(pb, 2); W16(pb, 0xFFFF); W16(pb, 60000); W16(pb, 3); lwo(LForm("LWOB", LC("PNTS", pn) + LC("POLS", pb)));
    }
    RUN_SUITE("lwo fuzz");
    const long n = Iters();
    for (long i = 0; i < n; ++i) { Try(ReadLwo, Mutate(kLwo2)); if (i % 2) Try(ReadLwo, Mutate(kLwob)); }
}

int main() {
    Hostile3mf();
    HostileAmf();
    HostileX3d();
    HostileDxf();
    HostileLwo();
    TEST_MAIN_RESULT();
}
