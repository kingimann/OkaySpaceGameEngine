// Every own model reader has a sample file in examples/own_readers, and every file there imports with OUR reader.
//
// examples/own_readers ships in the Windows download (scripts/build-windows-dist.sh copies its top-level files), so the owner
// can switch one reader on (OKAY_OWN_<FMT>=1) and import its sample (docs/try_it.md, section 8). This test keeps that promise:
//   1. one table below lists every reader switch with the extensions it reads;
//   2. every file in the folder (except README.txt) must belong to a row, and must import with the own reader forced on:
//      ImportModelData(..., ImportBackend::Own) gives backend "own" and a mesh with triangles (or a clip, for the motion-only
//      formats); a file named rig.* must also carry a skin. FBX goes through our okay::fbx reader directly (it is not on the
//      ImportModelData table);
//   3. every row has at least one sample (NDO is skipped: that reader is a seam that refuses every file);
//   4. the table is cross-checked against the engine (HasOwnReader, ImportableExtensions) and against the startup line's
//      reader list (OwnStack.hpp Readers()), so a new reader without a sample fails here.
// The generated samples come from scripts/gen_own_reader_samples.py; the copied ones are our own test fixtures.
#include "test_framework.hpp"
#include "OwnStack.hpp"
#include "okay/Render/FbxReader.hpp"
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

namespace {

enum class Want { Mesh, Clip };   // what a sample of the format must hold at least

struct Row {
    const char* var;      // the run-time switch ("" when there is none)
    const char* label;    // for messages
    std::vector<std::string> exts;
    Want want;
    bool skip;            // no sample expected (NDO)
};

// One row per own reader. Keep in step with OwnSlot / ImportableExtensions in engine/src/Render/ModelImport.cpp.
const std::vector<Row>& Table() {
    static const std::vector<Row> t = {
        {"OKAY_OWN_FBX", "FBX", {".fbx"}, Want::Mesh, false},
        {"OKAY_OWN_COLLADA", "COLLADA", {".dae"}, Want::Mesh, false},
        {"OKAY_OWN_3DS", "3DS", {".3ds"}, Want::Mesh, false},
        {"OKAY_OWN_BVH", "BVH", {".bvh"}, Want::Clip, false},
        {"OKAY_OWN_OFF", "OFF", {".off"}, Want::Mesh, false},
        {"OKAY_OWN_SMD", "SMD", {".smd"}, Want::Mesh, false},
        {"OKAY_OWN_MD5", "MD5", {".md5mesh", ".md5anim"}, Want::Mesh, false},   // an .md5anim alone is checked for a clip below
        {"OKAY_OWN_MS3D", "MS3D", {".ms3d"}, Want::Mesh, false},
        {"OKAY_OWN_ASE", "ASE", {".ase"}, Want::Mesh, false},
        {"OKAY_OWN_3MF", "3MF", {".3mf"}, Want::Mesh, false},
        {"OKAY_OWN_AMF", "AMF", {".amf"}, Want::Mesh, false},
        {"OKAY_OWN_X3D", "X3D", {".x3d"}, Want::Mesh, false},
        {"OKAY_OWN_DXF", "DXF", {".dxf"}, Want::Mesh, false},
        {"OKAY_OWN_LWO", "LWO", {".lwo"}, Want::Mesh, false},
        {"OKAY_OWN_BLEND", "Blender", {".blend"}, Want::Mesh, false},
        {"OKAY_OWN_MD2", "MD2", {".md2"}, Want::Mesh, false},
        {"OKAY_OWN_MD3", "MD3", {".md3"}, Want::Mesh, false},
        {"OKAY_OWN_IRRMESH", "IRRMESH", {".irrmesh"}, Want::Mesh, false},
        {"OKAY_OWN_B3D", "B3D", {".b3d"}, Want::Mesh, false},
        {"OKAY_OWN_X", "X", {".x"}, Want::Mesh, false},
        {"OKAY_OWN_AC3D", "AC3D", {".ac", ".acc", ".ac3d"}, Want::Mesh, false},
        {"OKAY_OWN_OGRE", "OGRE", {".mesh.xml"}, Want::Mesh, false},
        {"OKAY_OWN_XGL", "XGL", {".xgl", ".zgl"}, Want::Mesh, false},
        {"OKAY_OWN_IRR", "IRR", {".irr"}, Want::Mesh, false},
        {"OKAY_OWN_NFF", "NFF", {".nff", ".enff"}, Want::Mesh, false},
        {"OKAY_OWN_TER", "Terragen", {".ter"}, Want::Mesh, false},
        {"OKAY_OWN_MDC", "MDC", {".mdc"}, Want::Mesh, false},
        {"OKAY_OWN_HMP", "HMP", {".hmp"}, Want::Mesh, false},
        {"OKAY_OWN_CSM", "CSM", {".csm"}, Want::Clip, false},
        {"OKAY_OWN_Q3D", "Q3D", {".q3o", ".q3s"}, Want::Mesh, false},
        {"OKAY_OWN_RAW", "RAW", {".raw"}, Want::Mesh, false},
        {"OKAY_OWN_IQM", "IQM", {".iqm"}, Want::Mesh, false},
        {"OKAY_OWN_COB", "COB", {".cob"}, Want::Mesh, false},
        {"OKAY_OWN_NDO", "NDO", {".ndo"}, Want::Mesh, true},   // the reader is a seam: it refuses every file (docs/own_ndo_reader.md)
        {"OKAY_OWN_PMX", "PMX", {".pmx"}, Want::Mesh, false},
        {"OKAY_OWN_LWS", "LWS", {".lws", ".mot"}, Want::Mesh, false},
        // STL and PLY: own readers chosen by the build option OKAY_PREFER_OWN_READERS (no run-time switch yet: flip proposal row 0.8)
        {"", "STL", {".stl"}, Want::Mesh, false},
        {"", "PLY", {".ply"}, Want::Mesh, false},
    };
    return t;
}

std::string Lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }
bool EndsWith(const std::string& s, const std::string& suf) { return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0; }

