// Our own model readers: OBJ (+MTL), STL (ascii + binary), PLY (ascii + binary).
// Every reader works on an in-memory buffer, bounds-checks every read, and fails
// with a one-line message on malformed input. See okay/Render/ModelData.hpp.
#include "FileOpen.hpp"
#include "NumParse.hpp"
#include "okay/Render/ModelData.hpp"
#include "PolygonTriangulate.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace okay {

namespace {

constexpr std::size_t kMaxFileBytes = 512u * 1024u * 1024u;   // refuse anything bigger than this
constexpr std::size_t kMaxMtlBytes  = 16u * 1024u * 1024u;

bool Fail(std::string* err, const char* msg) { if (err) *err = msg; return false; }

bool ReadAllBytes(const std::string& path, std::size_t cap, std::string& out, std::string* err) {
    std::FILE* fp = detail::OpenRead(path);
    if (!fp) return Fail(err, "cannot open file");
    out.clear();
    char buf[65536];
    for (;;) {
        std::size_t n = std::fread(buf, 1, sizeof(buf), fp);
        if (n == 0) break;
        if (out.size() + n > cap) { std::fclose(fp); out.clear(); return Fail(err, "file too large"); }
        out.append(buf, n);
    }
    std::fclose(fp);
    return true;
}

std::string LowerStr(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

inline bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }
inline bool Finite(float v) { return std::isfinite(v); }
inline bool Finite3(const Vec3& v) { return Finite(v.x) && Finite(v.y) && Finite(v.z); }

// ============================================================================
// OBJ + MTL
// ============================================================================

struct ObjCorner { int p, t, n; };   // 0-based; t / n are -1 when absent

std::string Trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && IsSpace(s[a])) ++a;
    while (b > a && IsSpace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::string ResolveTexturePath(std::string tp, const std::string& baseDir) {
    for (char& c : tp) if (c == '\\') c = '/';
    const bool abs = tp.size() > 1 && (tp[0] == '/' || tp[1] == ':');
    if (abs) return {};   // an absolute path would point the engine at any file on the machine (#753): drop it
    // Stay inside the model's own directory tree: an untrusted model must not be able to point the
    // engine at any file on the machine ("map_Kd ../../../../etc/passwd" used to be kept verbatim).
    // The ".."s are resolved here; one that climbs out of the model's directory drops the texture.
    std::vector<std::string> parts;
    std::size_t i = 0;
    while (i < tp.size()) {
        std::size_t a = i;
        while (a < tp.size() && tp[a] != '/') ++a;   // one path component
        const std::string comp = tp.substr(i, a - i);
        i = a + 1;
        if (comp.empty() || comp == ".") continue;
        if (comp == "..") {
            if (parts.empty()) return {};
            parts.pop_back();
            continue;
        }
        parts.push_back(comp);
    }
    std::string out;
    for (const std::string& p : parts) { if (!out.empty()) out += '/'; out += p; }
    return baseDir + out;
}

// The file name of a texture statement ("map_Kd -s 1 1 1 wood.png"): skip the
// "-option value..." prefix, the rest of the line is the name.
std::string MtlTextureName(const std::string& rest) {
    std::size_t i = 0;
    auto skipWs = [&] { while (i < rest.size() && IsSpace(rest[i])) ++i; };
    auto word = [&]() { std::size_t a = i; while (i < rest.size() && !IsSpace(rest[i])) ++i; return rest.substr(a, i - a); };
    auto isNumber = [](const std::string& w) {
        if (w.empty()) return false;
        char* e = nullptr; detail::StrToD(w.c_str(), &e); return e && *e == '\0';
    };
    for (;;) {
        skipWs();
        if (i >= rest.size() || rest[i] != '-') break;
        std::size_t save = i;
        std::string opt = LowerStr(word());
        int arity = 1;
        if (opt == "-mm") arity = 2;
        else if (opt == "-o" || opt == "-s" || opt == "-t") arity = 3;
        for (int k = 0; k < arity; ++k) {
            skipWs();
            std::size_t before = i;
            std::string w = word();
            if (!isNumber(w)) { i = before; break; }   // o/s/t may give fewer numbers
        }
        if (i == save) break;
    }
    skipWs();
    return Trim(rest.substr(i));
}

void ParseMtl(const std::string& text, const std::string& baseDir, std::vector<ImportedMaterial>& mats) {
    ImportedMaterial* cur = nullptr;
    std::size_t p = 0;
    while (p < text.size()) {
        std::size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        std::string line = Trim(text.substr(p, e - p));
        p = e + 1;
        if (line.empty() || line[0] == '#') continue;
        std::size_t sp = 0; while (sp < line.size() && !IsSpace(line[sp])) ++sp;
        const std::string key = LowerStr(line.substr(0, sp));
        const std::string rest = Trim(line.substr(sp));
        if (key == "newmtl") { mats.emplace_back(); cur = &mats.back(); cur->name = rest; continue; }
        if (!cur) continue;
        auto num3 = [&](float& a, float& b, float& c) {
            const char* s = rest.c_str(); char* q = nullptr;
            a = detail::StrToF(s, &q); if (q == s) return false; s = q;
            b = detail::StrToF(s, &q); if (q == s) { b = a; c = a; return Finite(a); }   // "Kd 0.5" = grey
            s = q; c = detail::StrToF(s, &q); if (q == s) return false;
            return Finite(a) && Finite(b) && Finite(c);
        };
        if (key == "kd")      { float r, g, b; if (num3(r, g, b)) { cur->hasDiffuse = true; cur->diffuse = Color(r, g, b, 1.0f); } }
        else if (key == "ke") { float r, g, b; if (num3(r, g, b)) { cur->hasEmissive = true; cur->emissive = Color(r, g, b, 1.0f); } }
        else if (key == "ns") { char* q = nullptr; float v = detail::StrToF(rest.c_str(), &q); if (q != rest.c_str() && Finite(v)) cur->shininess = v; }
        else if (key == "pm") { char* q = nullptr; float v = detail::StrToF(rest.c_str(), &q); if (q != rest.c_str() && Finite(v)) cur->metallic = v; }
        else if (key == "pr") { char* q = nullptr; float v = detail::StrToF(rest.c_str(), &q); if (q != rest.c_str() && Finite(v)) cur->roughness = v; }
        else if (key == "map_kd") { std::string t = MtlTextureName(rest); if (!t.empty()) cur->texture = ResolveTexturePath(t, baseDir); }
        else if (key == "map_bump" || key == "bump" || key == "norm") {
            std::string t = MtlTextureName(rest);
            if (!t.empty() && (cur->normalMap.empty() || key == "norm")) cur->normalMap = ResolveTexturePath(t, baseDir);
        }
        else if (key == "map_ks") { std::string t = MtlTextureName(rest); if (!t.empty()) cur->specularMap = ResolveTexturePath(t, baseDir); }
        else if (key == "map_ns") { std::string t = MtlTextureName(rest); if (!t.empty() && cur->specularMap.empty()) cur->specularMap = ResolveTexturePath(t, baseDir); }
    }
}

} // namespace

