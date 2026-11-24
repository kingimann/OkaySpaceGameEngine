// The binary OGRE skeleton (.skeleton). SYNTHETIC fixtures, every byte written by hand here from the public description of the
// binary skeleton chunks (docs/content/model_readers.md, OGRE section): a u16 chunk id and a u32 length that counts the 6-byte
// header; 0x1000 header (then the version text up to a line feed, no length), 0x1010 blend mode (u16), 0x2000 bone (name up to a
// line feed, u16 handle, position 3 floats, orientation x y z w, and a scale of 3 floats when the chunk is long enough), 0x3000
// parent (child handle u16, parent handle u16), 0x4000 animation (name, length f32), 0x4010 base info, 0x4100 track (bone handle
// u16), 0x4110 keyframe (time f32, rotation x y z w, translation 3 floats, scale when the chunk is long enough), 0x5000 linked
// skeleton (name, scale f32). The skeleton here is the one of tests/test_ogre_skeleton_reader.cpp, whose numbers were worked out by
// hand there: the binary form must give exactly what the XML form gives. With OKAY_MODEL_CORPUS set to the "test" directory of an
// Assimp 5.4.3 checkout (docs/content/model_corpus.md) the real fish.skeleton and ninja.skeleton are read and compared with the
// .skeleton.xml files that ship next to them, and the real binary .mesh files are skinned with them.
// Builds on its own (OKAY_STANDALONE_TEST, reader source only).
#include "test_framework.hpp"
#include "okay/Render/ModelData.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

using namespace okay;

static bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) <= e; }
static bool HasNote(const ImportedModel& m, const char* what) { for (const std::string& n : m.notes) if (n.find(what) != std::string::npos) return true; return false; }

static void P16(std::string& s, unsigned v) { s.push_back((char)(v & 255)); s.push_back((char)((v >> 8) & 255)); }
static void P32(std::string& s, std::uint32_t v) { for (int i = 0; i < 4; ++i) s.push_back((char)((v >> (8 * i)) & 255)); }
static void PF(std::string& s, float f) { std::uint32_t u; std::memcpy(&u, &f, 4); P32(s, u); }
// a chunk: id, length (header included), body
static std::string Chunk(unsigned id, const std::string& body) { std::string s; P16(s, id); P32(s, (std::uint32_t)(body.size() + 6)); return s + body; }

static const float kS = 0.70710678118654752f;   // sin 45 = cos 45: a 90 degree turn about Z is (0, 0, kS, kS)

static std::string Bone(const char* name, unsigned handle, float px, float py, float pz, float qx, float qy, float qz, float qw, bool scale = false, float sx = 1, float sy = 1, float sz = 1) {
    std::string b = std::string(name) + "\n";
    P16(b, handle);
    PF(b, px); PF(b, py); PF(b, pz);
    PF(b, qx); PF(b, qy); PF(b, qz); PF(b, qw);
    if (scale) { PF(b, sx); PF(b, sy); PF(b, sz); }
    return Chunk(0x2000, b);
}
static std::string Parent(unsigned child, unsigned parent) { std::string b; P16(b, child); P16(b, parent); return Chunk(0x3000, b); }
static std::string Key(float t, float qx, float qy, float qz, float qw, float tx, float ty, float tz, bool scale, float sx = 1, float sy = 1, float sz = 1) {
    std::string b;
    PF(b, t); PF(b, qx); PF(b, qy); PF(b, qz); PF(b, qw); PF(b, tx); PF(b, ty); PF(b, tz);
    if (scale) { PF(b, sx); PF(b, sy); PF(b, sz); }
    return Chunk(0x4110, b);
}
static std::string Track(unsigned handle, const std::string& keys) { std::string b; P16(b, handle); return Chunk(0x4100, b + keys); }

