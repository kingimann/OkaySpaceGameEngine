// Hostile input and mutation fuzz for the batch-D own readers: AC3D, OGRE XML, XGL / ZGL, Irrlicht scene, NFF. Needs only the
// reader sources and okay/Render/ModelData.hpp (and the engine's Core/ImageDecode*.cpp for ZGL's inflate), so it builds on its own, e.g.
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all -DOKAY_STANDALONE_TEST
//       -Iengine/include -Iengine/src/Core -Itests tests/test_batchd_hostile.cpp engine/src/Render/{Ac3d,OgreXml,Xgl,IrrScene,Nff}Reader.cpp
//       engine/src/Core/ImageDecode.cpp engine/src/Core/ImageDecodeJpeg.cpp -o hostile
// OKAY_BATCHD_FUZZ_ITERS=200000 for a long run (per reader). All fixtures here are SYNTHETIC.
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "batchd_fixtures.hpp"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace okay;

static long Iters() { const char* e = std::getenv("OKAY_BATCHD_FUZZ_ITERS"); return e ? std::atol(e) : 3000; }
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
    case 4: { const char* t[] = {"0", "-1", "99999999999", "1e308", "nan", "-0", "4294967295", "2147483648", " ", ",", "-", "e", ".", "inf", "0x7fffffff", "1e999"}; s.replace(p, std::min<std::size_t>(3, s.size() - p), t[fx::Rnd() % 16]); break; }
    default: s.resize(p); break;
    }
    return s;
}
// Every truncation, then a mutation sweep.
template <class F> static void Sweep(const char* name, F&& read, const std::string& fixture, long iters) {
    const double t0 = Now();
    for (std::size_t n = 0; n < fixture.size(); ++n) Try(read, fixture.substr(0, n));
    for (long i = 0; i < iters; ++i) Try(read, Mutate(fixture));
    std::fprintf(stderr, "  %s: %zu truncations + %ld mutations in %.1f s\n", name, fixture.size(), iters, Now() - t0);
}
template <class F> static double Timed(F&& f) { const double t = Now(); f(); return Now() - t; }

static std::string Repeat(const std::string& s, std::size_t n) { std::string r; r.reserve(s.size() * n); for (std::size_t i = 0; i < n; ++i) r += s; return r; }

