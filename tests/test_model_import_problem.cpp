// Why a model did not import, in plain words (okay::DescribeModelImportProblem). The editor used to print only
// "Model import failed: <path>". Every file below really fails ImportModelScene first, so the reason is for a real
// failure, and nothing here may crash or throw.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/ModelImportProblem.hpp"
#include "okay/Graphics/TtfFont.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace okay;

namespace {
struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / ("okay_impprob_" + std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code ec;
        fs::create_directories(path, ec);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
    std::string at(const std::string& f) const { return (path / f).generic_string(); }
};
void Write(const std::string& p, const std::string& bytes) { std::ofstream(p, std::ios::binary | std::ios::trunc) << bytes; }
bool Has(const std::string& hay, const std::string& n) { return hay.find(n) != std::string::npos; }
std::string Le32(std::uint32_t v) { return std::string{char(v & 0xFF), char((v >> 8) & 0xFF), char((v >> 16) & 0xFF), char((v >> 24) & 0xFF)}; }

// Import must fail, and the reason must contain `want`.
bool Fails(const std::string& path, const std::string& want) {
    Scene s("probe");
    bool ok = true;
    GameObject* r = ImportModelScene(s, path, &ok);
    const std::string why = DescribeModelImportProblem(path);
    const bool good = (!r || !ok) && Has(why, want);
    if (!good) std::cerr << "  " << path << ": imported=" << (r && ok) << " reason='" << why << "' (wanted '" << want << "')\n";
    return good;
}
} // namespace

int main() {
    RUN_SUITE("the reason a model file did not import");
    TempDir d;
    CHECK(Has(DescribeModelImportProblem(""), "no file"));
    CHECK(Fails(d.at("gone.glb"), "not there"));
    fs::create_directories(d.path / "folder.obj");
    CHECK(Fails(d.at("folder.obj"), "folder"));
    for (const char* ext : {".fbx", ".glb", ".gltf", ".stl", ".ply", ".dae", ".3ds"}) {   // a folder with a model's name never crashes the import
        fs::create_directories(d.path / (std::string("folder") + ext));
        CHECK(Fails(d.at(std::string("folder") + ext), "folder"));
    }
    Write(d.at("empty.fbx"), "");
    CHECK(Fails(d.at("empty.fbx"), "empty (0 bytes)"));
    Write(d.at("model.xyz"), "whatever");
    CHECK(Fails(d.at("model.xyz"), "does not read .xyz"));
    Write(d.at("noext"), "whatever");
    CHECK(Fails(d.at("noext"), "no extension"));

    // glTF binary: a picture renamed, text in a .glb, glTF 1, a cut-off file, a short file
    Write(d.at("picture.glb"), std::string("\x89PNG\r\n\x1a\n", 8) + std::string(64, '\0'));
    CHECK(Fails(d.at("picture.glb"), "does not start with \"glTF\""));
    Write(d.at("text.glb"), "{\"asset\":{\"version\":\"2.0\"}}");
    CHECK(Fails(d.at("text.glb"), "rename it to .gltf"));
    Write(d.at("v1.glb"), "glTF" + Le32(1) + Le32(20) + std::string(8, '\0'));
    CHECK(Fails(d.at("v1.glb"), "glTF 1 binary"));
    Write(d.at("cut.glb"), "glTF" + Le32(2) + Le32(100000) + std::string(20, '\0'));
    CHECK(Fails(d.at("cut.glb"), "cut off"));
    Write(d.at("short.glb"), "glTF");
    CHECK(Fails(d.at("short.glb"), "too short"));
    // glTF text: binary in a .gltf, not JSON, broken JSON
    Write(d.at("bin.gltf"), "glTF" + Le32(2) + Le32(12));
    CHECK(Fails(d.at("bin.gltf"), "rename it to .glb"));
    Write(d.at("words.gltf"), "hello, this is not json");
    CHECK(Fails(d.at("words.gltf"), "starts with '{'"));
    Write(d.at("broken.gltf"), "\xEF\xBB\xBF  {\"asset\": {\"version\": \"2.0\"}, \"meshes\": [ {");
    CHECK(Fails(d.at("broken.gltf"), "could not make sense"));
    // FBX: not an FBX at all, and a binary FBX header with nothing after it
    Write(d.at("notfbx.fbx"), "PK\x03\x04 a zip file renamed");
    CHECK(Fails(d.at("notfbx.fbx"), "not an FBX file"));
    Write(d.at("cut.fbx"), std::string("Kaydara FBX Binary  \0\x1a\0", 23) + Le32(7400));
    CHECK(Fails(d.at("cut.fbx"), "FBX reader could not read it"));
    // FBX 5.x (2005 and before) is not read: the message names the version and says what to do
    Write(d.at("old5.fbx"), std::string("Kaydara FBX Binary  \0\x1a\0", 23) + Le32(5000) + std::string(13, '\0'));
    CHECK(Fails(d.at("old5.fbx"), "FBX 5.0"));
    CHECK(Has(DescribeModelImportProblem(d.at("old5.fbx")), "FBX 2011"));
    // read through the import seam: the reader's own words come back
    Write(d.at("junk.stl"), "solid x\n  this is not a facet\nendsolid\n");
    {
        Scene s("probe");
        bool ok = true;
        GameObject* r = ImportModelScene(s, d.at("junk.stl"), &ok);
        const std::string why = DescribeModelImportProblem(d.at("junk.stl"));
        CHECK((!r || !ok) && !why.empty());
        std::cout << "  junk.stl: " << why << "\n";
    }
    Write(d.at("nothing.obj"), "# only a comment\no Empty\n");
    {
        Scene s("probe");
        bool ok = true;
        GameObject* r = ImportModelScene(s, d.at("nothing.obj"), &ok);
        const std::string why = DescribeModelImportProblem(d.at("nothing.obj"));
        CHECK((!r || !ok) && !why.empty());
        std::cout << "  nothing.obj: " << why << "\n";
    }
    // a good file is not called broken by the checks before the reader (it would only be asked after a failure)
    Write(d.at("tri.obj"), "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    {
        Scene s("probe");
        bool ok = false;
        CHECK(ImportModelScene(s, d.at("tri.obj"), &ok) && ok);
        CHECK(Has(DescribeModelImportProblem(d.at("tri.obj")), "no object could be made"));
    }
    RUN_SUITE("a folder named like a font is refused, not a crash");
    {
        fs::create_directories(d.path / "folder.ttf");
        TtfFont f;
        CHECK(!f.LoadFromFile(d.at("folder.ttf")));
        Write(d.at("empty.ttf"), "");
        CHECK(!f.LoadFromFile(d.at("empty.ttf")));
    }
    TEST_MAIN_RESULT();
}
