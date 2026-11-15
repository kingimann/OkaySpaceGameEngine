// Our own Unreal 1 vertex-mesh reader: the `_d.3d` data file, the `_a.3d` animation (aniv) file and the `.uc` class file.
//
// Written from the public description of the format only: "Unreal File Format" by PantherD of Team Panther, edited by Paul
// Bourke (paulbourke.net/dataformats/unreal): the data-file header (polygon and vertex counts, then fields UnrealEd does not
// read; 48 bytes), the 16-byte triangle record (three vertex indices, the James mesh type, a colour, UV bytes on a 256 grid,
// a texture number, flags), the aniv file (frame count, frame size, then every frame's vertices, each packed in 32 bits:
// x and y as 11-bit signed values in eighths, z as a 10-bit signed value in quarters), and the `#exec` lines of the class
// file. No importer source was read (not Assimp's, not any other program's): see docs/own_unreal3d_reader.md.
//
// The output is the first frame as one static mesh per texture number (the engine has no vertex-animation clips). Unreal is
// left-handed and Z-up: Y is negated (the model is not mirrored) and the root node turns Z-up into Y-up, as for MD2.
#include "ReaderCommon.hpp"
#include "SceneRefPath.hpp"
#include "TextReaderUtil.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace okay {

namespace {
namespace unr {

constexpr std::size_t kMaxDataBytes = 16u * 1024u * 1024u;    // 48 + 65535 * 16 is about 1 MB
constexpr std::size_t kMaxAnivBytes = 256u * 1024u * 1024u;   // 65535 frames of 65535 vertices would be far more
constexpr std::size_t kMaxUcBytes   = 1024u * 1024u;
constexpr std::size_t kHeaderBytes  = 48;
constexpr std::size_t kTriBytes     = 16;

std::uint16_t U16(const unsigned char* p) { return (std::uint16_t)(p[0] | (p[1] << 8)); }
std::uint32_t U32(const unsigned char* p) { return (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) | ((std::uint32_t)p[2] << 16) | ((std::uint32_t)p[3] << 24); }

// A field of `bits` bits holding a two's-complement value.
int SignedField(std::uint32_t v, int bits) {
    const std::uint32_t mask = (1u << bits) - 1u;
    v &= mask;
    return (v & (1u << (bits - 1))) ? (int)v - (int)(1u << bits) : (int)v;
}

std::string Upper(std::string s) { for (char& c : s) c = (char)std::toupper((unsigned char)c); return s; }

// The file name part of a path written with backslashes or slashes ("MODELS\box_a.3d" -> "box_a.3d").
std::string FileNameOf(const std::string& p) {
    const std::size_t at = p.find_last_of("/\\");
    return at == std::string::npos ? p : p.substr(at + 1);
}

// What the class file says, from its #exec lines.
struct ClassInfo {
    std::string datafile, anivfile;          // file names only
    double scale[3] = {1, 1, 1};
    bool haveScale = false;
    double origin[6] = {0, 0, 0, 0, 0, 0};   // X Y Z PITCH YAW ROLL of MESH ORIGIN
    int sequences = 0;
    std::map<int, std::string> textureOfNum;          // MESHMAP SETTEXTURE NUM -> texture name
    std::map<std::string, std::string> fileOfTexture;  // TEXTURE IMPORT NAME -> FILE (the first one)
};

// KEY=VALUE pairs of one #exec line (values may be quoted); keys are upper-cased.
std::map<std::string, std::string> Pairs(const std::string& line) {
    std::map<std::string, std::string> out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace((unsigned char)line[i])) ++i;
        std::size_t k = i;
        while (k < line.size() && line[k] != '=' && !std::isspace((unsigned char)line[k])) ++k;
        if (k >= line.size() || line[k] != '=') { while (i < line.size() && !std::isspace((unsigned char)line[i])) ++i; continue; }
        const std::string key = Upper(line.substr(i, k - i));
        std::size_t v = k + 1;
        std::string val;
        if (v < line.size() && line[v] == '"') {
            const std::size_t e = line.find('"', v + 1);
            val = line.substr(v + 1, (e == std::string::npos ? line.size() : e) - v - 1);
            i = e == std::string::npos ? line.size() : e + 1;
        } else {
            std::size_t e = v;
            while (e < line.size() && !std::isspace((unsigned char)line[e])) ++e;
            val = line.substr(v, e - v);
            i = e;
        }
        if (!out.count(key)) out[key] = val;
    }
    return out;
}

