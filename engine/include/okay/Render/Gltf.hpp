#pragma once
// ---------------------------------------------------------------------------
// Gltf — a small, self-contained glTF 2.0 mesh importer (.gltf + .glb). Brings
// real 3D models (exported from Blender, Maya, Sketchfab, etc.) into the engine's
// Mesh: positions, normals, UVs and indices, merging all mesh primitives. Handles
//   * .glb  (binary container: header + JSON chunk + BIN chunk),
//   * .gltf with an embedded base64 buffer (data:...;base64,...), and
//   * .gltf with an external .bin next to it.
// No external dependency — a tiny JSON parser + base64 decoder live here.
//
// Also resolves each material for the MeshRenderer: the base-color (albedo) and
// normal-map textures to file paths (external images referenced in place, embedded
// `data:`/.glb images extracted to a sidecar PNG/JPEG next to the model), plus the
// base-color / emissive factors and a metallic-roughness -> Blinn-Phong glint.
// (ImportModelScene rebuilds the node graph with per-node transforms; the merged
// LoadGLTF() path does not bake them.) Skinned/animated import and the packed
// metallic-roughness *texture* are planned follow-ups.
// ---------------------------------------------------------------------------
#include "okay/Render/Mesh.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Vec2.hpp"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace okay {
namespace gltf_detail {

// ---- Minimal JSON value + recursive-descent parser (enough for glTF) ----
struct JVal {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    double num = 0.0; bool boolean = false; std::string str;
    std::vector<JVal> arr;
    std::map<std::string, JVal> obj;

    const JVal* Find(const std::string& k) const {
        if (type != Obj) return nullptr;
        auto it = obj.find(k); return it == obj.end() ? nullptr : &it->second;
    }
    int    Int(int d = 0)    const { return type == Num ? (int)num : d; }
    double Number(double d=0)const { return type == Num ? num : d; }
    const std::string& Text()const { static std::string e; return type == Str ? str : e; }
    std::size_t Size()       const { return type == Arr ? arr.size() : 0; }
};

struct JParser {
    const char* p; const char* end; bool ok = true;
    explicit JParser(const std::string& s) : p(s.data()), end(s.data() + s.size()) {}
    void ws() { while (p < end && (*p==' '||*p=='\t'||*p=='\n'||*p=='\r')) ++p; }
    JVal Parse() { ws(); return Value(); }
    JVal Value() {
        ws(); if (p >= end) { ok = false; return {}; }
        char c = *p;
        if (c == '{') return Object();
        if (c == '[') return Array();
        if (c == '"') { JVal v; v.type = JVal::Str; v.str = String(); return v; }
        if (c == 't' || c == 'f') return Bool();
        if (c == 'n') { p += (end - p >= 4) ? 4 : (end - p); JVal v; v.type = JVal::Null; return v; }
        return Num();
    }
    JVal Object() {
        JVal v; v.type = JVal::Obj; ++p; ws();
        if (p < end && *p == '}') { ++p; return v; }
        while (p < end) {
            ws(); if (p >= end || *p != '"') { ok = false; break; }
            std::string key = String(); ws();
            if (p >= end || *p != ':') { ok = false; break; }
            ++p; v.obj[key] = Value(); ws();
            if (p < end && *p == ',') { ++p; continue; }
            if (p < end && *p == '}') { ++p; break; }
            ok = false; break;
        }
        return v;
    }
    JVal Array() {
        JVal v; v.type = JVal::Arr; ++p; ws();
        if (p < end && *p == ']') { ++p; return v; }
        while (p < end) {
            v.arr.push_back(Value()); ws();
            if (p < end && *p == ',') { ++p; continue; }
            if (p < end && *p == ']') { ++p; break; }
            ok = false; break;
        }
        return v;
    }
    std::string String() {
        std::string s; ++p;   // opening quote
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) {
                ++p; char e = *p;
                switch (e) {
                    case 'n': s += '\n'; break; case 't': s += '\t'; break;
                    case 'r': s += '\r'; break; case '"': s += '"'; break;
                    case '\\': s += '\\'; break; case '/': s += '/'; break;
                    case 'u': if (end - p >= 5) { p += 4; s += '?'; } break;   // BMP escape -> placeholder
                    default: s += e; break;
                }
                ++p;
            } else s += *p++;
        }
        if (p < end) ++p;   // closing quote
        return s;
    }
    JVal Bool() {
        JVal v; v.type = JVal::Bool;
        if (end - p >= 4 && std::strncmp(p, "true", 4) == 0) { v.boolean = true; p += 4; }
        else if (end - p >= 5 && std::strncmp(p, "false", 5) == 0) { v.boolean = false; p += 5; }
        else ok = false;
        return v;
    }
    JVal Num() {
        char* e2 = nullptr;
        double d = std::strtod(p, &e2);
        JVal v; v.type = JVal::Num; v.num = d;
        if (e2 == p) ok = false; else p = e2;
        return v;
    }
};