// The skeleton of test_ogre_skeleton_reader.cpp in binary form. Root(0) at (0,1,0); Arm(1) at (2,0,0) turned 90 about Z; Hand(2)
// at (1,0,0) under Arm (written with an explicit unit scale); E1(3) and E2(4) under Root. Animation "wave" (length 2): a track on
// Arm with keys at 0 (bind pose, scale written), 1 (translate (0,3,0), turn 90 about Z, scale (2,1,1)) and 2 (no scale written),
// and a track on handle 9, which the skeleton does not have.
static std::string BinSkeleton(const char* version = "[Serializer_v1.80]", bool withLink = false, bool withBase = false) {
    std::string s;
    P16(s, 0x1000); s += version; s += "\n";
    if (std::string(version) != "[Serializer_v1.10]") { std::string b; P16(b, 0); s += Chunk(0x1010, b); }
    s += Bone("Root", 0, 0, 1, 0, 0, 0, 0, 1);
    s += Bone("Arm", 1, 2, 0, 0, 0, 0, kS, kS);
    s += Bone("Hand", 2, 1, 0, 0, 0, 0, 0, 1, true, 1, 1, 1);
    s += Bone("E1", 3, 0, 0, 0, 0, 0, 0, 1);
    s += Bone("E2", 4, 0, 0, 0, 0, 0, 0, 1);
    s += Parent(1, 0); s += Parent(2, 1); s += Parent(3, 0); s += Parent(4, 0);
    std::string a = "wave\n";
    PF(a, 2.0f);
    if (withBase) { std::string bi = "idle\n"; PF(bi, 0.0f); a += Chunk(0x4010, bi); }
    a += Track(1, Key(0, 0, 0, 0, 1, 0, 0, 0, true) + Key(1, 0, 0, kS, kS, 0, 3, 0, true, 2, 1, 1) + Key(2, 0, 0, 0, 1, 0, 0, 0, false));
    a += Track(9, Key(0, 0, 0, 0, 1, 0, 0, 0, false));
    s += Chunk(0x4000, a);
    if (withLink) { std::string l = "other.skeleton\n"; PF(l, 1.0f); s += Chunk(0x5000, l); }
    return s;
}

static const char* kSkeletonXml = R"(<skeleton><bones>
<bone id="0" name="Root"><position x="0" y="1" z="0"/></bone>
<bone id="1" name="Arm"><position x="2" y="0" z="0"/><rotation angle="1.5707963267948966"><axis x="0" y="0" z="1"/></rotation></bone>
<bone id="2" name="Hand"><position x="1" y="0" z="0"/></bone>
<bone id="3" name="E1"/><bone id="4" name="E2"/></bones>
<bonehierarchy><boneparent bone="Arm" parent="Root"/><boneparent bone="Hand" parent="Arm"/><boneparent bone="E1" parent="Root"/><boneparent bone="E2" parent="Root"/></bonehierarchy>
<animations><animation name="wave" length="2"><tracks><track bone="Arm"><keyframes>
<keyframe time="0"/>
<keyframe time="1"><translate x="0" y="3" z="0"/><rotate angle="1.5707963267948966"><axis x="0" y="0" z="1"/></rotate><scale x="2" y="1" z="1"/></keyframe>
<keyframe time="2"/>
</keyframes></track></tracks></animation></animations>
</skeleton>)";

