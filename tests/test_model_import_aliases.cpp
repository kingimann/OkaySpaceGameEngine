// The last extensions Assimp's importers claim that no reader of ours took (docs/status/own_code_roadmap.md, "What still needs
// Assimp", table 4): each now goes to one of our readers, under the switch of the format it belongs to.
//   .ask       ASE text (the ASE reader, OKAY_OWN_ASE)          .scn  trueSpace scene, Caligari text or binary (COB reader, OKAY_OWN_COB)
//   .prj       3D Studio project, main chunk 0xC23D (3DS reader, OKAY_OWN_3DS)
//   .x3db      X3D (the X3D reader, OKAY_OWN_X3D; the XML encoding only: a binary-encoded file is refused with the reason)
//   .vta       Valve vertex animation (the SMD reader, OKAY_OWN_SMD; the first frame is the base mesh, later frames are noted)
//   .md5camera Doom 3 camera path (our new MD5 camera reader, under OKAY_OWN_MD5)
//   .vrm       VRM avatar: a binary glTF 2.0 file, read by our glTF reader like .glb
// Fixtures are built in memory (nothing binary is committed). Each file goes through ImportModelData (or ImportModel for .vrm) and
// must come back from our reader, equal to what the format's own Read*File gives directly.
#include "test_framework.hpp"
#include "e3_fixtures.hpp"
#include "own_seam_checks.hpp"
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Render/X3dReader.hpp"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

static bool HasNote(const ImportedModel& m, const char* part) { for (const std::string& n : m.notes) if (n.find(part) != std::string::npos) return true; return false; }

static std::string Chunk3ds(unsigned id, const std::string& body) { std::string s; fx::P16(s, id); fx::P32(s, (std::uint32_t)(6 + body.size())); return s + body; }

// a 3D Studio project file: the 0xC23D main chunk around the same editor chunk (0x3D3D) a .3ds holds; one triangle
static std::string Prj() {
    std::string pts; fx::P16(pts, 3); for (float f : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f}) fx::PutF(pts, f);
    std::string faces; fx::P16(faces, 1); fx::P16(faces, 0); fx::P16(faces, 1); fx::P16(faces, 2); fx::P16(faces, 7);
    const std::string obj = Chunk3ds(0x4000, std::string("T") + '\0' + Chunk3ds(0x4100, Chunk3ds(0x4110, pts) + Chunk3ds(0x4120, faces)));
    return Chunk3ds(0xC23D, Chunk3ds(0x3D3D, obj));
}

static const char* kAse =
    "*3DSMAX_ASCIIEXPORT 200\n*GEOMOBJECT {\n*NODE_NAME \"T\"\n*MESH {\n*MESH_NUMVERTEX 3\n*MESH_NUMFACES 1\n*MESH_VERTEX_LIST {\n"
    "*MESH_VERTEX 0 0 0 0\n*MESH_VERTEX 1 1 0 0\n*MESH_VERTEX 2 0 1 0\n}\n*MESH_FACE_LIST {\n*MESH_FACE 0: A: 0 B: 1 C: 2 AB: 1 BC: 1 CA: 1\n}\n}\n}\n";

static const char* kX3d =
    "<?xml version=\"1.0\"?>\n<X3D version=\"3.3\"><Scene><Shape><IndexedFaceSet coordIndex=\"0 1 2 -1\">"
    "<Coordinate point=\"0 0 0 1 0 0 0 1 0\"/></IndexedFaceSet></Shape></Scene></X3D>\n";

// a VTA: one bone, two frames of the skeleton, and vertex animation frame 0 holding the base mesh (6 corners = 2 triangles, in
// corner order) and frame 1 moving one vertex (a flex shape)
static const char* kVta =
    "version 1\nnodes\n0 \"root\" -1\nend\nskeleton\ntime 0\n0 0 0 0 0 0 0\ntime 1\n0 0 0 0 0 0 0\nend\nvertexanimation\ntime 0\n"
    "0 0 0 0 0 0 1\n1 1 0 0 0 0 1\n2 0 1 0 0 0 1\n3 1 0 0 0 0 1\n4 1 1 0 0 0 1\n5 0 1 0 0 0 1\ntime 1\n4 1 1 0.5 0 0 1\nend\n";

