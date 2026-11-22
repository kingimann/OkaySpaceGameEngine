#pragma once
// Builds glTF 1.0 files in memory for the reader tests, from the Khronos glTF 1.0 specification: collections are objects
// keyed by string ids, accessors carry their own byteStride, and the binary form (KHR_binary_glTF) is a 20-byte header,
// one JSON content block and one body addressed by the buffer id "binary_glTF".
//
// The standard scene: a unit quad in the z = 0 plane (4 vertices, normals +Z, texture coordinates at the corners, 6
// unsigned-short indices), on node "child" (a matrix: scale 2, then translation 0 0 5) under node "root" (translation
// 1 2 3), in scene "s", with material "mat" (diffuse 0.5 0.25 1 1, shininess 8).
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace gltf1build {

inline void PutF(std::string& s, float f) { std::uint32_t u; std::memcpy(&u, &f, 4); for (int k = 0; k < 4; ++k) s.push_back((char)((u >> (8 * k)) & 0xff)); }
inline void PutU16(std::string& s, unsigned v) { s.push_back((char)(v & 0xff)); s.push_back((char)((v >> 8) & 0xff)); }
inline void PutU32(std::string& s, std::uint32_t v) { for (int k = 0; k < 4; ++k) s.push_back((char)((v >> (8 * k)) & 0xff)); }

// The quad's buffer: positions (48 bytes at 0), normals (48 at 48), texture coordinates (32 at 96), indices (12 at 128).
inline std::string QuadBuffer() {
    std::string b;
    const float pos[4][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
    for (auto& p : pos) for (float f : p) PutF(b, f);
    for (int i = 0; i < 4; ++i) { PutF(b, 0); PutF(b, 0); PutF(b, 1); }
    const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (auto& t : uv) for (float f : t) PutF(b, f);
    for (unsigned i : {0u, 1u, 2u, 0u, 2u, 3u}) PutU16(b, i);
    return b;   // 140 bytes
}

inline std::string Base64(const std::string& in) {
    static const char* abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const std::uint32_t v = ((std::uint8_t)in[i] << 16) | ((std::uint8_t)in[i + 1] << 8) | (std::uint8_t)in[i + 2];
        out += abc[(v >> 18) & 63]; out += abc[(v >> 12) & 63]; out += abc[(v >> 6) & 63]; out += abc[v & 63];
    }
    if (i + 1 == in.size()) { const std::uint32_t v = (std::uint8_t)in[i] << 16; out += abc[(v >> 18) & 63]; out += abc[(v >> 12) & 63]; out += "=="; }
    else if (i + 2 == in.size()) { const std::uint32_t v = ((std::uint8_t)in[i] << 16) | ((std::uint8_t)in[i + 1] << 8); out += abc[(v >> 18) & 63]; out += abc[(v >> 12) & 63]; out += abc[(v >> 6) & 63]; out += '='; }
    return out;
}

struct QuadOptions {
    std::string bufferId = "buf";
    std::string bufferUri;            // empty: a base64 data URI of QuadBuffer()
    std::string primitive;            // empty: the standard indexed triangle primitive
    std::string materialValues = "\"diffuse\": [0.5, 0.25, 1, 1], \"shininess\": 8";
    std::string materialExtra;        // e.g. an "extensions" member, with a leading comma
    std::string extraTopLevel;        // more top-level members, with a leading comma
    std::string version = "1.0";
    bool withScene = true;
};

inline std::string QuadJson(const QuadOptions& o = QuadOptions()) {
    const std::string uri = o.bufferUri.empty() ? "data:application/octet-stream;base64," + Base64(QuadBuffer()) : o.bufferUri;
    const std::string prim = o.primitive.empty()
        ? "{ \"attributes\": { \"POSITION\": \"pos\", \"NORMAL\": \"nor\", \"TEXCOORD_0\": \"uv\" }, \"indices\": \"idx\", \"material\": \"mat\", \"mode\": 4 }"
        : o.primitive;
    std::string j = "{\n";
    j += "  \"asset\": { \"version\": \"" + o.version + "\" },\n";
    if (o.withScene) j += "  \"scene\": \"s\",\n  \"scenes\": { \"s\": { \"nodes\": [\"root\"] } },\n";
    j += "  \"nodes\": {\n";
    j += "    \"root\": { \"name\": \"Root\", \"translation\": [1, 2, 3], \"children\": [\"child\"] },\n";
    j += "    \"child\": { \"matrix\": [2,0,0,0, 0,2,0,0, 0,0,2,0, 0,0,5,1], \"meshes\": [\"m\"] }\n  },\n";
    j += "  \"meshes\": { \"m\": { \"primitives\": [ " + prim + " ] } },\n";
    j += "  \"materials\": { \"mat\": { \"name\": \"Mat\", \"values\": { " + o.materialValues + " }" + o.materialExtra + " } },\n";
    j += "  \"accessors\": {\n";
    j += "    \"pos\": { \"bufferView\": \"bv_attr\", \"byteOffset\": 0, \"byteStride\": 12, \"componentType\": 5126, \"count\": 4, \"type\": \"VEC3\" },\n";
    j += "    \"nor\": { \"bufferView\": \"bv_attr\", \"byteOffset\": 48, \"byteStride\": 12, \"componentType\": 5126, \"count\": 4, \"type\": \"VEC3\" },\n";
    j += "    \"uv\":  { \"bufferView\": \"bv_attr\", \"byteOffset\": 96, \"byteStride\": 8, \"componentType\": 5126, \"count\": 4, \"type\": \"VEC2\" },\n";
    j += "    \"idx\": { \"bufferView\": \"bv_idx\", \"byteOffset\": 0, \"componentType\": 5123, \"count\": 6, \"type\": \"SCALAR\" }\n  },\n";
    j += "  \"bufferViews\": {\n";
    j += "    \"bv_attr\": { \"buffer\": \"" + o.bufferId + "\", \"byteOffset\": 0, \"byteLength\": 128, \"target\": 34962 },\n";
    j += "    \"bv_idx\":  { \"buffer\": \"" + o.bufferId + "\", \"byteOffset\": 128, \"byteLength\": 12, \"target\": 34963 }\n  },\n";
    j += "  \"buffers\": { \"" + o.bufferId + "\": { \"uri\": \"" + uri + "\", \"byteLength\": 140 } }" + o.extraTopLevel + "\n}\n";
    return j;
}

// The binary form: header (magic, version 1, total length, content length, format 0), the JSON padded with spaces so the
// body starts on a 4-byte boundary, then the body.
inline std::string Binary(const std::string& json, const std::string& body, std::uint32_t version = 1) {
    std::string content = json;
    while ((20 + content.size()) % 4) content.push_back(' ');
    std::string out = "glTF";
    PutU32(out, version);
    PutU32(out, (std::uint32_t)(20 + content.size() + body.size()));
    PutU32(out, (std::uint32_t)content.size());
    PutU32(out, 0);
    return out + content + body;
}

inline std::string QuadBinary() {
    QuadOptions o;
    o.bufferId = "binary_glTF";
    o.bufferUri = "data:,";
    o.extraTopLevel = ",\n  \"extensionsUsed\": [\"KHR_binary_glTF\"]";
    return Binary(QuadJson(o), QuadBuffer());
}

} // namespace gltf1build