// ======================= AC3D =======================
static bool ReadAc(const std::string& b, ImportedModel& m, std::string& e) { return ReadAc3dBuffer(b, "", m, &e); }
static void Ac3d() {
    const std::string good = fx::BuildAc3d();
    { ImportedModel m; std::string e; CHECK(ReadAc(good, m, e) && fx::Sane(m)); }
    Sweep("ac3d", ReadAc, good, Iters());
    Sweep("ac3d cube subdiv", ReadAc, fx::BuildAc3dCube(1), Iters() / 4);
    const std::string head = "AC3Db\nMATERIAL \"m\" rgb 1 1 1 amb 0 0 0 emis 0 0 0 spec 0 0 0 shi 0 trans 0\nOBJECT world\nkids 1\nOBJECT poly\n";
    // counts at, past and far past the caps with tiny text: refused or ended, never allocated
    for (const char* c : {"5000000", "5000001", "99999999999", "-1", "2147483647", "4294967296", "1e9", "0x10"}) {
        Try(ReadAc, head + "numvert " + c + "\n0 0 0\n");
        Try(ReadAc, head + "numvert 3\n0 0 0\n1 0 0\n0 1 0\nnumsurf " + c + "\nSURF 0x0\nmat 0\nrefs 3\n0 0 0\n1 0 0\n2 0 0\n");
        Try(ReadAc, head + "numvert 3\n0 0 0\n1 0 0\n0 1 0\nnumsurf 1\nSURF 0x0\nmat 0\nrefs " + c + "\n0 0 0\n1 0 0\n2 0 0\n");
        Try(ReadAc, head + "kids " + c + "\n");
        Try(ReadAc, head + "data " + c + "\nabc\n");
        Try(ReadAc, head + "subdiv " + c + "\nnumvert 3\n0 0 0\n1 0 0\n0 1 0\nnumsurf 1\nSURF 0x0\nmat 0\nrefs 3\n0 0 0\n1 0 0\n2 0 0\nkids 0\n");
        Try(ReadAc, head + "crease " + c + "\nnumvert 3\n0 0 0\n1 0 0\n0 1 0\nnumsurf 1\nSURF 0x10\nmat 0\nrefs 3\n0 0 0\n1 0 0\n2 0 0\nkids 0\n");
    }
    { ImportedModel m; std::string e; CHECK(!ReadAc(head + "numvert 5000000\n0 0 0\n", m, e) && !e.empty()); }
    // NaN, infinity, huge and hex numbers in every kind of field
    for (const char* t : {"nan", "inf", "-inf", "1e999", "1e308", "-1e308", "0x1p3", "1e15", "1e16", "1,5", "--1", "."}) {
        const std::string v = std::string(t);
        Try(ReadAc, head + "loc " + v + " 0 0\nnumvert 3\n" + v + " 0 0\n1 0 0\n0 1 0\nnumsurf 1\nSURF 0x0\nmat 0\nrefs 3\n0 0 0\n1 0 0\n2 0 0\nkids 0\n");
        Try(ReadAc, head + "rot " + v + " 0 0 0 1 0 0 0 1\nnumvert 3\n0 0 0\n1 0 0\n0 1 0\nnumsurf 1\nSURF 0x0\nmat 0\nrefs 3\n0 0 0\n1 0 0\n2 0 0\nkids 0\n");
        Try(ReadAc, head + "texrep " + v + " " + v + "\ntexture \"a\"\nnumvert 3\n0 0 0\n1 0 0\n0 1 0\nnumsurf 1\nSURF 0x0\nmat 0\nrefs 3\n0 0 " + v + "\n1 0 0\n2 0 0\nkids 0\n");
        Try(ReadAc, "AC3Db\nMATERIAL \"m\" rgb " + v + " 0 0 amb 0 0 0 emis 0 0 0 spec 0 0 0 shi " + v + " trans " + v + "\nOBJECT world\nkids 0\n");
    }
    // a singular rotation matrix, a mirror and a huge scale in rot
    for (const char* r : {"0 0 0 0 0 0 0 0 0", "1 0 0 0 1 0 0 0 -1", "1e14 0 0 0 1e14 0 0 0 1e14", "1 1 1 1 1 1 1 1 1"})
        Try(ReadAc, head + "rot " + r + "\nnumvert 3\n0 0 0\n1 0 0\n0 1 0\nnumsurf 1\nSURF 0x0\nmat 0\nrefs 3\n0 0 0\n1 0 0\n2 0 0\nkids 0\n");
    // 5,000 levels of nesting (the cap is 512), 20,000 objects, 20,000 materials, a very long line
    { std::string s = "AC3Db\n"; for (int i = 0; i < 5000; ++i) s += "OBJECT group\nkids 1\n"; s += "OBJECT poly\nkids 0\n";
      ImportedModel m; std::string e; CHECK(!ReadAc(s, m, e) && e.find("deeper") != std::string::npos); }
    { std::string s = "AC3Db\nOBJECT world\nkids 20000\n"; s += Repeat("OBJECT group\nkids 0\n", 20000); Try(ReadAc, s); }
    { std::string s = "AC3Db\n"; s += Repeat("MATERIAL \"m\" rgb 1 1 1\n", 20000); s += "OBJECT world\nkids 0\n"; Try(ReadAc, s); }
    Try(ReadAc, head + "numvert 1\n" + std::string(3000000, '1') + " 0 0\n");
    // a big valid mesh: 60,000 triangles in a fan and in a strip, and a subdivision asked 1,000,000 levels deep (clamped)
    {   std::string s = head + "numvert 60002\n"; for (int i = 0; i < 60002; ++i) s += std::to_string(i % 100) + " " + std::to_string(i / 100) + " " + std::to_string(i % 7) + "\n";
        s += "numsurf 1\nSURF 0x14\nmat 0\nrefs 60002\n"; for (int i = 0; i < 60002; ++i) s += std::to_string(i) + " 0 0\n"; s += "kids 0\n";
        ImportedModel m; std::string e; const double t = Timed([&] { ReadAc(s, m, e); });
        CHECK(!e.empty() || fx::Sane(m)); CHECK(t < 20.0); }
    {   const double t = Timed([&] { ImportedModel m; std::string e; std::string s = fx::BuildAc3dCube(0); s.replace(s.find("name \"cube\"\n"), 12, "name \"cube\"\nsubdiv 1000000\n"); const bool ok = ReadAc(s, m, e); CHECK(ok && fx::Sane(m)); CHECK(m.parts.size() == 1 && m.parts[0].mesh.triangles.size() > 100000); });
        CHECK(t < 60.0); }
    // an index at 4294967295, negative indices, a polygon that names one vertex 1,000 times
    Try(ReadAc, head + "numvert 3\n0 0 0\n1 0 0\n0 1 0\nnumsurf 1\nSURF 0x0\nmat 0\nrefs 3\n4294967295 0 0\n1 0 0\n2 0 0\nkids 0\n");
    Try(ReadAc, head + "numvert 3\n0 0 0\n1 0 0\n0 1 0\nnumsurf 1\nSURF 0x0\nmat 0\nrefs 3\n-1 0 0\n1 0 0\n2 0 0\nkids 0\n");
    Try(ReadAc, head + "numvert 3\n0 0 0\n1 0 0\n0 1 0\nnumsurf 1\nSURF 0x0\nmat 0\nrefs 1000\n" + Repeat("1 0 0\n", 1000) + "kids 0\n");
    Try(ReadAc, head + "numvert 3\n0 0 0\n1 0 0\n0 1 0\nnumsurf 1\nSURF 0x4\nmat 0\nrefs 1000\n" + Repeat("1 0 0\n", 1000) + "kids 0\n");   // a strip of one vertex: all degenerate
    Try(ReadAc, head + "numvert 3\n0 0 0\n1 0 0\n0 1 0\nnumsurf 1\nSURF 0x0\nmat 2147483647\nrefs 3\n0 0 0\n1 0 0\n2 0 0\nkids 0\n");
    Try(ReadAc, head + "numvert 3\n0 0 0 nan nan nan\n1 0 0 0 0 0\n0 1 0 inf 0 0\nnumsurf 1\nSURF 0x10\nmat 0\nrefs 3\n0 0 0\n1 0 0\n2 0 0\nkids 0\n");
}