bool Number(const std::map<std::string, std::string>& kv, const char* key, double& out) {
    auto it = kv.find(key);
    if (it == kv.end()) return false;
    double d = 0;
    if (!detail::txt::ToDouble(it->second, d) || !std::isfinite(d)) return false;
    out = d;
    return true;
}

ClassInfo ParseClass(const std::string& text) {
    ClassInfo ci;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find_first_of("\r\n", pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        std::size_t a = line.find_first_not_of(" \t");
        if (a == std::string::npos) continue;
        line = line.substr(a);
        if (line.size() < 5 || Upper(line.substr(0, 5)) != "#EXEC") continue;   // comments (//...) and code are skipped
        std::string rest = line.substr(5);
        std::string words[2];
        std::size_t i = 0;
        for (std::string& w : words) {
            while (i < rest.size() && std::isspace((unsigned char)rest[i])) ++i;
            std::size_t s = i;
            while (i < rest.size() && !std::isspace((unsigned char)rest[i])) ++i;
            w = Upper(rest.substr(s, i - s));
        }
        const auto kv = Pairs(rest.substr(i));
        if (words[0] == "MESH" && words[1] == "IMPORT") {
            if (kv.count("DATAFILE")) ci.datafile = FileNameOf(kv.at("DATAFILE"));
            if (kv.count("ANIVFILE")) ci.anivfile = FileNameOf(kv.at("ANIVFILE"));
        } else if (words[0] == "MESH" && words[1] == "ORIGIN") {
            const char* keys[6] = {"X", "Y", "Z", "PITCH", "YAW", "ROLL"};
            for (int k = 0; k < 6; ++k) Number(kv, keys[k], ci.origin[k]);
        } else if (words[0] == "MESH" && words[1] == "SEQUENCE") {
            ++ci.sequences;
        } else if (words[0] == "MESHMAP" && words[1] == "SCALE") {
            double s[3] = {1, 1, 1};
            const char* keys[3] = {"X", "Y", "Z"};
            for (int k = 0; k < 3; ++k) Number(kv, keys[k], s[k]);
            bool ok = true;
            for (double v : s) if (!(std::fabs(v) > 1e-9 && std::fabs(v) < 1e6)) ok = false;
            if (ok) { for (int k = 0; k < 3; ++k) ci.scale[k] = s[k]; ci.haveScale = true; }
        } else if (words[0] == "MESHMAP" && words[1] == "SETTEXTURE") {
            double num = -1;
            if (Number(kv, "NUM", num) && num >= 0 && num < 256 && kv.count("TEXTURE")) ci.textureOfNum[(int)num] = kv.at("TEXTURE");
        } else if (words[0] == "TEXTURE" && words[1] == "IMPORT") {
            if (kv.count("NAME") && kv.count("FILE") && !ci.fileOfTexture.count(Upper(kv.at("NAME"))))
                ci.fileOfTexture[Upper(kv.at("NAME"))] = kv.at("FILE");
        }
    }
    return ci;
}

struct Reader {
    std::string err;
    std::vector<std::string> notes;
    bool Fail(const std::string& m) { if (err.empty()) err = "Unreal: " + m; return false; }
    void Note(const std::string& m) { notes.push_back("Unreal: " + m); }

