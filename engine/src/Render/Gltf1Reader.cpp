// Our own glTF 1.0 reader (.gltf with asset.version 1.x, and the binary form of the KHR_binary_glTF extension).
//
// Written from the Khronos glTF 1.0 specification only: specification/1.0/README.md and its JSON schemas, and the
// KHR_binary_glTF and KHR_materials_common extension descriptions (KhronosGroup/glTF repository). No importer source was
// read (not Assimp's, not any other program's): see docs/own_gltf1_reader.md. The JSON parser and the base64 decoder are
// the engine's own (okay/Render/Gltf.hpp, which reads glTF 2.0).
//
// glTF 1.0 differs from 2.0 in ways that make a separate reader simpler than a mode in the 2.0 one: every top-level
// collection is an object keyed by string ids (not an array), a node lists several meshes, accessors have their own
// byteStride, materials are a technique plus a dictionary of values, and the binary container is a 20-byte header with
// one JSON content block and one body (not 2.0's chunks).
//
// Hostile input is the normal case: every offset, length, count and index is checked with 64-bit arithmetic before use,
// JSON nesting is limited before parsing, the node graph is walked with an explicit stack (a loop or a node with two
// parents is refused), and the vertex, triangle, node and buffer totals have budgets. External files a model names must
// lie inside its own folder (SceneRefPath.hpp).
#include "SceneRefPath.hpp"
#include "TextReaderUtil.hpp"
#include "okay/Render/Gltf.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace okay {

namespace {
namespace gltf1 {

using gltf_detail::JVal;

constexpr std::size_t   kMaxFileBytes     = 512u * 1024u * 1024u;   // the .gltf / binary file itself
constexpr std::uint64_t kMaxBufferBytes   = 512ull * 1024u * 1024u; // all buffers together (external, data URIs, body)
constexpr int           kMaxJsonDepth     = 128;                    // glTF 1.0 nests about 8 deep; anything far deeper is hostile
constexpr std::uint64_t kMaxAccessorCount = 20000000;               // elements in one accessor
constexpr std::uint64_t kMaxDecoded       = 64000000;               // accessor values decoded in the whole file (4 bytes each),
                                                                    // counted before anything is allocated: a node may list one
                                                                    // mesh many times, and each listing decodes it again
constexpr std::uint64_t kMaxVertices      = 20000000;               // whole file
constexpr std::uint64_t kMaxTriangles     = 20000000;               // whole file
constexpr std::size_t   kMaxNodes         = 100000;
constexpr std::size_t   kMaxParts         = 100000;

std::uint32_t U32(const unsigned char* p) {
    return (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) | ((std::uint32_t)p[2] << 16) | ((std::uint32_t)p[3] << 24);
}

// Deepest bracket nesting outside strings; false once it passes `limit` (the JSON parser is recursive).
bool NestingWithin(const std::string& s, int limit) {
    int depth = 0;
    bool inString = false, escaped = false;
    for (char c : s) {
        if (inString) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') inString = false;
        } else if (c == '"') inString = true;
        else if (c == '[' || c == '{') { if (++depth > limit) return false; }
        else if (c == ']' || c == '}') { if (depth > 0) --depth; }
    }
    return true;
}

// A JSON number that is a whole number in 0 .. max.
bool WholeNumber(const JVal* v, std::uint64_t max, std::uint64_t& out) {
    if (!v || v->type != JVal::Num || !std::isfinite(v->num) || v->num < 0 || v->num > (double)max) return false;
    if (std::floor(v->num) != v->num) return false;
    out = (std::uint64_t)v->num;
    return true;
}

int ComponentsOf(const std::string& type) {
    if (type == "SCALAR") return 1;
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    if (type == "VEC4") return 4;
    if (type == "MAT2") return 4;
    if (type == "MAT3") return 9;
    if (type == "MAT4") return 16;
    return 0;
}

// Bytes per component for the WebGL types the specification lists (5125, unsigned int, is accepted for indices only).
int BytesOf(std::uint64_t componentType) {
    switch (componentType) {
    case 5120: case 5121: return 1;   // byte, unsigned byte
    case 5122: case 5123: return 2;   // short, unsigned short
    case 5125: case 5126: return 4;   // unsigned int, float
    default: return 0;
    }
}

struct Accessor {
    std::vector<float> values;           // count * components, row by row (attributes)
    std::vector<std::uint32_t> indices;  // count values (an indices accessor: decoded straight to integers)
    std::uint64_t count = 0;
    int components = 0;
    std::uint64_t componentType = 0;
};

struct Reader {
    std::string err;
    std::vector<std::string> notes;
    std::string baseDir;
    const unsigned char* body = nullptr;   // the binary body of a KHR_binary_glTF file
    std::size_t bodySize = 0;
    JVal root;
    std::map<std::string, std::vector<std::uint8_t>> buffers;
    std::uint64_t bufferBytes = 0;
    std::uint64_t vertices = 0, triangles = 0, decoded = 0, primitives = 0;
    std::map<std::string, int> materialIndex;
    std::size_t linePrims = 0, unknownModePrims = 0, droppedNormals = 0, droppedUvs = 0, degenerate = 0;
    std::size_t embeddedImages = 0, refusedImages = 0;