// ======================= OGRE XML =======================
static bool ReadOg(const std::string& b, ImportedModel& m, std::string& e) { return ReadOgreXmlBuffer(b, "", m, &e, nullptr); }
static void Ogre() {
    const std::string good = fx::BuildOgreXml();
    { ImportedModel m; std::string e; CHECK(ReadOg(good, m, e) && fx::Sane(m)); }
    Sweep("ogre", ReadOg, good, Iters());
    std::string crlf; for (char c : good) { if (c == '\n') crlf += "\r\n"; else crlf += c; }
    Sweep("ogre crlf", ReadOg, crlf, Iters() / 4);
    // counts at, past and far past the caps with tiny text: refused without allocating
    for (const char* c : {"8000000", "8000001", "99999999999", "-1", "4294967296", "nan", "1e999"}) {
        std::string s = good; s.replace(s.find("vertexcount=\"4\""), 15, std::string("vertexcount=\"") + c + "\"");
        Try(ReadOg, s);
        s = good; s.replace(s.find("<faces count=\"2\">"), 17, std::string("<faces count=\"") + c + "\">");
        Try(ReadOg, s);
    }
    { ImportedModel m; std::string e; std::string s = good; s.replace(s.find("vertexcount=\"4\""), 15, "vertexcount=\"8000000\""); CHECK(!ReadOg(s, m, e) && !e.empty()); }
    // NaN, infinity and huge coordinates, bad indices
    for (const char* t : {"nan", "inf", "1e999", "1e308", "-1e308", "0x10", "1e15", "1e16"}) {
        std::string s = good; s.replace(s.find("x=\"2\" y=\"0\" z=\"0\""), 17, std::string("x=\"") + t + "\" y=\"0\" z=\"0\""); Try(ReadOg, s);
        s = good; s.replace(s.find("u=\"1\" v=\"0\""), 11, std::string("u=\"") + t + "\" v=\"0\""); Try(ReadOg, s);
        s = good; s.replace(s.find("v1=\"0\" v2=\"1\""), 13, std::string("v1=\"") + t + "\" v2=\"1\""); Try(ReadOg, s);
    }
    // a billion-laughs entity, deep nesting, 100,000 empty submeshes, 100,000 submesh names, a 3 MB number
    Try(ReadOg, "<!DOCTYPE m [<!ENTITY a \"aaaa\"><!ENTITY b \"&a;&a;&a;&a;\">]><mesh><submeshes/></mesh>");
    Try(ReadOg, Repeat("<a>", 5000) + Repeat("</a>", 5000));
    { std::string s = "<mesh><submeshes>" + Repeat("<submesh material=\"m\"/>", 100000) + "</submeshes></mesh>"; ImportedModel m; std::string e; CHECK(!ReadOg(s, m, e)); }
    { std::string s = "<mesh><submeshes><submesh><geometry vertexcount=\"1\"><vertexbuffer><vertex><position x=\"1\" y=\"1\" z=\"" + std::string(3000000, '1') + "\"/></vertex></vertexbuffer></geometry><faces count=\"0\"/></submesh></submeshes></mesh>"; Try(ReadOg, s); }
    { std::string s = good; s.insert(s.find("<submeshes>") + 11, Repeat("<submesh usesharedvertices=\"true\"><faces count=\"1\"><face v1=\"0\" v2=\"1\" v3=\"2\"/></faces></submesh>", 50000)); ImportedModel m; std::string e; const double t = Timed([&] { ReadOg(s, m, e); }); CHECK(t < 20.0); CHECK(e.empty() ? fx::Sane(m) : true); }
    // a big valid mesh: 60,000 triangles over 30,000 vertices
    {   std::string s = "<mesh><submeshes><submesh material=\"m\"><faces count=\"60000\">"; for (int i = 0; i < 60000; ++i) s += "<face v1=\"" + std::to_string(i % 30000) + "\" v2=\"" + std::to_string((i + 1) % 30000) + "\" v3=\"" + std::to_string((i + 2) % 30000) + "\"/>";
        s += "</faces><geometry vertexcount=\"30000\"><vertexbuffer positions=\"true\">"; for (int i = 0; i < 30000; ++i) s += "<vertex><position x=\"" + std::to_string(i % 173) + "\" y=\"" + std::to_string(i / 173) + "\" z=\"" + std::to_string(i % 11) + "\"/></vertex>";
        s += "</vertexbuffer></geometry></submesh></submeshes></mesh>";
        ImportedModel m; std::string e; const double t = Timed([&] { ReadOg(s, m, e); }); CHECK(e.empty() && fx::Sane(m)); CHECK(t < 20.0); }
    // material scripts: unbalanced braces, a million words, deep braces, junk numbers, a very long name
    for (const std::string& script : {Repeat("{", 100000), Repeat("}", 100000), Repeat("material a { pass { diffuse nan inf 1e999 } ", 20000), Repeat("material ", 100000), "material x : : : {" + Repeat("technique {", 50000), std::string("material ") + std::string(2000000, 'q') + " { pass { texture_unit { texture " + std::string(100000, 'z') + " } } }"}) {
        const std::vector<std::string> sc{script};
        ImportedModel m; std::string e;
        const bool ok = ReadOgreXmlBuffer(good, "", m, &e, &sc);
        CHECK(ok ? fx::Sane(m) : !e.empty());
    }
}