bool ReadObjBuffer(const std::string& data, const std::string& baseDir, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    out.backend = "own";
    if (data.size() > kMaxFileBytes) return Fail(err, "file too large");

    std::vector<Vec3> pos, nrm; std::vector<Vec2> uv;
    std::vector<ObjCorner> corners;          // all face corners, back to back
    std::vector<std::uint32_t> faceStart;    // corner offset of each face
    std::vector<int> faceMat;                // material slot of each face (-1 none)
    std::vector<std::string> mtlNames;       // slot -> usemtl name
    std::vector<std::string> mtllibs;
    int curMat = -1;
    bool badIndex = false;

    const char* p = data.c_str();
    const char* end = p + data.size();
    auto readF = [&](const char*& c, bool& bad) -> float {
        while (c < end && (*c == ' ' || *c == '\t' || *c == '\r')) ++c;
        if (c >= end || *c == '\n') return 0.0f;
        char* q = nullptr; float v = detail::StrToF(c, &q);
        if (q == c) { bad = true; ++c; return 0.0f; }   // junk: always advance
        c = q; if (!Finite(v)) bad = true; return v;
    };
    bool badNumber = false;
    while (p < end) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) ++p;
        if (p < end) {
            if (p[0] == 'v' && p + 1 < end && (p[1] == ' ' || p[1] == '\t')) {
                p += 2; Vec3 v; v.x = readF(p, badNumber); v.y = readF(p, badNumber); v.z = readF(p, badNumber); pos.push_back(v);
            } else if (p[0] == 'v' && p + 2 < end && p[1] == 't' && (p[2] == ' ' || p[2] == '\t')) {
                p += 3; Vec2 t; t.x = readF(p, badNumber); t.y = readF(p, badNumber); uv.push_back(t);
            } else if (p[0] == 'v' && p + 2 < end && p[1] == 'n' && (p[2] == ' ' || p[2] == '\t')) {
                p += 3; Vec3 n; n.x = readF(p, badNumber); n.y = readF(p, badNumber); n.z = readF(p, badNumber); nrm.push_back(n);
            } else if (p[0] == 'f' && p + 1 < end && (p[1] == ' ' || p[1] == '\t')) {
                p += 2;
                const std::size_t first = corners.size();
                for (;;) {
                    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) ++p;
                    if (p >= end || *p == '\n') break;
                    char* q = nullptr;
                    long vi = std::strtol(p, &q, 10); if (q == p) break; p = q;
                    long ti = 0, ni = 0;
                    if (p < end && *p == '/') {
                        ++p;
                        if (p < end && *p == '/') { ++p; ni = std::strtol(p, &q, 10); p = q; }
                        else {
                            ti = std::strtol(p, &q, 10); p = q;
                            if (p < end && *p == '/') { ++p; ni = std::strtol(p, &q, 10); p = q; }
                        }
                    }
                    if (vi < 0) vi = (long)pos.size() + vi + 1;
                    if (ti < 0) ti = (long)uv.size() + ti + 1;
                    if (ni < 0) ni = (long)nrm.size() + ni + 1;
                    if (vi < 1 || vi > INT32_MAX || ti < 0 || ti > INT32_MAX || ni < 0 || ni > INT32_MAX) badIndex = true;
                    corners.push_back({(int)vi - 1, (int)ti - 1, (int)ni - 1});
                }
                if (corners.size() - first >= 3) { faceStart.push_back((std::uint32_t)first); faceMat.push_back(curMat); }
                else corners.resize(first);
            } else if (end - p >= 7 && std::strncmp(p, "usemtl", 6) == 0 && (p[6] == ' ' || p[6] == '\t')) {
                p += 7; const char* s = p;
                while (p < end && *p != '\n' && *p != '\r') ++p;
                std::string name = Trim(std::string(s, p));
                auto it = std::find(mtlNames.begin(), mtlNames.end(), name);
                if (it == mtlNames.end()) { mtlNames.push_back(name); curMat = (int)mtlNames.size() - 1; }
                else curMat = (int)(it - mtlNames.begin());
            } else if (end - p >= 7 && std::strncmp(p, "mtllib", 6) == 0 && (p[6] == ' ' || p[6] == '\t')) {
                p += 7; const char* s = p;
                while (p < end && *p != '\n' && *p != '\r') ++p;
                std::string name = Trim(std::string(s, p));
                if (!name.empty()) mtllibs.push_back(name);
            }
        }
        while (p < end && *p != '\n') ++p;
        if (p < end) ++p;
    }
    if (badNumber) return Fail(err, "bad or non-finite number");
    if (faceStart.empty()) return Fail(err, "no faces");
    for (const ObjCorner& c : corners) {
        if (c.p < 0 || c.p >= (int)pos.size()) badIndex = true;
        if (c.t >= (int)uv.size() || c.n >= (int)nrm.size()) badIndex = true;
    }
    if (badIndex) return Fail(err, "face index out of range");

    // Materials from the .mtl file(s); usemtl names map onto them.
    std::vector<ImportedMaterial> mtl;
    for (const std::string& lib : mtllibs) {
        std::string bytes;
        std::string name = lib; for (char& c : name) if (c == '\\') c = '/';
        if (ReadAllBytes(baseDir + name, kMaxMtlBytes, bytes, nullptr)) { ParseMtl(bytes, baseDir, mtl); continue; }
        // A name with spaces may list several files.
        std::size_t a = 0;
        while (a < lib.size()) {
            while (a < lib.size() && IsSpace(lib[a])) ++a;
            std::size_t b = a; while (b < lib.size() && !IsSpace(lib[b])) ++b;
            if (b > a && ReadAllBytes(baseDir + lib.substr(a, b - a), kMaxMtlBytes, bytes, nullptr)) ParseMtl(bytes, baseDir, mtl);
            a = b;
        }
    }
    std::vector<int> slotToMat(mtlNames.size(), -1);
    for (std::size_t s = 0; s < mtlNames.size(); ++s) {
        int found = -1;
        for (int i = (int)mtl.size() - 1; i >= 0; --i) if (mtl[(std::size_t)i].name == mtlNames[s]) { found = i; break; }
        if (found < 0) { ImportedMaterial m; m.name = mtlNames[s]; mtl.push_back(m); found = (int)mtl.size() - 1; }
        slotToMat[s] = found;
    }
    out.materials = std::move(mtl);

    // One part per material slot, in order of first use.
    std::vector<int> order; std::vector<int> slotPart(mtlNames.size() + 1, -1);
    for (int m : faceMat) { int k = m + 1; if (slotPart[(std::size_t)k] < 0) { slotPart[(std::size_t)k] = (int)order.size(); order.push_back(m); } }
    out.parts.resize(order.size());
    struct CKey { int p, t, n; bool operator==(const CKey& o) const { return p == o.p && t == o.t && n == o.n; } };
    struct CKeyHash {
        std::size_t operator()(const CKey& k) const {
            std::size_t h = 1469598103934665603ull;
            auto mix = [&](long v) { h ^= (std::size_t)(v + 1); h *= 1099511628211ull; };
            mix(k.p); mix(k.t); mix(k.n); return h;
        }
    };
    for (std::size_t pi = 0; pi < order.size(); ++pi) {
        ImportedPart& part = out.parts[pi];
        part.node = 0;
        part.material = order[pi] >= 0 ? slotToMat[(std::size_t)order[pi]] : -1;
        if (order[pi] >= 0) part.mesh.name = mtlNames[(std::size_t)order[pi]];
        bool allUV = true, allN = true;
        for (std::size_t fi = 0; fi < faceStart.size(); ++fi) {
            if (faceMat[fi] != order[pi]) continue;
            const std::size_t b = faceStart[fi], e = fi + 1 < faceStart.size() ? faceStart[fi + 1] : corners.size();
            for (std::size_t c = b; c < e; ++c) { if (corners[c].t < 0) allUV = false; if (corners[c].n < 0) allN = false; }
        }
        std::unordered_map<CKey, int, CKeyHash> remap;
        std::vector<int> idx;
        std::vector<Vec3> ring;
        std::vector<std::array<int, 3>> tris;
        for (std::size_t fi = 0; fi < faceStart.size(); ++fi) {
            if (faceMat[fi] != order[pi]) continue;
            const std::size_t b = faceStart[fi], e = fi + 1 < faceStart.size() ? faceStart[fi + 1] : corners.size();
            idx.clear();
            for (std::size_t c = b; c < e; ++c) {
                const ObjCorner& oc = corners[c];
                CKey key{oc.p, allUV ? oc.t : -1, allN ? oc.n : -1};
                auto it = remap.find(key);
                if (it == remap.end()) {
                    int ni = (int)part.mesh.vertices.size();
                    part.mesh.vertices.push_back(pos[(std::size_t)oc.p]);
                    if (allUV) part.mesh.uvs.push_back(uv[(std::size_t)oc.t]);
                    if (allN)  part.mesh.normals.push_back(nrm[(std::size_t)oc.n]);
                    it = remap.emplace(key, ni).first;
                }
                idx.push_back(it->second);
            }
            if (idx.size() == 3) { part.mesh.triangles.insert(part.mesh.triangles.end(), {idx[0], idx[1], idx[2]}); continue; }
            // quads and larger: convex polygons fan from the first corner; concave ones are ear clipped (a fan covers the notch)
            ring.resize(idx.size());
            for (std::size_t i = 0; i < idx.size(); ++i) ring[i] = part.mesh.vertices[(std::size_t)idx[i]];
            detail::TriangulatePolygon(ring, tris);
            for (const auto& t : tris) part.mesh.triangles.insert(part.mesh.triangles.end(), {idx[(std::size_t)t[0]], idx[(std::size_t)t[1]], idx[(std::size_t)t[2]]});
        }
    }
    return true;
}