    bool Fail(const std::string& m) { if (err.empty()) err = "glTF 1.0: " + m; return false; }
    void Note(const std::string& m) { notes.push_back("glTF 1.0: " + m); }

    // root[collection][id], or nullptr.
    const JVal* Get(const char* collection, const std::string& id) const {
        const JVal* c = root.Find(collection);
        if (!c || c->type != JVal::Obj) return nullptr;
        const JVal* v = c->Find(id);
        return v && v->type == JVal::Obj ? v : nullptr;
    }

    bool LoadBuffer(const std::string& id, const std::vector<std::uint8_t>*& out) {
        auto have = buffers.find(id);
        if (have != buffers.end()) { out = &have->second; return true; }
        std::vector<std::uint8_t> data;
        const JVal* b = Get("buffers", id);
        if (id == "binary_glTF" && body) {   // KHR_binary_glTF: the body, whatever the buffer entry says
            data.assign(body, body + bodySize);
        } else {
            if (!b) return Fail("buffer '" + id + "' is missing");
            if (const JVal* t = b->Find("type")) if (t->Text() == "text") return Fail("buffer '" + id + "' has type text, which holds no geometry");
            const std::string uri = b->Find("uri") ? b->Find("uri")->Text() : std::string();
            if (uri.empty()) return Fail("buffer '" + id + "' has no uri");
            if (uri.compare(0, 5, "data:") == 0) {
                const std::size_t comma = uri.find(',');
                if (comma == std::string::npos || uri.find(";base64", 0) == std::string::npos || uri.find(";base64") > comma)
                    return Fail("buffer '" + id + "' is a data URI that is not base64");
                if ((std::uint64_t)(uri.size() - comma) / 4 * 3 > kMaxBufferBytes - bufferBytes) return Fail("the buffers are larger than the limit (512 MB)");
                gltf_detail::Base64Decode(uri.substr(comma + 1), data);
            } else {
                std::string path, why;
                if (!detail::ResolveInsideDir(baseDir, uri, path, why)) return Fail("buffer '" + id + "': " + why);
                std::string bytes, rerr;
                if (!detail::txt::ReadFileCapped(path, (std::size_t)(kMaxBufferBytes - bufferBytes), bytes, &rerr))
                    return Fail("buffer '" + id + "' (" + uri + "): " + (rerr.empty() ? std::string("cannot read the file") : rerr));
                data.assign(bytes.begin(), bytes.end());
            }
            std::uint64_t declared = 0;
            if (WholeNumber(b->Find("byteLength"), UINT64_MAX / 2, declared) && declared > data.size())
                return Fail("buffer '" + id + "' is shorter than its byteLength (" + std::to_string(data.size()) + " of " + std::to_string(declared) + " bytes)");
        }
        if (data.size() > kMaxBufferBytes - bufferBytes) return Fail("the buffers are larger than the limit (512 MB)");
        bufferBytes += data.size();
        out = &(buffers[id] = std::move(data));
        return true;
    }