// ======================= XGL / ZGL =======================
static bool ReadXg(const std::string& b, ImportedModel& m, std::string& e) { return ReadXglBuffer(b, "", m, &e); }
static std::string Zlib(const std::string& deflate) { std::string s; s.push_back((char)0x78); s.push_back((char)0x9C); s += deflate; s += std::string(4, '\0'); return s; }
static void Xgl() {
    const std::string good = fx::BuildXgl();
    { ImportedModel m; std::string e; CHECK(ReadXg(good, m, e) && fx::Sane(m)); }
    Sweep("xgl", ReadXg, good, Iters());
    Sweep("zgl", ReadXg, fx::ToZlibStored(good), Iters() / 4);
    // zlib bombs: 300 MB of '<' (over the 256 MiB cap), and a legal 1 MB of one letter (not XML)
    { ImportedModel m; std::string e; CHECK(!ReadXg(Zlib(fx::DeflateRepeat('<', 1200000)), m, e) && !e.empty()); }
    { ImportedModel m; std::string e; CHECK(!ReadXg(Zlib(fx::DeflateRepeat('a', 4000)), m, e) && !e.empty()); }
    { ImportedModel m; std::string e; CHECK(!ReadXg(Zlib(fx::DeflateLiterals("<WORLD></WORLD>")), m, e) && e.find("no mesh") != std::string::npos); }
    // ids and refs at the extremes, NaN and infinity, a missing PREF, an unknown MATREF
    for (const char* t : {"nan", "inf", "1e999", "-1", "4294967296", "99999999999", "0x10", "1e15"}) {
        std::string s = good; s.replace(s.find("<P ID=\"1\">1,0,0"), 15, std::string("<P ID=\"1\">") + t + ",0,0"); Try(ReadXg, s);
        s = good; s.replace(s.find("<PREF>1</PREF>"), 14, std::string("<PREF>") + t + "</PREF>"); Try(ReadXg, s);
        s = good; s.replace(s.find("<MATREF>0</MATREF>"), 18, std::string("<MATREF>") + t + "</MATREF>"); Try(ReadXg, s);
        s = good; s.replace(s.find("<SCALE>2</SCALE>"), 16, std::string("<SCALE>") + t + "</SCALE>"); Try(ReadXg, s);
        s = good; s.replace(s.find("<POSITION>5,0,0</POSITION>"), 26, std::string("<POSITION>") + t + ",0,0</POSITION>"); Try(ReadXg, s);
        s = good; s.replace(s.find("<FORWARD>1,0,0</FORWARD>"), 24, std::string("<FORWARD>") + t + ",0,0</FORWARD>"); Try(ReadXg, s);
        s = good; s.replace(s.find("<SHINE>64</SHINE>"), 17, std::string("<SHINE>") + t + "</SHINE>"); Try(ReadXg, s);
    }
    // a zero scale, a singular orientation, forward parallel to up
    for (const char* tr : {"<FORWARD>0,0,0</FORWARD><UP>0,0,0</UP>", "<FORWARD>0,1,0</FORWARD><UP>0,1,0</UP>", "<SCALE>0</SCALE>", "<SCALE>-3</SCALE>", "<SCALE>1e14</SCALE>"}) {
        std::string s = good; s.replace(s.find("<FORWARD>1,0,0</FORWARD><UP>0,1,0</UP>"), 38, tr); Try(ReadXg, s);
    }
    // 5,000 nested objects (cap 256 for the nesting), 100,000 objects each using the mesh (output cap), a mesh with 100,000 faces
    { std::string s = "<WORLD>" + Repeat("<OBJECT>", 5000) + Repeat("</OBJECT>", 5000) + "</WORLD>"; Try(ReadXg, s); }
    { std::string g = good; g.replace(g.find("<OBJECT><NAME>A"), g.find("</WORLD>") - g.find("<OBJECT><NAME>A"), Repeat("<OBJECT><MESHREF>0</MESHREF></OBJECT>", 20000)); ImportedModel m; std::string e; const double t = Timed([&] { ReadXg(g, m, e); }); CHECK(e.empty() ? fx::Sane(m) : true); CHECK(t < 30.0); }
    { std::string s = "<WORLD><MESH><P ID=\"0\">0,0,0</P><P ID=\"1\">1,0,0</P><P ID=\"2\">0,1,0</P>" + Repeat("<F><FV1><PREF>0</PREF></FV1><FV2><PREF>1</PREF></FV2><FV3><PREF>2</PREF></FV3></F>", 100000) + "</MESH></WORLD>";
      ImportedModel m; std::string e; const double t = Timed([&] { CHECK(ReadXg(s, m, e)); }); CHECK(fx::Sane(m)); CHECK(t < 20.0); }
    // a face with 20,000 corners; a billion-laughs entity; a long text
    { std::string s = "<WORLD><MESH><P ID=\"0\">0,0,0</P><F>"; for (int i = 1; i <= 20000; ++i) s += "<FV" + std::to_string(i) + "><PREF>0</PREF></FV" + std::to_string(i) + ">"; s += "</F></MESH></WORLD>"; Try(ReadXg, s); }
    Try(ReadXg, "<!DOCTYPE w [<!ENTITY a \"aaaa\"><!ENTITY b \"&a;&a;&a;&a;\">]><WORLD/>");
    Try(ReadXg, "<WORLD><MESH><P ID=\"0\">" + std::string(3000000, '1') + ",0,0</P></MESH></WORLD>");
}

