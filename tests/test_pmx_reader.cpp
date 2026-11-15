// Our own PMX reader (engine/src/Render/PmxReader.cpp). SYNTHETIC files built in memory (tests/pmxlws_fixtures.hpp: BuildPmx has
// 6 vertices, 2 materials, 3 bones and 5 morphs, every number checked by hand below), in both text encodings and with index sizes
// of 1, 2 and 4 bytes. Then hostile files: bad headers, counts of 0xFFFFFFFF, offsets past the end, bone parent loops, every
// truncation, every byte replaced, texture paths that climb out of the model's folder. No real .pmx file is in this repository: the
// reader is unverified against real files. Builds on its own with -DOKAY_STANDALONE_TEST (reader source only).
#include "test_framework.hpp"
#include "pmxlws_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include <cmath>
#include <string>

using namespace okay;
using fx::Near;

static bool HasNote(const ImportedModel& m, const char* what) { for (const std::string& n : m.notes) if (n.find(what) != std::string::npos) return true; return false; }
static void Put32(std::string& s, std::size_t off, std::uint32_t v) { for (int i = 0; i < 4; ++i) s[off + (std::size_t)i] = (char)((v >> (8 * i)) & 255); }
static bool Refused(const std::string& bytes, const char* reasonPart) {
    ImportedModel m; std::string err;
    const bool ok = ReadPmxBuffer(bytes, "dir/", m, &err);
    if (ok) { std::fprintf(stderr, "  accepted (wanted a refusal mentioning '%s')\n", reasonPart); return false; }
    if (err.find(reasonPart) == std::string::npos) { std::fprintf(stderr, "  refused with '%s' (wanted '%s')\n", err.c_str(), reasonPart); return false; }
    return m.parts.empty() && m.nodes.empty();
}