    bool ReadAccessor(const std::string& id, Accessor& acc, bool indices) {
        const JVal* a = Get("accessors", id);
        if (!a) return Fail("accessor '" + id + "' is missing");
        const std::string bvId = a->Find("bufferView") ? a->Find("bufferView")->Text() : std::string();
        const JVal* bv = Get("bufferViews", bvId);
        if (!bv) return Fail("accessor '" + id + "' names a missing bufferView '" + bvId + "'");
        const std::string bufId = bv->Find("buffer") ? bv->Find("buffer")->Text() : std::string();
        const std::vector<std::uint8_t>* buf = nullptr;
        if (!LoadBuffer(bufId, buf)) return false;

        std::uint64_t bvOffset = 0, bvLength = 0;
        if (bv->Find("byteOffset") && !WholeNumber(bv->Find("byteOffset"), UINT64_MAX / 4, bvOffset)) return Fail("bufferView '" + bvId + "' has a bad byteOffset");
        if (bvOffset > buf->size()) return Fail("bufferView '" + bvId + "' starts past the end of its buffer");
        if (bv->Find("byteLength")) {
            if (!WholeNumber(bv->Find("byteLength"), UINT64_MAX / 4, bvLength)) return Fail("bufferView '" + bvId + "' has a bad byteLength");
        }
        if (bvLength == 0) bvLength = buf->size() - bvOffset;   // 0 (the default) means the rest of the buffer
        if (bvLength > buf->size() - bvOffset) return Fail("bufferView '" + bvId + "' runs past the end of its buffer");

        std::uint64_t offset = 0, stride = 0, count = 0, type = 0;
        if (a->Find("byteOffset") && !WholeNumber(a->Find("byteOffset"), UINT64_MAX / 4, offset)) return Fail("accessor '" + id + "' has a bad byteOffset");
        if (a->Find("byteStride") && !WholeNumber(a->Find("byteStride"), 255, stride)) return Fail("accessor '" + id + "' has a bad byteStride (0 to 255)");
        if (!WholeNumber(a->Find("count"), kMaxAccessorCount, count) || count == 0) return Fail("accessor '" + id + "' has a bad count");
        if (!WholeNumber(a->Find("componentType"), 70000, type)) return Fail("accessor '" + id + "' has no componentType");
        const int bytes = BytesOf(type);
        if (bytes == 0 || (type == 5125 && !indices)) return Fail("accessor '" + id + "' has an unknown componentType " + std::to_string(type));
        const int comps = ComponentsOf(a->Find("type") ? a->Find("type")->Text() : std::string());
        if (comps == 0) return Fail("accessor '" + id + "' has an unknown type");
        const std::uint64_t elem = (std::uint64_t)comps * (std::uint64_t)bytes;
        if (stride == 0) stride = elem;
        if (stride < elem) return Fail("accessor '" + id + "' has a byteStride smaller than one element");
        // The first element must fit, and the last one (count - 1 strides further on) must end inside the bufferView.
        if (offset > bvLength || elem > bvLength - offset) return Fail("accessor '" + id + "' reads past the end of its bufferView");
        if (count - 1 > (bvLength - offset - elem) / stride) return Fail("accessor '" + id + "' reads past the end of its bufferView");

        // The whole file's decode budget, checked before the vectors are allocated.
        const std::uint64_t values = count * (std::uint64_t)comps;
        if (values > kMaxDecoded - decoded) return Fail("the file decodes more than 64,000,000 accessor values (a mesh listed many times, or very large accessors)");
        decoded += values;

        acc.count = count; acc.components = comps; acc.componentType = type;
        const unsigned char* base = buf->data() + bvOffset + offset;
        if (indices) {   // integers, kept exact (a float holds whole numbers exactly only up to 2^24)
            if (comps != 1 || (type != 5121 && type != 5123 && type != 5125)) return Fail("indices accessor '" + id + "' is not SCALAR unsigned byte, short or int");
            acc.indices.resize((std::size_t)count);
            for (std::uint64_t e = 0; e < count; ++e) {
                const unsigned char* p = base + e * stride;
                acc.indices[(std::size_t)e] = type == 5121 ? p[0] : type == 5123 ? (std::uint32_t)(p[0] | (p[1] << 8)) : U32(p);
            }
            return true;
        }
        acc.values.resize((std::size_t)values);
        for (std::uint64_t e = 0; e < count; ++e) {
            const unsigned char* p = base + e * stride;
            for (int c = 0; c < comps; ++c, p += bytes) {
                float v = 0;
                switch (type) {
                case 5120: v = (float)(std::int8_t)p[0]; break;
                case 5121: v = (float)p[0]; break;
                case 5122: v = (float)(std::int16_t)(std::uint16_t)(p[0] | (p[1] << 8)); break;
                case 5123: v = (float)(std::uint16_t)(p[0] | (p[1] << 8)); break;
                default: { const std::uint32_t u = U32(p); std::memcpy(&v, &u, 4); break; }
                }
                if (!std::isfinite(v)) return Fail("accessor '" + id + "' holds a value that is not a finite number");
                acc.values[(std::size_t)(e * (std::uint64_t)comps) + (std::size_t)c] = v;
            }
        }
        return true;
    }