inline bool Base64Decode(const std::string& in, std::vector<std::uint8_t>& out) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62; if (c == '/') return 63;
        return -1;
    };
    int bits = 0, acc = 0;
    for (char c : in) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        int v = val(c); if (v < 0) continue;
        acc = (acc << 6) | v; bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((std::uint8_t)((acc >> bits) & 0xFF)); }
    }
    return true;
}

inline std::uint32_t RdU32(const std::uint8_t* d) { return d[0] | (d[1]<<8) | (d[2]<<16) | ((std::uint32_t)d[3]<<24); }

// A parsed glTF document: the JSON root + resolved binary buffers. Built by LoadDoc,
// consumed by ReadAccessor / AppendPrimitive (shared by the mesh + scene importers).
struct GltfDoc {
    JVal root;
    std::vector<std::vector<std::uint8_t>> buffers;
    std::string dir;
    bool ok = false;
};

inline GltfDoc LoadDoc(const std::string& path) {
    GltfDoc doc;
    std::ifstream f(path, std::ios::binary);
    if (!f) return doc;
    std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bytes.size() < 4) return doc;

    std::string jsonText;
    std::vector<std::uint8_t> glbBin;   // BIN chunk for .glb (buffer 0 with no uri)
    if (std::memcmp(bytes.data(), "glTF", 4) == 0) {        // .glb container
        const std::uint8_t* d = (const std::uint8_t*)bytes.data();
        std::size_t n = bytes.size(), off = 12;            // header: magic(4) version(4) length(4)
        while (off + 8 <= n) {
            std::uint32_t clen = RdU32(d + off), ctype = RdU32(d + off + 4);
            std::size_t cstart = off + 8;
            if (cstart + clen > n) break;
            if (ctype == 0x4E4F534A)      jsonText.assign((const char*)d + cstart, clen);   // "JSON"
            else if (ctype == 0x004E4942) glbBin.assign(d + cstart, d + cstart + clen);       // "BIN\0"
            off = cstart + clen + ((clen % 4) ? (4 - clen % 4) : 0);
        }
    } else {
        jsonText = bytes;
    }
    if (jsonText.empty()) return doc;

    JParser jp(jsonText);
    doc.root = jp.Parse();
    if (!jp.ok || doc.root.type != JVal::Obj) { doc.root = JVal{}; return doc; }
    { std::size_t s = path.find_last_of("/\\"); if (s != std::string::npos) doc.dir = path.substr(0, s + 1); }

    if (const JVal* bufs = doc.root.Find("buffers")) {
        for (const JVal& b : bufs->arr) {
            std::vector<std::uint8_t> data;
            const JVal* uri = b.Find("uri");
            if (uri && uri->type == JVal::Str) {
                const std::string& u = uri->str; const std::string mark = "base64,";
                std::size_t bp = u.find(mark);
                if (u.rfind("data:", 0) == 0 && bp != std::string::npos)
                    Base64Decode(u.substr(bp + mark.size()), data);
                else { std::ifstream bf(doc.dir + u, std::ios::binary);
                       if (bf) data.assign((std::istreambuf_iterator<char>(bf)), std::istreambuf_iterator<char>()); }
            } else data = glbBin;   // bufferless entry in a .glb -> the BIN chunk
            doc.buffers.push_back(std::move(data));
        }
    }
    doc.ok = true;
    return doc;
}