static void CheckStandard(const fx::PmxOpts& o) {
    ImportedModel m; std::string err;
    const std::string bytes = fx::BuildPmx(o);
    const bool ok = ReadPmxBuffer(bytes, "dir/", m, &err);
    if (!ok) std::fprintf(stderr, "  refused: %s\n", err.c_str());
    CHECK(ok);
    if (!ok) return;
    CHECK(m.backend == "own" && fx::Sane(m));
    CHECK(m.parts.size() == 2 && fx::TriCount(m) == 3 && m.materials.size() == 2 && m.nodes.size() == 4 && m.clips.empty());
    // the skeleton: bone positions are world positions with z mirrored; a node holds the position relative to its parent
    CHECK(m.nodes[0].name == "Mdl" && m.nodes[1].name == "root" && m.nodes[2].name == "child" && m.nodes[3].name == "extra");
    CHECK(m.nodes[1].parent == 0 && m.nodes[2].parent == 1 && m.nodes[3].parent == 2);
    CHECK(Near(m.nodes[1].position.x, 1) && Near(m.nodes[1].position.y, 2) && Near(m.nodes[1].position.z, -3));
    CHECK(Near(m.nodes[2].position.x, 3) && Near(m.nodes[2].position.y, 4) && Near(m.nodes[2].position.z, -7));
    CHECK(Near(m.nodes[3].position.x, -4) && Near(m.nodes[3].position.y, -6) && Near(m.nodes[3].position.z, 10));
    if (m.parts.size() != 2) return;
    // material 0: the quad (0,1,2) (0,2,3); z mirrored, winding reversed (0,2,1) (0,3,2)
    const ImportedPart& a = m.parts[0];
    CHECK(a.material == 0 && a.mesh.vertices.size() == 4 && a.mesh.triangles.size() == 6 && a.mesh.uvs.size() == 4 && a.mesh.normals.size() == 4);
    if (a.mesh.triangles.size() == 6) { const int want[6] = {0, 2, 1, 0, 3, 2}; for (int i = 0; i < 6; ++i) CHECK(a.mesh.triangles[(std::size_t)i] == want[i]); }
    if (a.mesh.vertices.size() == 4) {
        CHECK(Near(a.mesh.vertices[1].x, 1) && Near(a.mesh.vertices[1].y, 0) && Near(a.mesh.vertices[2].x, 1) && Near(a.mesh.vertices[2].y, 1) && Near(a.mesh.vertices[3].y, 1));
        CHECK(Near(a.mesh.uvs[2].x, 1) && Near(a.mesh.uvs[2].y, 1) && Near(a.mesh.uvs[3].x, 0) && Near(a.mesh.uvs[3].y, 1));   // kept as written
        CHECK(Near(a.mesh.normals[0].z, 1) && Near(a.mesh.normals[0].x, 0));   // the file's (0,0,-1) with z mirrored
        // the counter clockwise normal of the first triangle (0,2,1): (v2 - v0) x (v1 - v0) = (1,1,0) x (1,0,0) = (0,0,-1)
        const Vec3 c = Vec3::Cross(a.mesh.vertices[2] - a.mesh.vertices[0], a.mesh.vertices[1] - a.mesh.vertices[0]);
        CHECK(Near(c.x, 0) && Near(c.y, 0) && Near(c.z, -1));
    }
    // skin: 3 bones and one root bone for the vertex that has none
    const bool unassigned = !o.qdef;
    const std::size_t nSkin = unassigned ? 4 : 3;   // with no vertex lacking a bone there is no root bone
    CHECK(a.bones.size() == nSkin && a.jointIdx.size() == 4 && a.jointWt.size() == 4);
    if (a.bones.size() == nSkin && a.jointIdx.size() == 4) {
        CHECK(a.bones[0].name == "root" && a.bones[1].name == "child" && a.bones[2].name == "extra" && (!unassigned || a.bones[3].name == "Mdl"));
        const Vec3 o0 = a.bones[0].inverseBind.MultiplyPoint(Vec3{1, 2, -3});   // the inverse bind takes the bone's own position to the origin
        CHECK(Near(o0.x, 0) && Near(o0.y, 0) && Near(o0.z, 0));
        const Vec3 o1 = a.bones[1].inverseBind.MultiplyPoint(Vec3{4, 6, -10});
        CHECK(Near(o1.x, 0) && Near(o1.y, 0) && Near(o1.z, 0));
        CHECK(a.jointIdx[0][0] == 0 && Near(a.jointWt[0][0], 1) && Near(a.jointWt[0][1], 0));                                          // BDEF1
        CHECK(a.jointIdx[2][0] == 0 && a.jointIdx[2][1] == 1 && Near(a.jointWt[2][0], 0.75f) && Near(a.jointWt[2][1], 0.25f));        // BDEF2
        CHECK(a.jointIdx[3][0] == 0 && a.jointIdx[3][1] == 1 && Near(a.jointWt[3][0], 0.5f) && Near(a.jointWt[3][1], 0.5f) && Near(a.jointWt[3][2], 0));   // BDEF4
    }
    // material 1: the triangle (1,4,5) -> vertices v1 v4 v5 -> (0,2,1)
    const ImportedPart& b = m.parts[1];
    CHECK(b.material == 1 && b.mesh.vertices.size() == 3 && b.mesh.triangles.size() == 3 && b.mesh.triangles[0] == 0 && b.mesh.triangles[1] == 2 && b.mesh.triangles[2] == 1);
    if (b.mesh.vertices.size() == 3 && b.jointIdx.size() == 3) {
        CHECK(Near(b.mesh.vertices[1].z, -2) && Near(b.mesh.vertices[2].x, 1) && Near(b.mesh.vertices[2].z, -2));
        CHECK(b.jointIdx[1][0] == 1 && b.jointIdx[1][1] == 0 && Near(b.jointWt[1][0], 0.25f) && Near(b.jointWt[1][1], 0.75f));        // SDEF as a two-bone blend
        if (unassigned) CHECK(b.jointIdx[2][0] == 3 && Near(b.jointWt[2][0], 1));                                                        // no bone: the root bone
        else CHECK(b.jointIdx[2][0] == 2 && b.jointIdx[2][1] == 0 && Near(b.jointWt[2][0], 0.75f) && Near(b.jointWt[2][1], 0.25f));      // QDEF: 3 and 1, normalised
    }
    // materials
    CHECK(m.materials[0].name == "Mat0" && m.materials[0].hasDiffuse && Near(m.materials[0].diffuse.r, 1) && Near(m.materials[0].diffuse.g, 0.5f) && Near(m.materials[0].diffuse.b, 0.25f) && Near(m.materials[0].diffuse.a, 0.75f));
    CHECK(Near(m.materials[0].shininess, 20) && m.materials[0].texture == "dir/tex/a.png");
    CHECK(m.materials[1].name == "Mat1" && m.materials[1].texture.empty() && m.materials[1].shininess < 0);
    CHECK(HasNote(m, "3 bone(s) as nodes") && HasNote(m, "1 SDEF vertex(es)"));
    CHECK(HasNote(m, o.flipMorph ? "6 morph(s) read and checked (7 offsets" : "5 morph(s) read and checked (6 offsets"));
    CHECK(HasNote(m, "vertex 1, bone 1, UV 1, material 1"));
    CHECK(HasNote(m, "inherit-rotation / inherit-move (1), fixed axes (1), local axes (1), external parents (1) and IK chains (1)"));
    CHECK(HasNote(m, "1 material(s)") == false);
    CHECK(HasNote(m, "without a valid bone") == unassigned);
    CHECK(!HasNote(m, "that do not exist") && !HasNote(m, "refused"));
}