namespace {

// ============================================================================
// STL
// ============================================================================

std::uint32_t LE32(const unsigned char* b) { return (std::uint32_t)b[0] | ((std::uint32_t)b[1] << 8) | ((std::uint32_t)b[2] << 16) | ((std::uint32_t)b[3] << 24); }
float LEFloat(const unsigned char* b) { std::uint32_t u = LE32(b); float f; std::memcpy(&f, &u, 4); return f; }

struct StlWelder {
    struct Key { std::uint32_t v[6]; bool operator==(const Key& o) const { return std::memcmp(v, o.v, sizeof v) == 0; } };
    struct Hash { std::size_t operator()(const Key& k) const { std::size_t h = 1469598103934665603ull; for (auto x : k.v) { h ^= x; h *= 1099511628211ull; } return h; } };
    std::unordered_map<Key, int, Hash> map;
    Mesh* mesh = nullptr;
    int Add(const Vec3& p, const Vec3& n) {
        Key k; float f[6] = {p.x + 0.0f, p.y + 0.0f, p.z + 0.0f, n.x + 0.0f, n.y + 0.0f, n.z + 0.0f};   // +0 folds -0 into 0
        for (int i = 0; i < 6; ++i) std::memcpy(&k.v[i], &f[i], 4);
        auto it = map.find(k);
        if (it != map.end()) return it->second;
        int id = (int)mesh->vertices.size();
        mesh->vertices.push_back(p); mesh->normals.push_back(n);
        map.emplace(k, id); return id;
    }
};

// A facet's normal: the one in the file when usable, else from the winding.
Vec3 FacetNormal(const Vec3& fileN, const Vec3& a, const Vec3& b, const Vec3& c) {
    float m = fileN.Magnitude();
    if (Finite3(fileN) && m > 1e-12f) return fileN * (1.0f / m);
    Vec3 g = Vec3::Cross(b - a, c - a);
    float gm = g.Magnitude();
    return gm > 1e-20f ? g * (1.0f / gm) : Vec3{0, 1, 0};
}

} // namespace