    // A texture id to the image file it names, kept only when the file lies inside the model's folder.
    void TextureFor(const std::string& textureId, std::string& out) {
        const JVal* t = Get("textures", textureId);
        if (!t) return;
        const std::string imageId = t->Find("source") ? t->Find("source")->Text() : std::string();
        const JVal* img = Get("images", imageId);
        if (!img) return;
        if (const JVal* e = img->Find("extensions")) if (e->Find("KHR_binary_glTF")) { ++embeddedImages; return; }
        const std::string uri = img->Find("uri") ? img->Find("uri")->Text() : std::string();
        if (uri.empty()) return;
        if (uri.compare(0, 5, "data:") == 0) { ++embeddedImages; return; }
        std::string path, why;
        if (!detail::ResolveInsideDir(baseDir, uri, path, why)) { ++refusedImages; return; }
        out = path;
    }

    // One primitive as a mesh part. false only for a hard error (a bad accessor); a primitive we skip returns true.
    bool Primitive(const JVal& prim, int node, ImportedModel& out) {
        std::uint64_t mode = 4;
        // Every primitive counts, including those skipped below, so a file cannot make unbounded work out of them.
        if (++primitives > kMaxParts) return Fail("the file has more than 100,000 primitives");
        if (prim.Find("mode") && !WholeNumber(prim.Find("mode"), 1000, mode)) return Fail("a primitive has a bad mode");
        if (mode <= 3) { ++linePrims; return true; }       // points, lines, line loop, line strip
        if (mode > 6) { ++unknownModePrims; return true; }
        const JVal* attrs = prim.Find("attributes");
        const JVal* posRef = attrs ? attrs->Find("POSITION") : nullptr;
        if (!posRef || posRef->type != JVal::Str) return Fail("a primitive has no POSITION attribute");
        Accessor pos;
        if (!ReadAccessor(posRef->Text(), pos, false)) return false;
        if (pos.components != 3) return Fail("POSITION accessor '" + posRef->Text() + "' is not VEC3");
        if (pos.count > kMaxVertices - vertices) return Fail("the file has more than 20,000,000 vertices");
        vertices += pos.count;
        const std::size_t n = (std::size_t)pos.count;

        Mesh mesh;
        mesh.vertices.resize(n);
        for (std::size_t i = 0; i < n; ++i) mesh.vertices[i] = Vec3{pos.values[i * 3], pos.values[i * 3 + 1], pos.values[i * 3 + 2]};
        pos.values = std::vector<float>();   // give the memory back before the next accessor
        if (const JVal* nr = attrs->Find("NORMAL")) {
            Accessor nor;
            if (!ReadAccessor(nr->Text(), nor, false)) return false;
            if (nor.components == 3 && nor.count == pos.count) {
                mesh.normals.resize(n);
                for (std::size_t i = 0; i < n; ++i) mesh.normals[i] = Vec3{nor.values[i * 3], nor.values[i * 3 + 1], nor.values[i * 3 + 2]};
            } else ++droppedNormals;
        }
        if (const JVal* tr = attrs->Find("TEXCOORD_0")) {
            Accessor uv;
            if (!ReadAccessor(tr->Text(), uv, false)) return false;
            if (uv.components == 2 && uv.count == pos.count) {
                mesh.uvs.resize(n);
                for (std::size_t i = 0; i < n; ++i) mesh.uvs[i] = Vec2{uv.values[i * 2], uv.values[i * 2 + 1]};
            } else ++droppedUvs;
        }

        std::vector<std::uint32_t> idx;
        if (const JVal* ir = prim.Find("indices")) {
            Accessor ia;
            if (!ReadAccessor(ir->Text(), ia, true)) return false;
            idx = std::move(ia.indices);
            for (std::uint32_t v : idx)
                if (v >= pos.count) return Fail("an index (" + std::to_string(v) + ") is past the primitive's " + std::to_string(pos.count) + " vertices");
        } else {
            idx.resize(n);
            for (std::size_t i = 0; i < n; ++i) idx[i] = (std::uint32_t)i;
        }
        auto tri = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) -> bool {
            // Every triangle slot counts against the budget, a degenerate one too, so equal indices cannot make free work.
            if (triangles >= kMaxTriangles) return Fail("the file has more than 20,000,000 triangles (degenerate ones included)");
            ++triangles;
            if (a == b || b == c || a == c) { ++degenerate; return true; }
            mesh.triangles.push_back((int)a); mesh.triangles.push_back((int)b); mesh.triangles.push_back((int)c);
            return true;
        };
        const std::size_t k = idx.size();
        if (mode == 4) { for (std::size_t i = 0; i + 2 < k; i += 3) if (!tri(idx[i], idx[i + 1], idx[i + 2])) return false; }
        else if (mode == 5) { for (std::size_t i = 2; i < k; ++i) if (!(i % 2 == 0 ? tri(idx[i - 2], idx[i - 1], idx[i]) : tri(idx[i - 1], idx[i - 2], idx[i]))) return false; }
        else { for (std::size_t i = 2; i < k; ++i) if (!tri(idx[0], idx[i - 1], idx[i])) return false; }
        if (mesh.triangles.empty()) return true;