// the XML-skinned binary mesh of test_ogre_skinbin_reader.cpp (three vertices, the same assignments), linking `link`
static void BP16(std::string& s, unsigned v) { P16(s, v); }
static void BChunk(std::string& s, unsigned id, std::uint32_t len) { P16(s, id); P32(s, len); }
static void BAsg(std::string& s, std::uint32_t v, unsigned bone, float w) { BChunk(s, 0x4100, 16); P32(s, v); BP16(s, bone); PF(s, w); }
static std::string BinMesh(const char* link = "x.skeleton") {
    std::string s;
    P16(s, 0x1000); s += "[MeshSerializer_v1.8]\n";
    BChunk(s, 0x3000, 0); s.push_back(1);
    BChunk(s, 0x4000, 0); s += "M\n"; s.push_back(0); P32(s, 3); s.push_back(0); P16(s, 0); P16(s, 1); P16(s, 2);
    BChunk(s, 0x5000, 0); P32(s, 3);
    BChunk(s, 0x5100, 6);
    BChunk(s, 0x5110, 16); P16(s, 0); P16(s, 2); P16(s, 1); P16(s, 0); P16(s, 0);
    BChunk(s, 0x5200, 0); P16(s, 0); P16(s, 12);
    BChunk(s, 0x5210, 0);
    for (float f : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f}) PF(s, f);
    BChunk(s, 0x4010, 8); P16(s, 4);
    BAsg(s, 0, 0, 1.0f);
    BAsg(s, 1, 1, 0.4f); BAsg(s, 1, 2, 0.3f); BAsg(s, 1, 0, 0.2f); BAsg(s, 1, 3, 0.06f); BAsg(s, 1, 4, 0.04f);
    BAsg(s, 9, 0, 1.0f); BAsg(s, 2, 77, 1.0f);
    if (link) { BChunk(s, 0x6000, 0); s += link; s += "\n"; }
    return s;
}
static std::string MeshXml() {
    return R"(<mesh><submeshes><submesh material="M" usesharedvertices="false" operationtype="triangle_list">
<faces count="1"><face v1="0" v2="1" v3="2"/></faces>
<geometry vertexcount="3"><vertexbuffer positions="true"><vertex><position x="0" y="0" z="0"/></vertex><vertex><position x="1" y="0" z="0"/></vertex><vertex><position x="0" y="1" z="0"/></vertex></vertexbuffer></geometry>
<boneassignments><vertexboneassignment vertexindex="0" boneindex="0" weight="1"/><vertexboneassignment vertexindex="1" boneindex="1" weight="0.4"/></boneassignments>
</submesh></submeshes><skeletonlink name="x.skeleton"/></mesh>)";
}

// node by node and key by key, the two skeleton readings agree
static bool SameSkeleton(const ImportedModel& a, const ImportedModel& b, float eps) {
    if (a.nodes.size() != b.nodes.size() || a.clips.size() != b.clips.size()) return false;
    for (std::size_t i = 0; i < a.nodes.size(); ++i) {
        const ImportedNode& x = a.nodes[i]; const ImportedNode& y = b.nodes[i];
        if (x.name != y.name || x.parent != y.parent) return false;
        if (!Near(x.position.x, y.position.x, eps) || !Near(x.position.y, y.position.y, eps) || !Near(x.position.z, y.position.z, eps)) return false;
        const float dot = x.rotation.x * y.rotation.x + x.rotation.y * y.rotation.y + x.rotation.z * y.rotation.z + x.rotation.w * y.rotation.w;
        if (!(std::fabs(dot) > 1 - 1e-4f)) return false;   // q and -q are the same turn
        if (!Near(x.scale.x, y.scale.x, eps) || !Near(x.scale.y, y.scale.y, eps) || !Near(x.scale.z, y.scale.z, eps)) return false;
    }
    for (std::size_t c = 0; c < a.clips.size(); ++c) {
        const ImportedClip& x = a.clips[c]; const ImportedClip& y = b.clips[c];
        if (x.name != y.name || x.channels.size() != y.channels.size()) return false;
        for (std::size_t k = 0; k < x.channels.size(); ++k) {
            const ImportedChannel& p = x.channels[k]; const ImportedChannel& q = y.channels[k];
            if (p.node != q.node || p.position.size() != q.position.size() || p.rotation.size() != q.rotation.size() || p.scale.size() != q.scale.size()) return false;
            for (std::size_t i = 0; i < p.position.size(); ++i) {
                if (!Near(p.position[i].time, q.position[i].time, 1e-4f)) return false;
                const Vec3 u = p.position[i].value, v = q.position[i].value;
                if (!Near(u.x, v.x, eps) || !Near(u.y, v.y, eps) || !Near(u.z, v.z, eps)) return false;
                const Quat r = p.rotation[i].value, s = q.rotation[i].value;
                if (!(std::fabs(r.x * s.x + r.y * s.y + r.z * s.z + r.w * s.w) > 1 - 1e-4f)) return false;
                const Vec3 g = p.scale[i].value, h = q.scale[i].value;
                if (!Near(g.x, h.x, eps) || !Near(g.y, h.y, eps) || !Near(g.z, h.z, eps)) return false;
            }
        }
    }
    return true;
}
static bool SameSkin(const ImportedModel& a, const ImportedModel& b) {
    if (a.parts.size() != b.parts.size()) return false;
    for (std::size_t i = 0; i < a.parts.size(); ++i) {
        const ImportedPart& p = a.parts[i]; const ImportedPart& q = b.parts[i];
        if (p.jointIdx != q.jointIdx || p.bones.size() != q.bones.size()) return false;
        for (std::size_t k = 0; k < p.jointWt.size(); ++k) for (int j = 0; j < 4; ++j) if (!Near(p.jointWt[k][j], q.jointWt[k][j], 1e-4f)) return false;
        for (std::size_t k = 0; k < p.bones.size(); ++k) {
            if (p.bones[k].name != q.bones[k].name) return false;
            for (int e = 0; e < 16; ++e) if (!Near(p.bones[k].inverseBind.m[e], q.bones[k].inverseBind.m[e], 1e-3f * (1 + std::fabs(q.bones[k].inverseBind.m[e])))) return false;
        }
    }
    return true;
}

