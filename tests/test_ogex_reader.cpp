// Our own OpenGEX reader (engine/src/Render/OgexReader.cpp) and its OpenDDL parser (OpenDdl.hpp). SYNTHETIC files built in
// memory (tests/e4_fixtures.hpp), checked against numbers worked out by hand. No real .ogex file is in this repository: the
// reader is unverified against real files. Builds on its own with -DOKAY_STANDALONE_TEST (reader source only).
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "e4_fixtures.hpp"
#include "OpenDdl.hpp"
#include "okay/Render/ModelData.hpp"
#include <cmath>
#include <string>

using namespace okay;
using namespace okay::detail::ddl;

static bool HasNote(const ImportedModel& m, const char* what) { for (const std::string& n : m.notes) if (n.find(what) != std::string::npos) return true; return false; }
static bool Read(const std::string& t, ImportedModel& m, std::string& err, const std::string& dir = "") { return ReadOgexBuffer(t, dir, m, &err); }
static bool Near(float a, float b, float e = 1e-5f) { return std::fabs(a - b) <= e; }

// the first number of the first data structure named `ident` anywhere in a parsed text
static bool ParseOk(const std::string& t, Doc& d) { std::string e; const bool ok = Parse(t, d, e); if (!ok) std::fprintf(stderr, "  parse error: %s\n", e.c_str()); return ok; }

