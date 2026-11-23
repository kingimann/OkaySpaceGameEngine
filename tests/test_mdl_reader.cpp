// Our own MDL reader (engine/src/Render/MdlReader.cpp): the Quake 1 model and the Half-Life 1 studio model. SYNTHETIC files built in
// memory (tests/e4_fixtures.hpp), checked against numbers worked out by hand. No real .mdl file is in this repository: the reader
// is unverified against real files. Builds on its own with -DOKAY_STANDALONE_TEST (reader source only).
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "e4_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include <cmath>
#include <string>

using namespace okay;

static bool HasNote(const ImportedModel& m, const char* what) { for (const std::string& n : m.notes) if (n.find(what) != std::string::npos) return true; return false; }
static bool Near(float a, float b, float e = 1e-5f) { return std::fabs(a - b) <= e; }
static bool Read(const std::string& t, ImportedModel& m, std::string& err) { return ReadMdlBuffer(t, "", m, &err); }
static int FindVertex(const Mesh& me, float x, float y, float z, float u = -1, float v = -1) {
    for (std::size_t i = 0; i < me.vertices.size(); ++i)
        if (Near(me.vertices[i].x, x) && Near(me.vertices[i].y, y) && Near(me.vertices[i].z, z) && (u < 0 || (Near(me.uvs[i].x, u) && Near(me.uvs[i].y, v)))) return (int)i;
    return -1;
}
static void Put32(std::string& s, std::size_t o, std::int32_t v) { for (int i = 0; i < 4; ++i) s[o + (std::size_t)i] = (char)(((std::uint32_t)v >> (8 * i)) & 255); }