bool ReadStlBuffer(const std::string& bytes, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    out.backend = "own";
    const std::size_t n = bytes.size();
    if (n > kMaxFileBytes) return Fail(err, "file too large");
    if (n < 15) return Fail(err, "file too small to be an STL");
    const unsigned char* d = reinterpret_cast<const unsigned char*>(bytes.data());

    const std::uint64_t binCount = n >= 84 ? LE32(d + 80) : 0;
    const std::uint64_t binSize = 84 + binCount * 50;
    bool binary = false;
    if (n >= 84 && binSize == n) binary = true;
    else {
        std::size_t i = 0; while (i < n && IsSpace(bytes[i])) ++i;
        const bool solid = n - i >= 5 && LowerStr(bytes.substr(i, 5)) == "solid";
        if (!solid) {
            if (n >= 84 && binSize <= n) binary = true;   // padded binary
            else return Fail(err, "not an STL (no 'solid' and the size does not match a binary file)");
        }
    }

    if (binary) {
        if (binCount == 0) return Fail(err, "no triangles");
        out.parts.resize(1);
        ImportedPart& part = out.parts[0];
        StlWelder w; w.mesh = &part.mesh;
        w.map.reserve((std::size_t)std::min<std::uint64_t>(binCount * 3, 1u << 22));
        for (std::uint64_t t = 0; t < binCount; ++t) {
            const unsigned char* r = d + 84 + t * 50;
            Vec3 fn{LEFloat(r), LEFloat(r + 4), LEFloat(r + 8)};
            Vec3 v[3];
            for (int k = 0; k < 3; ++k) v[k] = Vec3{LEFloat(r + 12 + 12 * k), LEFloat(r + 16 + 12 * k), LEFloat(r + 20 + 12 * k)};
            if (!Finite3(v[0]) || !Finite3(v[1]) || !Finite3(v[2])) return Fail(err, "non-finite vertex");
            Vec3 nn = FacetNormal(fn, v[0], v[1], v[2]);
            int a = w.Add(v[0], nn), b = w.Add(v[1], nn), c = w.Add(v[2], nn);
            part.mesh.triangles.insert(part.mesh.triangles.end(), {a, b, c});
        }
        return true;
    }

    // ASCII
    const char* p = bytes.c_str();
    const char* end = p + n;
    auto word = [&](std::string& w) -> bool {
        while (p < end && IsSpace(*p)) ++p;
        if (p >= end) return false;
        const char* s = p;
        while (p < end && !IsSpace(*p)) ++p;
        w.assign(s, (std::size_t)std::min<std::ptrdiff_t>(p - s, 32));
        for (char& c : w) c = (char)std::tolower((unsigned char)c);
        return true;
    };
    auto num = [&](float& v) -> bool {
        while (p < end && IsSpace(*p)) ++p;
        if (p >= end) return false;
        char* q = nullptr; v = detail::StrToF(p, &q);
        if (q == p || !Finite(v)) return false;
        p = q; return true;
    };
    std::vector<ImportedPart> solids;
    StlWelder w;
    auto openSolid = [&]() { solids.emplace_back(); w.map.clear(); w.mesh = &solids.back().mesh; };
    std::string tok;
    bool inSolid = false;
    while (word(tok)) {
        if (tok == "solid") {
            std::string name;
            while (p < end && *p != '\n' && *p != '\r') name += *p++;
            if (inSolid && !solids.empty() && solids.back().mesh.triangles.empty()) solids.pop_back();
            openSolid(); inSolid = true;
            solids.back().mesh.name = Trim(name);
        } else if (tok == "endsolid") {
            while (p < end && *p != '\n' && *p != '\r') ++p;
            inSolid = false;
        } else if (tok == "facet") {
            if (!inSolid) { openSolid(); inSolid = true; }
            Vec3 fn{0, 0, 0}; Vec3 v[3]; int nv = 0;
            if (!word(tok)) return Fail(err, "truncated facet");
            if (tok == "normal") {
                if (!num(fn.x) || !num(fn.y) || !num(fn.z)) return Fail(err, "bad facet normal");
            } else return Fail(err, "facet without normal");
            for (;;) {
                if (!word(tok)) return Fail(err, "truncated facet");
                if (tok == "vertex") {
                    if (nv >= 3) return Fail(err, "facet with more than 3 vertices");
                    if (!num(v[nv].x) || !num(v[nv].y) || !num(v[nv].z)) return Fail(err, "bad vertex");
                    ++nv;
                } else if (tok == "endfacet") break;
                else if (tok == "outer" || tok == "loop" || tok == "endloop") continue;
                else return Fail(err, "unexpected word inside facet");
            }
            if (nv != 3) return Fail(err, "facet without 3 vertices");
            Vec3 nn = FacetNormal(fn, v[0], v[1], v[2]);
            int a = w.Add(v[0], nn), b = w.Add(v[1], nn), c = w.Add(v[2], nn);
            solids.back().mesh.triangles.insert(solids.back().mesh.triangles.end(), {a, b, c});
        } else return Fail(err, "unexpected word in STL");
    }
    for (ImportedPart& s : solids) if (!s.mesh.triangles.empty()) out.parts.push_back(std::move(s));
    if (out.parts.empty()) return Fail(err, "no triangles");
    return true;
}