inline int CompSize(int ct) {
    switch (ct) { case 5120: case 5121: return 1; case 5122: case 5123: return 2;
                  case 5125: case 5126: return 4; default: return 4; }
}
inline int TypeComps(const std::string& t) {
    if (t == "SCALAR") return 1; if (t == "VEC2") return 2; if (t == "VEC3") return 3;
    if (t == "VEC4") return 4; if (t == "MAT4") return 16; return 1;
}

// Read accessor `ai` as a flat float array (count*components). Integer index types are
// converted to floats too (for `indices`).
inline std::vector<float> ReadAccessor(const GltfDoc& doc, int ai, int& outComps, int& outCount) {
    std::vector<float> out; outComps = 0; outCount = 0;
    const JVal* accs = doc.root.Find("accessors");
    const JVal* views = doc.root.Find("bufferViews");
    if (!accs || !views || ai < 0 || ai >= (int)accs->arr.size()) return out;
    const JVal& a = accs->arr[ai];
    int viIdx = a.Find("bufferView") ? a.Find("bufferView")->Int(-1) : -1;
    int ct = a.Find("componentType") ? a.Find("componentType")->Int(5126) : 5126;
    int count = a.Find("count") ? a.Find("count")->Int(0) : 0;
    int comps = TypeComps(a.Find("type") ? a.Find("type")->Text() : "SCALAR");
    int aoff = a.Find("byteOffset") ? a.Find("byteOffset")->Int(0) : 0;
    outComps = comps; outCount = count;
    if (viIdx < 0 || viIdx >= (int)views->arr.size()) return out;
    const JVal& v = views->arr[viIdx];
    int buf = v.Find("buffer") ? v.Find("buffer")->Int(-1) : -1;
    int voff = v.Find("byteOffset") ? v.Find("byteOffset")->Int(0) : 0;
    int stride = v.Find("byteStride") ? v.Find("byteStride")->Int(0) : 0;
    if (buf < 0 || buf >= (int)doc.buffers.size()) return out;
    const std::vector<std::uint8_t>& data = doc.buffers[buf];
    int csz = CompSize(ct);
    if (stride == 0) stride = comps * csz;
    out.reserve((std::size_t)count * comps);
    for (int i = 0; i < count; ++i) {
        std::size_t base = (std::size_t)voff + aoff + (std::size_t)i * stride;
        for (int c = 0; c < comps; ++c) {
            std::size_t at = base + (std::size_t)c * csz;
            if (at + csz > data.size()) { out.push_back(0.0f); continue; }
            const std::uint8_t* d = data.data() + at; float fv = 0.0f;
            switch (ct) {
                case 5126: { float t; std::memcpy(&t, d, 4); fv = t; break; }
                case 5125: { std::uint32_t t; std::memcpy(&t, d, 4); fv = (float)t; break; }
                case 5123: { std::uint16_t t; std::memcpy(&t, d, 2); fv = (float)t; break; }
                case 5121: fv = (float)d[0]; break;
                case 5122: { std::int16_t t; std::memcpy(&t, d, 2); fv = (float)t; break; }
                case 5120: fv = (float)(std::int8_t)d[0]; break;
                default: break;
            }
            out.push_back(fv);
        }
    }
    return out;
}

// Append one primitive's geometry to `mesh` (offsetting indices by the current vertex
// count), so a mesh's primitives — or many meshes — can be merged into one Mesh.
inline void AppendPrimitive(const GltfDoc& doc, const JVal& prim, Mesh& mesh) {
    const JVal* attr = prim.Find("attributes");
    if (!attr) return;
    int base = (int)mesh.vertices.size(), pc = 0, pn = 0;
    if (const JVal* P = attr->Find("POSITION")) {
        auto pos = ReadAccessor(doc, P->Int(-1), pc, pn);
        for (int i = 0; i < pn; ++i) mesh.vertices.push_back(Vec3{pos[i*3+0], pos[i*3+1], pos[i*3+2]});
    }
    if (const JVal* N = attr->Find("NORMAL")) {
        int nc = 0, nn = 0; auto nor = ReadAccessor(doc, N->Int(-1), nc, nn);
        for (int i = 0; i < nn; ++i) mesh.normals.push_back(Vec3{nor[i*3+0], nor[i*3+1], nor[i*3+2]});
    }
    if (const JVal* T = attr->Find("TEXCOORD_0")) {
        int tc = 0, tn = 0; auto uv = ReadAccessor(doc, T->Int(-1), tc, tn);
        for (int i = 0; i < tn; ++i) mesh.uvs.push_back(Vec2{uv[i*2+0], uv[i*2+1]});
    }
    if (const JVal* I = prim.Find("indices")) {
        int ic = 0, in = 0; auto idx = ReadAccessor(doc, I->Int(-1), ic, in);
        for (int i = 0; i < in; ++i) mesh.triangles.push_back(base + (int)(idx[i] + 0.5f));
    } else {
        for (int i = 0; i + 2 < pn; i += 3) {
            mesh.triangles.push_back(base + i);
            mesh.triangles.push_back(base + i + 1);
            mesh.triangles.push_back(base + i + 2);
        }
    }
}

