// Malformed input and a deterministic fuzz sweep for our own model readers (OBJ+MTL, STL, PLY).
// This file needs only the readers (okay/Render/ModelData.hpp, engine/src/Render/ModelReaders.cpp),
// so it can also be built on its own with -fsanitize=address,undefined; under ctest it runs on the
// engine library. Fixtures: tests/data/models.
#include "test_framework.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace okay;

static const std::string kDir = std::string(OKAY_SOURCE_DIR) + "/tests/data/models/";

static std::string Slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// Every mesh in the model satisfies what the renderer assumes.
static bool Sane(const ImportedModel& m) {
    for (const ImportedPart& p : m.parts) {
        const Mesh& me = p.mesh;
        if (me.triangles.size() % 3) return false;
        for (int t : me.triangles) if (t < 0 || t >= (int)me.vertices.size()) return false;
        for (const Vec3& v : me.vertices) if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return false;
        if (!me.normals.empty() && me.normals.size() != me.vertices.size()) return false;
        if (!me.uvs.empty() && me.uvs.size() != me.vertices.size()) return false;
    }
    return true;
}

// Deterministic pseudo-random bytes for the fuzz sweep.
struct FuzzRng {
    std::uint64_t s;
    std::uint32_t Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (std::uint32_t)(s >> 11); }
};

static std::string Mutate(const std::string& src, FuzzRng& r) {
    std::string s = src;
    int kind = (int)(r.Next() % 6);
    if (s.empty()) return s;
    switch (kind) {
        case 0: for (int i = 0, n = 1 + (int)(r.Next() % 4); i < n; ++i) s[r.Next() % s.size()] = (char)r.Next(); break;
        case 1: s.resize(r.Next() % s.size()); break;                                   // truncate
        case 2: s.insert(r.Next() % s.size(), std::string(1 + r.Next() % 8, (char)r.Next())); break;
        case 3: { std::size_t a = r.Next() % s.size(), n = r.Next() % (s.size() - a + 1); s.erase(a, n); } break;
        case 4: { std::size_t a = r.Next() % s.size(), n = r.Next() % (s.size() - a + 1); s.insert(r.Next() % s.size(), s.substr(a, n)); } break;
        default: for (char& c : s) if ((r.Next() & 63) == 0) c = (char)r.Next(); break;   // sprinkle
    }
    return s;
}

static void FuzzOne(const std::string& bytes, int& ran, int& accepted) {
    ImportedModel m; std::string err;
    ++ran;
    if (ReadObjBuffer(bytes, "", m, &err)) { ++accepted; CHECK(Sane(m)); }
    if (ReadStlBuffer(bytes, m, &err))     { ++accepted; CHECK(Sane(m)); }
    if (ReadPlyBuffer(bytes, m, &err))     { ++accepted; CHECK(Sane(m)); }
}