// The same rule as the import seam: ".mesh.xml" is two parts, everything else is the last dot onwards.
std::string ExtOf(const std::string& name) {
    const std::string l = Lower(name);
    if (EndsWith(l, ".mesh.xml")) return ".mesh.xml";
    const std::size_t dot = l.find_last_of('.');
    return dot == std::string::npos ? std::string() : l.substr(dot);
}

int RowOf(const std::string& ext) {
    const auto& t = Table();
    for (std::size_t i = 0; i < t.size(); ++i)
        for (const std::string& e : t[i].exts) if (e == ext) return (int)i;
    return -1;
}

std::size_t Tris(const ImportedModel& m) { std::size_t n = 0; for (const ImportedPart& p : m.parts) n += p.mesh.triangles.size() / 3; return n; }
std::size_t Bones(const ImportedModel& m) { std::size_t n = 0; for (const ImportedPart& p : m.parts) n = std::max(n, p.bones.size()); return n; }

bool ImportOne(const fs::path& file, const Row& row) {
    const std::string path = file.u8string(), name = file.filename().u8string();
    const bool rig = name.rfind("rig.", 0) == 0;
    if (row.exts[0] == ".fbx") {
        fbx::Scene s; std::string err;
        const bool ok = fbx::LoadFile(path, s, &err);
        std::size_t tris = 0, clusters = 0, embedded = 0;
        for (const fbx::Mesh& m : s.meshes) { tris += m.triangleCount(); clusters += m.clusters.size(); }
        for (const fbx::Texture& t : s.textures) if (!t.content.empty()) ++embedded;
        std::printf("  %-22s %-10s %s: %zu meshes, %zu triangles, %zu skin clusters, %zu clips, %zu materials, %zu embedded textures%s%s\n", name.c_str(), row.label,
                    ok ? "ok" : "FAILED", s.meshes.size(), tris, clusters, s.clips.size(), s.materials.size(), embedded, ok ? "" : ": ", ok ? "" : err.c_str());
        return ok && !s.meshes.empty() && tris > 0 && (!rig || clusters > 0);
    }
    ImportedModel m; std::string err;
    const bool ok = ImportModelData(path, m, ImportBackend::Own, false, &err);
    std::printf("  %-22s %-10s %s: backend '%s', %zu parts, %zu triangles, %zu bones, %zu nodes, %zu clips, %zu notes%s%s\n", name.c_str(), row.label, ok ? "ok" : "FAILED",
                m.backend.c_str(), m.parts.size(), Tris(m), Bones(m), m.nodes.size(), m.clips.size(), m.notes.size(), ok ? "" : ": ", ok ? "" : err.c_str());
    for (const ImportedMaterial& mt : m.materials)   // the colour the shape should show (the README names it)
        if (mt.hasDiffuse) std::printf("      material '%s' diffuse %.2f %.2f %.2f%s%s\n", mt.name.c_str(), mt.diffuse.r, mt.diffuse.g, mt.diffuse.b, mt.texture.empty() ? "" : ", texture ", mt.texture.c_str());
    for (const ImportedClip& c : m.clips) std::printf("      clip '%s', %zu channels\n", c.name.c_str(), c.channels.size());
    for (const std::string& n : m.notes) std::printf("      note: %s\n", n.c_str());
    if (!ok || m.backend != "own") return false;
    const bool motionOnly = row.want == Want::Clip || ExtOf(name) == ".md5anim";
    if (motionOnly) return !m.clips.empty() && !m.clips[0].channels.empty();
    return !m.parts.empty() && Tris(m) > 0 && (!rig || Bones(m) > 0);
}

} // namespace