// Build one glTF mesh (by index) into a Mesh.
inline Mesh BuildMeshAt(const GltfDoc& doc, int meshIndex) {
    Mesh mesh;
    const JVal* meshes = doc.root.Find("meshes");
    if (!meshes || meshIndex < 0 || meshIndex >= (int)meshes->arr.size()) return mesh;
    if (const JVal* prims = meshes->arr[meshIndex].Find("primitives"))
        for (const JVal& prim : prims->arr) AppendPrimitive(doc, prim, mesh);
    if (mesh.normals.size() != mesh.vertices.size()) mesh.normals.clear();
    if (mesh.uvs.size()     != mesh.vertices.size()) mesh.uvs.clear();
    return mesh;
}

// Decode %20-style percent escapes in a glTF URI (Blender writes spaces as %20).
inline std::string UriDecode(const std::string& s) {
    std::string out; out.reserve(s.size());
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int h = hex(s[i + 1]), l = hex(s[i + 2]);
            if (h >= 0 && l >= 0) { out += (char)((h << 4) | l); i += 2; continue; }
        }
        out += s[i];
    }
    return out;
}

// Write already-encoded image bytes (PNG/JPEG, as glTF stores them) to a sidecar
// file next to the model, named "<model>_texN.<ext>". Returns "" on failure.
inline std::string WriteImageSidecar(const GltfDoc& doc, const std::string& modelPath,
                                     int imageIdx, const std::string& mime,
                                     const std::vector<std::uint8_t>& data) {
    if (data.empty()) return {};
    std::string ext = (mime.find("jpeg") != std::string::npos || mime.find("jpg") != std::string::npos)
                          ? ".jpg" : ".png";
    std::string stem = modelPath;
    if (std::size_t sl = stem.find_last_of("/\\"); sl != std::string::npos) stem = stem.substr(sl + 1);
    if (std::size_t dot = stem.find_last_of('.'); dot != std::string::npos) stem = stem.substr(0, dot);
    std::string out = doc.dir + stem + "_tex" + std::to_string(imageIdx) + ext;
    std::ofstream of(out, std::ios::binary);
    if (!of) return {};
    of.write((const char*)data.data(), (std::streamsize)data.size());
    return of ? out : std::string{};
}

// The image index of a mesh's base-color texture (its first primitive's material),
// or -1 if that mesh has none.
inline int MeshBaseColorImage(const GltfDoc& doc, int meshIndex) {
    const JVal* meshes = doc.root.Find("meshes");
    const JVal* mats   = doc.root.Find("materials");
    const JVal* texs   = doc.root.Find("textures");
    if (!meshes || !mats || !texs || meshIndex < 0 || meshIndex >= (int)meshes->arr.size()) return -1;
    const JVal* prims = meshes->arr[meshIndex].Find("primitives"); if (!prims) return -1;
    for (const JVal& prim : prims->arr) {
        const JVal* mi = prim.Find("material"); if (!mi) continue;
        int matI = mi->Int(-1); if (matI < 0 || matI >= (int)mats->arr.size()) continue;
        const JVal* pbr = mats->arr[matI].Find("pbrMetallicRoughness"); if (!pbr) continue;
        const JVal* bct = pbr->Find("baseColorTexture"); if (!bct) continue;
        int texI = bct->Find("index") ? bct->Find("index")->Int(-1) : -1;
        if (texI < 0 || texI >= (int)texs->arr.size()) continue;
        const JVal* src = texs->arr[texI].Find("source"); if (!src) continue;
        int im = src->Int(-1); if (im >= 0) return im;
    }
    return -1;
}