namespace {

// ============================================================================
// PLY
// ============================================================================

enum class PlyType { None, I8, U8, I16, U16, I32, U32, F32, F64 };

PlyType PlyTypeFromName(const std::string& s) {
    if (s == "char" || s == "int8") return PlyType::I8;
    if (s == "uchar" || s == "uint8") return PlyType::U8;
    if (s == "short" || s == "int16") return PlyType::I16;
    if (s == "ushort" || s == "uint16") return PlyType::U16;
    if (s == "int" || s == "int32") return PlyType::I32;
    if (s == "uint" || s == "uint32") return PlyType::U32;
    if (s == "float" || s == "float32") return PlyType::F32;
    if (s == "double" || s == "float64") return PlyType::F64;
    return PlyType::None;
}
int PlySize(PlyType t) {
    switch (t) { case PlyType::I8: case PlyType::U8: return 1; case PlyType::I16: case PlyType::U16: return 2;
                 case PlyType::I32: case PlyType::U32: case PlyType::F32: return 4; case PlyType::F64: return 8; default: return 0; }
}

struct PlyProp { std::string name; PlyType type = PlyType::None; bool list = false; PlyType countType = PlyType::None; };
struct PlyElement { std::string name; std::uint64_t count = 0; std::vector<PlyProp> props; };

enum class PlyFormat { Ascii, Little, Big };

struct PlyStream {
    const char* p; const char* end; PlyFormat fmt;
    bool Read(PlyType t, double& v) {
        if (fmt == PlyFormat::Ascii) {
            while (p < end && IsSpace(*p)) ++p;
            if (p >= end) return false;
            char* q = nullptr; v = detail::StrToD(p, &q);
            if (q == p) return false;
            p = q; return true;
        }
        const int sz = PlySize(t);
        if (end - p < sz) return false;
        unsigned char b[8];
        for (int i = 0; i < sz; ++i) b[i] = (unsigned char)p[fmt == PlyFormat::Little ? i : sz - 1 - i];
        p += sz;
        switch (t) {
            case PlyType::I8:  v = (std::int8_t)b[0]; break;
            case PlyType::U8:  v = b[0]; break;
            case PlyType::I16: v = (std::int16_t)(b[0] | (b[1] << 8)); break;
            case PlyType::U16: v = (std::uint16_t)(b[0] | (b[1] << 8)); break;
            case PlyType::I32: { std::uint32_t u = LE32(b); std::int32_t s; std::memcpy(&s, &u, 4); v = s; break; }
            case PlyType::U32: v = LE32(b); break;
            case PlyType::F32: { std::uint32_t u = LE32(b); float f; std::memcpy(&f, &u, 4); v = f; break; }
            case PlyType::F64: { std::uint64_t u = 0; for (int i = 7; i >= 0; --i) u = (u << 8) | b[i]; double f; std::memcpy(&f, &u, 8); v = f; break; }
            default: return false;
        }
        return true;
    }
};

} // namespace