        // Normals the file did not give (or gave as zero): from the faces, weighted by area, shared per vertex index.
        bool needNormals = mesh.normals.empty();
        for (Vec3& v : mesh.normals) { const float m = v.Magnitude(); if (!(m > 1e-12f) || !std::isfinite(m)) { needNormals = true; break; } v = v * (1.0f / m); }
        if (needNormals) {
            mesh.normals.assign(n, Vec3{0, 0, 0});
            for (std::size_t t = 0; t + 2 < mesh.triangles.size(); t += 3) {
                const int a = mesh.triangles[t], b = mesh.triangles[t + 1], c = mesh.triangles[t + 2];
                const Vec3 f = Vec3::Cross(mesh.vertices[(std::size_t)b] - mesh.vertices[(std::size_t)a], mesh.vertices[(std::size_t)c] - mesh.vertices[(std::size_t)a]);
                mesh.normals[(std::size_t)a] = mesh.normals[(std::size_t)a] + f;
                mesh.normals[(std::size_t)b] = mesh.normals[(std::size_t)b] + f;
                mesh.normals[(std::size_t)c] = mesh.normals[(std::size_t)c] + f;
            }
            for (Vec3& v : mesh.normals) { const float m = v.Magnitude(); v = (m > 1e-20f && std::isfinite(m)) ? v * (1.0f / m) : Vec3{0, 1, 0}; }
        }

        if (out.parts.size() >= kMaxParts) return Fail("the file has more than 100,000 mesh parts");
        ImportedPart part;
        part.mesh = std::move(mesh);
        part.node = node;
        const std::string matId = prim.Find("material") ? prim.Find("material")->Text() : std::string();
        if (!matId.empty() && Get("materials", matId)) {   // one engine material per glTF material id, built on first use
            auto have = materialIndex.find(matId);
            if (have == materialIndex.end()) {
                have = materialIndex.emplace(matId, (int)out.materials.size()).first;
                out.materials.push_back(BuildMaterial(matId));
            }
            part.material = have->second;
        }
        out.parts.push_back(std::move(part));
        return true;
    }

    ImportedMaterial BuildMaterial(const std::string& id) {
        ImportedMaterial out;
        const JVal* m = Get("materials", id);
        out.name = id;
        if (!m) return out;
        if (m->Find("name") && !m->Find("name")->Text().empty()) out.name = m->Find("name")->Text();
        std::vector<const JVal*> valueSets;
        if (const JVal* v = m->Find("values")) valueSets.push_back(v);
        if (const JVal* e = m->Find("extensions")) if (const JVal* k = e->Find("KHR_materials_common")) if (const JVal* v = k->Find("values")) valueSets.push_back(v);
        auto colour = [](const JVal* v, Color& c) {
            if (!v || v->type != JVal::Arr || v->arr.size() < 3) return false;
            float f[4] = {1, 1, 1, 1};
            for (std::size_t i = 0; i < v->arr.size() && i < 4; ++i) {
                const double d = v->arr[i].Number(-1);
                if (!std::isfinite(d)) return false;
                f[i] = (float)std::max(0.0, std::min(1.0, d));
            }
            c = Color{f[0], f[1], f[2], f[3]};
            return true;
        };
        for (const JVal* vals : valueSets) {
            if (vals->type != JVal::Obj) continue;
            if (const JVal* d = vals->Find("diffuse")) {
                if (d->type == JVal::Str) TextureFor(d->Text(), out.texture);
                else if (colour(d, out.diffuse)) out.hasDiffuse = true;
            }
            if (const JVal* e = vals->Find("emission")) if (colour(e, out.emissive)) out.hasEmissive = true;
            if (const JVal* s = vals->Find("shininess")) {
                const double sh = s->type == JVal::Arr && !s->arr.empty() ? s->arr[0].Number(-1) : s->Number(-1);
                if (std::isfinite(sh) && sh >= 0 && sh <= 1e6) out.shininess = (float)sh;
            }
        }
        return out;
    }