int main() {
    RUN_SUITE("mdl_reader");

    // ---- Quake 1 ----------------------------------------------------------------------------------------------------------------
    {
        ImportedModel m; std::string err;
        const bool ok = Read(e4::QuakeMdl(), m, err);
        if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
        CHECK(ok && m.backend == "own" && fx::Sane(m));
        CHECK(m.nodes.size() == 1 && m.parts.size() == 1 && m.materials.size() == 1 && m.clips.empty() && m.parts[0].bones.empty());
        // Z-up becomes Y-up on the root
        CHECK(Near(m.nodes[0].rotation.x, -0.70710678f) && Near(m.nodes[0].rotation.w, 0.70710678f));
        const Mesh& me = m.parts[0].mesh;
        // 6 corners: vertex 1 is shared by both triangles (same texture coordinate), vertex 2 is on the seam and differs per triangle
        CHECK(me.vertices.size() == 5 && me.triangles.size() == 6 && me.uvs.size() == 5 && me.normals.size() == 5);
        // v0 (10,20,30) uv (0.125, 0.25); v1 (11,20,30) uv (0.875, 0.25); v2 front: (10,22,30) uv (0.125, 0.75); v2 back: uv (0.625, 0.75); v3 (11,22,33) uv (0.875, 0.75)
        CHECK(FindVertex(me, 10, 20, 30, 0.125f, 0.25f) >= 0 && FindVertex(me, 11, 20, 30, 0.875f, 0.25f) >= 0 && FindVertex(me, 11, 22, 33, 0.875f, 0.75f) >= 0);
        CHECK(FindVertex(me, 10, 22, 30, 0.125f, 0.75f) >= 0 && FindVertex(me, 10, 22, 30, 0.625f, 0.75f) >= 0);
        // winding reversed: the first triangle is v2, v1, v0
        CHECK(Near(me.vertices[(std::size_t)me.triangles[0]].y, 22) && Near(me.vertices[(std::size_t)me.triangles[1]].x, 11) && Near(me.vertices[(std::size_t)me.triangles[2]].x, 10) && Near(me.vertices[(std::size_t)me.triangles[2]].y, 20));
        // the seam vertex takes its texture coordinate from the triangle's own facing: the first triangle faces front (u 0.125), the second back (u 0.625)
        CHECK(Near(me.uvs[(std::size_t)me.triangles[0]].x, 0.125f) && Near(me.uvs[(std::size_t)me.triangles[0]].y, 0.75f));
        CHECK(Near(me.uvs[(std::size_t)me.triangles[3]].x, 0.625f) && Near(me.uvs[(std::size_t)me.triangles[3]].y, 0.75f));
        CHECK(me.name == "stand1");
        CHECK(m.materials[0].texture.empty() && HasNote(m, "embedded skin") && HasNote(m, "2 frames"));
    }
    {   // a group skin, a group first frame
        e4::QuakeOffsets o;
        std::string f = e4::QuakeMdl(&o);
        ImportedModel base; std::string err;
        CHECK(Read(f, base, err));
        // turn the single skin into a group of two skin pictures: insert 4 (nb) + 8 (times) + 8 more picture bytes
        std::string g = f.substr(0, o.skins);
        auto p32 = [&](std::int32_t v) { for (int i = 0; i < 4; ++i) g.push_back((char)(((std::uint32_t)v >> (8 * i)) & 255)); };
        p32(1); p32(2); p32(0); p32(0); g.append(16, 'x');
        g += f.substr(o.skins + 4 + 8);
        ImportedModel m;
        CHECK(Read(g, m, err) && m.parts[0].mesh.vertices.size() == 5 && HasNote(m, "animated skin group"));
        // a frame group first: type 1, count 2, bounds, two times, then the subframes (each with bounds, name, vertices): the first subframe is read
        ImportedModel fm;
        CHECK(Read(e4::QuakeMdl(nullptr, true), fm, err) && fm.parts[0].mesh.vertices.size() == 5 && FindVertex(fm.parts[0].mesh, 11, 22, 33, 0.875f, 0.75f) >= 0 && fm.parts[0].mesh.name == "run");
    }
    {   // numbers: scale and origin applied, a vertex byte of 255
        std::string f = e4::QuakeMdl();
        ImportedModel m; std::string err;
        e4::QuakeOffsets o; e4::QuakeMdl(&o);
        f[o.frames + 4 + 8 + 16 + 4 * 3] = (char)255;   // vertex 3, x
        CHECK(Read(f, m, err) && FindVertex(m.parts[0].mesh, 265, 22, 33) >= 0);   // the byte 255 times scale 1 plus origin 10
    }
    {   // refusals
        std::string f = e4::QuakeMdl();
        e4::QuakeOffsets o; e4::QuakeMdl(&o);
        ImportedModel m; std::string err;
        std::string t = f; Put32(t, 4, 5);       CHECK(!Read(t, m, err) && err.find("version 5") != std::string::npos);
        t = f; Put32(t, 60, 0);                  CHECK(!Read(t, m, err));
        t = f; Put32(t, 60, 0x7FFFFFFF);         CHECK(!Read(t, m, err) && err.find("vertex count") != std::string::npos);
        t = f; Put32(t, 60, (std::int32_t)0xFFFFFFFFu); CHECK(!Read(t, m, err));
        t = f; Put32(t, 64, (std::int32_t)0xFFFFFFFFu); CHECK(!Read(t, m, err));
        t = f; Put32(t, 68, (std::int32_t)0xFFFFFFFFu); CHECK(!Read(t, m, err));
        t = f; Put32(t, 48, 1000000);            CHECK(!Read(t, m, err));
        t = f; Put32(t, 52, 0);                  CHECK(!Read(t, m, err));
        t = f; Put32(t, 52, 16385);              CHECK(!Read(t, m, err));
        t = f; Put32(t, 60, 100000);             CHECK(!Read(t, m, err) && err.find("ends inside") != std::string::npos);   // the file is far too short for 100,000 vertices
        t = f; Put32(t, 64, 400000);             CHECK(!Read(t, m, err));
        t = f; Put32(t, o.skins, 7);             CHECK(!Read(t, m, err));   // a skin group of a bad size
        t = f; Put32(t, o.tris + 4, 99);         CHECK(Read(t, m, err) && HasNote(m, "out-of-range vertex index") && m.parts[0].mesh.triangles.size() == 3);   // one triangle skipped
        t = f; Put32(t, o.tris + 4, 99); Put32(t, o.tris + 20, 99); CHECK(!Read(t, m, err) && err.find("no usable triangles") != std::string::npos);
        t = f; Put32(t, 8, 0x7F800000);          CHECK(!Read(t, m, err) && err.find("not finite") != std::string::npos);   // an infinite scale
        for (std::size_t cut = 0; cut < f.size(); ++cut) {   // every truncation: refused (the frames after the first are not read, so the tail may be missing)
            ImportedModel c; std::string e;
            const bool ok = Read(f.substr(0, cut), c, e);
            CHECK(ok ? fx::Sane(c) : (c.parts.empty() && c.nodes.empty() && !e.empty()));
            if (cut < o.frames + 4 + 8 + 16 + 16) CHECK(!ok);
        }
    }

    // ---- 3D GameStudio and other files ----------------------------------------------------------------------------------------------
    {
        ImportedModel m; std::string err;
        for (const char* magic : {"MDL3", "MDL4", "MDL5", "MDL7"}) {
            CHECK(!Read(std::string(magic) + std::string(200, '\0'), m, err) && err.find("3D GameStudio") != std::string::npos && err.find(magic) != std::string::npos && err.find("not supported") != std::string::npos);
        }
        CHECK(!Read("IDSQ" + std::string(200, '\0'), m, err) && err.find("sequence group") != std::string::npos);
        CHECK(!Read("IDP2" + std::string(200, '\0'), m, err) && err.find("not a supported MDL") != std::string::npos);
        CHECK(!Read("", m, err) && !Read("IDPO", m, err));
    }

    // ---- Half-Life 1 ----------------------------------------------------------------------------------------------------------------
    {
        e4::HlOffsets o;
        ImportedModel m; std::string err;
        const bool ok = Read(e4::HalfLifeMdl(1.0f, &o), m, err);
        if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
        CHECK(ok && m.backend == "own" && fx::Sane(m));
        // nodes: the root, then the bones parent first
        CHECK(m.nodes.size() == 3 && m.nodes[1].name == "root" && m.nodes[2].name == "child" && m.nodes[1].parent == 0 && m.nodes[2].parent == 1);
        CHECK(fx::NearXyz(m.nodes[1].position, 1, 2, 3, 1e-5f));
        CHECK(Near(m.nodes[2].position.y, 1) && Near(m.nodes[2].rotation.z, 0.70710678f, 1e-5f) && Near(m.nodes[2].rotation.w, 0.70710678f, 1e-5f));
        CHECK(Near(m.nodes[0].rotation.x, -0.70710678f));   // Z-up
        CHECK(m.materials.size() == 1 && m.materials[0].name == "tex.bmp" && m.materials[0].texture.empty() && HasNote(m, "embedded 8-bit palette"));
        CHECK(m.parts.size() == 1 && m.parts[0].material == 0 && m.parts[0].node == 0 && m.parts[0].bones.size() == 2);
        const ImportedPart& p = m.parts[0];
        const Mesh& me = p.mesh;
        CHECK(me.vertices.size() == 3 && me.triangles.size() == 3 && me.uvs.size() == 3 && me.normals.size() == 3);
        // bind pose positions: v0 (1,0,0) in the child bone = turn 90 degrees about Z -> (0,1,0), + (0,1,0), + root (1,2,3) = (1,4,3);
        // v1 (0,1,0) -> (-1,0,0) + (0,1,0) + (1,2,3) = (0,3,3); v2 (0,0,0) in the root bone = (1,2,3)
        CHECK(Near(me.vertices[0].x, 1) && Near(me.vertices[0].y, 4) && Near(me.vertices[0].z, 3));
        CHECK(Near(me.vertices[1].x, 0) && Near(me.vertices[1].y, 3) && Near(me.vertices[1].z, 3));
        CHECK(Near(me.vertices[2].x, 1) && Near(me.vertices[2].y, 2) && Near(me.vertices[2].z, 3));
        // texture coordinates s / 64, t / 32
        CHECK(Near(me.uvs[0].x, 0) && Near(me.uvs[0].y, 0) && Near(me.uvs[1].x, 1) && Near(me.uvs[1].y, 0) && Near(me.uvs[2].x, 0) && Near(me.uvs[2].y, 1));
        // the stored normal (0,0,1) is in the root bone: unchanged; the strip reversed would face -Z, which disagrees with the normal: flipped back (2,0,1)
        CHECK(Near(me.normals[0].z, 1));
        CHECK(me.triangles[0] == 2 && me.triangles[1] == 0 && me.triangles[2] == 1 && HasNote(m, "winding flipped"));
        // rigid skin: each vertex belongs to its bone
        CHECK(p.jointIdx[0][0] == 1 && p.jointIdx[1][0] == 1 && p.jointIdx[2][0] == 0 && Near(p.jointWt[0][0], 1) && Near(p.jointWt[2][0], 1));
        // inverse bind of the child: the inverse of T(1,2,3) T(0,1,0) Rz(90): applied to the bind position (1,4,3) gives the bone-space (1,0,0)
        {
            const Mat4& ib = p.bones[1].inverseBind;
            const Vec3 r = ib.MultiplyPoint(me.vertices[0]);
            CHECK(Near(r.x, 1, 1e-5f) && Near(r.y, 0, 1e-5f) && Near(r.z, 0, 1e-5f));
            const Vec3 r2 = p.bones[0].inverseBind.MultiplyPoint(me.vertices[2]);
            CHECK(Near(r2.x, 0, 1e-5f) && Near(r2.y, 0, 1e-5f) && Near(r2.z, 0, 1e-5f));
        }
        // the animation: 4 frames at 10 per second on the child's Z rotation; raw 100, 100, 300, 400 at 0.01 each, plus the rest angle pi/2
        CHECK(m.clips.size() == 1 && m.clips[0].name == "walk" && m.clips[0].channels.size() == 1 && m.clips[0].channels[0].node == "child");
        const ImportedChannel& ch = m.clips[0].channels[0];
        CHECK(ch.rotation.size() == 4 && ch.position.size() == 4);
        const double rest = 3.14159265358979323846 / 2, add[4] = {1.0, 1.0, 3.0, 4.0};
        for (int f = 0; f < 4; ++f) {
            CHECK(Near(ch.rotation[(std::size_t)f].time, 0.1f * (float)f, 1e-6f));
            CHECK(Near(ch.rotation[(std::size_t)f].value.z, (float)std::sin((rest + add[f]) / 2), 1e-5f) && Near(ch.rotation[(std::size_t)f].value.w, (float)std::cos((rest + add[f]) / 2), 1e-5f));
            CHECK(Near(ch.position[(std::size_t)f].value.y, 1) && Near(ch.position[(std::size_t)f].value.x, 0));   // not animated: the bone's own position
        }
        CHECK(HasNote(m, "alternative") == false);
    }
    {   // the normal pointing the other way: the strip's winding already agrees, no flip; the triangle stays (2,1,0)
        ImportedModel m; std::string err;
        CHECK(Read(e4::HalfLifeMdl(-1.0f), m, err));
        const Mesh& me = m.parts[0].mesh;
        CHECK(me.triangles[0] == 2 && me.triangles[1] == 1 && me.triangles[2] == 0 && !HasNote(m, "winding flipped") && Near(me.normals[0].z, -1));
    }
    {   // the Euler angles of a bone (rotation about X, then Y, then Z): the node's quaternion turns (1,2,3) like Rz(0.1) Ry(0.2) Rx(0.3) does
        const float euler[3] = {0.3f, 0.2f, 0.1f};
        ImportedModel m; std::string err;
        CHECK(Read(e4::HalfLifeMdl(1.0f, nullptr, true, euler), m, err));
        double x = 1, y = 2, z = 3;
        { const double c = std::cos(0.3), s = std::sin(0.3), ny = y * c - z * s, nz = y * s + z * c; y = ny; z = nz; }   // about X
        { const double c = std::cos(0.2), s = std::sin(0.2), nx = x * c + z * s, nz = -x * s + z * c; x = nx; z = nz; }   // about Y
        { const double c = std::cos(0.1), s = std::sin(0.1), nx = x * c - y * s, ny = x * s + y * c; x = nx; y = ny; }   // about Z
        const Vec3 r = m.nodes[1].rotation * Vec3{1, 2, 3};
        CHECK(Near(r.x, (float)x, 1e-4f) && Near(r.y, (float)y, 1e-4f) && Near(r.z, (float)z, 1e-4f));
    }
    {   // a strip of four vertices: the second triangle of a strip has its first two vertices swapped. Corners 0..3 are at (1,4,3) (0,3,3) (1,2,3) (0,2,3).
        // The reversed triangles are (2,1,0) and (3,1,2); both face against the +Z normal, so with it the whole mesh is flipped to (2,0,1) and (3,2,1).
        ImportedModel m; std::string err;
        CHECK(Read(e4::HalfLifeMdl(-1.0f, nullptr, true, nullptr, true), m, err) && m.parts[0].mesh.vertices.size() == 4 && m.parts[0].mesh.triangles.size() == 6);
        const int keep[6] = {2, 1, 0, 3, 1, 2};
        for (int i = 0; i < 6; ++i) CHECK(m.parts[0].mesh.triangles[(std::size_t)i] == keep[i]);
        CHECK(Near(m.parts[0].mesh.vertices[3].x, 0) && Near(m.parts[0].mesh.vertices[3].y, 2) && Near(m.parts[0].mesh.vertices[3].z, 3));
        ImportedModel f;
        CHECK(Read(e4::HalfLifeMdl(1.0f, nullptr, true, nullptr, true), f, err));
        const int flip[6] = {2, 0, 1, 3, 2, 1};
        for (int i = 0; i < 6; ++i) CHECK(f.parts[0].mesh.triangles[(std::size_t)i] == flip[i]);
        CHECK(f.clips.size() == 1 && f.clips[0].channels[0].rotation.size() == 4);   // the sequence after the longer command list is still found
    }
    {   // node names: bones named a_1, a, a, a must give four different names (a_1, a, a_1_1, a_2), not two a_1
        const std::vector<std::string> names = {"a_1", "a", "a", "a"};
        ImportedModel m; std::string err;
        CHECK(Read(e4::HlStress(4, 1, 1, 3, 0, &names), m, err) && m.nodes.size() == 5);
        CHECK(m.nodes[1].name == "a_1" && m.nodes[2].name == "a" && m.nodes[3].name == "a_2" && m.nodes[4].name == "a_3");
        std::string seen;
        for (std::size_t i = 1; i < m.nodes.size(); ++i) { CHECK(seen.find("|" + m.nodes[i].name + "|") == std::string::npos); seen += "|" + m.nodes[i].name + "|"; }
        // two body parts naming the same model: read once, with a note
        ImportedModel s;
        CHECK(Read(e4::HlStress(1, 2, 1, 3, 0), s, err) && s.parts.size() == 1 && HasNote(s, "read once"));
    }
    {   // no sequences: no clips
        ImportedModel m; std::string err;
        CHECK(Read(e4::HalfLifeMdl(1.0f, nullptr, false), m, err) && m.clips.empty());
    }
    {   // a fan: 4 vertices (the strip fixture's three plus one), commands rewritten as a negative count
        e4::HlOffsets o;
        std::string f = e4::HalfLifeMdl(-1.0f, &o);
        // grow the command list: count -4, vertices 0 1 2 then 0 again; the list is followed by the sequence table, so the commands are
        // rewritten in place: the count is 3 -> -3 (a fan of three is one triangle (v0, v1, v2) reversed to (v2, v1, v0))
        f[o.cmds] = (char)0xFD; f[o.cmds + 1] = (char)0xFF;
        ImportedModel m; std::string err;
        CHECK(Read(f, m, err) && m.parts[0].mesh.triangles.size() == 3 && m.parts[0].mesh.triangles[0] == 2 && m.parts[0].mesh.triangles[1] == 1 && m.parts[0].mesh.triangles[2] == 0);
    }
    {   // cyclic bone parents: bone 0 names bone 1 and bone 1 names bone 0; the loop is cut and the nodes come out parent first
        e4::HlOffsets o;
        std::string f = e4::HalfLifeMdl(1.0f, &o);
        Put32(f, o.bones + 32, 1);
        ImportedModel m; std::string err;
        CHECK(Read(f, m, err) && fx::Sane(m) && m.nodes.size() == 3 && HasNote(m, "parent loop"));
        Put32(f, o.bones + 32, 0);   // a bone that is its own parent
        ImportedModel m2;
        CHECK(Read(f, m2, err) && fx::Sane(m2) && HasNote(m2, "parent loop"));
        // a parent index outside the table
        f = e4::HalfLifeMdl(1.0f, &o); Put32(f, o.bones + 32, 77);
        CHECK(!Read(f, m, err) && err.find("parent outside") != std::string::npos);
        f = e4::HalfLifeMdl(1.0f, &o); Put32(f, o.bones + 32, -2);
        CHECK(!Read(f, m, err));
    }
    {   // hostile counts and offsets: every one a refusal
        e4::HlOffsets o;
        const std::string f = e4::HalfLifeMdl(1.0f, &o);
        const std::size_t fields[] = {140, 144, 164, 168, 180, 184, 192, 196, 200, 204, 208};   // numbones .. bodypartindex
        for (std::size_t at : fields) {
            for (std::int32_t v : {(std::int32_t)0xFFFFFFFFu, 0x7FFFFFFF, (std::int32_t)0x80000000u, 1000000}) {
                std::string t = f; Put32(t, at, v);
                ImportedModel m; std::string err;
                const bool ok = Read(t, m, err);
                CHECK(ok ? fx::Sane(m) : (m.parts.empty() && m.nodes.empty() && !err.empty()));
                if (at == 140 || at == 204) CHECK(!ok);   // counts that the model cannot have
            }
        }
        const std::size_t inner[] = {o.body + 64, o.body + 72, o.model + 72, o.model + 76, o.model + 80, o.model + 84, o.model + 88, o.model + 92, o.model + 96, o.model + 100, o.mesh + 4, o.mesh + 8,
                                     o.seq + 56, o.seq + 120, o.seq + 124, o.seq + 156, o.tex + 68, o.tex + 72};
        for (std::size_t at : inner) {
            for (std::int32_t v : {(std::int32_t)0xFFFFFFFFu, 0x7FFFFFFF, (std::int32_t)0x80000000u, 1000000, 0, 5000}) {
                std::string t = f; Put32(t, at, v);
                ImportedModel m; std::string err;
                const bool ok = Read(t, m, err);
                CHECK(ok ? fx::Sane(m) : (m.parts.empty() && m.nodes.empty() && !err.empty()));
            }
        }
        // a Half-Life 2 / Source model
        std::string t = f; Put32(t, 4, 44);
        ImportedModel m; std::string err;
        CHECK(!Read(t, m, err) && err.find("Half-Life 2") != std::string::npos);
        // a triangle command list that runs off the file
        t = f; t[o.cmds] = (char)0xFF; t[o.cmds + 1] = (char)0x7F;
        CHECK(!Read(t, m, err) && err.find("triangle command") != std::string::npos);
        // vertex indices past the table: the triangle is skipped, then the file has no triangles
        t = f; t[o.cmds + 2] = (char)200;
        CHECK(!Read(t, m, err) || m.parts[0].mesh.triangles.empty() == false);
        // a sequence in an external group is skipped with a note; bad animation data (an offset past the file) skips the sequence
        t = f; Put32(t, o.seq + 156, 2);
        CHECK(Read(t, m, err) && m.clips.empty() && HasNote(m, "external sequence group"));
        t = f; t[o.anim + 12 + 10] = (char)0xFF; t[o.anim + 12 + 11] = (char)0xFF;
        ImportedModel m3;
        CHECK(Read(t, m3, err) && m3.clips.empty() && HasNote(m3, "bad or truncated animation"));
        // an animation run with total 0 (would never end): skipped
        t = f; t[o.rle + 1] = 0;
        ImportedModel m4;
        CHECK(Read(t, m4, err) && m4.clips.empty());
        // an enormous frame count: refused by the key budget before any decoding
        t = f; Put32(t, o.seq + 56, 65536);
        ImportedModel m5;
        CHECK(Read(t, m5, err));   // 65536 frames x 2 bones is under the budget: the runs end early, the sequence is dropped as truncated data
        CHECK(m5.clips.empty() || m5.clips[0].channels[0].rotation.size() == 65536);
        for (std::size_t cut = 0; cut < f.size(); cut += 3) {   // every third truncation
            ImportedModel c; std::string e;
            const bool ok = Read(f.substr(0, cut), c, e);
            CHECK(ok ? fx::Sane(c) : (c.parts.empty() && c.nodes.empty() && !e.empty()));
            if (cut < o.cmds) CHECK(!ok);
        }
    }
    TEST_MAIN_RESULT();
}