bool ReadPlyBuffer(const std::string& bytes, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    out.backend = "own";
    if (bytes.size() > kMaxFileBytes) return Fail(err, "file too large");
    const char* base = bytes.c_str();
    const char* end = base + bytes.size();
    const char* p = base;

    // ---- header ----
    auto line = [&](std::string& l) -> bool {
        if (p >= end) return false;
        const char* s = p;
        while (p < end && *p != '\n') ++p;
        l.assign(s, p);
        if (p < end) ++p;
        while (!l.empty() && l.back() == '\r') l.pop_back();
        return true;
    };
    auto words = [](const std::string& l) {
        std::vector<std::string> w; std::size_t i = 0;
        while (i < l.size()) { while (i < l.size() && IsSpace(l[i])) ++i; std::size_t a = i; while (i < l.size() && !IsSpace(l[i])) ++i; if (i > a) w.push_back(l.substr(a, i - a)); }
        return w;
    };
    std::string l;
    if (!line(l) || Trim(l) != "ply") return Fail(err, "not a PLY (missing 'ply' magic)");
    PlyFormat fmt = PlyFormat::Ascii; bool haveFormat = false, haveEnd = false;
    std::vector<PlyElement> elems;
    int headerLines = 0;
    while (line(l)) {
        if (++headerLines > 4096) return Fail(err, "PLY header too long");
        std::vector<std::string> w = words(l);
        if (w.empty()) continue;
        if (w[0] == "end_header") { haveEnd = true; break; }
        if (w[0] == "comment" || w[0] == "obj_info") continue;
        if (w[0] == "format") {
            if (w.size() < 2) return Fail(err, "bad PLY format line");
            if (w[1] == "ascii") fmt = PlyFormat::Ascii;
            else if (w[1] == "binary_little_endian") fmt = PlyFormat::Little;
            else if (w[1] == "binary_big_endian") fmt = PlyFormat::Big;
            else return Fail(err, "unknown PLY format");
            haveFormat = true;
        } else if (w[0] == "element") {
            if (w.size() != 3) return Fail(err, "bad PLY element line");
            if (elems.size() >= 64) return Fail(err, "too many PLY elements");
            char* q = nullptr; unsigned long long c = std::strtoull(w[2].c_str(), &q, 10);
            if (!q || *q != '\0' || w[2][0] == '-') return Fail(err, "bad PLY element count");
            // A second `element vertex` is not another slice of one vertex list.
            // Reading it used to keep the first block's normal bytes on the second
            // block's vertices, or concatenate a different property subset so
            // normals and UVs no longer lined up with positions. Refuse it here,
            // before any vertex is written.
            if (w[1] == "vertex")
                for (const PlyElement& prev : elems)
                    if (prev.name == "vertex") return Fail(err, "PLY declares more than one vertex element");
            PlyElement e; e.name = w[1]; e.count = c; elems.push_back(std::move(e));
        } else if (w[0] == "property") {
            if (elems.empty()) return Fail(err, "PLY property before any element");
            if (elems.back().props.size() >= 64) return Fail(err, "too many PLY properties");
            PlyProp pr;
            if (w.size() == 5 && w[1] == "list") {
                pr.list = true; pr.countType = PlyTypeFromName(w[2]); pr.type = PlyTypeFromName(w[3]); pr.name = w[4];
                if (pr.countType == PlyType::None || pr.type == PlyType::None) return Fail(err, "unknown PLY list type");
            } else if (w.size() == 3) {
                pr.type = PlyTypeFromName(w[1]); pr.name = w[2];
                if (pr.type == PlyType::None) return Fail(err, "unknown PLY property type");
            } else return Fail(err, "bad PLY property line");
            elems.back().props.push_back(std::move(pr));
        }   // any other header line is ignored, as an unknown keyword (some exporters write free text without "comment")
    }
    if (!haveFormat) return Fail(err, "PLY has no format line");
    if (!haveEnd) return Fail(err, "PLY header never ends");

    PlyStream st{p, end, fmt};
    Mesh mesh;
    std::vector<std::uint32_t> faceIdx;   // every polygon's corners back to back, validated at the end
    std::vector<std::uint32_t> faceLen;   // corners of each polygon
    bool sawVertex = false, hasN = false, hasUV = false;
    constexpr std::size_t kMaxFaceVerts = 65535;

    for (const PlyElement& el : elems) {
        const std::uint64_t remaining = (std::uint64_t)(end - st.p);
        std::uint64_t minRec = 0;
        for (const PlyProp& pr : el.props) minRec += fmt == PlyFormat::Ascii ? 1 : (std::uint64_t)PlySize(pr.list ? pr.countType : pr.type);
        if (el.count > 0 && el.props.empty()) return Fail(err, "PLY element with no properties");
        if (el.count > 0 && minRec > 0 && el.count > remaining / minRec) return Fail(err, "PLY element count exceeds the file");
        const bool isVertex = el.name == "vertex", isFace = el.name == "face";
        int ix = -1, iy = -1, iz = -1, inx = -1, iny = -1, inz = -1, iu = -1, iv = -1, ifaceList = -1;
        if (isVertex) {
            // Header parsing rejects this first. This is the write-site guard: a
            // second block must not append positions onto normals/UVs from the first.
            if (sawVertex) return Fail(err, "PLY declares more than one vertex element");
            for (std::size_t k = 0; k < el.props.size(); ++k) {
                const std::string& n = el.props[k].name; int ki = (int)k;
                if (el.props[k].list) continue;
                if (n == "x") ix = ki; else if (n == "y") iy = ki; else if (n == "z") iz = ki;
                else if (n == "nx") inx = ki; else if (n == "ny") iny = ki; else if (n == "nz") inz = ki;
                else if (n == "s" || n == "u" || n == "texture_u" || n == "texture_s") iu = ki;
                else if (n == "t" || n == "v" || n == "texture_v" || n == "texture_t") iv = ki;
            }
            if (ix < 0 || iy < 0 || iz < 0) return Fail(err, "PLY vertices lack x, y, z");
            hasN = inx >= 0 && iny >= 0 && inz >= 0;
            hasUV = iu >= 0 && iv >= 0;
            sawVertex = true;
            mesh.vertices.reserve((std::size_t)std::min<std::uint64_t>(el.count, 1u << 20));
        }
        if (isFace) {
            for (std::size_t k = 0; k < el.props.size(); ++k)
                if (el.props[k].list && (el.props[k].name == "vertex_indices" || el.props[k].name == "vertex_index")) { ifaceList = (int)k; break; }
            if (ifaceList < 0)
                for (std::size_t k = 0; k < el.props.size(); ++k) if (el.props[k].list) { ifaceList = (int)k; break; }
        }
        std::vector<double> vals(el.props.size());
        std::vector<std::uint32_t> poly;
        for (std::uint64_t r = 0; r < el.count; ++r) {
            poly.clear();
            for (std::size_t k = 0; k < el.props.size(); ++k) {
                const PlyProp& pr = el.props[k];
                double v = 0;
                if (!pr.list) { if (!st.Read(pr.type, v)) return Fail(err, "PLY data truncated or malformed"); vals[k] = v; continue; }
                double cnt = 0;
                if (!st.Read(pr.countType, cnt)) return Fail(err, "PLY data truncated or malformed");
                if (!(cnt >= 0) || cnt > (double)kMaxFaceVerts) return Fail(err, "PLY list too long");
                const std::size_t c = (std::size_t)cnt;
                const bool keep = isFace && (int)k == ifaceList;
                for (std::size_t j = 0; j < c; ++j) {
                    if (!st.Read(pr.type, v)) return Fail(err, "PLY data truncated or malformed");
                    if (keep) {
                        if (!(v >= 0) || v > 4294967295.0 || v != std::floor(v)) return Fail(err, "PLY bad vertex index");
                        poly.push_back((std::uint32_t)v);
                    }
                }
            }
            if (isVertex) {
                Vec3 pt{(float)vals[(std::size_t)ix], (float)vals[(std::size_t)iy], (float)vals[(std::size_t)iz]};
                if (!Finite3(pt)) return Fail(err, "PLY non-finite vertex");
                mesh.vertices.push_back(pt);
                if (hasN) {
                    Vec3 nn{(float)vals[(std::size_t)inx], (float)vals[(std::size_t)iny], (float)vals[(std::size_t)inz]};
                    if (!Finite3(nn)) return Fail(err, "PLY non-finite normal");
                    mesh.normals.push_back(nn);
                }
                if (hasUV) {
                    Vec2 t{(float)vals[(std::size_t)iu], 1.0f - (float)vals[(std::size_t)iv]};
                    if (!Finite(t.x) || !Finite(t.y)) return Fail(err, "PLY non-finite UV");
                    mesh.uvs.push_back(t);
                }
            } else if (isFace && poly.size() >= 3) {
                faceLen.push_back((std::uint32_t)poly.size());
                faceIdx.insert(faceIdx.end(), poly.begin(), poly.end());
            }
        }
    }
    if (!sawVertex) return Fail(err, "PLY has no vertex element");
    if (faceIdx.empty()) return Fail(err, "PLY has no faces");
    for (std::uint32_t i : faceIdx) if (i >= mesh.vertices.size()) return Fail(err, "PLY face index out of range");
    {   // triangles: convex polygons fan from their first corner, concave ones are ear clipped
        std::vector<Vec3> ring; std::vector<std::array<int, 3>> tris;
        std::size_t at = 0;
        for (std::uint32_t len : faceLen) {
            const std::uint32_t* f = faceIdx.data() + at; at += len;
            if (len == 3) { mesh.triangles.insert(mesh.triangles.end(), {(int)f[0], (int)f[1], (int)f[2]}); continue; }
            ring.resize(len);
            for (std::uint32_t i = 0; i < len; ++i) ring[i] = mesh.vertices[f[i]];
            detail::TriangulatePolygon(ring, tris);
            for (const auto& t : tris) mesh.triangles.insert(mesh.triangles.end(), {(int)f[t[0]], (int)f[t[1]], (int)f[t[2]]});
        }
    }
    if (!hasN) mesh.ComputeSmoothNormals();   // what Assimp's GenSmoothNormals does for the same file
    // Normals cover every position (authored, or the smooth fill above). UVs are
    // all or nothing: a short list would be read as belonging to the wrong vertices.
    if (mesh.normals.size() != mesh.vertices.size()) return Fail(err, "PLY normals do not cover every vertex");
    if (!mesh.uvs.empty() && mesh.uvs.size() != mesh.vertices.size()) return Fail(err, "PLY UVs do not cover every vertex");
    out.parts.resize(1);
    out.parts[0].mesh = std::move(mesh);
    return true;
}