static void WriteFile(const std::string& path, const std::string& d) { std::ofstream f(path, std::ios::binary); f.write(d.data(), (std::streamsize)d.size()); }
static std::string Slurp(const std::string& path) { std::ifstream f(path, std::ios::binary); std::ostringstream o; o << f.rdbuf(); return o.str(); }
static void SetSwitch(const char* v) {
#ifdef _WIN32
    _putenv_s("OKAY_OWN_OGRE_SKELBIN", v ? v : "");
#else
    if (v) setenv("OKAY_OWN_OGRE_SKELBIN", v, 1); else unsetenv("OKAY_OWN_OGRE_SKELBIN");
#endif
}

static void RealFiles() {
    const char* root = std::getenv("OKAY_MODEL_CORPUS");
    if (!root || !*root) { std::fprintf(stderr, "  real files: OKAY_MODEL_CORPUS not set, skipped\n"); return; }
    const std::string dir = std::string(root) + "/models-nonbsd/Ogre/OgreSDK/";
    for (const char* name : {"fish", "ninja"}) {
        const std::string bin = Slurp(dir + name + ".skeleton"), xml = Slurp(dir + name + ".skeleton.xml"), mesh = Slurp(dir + name + ".mesh");
        if (bin.empty() || xml.empty() || mesh.empty()) { std::fprintf(stderr, "  real files: %s not found, skipped\n", name); continue; }
        ImportedModel b, x; std::string eb, ex;
        CHECK(ReadOgreSkeletonBinary(bin, b, &eb));
        CHECK(ReadOgreSkeletonXml(xml, x, &ex));
        CHECK(b.nodes.size() > 1 && SameSkeleton(b, x, 2e-3f));
        std::fprintf(stderr, "  real files: %s.skeleton: %zu bones, %zu clip(s); the same as %s.skeleton.xml: %s\n", name, b.nodes.size() - 1, b.clips.size(), name,
                     SameSkeleton(b, x, 2e-3f) ? "yes" : "NO");
        ImportedModel mb, mx;
        CHECK(ReadOgreBinaryBuffer(mesh, dir, mb, &eb, nullptr, &bin));
        CHECK(ReadOgreBinaryBuffer(mesh, dir, mx, &ex, nullptr, &xml));
        CHECK(!mb.parts.empty() && !mb.parts[0].bones.empty() && SameSkin(mb, mx));
    }
}