// a Doom 3 camera path: 4 frames at 24 per second, one cut at frame 2
static const char* kCam =
    "MD5Version 10\ncommandline \"fixture\"\n\nnumFrames 4\nframeRate 24\nnumCuts 1\n\ncuts {\n\t2\n}\n\ncamera {\n"
    "\t( 0 0 0 ) ( 0 0 0 ) 90\n\t( 1 0 0 ) ( 0 0 0 ) 90\n\t( 2 0 0 ) ( 0 0 0.7071 ) 60\n\t( 3 0 0 ) ( 0 0 0.7071 ) 60\n}\n";

// a VRM file: GLB (glTF 2.0 binary) with one triangle and the VRM extension named
static std::string Vrm() {
    std::string json =
        "{\"asset\":{\"version\":\"2.0\"},\"extensionsUsed\":[\"VRM\"],\"extensions\":{\"VRM\":{}},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
        "\"nodes\":[{\"mesh\":0}],\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[1,1,0]},"
        "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6}],"
        "\"buffers\":[{\"byteLength\":44}]}";
    while (json.size() % 4) json += ' ';
    std::string bin;
    for (float f : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f}) fx::PutF(bin, f);
    fx::P16(bin, 0); fx::P16(bin, 1); fx::P16(bin, 2); fx::P16(bin, 0);
    std::string s = "glTF";
    fx::P32(s, 2); fx::P32(s, (std::uint32_t)(12 + 8 + json.size() + 8 + bin.size()));
    fx::P32(s, (std::uint32_t)json.size()); fx::P32(s, 0x4E4F534Au); s += json;
    fx::P32(s, (std::uint32_t)bin.size()); fx::P32(s, 0x004E4942u); s += bin;
    return s;
}

using Direct = bool (*)(const std::string&, ImportedModel&, std::string*);
static bool DAse(const std::string& p, ImportedModel& m, std::string* e) { return ReadAseFile(p, m, e); }
static bool DCob(const std::string& p, ImportedModel& m, std::string* e) { return ReadCobFile(p, m, e); }
static bool D3ds(const std::string& p, ImportedModel& m, std::string* e) { return Read3dsFile(p, m, e); }
static bool DX3d(const std::string& p, ImportedModel& m, std::string* e) { return ReadX3dFile(p, m, e); }
static bool DSmd(const std::string& p, ImportedModel& m, std::string* e) { return ReadSmdFile(p, m, e); }
static bool DCam(const std::string& p, ImportedModel& m, std::string* e) { return ReadMd5CameraFile(p, m, e); }

struct Case { const char* file; const char* ext; const char* env; std::string bytes; Direct direct; std::size_t tris; };

