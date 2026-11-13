#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/Gltf.hpp"
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace okay;
typedef std::vector<std::uint8_t> Bytes;

// .glb container parsing (header, chunk walk, JSON/BIN extraction, the bufferless
// buffer -> BIN fallback), driven with files built here and written to the temp dir.

static void P32(Bytes& b, std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((std::uint8_t)(v >> (8 * i))); }
static void PF(Bytes& b, float f) { std::uint8_t t[4]; std::memcpy(t, &f, 4); b.insert(b.end(), t, t + 4); }

// A triangle (3 x vec3 float, 36 bytes) in a bufferless buffer, so the .glb BIN chunk is its data.
static std::string TriJson(int byteLength = 36) {
    return "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":" + std::to_string(byteLength) + "}],"
           "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36}],"
           "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}],"
           "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}]}";
}
static Bytes TriBin() {
    Bytes b; const float v[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    for (float f : v) PF(b, f);
    return b;
}
static Bytes Chunk(std::uint32_t type, const Bytes& data, std::uint8_t pad = 0, bool padToFour = true) {
    Bytes c = data;
    if (padToFour) while (c.size() % 4) c.push_back(pad);
    Bytes o; P32(o, (std::uint32_t)c.size()); P32(o, type);
    o.insert(o.end(), c.begin(), c.end());
    return o;
}
static const std::uint32_t kJson = 0x4E4F534A, kBin = 0x004E4942;
static Bytes Glb(const std::vector<Bytes>& chunks) {
    Bytes body; for (const Bytes& c : chunks) body.insert(body.end(), c.begin(), c.end());
    Bytes o = {'g', 'l', 'T', 'F'}; P32(o, 2); P32(o, (std::uint32_t)(12 + body.size()));
    o.insert(o.end(), body.begin(), body.end());
    return o;
}
static Bytes Str(const std::string& s) { return Bytes(s.begin(), s.end()); }

struct Result { bool docOk = false; std::size_t bufBytes = 0; bool meshOk = false; std::size_t verts = 0; };
static Result Load(const Bytes& file, const char* name) {
    const std::string path = (std::filesystem::temp_directory_path() / name).string();
    { std::ofstream o(path, std::ios::binary); o.write((const char*)file.data(), (std::streamsize)file.size()); }
    Result r;
    gltf_detail::GltfDoc d = gltf_detail::LoadDoc(path);
    r.docOk = d.ok; r.bufBytes = d.buffers.empty() ? 0 : d.buffers[0].size();
    Mesh m = LoadGLTF(path, &r.meshOk);
    r.verts = m.vertices.size();
    std::filesystem::remove(path);
    return r;
}

int main() {
    RUN_SUITE("glb_container");

    // The reference: a valid minimal file loads, one triangle.
    {
        Result r = Load(Glb({Chunk(kJson, Str(TriJson()), ' '), Chunk(kBin, TriBin())}), "okay_glb_valid.glb");
        CHECK(r.docOk && r.meshOk && r.verts == 3 && r.bufBytes == 36);
    }
    // Header only / shorter than the 12-byte header: rejected, no crash.
    {
        const Bytes full = Glb({Chunk(kJson, Str(TriJson()), ' '), Chunk(kBin, TriBin())});
        for (std::size_t n : {std::size_t(0), std::size_t(3), std::size_t(4), std::size_t(8), std::size_t(11), std::size_t(12), std::size_t(19)}) {
            Result r = Load(Bytes(full.begin(), full.begin() + (std::ptrdiff_t)n), "okay_glb_trunc.glb");
            CHECK(!r.docOk && !r.meshOk);
        }
    }
    // A chunk whose length runs past the end of the file.
    {
        Bytes f = Glb({Chunk(kJson, Str(TriJson()), ' ')});
        f[12] = 0xF0; f[13] = 0xFF; f[14] = 0xFF; f[15] = 0xFF;          // JSON chunk claims 4 GB
        Result r = Load(f, "okay_glb_pastjson.glb");
        CHECK(!r.docOk && !r.meshOk);
        Bytes g = Glb({Chunk(kJson, Str(TriJson()), ' '), Chunk(kBin, TriBin())});
        const std::size_t binHdr = 12 + 8 + ((TriJson().size() + 3) / 4) * 4;
        g[binHdr] = 0xF0; g[binHdr + 1] = 0xFF; g[binHdr + 2] = 0xFF; g[binHdr + 3] = 0xFF;   // BIN chunk claims 4 GB
        Result q = Load(g, "okay_glb_pastbin.glb");
        CHECK(q.docOk && q.bufBytes == 0 && !q.meshOk && q.verts == 0);   // JSON survives, the data is missing
        Bytes h = Glb({Chunk(kJson, Str(TriJson()), ' '), Chunk(kBin, TriBin())});
        h.resize(h.size() - 5);                                           // file cut inside the BIN data
        Result t = Load(h, "okay_glb_cutbin.glb");
        CHECK(t.docOk && !t.meshOk && t.verts == 0);
    }
    // Zero-length JSON chunk.
    {
        Result r = Load(Glb({Chunk(kJson, Bytes()), Chunk(kBin, TriBin())}), "okay_glb_emptyjson.glb");
        CHECK(!r.docOk && !r.meshOk);
        Result q = Load(Glb({Chunk(kBin, TriBin())}), "okay_glb_nojson.glb");   // no JSON chunk at all
        CHECK(!q.docOk && !q.meshOk);
    }
    // BIN shorter than the accessors / the JSON's byteLength: the read is refused, not run off the end.
    {
        Bytes shortBin = TriBin(); shortBin.resize(20);
        Result r = Load(Glb({Chunk(kJson, Str(TriJson()), ' '), Chunk(kBin, shortBin)}), "okay_glb_shortbin.glb");
        CHECK(r.docOk && r.bufBytes == 20 && !r.meshOk && r.verts == 0);
        Result q = Load(Glb({Chunk(kJson, Str(TriJson()), ' '), Chunk(kBin, Bytes())}), "okay_glb_emptybin.glb");
        CHECK(q.docOk && !q.meshOk && q.verts == 0);
        Result n = Load(Glb({Chunk(kJson, Str(TriJson()), ' ')}), "okay_glb_missingbin.glb");   // bufferless entry, no BIN chunk
        CHECK(n.docOk && !n.meshOk && n.verts == 0);
    }
    // Chunk lengths that are not a multiple of 4: an unpadded JSON chunk (length stays odd) and
    // a final BIN chunk with no padding both load; padding is only skipped between chunks.
    {
        const std::string j = TriJson();
        Bytes odd = Str(j);
        while (odd.size() % 4 != 1) odd.push_back(' ');                   // JSON length = 1 mod 4, padded by hand to 4n+1
        Bytes jc = Chunk(kJson, odd, ' ', false);
        while (jc.size() % 4) jc.push_back(' ');                          // the 3 pad bytes after the odd chunk
        Result r = Load(Glb({jc, Chunk(kBin, TriBin())}), "okay_glb_oddjson.glb");
        CHECK(r.docOk && r.meshOk && r.verts == 3);
        Bytes bin = TriBin(); bin.push_back(7); bin.push_back(7);        // BIN 38 bytes: longer than needed, unpadded
        Result q = Load(Glb({Chunk(kJson, Str(TriJson(38)), ' '), Chunk(kBin, bin, 0, false)}), "okay_glb_oddbin.glb");
        CHECK(q.docOk && q.meshOk && q.verts == 3 && q.bufBytes == 38);
    }
    // JSON chunk after the BIN chunk (spec says JSON first): still read, order does not matter.
    {
        Result r = Load(Glb({Chunk(kBin, TriBin()), Chunk(kJson, Str(TriJson()), ' ')}), "okay_glb_jsonlast.glb");
        CHECK(r.docOk && r.meshOk && r.verts == 3);
    }
    // An unknown chunk type between them is skipped; trailing NUL padding on the JSON is tolerated.
    {
        Result r = Load(Glb({Chunk(kJson, Str(TriJson()), 0), Chunk(0x12345678, Str("xxxx")), Chunk(kBin, TriBin())}), "okay_glb_unknown.glb");
        CHECK(r.docOk && r.meshOk && r.verts == 3);
    }
    TEST_MAIN_RESULT();
}
