// Our own glTF 1.0 reader (engine/src/Render/Gltf1Reader.cpp): files built in memory (tests/gltf1_test_build.hpp) with
// every value worked out by hand from the glTF 1.0 specification, the binary form, external files and their folder rule,
// refusals, and the import seam (off by default; OKAY_OWN_GLTF1 and SetImportBackend(".gltf1") switch it on; glTF 2.0 files
// keep going to the 2.0 reader). The real files are checked by test_model_corpus (reader name gltf1).
#include "test_framework.hpp"
#include "gltf1_test_build.hpp"
#include <Okay.hpp>
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;
using namespace gltf1build;
namespace fs = std::filesystem;

static void TestSetEnv(const char* n, const char* v) {
#ifdef _WIN32
    _putenv_s(n, v);
#else
    setenv(n, v, 1);
#endif
}
static void TestUnsetEnv(const char* n) {
#ifdef _WIN32
    _putenv_s(n, "");
#else
    unsetenv(n);
#endif
}
static bool Near(float a, float b, float e = 1e-5f) { return std::fabs(a - b) <= e; }
static bool NearV(const Vec3& a, const Vec3& b, float e = 1e-5f) { return Near(a.x, b.x, e) && Near(a.y, b.y, e) && Near(a.z, b.z, e); }
static bool HasNote(const ImportedModel& m, const char* part) {
    for (const std::string& n : m.notes) if (n.find(part) != std::string::npos) return true;
    return false;
}
static void WriteFile(const fs::path& p, const std::string& bytes) { std::ofstream o(p, std::ios::binary); o.write(bytes.data(), (std::streamsize)bytes.size()); }

// The standard quad scene, checked value by value.
static void CheckQuad(const ImportedModel& m) {
    CHECK(m.backend == "own");
    CHECK(m.nodes.size() == 3 && m.parts.size() == 1 && m.materials.size() == 1);
    if (m.nodes.size() != 3 || m.parts.size() != 1 || m.materials.size() != 1) return;
    CHECK(m.nodes[0].name == "glTF" && m.nodes[0].parent == -1);
    CHECK(m.nodes[1].name == "Root" && m.nodes[1].parent == 0 && NearV(m.nodes[1].position, Vec3{1, 2, 3}) && NearV(m.nodes[1].scale, Vec3{1, 1, 1}));
    // child: matrix columns (2 0 0 0) (0 2 0 0) (0 0 2 0) (0 0 5 1): scale 2, translation (0, 0, 5), no rotation
    CHECK(m.nodes[2].name == "child" && m.nodes[2].parent == 1 && NearV(m.nodes[2].position, Vec3{0, 0, 5}) && NearV(m.nodes[2].scale, Vec3{2, 2, 2}));
    CHECK(Near(std::fabs(m.nodes[2].rotation.w), 1.0f));
    const ImportedPart& p = m.parts[0];
    CHECK(p.node == 2 && p.material == 0);
    const Mesh& me = p.mesh;
    CHECK(me.vertices.size() == 4 && me.normals.size() == 4 && me.uvs.size() == 4 && me.triangles.size() == 6);
    if (me.vertices.size() == 4 && me.uvs.size() == 4 && me.normals.size() == 4) {
        CHECK(NearV(me.vertices[0], Vec3{0, 0, 0}) && NearV(me.vertices[1], Vec3{1, 0, 0}) && NearV(me.vertices[2], Vec3{1, 1, 0}) && NearV(me.vertices[3], Vec3{0, 1, 0}));
        for (const Vec3& n : me.normals) CHECK(NearV(n, Vec3{0, 0, 1}));
        CHECK(Near(me.uvs[2].x, 1) && Near(me.uvs[2].y, 1) && Near(me.uvs[3].x, 0) && Near(me.uvs[3].y, 1));   // as written (no v flip, like the 2.0 reader)
    }
    if (me.triangles.size() == 6) CHECK(me.triangles[0] == 0 && me.triangles[1] == 1 && me.triangles[2] == 2 && me.triangles[3] == 0 && me.triangles[4] == 2 && me.triangles[5] == 3);
    const ImportedMaterial& mat = m.materials[0];
    CHECK(mat.name == "Mat" && mat.hasDiffuse && Near(mat.diffuse.r, 0.5f) && Near(mat.diffuse.g, 0.25f) && Near(mat.diffuse.b, 1) && Near(mat.diffuse.a, 1));
    CHECK(Near(mat.shininess, 8));
}