    // A node's local transform: `matrix` (column-major, 16 numbers) or translation / rotation (x, y, z, w) / scale.
    bool Transform(const JVal& n, const std::string& id, ImportedNode& node) {
        auto numbers = [&](const JVal* a, std::size_t want, float* f) -> bool {
            if (!a || a->type != JVal::Arr || a->arr.size() != want) return false;
            for (std::size_t i = 0; i < want; ++i) {
                const double d = a->arr[i].type == JVal::Num ? a->arr[i].num : NAN;
                if (!std::isfinite(d) || std::fabs(d) > 1e15) return false;
                f[i] = (float)d;
            }
            return true;
        };
        if (const JVal* m = n.Find("matrix")) {
            Mat4 mat;
            if (!numbers(m, 16, mat.m)) return Fail("node '" + id + "' has a bad matrix (16 finite numbers)");
            Vec3 t, s; Quat r;
            if (!mat.Decompose(t, r, s)) Note("node '" + id + "': its matrix has shear or a zero scale; the closest translation, rotation and scale were used");
            node.position = t; node.rotation = r; node.scale = s;
            return true;
        }
        float f[4];
        if (const JVal* t = n.Find("translation")) { if (!numbers(t, 3, f)) return Fail("node '" + id + "' has a bad translation"); node.position = Vec3{f[0], f[1], f[2]}; }
        if (const JVal* s = n.Find("scale")) { if (!numbers(s, 3, f)) return Fail("node '" + id + "' has a bad scale"); node.scale = Vec3{f[0], f[1], f[2]}; }
        if (const JVal* r = n.Find("rotation")) {
            if (!numbers(r, 4, f)) return Fail("node '" + id + "' has a bad rotation");
            const double len = std::sqrt((double)f[0] * f[0] + (double)f[1] * f[1] + (double)f[2] * f[2] + (double)f[3] * f[3]);
            if (len < 1e-12) Note("node '" + id + "' has a zero rotation quaternion; no rotation was used");
            else node.rotation = Quat{(float)(f[0] / len), (float)(f[1] / len), (float)(f[2] / len), (float)(f[3] / len)};
        }
        return true;
    }