// Resolve an image index to a file path on disk. External images are referenced in
// place (resolved next to the model, percent-decoded); embedded images (`data:`
// URIs or `.glb` bufferViews) are extracted to a sidecar file. "" if unavailable.
inline std::string ImageFilePath(const GltfDoc& doc, int imageIdx, const std::string& modelPath) {
    const JVal* imgs = doc.root.Find("images");
    if (!imgs || imageIdx < 0 || imageIdx >= (int)imgs->arr.size()) return {};
    const JVal& img = imgs->arr[imageIdx];

    if (const JVal* uri = img.Find("uri"); uri && uri->type == JVal::Str) {
        const std::string& u = uri->str;
        if (u.rfind("data:", 0) != 0) {                       // external image file
            std::string du = UriDecode(u);
            bool absolute = (!du.empty() && (du[0] == '/' || du[0] == '\\')) ||
                            (du.size() > 1 && du[1] == ':');
            return absolute ? du : doc.dir + du;
        }
        std::string mime = "image/png";                       // data: base64 payload
        std::size_t colon = u.find(':'), semi = u.find(';');
        if (colon != std::string::npos && semi != std::string::npos && semi > colon)
            mime = u.substr(colon + 1, semi - colon - 1);
        const std::string mark = "base64,"; std::size_t bp = u.find(mark);
        if (bp == std::string::npos) return {};
        std::vector<std::uint8_t> data; Base64Decode(u.substr(bp + mark.size()), data);
        return WriteImageSidecar(doc, modelPath, imageIdx, mime, data);
    }
    if (const JVal* bv = img.Find("bufferView")) {            // embedded in a .glb
        int bvi = bv->Int(-1);
        const JVal* views = doc.root.Find("bufferViews");
        if (!views || bvi < 0 || bvi >= (int)views->arr.size()) return {};
        const JVal& v = views->arr[bvi];
        int buf = v.Find("buffer") ? v.Find("buffer")->Int(-1) : -1;
        int off = v.Find("byteOffset") ? v.Find("byteOffset")->Int(0) : 0;
        int len = v.Find("byteLength") ? v.Find("byteLength")->Int(0) : 0;
        if (buf < 0 || buf >= (int)doc.buffers.size() || len <= 0) return {};
        const auto& s = doc.buffers[buf];
        if ((std::size_t)off + (std::size_t)len > s.size()) return {};
        std::vector<std::uint8_t> data(s.begin() + off, s.begin() + off + len);
        std::string mime = img.Find("mimeType") ? img.Find("mimeType")->Text() : "image/png";
        return WriteImageSidecar(doc, modelPath, imageIdx, mime, data);
    }
    return {};
}

// Base-color texture path for a single mesh (per-node import). "" if none.
inline std::string ResolveMeshTexture(const GltfDoc& doc, int meshIndex, const std::string& modelPath) {
    int im = MeshBaseColorImage(doc, meshIndex);
    return im < 0 ? std::string{} : ImageFilePath(doc, im, modelPath);
}

// A mesh's material distilled to what the engine's MeshRenderer can use: albedo +
// normal-map file paths, and base-color / emissive / metallic-roughness factors.
struct GltfMat {
    std::string baseColorTex;                // albedo image path ("" if none)
    std::string normalTex;                   // tangent-space normal map path ("" if none)
    float baseColor[4] = {1, 1, 1, 1};
    float emissive[3]  = {0, 0, 0};
    float metallic  = 1.0f, roughness = 1.0f;
    bool  hasBaseColorFactor = false, hasEmissiveFactor = false, hasMetalRough = false;
};

// The material index a mesh uses (its first primitive with a material), or -1.
inline int MeshMaterialIndex(const GltfDoc& doc, int meshIndex) {
    const JVal* meshes = doc.root.Find("meshes");
    if (!meshes || meshIndex < 0 || meshIndex >= (int)meshes->arr.size()) return -1;
    const JVal* prims = meshes->arr[meshIndex].Find("primitives"); if (!prims) return -1;
    for (const JVal& prim : prims->arr) {
        const JVal* mi = prim.Find("material");
        if (mi) { int m = mi->Int(-1); if (m >= 0) return m; }
    }
    return -1;
}