int main() {
    RUN_SUITE("gltf1_reader");
    std::string err;

    {   // JSON with the buffer as a base64 data URI
        ImportedModel m;
        CHECK(ReadGltf1Buffer(QuadJson(), "", m, &err));
        CheckQuad(m);
        CHECK(m.notes.empty());
    }
    {   // the binary form (KHR_binary_glTF): the same scene from the body
        ImportedModel m;
        const std::string bin = QuadBinary();
        CHECK(IsGltf1(bin));
        CHECK(ReadGltf1Buffer(bin, "", m, &err));
        CheckQuad(m);
        // a binary file cut short in its last bytes, after everything the scene uses: read, with a note
        std::string cut = bin;   // add 8 bytes to the body the header claims but the file does not have
        const std::uint32_t claimed = (std::uint32_t)cut.size() + 8;
        for (int k = 0; k < 4; ++k) cut[8 + k] = (char)((claimed >> (8 * k)) & 0xff);
        CHECK(ReadGltf1Buffer(cut, "", m, &err) && HasNote(m, "shorter than its header"));
    }
    {   // node transforms given as translation / rotation / scale; a node with no scene (every node that is nobody's child)
        QuadOptions o; o.withScene = false;
        std::string j = QuadJson(o);
        const std::string from = "\"matrix\": [2,0,0,0, 0,2,0,0, 0,0,2,0, 0,0,5,1]";
        j.replace(j.find(from), from.size(), "\"translation\": [0, 0, 5], \"rotation\": [0, 0, 0.7071068, 0.7071068], \"scale\": [2, 2, 2]");
        ImportedModel m;
        CHECK(ReadGltf1Buffer(j, "", m, &err) && m.nodes.size() == 3);
        if (m.nodes.size() == 3) {
            CHECK(NearV(m.nodes[2].position, Vec3{0, 0, 5}) && NearV(m.nodes[2].scale, Vec3{2, 2, 2}));
            CHECK(Near(m.nodes[2].rotation.z, 0.7071068f) && Near(m.nodes[2].rotation.w, 0.7071068f));
        }
    }
    {   // KHR_materials_common gives the colour when values has none; a texture id gives the image file
        QuadOptions o;
        o.materialValues = "";
        o.materialExtra = ", \"extensions\": { \"KHR_materials_common\": { \"technique\": \"BLINN\", \"values\": { \"diffuse\": [0.1, 0.2, 0.3, 1], \"emission\": [0, 0, 0.5, 1] } } }";
        ImportedModel m;
        CHECK(ReadGltf1Buffer(QuadJson(o), "", m, &err) && m.materials.size() == 1);
        if (m.materials.size() == 1) CHECK(m.materials[0].hasDiffuse && Near(m.materials[0].diffuse.b, 0.3f) && m.materials[0].hasEmissive && Near(m.materials[0].emissive.b, 0.5f));
        o = QuadOptions();
        o.materialValues = "\"diffuse\": \"tex\"";
        o.extraTopLevel = ",\n  \"textures\": { \"tex\": { \"sampler\": \"smp\", \"source\": \"img\" } },\n  \"images\": { \"img\": { \"uri\": \"pics/wood.png\" } }";
        CHECK(ReadGltf1Buffer(QuadJson(o), "models/", m, &err) && m.materials.size() == 1);
        if (m.materials.size() == 1) CHECK(m.materials[0].texture == "models/pics/wood.png" && !m.materials[0].hasDiffuse);
        // an image outside the model's folder is refused (reported), the model still reads
        o.extraTopLevel = ",\n  \"textures\": { \"tex\": { \"sampler\": \"smp\", \"source\": \"img\" } },\n  \"images\": { \"img\": { \"uri\": \"../../secret.png\" } }";
        CHECK(ReadGltf1Buffer(QuadJson(o), "models/", m, &err) && m.materials.size() == 1 && m.materials[0].texture.empty() && HasNote(m, "outside the model's folder"));
    }
    {   // triangle strips and fans: 4 vertices give 2 triangles each, wound as the specification's modes describe
        QuadOptions o;
        o.primitive = "{ \"attributes\": { \"POSITION\": \"pos\" }, \"material\": \"mat\", \"mode\": 5 }";
        ImportedModel m;
        CHECK(ReadGltf1Buffer(QuadJson(o), "", m, &err) && m.parts.size() == 1);
        if (m.parts.size() == 1) {
            const std::vector<int>& t = m.parts[0].mesh.triangles;
            CHECK(t.size() == 6 && t[0] == 0 && t[1] == 1 && t[2] == 2 && t[3] == 2 && t[4] == 1 && t[5] == 3);
            CHECK(m.parts[0].mesh.normals.size() == 4);   // no NORMAL attribute: made from the faces
        }
        o.primitive = "{ \"attributes\": { \"POSITION\": \"pos\" }, \"material\": \"mat\", \"mode\": 6 }";
        CHECK(ReadGltf1Buffer(QuadJson(o), "", m, &err) && m.parts.size() == 1);
        if (m.parts.size() == 1) {
            const std::vector<int>& t = m.parts[0].mesh.triangles;
            CHECK(t.size() == 6 && t[0] == 0 && t[1] == 1 && t[2] == 2 && t[3] == 0 && t[4] == 2 && t[5] == 3);
        }
        // points and lines are skipped with a note; a file with nothing else is refused
        o.primitive = "{ \"attributes\": { \"POSITION\": \"pos\" }, \"material\": \"mat\", \"mode\": 1 }, { \"attributes\": { \"POSITION\": \"pos\" }, \"indices\": \"idx\", \"material\": \"mat\" }";
        CHECK(ReadGltf1Buffer(QuadJson(o), "", m, &err) && HasNote(m, "points or lines"));
        o.primitive = "{ \"attributes\": { \"POSITION\": \"pos\" }, \"material\": \"mat\", \"mode\": 0 }";
        CHECK(!ReadGltf1Buffer(QuadJson(o), "", m, &err) && err.find("no triangles") != std::string::npos);
    }
    {   // refusals: each leaves the model empty and says why
        auto with = [](const std::string& from, const std::string& to) { std::string j = QuadJson(); const std::size_t at = j.find(from); if (at != std::string::npos) j.replace(at, from.size(), to); return j; };
        const struct { std::string file; const char* why; } bad[] = {
            {"", "not valid JSON"},
            {"{ \"asset\": {", "not valid JSON"},
            {std::string(200, '[') + std::string(200, ']'), "nested more than 128"},
            {QuadJson([] { QuadOptions o; o.version = "2.0"; return o; }()), "asset.version is 2.0"},
            {"{ \"asset\": { \"version\": \"1.0\" }, \"meshes\": [] }", "this is glTF 2.0"},
            {with("\"count\": 4, \"type\": \"VEC3\" },\n    \"nor\"", "\"count\": 11, \"type\": \"VEC3\" },\n    \"nor\""), "past the end of its bufferView"},
            {with("\"byteStride\": 12, \"componentType\": 5126, \"count\": 4, \"type\": \"VEC3\" },\n    \"nor\"", "\"byteStride\": 8, \"componentType\": 5126, \"count\": 4, \"type\": \"VEC3\" },\n    \"nor\""), "smaller than one element"},
            {with("\"componentType\": 5123", "\"componentType\": 5124"), "unknown componentType"},
            {with("\"count\": 6", "\"count\": 0"), "bad count"},
            {with("\"count\": 6", "\"count\": 4294967295"), "bad count"},
            {with("\"indices\": \"idx\"", "\"indices\": \"nothing\""), "accessor 'nothing' is missing"},
            {with("\"byteLength\": 12, \"target\": 34963", "\"byteLength\": 4000, \"target\": 34963"), "past the end of its buffer"},
            {with("\"meshes\": [\"m\"] }", "\"meshes\": [\"m\"], \"children\": [\"root\"] }"), "reached twice"},
            {with("\"children\": [\"child\"]", "\"children\": [\"child\", \"child\"]"), "reached twice"},
            {with("\"meshes\": [\"m\"] }", "\"meshes\": [\"missing\"] }"), "missing mesh"},
            {with("\"matrix\": [2,0,0,0", "\"matrix\": [2,0,0"), "bad matrix"},
            {with("\"translation\": [1, 2, 3]", "\"translation\": [1, 2, 1e400]"), "bad translation"},
            {with("\"scene\": \"s\"", "\"scene\": \"nope\""), "default scene 'nope' is missing"},
            {QuadJson([] { QuadOptions o; o.bufferUri = "../outside.bin"; return o; }()), "buffer 'buf'"},
            {QuadJson([] { QuadOptions o; o.bufferUri = "/etc/passwd"; return o; }()), "buffer 'buf'"},
            {QuadJson([] { QuadOptions o; o.bufferUri = "data:application/octet-stream,abc"; return o; }()), "not base64"},
            {QuadJson([] { QuadOptions o; o.bufferUri = "data:application/octet-stream;base64,AAAA"; return o; }()), "shorter than its byteLength"},
            {Binary("{}", "", 2), "binary glTF version 2"},
            {std::string("glTF\x01\x00\x00\x00", 8), "cut short"},
        };
        for (const auto& b : bad) {
            ImportedModel m;
            err.clear();
            const bool ok = ReadGltf1Buffer(b.file, "models/", m, &err);
            CHECK(!ok && m.nodes.empty() && m.parts.empty() && err.rfind("glTF 1.0: ", 0) == 0);
            if (err.find(b.why) == std::string::npos) std::printf("  refusal '%s' does not contain '%s'\n", err.c_str(), b.why);
            CHECK(err.find(b.why) != std::string::npos);
        }
        // a NaN position is refused (a float accessor value must be finite)
        std::string buf = QuadBuffer();
        const float nan = std::nanf("");
        std::memcpy(&buf[4], &nan, 4);
        QuadOptions o; o.bufferUri = "data:application/octet-stream;base64," + Base64(buf);
        ImportedModel m;
        CHECK(!ReadGltf1Buffer(QuadJson(o), "", m, &err) && err.find("not a finite number") != std::string::npos);
    }

    // ---- files on disk: an external buffer next to the model, a UTF-8 folder name, and the import seam ----
    const fs::path dir = fs::temp_directory_path() / fs::u8path(u8"okay_gltf1_Müller_模型");
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "sub", ec);
    {
        QuadOptions o; o.bufferUri = "sub/quad.bin";
        WriteFile(dir / "quad.gltf", QuadJson(o));
        WriteFile(dir / "sub" / "quad.bin", QuadBuffer());
        WriteFile(dir / "quad_binary.glb", QuadBinary());
        ImportedModel m;
        CHECK(ReadGltf1File((dir / "quad.gltf").u8string(), m, &err));
        CheckQuad(m);
        CHECK(ReadGltf1File((dir / "quad_binary.glb").u8string(), m, &err));
        CheckQuad(m);
        CHECK(GltfFileIsVersion1((dir / "quad.gltf").u8string()) && GltfFileIsVersion1((dir / "quad_binary.glb").u8string()));
        // a missing external buffer is a refusal
        o.bufferUri = "sub/none.bin";
        WriteFile(dir / "broken.gltf", QuadJson(o));
        CHECK(!ReadGltf1File((dir / "broken.gltf").u8string(), m, &err) && err.find("none.bin") != std::string::npos);
        // a percent-escaped file name (Blender and other exporters write a space as %20): the buffer and the texture are found. Before the
        // fix this reader did not decode the URI and refused the file with "cannot open file" (the glTF 2.0 reader decodes it).
        WriteFile(dir / "sub" / "my quad.bin", QuadBuffer());
        o.bufferUri = "sub/my%20quad.bin";
        o.materialValues = "\"diffuse\": \"tex\"";
        o.extraTopLevel = ",\n  \"textures\": { \"tex\": { \"source\": \"img\" } },\n  \"images\": { \"img\": { \"uri\": \"pics/my%20wood.png\" } }";
        WriteFile(dir / "pct.gltf", QuadJson(o));
        CHECK(ReadGltf1File((dir / "pct.gltf").u8string(), m, &err));
        if (m.materials.size() == 1) CHECK(m.materials[0].texture == (dir / "pics" / "my wood.png").u8string());
        // an escaped ".." or NUL is decoded first and then refused by the folder rule, as the plain forms are
        o.bufferUri = "%2e%2e/outside.bin";
        WriteFile(dir / "pct_dots.gltf", QuadJson(o));
        CHECK(!ReadGltf1File((dir / "pct_dots.gltf").u8string(), m, &err) && err.find("leaves") != std::string::npos);
        o.bufferUri = "sub/quad.bin%00.png";
        WriteFile(dir / "pct_nul.gltf", QuadJson(o));
        CHECK(!ReadGltf1File((dir / "pct_nul.gltf").u8string(), m, &err));
    }
    {   // a glTF 2.0 file is not version 1, and keeps going to the 2.0 reader even with ours switched on
        const std::string v2 = std::string("{ \"asset\": { \"version\": \"2.0\" }, \"scene\": 0, \"scenes\": [ { \"nodes\": [0] } ], \"nodes\": [ { \"mesh\": 0 } ], ") +
            "\"meshes\": [ { \"primitives\": [ { \"attributes\": { \"POSITION\": 0 }, \"indices\": 1 } ] } ], " +
            "\"accessors\": [ { \"bufferView\": 0, \"componentType\": 5126, \"count\": 4, \"type\": \"VEC3\", \"min\": [0,0,0], \"max\": [1,1,0] }, { \"bufferView\": 1, \"componentType\": 5123, \"count\": 6, \"type\": \"SCALAR\" } ], " +
            "\"bufferViews\": [ { \"buffer\": 0, \"byteOffset\": 0, \"byteLength\": 48 }, { \"buffer\": 0, \"byteOffset\": 128, \"byteLength\": 12 } ], " +
            "\"buffers\": [ { \"byteLength\": 140, \"uri\": \"data:application/octet-stream;base64," + Base64(QuadBuffer()) + "\" } ] }";
        WriteFile(dir / "v2.gltf", v2);
        CHECK(!GltfFileIsVersion1((dir / "v2.gltf").u8string()));
        TestSetEnv("OKAY_OWN_GLTF1", "1");
        Scene scene; scene.physicsEnabled = false;
        bool ok = false;
        CHECK(ImportModelScene(scene, (dir / "v2.gltf").u8string(), &ok) != nullptr && ok);
        TestUnsetEnv("OKAY_OWN_GLTF1");
    }
    {   // the seam: off by default; the environment variable or SetImportBackend(".gltf1") switch it on
        const std::string path = (dir / "quad.gltf").u8string();
        CHECK(HasOwnReader(".gltf1"));
        TestUnsetEnv("OKAY_OWN_GLTF1");
        SetImportBackend(".gltf1", ImportBackend::Default);
        ImportedModel m;
        if (EffectiveImportBackend(".gltf1") != ImportBackend::Own) {   // a build without -DOKAY_OWN_GLTF1=ON
            CHECK(!ImportModelData(path, m, ImportBackend::Default, false, &err) || m.backend != "own");
            Scene scene; scene.physicsEnabled = false;
            bool ok = true;
            CHECK(ImportModelScene(scene, path, &ok) == nullptr && !ok);
        }
        TestSetEnv("OKAY_OWN_GLTF1", "1");
        CHECK(EffectiveImportBackend(".gltf1") == ImportBackend::Own);
        CHECK(ImportModelData(path, m, ImportBackend::Default, false, &err) && m.backend == "own" && m.parts.size() == 1);
        {   Scene scene; scene.physicsEnabled = false;
            bool ok = false;
            GameObject* root = ImportModelScene(scene, path, &ok);
            CHECK(ok && root != nullptr); }
        {   Scene scene; scene.physicsEnabled = false;
            bool ok = false;
            CHECK(ImportModelScene(scene, (dir / "quad_binary.glb").u8string(), &ok) != nullptr && ok); }
        bool mok = false;
        Mesh merged = ImportModel(path, &mok);
        CHECK(mok && merged.TriangleCount() == 2);
        // flatten bakes the node transforms: root (1 2 3) times child (scale 2, then 0 0 5): corner (1 1 0) lands at (3 4 8)
        CHECK(ImportModelData(path, m, ImportBackend::Default, true, &err) && m.parts.size() == 1);
        if (m.parts.size() == 1 && m.parts[0].mesh.vertices.size() == 4) CHECK(NearV(m.parts[0].mesh.vertices[2], Vec3{3, 4, 8}, 1e-4f));
        TestSetEnv("OKAY_OWN_GLTF1", "0");
        CHECK(EffectiveImportBackend(".gltf1") != ImportBackend::Own);
        CHECK(SetImportBackend(".gltf1", ImportBackend::Own));
        CHECK(EffectiveImportBackend(".gltf1") == ImportBackend::Own);
        CHECK(ImportModelData(path, m, ImportBackend::Default, false, &err) && m.backend == "own");
        SetImportBackend(".gltf1", ImportBackend::Default);
        TestUnsetEnv("OKAY_OWN_GLTF1");
    }
    fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