    bool Run(const std::string& data, const std::string& aniv, const std::string& uc, const std::string& baseDir, ImportedModel& out) {
        if (data.size() > kMaxDataBytes) return Fail("the data file is too large");
        if (aniv.size() > kMaxAnivBytes) return Fail("the animation file is too large");
        if (data.size() < kHeaderBytes) return Fail("the data file is shorter than its 48-byte header");
        if (aniv.size() < 4) return Fail("the animation file is shorter than its 4-byte header");
        const unsigned char* d = reinterpret_cast<const unsigned char*>(data.data());
        const unsigned char* a = reinterpret_cast<const unsigned char*>(aniv.data());
        const std::uint32_t numTris = U16(d), numVerts = U16(d + 2);
        if (numTris == 0) return Fail("the data file has no triangles");
        if (numVerts < 3) return Fail("the data file has fewer than 3 vertices");
        if ((std::uint64_t)numTris * kTriBytes > data.size() - kHeaderBytes)
            return Fail("the data file says " + std::to_string(numTris) + " triangles but holds " + std::to_string((data.size() - kHeaderBytes) / kTriBytes));
        const std::uint32_t numFrames = U16(a), frameSize = U16(a + 2);
        if (numFrames == 0) return Fail("the animation file has no frames");
        if (frameSize != numVerts * 4u)
            return Fail("the animation file's frame size (" + std::to_string(frameSize) + " bytes) is not 4 bytes for each of the " + std::to_string(numVerts) + " vertices");
        if ((std::uint64_t)numFrames * frameSize > aniv.size() - 4)
            return Fail("the animation file says " + std::to_string(numFrames) + " frames but is too short for them");
        if ((data.size() - kHeaderBytes) / kTriBytes > numTris) Note("bytes after the data file's triangles were ignored");

        ClassInfo ci;
        if (!uc.empty()) {
            if (uc.size() > kMaxUcBytes) return Fail("the class file is too large");
            ci = ParseClass(uc);
        }
        const double sx = ci.scale[0], sy = ci.scale[1], sz = ci.scale[2];

        // The first frame, scaled by the class file's MESHMAP SCALE; Y negated (left-handed to right-handed).
        std::vector<Vec3> pos(numVerts);
        for (std::uint32_t v = 0; v < numVerts; ++v) {
            const std::uint32_t packed = U32(a + 4 + 4 * (std::size_t)v);
            const double x = SignedField(packed, 11) / 8.0, y = SignedField(packed >> 11, 11) / 8.0, z = SignedField(packed >> 22, 10) / 4.0;
            pos[v] = Vec3{(float)(x * sx), (float)(-y * sy), (float)(z * sz)};
        }

        // One soup per texture number; triangles of type 8 are weapon-position markers, not drawn.
        std::map<int, detail::rc::Soup> soups;
        std::size_t badIndex = 0, markers = 0, twoSided = 0;
        for (std::uint32_t t = 0; t < numTris; ++t) {
            const unsigned char* r = d + kHeaderBytes + (std::size_t)t * kTriBytes;
            const std::uint32_t vi[3] = {U16(r), U16(r + 2), U16(r + 4)};
            const int type = (int)(std::uint8_t)r[6];
            const int texNum = (int)(std::uint8_t)r[14];
            if (type == 8) { ++markers; continue; }
            if (vi[0] >= numVerts || vi[1] >= numVerts || vi[2] >= numVerts) { ++badIndex; continue; }
            if (type >= 1 && type <= 4) ++twoSided;
            detail::rc::Soup& s = soups[texNum];
            if (s.pos.empty()) s.pos = pos;
            // Mirroring Y reverses the turning sense, so the corners are taken in reverse order to keep faces outward.
            for (int k : {2, 1, 0}) {
                s.ci.push_back(vi[k]);
                s.cuv.push_back(Vec2{(float)r[8 + 2 * k] / 256.0f, (float)r[9 + 2 * k] / 256.0f});
            }
        }
        if (soups.empty()) return Fail("no drawable triangles (all are weapon markers or have bad indices)");

        ImportedNode root;
        root.name = "Unreal";
        out.nodes.push_back(root);
        for (auto& kv : soups) {
            ImportedPart part;
            detail::rc::BuildStats st;
            if (!detail::rc::BuildMesh(kv.second, 180.0, part.mesh, &st)) return Fail("too many output vertices");
            if (part.mesh.triangles.empty()) continue;
            if (!detail::rc::PartInRange(part)) return Fail("a vertex is out of range");
            ImportedMaterial mat;
            mat.name = "Skin " + std::to_string(kv.first);
            auto tn = ci.textureOfNum.find(kv.first);
            if (tn != ci.textureOfNum.end()) {
                mat.name = tn->second;
                auto fi = ci.fileOfTexture.find(Upper(tn->second));
                if (fi != ci.fileOfTexture.end()) {
                    std::string path, why;
                    if (detail::ResolveInsideDir(baseDir, FileNameOf(fi->second), path, why)) mat.texture = path;
                }
            }
            mat.hasDiffuse = true;
            mat.diffuse = Color{1, 1, 1, 1};
            part.material = (int)out.materials.size();
            out.materials.push_back(mat);
            part.node = 0;
            part.mesh.name = mat.name;
            out.parts.push_back(std::move(part));
        }
        if (out.parts.empty()) return Fail("no usable triangles (all degenerate)");
        detail::rc::ApplyUpAxis(out, true, false);   // Unreal is Z-up
        out.backend = "own";

        if (numFrames > 1) Note(std::to_string(numFrames) + " animation frames: only the first is read (the engine has no vertex-animation clips)" + (ci.sequences ? "; " + std::to_string(ci.sequences) + " MESH SEQUENCE line(s) not read" : std::string()));
        if (markers) Note(std::to_string(markers) + " weapon-position marker triangle(s) (type 8) skipped");
        if (badIndex) Note(std::to_string(badIndex) + " triangle(s) with a vertex index past the " + std::to_string(numVerts) + " vertices skipped");
        if (twoSided) Note(std::to_string(twoSided) + " two-sided, translucent, masked or modulated triangle(s): read as one-sided");
        bool origin = false;
        for (double o : ci.origin) if (o != 0) origin = true;
        if (origin) Note("MESH ORIGIN (offset and rotation) is not applied");
        if (uc.empty()) Note("no class (.uc) file: no scale and no texture names were applied");
        for (const std::string& n : notes) out.notes.push_back(n);
        return true;
    }
};

} // namespace unr
} // namespace