// Resolve a mesh's material into a GltfMat (texture paths + factors).
inline GltfMat ResolveMeshMaterial(const GltfDoc& doc, int meshIndex, const std::string& modelPath) {
    GltfMat out;
    const JVal* mats = doc.root.Find("materials");
    const JVal* texs = doc.root.Find("textures");
    int matI = MeshMaterialIndex(doc, meshIndex);
    if (!mats || matI < 0 || matI >= (int)mats->arr.size()) return out;
    const JVal& mat = mats->arr[matI];
    auto texImage = [&](const JVal* ref) -> int {          // a {index:..} texture ref -> image index
        if (!ref || !texs) return -1;
        int ti = ref->Find("index") ? ref->Find("index")->Int(-1) : -1;
        if (ti < 0 || ti >= (int)texs->arr.size()) return -1;
        const JVal* src = texs->arr[ti].Find("source");
        return src ? src->Int(-1) : -1;
    };
    if (const JVal* pbr = mat.Find("pbrMetallicRoughness")) {
        if (const JVal* bcf = pbr->Find("baseColorFactor"); bcf && bcf->type == JVal::Arr) {
            for (int i = 0; i < 4 && i < (int)bcf->arr.size(); ++i) out.baseColor[i] = (float)bcf->arr[i].Number(out.baseColor[i]);
            out.hasBaseColorFactor = true;
        }
        if (int im = texImage(pbr->Find("baseColorTexture")); im >= 0) out.baseColorTex = ImageFilePath(doc, im, modelPath);
        if (const JVal* mf = pbr->Find("metallicFactor"))  { out.metallic  = (float)mf->Number(1.0); out.hasMetalRough = true; }
        if (const JVal* rf = pbr->Find("roughnessFactor")) { out.roughness = (float)rf->Number(1.0); out.hasMetalRough = true; }
    }
    if (int im = texImage(mat.Find("normalTexture")); im >= 0) out.normalTex = ImageFilePath(doc, im, modelPath);
    if (const JVal* ef = mat.Find("emissiveFactor"); ef && ef->type == JVal::Arr) {
        for (int i = 0; i < 3 && i < (int)ef->arr.size(); ++i) out.emissive[i] = (float)ef->arr[i].Number(0.0);
        out.hasEmissiveFactor = true;
    }
    return out;
}

// Base-color texture path for the whole model (first textured mesh) — used by the
// merged LoadGLTF path where all primitives collapse into one Mesh.
inline std::string ResolveBaseColorTexture(const GltfDoc& doc, const std::string& modelPath) {
    const JVal* meshes = doc.root.Find("meshes"); if (!meshes) return {};
    for (int i = 0; i < (int)meshes->arr.size(); ++i) {
        std::string p = ResolveMeshTexture(doc, i, modelPath);
        if (!p.empty()) return p;
    }
    return {};
}

} // namespace gltf_detail

// Load a glTF/GLB file into one merged Mesh. `ok` (optional) reports success;
// `outTexture` (optional) receives the base-color texture's file path (external
// images are referenced in place, embedded ones extracted next to the model).
inline Mesh LoadGLTF(const std::string& path, bool* ok = nullptr, std::string* outTexture = nullptr) {
    using namespace gltf_detail;
    Mesh mesh;
    GltfDoc doc = LoadDoc(path);
    const JVal* meshes = doc.ok ? doc.root.Find("meshes") : nullptr;
    if (!meshes) { if (ok) *ok = false; return mesh; }
    for (const JVal& m : meshes->arr)
        if (const JVal* prims = m.Find("primitives"))
            for (const JVal& prim : prims->arr) AppendPrimitive(doc, prim, mesh);
    if (mesh.normals.size() != mesh.vertices.size()) mesh.normals.clear();
    if (mesh.uvs.size()     != mesh.vertices.size()) mesh.uvs.clear();
    if (outTexture) { std::string t = ResolveBaseColorTexture(doc, path); if (!t.empty()) *outTexture = t; }
    if (ok) *ok = !mesh.vertices.empty();
    return mesh;
}

} // namespace okay