bool ReadObjFile(const std::string& path, ImportedModel& out, std::string* err) {
    std::string d; if (!ReadAllBytes(path, kMaxFileBytes, d, err)) return false;
    std::string dir; { std::size_t s = path.find_last_of("/\\"); if (s != std::string::npos) dir = path.substr(0, s + 1); }
    return ReadObjBuffer(d, dir, out, err);
}
bool ReadStlFile(const std::string& path, ImportedModel& out, std::string* err) {
    std::string d; if (!ReadAllBytes(path, kMaxFileBytes, d, err)) return false;
    return ReadStlBuffer(d, out, err);
}
bool ReadPlyFile(const std::string& path, ImportedModel& out, std::string* err) {
    std::string d; if (!ReadAllBytes(path, kMaxFileBytes, d, err)) return false;
    return ReadPlyBuffer(d, out, err);
}

bool PlyFileDeclaresDuplicateVertex(const std::string& path) {
    std::FILE* fp = detail::OpenRead(path);
    if (!fp) return false;
    std::string pending;
    pending.reserve(8192);
    char buf[4096];
    int vertexElems = 0;
    int headerLines = 0;
    bool sawMagic = false;
    bool done = false;
    bool duplicate = false;
    auto words = [](const std::string& l) {
        std::vector<std::string> w; std::size_t i = 0;
        while (i < l.size()) {
            while (i < l.size() && IsSpace(l[i])) ++i;
            std::size_t a = i;
            while (i < l.size() && !IsSpace(l[i])) ++i;
            if (i > a) w.push_back(l.substr(a, i - a));
        }
        return w;
    };
    auto take = [&](std::string line) {
        while (!line.empty() && line.back() == '\r') line.pop_back();
        if (!sawMagic) {
            if (Trim(line).empty()) return;
            sawMagic = true;
            if (Trim(line) != "ply") done = true;
            return;
        }
        if (++headerLines > 4096) { done = true; return; }
        std::vector<std::string> w = words(line);
        if (w.empty() || w[0] == "comment" || w[0] == "obj_info") return;
        if (w[0] == "end_header") { done = true; return; }
        if (w[0] == "element" && w.size() == 3 && w[1] == "vertex" && ++vertexElems > 1) {
            duplicate = true;
            done = true;
        }
    };
    std::size_t got = 0;
    while (!done && got < 1024u * 1024u) {
        std::size_t n = std::fread(buf, 1, sizeof(buf), fp);
        if (n == 0) break;
        got += n;
        pending.append(buf, n);
        std::size_t start = 0;
        for (;;) {
            std::size_t nl = pending.find('\n', start);
            if (nl == std::string::npos) { pending.erase(0, start); break; }
            take(pending.substr(start, nl - start));
            start = nl + 1;
            if (done) break;
        }
    }
    if (!done && !pending.empty()) take(pending);
    std::fclose(fp);
    return duplicate;
}

} // namespace okay