    bool Run(const std::string& bytes, ImportedModel& out) {
        if (bytes.size() > kMaxFileBytes) return Fail("file too large");
        std::string json;
        const unsigned char* b = reinterpret_cast<const unsigned char*>(bytes.data());
        if (bytes.size() >= 4 && std::memcmp(b, "glTF", 4) == 0) {
            // KHR_binary_glTF: magic, version 1, total length, content length, content format 0 (JSON); content; body.
            if (bytes.size() < 20) return Fail("the binary header is cut short");
            std::uint32_t length = U32(b + 8);
            const std::uint32_t version = U32(b + 4), contentLength = U32(b + 12), format = U32(b + 16);
            if (version != 1) return Fail("binary glTF version " + std::to_string(version) + " (this reader reads version 1; version 2 is glTF 2.0)");
            if (length < 20) return Fail("the binary header's length (" + std::to_string(length) + ") is shorter than the header");
            if (length > bytes.size()) {
                // A file cut short (one of the real sample files is, by 33 bytes, at the end of an embedded image): read what is
                // there. Every accessor is still checked against the bytes that exist, so data past the end is refused.
                Note("the file is " + std::to_string(length - bytes.size()) + " byte(s) shorter than its header says; only the bytes present were read");
                length = (std::uint32_t)bytes.size();
            }
            if (contentLength == 0 || contentLength > length - 20) return Fail("the binary header's content length does not fit the file");
            if (format != 0) return Fail("binary content format " + std::to_string(format) + " (only 0, JSON, is defined)");
            json.assign(bytes.data() + 20, contentLength);
            body = b + 20 + contentLength;
            bodySize = length - 20 - contentLength;
            if (length < bytes.size()) Note(std::to_string(bytes.size() - length) + " byte(s) after the binary glTF's length were ignored");
        } else {
            json = bytes;
            if (json.size() >= 3 && (unsigned char)json[0] == 0xEF && (unsigned char)json[1] == 0xBB && (unsigned char)json[2] == 0xBF) json.erase(0, 3);
        }
        if (!NestingWithin(json, kMaxJsonDepth)) return Fail("the JSON is nested more than 128 deep");
        gltf_detail::JParser jp(json);
        root = jp.Parse();
        if (!jp.ok || root.type != JVal::Obj) return Fail("the file is not valid JSON");
        if (const JVal* asset = root.Find("asset")) if (const JVal* v = asset->Find("version")) {
            const std::string ver = v->type == JVal::Str ? v->str : (v->type == JVal::Num ? std::to_string(v->num) : std::string());
            if (!ver.empty() && ver[0] != '1') return Fail("asset.version is " + ver + " (this reader reads glTF 1.x; 2.x is read by the glTF 2.0 reader)");
        }
        for (const char* c : {"meshes", "nodes", "accessors"})
            if (const JVal* col = root.Find(c)) if (col->type == JVal::Arr) return Fail(std::string("'") + c + "' is an array: this is glTF 2.0, not 1.0");

        // The scene: `scene` names the default; otherwise the first scene by id; otherwise every node that is nobody's child.
        std::vector<std::string> roots;
        const JVal* scenes = root.Find("scenes");
        std::string sceneId = root.Find("scene") ? root.Find("scene")->Text() : std::string();
        if (sceneId.empty() && scenes && scenes->type == JVal::Obj && !scenes->obj.empty()) {
            sceneId = scenes->obj.begin()->first;
            if (scenes->obj.size() > 1) Note("no default scene; the first scene by name ('" + sceneId + "') was read");
        }
        const JVal* nodes = root.Find("nodes");
        if (!sceneId.empty()) {
            const JVal* s = Get("scenes", sceneId);
            if (!s) return Fail("the default scene '" + sceneId + "' is missing");
            if (const JVal* ns = s->Find("nodes")) for (const JVal& n : ns->arr) roots.push_back(n.Text());
        } else if (nodes && nodes->type == JVal::Obj) {
            std::set<std::string> children;
            for (const auto& kv : nodes->obj) if (const JVal* c = kv.second.Find("children")) for (const JVal& x : c->arr) children.insert(x.Text());
            for (const auto& kv : nodes->obj) if (!children.count(kv.first)) roots.push_back(kv.first);
        }
        if (nodes && nodes->type == JVal::Obj && nodes->obj.size() > kMaxNodes) return Fail("the file has more than 100,000 nodes");

        ImportedNode top;
        top.name = "glTF";
        out.nodes.push_back(top);
        std::set<std::string> placed;
        std::size_t cameras = 0, skinned = 0, joints = 0;
        // Depth-first with an explicit stack of (node id, parent index), roots in file order.
        std::vector<std::pair<std::string, int>> stack;
        for (auto it = roots.rbegin(); it != roots.rend(); ++it) stack.push_back({*it, 0});
        while (!stack.empty()) {
            const auto [id, parent] = stack.back();
            stack.pop_back();
            const JVal* n = Get("nodes", id);
            if (!n) return Fail("node '" + id + "' is missing");
            if (!placed.insert(id).second) return Fail("node '" + id + "' is reached twice (a loop, or a node with two parents)");
            if (out.nodes.size() > kMaxNodes) return Fail("the file has more than 100,000 nodes");
            ImportedNode node;
            node.name = n->Find("name") && !n->Find("name")->Text().empty() ? n->Find("name")->Text() : id;
            node.parent = parent;
            if (!Transform(*n, id, node)) return false;
            const int index = (int)out.nodes.size();
            out.nodes.push_back(node);
            if (n->Find("camera")) ++cameras;
            if (n->Find("skin") || n->Find("skeletons")) ++skinned;
            if (n->Find("jointName")) ++joints;
            if (const JVal* ms = n->Find("meshes")) {
                for (const JVal& mref : ms->arr) {
                    const JVal* mesh = Get("meshes", mref.Text());
                    if (!mesh) return Fail("node '" + id + "' names a missing mesh '" + mref.Text() + "'");
                    if (const JVal* prims = mesh->Find("primitives"))
                        for (const JVal& prim : prims->arr) if (!Primitive(prim, index, out)) return false;
                }
            }
            if (const JVal* cs = n->Find("children"))
                for (auto it = cs->arr.rbegin(); it != cs->arr.rend(); ++it) stack.push_back({it->Text(), index});
        }
        if (out.parts.empty()) return Fail("the scene has no triangles (no mesh, or only points and lines)");

        out.backend = "own";
        if (linePrims) Note(std::to_string(linePrims) + " primitive(s) of points or lines skipped");
        if (unknownModePrims) Note(std::to_string(unknownModePrims) + " primitive(s) with an unknown mode skipped");
        if (droppedNormals) Note(std::to_string(droppedNormals) + " NORMAL attribute(s) that are not VEC3 per vertex ignored (normals made from the faces)");
        if (droppedUvs) Note(std::to_string(droppedUvs) + " TEXCOORD_0 attribute(s) that are not VEC2 per vertex ignored");
        if (degenerate) Note(std::to_string(degenerate) + " degenerate triangle(s) skipped");
        if (embeddedImages) Note(std::to_string(embeddedImages) + " embedded image(s) (data URI or binary body) not extracted; the material keeps its colour");
        if (refusedImages) Note(std::to_string(refusedImages) + " image path(s) outside the model's folder refused");
        if (cameras) Note(std::to_string(cameras) + " camera node(s): cameras are not imported");
        if (skinned || joints || (root.Find("skins") && !root.Find("skins")->obj.empty())) Note("skins (joints and weights) are not read; skinned meshes import in their bind pose");
        if (root.Find("animations") && !root.Find("animations")->obj.empty()) Note(std::to_string(root.Find("animations")->obj.size()) + " animation(s) not read");
        if (root.Find("techniques") && !root.Find("techniques")->obj.empty()) Note("material techniques and shaders are not run; each material's diffuse colour or texture, emission and shininess are used");
        return true;
    }
};

} // namespace gltf1
} // namespace