int main() {
    RUN_SUITE("own_reader_samples");
    const fs::path dir = fs::u8path(std::string(OKAY_SOURCE_DIR)) / "examples" / "own_readers";
    CHECK(fs::is_directory(dir));

    // Every reader forced on: the samples must be read by OUR code, never by a fallback. The Irrlicht and LightWave scene
    // samples name a mesh file next to them; that one is read through the seam too, so it is ours as well. FBX is read by
    // okay::fbx::LoadFile, our reader itself.
    SetImportBackend("*", ImportBackend::Own);

    std::map<int, std::vector<std::string>> samples;   // table row -> sample file names
    std::vector<fs::path> files;
    if (fs::is_directory(dir))
        for (const fs::directory_entry& e : fs::directory_iterator(dir)) {
            if (e.is_directory()) { std::fprintf(stderr, "  subfolder %s: the Windows build script copies top-level files only\n", e.path().filename().u8string().c_str()); CHECK(!e.is_directory()); continue; }
            files.push_back(e.path());
        }
    std::sort(files.begin(), files.end());
    std::printf("%zu files in %s\n", files.size(), dir.u8string().c_str());
    for (const fs::path& f : files) {
        const std::string name = f.filename().u8string();
        if (name == "README.txt") continue;
        const int r = RowOf(ExtOf(name));
        if (r < 0) { std::fprintf(stderr, "  %s: no own reader reads this extension\n", name.c_str()); CHECK(r >= 0); continue; }
        samples[r].push_back(name);
        const bool good = ImportOne(f, Table()[(std::size_t)r]);
        if (!good) std::fprintf(stderr, "  FAILED: %s did not import with our own %s reader as expected\n", name.c_str(), Table()[(std::size_t)r].label);
        CHECK(good);
    }

    // Every reader has a sample.
    int missing = 0;
    for (std::size_t i = 0; i < Table().size(); ++i) {
        const Row& row = Table()[i];
        if (row.skip) { std::printf("  %-10s skipped (the reader refuses every file)\n", row.label); continue; }
        if (samples[(int)i].empty()) {
            ++missing;
            std::fprintf(stderr, "  missing sample: %s (%s, %s)\n", row.label, row.var[0] ? row.var : "no switch", row.exts[0].c_str());
        }
    }
    std::printf("%d of %zu readers without a sample\n", missing, Table().size() - 1);
    CHECK(missing == 0);

    // The table matches the engine: each extension has an own reader, and every opt-in own format the engine advertises is
    // in the table (a reader added later without a row and a sample fails here). OBJ is always ours and needs no switch.
    for (const Row& row : Table())
        for (const std::string& e : row.exts) if (e != ".fbx") { const bool has = HasOwnReader(e); if (!has) std::fprintf(stderr, "  %s: HasOwnReader is false\n", e.c_str()); CHECK(has); }
    {
        const std::string list = ImportableExtensions() + " ";
        std::string cur;
        for (char c : list) {
            if (c != ' ') { cur += c; continue; }
            if (!cur.empty() && HasOwnReader(cur) && cur != ".obj" && RowOf(Lower(cur)) < 0) {
                std::fprintf(stderr, "  %s has an own reader but no row in this test's table\n", cur.c_str());
                CHECK(RowOf(Lower(cur)) >= 0);
            }
            cur.clear();
        }
    }

    // The startup line's reader list (shared/OwnStack.hpp): each switch it names has a sample.
    for (const ownstack::ReaderSwitch& rs : ownstack::Readers()) {
        const std::string ext = rs.ext[0] ? Lower(rs.ext) : std::string(".fbx");   // "" is FBX, which has its own switch
        const int r = RowOf(ext);
        if (r < 0) { std::fprintf(stderr, "  %s (%s): not in this test's table\n", rs.var, ext.c_str()); CHECK(r >= 0); continue; }
        if (Table()[(std::size_t)r].skip) continue;
        if (samples[r].empty()) std::fprintf(stderr, "  %s: in the startup line but no sample\n", rs.var);
        CHECK(!samples[r].empty());
    }

    SetImportBackend("*", ImportBackend::Default);
    TEST_MAIN_RESULT();
}
