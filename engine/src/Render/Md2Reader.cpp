// Our own MD2 (Quake II model, binary) reader: the first frame of the vertex animation as one static mesh, with the
// texture coordinates and the first skin name. Written from the public description of the format (id Software's
// Quake II source, qfiles.h, and the many format notes built on it). Bounds-checked, capped, no recursion.
// See okay/Render/ModelData.hpp and docs/own_md2_reader.md.
#include "BinReader.hpp"
#include "ReaderCommon.hpp"
#include "TextReaderUtil.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace okay {

namespace {
namespace md2 {

using detail::bin::Br;

constexpr std::size_t kMaxFileBytes = 256u * 1024u * 1024u;
constexpr std::uint32_t kMaxVerts = 1000000, kMaxTris = 2000000, kMaxTexCoords = 4000000;

bool Fail(std::string* err, const std::string& m) { if (err) *err = "MD2: " + m; return false; }

} // namespace md2
} // namespace

bool ReadMd2Buffer(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err) {
    using namespace md2;
    out = ImportedModel{};
    Br b(bytes);
    if (b.n < 68) return Fail(err, "file too short");
    if (std::memcmp(b.d, "IDP2", 4) != 0) return Fail(err, "not an MD2 file (the magic text IDP2 is missing)");
    b.pos = 4;
    const std::int32_t version = b.I32();
    if (version != 8) return Fail(err, "unsupported version " + std::to_string(version) + " (8 is read)");
    const std::int32_t skinW = b.I32(), skinH = b.I32(), frameSize = b.I32(), numSkins = b.I32(), numXyz = b.I32(), numSt = b.I32(),
                       numTris = b.I32(), numGl = b.I32(), numFrames = b.I32(), ofsSkins = b.I32(), ofsSt = b.I32(), ofsTris = b.I32(),
                       ofsFrames = b.I32(), ofsGl = b.I32();
    (void)numGl; (void)ofsGl;
    if (numXyz <= 0 || (std::uint32_t)numXyz > kMaxVerts) return Fail(err, "bad vertex count " + std::to_string(numXyz));
    if (numTris <= 0 || (std::uint32_t)numTris > kMaxTris) return Fail(err, "bad triangle count " + std::to_string(numTris));
    if (numSt < 0 || (std::uint32_t)numSt > kMaxTexCoords) return Fail(err, "bad texture coordinate count " + std::to_string(numSt));
    if (numFrames <= 0) return Fail(err, "the file has no frames");
    if (numSkins < 0) return Fail(err, "bad skin count");
    if (ofsFrames < 0 || ofsTris < 0 || ofsSt < 0 || ofsSkins < 0) return Fail(err, "a negative table offset");
    if (frameSize < 40 + 4 * numXyz) return Fail(err, "the frame size " + std::to_string(frameSize) + " is too small for " + std::to_string(numXyz) + " vertices");

    // ---- frame 0 ----
    if (!b.Seek((std::uint64_t)ofsFrames)) return Fail(err, "the frame table starts outside the file");
    if (!b.Has(40 + 4ull * (std::uint64_t)numXyz)) return Fail(err, "the file ends inside the first frame");
    float scale[3], trans[3];
    for (float& f : scale) f = b.F32();
    for (float& f : trans) f = b.F32();
    for (int k = 0; k < 3; ++k) if (!std::isfinite(scale[k]) || !std::isfinite(trans[k]) || std::fabs(scale[k]) > 1e9f || std::fabs(trans[k]) > 1e9f) return Fail(err, "the first frame has a scale or offset that is not finite or is out of range");
    const std::string frameName = b.Str(16);
    detail::rc::Soup soup;
    soup.pos.resize((std::size_t)numXyz);
    for (Vec3& p : soup.pos) {
        const float x = (float)b.U8(), y = (float)b.U8(), z = (float)b.U8();
        b.U8();   // index into Quake's table of 162 normals: not used, smooth normals are made instead
        p = Vec3{x * scale[0] + trans[0], y * scale[1] + trans[1], z * scale[2] + trans[2]};
    }
    if (b.bad) return Fail(err, "the file ends inside the first frame");

    // ---- texture coordinates ----
    std::vector<Vec2> st;
    if (numSt > 0) {
        if (!b.Seek((std::uint64_t)ofsSt) || !b.Fits((std::uint64_t)numSt, 4)) return Fail(err, "the file ends inside the texture coordinates");
        const float du = skinW > 0 ? (float)skinW : 1.0f, dv = skinH > 0 ? (float)skinH : 1.0f;
        st.resize((std::size_t)numSt);
        for (Vec2& t : st) { const float s = (float)b.I16(), tt = (float)b.I16(); t = Vec2{s / du, tt / dv}; }
    }

    // ---- triangles: 3 vertex indices then 3 texture indices, 16 bit each; Quake winds clockwise, the order is flipped ----
    if (!b.Seek((std::uint64_t)ofsTris) || !b.Fits((std::uint64_t)numTris, 12)) return Fail(err, "the file ends inside the triangle list");
    int badVert = 0, badSt = 0;
    soup.ci.reserve((std::size_t)numTris * 3);
    if (!st.empty()) soup.cuv.reserve((std::size_t)numTris * 3);
    for (std::int32_t t = 0; t < numTris; ++t) {
        std::uint16_t v[3], s[3];
        for (auto& x : v) x = b.U16();
        for (auto& x : s) x = b.U16();
        if (v[0] >= (std::uint32_t)numXyz || v[1] >= (std::uint32_t)numXyz || v[2] >= (std::uint32_t)numXyz) { ++badVert; continue; }
        if (!st.empty() && (s[0] >= st.size() || s[1] >= st.size() || s[2] >= st.size())) { ++badSt; continue; }
        for (int k : {2, 1, 0}) {
            soup.ci.push_back(v[k]);
            if (!st.empty()) soup.cuv.push_back(st[s[k]]);
        }
    }

    // ---- first skin ----
    std::string texture;
    if (numSkins > 0 && ofsSkins >= 0 && (std::uint64_t)ofsSkins + 64 <= b.n) {
        b.Seek((std::uint64_t)ofsSkins);
        texture = b.Str(64);
        for (char& c : texture) if (c == '\\') c = '/';
        if (!texture.empty() && !baseDir.empty() && texture[0] != '/' && texture.find(':') == std::string::npos) texture = baseDir + texture;
    }

    // ---- build ----
    ImportedPart part;
    detail::rc::BuildStats stats;
    if (!detail::rc::BuildMesh(soup, 180.0, part.mesh, &stats)) return Fail(err, "too many output vertices");
    if (part.mesh.triangles.empty()) return Fail(err, "the file has no usable triangles");
    part.mesh.name = frameName.empty() ? "MD2" : frameName;
    if (!detail::rc::PartInRange(part)) return Fail(err, "a vertex is out of range");
    ImportedNode root;
    root.name = "MD2";
    out.nodes.push_back(root);
    ImportedMaterial mat;
    mat.name = "MD2 skin";
    mat.texture = texture;
    mat.hasDiffuse = true;
    mat.diffuse = Color{1, 1, 1, 1};
    out.materials.push_back(mat);
    part.node = 0;
    part.material = 0;
    out.parts.push_back(std::move(part));
    detail::rc::ApplyUpAxis(out, true, false);   // Quake is Z-up
    out.backend = "own";

    auto note = [&](const std::string& s) { out.notes.push_back("MD2: " + s); };
    if (numFrames > 1) note(std::to_string(numFrames) + " frames of vertex animation: only the first frame is read (the engine has no vertex animation)");
    note("the stored vertex normals (Quake's table of 162 directions) are not read: smooth normals were generated");
    if (numSkins > 1) note(std::to_string(numSkins - 1) + " further skin(s) not imported");
    if (texture.empty()) note("the file names no skin: no texture");
    if (badVert) note(std::to_string(badVert) + " triangle(s) with an out-of-range vertex index skipped");
    if (badSt) note(std::to_string(badSt) + " triangle(s) with an out-of-range texture coordinate index skipped");
    if (stats.degenerate) note(std::to_string(stats.degenerate) + " degenerate triangle(s) skipped");
    note("the OpenGL command list is not read (the triangle list is used)");
    return true;
}

bool ReadMd2File(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, md2::kMaxFileBytes, bytes, err)) return false;
    std::string dir;
    const std::size_t sl = path.find_last_of("/\\");
    if (sl != std::string::npos) dir = path.substr(0, sl + 1);
    return ReadMd2Buffer(bytes, dir, out, err);
}

} // namespace okay