int main() {
    RUN_SUITE("ogex_reader");

    // ---- the OpenDDL grammar --------------------------------------------------------------------------------------------
    {
        Doc d;
        CHECK(ParseOk("// c\n/* block\n comment */ Foo $a (p = 1, q = \"s\", r = true, t = $x%y, u = null, w = float, bare) { int8 {1, -2, 0x7F, 'A', 1_0} "
                      "float {1.5, -2e2, 0x3F800000, .5} string {\"he\" \"llo\\x21\\n\"} bool {true, false, 1} ref {$a, %b, $c%d, null} type {float, int8} "
                      "int16[2] {{1,2},{3,4}} unsigned_int8 %loc {255} half {0x3C00} double {0x3FF0000000000000} }", d));
        CHECK(d.nodes.size() == 11);
        const Node& foo = d.nodes[0];
        CHECK(d.Is(foo, "Foo") && d.texts[foo.name] == "$a" && foo.propCount == 7);
        CHECK(d.PropNum(foo, "p", 0) == 1 && d.PropStr(foo, "q") == "s" && d.PropNum(foo, "r", 0) == 1 && d.PropStr(foo, "t") == "$x%y");
        CHECK(d.PropStr(foo, "u") == "null" && d.PropStr(foo, "w") == "float" && d.HasProp(foo, "bare") && d.PropNum(foo, "bare", 0) == 1);
        const Node& i8 = d.nodes[d.Child(0, "int8")];
        CHECK(i8.dataCount == 5 && d.nums[i8.dataBegin] == 1 && d.nums[i8.dataBegin + 1] == -2 && d.nums[i8.dataBegin + 2] == 127 && d.nums[i8.dataBegin + 3] == 65 && d.nums[i8.dataBegin + 4] == 10);
        const Node& fl = d.nodes[d.Child(0, "float")];
        CHECK(fl.dataCount == 4 && d.nums[fl.dataBegin] == 1.5 && d.nums[fl.dataBegin + 1] == -200 && d.nums[fl.dataBegin + 2] == 1.0 && d.nums[fl.dataBegin + 3] == 0.5);
        const Node& st = d.nodes[d.Child(0, "string")];
        CHECK(st.dataCount == 1 && d.strs[st.dataBegin] == "hello!\n");
        const Node& bo = d.nodes[d.Child(0, "bool")];
        CHECK(bo.dataCount == 3 && d.nums[bo.dataBegin] == 1 && d.nums[bo.dataBegin + 1] == 0 && d.nums[bo.dataBegin + 2] == 1);
        const Node& rf = d.nodes[d.Child(0, "ref")];
        CHECK(rf.dataCount == 4 && d.strs[rf.dataBegin + 2] == "$c%d" && d.strs[rf.dataBegin + 3] == "null");
        const Node& ty = d.nodes[d.Child(0, "type")];
        CHECK(ty.dataCount == 2 && d.strs[ty.dataBegin + 1] == "int8");
        const Node& sub = d.nodes[d.Child(0, "int16")];
        CHECK(sub.sub == 2 && sub.dataCount == 4 && d.nums[sub.dataBegin + 3] == 4);
        const Node& u8 = d.nodes[d.Child(0, "unsigned_int8")];
        CHECK(d.texts[u8.name] == "%loc" && d.nums[u8.dataBegin] == 255);
        CHECK(d.nums[d.nodes[d.Child(0, "half")].dataBegin] == 1.0 && d.nums[d.nodes[d.Child(0, "double")].dataBegin] == 1.0);
    }
    {   // grammar errors, each refused with the line
        const char* bad[] = {
            "Foo {", "Foo }", "Foo { int8 {128} }", "Foo { unsigned_int8 {-1} }", "Foo { float {1 2} }", "Foo { float[2] {{1,2,3}} }", "Foo { float[2] {1,2} }",
            "Foo { float {nan} }", "Foo { float {1e400} }", "Foo { string {\"open} }", "Foo { string {\"\\q\"} }", "Foo { ref {abc} }", "Foo (a = ) {}", "Foo /* open",
            "Foo { bool {2} }", "Foo { int8 {0x} }", "Foo { float {0x7F800000} }", "123", "Foo $ {}", "Foo { int8 {'ab', 1} }", "Foo (a = 1, ) {}", "Foo { float[0] {} }"};
        for (const char* t : bad) { Doc d; std::string e; CHECK(!Parse(t, d, e) && !e.empty() && e.find("line") != std::string::npos); }
        Doc d; std::string e;
        CHECK(!Parse("Foo {\n\n int8 {500} }", d, e) && e.find("line 3") != std::string::npos);
    }
    {   // budgets of the parser: nesting depth, structure count, values, strings
        Doc d; std::string e;
        std::string deep;
        for (int i = 0; i < 300; ++i) deep += "Node {";
        for (int i = 0; i < 300; ++i) deep += "}";
        CHECK(!Parse(deep, d, e) && e.find("nested deeper") != std::string::npos);
        std::string ok255;
        for (int i = 0; i < 255; ++i) ok255 += "Node {";
        for (int i = 0; i < 255; ++i) ok255 += "}";
        CHECK(Parse(ok255, d, e));
        std::string unclosed;
        for (int i = 0; i < 1000000; ++i) unclosed += "N{";
        CHECK(!Parse(unclosed, d, e) && e.find("nested deeper") != std::string::npos);   // 1,000,000 open structures: refused at depth 256, no recursion
        Limits lim; lim.maxStructs = 100;
        std::string many;
        for (int i = 0; i < 101; ++i) many += "N{}";
        CHECK(!Parse(many, d, e, lim) && e.find("too many structures") != std::string::npos);
        lim = Limits(); lim.maxValues = 10;
        CHECK(!Parse("F { float {1,2,3,4,5,6,7,8,9,10,11} }", d, e, lim) && e.find("too many data values") != std::string::npos);
        lim = Limits(); lim.maxStringBytes = 100;
        CHECK(!Parse("F { string {\"" + std::string(200, 'a') + "\"} }", d, e, lim) && e.find("too much string") != std::string::npos);
        lim = Limits(); lim.maxSubArray = 4;
        CHECK(!Parse("F { float[5] {{1,2,3,4,5}} }", d, e, lim));
    }

    // ---- the quad ----------------------------------------------------------------------------------------------------------
    {
        ImportedModel m; std::string err;
        const bool ok = Read(e4::OgexQuad(), m, err, "dir/");
        if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
        CHECK(ok && m.backend == "own" && fx::Sane(m));
        CHECK(m.nodes.size() == 3 && m.nodes[0].name == "OpenGEX" && m.nodes[1].name == "root" && m.nodes[2].name == "quad");
        CHECK(m.nodes[1].parent == 0 && m.nodes[2].parent == 1);
        CHECK(Near(m.nodes[1].position.x, 1) && Near(m.nodes[1].position.y, 2) && Near(m.nodes[1].position.z, 3));
        CHECK(Near(m.nodes[2].position.x, 4) && Near(m.nodes[2].position.y, 5) && Near(m.nodes[2].position.z, 6));
        // Metric distance 2: scale of the root; Metric up "z": the root turns by -90 degrees about X
        CHECK(Near(m.nodes[0].scale.x, 2) && Near(m.nodes[0].scale.y, 2) && Near(m.nodes[0].scale.z, 2));
        CHECK(Near(m.nodes[0].rotation.x, -0.70710678f) && Near(m.nodes[0].rotation.w, 0.70710678f) && Near(m.nodes[0].rotation.y, 0) && Near(m.nodes[0].rotation.z, 0));
        CHECK(m.parts.size() == 1 && m.parts[0].node == 2 && m.parts[0].material == 0 && m.parts[0].bones.empty());
        const Mesh& me = m.parts[0].mesh;
        CHECK(me.vertices.size() == 4 && me.triangles.size() == 6 && me.normals.size() == 4 && me.uvs.size() == 4);
        const int tri[6] = {0, 1, 2, 1, 3, 2};
        for (int i = 0; i < 6; ++i) CHECK(me.triangles[(std::size_t)i] == tri[i]);
        CHECK(Near(me.vertices[3].x, 1) && Near(me.vertices[3].y, 1) && Near(me.vertices[3].z, 0));
        CHECK(Near(me.normals[2].z, 1) && Near(me.uvs[1].x, 1) && Near(me.uvs[1].y, 0) && Near(me.uvs[2].y, 1));   // texture coordinates as written
        CHECK(m.materials.size() == 1 && m.materials[0].name == "red" && m.materials[0].hasDiffuse && Near(m.materials[0].diffuse.r, 1) && Near(m.materials[0].diffuse.g, 0));
        CHECK(Near(m.materials[0].shininess, 32) && m.materials[0].texture == "dir/tex/red.png");
        CHECK(HasNote(m, "texture coordinates are kept as written"));
    }
    {   // UTF-16 with a byte order mark is the same file
        const std::string t = e4::OgexQuad();
        std::string u16 = "\xFF\xFE";
        for (char c : t) { u16.push_back(c); u16.push_back('\0'); }
        ImportedModel m; std::string err;
        CHECK(Read(u16, m, err, "") && m.parts.size() == 1 && m.parts[0].mesh.triangles.size() == 6);
        ImportedModel m2;
        CHECK(Read("\xEF\xBB\xBF" + t, m2, err, "") && m2.parts.size() == 1);
    }
    {   // textures: only files inside the model's folder; an absolute path and a path that climbs out are refused (the model still reads)
        for (const char* path : {"/etc/passwd", "../x.png", "C:/x.png", "a/../../x.png"}) {
            std::string t = e4::OgexQuad();
            t.replace(t.find("tex/red.png"), 11, path);
            ImportedModel m; std::string err;
            CHECK(Read(t, m, err, "dir/") && m.materials[0].texture.empty() && HasNote(m, "refused"));
        }
        std::string t = e4::OgexQuad();
        t.replace(t.find("tex/red.png"), 11, "a/../b.png");
        ImportedModel m; std::string err;
        CHECK(Read(t, m, err, "dir/") && m.materials[0].texture == "dir/a/../b.png");
    }

    // ---- primitives, winding, index arrays ------------------------------------------------------------------------------------
    {
        ImportedModel m; std::string err;
        CHECK(Read(e4::OgexMesh("quads", "", "unsigned_int16[4] {{0,1,2,3}}"), m, err) && m.parts.size() == 1);
        const int q[6] = {0, 1, 2, 0, 2, 3};
        CHECK(m.parts[0].mesh.triangles.size() == 6);
        for (int i = 0; i < 6; ++i) CHECK(m.parts[0].mesh.triangles[(std::size_t)i] == q[i]);
        ImportedModel s;
        CHECK(Read(e4::OgexMesh("triangle_strip", "", "unsigned_int8 {0,1,2,3}"), s, err) && s.parts[0].mesh.triangles.size() == 6);
        const int st[6] = {0, 1, 2, 2, 1, 3};   // the second triangle of a strip has its first two vertices swapped
        for (int i = 0; i < 6; ++i) CHECK(s.parts[0].mesh.triangles[(std::size_t)i] == st[i]);
        ImportedModel r;   // a restart index ends the strip: 0 1 2 | 3 ... leaves no second triangle from {2,3} alone
        CHECK(Read(e4::OgexMesh("triangle_strip", "(restart = 255)", "unsigned_int8 {0,1,2,255,1,2,3}", 4), r, err) && r.parts[0].mesh.triangles.size() == 6);
        CHECK(r.parts[0].mesh.triangles[0] == 0 && r.parts[0].mesh.triangles[3] == 1 && r.parts[0].mesh.triangles[4] == 2 && r.parts[0].mesh.triangles[5] == 3);
        ImportedModel cw;   // front = "cw": the winding is reversed
        CHECK(Read(e4::OgexMesh("triangles", "(front = \"cw\")", "unsigned_int32[3] {{0,1,2}}"), cw, err));
        {   // vertices are renumbered by first use, so look at the positions: (i, i*i, 0) for original vertex i; the order is now 0, 2, 1
            const Mesh& cm = cw.parts[0].mesh;
            CHECK(Near(cm.vertices[(std::size_t)cm.triangles[0]].x, 0) && Near(cm.vertices[(std::size_t)cm.triangles[1]].x, 2) && Near(cm.vertices[(std::size_t)cm.triangles[2]].x, 1));
            CHECK(Near(cm.vertices[(std::size_t)cm.triangles[1]].y, 4));
        }
        ImportedModel seq;   // no index array: the vertices in sequence
        CHECK(Read("GeometryObject $g {Mesh {VertexArray (attrib = \"position\") {float[3] {{0,0,0},{1,0,0},{0,1,0},{5,5,5},{6,5,5},{5,6,5}}}}}\n"
                   "GeometryNode $n {ObjectRef {ref {$g}}}", seq, err) && seq.parts.size() == 1 && seq.parts[0].mesh.triangles.size() == 6 && seq.parts[0].mesh.vertices.size() == 6);
        ImportedModel two;   // two index arrays with their materials: one part each, each holding only the vertices it uses
        CHECK(Read("Material $m0 {Name {string {\"a\"}}} Material $m1 {Name {string {\"b\"}}}\n"
                   "GeometryObject $g {Mesh {VertexArray (attrib = \"position\") {float[3] {{0,0,0},{1,0,0},{0,1,0},{5,5,5},{6,5,5},{5,6,5}}}\n"
                   " IndexArray (material = 0) {unsigned_int8[3] {{0,1,2}}} IndexArray (material = 1) {unsigned_int8[3] {{3,4,5}}}}}\n"
                   "GeometryNode $n {ObjectRef {ref {$g}} MaterialRef (index = 0) {ref {$m0}} MaterialRef (index = 1) {ref {$m1}}}", two, err));
        CHECK(two.parts.size() == 2 && two.parts[0].mesh.vertices.size() == 3 && two.parts[1].mesh.vertices.size() == 3 && two.parts[0].material == 0 && two.parts[1].material == 1);
        CHECK(Near(two.parts[1].mesh.vertices[0].x, 5));
        ImportedModel pts;   // points, lines: not read
        CHECK(!Read(e4::OgexMesh("lines", "", "unsigned_int8[2] {{0,1}}"), pts, err) && err.find("mesh") != std::string::npos);
        ImportedModel bad;   // an index past the vertex array: that triangle is skipped, the rest is read
        CHECK(Read(e4::OgexMesh("triangles", "", "unsigned_int32[3] {{0,1,2},{0,1,4294967295},{1,2,3}}"), bad, err) && bad.parts[0].mesh.triangles.size() == 6 && HasNote(bad, "past the vertex array"));
        ImportedModel none;
        CHECK(!Read(e4::OgexMesh("triangles", "", "unsigned_int32[3] {{9,9,9}}"), none, err));
    }

    // ---- transforms ----------------------------------------------------------------------------------------------------------
    {
        auto node = [](const std::string& body) { return "Node {Name {string {\"n\"}} " + body + "}"; };
        ImportedModel m; std::string err;
        // a column-major matrix: the translation is in the last four numbers
        CHECK(Read("GeometryObject $g {Mesh {VertexArray (attrib = \"position\") {float[3] {{0,0,0},{1,0,0},{0,1,0}}}}}\n"
                   "GeometryNode $n {Name {string {\"n\"}} ObjectRef {ref {$g}} Transform {float[16] {{1,0,0,0, 0,1,0,0, 0,0,1,0, 7,8,9,1}}}}", m, err));
        CHECK(Near(m.nodes[1].position.x, 7) && Near(m.nodes[1].position.y, 8) && Near(m.nodes[1].position.z, 9));
        // structures compose in file order: Translation then Rotation = T * R (the node rotates, and sits at the translation) ...
        const std::string geo = "GeometryObject $g {Mesh {VertexArray (attrib = \"position\") {float[3] {{0,0,0},{1,0,0},{0,1,0}}}}}\n";
        ImportedModel a;
        CHECK(Read(geo + "GeometryNode $n {Name {string {\"n\"}} ObjectRef {ref {$g}} Translation {float[3] {{1,0,0}}} Rotation (kind = \"z\") {float {1.5707963267948966}}}", a, err));
        CHECK(Near(a.nodes[1].position.x, 1, 1e-5f) && Near(a.nodes[1].position.y, 0, 1e-5f) && Near(a.nodes[1].rotation.z, 0.70710678f) && Near(a.nodes[1].rotation.w, 0.70710678f));
        // ... and Rotation then Translation = R * T: the translation is turned by the rotation (1,0,0) -> (0,1,0)
        ImportedModel b;
        CHECK(Read(geo + "GeometryNode $n {Name {string {\"n\"}} ObjectRef {ref {$g}} Rotation (kind = \"z\") {float {1.5707963267948966}} Translation {float[3] {{1,0,0}}}}", b, err));
        CHECK(Near(b.nodes[1].position.x, 0, 1e-5f) && Near(b.nodes[1].position.y, 1, 1e-5f));
        // quaternion and axis rotations, one-axis scale
        ImportedModel c;
        CHECK(Read(geo + "GeometryNode $n {Name {string {\"n\"}} ObjectRef {ref {$g}} Rotation (kind = \"quaternion\") {float[4] {{0, 0, 0.70710678, 0.70710678}}} Scale (kind = \"y\") {float {3}}}", c, err));
        CHECK(Near(c.nodes[1].rotation.z, 0.70710678f, 1e-4f) && Near(c.nodes[1].rotation.w, 0.70710678f, 1e-4f));
        CHECK(Near(std::fabs(c.nodes[1].scale.x) + std::fabs(c.nodes[1].scale.y) + std::fabs(c.nodes[1].scale.z), 5.0f, 1e-4f));   // (1, 3, 1): one axis of 3
        ImportedModel e;
        CHECK(Read(geo + "GeometryNode $n {Name {string {\"n\"}} ObjectRef {ref {$g}} Rotation {float[4] {{0, 1, 0, 3.14159265358979}}}}", e, err));
        CHECK(Near(std::fabs(e.nodes[1].rotation.y), 1.0f, 1e-4f));
        // an object-only transform scales the mesh and leaves the node alone
        ImportedModel o;
        CHECK(Read(geo + "GeometryNode $n {Name {string {\"n\"}} ObjectRef {ref {$g}} Scale (kind = \"x\", object = true) {float {2}}}", o, err));
        CHECK(Near(o.nodes[1].scale.x, 1) && Near(o.parts[0].mesh.vertices[1].x, 2) && Near(o.parts[0].mesh.vertices[2].y, 1));
        // the angle metric: a rotation of "1 unit" where a unit is 90 degrees
        ImportedModel g;
        CHECK(Read("Metric (key = \"angle\") {float {1.5707963267948966}}\n" + geo + "GeometryNode $n {Name {string {\"n\"}} ObjectRef {ref {$g}} Rotation (kind = \"z\") {float {1}}}", g, err));
        CHECK(Near(g.nodes[1].rotation.z, 0.70710678f, 1e-5f));
        // names: repeated names get a suffix; an unnamed node is named after its $name, then by number
        ImportedModel n;
        CHECK(Read("Node $first {} Node $first2 {Name {string {\"x\"}}} Node {Name {string {\"x\"}}} " + geo + "GeometryNode $n {ObjectRef {ref {$g}}}", n, err));
        CHECK(n.nodes.size() == 5 && n.nodes[1].name == "first" && n.nodes[2].name == "x" && n.nodes[3].name == "x_1");
        (void)node;
    }

    // ---- skin ------------------------------------------------------------------------------------------------------------------
    {
        ImportedModel m; std::string err;
        CHECK(Read(e4::OgexSkin(), m, err) && fx::Sane(m));
        CHECK(m.parts.size() == 1 && m.parts[0].node == 0 && m.parts[0].bones.size() == 2);   // a skinned part sits on the root
        const ImportedPart& p = m.parts[0];
        CHECK(p.bones[0].name == "b0" && p.bones[1].name == "b1");
        // bone 1 sits at (0,1,0): its inverse bind translates by (0,-1,0); bone 0 is at the origin
        CHECK(Near(p.bones[1].inverseBind.at(3, 1), -1) && Near(p.bones[1].inverseBind.at(3, 0), 0) && Near(p.bones[0].inverseBind.at(3, 1), 0));
        CHECK(p.jointIdx.size() == 3 && p.jointWt.size() == 3);
        CHECK(p.jointIdx[0][0] == 0 && Near(p.jointWt[0][0], 1) && Near(p.jointWt[0][1], 0));
        // vertex 1: bone 0 with 0.25 and bone 1 with 0.75
        float w0 = 0, w1 = 0;
        for (int k = 0; k < 4; ++k) { if (p.jointWt[1][(std::size_t)k] > 0) { if (p.jointIdx[1][(std::size_t)k] == 0) w0 = p.jointWt[1][(std::size_t)k]; else w1 = p.jointWt[1][(std::size_t)k]; } }
        CHECK(Near(w0, 0.25f) && Near(w1, 0.75f));
        CHECK(p.jointIdx[2][0] == 1 && Near(p.jointWt[2][0], 1));
        // the skin transform (a bind shape moving the mesh by (5,0,0)) is folded into the inverse binds: bone 0: translation (5,0,0)
        ImportedModel s;
        CHECK(Read(e4::OgexSkin("1,0,0,0, 0,1,0,0, 0,0,1,0, 5,0,0,1"), s, err));
        CHECK(Near(s.parts[0].bones[0].inverseBind.at(3, 0), 5) && Near(s.parts[0].bones[1].inverseBind.at(3, 0), 5) && Near(s.parts[0].bones[1].inverseBind.at(3, 1), -1));
        // bone counts that do not add up: the skin is dropped with a note, the mesh stays rigid
        std::string t = e4::OgexSkin();
        t.replace(t.find("{1, 2, 1}"), 9, "{1, 2, 2}");
        ImportedModel bad;
        CHECK(Read(t, bad, err) && bad.parts.size() == 1 && bad.parts[0].bones.empty() && HasNote(bad, "do not add up"));
        // a bone count of 4294967295
        t = e4::OgexSkin();
        t.replace(t.find("unsigned_int8 {1, 2, 1}"), 23, "unsigned_int32 {1, 4294967295, 1}");
        ImportedModel huge;
        CHECK(Read(t, huge, err) && huge.parts[0].bones.empty());
        // a bone reference to a node that is not in the file: the vertex is bound to the root bone instead
        t = e4::OgexSkin();
        t.replace(t.find("$b1}"), 4, "$zz}");
        ImportedModel miss;
        CHECK(Read(t, miss, err) && miss.parts[0].bones.size() == 2 && HasNote(miss, "names no node"));
    }

    // ---- animation -------------------------------------------------------------------------------------------------------------
    {
        ImportedModel m; std::string err;
        CHECK(Read(e4::OgexAnim(), m, err) && fx::Sane(m));
        CHECK(m.clips.size() == 2 && m.clips[0].name == "walk" && m.clips[1].name == "clip1");
        CHECK(m.clips[0].channels.size() == 1 && m.clips[0].channels[0].node == "anim");
        const ImportedChannel& ch = m.clips[0].channels[0];
        // the key times are the union of the two tracks' times: 0, 0.5, 1
        CHECK(ch.position.size() == 3 && ch.rotation.size() == 3 && ch.scale.size() == 3);
        CHECK(Near(ch.position[0].time, 0) && Near(ch.position[1].time, 0.5f) && Near(ch.position[2].time, 1));
        // translation (0,0,0) -> (2,4,6) over time 0..1, linear: at 0.5 it is (1,2,3)
        CHECK(Near(ch.position[1].value.x, 1) && Near(ch.position[1].value.y, 2) && Near(ch.position[1].value.z, 3) && Near(ch.position[2].value.z, 6));
        // rotation about Z: 0, 1.0, 2.0 radians
        CHECK(Near(ch.rotation[0].value.w, 1) && Near(ch.rotation[1].value.z, std::sin(0.5f)) && Near(ch.rotation[1].value.w, std::cos(0.5f)) && Near(ch.rotation[2].value.z, std::sin(1.0f)) && Near(ch.rotation[2].value.w, std::cos(1.0f)));
        CHECK(Near(ch.scale[1].value.x, 1) && Near(ch.scale[1].value.y, 1));
        // clip 1: translation (0,0,0) -> (0,8,0) over 0..2; the rotation stays at its rest value
        const ImportedChannel& c1 = m.clips[1].channels[0];
        CHECK(c1.position.size() == 2 && Near(c1.position[1].time, 2) && Near(c1.position[1].value.y, 8) && Near(c1.rotation[1].value.w, 1));
        // the Metric "time" scales the key times
        ImportedModel t2;
        CHECK(Read("Metric (key = \"time\") {float {2}}\n" + e4::OgexAnim(), t2, err) && Near(t2.clips[0].channels[0].position[1].time, 1.0f) && Near(t2.clips[0].channels[0].position[2].time, 2.0f));
        // a track that names no transform, or whose key counts differ: skipped with a note
        std::string t = e4::OgexAnim();
        t.replace(t.find("target = %r"), 11, "target = %zz");
        ImportedModel b1;
        CHECK(Read(t, b1, err) && b1.clips[0].channels[0].position.size() == 2 && HasNote(b1, "target is not a named transform"));
        t = e4::OgexAnim();
        t.replace(t.find("{0, 0.5, 1}"), 11, "{0, 0.5}");
        ImportedModel b2;
        CHECK(Read(t, b2, err) && HasNote(b2, "key counts do not match"));
        // keys out of order are sorted
        t = e4::OgexAnim();
        t.replace(t.find("{0, 1}}}"), 8, "{1, 0}}}");
        t.replace(t.find("{{0, 0, 0}, {2, 4, 6}}"), 22, "{{2, 4, 6}, {0, 0, 0}}");
        ImportedModel b3;
        CHECK(Read(t, b3, err) && HasNote(b3, "sorted") && Near(b3.clips[0].channels[0].position[2].value.z, 6));
        // negative times: the track is skipped
        t = e4::OgexAnim();
        t.replace(t.find("{0, 1}}}"), 8, "{-1, 1}}}");
        ImportedModel b4;
        CHECK(Read(t, b4, err) && HasNote(b4, "negative or huge times"));
    }

    // ---- refusals ----------------------------------------------------------------------------------------------------------------
    {
        ImportedModel m; std::string err;
        CHECK(!Read("", m, err) && m.nodes.empty() && !err.empty());
        CHECK(!Read("Node {}", m, err) && m.parts.empty() && m.nodes.empty());
        CHECK(!Read("Metric (key = \"up\") {string {\"y\"}}", m, err));
        CHECK(!Read("Node { float {1e999} }", m, err));
        const std::string t = e4::OgexQuad();
        for (std::size_t cut = 0; cut < t.size(); cut += 7) {   // every 7th truncation: refused or read, never worse, and a refusal leaves nothing behind
            ImportedModel c; std::string e;
            const bool ok = Read(t.substr(0, cut), c, e);
            CHECK(ok ? fx::Sane(c) : (c.parts.empty() && c.nodes.empty() && !e.empty()));
        }
    }
    TEST_MAIN_RESULT();
}