bool ReadGltf1Buffer(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    gltf1::Reader r;
    r.baseDir = baseDir;
    if (!r.Run(bytes, out)) {
        const std::string m = r.err.empty() ? "glTF 1.0: could not read file" : r.err;
        out = ImportedModel{};
        if (err) *err = m;
        return false;
    }
    out.notes.insert(out.notes.begin(), r.notes.begin(), r.notes.end());
    return true;
}

bool ReadGltf1File(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, gltf1::kMaxFileBytes, bytes, err)) return false;
    const std::size_t slash = path.find_last_of("/\\");
    const std::string baseDir = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
    return ReadGltf1Buffer(bytes, baseDir, out, err);
}

bool GltfFileIsVersion1(const std::string& path) {
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, gltf1::kMaxFileBytes, bytes, nullptr)) return false;
    if (bytes.size() >= 4 && std::memcmp(bytes.data(), "glTF", 4) == 0) return IsGltf1(bytes);
    if (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF && (unsigned char)bytes[1] == 0xBB && (unsigned char)bytes[2] == 0xBF) bytes.erase(0, 3);
    if (!gltf1::NestingWithin(bytes, gltf1::kMaxJsonDepth)) return false;
    gltf_detail::JParser jp(bytes);
    const gltf_detail::JVal root = jp.Parse();
    if (!jp.ok || root.type != gltf_detail::JVal::Obj) return false;
    if (const gltf_detail::JVal* asset = root.Find("asset")) if (const gltf_detail::JVal* v = asset->Find("version"))
        if (v->type == gltf_detail::JVal::Str && !v->str.empty()) return v->str[0] == '1';
    // No version: glTF 1.0 keeps its collections in objects keyed by id, 2.0 in arrays.
    for (const char* c : {"meshes", "nodes", "accessors"})
        if (const gltf_detail::JVal* col = root.Find(c)) return col->type == gltf_detail::JVal::Obj;
    return false;
}

bool IsGltf1(const std::string& bytesOrPrefix) {
    const unsigned char* b = reinterpret_cast<const unsigned char*>(bytesOrPrefix.data());
    if (bytesOrPrefix.size() >= 8 && std::memcmp(b, "glTF", 4) == 0) return gltf1::U32(b + 4) == 1;
    return false;
}

} // namespace okay