bool ReadUnreal3dBuffers(const std::string& data, const std::string& aniv, const std::string& uc, const std::string& baseDir,
                         ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    unr::Reader r;
    if (!r.Run(data, aniv, uc, baseDir, out)) {
        const std::string m = r.err.empty() ? "Unreal: could not read the mesh" : r.err;
        out = ImportedModel{};
        if (err) *err = m;
        return false;
    }
    return true;
}

bool ReadUnreal3dFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    auto fail = [&](const std::string& m) { if (err) *err = "Unreal: " + m; return false; };
    const std::size_t slash = path.find_last_of("/\\");
    const std::string dir = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    std::string lower = name;
    for (char& c : lower) c = (char)std::tolower((unsigned char)c);
    auto endsWith = [&](const char* s) { const std::size_t n = std::strlen(s); return lower.size() >= n && lower.compare(lower.size() - n, n, s) == 0; };

    std::string dataName, anivName, ucText;
    if (endsWith(".uc")) {
        if (!detail::txt::ReadFileCapped(path, unr::kMaxUcBytes, ucText, err)) return false;
        const unr::ClassInfo ci = unr::ParseClass(ucText);
        if (ci.datafile.empty() || ci.anivfile.empty()) return fail("the class file has no MESH IMPORT line with DATAFILE and ANIVFILE");
        dataName = ci.datafile; anivName = ci.anivfile;
    } else if (endsWith("_d.3d") || endsWith("_a.3d")) {
        // name_d.3d <-> name_a.3d: keep the stem, the letter's case and the extension's spelling, swap the letter
        const std::string stem = name.substr(0, name.size() - 5);
        const bool upper = name[name.size() - 4] == 'D' || name[name.size() - 4] == 'A';
        const std::string ext = name.substr(name.size() - 3);
        dataName = stem + "_" + (upper ? "D" : "d") + ext;
        anivName = stem + "_" + (upper ? "A" : "a") + ext;
        std::string ucPath, why;   // the class file next to them, if any: <stem>.uc
        if (detail::ResolveInsideDir(dir, stem + ".uc", ucPath, why)) {
            std::string e;
            if (!detail::txt::ReadFileCapped(ucPath, unr::kMaxUcBytes, ucText, &e)) ucText.clear();
        }
    } else {
        return fail("not an Unreal mesh file (expected name_d.3d, name_a.3d or name.uc)");
    }
    std::string dataPath, anivPath, why;
    if (!detail::ResolveInsideDir(dir, dataName, dataPath, why)) return fail("data file '" + dataName + "': " + why);
    if (!detail::ResolveInsideDir(dir, anivName, anivPath, why)) return fail("animation file '" + anivName + "': " + why);
    std::string data, aniv, e1, e2;
    if (!detail::txt::ReadFileCapped(dataPath, unr::kMaxDataBytes, data, &e1)) return fail("data file '" + dataName + "': " + (e1.empty() ? "cannot read it" : e1));
    if (!detail::txt::ReadFileCapped(anivPath, unr::kMaxAnivBytes, aniv, &e2)) return fail("animation file '" + anivName + "': " + (e2.empty() ? "cannot read it" : e2));
    return ReadUnreal3dBuffers(data, aniv, ucText, dir, out, err);
}

} // namespace okay
