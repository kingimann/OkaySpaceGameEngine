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
// Scope (v1): geometry only (no skin/animation yet); node transforms are not baked,
// so a model authored at the origin imports 1:1. Skinned/animated import is a planned
// follow-up that will feed the Character/Animator systems.
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

} // namespace gltf_detail

// Load a glTF/GLB file into one merged Mesh. `ok` (optional) reports success.
inline Mesh LoadGLTF(const std::string& path, bool* ok = nullptr) {
    using namespace gltf_detail;
    Mesh mesh;
    auto fail = [&]() -> Mesh { if (ok) *ok = false; return mesh; };

    std::ifstream f(path, std::ios::binary);
    if (!f) return fail();
    std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bytes.size() < 4) return fail();

    std::string jsonText;
    std::vector<std::uint8_t> glbBin;   // BIN chunk for .glb (buffer 0 with no uri)
    bool isGlb = (std::memcmp(bytes.data(), "glTF", 4) == 0);
    if (isGlb) {
        const std::uint8_t* d = (const std::uint8_t*)bytes.data();
        std::size_t n = bytes.size();
        std::size_t off = 12;   // header: magic(4) version(4) length(4)
        while (off + 8 <= n) {
            std::uint32_t clen = RdU32(d + off);
            std::uint32_t ctype = RdU32(d + off + 4);
            std::size_t cstart = off + 8;
            if (cstart + clen > n) break;
            if (ctype == 0x4E4F534A)        jsonText.assign((const char*)d + cstart, clen);   // "JSON"
            else if (ctype == 0x004E4942)   glbBin.assign(d + cstart, d + cstart + clen);       // "BIN\0"
            off = cstart + clen + ((clen % 4) ? (4 - clen % 4) : 0);
        }
    } else {
        jsonText = bytes;
    }
    if (jsonText.empty()) return fail();

    JParser jp(jsonText);
    JVal root = jp.Parse();
    if (!jp.ok || root.type != JVal::Obj) return fail();

    std::string dir;
    { std::size_t s = path.find_last_of("/\\"); if (s != std::string::npos) dir = path.substr(0, s + 1); }

    // ---- Resolve buffers (base64 data URI, external .bin, or the GLB BIN chunk) ----
    std::vector<std::vector<std::uint8_t>> buffers;
    if (const JVal* bufs = root.Find("buffers")) {
        for (const JVal& b : bufs->arr) {
            std::vector<std::uint8_t> data;
            const JVal* uri = b.Find("uri");
            if (uri && uri->type == JVal::Str) {
                const std::string& u = uri->str;
                const std::string mark = "base64,";
                std::size_t bp = u.find(mark);
                if (u.rfind("data:", 0) == 0 && bp != std::string::npos)
                    Base64Decode(u.substr(bp + mark.size()), data);
                else {   // external file relative to the .gltf
                    std::ifstream bf(dir + u, std::ios::binary);
                    if (bf) data.assign((std::istreambuf_iterator<char>(bf)), std::istreambuf_iterator<char>());
                }
            } else {
                data = glbBin;   // bufferless entry in a .glb -> the BIN chunk
            }
            buffers.push_back(std::move(data));
        }
    }

    const JVal* views = root.Find("bufferViews");
    const JVal* accs  = root.Find("accessors");
    const JVal* meshes = root.Find("meshes");
    if (!views || !accs || !meshes) return fail();

    auto compSize = [](int ct) -> int {
        switch (ct) { case 5120: case 5121: return 1; case 5122: case 5123: return 2;
                      case 5125: case 5126: return 4; default: return 4; }
    };
    auto typeComps = [](const std::string& t) -> int {
        if (t == "SCALAR") return 1; if (t == "VEC2") return 2; if (t == "VEC3") return 3;
        if (t == "VEC4") return 4; if (t == "MAT4") return 16; return 1;
    };

    // Read accessor `ai` as a flat array of floats (count*components). Handles the
    // common integer index types for `indices` too (returned as floats).
    auto readAccessor = [&](int ai, int& outComps, int& outCount) -> std::vector<float> {
        std::vector<float> out;
        if (ai < 0 || ai >= (int)accs->arr.size()) return out;
        const JVal& a = accs->arr[ai];
        int viIdx = a.Find("bufferView") ? a.Find("bufferView")->Int(-1) : -1;
        int ct = a.Find("componentType") ? a.Find("componentType")->Int(5126) : 5126;
        int count = a.Find("count") ? a.Find("count")->Int(0) : 0;
        int comps = typeComps(a.Find("type") ? a.Find("type")->Text() : "SCALAR");
        int aoff = a.Find("byteOffset") ? a.Find("byteOffset")->Int(0) : 0;
        outComps = comps; outCount = count;
        if (viIdx < 0 || viIdx >= (int)views->arr.size()) return out;
        const JVal& v = views->arr[viIdx];
        int buf = v.Find("buffer") ? v.Find("buffer")->Int(-1) : -1;
        int voff = v.Find("byteOffset") ? v.Find("byteOffset")->Int(0) : 0;
        int stride = v.Find("byteStride") ? v.Find("byteStride")->Int(0) : 0;
        if (buf < 0 || buf >= (int)buffers.size()) return out;
        const std::vector<std::uint8_t>& data = buffers[buf];
        int csz = compSize(ct);
        if (stride == 0) stride = comps * csz;   // tightly packed
        out.reserve((std::size_t)count * comps);
        for (int i = 0; i < count; ++i) {
            std::size_t base = (std::size_t)voff + aoff + (std::size_t)i * stride;
            for (int c = 0; c < comps; ++c) {
                std::size_t at = base + (std::size_t)c * csz;
                if (at + csz > data.size()) { out.push_back(0.0f); continue; }
                const std::uint8_t* d = data.data() + at;
                float fv = 0.0f;
                switch (ct) {
                    case 5126: { float t; std::memcpy(&t, d, 4); fv = t; break; }            // float
                    case 5125: { std::uint32_t t; std::memcpy(&t, d, 4); fv = (float)t; break; } // uint
                    case 5123: { std::uint16_t t; std::memcpy(&t, d, 2); fv = (float)t; break; } // ushort
                    case 5121: fv = (float)d[0]; break;                                      // ubyte
                    case 5122: { std::int16_t t; std::memcpy(&t, d, 2); fv = (float)t; break; }  // short
                    case 5120: fv = (float)(std::int8_t)d[0]; break;                         // byte
                    default: break;
                }
                out.push_back(fv);
            }
        }
        return out;
    };

    // ---- Merge every primitive of every mesh ----
    for (const JVal& m : meshes->arr) {
        const JVal* prims = m.Find("primitives");
        if (!prims) continue;
        for (const JVal& prim : prims->arr) {
            const JVal* attr = prim.Find("attributes");
            if (!attr) continue;
            int base = (int)mesh.vertices.size();
            int pc = 0, pn = 0;
            if (const JVal* P = attr->Find("POSITION")) {
                auto pos = readAccessor(P->Int(-1), pc, pn);
                for (int i = 0; i < pn; ++i)
                    mesh.vertices.push_back(Vec3{pos[i*3+0], pos[i*3+1], pos[i*3+2]});
            }
            if (const JVal* N = attr->Find("NORMAL")) {
                int nc = 0, nn = 0; auto nor = readAccessor(N->Int(-1), nc, nn);
                for (int i = 0; i < nn; ++i)
                    mesh.normals.push_back(Vec3{nor[i*3+0], nor[i*3+1], nor[i*3+2]});
            }
            if (const JVal* T = attr->Find("TEXCOORD_0")) {
                int tc = 0, tn = 0; auto uv = readAccessor(T->Int(-1), tc, tn);
                for (int i = 0; i < tn; ++i)
                    mesh.uvs.push_back(Vec2{uv[i*2+0], uv[i*2+1]});
            }
            if (const JVal* I = prim.Find("indices")) {
                int ic = 0, in = 0; auto idx = readAccessor(I->Int(-1), ic, in);
                for (int i = 0; i < in; ++i) mesh.triangles.push_back(base + (int)(idx[i] + 0.5f));
            } else {   // non-indexed: sequential triangles
                for (int i = 0; i + 2 < pn; i += 3) {
                    mesh.triangles.push_back(base + i);
                    mesh.triangles.push_back(base + i + 1);
                    mesh.triangles.push_back(base + i + 2);
                }
            }
        }
    }

    // Keep attribute arrays consistent (only if fully present).
    if (mesh.normals.size() != mesh.vertices.size()) mesh.normals.clear();
    if (mesh.uvs.size()     != mesh.vertices.size()) mesh.uvs.clear();
    if (ok) *ok = !mesh.vertices.empty();
    return mesh;
}

} // namespace okay