int main() {
    RUN_SUITE("model_import_aliases");
    const fs::path dir = fs::temp_directory_path() / "okay_model_import_aliases";
    std::error_code ec; fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const Case cases[] = {
        {"a.ask", ".ask", "OKAY_OWN_ASE", kAse, DAse, 1},
        {"a.scn", ".scn", "OKAY_OWN_COB", fx::CobAscii(), DCob, 3},
        {"b.scn", ".scn", "OKAY_OWN_COB", fx::CobBinary(), DCob, 3},
        {"a.prj", ".prj", "OKAY_OWN_3DS", Prj(), D3ds, 1},
        {"a.x3db", ".x3db", "OKAY_OWN_X3D", kX3d, DX3d, 1},
        {"a.vta", ".vta", "OKAY_OWN_SMD", kVta, DSmd, 2},
        {"a.md5camera", ".md5camera", "OKAY_OWN_MD5", kCam, DCam, 0},
    };
    for (const Case& c : cases) {
        std::fprintf(stderr, "  case %s\n", c.file);
        const std::string path = (dir / c.file).string();
        seam::Write(path, c.bytes);
        CHECK(HasOwnReader(c.ext));
        ImportedModel a, b; std::string err;
        CHECK(c.direct(path, b, &err) && b.backend == "own");
        if (!err.empty()) std::fprintf(stderr, "    direct: %s\n", err.c_str());
        SetImportBackend(c.ext, ImportBackend::Own);
        CHECK(EffectiveImportBackend(c.ext) == ImportBackend::Own);
        err.clear();
        const bool ok = ImportModelData(path, a, ImportBackend::Default, false, &err);
        if (!ok) std::fprintf(stderr, "    seam: %s\n", err.c_str());
        CHECK(ok && a.backend == "own" && fx::Sane(a) && fx::TriCount(a) == c.tris);
        CHECK(a.parts.size() == b.parts.size() && a.nodes.size() == b.nodes.size() && a.clips.size() == b.clips.size() && fx::TriCount(a) == fx::TriCount(b));
        SetImportBackend(c.ext, ImportBackend::Default);
        // the format's own switch covers the new extension: off, our reader is not asked
        seam::SetEnv(c.env, "0");
        CHECK(EffectiveImportBackend(c.ext) != ImportBackend::Own);
        seam::UnsetEnv(c.env);
#if defined(OKAY_EXPECT_OWN_READERS)
        // nothing set: our reader is the default, and the extension is offered
        CHECK(EffectiveImportBackend(c.ext) == ImportBackend::Own);
        CHECK(seam::Listed(c.ext));
        { ImportedModel d; CHECK(ImportModelData(path, d, ImportBackend::Default, false, &err) && d.backend == "own"); }
#endif
    }
    {   // the VTA: frame 0 is the base mesh on the root bone, the flex frame after it is noted, not read
        ImportedModel m; std::string err;
        CHECK(ReadSmdFile((dir / "a.vta").string(), m, &err) && m.parts.size() == 1 && m.parts[0].mesh.vertices.size() == 4);
        CHECK(HasNote(m, "vertex animation frame"));
        // an SMD with triangles keeps skipping its vertexanimation section, as before
        const std::string smd = std::string("version 1\nnodes\n0 \"root\" -1\nend\nskeleton\ntime 0\n0 0 0 0 0 0 0\nend\ntriangles\nm.png\n") +
                                "0 0 0 0 0 0 1 0 0\n0 1 0 0 0 0 1 1 0\n0 0 1 0 0 0 1 0 1\nend\nvertexanimation\ntime 0\n0 9 9 9 0 0 1\nend\n";
        seam::Write(dir / "c.smd", smd);
        ImportedModel s;
        CHECK(ReadSmdFile((dir / "c.smd").string(), s, &err) && fx::TriCount(s) == 1 && s.parts[0].mesh.vertices[0].x == 0.0f && HasNote(s, "vertexanimation section"));
        // VTA frame 0 corners that do not make whole triangles: refused with the reason when there is no skeleton either
        seam::Write(dir / "bad.vta", "version 1\nvertexanimation\ntime 0\n0 0 0 0 0 0 1\n1 1 0 0 0 0 1\nend\n");
        ImportedModel z;
        CHECK(!ReadSmdFile((dir / "bad.vta").string(), z, &err) && !err.empty());
    }
    {   // the camera: a root (Z up to Y up) and one camera node; one clip, a key per frame plus a held key before the cut
        ImportedModel m; std::string err;
        CHECK(ReadMd5CameraFile((dir / "a.md5camera").string(), m, &err));
        CHECK(m.nodes.size() == 2 && m.nodes[1].name == "camera" && m.nodes[1].parent == 0 && m.parts.empty());
        CHECK(m.clips.size() == 1 && m.clips[0].channels.size() == 1 && m.clips[0].channels[0].node == "camera");
        if (m.clips.size() == 1 && m.clips[0].channels.size() == 1) {
            const ImportedChannel& ch = m.clips[0].channels[0];
            CHECK(ch.position.size() == 5 && ch.rotation.size() == 5);
            if (ch.position.size() == 5) {
                CHECK(std::fabs(ch.position.back().time - 3.0f / 24.0f) < 1e-6f && ch.position.back().value.x == 3.0f);
                CHECK(ch.position[2].value.x == 1.0f && ch.position[2].time < 2.0f / 24.0f && ch.position[2].time > 1.0f / 24.0f);   // held up to the cut
                CHECK(ch.position[3].value.x == 2.0f && std::fabs(ch.position[3].time - 2.0f / 24.0f) < 1e-6f);
                for (std::size_t k = 1; k < ch.position.size(); ++k) CHECK(ch.position[k].time > ch.position[k - 1].time);
            }
        }
        CHECK(HasNote(m, "field of view"));
        // hostile: wrong version, frames missing, a cut out of range, a bad number
        const char* bad[] = {"", "MD5Version 11\n", "MD5Version 10\nnumFrames 3\nframeRate 24\nnumCuts 0\ncamera {\n( 0 0 0 ) ( 0 0 0 ) 90\n}\n",
                             "MD5Version 10\nnumFrames 1\nframeRate 24\nnumCuts 1\ncuts {\n7\n}\ncamera {\n( 0 0 0 ) ( 0 0 0 ) 90\n}\n",
                             "MD5Version 10\nnumFrames 1\nframeRate 24\nnumCuts 0\ncamera {\n( 0 x 0 ) ( 0 0 0 ) 90\n}\n",
                             "MD5Version 10\nnumFrames 2000000000\nframeRate 24\nnumCuts 0\ncamera {\n}\n",
                             "MD5Version 10\nnumFrames 1\nframeRate 0\nnumCuts 0\ncamera {\n( 0 0 0 ) ( 0 0 0 ) 90\n}\n"};
        for (const char* t : bad) {
            ImportedModel h; std::string e;
            CHECK(!ReadMd5CameraBuffer(t, h, &e) && !e.empty() && h.nodes.empty());
        }
        // every prefix of the good file is refused or read, never more than the whole file gives
        const std::string good = kCam;
        for (std::size_t n = 0; n < good.size(); ++n) { ImportedModel h; std::string e; if (ReadMd5CameraBuffer(good.substr(0, n), h, &e)) CHECK(h.clips.size() <= 1); }
    }
    {   // a .prj whose main chunk is neither 0x4D4D nor 0xC23D is still refused
        std::string p = Prj(); p[0] = 0x11; p[1] = 0x22;
        seam::Write(dir / "bad.prj", p);
        ImportedModel m; std::string err;
        CHECK(!Read3dsFile((dir / "bad.prj").string(), m, &err) && err.find("main chunk") != std::string::npos);
    }
    {   // a binary-encoded X3D (Fast Infoset) is refused with the reason, not misread
        seam::Write(dir / "fi.x3db", std::string("\xE0\x00\x00\x01\x00\x38\x00\x00", 8) + "binary");
        ImportedModel m; std::string err;
        CHECK(!ReadX3dFile((dir / "fi.x3db").string(), m, &err) && !err.empty());
    }
    {   // .vrm: our glTF reader, as one mesh and through the scene path's extension list
        const std::string path = (dir / "a.vrm").string();
        seam::Write(path, Vrm());
        bool ok = false;
        Mesh m = ImportModel(path, &ok);
        CHECK(ok && m.TriangleCount() == 1);
        CHECK((" " + ImportableExtensions() + " ").find(" .vrm ") != std::string::npos);
    }
    fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