int main() {
    RUN_SUITE("pmx_reader");
    // ---- valid files: both encodings, every index size, version 2.0 and 2.1, extra UV sets ----
    {
        fx::PmxOpts o;
        CheckStandard(o);                                                          // UTF-16, vertex 2 / texture 1 / material 1 / bone 2 / morph 1
        o = fx::PmxOpts(); o.utf8 = true; o.vsz = 1; o.bsz = 1; CheckStandard(o);  // UTF-8, 1 byte indices
        o = fx::PmxOpts(); o.utf8 = true; o.vsz = o.tsz = o.msz = o.bsz = o.mosz = o.rsz = 4; o.addUv = 2; o.version = 2.1f; CheckStandard(o);
        o = fx::PmxOpts(); o.vsz = 4; o.tsz = 2; o.msz = 2; o.bsz = 1; o.mosz = 2; o.rsz = 2; o.addUv = 4; CheckStandard(o);
        o = fx::PmxOpts(); o.version = 2.1f; o.qdef = true; o.flipMorph = true; CheckStandard(o);   // QDEF and the flip morph exist from 2.1
    }
    {   // a header that lists more than 8 globals: the extra ones are skipped
        std::string s = fx::BuildPmx();
        s.insert(17, std::string("\x05\x06", 2));   // two more globals after the eight
        s[8] = 10;
        ImportedModel m; std::string err;
        CHECK(ReadPmxBuffer(s, "", m, &err) && fx::Sane(m) && m.parts.size() == 2);
    }
    {   // the file ends after the morphs: display frames, rigid bodies and joints are not read, so this is fine; one byte less is not
        fx::PmxMeta meta;
        const std::string s = fx::BuildPmx(fx::PmxOpts(), &meta);
        ImportedModel m; std::string err;
        CHECK(ReadPmxBuffer(s.substr(0, meta.mainEnd), "", m, &err) && m.parts.size() == 2);
        CHECK(HasNote(m, "display frames, rigid bodies, joints and soft bodies are not read"));
    }
    // ---- every truncation before the end of the morph list is refused, with no model left behind ----
    {
        fx::PmxMeta meta;
        const std::string s = fx::BuildPmx(fx::PmxOpts(), &meta);
        int accepted = 0;
        for (std::size_t len = 0; len < meta.mainEnd; ++len) {
            ImportedModel m; std::string err;
            if (ReadPmxBuffer(s.substr(0, len), "", m, &err)) { ++accepted; std::fprintf(stderr, "  a file cut at %zu of %zu bytes was accepted\n", len, meta.mainEnd); }
            else if (!m.parts.empty() || !m.nodes.empty()) ++accepted;
        }
        CHECK(accepted == 0);
        const std::string u = fx::BuildPmx([] { fx::PmxOpts o; o.utf8 = true; o.vsz = 4; o.bsz = 4; return o; }(), &meta);
        accepted = 0;
        for (std::size_t len = 0; len < meta.mainEnd; ++len) { ImportedModel m; std::string err; if (ReadPmxBuffer(u.substr(0, len), "", m, &err)) ++accepted; }
        CHECK(accepted == 0);
    }
    // ---- every byte replaced by 0xFF, 0x00 and 0x7F, and random flips: never a crash, and what is read is sane ----
    {
        const std::string s = fx::BuildPmx();
        int ok = 0, bad = 0;
        for (std::size_t i = 0; i < s.size(); ++i) for (char v : {'\xFF', '\x00', '\x7F'}) {
            std::string t = s; t[i] = v;
            ImportedModel m; std::string err;
            if (ReadPmxBuffer(t, "d/", m, &err)) { ++ok; CHECK(fx::Sane(m)); } else { ++bad; CHECK(m.parts.empty()); }
        }
        for (int it = 0; it < 3000; ++it) {
            std::string t = s;
            const int flips = 1 + (int)(fx::Rnd() % 4);
            for (int f = 0; f < flips; ++f) t[(std::size_t)(fx::Rnd() % t.size())] ^= (char)(1u << (fx::Rnd() % 8));
            ImportedModel m; std::string err;
            if (ReadPmxBuffer(t, "d/", m, &err)) { ++ok; CHECK(fx::Sane(m)); } else ++bad;
        }
        CHECK(ok > 0 && bad > 0);   // some changes are harmless, some are refused
    }
    // ---- header ----
    {
        std::string s = fx::BuildPmx();
        { std::string t = s; t[0] = 'X'; CHECK(Refused(t, "not a PMX file")); }
        CHECK(Refused(std::string("PMX "), "not a PMX file"));
        CHECK(Refused(std::string(), "not a PMX file"));
        { std::string t = s; const float v = 3.0f; std::memcpy(&t[4], &v, 4); CHECK(Refused(t, "unsupported version")); }
        { std::string t = s; const float v = 1.0f; std::memcpy(&t[4], &v, 4); CHECK(Refused(t, "unsupported version")); }
        { std::string t = s; const std::uint32_t nan = 0x7FC00000u; std::memcpy(&t[4], &nan, 4); CHECK(Refused(t, "unsupported version")); }
        { std::string t = s; t[8] = 7; CHECK(Refused(t, "only 7 globals")); }
        { std::string t = s; t[9] = 2; CHECK(Refused(t, "text encoding")); }
        { std::string t = s; t[10] = 5; CHECK(Refused(t, "additional UV")); }
        for (int i = 11; i <= 16; ++i) { std::string t = s; t[(std::size_t)i] = 3; CHECK(Refused(t, "index size")); t[(std::size_t)i] = 0; CHECK(Refused(t, "index size")); t[(std::size_t)i] = 8; CHECK(Refused(t, "index size")); }
        { std::string t = s; Put32(t, 17, 0xFFFFFFFFu); CHECK(Refused(t, "text of")); }
        { std::string t = s; Put32(t, 17, 0x7FFFFFFFu); CHECK(Refused(t, "text of")); }
        { std::string t = s; Put32(t, 17, 5); CHECK(Refused(t, "odd number of bytes")); }          // UTF-16 text of 5 bytes
        { std::string t = s; Put32(t, 17, 2000); CHECK(Refused(t, "past the end")); }
    }
    // ---- lone surrogates in UTF-16 names do not stop the read ----
    {
        fx::PmxOpts o;
        std::string s = fx::BuildPmx(o);
        // replace the model name "Mdl" (3 UTF-16 units) by the lone surrogate D800 and a trailing high one: lengths stay 6
        s[21] = '\x00'; s[22] = '\xD8';   // first unit 0xD800
        ImportedModel m; std::string err;
        CHECK(ReadPmxBuffer(s, "", m, &err) && fx::Sane(m));
        CHECK(m.nodes[0].name.compare(0, 3, "\xEF\xBF\xBD") == 0);   // U+FFFD, then "dl"
    }
    // ---- counts: negative, 0xFFFFFFFF, over the budget, more than the file can hold ----
    {
        fx::PmxMeta meta;
        const std::string s = fx::BuildPmx(fx::PmxOpts(), &meta);
        struct Case { std::size_t off; std::uint32_t v; const char* why; };
        const Case cases[] = {
            {meta.vertCount, 0xFFFFFFFFu, "vertex count"}, {meta.vertCount, 2000001u, "(budget)"}, {meta.vertCount, 1000000u, "more than the rest of the file"},
            {meta.faceCount, 0xFFFFFFFFu, "face index count"}, {meta.faceCount, 12000003u, "(budget)"}, {meta.faceCount, 30000u, "more than the rest of the file"}, {meta.faceCount, 10u, "not a multiple of 3"},
            {meta.texCount, 0xFFFFFFFFu, "texture count"}, {meta.texCount, 100001u, "(budget)"}, {meta.texCount, 90000u, "more than the rest of the file"},
            {meta.matCount, 0xFFFFFFFFu, "material count"}, {meta.matCount, 100001u, "(budget)"}, {meta.matCount, 90000u, "more than the rest of the file"},
            {meta.boneCount, 0xFFFFFFFFu, "bone count"}, {meta.boneCount, 100001u, "(budget)"}, {meta.boneCount, 90000u, "more than the rest of the file"},
            {meta.morphCount, 0xFFFFFFFFu, "morph count"}, {meta.morphCount, 100001u, "(budget)"}, {meta.morphCount, 90000u, "more than the rest of the file"},
        };
        for (const Case& c : cases) { std::string t = s; Put32(t, c.off, c.v); if (!Refused(t, c.why)) std::fprintf(stderr, "  (case offset %zu value %u)\n", c.off, (unsigned)c.v); CHECK(Refused(t, c.why)); }
        // offsets of the first morph (vertex morph, 2 offsets): its offset count sits after two UTF-16 names, the panel and the type
        const std::size_t cnt = meta.firstMorph + (4 + 2) * 2 + 2;
        { std::string t = s; Put32(t, cnt, 0xFFFFFFFFu); CHECK(Refused(t, "offsets")); }
        { std::string t = s; Put32(t, cnt, 8000001u); CHECK(Refused(t, "(budget)")); }
        { std::string t = s; Put32(t, cnt, 60000u); CHECK(Refused(t, "run past the end")); }
        { std::string t = s; t[meta.firstMorph + (4 + 2) * 2 + 1] = 11; ImportedModel m; std::string err; CHECK(ReadPmxBuffer(t, "", m, &err) && HasNote(m, "unknown type stopped the walk")); }   // the morph type is unknown: the walk stops, the model is still read
    }
    // ---- materials ----
    {
        fx::PmxMeta meta;
        const std::string s = fx::BuildPmx(fx::PmxOpts(), &meta);
        // the surface count of material 0 is the last 4 bytes of its record; find it by searching the known value 6 after the memo
        std::size_t at = std::string::npos;
        for (std::size_t i = meta.matCount + 4; i + 4 <= meta.boneCount; ++i) if (s[i] == 6 && s[i + 1] == 0 && s[i + 2] == 0 && s[i + 3] == 0 && s.compare(i - 8, 8, std::string("m\0e\0m\0o\0", 8)) == 0) at = i;
        CHECK(at != std::string::npos);
        if (at != std::string::npos) {
            { std::string t = s; Put32(t, at, 7); CHECK(Refused(t, "not a multiple of 3")); }
            { std::string t = s; Put32(t, at, 0xFFFFFFFFu); CHECK(Refused(t, "not a multiple of 3, or negative")); }
            { std::string t = s; Put32(t, at, 9); CHECK(Refused(t, "cover")); }   // 9 + 3 = 12 > 9 face indices
            { std::string t = s; Put32(t, at, 3); ImportedModel m; std::string err; CHECK(ReadPmxBuffer(t, "", m, &err) && HasNote(m, "longer than the materials cover")); CHECK(m.parts.size() == 3 && m.materials.size() == 2); }   // the face list is longer: the rest is one part without a material
        }
    }
    // ---- vertices ----
    {
        fx::PmxMeta meta;
        const std::string s = fx::BuildPmx(fx::PmxOpts(), &meta);
        const std::size_t v0 = meta.firstVertex;
        { std::string t = s; const std::uint32_t nan = 0x7FC00000u; std::memcpy(&t[v0], &nan, 4); CHECK(Refused(t, "not a finite number")); }   // position x is NaN
        { std::string t = s; const float big = 2e15f; std::memcpy(&t[v0], &big, 4); CHECK(Refused(t, "not a finite number")); }
        { std::string t = s; const std::uint32_t nan = 0x7FC00000u; std::memcpy(&t[v0 + 12], &nan, 4); ImportedModel m; std::string err; CHECK(ReadPmxBuffer(t, "", m, &err) && fx::Sane(m) && HasNote(m, "vertex normal(s) not finite")); }
        { std::string t = s; t[v0 + 12 + 12 + 8] = 9; CHECK(Refused(t, "weight type 9")); }   // the weight type byte of vertex 0
        { std::string t = s; t[v0 + 12 + 12 + 8] = 4; CHECK(Refused(t, "QDEF needs version 2.1")); }
        // a bone index past the bone list: ignored, the vertex falls back to the root
        { std::string t = s; t[v0 + 12 + 12 + 8 + 1] = 100; t[v0 + 12 + 12 + 8 + 2] = 0; ImportedModel m; std::string err; CHECK(ReadPmxBuffer(t, "", m, &err) && fx::Sane(m) && HasNote(m, "bone reference(s) to bones that do not exist ignored")); }
        // face indices past the vertex list: the triangle is skipped
        { std::string t = s; t[meta.faceCount + 4] = 100; ImportedModel m; std::string err; CHECK(ReadPmxBuffer(t, "", m, &err) && fx::Sane(m) && HasNote(m, "past the vertex list") && fx::TriCount(m) == 2); }
        // every triangle bad: nothing to show
        { std::string t = s; for (int i = 0; i < 9; ++i) t[meta.faceCount + 4 + (std::size_t)i * 2] = 100; CHECK(Refused(t, "no usable triangle")); }
    }
    // ---- bones: parent loops, a bone that is its own parent, a parent that does not exist ----
    {
        fx::PmxOpts o;
        o.par[0] = 1; o.par[1] = 0; o.par[2] = 1;   // root and child are each other's parent
        ImportedModel m; std::string err;
        CHECK(ReadPmxBuffer(fx::BuildPmx(o), "", m, &err) && fx::Sane(m) && m.nodes.size() == 4 && HasNote(m, "1 bone parent loop(s) were cut"));
        o.par[0] = 0; o.par[1] = 0; o.par[2] = 2;   // self parents
        CHECK(ReadPmxBuffer(fx::BuildPmx(o), "", m, &err) && fx::Sane(m) && HasNote(m, "2 bone(s) that name themselves"));
        o.par[0] = 77; o.par[1] = -5; o.par[2] = 2000000000LL;
        CHECK(ReadPmxBuffer(fx::BuildPmx(o), "", m, &err) && fx::Sane(m) && HasNote(m, "3 bone(s) with a parent index that does not exist"));
        for (std::size_t i = 1; i < m.nodes.size(); ++i) CHECK(m.nodes[i].parent == 0);
        o.par[0] = 2; o.par[1] = 0; o.par[2] = 1;   // a ring of three
        CHECK(ReadPmxBuffer(fx::BuildPmx(o), "", m, &err) && fx::Sane(m) && HasNote(m, "parent loop"));
    }
    {   // duplicate bone names: "a", "a_1", "a" must give three different node and skin names
        fx::PmxOpts o; o.boneName[0] = "a"; o.boneName[1] = "a_1"; o.boneName[2] = "a";
        ImportedModel m; std::string err;
        CHECK(ReadPmxBuffer(fx::BuildPmx(o), "", m, &err) && m.nodes.size() == 4);
        CHECK(m.nodes[1].name != m.nodes[2].name && m.nodes[1].name != m.nodes[3].name && m.nodes[2].name != m.nodes[3].name && m.nodes[0].name != m.nodes[1].name);
        CHECK(m.nodes[1].name == "a" && m.nodes[2].name == "a_1" && m.nodes[3].name == "a_2");
        CHECK(!m.parts.empty() && m.parts[0].bones[2].name == "a_2");
        o.boneName[0] = std::string(500, 'x');   // a long name is cut
        CHECK(ReadPmxBuffer(fx::BuildPmx(o), "", m, &err) && m.nodes[1].name.size() == 64);
    }
    {   // IK link count and a bone flag that asks for IK in a truncated record
        fx::PmxMeta meta;
        const std::string s = fx::BuildPmx(fx::PmxOpts(), &meta);
        // bone "child" is the second record; its IK link count (value 1) follows target(2) loop(4) limit(4): find it by the known pattern 5 0.5
        std::size_t at = std::string::npos;
        const float lim = 0.5f; char pat[8] = {5, 0, 0, 0}; std::memcpy(pat + 4, &lim, 4);
        for (std::size_t i = meta.boneCount; i + 12 <= meta.morphCount; ++i) if (std::memcmp(&s[i], pat, 8) == 0) at = i + 8;
        CHECK(at != std::string::npos);
        if (at != std::string::npos) {
            { std::string t = s; Put32(t, at, 0x7FFFFFFFu); CHECK(Refused(t, "IK links")); }
            { std::string t = s; Put32(t, at, 0xFFFFFFFFu); CHECK(Refused(t, "IK links")); }
            { std::string t = s; Put32(t, at, 4000); CHECK(Refused(t, "run past the end")); }
        }
    }
    // ---- texture paths stay inside the model's folder ----
    {
        const char* refused[] = {"../../etc/passwd", "..\\up.png", "/etc/passwd", "\\\\host\\share\\x.png", "C:\\win\\x.png", "c:/x.png", "a/../../b.png", "tex/../../../x.png"};
        for (const char* t : refused) {
            fx::PmxOpts o; o.texName = t;
            ImportedModel m; std::string err;
            CHECK(ReadPmxBuffer(fx::BuildPmx(o), "dir/", m, &err));
            CHECK(m.materials.size() == 2 && m.materials[0].texture.empty());   // the texture is dropped, the model is read
            CHECK(HasNote(m, "1 texture(s) refused"));
            if (!m.materials.empty() && !m.materials[0].texture.empty()) std::fprintf(stderr, "  texture '%s' was kept as '%s'\n", t, m.materials[0].texture.c_str());
        }
        const char* allowed[] = {"a.png", "sub\\dir\\b.png", "./c.png", "sub/../d.png"};
        const char* want[] = {"dir/a.png", "dir/sub/dir/b.png", "dir/c.png", "dir/sub/../d.png"};
        for (int i = 0; i < 4; ++i) {
            fx::PmxOpts o; o.texName = allowed[i];
            ImportedModel m; std::string err;
            CHECK(ReadPmxBuffer(fx::BuildPmx(o), "dir/", m, &err) && m.materials[0].texture == want[i]);
        }
        fx::PmxOpts o; o.texName = "";   // an empty texture name: none
        ImportedModel m; std::string err;
        CHECK(ReadPmxBuffer(fx::BuildPmx(o), "dir/", m, &err) && m.materials[0].texture.empty() && !HasNote(m, "refused"));
    }
    TEST_MAIN_RESULT();
}