int main() {
    RUN_SUITE("ogre_skelbin");
    const std::string bin = BinSkeleton(), xml = kSkeletonXml;
    {   // the skeleton alone: the same nodes, bind pose and clips as the XML form
        ImportedModel b, x; std::string err;
        CHECK(ReadOgreSkeletonBinary(bin, b, &err));
        CHECK(ReadOgreSkeletonXml(xml, x, &err));
        CHECK(b.backend == "own" && b.parts.empty() && b.nodes.size() == 6 && b.nodes[0].name == "OGRE");
        CHECK(b.nodes[1].name == "Root" && b.nodes[2].name == "Arm" && b.nodes[3].name == "Hand" && b.nodes[3].parent == 2);
        CHECK(Near(b.nodes[2].rotation.z, kS) && Near(b.nodes[2].rotation.w, kS));
        CHECK(SameSkeleton(b, x, 1e-5f));
        // keyframe 1: bind (2,0,0) + (0,3,0); bind turn * 90 = 180 about Z; scale (2,1,1). Keyframe 2 wrote no scale: unit
        CHECK(b.clips.size() == 1 && b.clips[0].name == "wave" && b.clips[0].channels.size() == 1 && b.clips[0].channels[0].node == "Arm");
        const ImportedChannel& c = b.clips[0].channels[0];
        CHECK(c.position.size() == 3 && Near(c.position[1].value.x, 2) && Near(c.position[1].value.y, 3));
        CHECK(Near(std::fabs(c.rotation[1].value.z), 1) && Near(c.rotation[1].value.w, 0, 1e-3f));
        CHECK(Near(c.scale[1].value.x, 2) && Near(c.scale[2].value.x, 1) && Near(c.position[2].time, 2));
        CHECK(HasNote(b, "handle 9"));
    }
    {   // the older version (no blend mode chunk), base info and a linked skeleton: read, the link reported
        ImportedModel a, b; std::string err;
        CHECK(ReadOgreSkeletonBinary(BinSkeleton("[Serializer_v1.10]"), a, &err) && a.nodes.size() == 6 && a.clips.size() == 1);
        CHECK(ReadOgreSkeletonBinary(BinSkeleton("[Serializer_v1.80]", true, true), b, &err) && SameSkeleton(a, b, 1e-6f));
        CHECK(HasNote(b, "other.skeleton"));
    }
    {   // refusals
        ImportedModel m; std::string err;
        CHECK(!ReadOgreSkeletonBinary("", m, &err) && !err.empty());
        std::string be = bin; std::swap(be[0], be[1]);
        CHECK(!ReadOgreSkeletonBinary(be, m, &err) && err.find("big-endian") != std::string::npos);
        std::string mesh = BinMesh();
        CHECK(!ReadOgreSkeletonBinary(mesh, m, &err));   // a mesh is not a skeleton
        std::string h; P16(h, 0x1000); h += "[Serializer_v1.80]\n";
        CHECK(!ReadOgreSkeletonBinary(h, m, &err));       // no bones
        CHECK(!ReadOgreSkeletonBinary(h + Bone("A", 0, 0, 0, 0, 0, 0, 0, 1) + Bone("B", 0, 0, 0, 0, 0, 0, 0, 1), m, &err));   // two bones, one handle
        CHECK(!ReadOgreSkeletonBinary(h + Bone("A", 0, 0, 0, 0, 0, 0, 0, 1) + Bone("A", 1, 0, 0, 0, 0, 0, 0, 1), m, &err));   // two bones, one name
        CHECK(!ReadOgreSkeletonBinary(h + Bone("A", 0, 0, 0, 0, 0, 0, 0, 1) + Parent(0, 7), m, &err));                        // unknown parent
        CHECK(!ReadOgreSkeletonBinary(h + Bone("A", 0, 0, 0, 0, 0, 0, 0, 1) + Bone("B", 1, 0, 0, 0, 0, 0, 0, 1) + Parent(0, 1) + Parent(1, 0), m, &err));   // loop
        CHECK(!ReadOgreSkeletonBinary(h + Bone("A", 0, 0, 0, 0, 0, 0, 0, 1) + Parent(0, 0), m, &err));                        // its own parent
        std::string nanBone = h + Bone("A", 0, std::nanf(""), 0, 0, 0, 0, 0, 1);
        CHECK(!ReadOgreSkeletonBinary(nanBone, m, &err));
        CHECK(m.nodes.empty() && m.clips.empty());
        // children before their parents still come out parent-first
        CHECK(ReadOgreSkeletonBinary(h + Bone("Kid", 0, 1, 0, 0, 0, 0, 0, 1) + Bone("Mum", 1, 0, 0, 0, 0, 0, 0, 1) + Parent(0, 1), m, &err));
        CHECK(m.nodes.size() == 3 && m.nodes[1].name == "Mum" && m.nodes[2].name == "Kid" && m.nodes[2].parent == 1);
    }
    {   // every truncation reads cleanly or is refused with nothing left; every single-byte change does not crash
        for (std::size_t n = 0; n < bin.size(); ++n) {
            ImportedModel t; std::string e;
            const bool ok = ReadOgreSkeletonBinary(bin.substr(0, n), t, &e);
            CHECK(ok || (!e.empty() && t.nodes.empty() && t.clips.empty()));
        }
        int okCount = 0;
        for (std::size_t i = 0; i < bin.size(); ++i) for (int v : {0x00, 0xFF, 0x7F, 0x80}) {
            std::string b = bin; b[i] = (char)v;
            ImportedModel t; std::string e;
            if (ReadOgreSkeletonBinary(b, t, &e)) { ++okCount; CHECK(t.nodes.size() >= 2); } else CHECK(t.nodes.empty());
        }
        CHECK(okCount > 0);
    }
    {   // the mesh readers take a binary skeleton when one is given: the same skin as with the XML skeleton
        ImportedModel b, x; std::string err;
        CHECK(ReadOgreBinaryBuffer(BinMesh(), "", b, &err, nullptr, &bin));
        CHECK(ReadOgreBinaryBuffer(BinMesh(), "", x, &err, nullptr, &xml));
        CHECK(b.parts.size() == 1 && b.parts[0].bones.size() == 6 && b.clips.size() == 1 && SameSkin(b, x));
        ImportedModel c, d;
        CHECK(ReadOgreXmlBuffer(MeshXml(), "", c, &err, nullptr, &bin));
        CHECK(ReadOgreXmlBuffer(MeshXml(), "", d, &err, nullptr, &xml));
        CHECK(c.parts.size() == 1 && c.parts[0].bones.size() == 6 && SameSkin(c, d));
        // a broken binary skeleton: rigid, with the reason
        ImportedModel r;
        const std::string cut = bin.substr(0, 30);
        CHECK(ReadOgreBinaryBuffer(BinMesh(), "", r, &err, nullptr, &cut) && r.parts[0].bones.empty() && HasNote(r, "skeleton could not be read"));
    }
    {   // the file readers find <link> (binary) next to the mesh by default (OKAY_OWN_OGRE_SKELBIN=0 turns that off); the .skeleton.xml comes first
#ifdef _WIN32
        _mkdir("skelbin_tmp");
#else
        mkdir("skelbin_tmp", 0777);
#endif
        WriteFile("skelbin_tmp/w.mesh", BinMesh());
        WriteFile("skelbin_tmp/w.mesh.xml", MeshXml());
        WriteFile("skelbin_tmp/x.skeleton", bin);
        ImportedModel m; std::string err;
        SetSwitch("0");
        CHECK(ReadOgreBinaryFile("skelbin_tmp/w.mesh", m, &err) && m.parts[0].bones.empty() && HasNote(m, "skeleton 'x.skeleton'"));   // off: rigid
        SetSwitch("off");
        CHECK(ReadOgreBinaryFile("skelbin_tmp/w.mesh", m, &err) && m.parts[0].bones.empty());
        SetSwitch(nullptr);   // nothing set: on (the default since 2026-11-23)
        CHECK(ReadOgreBinaryFile("skelbin_tmp/w.mesh", m, &err) && m.parts[0].bones.size() == 6 && m.clips.size() == 1);
        SetSwitch("1");
        CHECK(ReadOgreBinaryFile("skelbin_tmp/w.mesh", m, &err) && m.parts[0].bones.size() == 6 && m.clips.size() == 1);
        CHECK(ReadOgreXmlFile("skelbin_tmp/w.mesh.xml", m, &err) && m.parts[0].bones.size() == 6);
        // with both next to the mesh, the .skeleton.xml is read (a one-bone skeleton tells them apart)
        WriteFile("skelbin_tmp/x.skeleton.xml", "<skeleton><bones><bone id=\"0\" name=\"Root\"/><bone id=\"1\" name=\"Arm\"/><bone id=\"2\" name=\"Hand\"/><bone id=\"3\" name=\"E1\"/><bone id=\"4\" name=\"E2\"/></bones></skeleton>");
        CHECK(ReadOgreBinaryFile("skelbin_tmp/w.mesh", m, &err) && m.parts[0].bones.size() == 6 && m.clips.empty());
        SetSwitch(nullptr);
        std::remove("skelbin_tmp/x.skeleton.xml");
        std::remove("skelbin_tmp/x.skeleton");
        std::remove("skelbin_tmp/w.mesh");
        std::remove("skelbin_tmp/w.mesh.xml");
    }
    RealFiles();
    TEST_MAIN_RESULT();
}