// ======================= Irrlicht scene =======================
static bool g_loaderCalled = false;
static bool BoundedLoader(const std::string& path, ImportedModel& m, std::string* err) {
    g_loaderCalled = true;
    if (path.find("fail") != std::string::npos) { if (err) *err = "refused"; return false; }
    ImportedNode r; r.name = "r";
    m.nodes = {r};
    ImportedPart p;
    p.mesh.vertices.assign(1000, Vec3{0, 0, 0});
    for (int i = 0; i < 1000; ++i) p.mesh.vertices[(std::size_t)i] = Vec3{(float)i, (float)(i % 7), 0};
    for (int i = 0; i + 2 < 1000; ++i) { p.mesh.triangles.push_back(i); p.mesh.triangles.push_back(i + 1); p.mesh.triangles.push_back(i + 2); }
    p.node = 0; p.material = -1;
    m.parts.push_back(p);
    m.backend = "own";
    return true;
}
static IrrMeshLoader g_loader = BoundedLoader;
static bool ReadIr(const std::string& b, ImportedModel& m, std::string& e) { return ReadIrrSceneBuffer(b, "", m, &e, &g_loader); }
static void Irr() {
    const std::string good = fx::BuildIrrScene();
    { ImportedModel m; std::string e; CHECK(ReadIr(good, m, e) && fx::Sane(m)); }
    Sweep("irr", ReadIr, good, Iters());
    Sweep("irr utf16", ReadIr, fx::ToUtf16leD(good), Iters() / 4);
    const std::string head = "<irr_scene><node type=\"cube\"><attributes>";
    const std::string tail = "</attributes></node></irr_scene>";
    for (const char* t : {"nan", "inf", "-inf", "1e999", "1e308", "-1e308", "0x1p3", "1e15", "1e16", "1e30", "-1", "0", "4294967296", "1,2,3,4,5"}) {
        const std::string v = t;
        Try(ReadIr, head + "<vector3d name=\"Position\" value=\"" + v + ", 0, 0\"/>" + tail);
        Try(ReadIr, head + "<vector3d name=\"Rotation\" value=\"" + v + ", " + v + ", " + v + "\"/>" + tail);
        Try(ReadIr, head + "<vector3d name=\"Scale\" value=\"" + v + ", " + v + ", " + v + "\"/>" + tail);
        Try(ReadIr, head + "<float name=\"Size\" value=\"" + v + "\"/>" + tail);
        Try(ReadIr, "<irr_scene><node type=\"sphere\"><attributes><float name=\"Radius\" value=\"" + v + "\"/><int name=\"PolyCountX\" value=\"" + v + "\"/><int name=\"PolyCountY\" value=\"" + v + "\"/></attributes></node></irr_scene>");
        Try(ReadIr, head + "<float name=\"Size\" value=\"1\"/>" + tail.substr(0, tail.find("</node>")) + "<materials><attributes><color name=\"Diffuse\" value=\"" + v + "\"/><float name=\"Shininess\" value=\"" + v + "\"/></attributes></materials></node></irr_scene>");
    }
    // 5,000 nested nodes (cap 256), 100,000 flat nodes, 200 mesh nodes naming 200 failing files, 20,000 nodes using one mesh (output cap)
    { std::string s = "<irr_scene>" + Repeat("<node type=\"empty\">", 5000) + Repeat("</node>", 5000) + "</irr_scene>"; ImportedModel m; std::string e; CHECK(!ReadIr(s, m, e)); }
    { std::string s = "<irr_scene>" + Repeat("<node type=\"cube\"/>", 3000) + "</irr_scene>"; ImportedModel m; std::string e; const double t = Timed([&] { ReadIr(s, m, e); }); CHECK(e.empty() ? fx::Sane(m) : true); CHECK(t < 30.0); }
    { std::string s = "<irr_scene>"; for (int i = 0; i < 1200; ++i) s += "<node type=\"mesh\"><attributes><string name=\"Mesh\" value=\"fail" + std::to_string(i) + ".x\"/></attributes></node>"; s += "</irr_scene>"; ImportedModel m; std::string e; CHECK(!ReadIr(s, m, e)); }
    { std::string s = "<irr_scene>" + Repeat("<node type=\"mesh\"><attributes><string name=\"Mesh\" value=\"a.x\"/></attributes></node>", 60000) + "</irr_scene>"; ImportedModel m; std::string e; const double t = Timed([&] { ReadIr(s, m, e); }); CHECK(e.empty() ? fx::Sane(m) : !e.empty()); CHECK(t < 60.0); }
    // a billion-laughs entity, a mesh path of 3 MB, a loader that returns a model with no nodes
    Try(ReadIr, "<!DOCTYPE s [<!ENTITY a \"aaaa\"><!ENTITY b \"&a;&a;&a;&a;\">]><irr_scene/>");
    Try(ReadIr, "<irr_scene><node type=\"mesh\"><attributes><string name=\"Mesh\" value=\"" + std::string(3000000, 'p') + "\"/></attributes></node></irr_scene>");
    {   IrrMeshLoader empty = [](const std::string&, ImportedModel& m, std::string*) { m = ImportedModel{}; return true; };
        ImportedModel m; std::string e; CHECK(!ReadIrrSceneBuffer("<irr_scene><node type=\"mesh\"><attributes><string name=\"Mesh\" value=\"a\"/></attributes></node></irr_scene>", "", m, &e, &empty) && !e.empty()); }
    {   // a loader that hands back parts whose node or material indices are wrong: the scene must still come out sane
        IrrMeshLoader wild = [](const std::string&, ImportedModel& m, std::string*) {
            ImportedNode r; r.name = "r"; m.nodes = {r};
            ImportedPart p; p.mesh.vertices = {Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{0, 1, 0}}; p.mesh.triangles = {0, 1, 2}; p.node = 99; p.material = 99;
            m.parts.push_back(p); return true; };
        ImportedModel m; std::string e; const bool ok = ReadIrrSceneBuffer("<irr_scene><node type=\"mesh\"><attributes><string name=\"Mesh\" value=\"a\"/></attributes></node></irr_scene>", "", m, &e, &wild); CHECK(ok ? fx::Sane(m) : !e.empty()); }
}

