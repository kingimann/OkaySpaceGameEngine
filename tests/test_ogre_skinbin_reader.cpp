// Skinning of the binary OGRE .mesh with the XML skeleton. SYNTHETIC fixtures, every byte written by hand here from the public
// description of the binary mesh chunks (docs/own_ogre_reader.md): bone assignment = vertex u32, bone u16, weight f32, in
// chunk 0x4100 (submesh) or 0x7000 (mesh, for shared vertices). The expected numbers are the ones of
// tests/test_ogre_skeleton_reader.cpp (worked out by hand there): the same skeleton, the same three vertices, the same assignments.
// Builds on its own (OKAY_STANDALONE_TEST, reader source only).
#include "test_framework.hpp"
#include "okay/Render/ModelData.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
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
static void Chunk(std::string& s, unsigned id, std::uint32_t len) { P16(s, id); P32(s, len); }
static void Asg(std::string& s, unsigned chunkId, std::uint32_t v, unsigned bone, float w) { Chunk(s, chunkId, 16); P32(s, v); P16(s, bone); PF(s, w); }

// Same skeleton as the XML skeleton test: Root(0) at (0,1,0); Arm(1) at (2,0,0) turned 90 about Z; Hand(2) at (1,0,0) under Arm; E1(3), E2(4) under Root.
static const char* kSkeleton = R"(<skeleton><bones>
<bone id="0" name="Root"><position x="0" y="1" z="0"/></bone>
<bone id="1" name="Arm"><position x="2" y="0" z="0"/><rotation angle="1.5707963267948966"><axis x="0" y="0" z="1"/></rotation></bone>
<bone id="2" name="Hand"><position x="1" y="0" z="0"/></bone>
<bone id="3" name="E1"/><bone id="4" name="E2"/></bones>
<bonehierarchy><boneparent bone="Arm" parent="Root"/><boneparent bone="Hand" parent="Arm"/><boneparent bone="E1" parent="Root"/><boneparent bone="E2" parent="Root"/></bonehierarchy>
</skeleton>)";

static void Geometry(std::string& s) {   // 3 vertices (0,0,0) (1,0,0) (0,1,0), positions only, one buffer of stride 12
    Chunk(s, 0x5000, 0); P32(s, 3);
    Chunk(s, 0x5100, 6);
    Chunk(s, 0x5110, 16); P16(s, 0); P16(s, 2); P16(s, 1); P16(s, 0); P16(s, 0);
    Chunk(s, 0x5200, 0); P16(s, 0); P16(s, 12);
    Chunk(s, 0x5210, 0);
    for (float f : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f}) PF(s, f);
}
// The assignments of the XML test: v0 Root 1; v1 Arm .4, Hand .3, Root .2, E1 .06, E2 .04; vertex 9 (bad); v2 bone 77 (bad)
static void Assignments(std::string& s, unsigned id) {
    Asg(s, id, 0, 0, 1.0f);
    Asg(s, id, 1, 1, 0.4f); Asg(s, id, 1, 2, 0.3f); Asg(s, id, 1, 0, 0.2f); Asg(s, id, 1, 3, 0.06f); Asg(s, id, 1, 4, 0.04f);
    Asg(s, id, 9, 0, 1.0f); Asg(s, id, 2, 77, 1.0f);
}
static std::string BinMesh(bool shared, bool withLink = true) {
    std::string s;
    P16(s, 0x1000); s += "[MeshSerializer_v1.8]\n";
    Chunk(s, 0x3000, 0); s.push_back(1);
    if (shared) {
        Geometry(s);
        Chunk(s, 0x4000, 0); s += "M\n"; s.push_back(1); P32(s, 3); s.push_back(0); P16(s, 0); P16(s, 1); P16(s, 2);
        Chunk(s, 0x4010, 8); P16(s, 4);
        if (withLink) { Chunk(s, 0x6000, 0); s += "x.skeleton\n"; }
        Assignments(s, 0x7000);
    } else {
        Chunk(s, 0x4000, 0); s += "M\n"; s.push_back(0); P32(s, 3); s.push_back(0); P16(s, 0); P16(s, 1); P16(s, 2);
        Geometry(s);
        Chunk(s, 0x4010, 8); P16(s, 4);
        Assignments(s, 0x4100);
        if (withLink) { Chunk(s, 0x6000, 0); s += "x.skeleton\n"; }
    }
    return s;
}