int main() {
    RUN_SUITE("model_readers_fuzz");

    // ---------------- Malformed input ----------------
    {
        ImportedModel m; std::string err;
        // OBJ
        CHECK(!ReadObjBuffer("", "", m, &err));
        CHECK(!ReadObjBuffer("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 9\n", "", m, &err));
        CHECK(!ReadObjBuffer("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 -7\n", "", m, &err));
        CHECK(!ReadObjBuffer("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1/5 2/5 3/5\n", "", m, &err));      // vt index past the list
        CHECK(!ReadObjBuffer("v 0 0 nan\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", "", m, &err));
        CHECK(!ReadObjBuffer("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 99999999999999999999\n", "", m, &err));
        CHECK(!ReadObjBuffer("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2\n", "", m, &err));                // degenerate face = no faces
        CHECK(!ReadObjBuffer("v a b c\nf 1 1 1\n", "", m, &err));
        CHECK(!ReadObjFile(kDir + "does_not_exist.obj", m, &err));
        // STL
        CHECK(!ReadStlBuffer("", m, &err));
        CHECK(!ReadStlBuffer("solid x\n", m, &err));
        CHECK(!ReadStlBuffer("solid x\nfacet normal 0 0 1\n outer loop\n vertex 0 0 0\n vertex 1 0 0\n endloop\nendfacet\nendsolid x\n", m, &err));   // 2 vertices
        CHECK(!ReadStlBuffer("solid x\nfacet normal 0 0 1\n outer loop\n vertex 0 0 0\n vertex 1 0 0\n vertex 0 1 0\n vertex 1 1 1\n endloop\nendfacet\nendsolid x\n", m, &err));   // 4 vertices
        CHECK(!ReadStlBuffer("solid x\nfacet normal 0 0 1\n outer loop\n vertex 0 0 nan\n vertex 1 0 0\n vertex 0 1 0\n endloop\nendfacet\nendsolid x\n", m, &err));
        CHECK(!ReadStlBuffer("solid x\nfacet normal 0 0 1\n outer loop\n vertex 0 0 0\n vertex 1 0\n", m, &err));                             // truncated
        CHECK(!ReadStlBuffer("solid x\nfacet normal 0 0 1\n outer loop\n garbage\n", m, &err));
        CHECK(!ReadStlBuffer(std::string(200, 'q'), m, &err));                                   // neither solid nor binary-sized
        std::string bin = Slurp(kDir + "cube_binary.stl");
        CHECK(!ReadStlBuffer(bin.substr(0, bin.size() - 3), m, &err));                           // truncated binary (count > data)
        std::string huge = bin; huge[80] = huge[81] = huge[82] = huge[83] = (char)0xFF;
        CHECK(!ReadStlBuffer(huge, m, &err));                                                    // claims 4 billion triangles
        std::string zero = bin; zero[80] = zero[81] = zero[82] = zero[83] = 0;
        CHECK(!ReadStlBuffer(zero.substr(0, 84), m, &err));                                      // header only
        std::string nanb = bin; { float f = std::nanf(""); std::memcpy(&nanb[84 + 12], &f, 4); }
        CHECK(!ReadStlBuffer(nanb, m, &err));
        // PLY
        CHECK(!ReadPlyBuffer("", m, &err));
        CHECK(!ReadPlyBuffer("not a ply\n", m, &err));
        CHECK(!ReadPlyBuffer("ply\nformat ascii 1.0\nelement vertex 1\nproperty float x\n", m, &err));                // no end_header
        CHECK(!ReadPlyBuffer("ply\nend_header\n", m, &err));                                                         // no format
        CHECK(!ReadPlyBuffer("ply\nformat ascii 1.0\nelement vertex 1\nproperty banana x\nend_header\n0\n", m, &err));
        CHECK(!ReadPlyBuffer("ply\nformat ebcdic 1.0\nend_header\n", m, &err));
        CHECK(!ReadPlyBuffer("ply\nformat ascii 1.0\nproperty float x\nend_header\n", m, &err));                     // property before element
        CHECK(!ReadPlyBuffer("ply\nformat ascii 1.0\nelement vertex -3\nproperty float x\nend_header\n", m, &err));
        CHECK(!ReadPlyBuffer("ply\nformat ascii 1.0\nelement vertex 4294967295\nproperty float x\nproperty float y\nproperty float z\nend_header\n0 0 0\n", m, &err));  // count past the file
        CHECK(!ReadPlyBuffer("ply\nformat binary_little_endian 1.0\nelement vertex 100000000\nproperty float x\nproperty float y\nproperty float z\nend_header\n", m, &err));
        CHECK(!ReadPlyBuffer("ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\nelement face 1\nproperty list uchar int vertex_indices\nend_header\n0 0 0\n1 0 0\n0 1 0\n3 0 1 7\n", m, &err));   // index range
        CHECK(!ReadPlyBuffer("ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\nelement face 1\nproperty list uchar int vertex_indices\nend_header\n0 0 0\n1 0 0\n0 1 0\n3 0 1 -1\n", m, &err));  // negative index
        CHECK(!ReadPlyBuffer("ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\nelement face 1\nproperty list uchar int vertex_indices\nend_header\n0 0 0\n1 0 0\n0 1 0\n3 0 1\n", m, &err));      // truncated face
        CHECK(!ReadPlyBuffer("ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\nend_header\n0 0 0\n1 0 0\n0 1 0\n", m, &err));                                                        // no faces
        CHECK(!ReadPlyBuffer("ply\nformat ascii 1.0\nelement vertex 1\nproperty float x\nproperty float y\nend_header\n0 0\n", m, &err));                                                                                         // no z
        CHECK(!ReadPlyBuffer("ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\nelement face 1\nproperty list uchar int vertex_indices\nend_header\n0 0 0\n1 0 nan\n0 1 0\n3 0 1 2\n", m, &err));    // NaN vertex
        CHECK(!ReadPlyBuffer("ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\nelement face 1\nproperty list int int vertex_indices\nend_header\n0 0 0\n1 0 0\n0 1 0\n2000000000 0 1\n", m, &err)); // list longer than allowed
        std::string le = Slurp(kDir + "cube_le.ply");
        CHECK(!ReadPlyBuffer(le.substr(0, le.size() - 5), m, &err));                              // truncated binary
        std::string hdr = "ply\nformat ascii 1.0\n"; for (int i = 0; i < 5000; ++i) hdr += "comment x\n";
        CHECK(!ReadPlyBuffer(hdr + "end_header\n", m, &err));                                     // header flood
        // Each failure leaves the result empty and says why.
        CHECK(m.parts.empty());
        CHECK(!ReadPlyBuffer("ply\n", m, &err) && !err.empty());
    }

    // ---------------- Fuzz sweep ----------------
    {
        std::vector<std::string> seeds;
        for (const char* f : {"cube.obj", "cube.mtl", "plain.obj", "cube_ascii.stl", "cube_binary.stl", "two_solids.stl",
                              "cube_ascii.ply", "cube_le.ply", "cube_be.ply", "tetra_plain.ply"})
            seeds.push_back(Slurp(kDir + f));
        int ran = 0, accepted = 0;
        for (const std::string& s : seeds) {
            FuzzOne(s, ran, accepted);
            for (std::size_t n = 0; n < s.size(); ++n) FuzzOne(s.substr(0, n), ran, accepted);       // every truncation
        }
        FuzzRng r{0x9E3779B97F4A7C15ull};
        int iters = 4000;
        if (const char* e = std::getenv("OKAY_FUZZ_ITERS")) iters = std::atoi(e);   // e.g. 500000 for a long sanitizer run
        for (int i = 0; i < iters; ++i) FuzzOne(Mutate(seeds[(std::size_t)(r.Next() % seeds.size())], r), ran, accepted);
        // Pure noise, and long runs of one byte.
        for (int i = 0; i < 300; ++i) { std::string n((std::size_t)(r.Next() % 400), 0); for (char& c : n) c = (char)r.Next(); FuzzOne(n, ran, accepted); }
        for (int c : std::initializer_list<int>{0, 0xFF, 'v', 'f', ' ', '\n', '-', '9'}) FuzzOne(std::string(4096, (char)c), ran, accepted);
        std::printf("  fuzz: %d inputs, %d accepted\n", ran, accepted);
        CHECK(ran > 8000);
        CHECK(accepted > 0);
    }

    TEST_MAIN_RESULT();
}