// ======================= NFF =======================
static bool ReadNf(const std::string& b, ImportedModel& m, std::string& e) { return ReadNffBuffer(b, "", m, &e); }
static void Nff() {
    const std::string good = fx::BuildNff();
    { ImportedModel m; std::string e; CHECK(ReadNf(good, m, e) && fx::Sane(m)); }
    Sweep("nff", ReadNf, good, Iters());
    Sweep("nff sense8", ReadNf, fx::BuildNffSense8(), Iters());
    for (const char* c : {"1000000", "1000001", "99999999999", "-1", "2147483647", "4294967296", "nan", "1e999", "10000000", "10000001"}) {
        Try(ReadNf, std::string("p ") + c + "\n0 0 0\n1 0 0\n0 1 0\n");
        Try(ReadNf, std::string("pp ") + c + "\n0 0 0 0 0 1\n");
        Try(ReadNf, std::string("tess ") + c + "\ns 0 0 0 1\n");
        Try(ReadNf, std::string("tess ") + c + "\nc\n0 0 0 1\n0 0 1 1\n");
        Try(ReadNf, std::string("nff\nObj\n") + c + "\n0 0 0\n");
        Try(ReadNf, std::string("nff\nObj\n3\n0 0 0\n1 0 0\n0 1 0\n") + c + "\n3 0 1 2\n");
        Try(ReadNf, std::string("nff\nObj\n3\n0 0 0\n1 0 0\n0 1 0\n1\n") + c + " 0 1 2\n");
    }
    for (const char* t : {"nan", "inf", "-inf", "1e999", "1e308", "-1e308", "0x1p3", "1e15", "1e16", "1e30"}) {
        const std::string v = t;
        Try(ReadNf, "s " + v + " 0 0 1\n");
        Try(ReadNf, "s 0 0 0 " + v + "\n");
        Try(ReadNf, "s 0 0 0 1 " + v + " " + v + "\n");
        Try(ReadNf, "c\n" + v + " 0 0 1\n0 0 1 " + v + "\n");
        Try(ReadNf, "hex 0 0 0 " + v + "\n");
        Try(ReadNf, "tet " + v + " " + v + " " + v + " 1\n");
        Try(ReadNf, "p 3\n" + v + " 0 0\n1 0 0\n0 1 0\n");
        Try(ReadNf, "pp 3\n0 0 0 " + v + " 0 0\n1 0 0 0 0 1\n0 1 0 0 0 1\n");
        Try(ReadNf, "f " + v + " 0 0 " + v + " " + v + " " + v + " " + v + " " + v + "\ns 0 0 0 1\n");
        Try(ReadNf, "nff\nObj\n3\n" + v + " 0 0 uv " + v + " " + v + "\n1 0 0\n0 1 0\n1\n3 0 1 2 matid " + v + "\n");
    }
    // a cone with zero length, equal ends at a point, a huge radius; a sphere with a huge radius; tess 6 solids, 2,000 of them (output cap)
    for (const char* c : {"c\n0 0 0 1\n0 0 0 1\n", "c\n0 0 0 0\n0 0 0 0\n", "c\n0 0 0 1e15\n0 0 1 1e15\n", "c\n0 0 0 -1\n0 0 1 1\n"}) Try(ReadNf, c);
    { std::string s = "tess 6\n" + Repeat("s 0 0 0 1e14\n", 600); ImportedModel m; std::string e; const double t = Timed([&] { ReadNf(s, m, e); }); CHECK(e.empty() ? fx::Sane(m) : !e.empty()); CHECK(t < 60.0); }
    { std::string s = "tess 6\n" + Repeat("c\n0 0 0 1\n0 0 1 1\n", 300); ImportedModel m; std::string e; const double t = Timed([&] { ReadNf(s, m, e); }); CHECK(e.empty() ? fx::Sane(m) : !e.empty()); CHECK(t < 60.0); }
    // 100,000 polygons, a polygon of 100,000 vertices, a Sense8 file of 100,000 polygons, 5,000 fills, a 3 MB token
    { std::string s = Repeat("p 3\n0 0 0\n1 0 0\n0 1 0\n", 100000); ImportedModel m; std::string e; const double t = Timed([&] { CHECK(ReadNf(s, m, e)); }); CHECK(fx::Sane(m) && m.parts.size() == 1 && fx::TriCount(m) == 100000); CHECK(t < 20.0); }
    { std::string s = "p 100000\n"; for (int i = 0; i < 100000; ++i) s += std::to_string(i % 977) + " " + std::to_string(i % 313) + " " + std::to_string(i % 7) + "\n"; ImportedModel m; std::string e; const double t = Timed([&] { ReadNf(s, m, e); }); CHECK(e.empty() ? fx::Sane(m) : !e.empty()); CHECK(t < 30.0); }
    { std::string s = "nff\nObj\n3\n0 0 0\n1 0 0\n0 1 0\n100000\n" + Repeat("3 0 1 2 matid 1\n", 100000); ImportedModel m; std::string e; const double t = Timed([&] { CHECK(ReadNf(s, m, e)); }); CHECK(fx::Sane(m)); CHECK(t < 20.0); }
    { std::string s; for (int i = 0; i < 5000; ++i) s += "f 1 0 0 0 0 " + std::to_string(i) + "\ns 0 0 0 1\n"; ImportedModel m; std::string e; const double t = Timed([&] { ReadNf(s, m, e); }); CHECK(e.empty() ? fx::Sane(m) : !e.empty()); CHECK(t < 60.0); }
    Try(ReadNf, "s 0 0 0 " + std::string(3000000, '1') + "\n");
    Try(ReadNf, "f 1 1 1 " + std::string(3000000, 'x') + "\ns 0 0 0 1\n");
    // the Sense8 mtable: a path that climbs out, a name that is a folder, a 3 MB name
    Try(ReadNf, "nff\nObj\nmtable ../../../../etc/passwd\n3\n0 0 0\n1 0 0\n0 1 0\n1\n3 0 1 2 matid 0\n");
    Try(ReadNf, "nff\nObj\nmtable /\n3\n0 0 0\n1 0 0\n0 1 0\n1\n3 0 1 2 matid 0\n");
    Try(ReadNf, "nff\nObj\nmtable " + std::string(3000000, 'm') + "\n3\n0 0 0\n1 0 0\n0 1 0\n1\n3 0 1 2 matid 0\n");
}

int main() {
    RUN_SUITE("batchd_hostile");
    std::fprintf(stderr, "  mutation iterations per reader: %ld (OKAY_BATCHD_FUZZ_ITERS)\n", Iters());
    Ac3d(); Ogre(); Xgl(); Irr(); Nff();
    TEST_MAIN_RESULT();
}