static void CheckSkinned(const ImportedModel& m) {
    CHECK(m.parts.size() == 1 && m.nodes.size() == 6);
    const ImportedPart& p = m.parts[0];
    CHECK(p.mesh.vertices.size() == 3 && p.jointIdx.size() == 3 && p.jointWt.size() == 3);
    CHECK(p.bones.size() == 6 && p.bones[0].name == "Root" && p.bones[1].name == "Arm" && p.bones[2].name == "Hand" && p.bones[5].name == "OGRE");
    // the world point (2,2,0) is Hand's origin; in Arm's frame (turned 90 about Z, at (2,1,0)) it is (1,0,0)
    const Vec3 h = p.bones[2].inverseBind.MultiplyPoint(Vec3{2, 2, 0});
    CHECK(Near(h.x, 0) && Near(h.y, 0) && Near(h.z, 0));
    const Vec3 a = p.bones[1].inverseBind.MultiplyPoint(Vec3{2, 2, 0});
    CHECK(Near(a.x, 1) && Near(a.y, 0) && Near(a.z, 0));
    for (std::size_t i = 0; i < 3; ++i) {
        const Vec3 v = p.mesh.vertices[i];
        if (v.x == 0 && v.y == 0) { CHECK(p.jointIdx[i][0] == 0 && Near(p.jointWt[i][0], 1) && Near(p.jointWt[i][1], 0)); }
        else if (v.x == 1) {   // four strongest of five, over their sum 0.96
            CHECK(p.jointIdx[i][0] == 1 && p.jointIdx[i][1] == 2 && p.jointIdx[i][2] == 0 && p.jointIdx[i][3] == 3);
            CHECK(Near(p.jointWt[i][0], 0.4f / 0.96f) && Near(p.jointWt[i][1], 0.3f / 0.96f) && Near(p.jointWt[i][2], 0.2f / 0.96f) && Near(p.jointWt[i][3], 0.06f / 0.96f));
        } else { CHECK(p.jointIdx[i][0] == 5 && Near(p.jointWt[i][0], 1)); }   // no usable weight: the root node
    }
    CHECK(HasNote(m, "2 bone assignment(s) skipped") && HasNote(m, "more than 4 bone weights") && !HasNote(m, "imported at its bind pose"));
}

static bool SamePart(const ImportedPart& a, const ImportedPart& b) {
    return a.jointIdx == b.jointIdx && a.jointWt == b.jointWt && a.bones.size() == b.bones.size();
}

int main() {
    RUN_SUITE("ogre_skinbin");
    const std::string skel = kSkeleton;
    for (int shared = 0; shared < 2; ++shared) {   // assignments on the submesh (0x4100) and on the mesh for shared vertices (0x7000)
        ImportedModel m; std::string err;
        CHECK(ReadOgreBinaryBuffer(BinMesh(shared != 0), "", m, &err, nullptr, &skel));
        CheckSkinned(m);
    }
    {   // the two ways of writing the same mesh give the same skin
        ImportedModel a, b; std::string err;
        CHECK(ReadOgreBinaryBuffer(BinMesh(false), "", a, &err, nullptr, &skel) && ReadOgreBinaryBuffer(BinMesh(true), "", b, &err, nullptr, &skel));
        CHECK(SamePart(a.parts[0], b.parts[0]));
    }
    {   // no skeleton given: rigid, the old note, no bones
        ImportedModel m; std::string err;
        CHECK(ReadOgreBinaryBuffer(BinMesh(false), "", m, &err));
        CHECK(m.parts.size() == 1 && m.parts[0].bones.empty() && m.parts[0].jointIdx.empty() && HasNote(m, "skeleton 'x.skeleton'") && HasNote(m, "not read"));
    }
    {   // a skeleton that cannot be read: rigid, with the reason
        ImportedModel m; std::string err;
        const std::string bad = "<skeleton><bones><bone id=\"0\" name=\"A\"/><bone id=\"0\" name=\"B\"/></bones></skeleton>";
        CHECK(ReadOgreBinaryBuffer(BinMesh(false), "", m, &err, nullptr, &bad));
        CHECK(m.parts.size() == 1 && m.parts[0].bones.empty() && HasNote(m, "skeleton could not be read"));
    }
    {   // a mesh with no link but a given skeleton is still skinned; and every truncation reads or is refused cleanly
        ImportedModel m; std::string err;
        CHECK(ReadOgreBinaryBuffer(BinMesh(false, false), "", m, &err, nullptr, &skel) && m.parts[0].bones.size() == 6);
        const std::string good = BinMesh(false);
        for (std::size_t n = 0; n < good.size(); ++n) {
            ImportedModel t; std::string e;
            const bool ok = ReadOgreBinaryBuffer(good.substr(0, n), "", t, &e, nullptr, &skel);
            CHECK(ok || (!e.empty() && t.parts.empty()));
            if (ok) CHECK(t.parts.size() == 1 && t.parts[0].jointIdx.size() == t.parts[0].mesh.vertices.size());
        }
    }
    {   // the file reader loads <link name>.xml next to the mesh
#ifdef _WIN32
        _mkdir("skinbin_tmp");
#else
        mkdir("skinbin_tmp", 0777);
#endif
        { std::ofstream f("skinbin_tmp/w.mesh", std::ios::binary); const std::string d = BinMesh(false); f.write(d.data(), (std::streamsize)d.size()); }
        { std::ofstream f("skinbin_tmp/x.skeleton.xml", std::ios::binary); f << kSkeleton; }
        ImportedModel m; std::string err;
        CHECK(ReadOgreBinaryFile("skinbin_tmp/w.mesh", m, &err));
        CheckSkinned(m);
        std::remove("skinbin_tmp/x.skeleton.xml");
        ImportedModel r;
        CHECK(ReadOgreBinaryFile("skinbin_tmp/w.mesh", r, &err) && r.parts[0].bones.empty() && HasNote(r, "skeleton 'x.skeleton'"));   // no skeleton file: rigid
        std::remove("skinbin_tmp/w.mesh");
    }
    TEST_MAIN_RESULT();
}
