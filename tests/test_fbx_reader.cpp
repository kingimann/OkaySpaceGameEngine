#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/SkinnedMesh.hpp"
#include "okay/Render/FbxReader.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Render/ModelImportProblem.hpp"
#include <set>
#include <map>
#include <sstream>
#include <iostream>
#include <tuple>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// okay::fbx (our own FBX reader, the only FBX reader) against hand-computed values, and every fixture in
// tests/assets/fbx (and, with OKAY_MODEL_CORPUS, every real FBX file) through the importer against the values recorded
// when ufbx and ours last agreed (see ExpectedImportTests). The fixtures are made by scripts/gen_fbx_fixtures.py.
using namespace okay;
namespace fs = std::filesystem;

static std::string Fx(const char* name) { return std::string(OKAY_SOURCE_DIR) + "/tests/assets/fbx/" + name; }
static std::vector<std::uint8_t> Hex(const char* h) {
    std::vector<std::uint8_t> o;
    for (const char* p = h; p[0] && p[1]; p += 2) { unsigned v; std::sscanf(p, "%2x", &v); o.push_back((std::uint8_t)v); }
    return o;
}
static bool Near(double a, double b, double eps = 1e-5) { return std::fabs(a - b) <= eps; }

// A baked track played at time t as the engine plays it: linear between the two keys around t (a key at t exactly is
// returned as it is); for rotations the second key's sign is matched to the first and the blend normalised. The clips
// of our reader have keys at the file's own key times plus the ones it adds (docs/content/model_readers.md (FBX part), "Animation keys"),
// so a check at a time needs this rather than a sample index.
template <class K> static K KeyAt(const std::vector<K>& keys, double t) {
    std::size_t i = 0;
    while (i + 1 < keys.size() && keys[i + 1].time <= t) ++i;
    if (keys[i].time == t || i + 1 >= keys.size() || t <= keys[i].time) return keys[i];
    const K& a = keys[i]; const K& b = keys[i + 1];
    const double u = (t - a.time) / (b.time - a.time);
    const int n = (int)(sizeof(a.v) / sizeof(a.v[0]));
    double sg = 1;
    if (n == 4) { double d = 0; for (int k = 0; k < 4; ++k) d += a.v[k] * b.v[k]; if (d < 0) sg = -1; }
    K r; r.time = t; double l = 0;
    for (int k = 0; k < n; ++k) { r.v[k] = a.v[k] + (sg * b.v[k] - a.v[k]) * u; l += r.v[k] * r.v[k]; }
    if (n == 4 && l > 0) for (int k = 0; k < 4; ++k) r.v[k] /= std::sqrt(l);
    return r;
}

static double TriArea(const fbx::Corner* a) {
    const double ux = a[1].p[0] - a[0].p[0], uy = a[1].p[1] - a[0].p[1], uz = a[1].p[2] - a[0].p[2];
    const double vx = a[2].p[0] - a[0].p[0], vy = a[2].p[1] - a[0].p[1], vz = a[2].p[2] - a[0].p[2];
    const double cx = uy * vz - uz * vy, cy = uz * vx - ux * vz, cz = ux * vy - uy * vx;
    return 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
}
static double MeshArea(const fbx::Mesh& m) {
    double s = 0;
    for (const fbx::Part& p : m.parts) { const std::vector<fbx::Corner> cs = p.ExpandCorners(); for (std::size_t i = 0; i + 2 < cs.size(); i += 3) s += TriArea(&cs[i]); }
    return s;
}
/// The reader, then the import steps: the triangulation of the meshes' polygons (fbx::BuildParts) and the clips' keys
/// (fbx::BuildClips), which the checks on parts and keys need.
static bool LoadBuilt(const std::string& path, fbx::Scene& s, std::string* err, const fbx::BuildOptions& o = fbx::BuildOptions()) {
    return fbx::LoadFile(path, s, err, o) && fbx::BuildParts(s, err) && fbx::BuildClips(s, err);
}
static const fbx::SceneNode* FindNode(const fbx::Scene& s, const char* n) {
    for (const auto& x : s.nodes) if (x.name == n) return &x;
    return nullptr;
}

static void InflateTests() {
    RUN_SUITE("fbx inflate");
    std::vector<std::uint8_t> out;
    // stored block
    CHECK(fbx::Inflate(Hex("7801010300fcff616263024d0127").data(), 14, out, 100) && out.size() == 3 && out[0] == 'a' && out[2] == 'c');
    // fixed Huffman with back references
    {
        auto z = Hex("78da4bcc48c94e4fce4a03a2a4ccd4c40c20020ba4430532a102d9508124a8400a54200d2a003526096a4c4ae2a8b983d65c00fb54c738");
        CHECK(fbx::Inflate(z.data(), z.size(), out, 1000));
        CHECK(out.size() == 500);
        bool same = out.size() == 500;
        for (std::size_t i = 0; i < out.size() && same; ++i) same = out[i] == (std::uint8_t)(((i * 7 + (i >> 3)) % 11) + 97);
        CHECK(same);
    }
    // dynamic Huffman
    {
        auto z = Hex("78da1dd455b6ab400044519ce0121a0dd2b8bb341de63fae97fb06503f7b9d5544c20d5eefdf05380a4f6b7c85c6a7a3f2343d8e6b56c0bcdde97e7ba4c0cadb5b05351904e30b12494e840fdc3cca79427e8e4da8e01e927b0c9473ee3b53e279413d8f8b783d02d082b8fbea45d03e6098647f3592276694561e1d4a5b7d7651f1a07c4b57e1484fe329b22c7cd7d30c3b56753b6a50b7513a4c7759cf173125e2f0b0563be523baa2ea3769ddf99b3934dc250ede46c764898760cabc6897b4ab278c3850462d9221da85703783d3cd99e00ed97e739fd2aaed2bb7ce2a788fb125b174ea59864152dc791f98513f821ed468e39568417ac570fea2f51a98b4d9aef54b3b5d6a0875a87ea782c5e5c7a63142b1ef38019415dd4edc74ee2e420917913072ecf83b9f3cfaa0ef6444152a127a9b1a153c686413be6521754d55d9f7a6e960d1ed453d22de96ac6466397fde9560515b592e985c00b775c608aa58bf3293bd0c722e22f68bd2f037d7c175dda478b38cec38d9fa229570e293707a255fa1e0c1a60ce69575fa09b5c23a7270b5b1b39781a52963091368589e6ca8ef4f75e4fd25f8a09e6837398c7ee463fc21b894cee5989f85d97ada8f0e75726d257a2c6255e085b1a99be1806935a2955025371a4830e48792931cfc8911315d6dd611f20b539bc4207f27831d53df32fa2c0c0296c7983c6e467d1809645bbdd1efaa21dc70e3c2cb45c8de6c2c8ed2e8ac1fe1528feccd61c09e7da1bed8b18dc330a914c570122b0aab6d63ac2a9d28bb7ff1feac343b38b89284afd1cfc53994d3f7913b78c8433ce6a1efe17d9ed6ac1c60f90367de94ec56f89441f5083172ff834f27387cca211aa6d77113187fe093299d53ebd9c021f1fd30f21f38d03e5d7d917a86de72b469eda425df7032f64f2d0d80ec9f42ff0ecab302616f6018fce6750b1c4db714d32fddb05c284b04f921eb05c147a3587075c397e248145465f42eb72b470604c2979e7d74de0646633fae374ccb11f508b16f2bdda55fe1184ce39f58ce56a7b739dff2fd2bdc13a179961eb936397b8e7551fe0aa7b9133317f1f04634c5cd2586c184b4621273464870f085add81a9776e54fa3a2e66396b6409c9e45df6799e595a169a697ea969fcd1d92452f5d5f315a5ed1e58458efd594adcd982a3554dfa9f61d34eaf035bcb465f94bf4bacb06e64ddaace48a182359dbf315ccbb5a8d62b05b9b1b5cde936f214ae4da6236f92c1c858d4dfe416914490249502ffd40c4cb128093ce1beb0c0d52b3afef4f52c983dd6bd55add22c5251ae705df5713b268ee541aedebe68337f064d3d75d37e9b14a485e471176b26bbf179a76bd93471011f0350b4b72970a6a22d0041a47a6e1afb1df1c6530abc6bbee775295bcb6c59cdb7330f813935b2222c115bd1c148b7b7e41f5990c0ec7367befcb62ca82a85ccf81a89b5603cd81230594a023dddf0bf99b90d1b9a874ea1274600df8c2a436e5aa03432a3d5d60c7aef33dc30441a11a2e1cf265a34d50dea69e6c4ac9f2e1691f5ca5444cccf62e6fe1d27e4180fb5422d1feffb7d9a62caa01564b3dff03fdb2b004");
        CHECK(fbx::Inflate(z.data(), z.size(), out, 5000));
        bool same = out.size() == 1500;
        for (std::size_t i = 0; i < out.size() && same; ++i) same = out[i] == (std::uint8_t)((i * i * 31 + i / 3 + (i % 7) * (i % 5)) % 61);
        CHECK(same);
        CHECK(!fbx::Inflate(z.data(), z.size(), out, 100));           // output limit respected
        z.resize(z.size() / 2);
        CHECK(!fbx::Inflate(z.data(), z.size(), out, 5000));          // truncated stream
        z = Hex("78da1dd455b6ab400044519ce0121a0dd2b8bb341de63fae97fb06503f7b9d5544c20d5eefdf05380a4f6b7c85c6a7a3f2343d8e6b56c0bcdde97e7ba4c0cadb5b05351904e30b12494e840fdc3cca79427e8e4da8e01e927b0c9473ee3b53e279413d8f8b783d02d082b8fbea45d03e6098647f3592276694561e1d4a5b7d7651f1a07c4b57e1484fe329b22c7cd7d30c3b56753b6a50b7513a4c7759cf173125e2f0b0563be523baa2ea3769ddf99b3934dc250ede46c764898760cabc6897b4ab278c3850462d9221da85703783d3cd99e00ed97e739fd2aaed2bb7ce2a788fb125b174ea59864152dc791f98513f821ed468e39568417ac570fea2f51a98b4d9aef54b3b5d6a0875a87ea782c5e5c7a63142b1ef38019415dd4edc74ee2e420917913072ecf83b9f3cfaa0ef6444152a127a9b1a153c686413be6521754d55d9f7a6e960d1ed453d22de96ac6466397fde9560515b592e985c00b775c608aa58bf3293bd0c722e22f68bd2f037d7c175dda478b38cec38d9fa229570e293707a255fa1e0c1a60ce69575fa09b5c23a7270b5b1b39781a52963091368589e6ca8ef4f75e4fd25f8a09e6837398c7ee463fc21b894cee5989f85d97ada8f0e75726d257a2c6255e085b1a99be1806935a2955025371a4830e48792931cfc8911315d6dd611f20b539bc4207f27831d53df32fa2c0c0296c7983c6e467d1809645bbdd1efaa21dc70e3c2cb45c8de6c2c8ed2e8ac1fe1528feccd61c09e7da1bed8b18dc330a914c570122b0aab6d63ac2a9d28bb7ff1feac343b38b89284afd1cfc53994d3f7913b78c8433ce6a1efe17d9ed6ac1c60f90367de94ec56f89441f5083172ff834f27387cca211aa6d77113187fe093299d53ebd9c021f1fd30f21f38d03e5d7d917a86de72b469eda425df7032f64f2d0d80ec9f42ff0ecab302616f6018fce6750b1c4db714d32fddb05c284b04f921eb05c147a3587075c397e248145465f42eb72b470604c2979e7d74de0646633fae374ccb11f508b16f2bdda55fe1184ce39f58ce56a7b739dff2fd2bdc13a179961eb936397b8e7551fe0aa7b9133317f1f04634c5cd2586c184b4621273464870f085add81a9776e54fa3a2e66396b6409c9e45df6799e595a169a697ea969fcd1d92452f5d5f315a5ed1e58458efd594adcd982a3554dfa9f61d34eaf035bcb465f94bf4bacb06e64ddaace48a182359dbf315ccbb5a8d62b05b9b1b5cde936f214ae4da6236f92c1c858d4dfe416914490249502ffd40c4cb128093ce1beb0c0d52b3afef4f52c983dd6bd55add22c5251ae705df5713b268ee541aedebe68337f064d3d75d37e9b14a485e471176b26bbf179a76bd93471011f0350b4b72970a6a22d0041a47a6e1afb1df1c6530abc6bbee775295bcb6c59cdb7330f813935b2222c115bd1c148b7b7e41f5990c0ec7367befcb62ca82a85ccf81a89b5603cd81230594a023dddf0bf99b90d1b9a874ea1274600df8c2a436e5aa03432a3d5d60c7aef33dc30441a11a2e1cf265a34d50dea69e6c4ac9f2e1691f5ca5444cccf62e6fe1d27e4180fb5422d1feffb7d9a62caa01564b3dff03fdb2b004"); z[2] ^= 0x04;                              // bad block type bits
        fbx::Inflate(z.data(), z.size(), out, 5000);                   // must not crash either way
    }
    CHECK(!fbx::Inflate(nullptr, 0, out, 10));
}

static void DirectTests() {
    RUN_SUITE("fbx scene: cube (Z up, cm), binary vs ascii");
    fbx::Scene b, a;
    std::string err;
    CHECK(LoadBuilt(Fx("cube_zup_cm_bin.fbx"), b, &err));
    CHECK(LoadBuilt(Fx("cube_zup_cm_ascii.fbx"), a, &err));
    CHECK(b.nodes.size() == 1 && a.nodes.size() == 1);
    if (b.nodes.size() == 1 && a.nodes.size() == 1) {
        const fbx::SceneNode& n = b.nodes[0];
        CHECK(n.name == "Cube" && a.nodes[0].name == "Cube");
        // (x, y, z) in Z-up/-Y-front centimeters -> (x, z, -y) * 0.01
        CHECK(Near(n.translation[0], 0.10) && Near(n.translation[1], 0.30) && Near(n.translation[2], -0.20));
        CHECK(Near(b.settings.appliedScale, 0.01));
        CHECK(n.mesh == 0 && a.nodes[0].mesh == 0);
        const fbx::Mesh& m = b.meshes[0];
        CHECK(m.triangleCount() == 12 && a.meshes[0].triangleCount() == 12);
        CHECK(m.hasUV && m.hasNormals && m.parts.size() == 1);
        float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
        for (const fbx::Corner& c : m.parts[0].ExpandCorners()) for (int k = 0; k < 3; ++k) { lo[k] = std::min(lo[k], c.p[k]); hi[k] = std::max(hi[k], c.p[k]); }
        for (int k = 0; k < 3; ++k) { CHECK(Near(lo[k], -0.01, 1e-6)); CHECK(Near(hi[k], 0.01, 1e-6)); }
        CHECK(Near(MeshArea(m), 6 * 0.02 * 0.02, 1e-7));
        // Like ufbx: geometry and normals keep the file's axes, the root node carries the Y-up rotation.
        // Local point (0,0,1 cm) of the Z-up node: T + R_y(30) S (0,0,1) = (10.5, 20, 30.866) cm -> (x, z, -y) * 0.01.
        const double in[3] = {0, 0, 0.01};
        const fbx::Mat& g2w = n.geometryToWorld;
        CHECK(Near(g2w.m[0] * in[0] + g2w.m[1] * in[1] + g2w.m[2] * in[2] + g2w.m[3], 0.105, 1e-6));
        CHECK(Near(g2w.m[4] * in[0] + g2w.m[5] * in[1] + g2w.m[6] * in[2] + g2w.m[7], 0.30866025, 1e-6));
        CHECK(Near(g2w.m[8] * in[0] + g2w.m[9] * in[1] + g2w.m[10] * in[2] + g2w.m[11], -0.2, 1e-6));
        bool rawNormal = false;
        for (const fbx::Corner& c : m.parts[0].ExpandCorners()) if (Near(c.n[2], 1.0, 1e-4)) rawNormal = true;
        CHECK(rawNormal);
        // ascii and binary agree corner by corner
        const auto ca = a.meshes[0].parts[0].ExpandCorners(); const auto cb = m.parts[0].ExpandCorners();
        CHECK(ca.size() == cb.size());
        for (std::size_t i = 0; i < std::min(ca.size(), cb.size()); ++i) { CHECK(Near(ca[i].p[0], cb[i].p[0], 1e-7) && Near(ca[i].uv[1], cb[i].uv[1], 1e-7) && Near(ca[i].n[2], cb[i].n[2], 1e-6)); }
        // material, texture reference and embedded bytes
        CHECK(b.materials.size() == 1 && b.materials[0].name == "Red" && b.materials[0].hasDiffuse && Near(b.materials[0].diffuse[0], 0.9));
        CHECK(b.materials[0].hasEmissive && b.materials[0].hasRoughness);
        CHECK(b.materials[0].diffuseTexture == 0);
        CHECK(b.textures.size() == 1);
        if (b.textures.size() == 1) { CHECK(b.textures[0].fileName == "C:\\art\\red.png"); CHECK(b.textures[0].relativeFileName == "red.png"); }
        CHECK(b.textures[0].content.size() > 20 && b.textures[0].content[1] == 'P');
        CHECK(a.textures.size() == 1 && a.textures[0].content.empty());
        CHECK(m.materials.size() == 1 && m.materials[0] == 0);
    }

    RUN_SUITE("fbx scene: Y up, 1 unit = 1 m (7.5, 64-bit offsets)");
    fbx::Scene y;
    CHECK(fbx::LoadFile(Fx("cube_yup_m_bin75.fbx"), y, &err));
    if (!y.nodes.empty()) {
        CHECK(Near(y.settings.appliedScale, 1.0));
        CHECK(Near(y.nodes[0].translation[0], 10) && Near(y.nodes[0].translation[1], 20) && Near(y.nodes[0].translation[2], 30));
        CHECK(Near(y.nodes[0].scale[1], 2.0, 1e-6));
    }

    RUN_SUITE("fbx scene: n-gons, ear clipping, per-polygon materials");
    fbx::Scene g;
    CHECK(LoadBuilt(Fx("ngon_bin.fbx"), g, &err));
    if (!g.meshes.empty()) {
        const fbx::Mesh& m = g.meshes[0];
        // L hexagon (area 3) + pentagon + triangle (0.5)
        auto shoelace = [](std::vector<std::pair<double, double>> p) { double s = 0; for (std::size_t i = 0; i < p.size(); ++i) { auto& a = p[i]; auto& b = p[(i + 1) % p.size()]; s += a.first * b.second - b.first * a.second; } return std::fabs(s) * 0.5; };
        const double pent = shoelace({{3, 0}, {4, 0}, {4.5, 1}, {3.5, 1.8}, {2.8, 1}});
        CHECK(Near(MeshArea(m), (3.0 + pent + 0.5) * 1e-4, 1e-9));   // file is in cm
        CHECK(m.triangleCount() == 4 + 3 + 1);
        CHECK(m.parts.size() == 2 && m.parts[0].slot == 0 && m.parts[1].slot == 1);
        if (m.parts.size() == 2) CHECK(m.parts[0].cornerCount() / 3 == 4 && m.parts[1].cornerCount() / 3 == 4);
        bool allUp = true;   // concave polygon: every triangle must keep the +Z winding
        for (const fbx::Part& p : m.parts) for (std::size_t i = 0; i + 2 < p.cornerCount(); i += 3) {
            const fbx::Corner c[3] = {p.corner(i), p.corner(i + 1), p.corner(i + 2)};
            const double z = (c[1].p[0] - c[0].p[0]) * (c[2].p[1] - c[0].p[1]) - (c[1].p[1] - c[0].p[1]) * (c[2].p[0] - c[0].p[0]);
            if (z <= 0) allUp = false;
        }
        CHECK(allUp);
        CHECK(m.materials.size() == 2 && g.materials[(std::size_t)m.materials[1]].name == "B");
        CHECK(!m.hasUV);
    }

    RUN_SUITE("fbx scene: generated normals, hierarchy, bad polygons");
    fbx::Scene pl;
    CHECK(LoadBuilt(Fx("plain_bin.fbx"), pl, &err));
    CHECK(pl.nodes.size() == 2 && pl.roots.size() == 1);
    if (pl.nodes.size() == 2) {
        CHECK(pl.nodes[0].name == "Parent" && pl.nodes[1].name == "Pyr" && pl.nodes[1].parent == 0);
        CHECK(Near(pl.nodes[1].world.m[3], 0.01, 1e-9) && Near(pl.nodes[1].world.m[7], 0.02, 1e-9));
        CHECK(Near(pl.nodes[1].scale[0], 2.0, 1e-6));
        CHECK(pl.nodes[1].mesh == 0 && pl.meshes[0].triangleCount() == 3);   // the 2-corner polygon was skipped
        bool unit = true;
        for (const fbx::Corner& c : pl.meshes[0].parts[0].ExpandCorners()) { const double l = std::sqrt(c.n[0] * c.n[0] + c.n[1] * c.n[1] + c.n[2] * c.n[2]); if (!Near(l, 1.0, 1e-4)) unit = false; }
        CHECK(unit);
        CHECK(!pl.meshes[0].hasUV);
    }

    RUN_SUITE("fbx scene: rig (skin, blend shape, animation)");
    for (const char* name : {"rig_bin75.fbx", "rig_ascii.fbx"}) {
        fbx::Scene r;
        CHECK(LoadBuilt(Fx(name), r, &err));
        CHECK(r.nodes.size() == 4);
        const fbx::SceneNode* tube = FindNode(r, "Tube");
        CHECK(tube && tube->mesh == 0);
        if (r.meshes.empty()) continue;
        const fbx::Mesh& m = r.meshes[0];
        CHECK(m.clusters.size() == 3 && m.skinWeights.size() == 16);
        CHECK(m.triangleCount() == 24);
        for (std::size_t i = 0; i < m.clusters.size(); ++i) {
            CHECK(m.clusters[i].boneNode >= 0);
            if (m.clusters[i].boneNode >= 0) CHECK(r.nodes[(std::size_t)m.clusters[i].boneNode].name == "Bone" + std::to_string(i));
            CHECK(Near(m.clusters[i].geometryToBone.m[7], -(double)i * 0.01, 1e-9));   // mesh node relative to a bone at y = i (cm -> m)
        }
        CHECK(m.skinWeights[0].size() == 1 && m.skinWeights[4].size() == 2 && Near(m.skinWeights[4][0].weight, 0.5));
        CHECK(m.channels.size() == 1 && Near(m.channels[0].weight, 0.25) && m.channels[0].indices.size() == 4 && m.channels[0].dp.size() == 12 && m.channels[0].dn.size() == 12);
        if (m.channels.size() == 1) CHECK(m.channels[0].name == "Fat");
        CHECK(r.clips.size() == 1);
        if (r.clips.size() != 1) continue;
        const fbx::Clip& c = r.clips[0];
        CHECK(c.name == "Take 001" && Near(c.duration, 2.0));
        CHECK(c.nodes.size() == 3 && c.blends.size() == 1);
        for (const fbx::NodeAnim& na : c.nodes) {
            // keys at the file's key times (0, 0.5, 1, 1.5, 2 s at most here) plus the added ones: far fewer than the
            // 61 samples of the 30 Hz bake, the same count on every channel, from 0 to the end of the take
            CHECK(!na.translation.empty() && na.translation.size() == na.rotation.size() && na.translation.size() == na.scale.size());
            // (Bone1's cubic bends more than 30 samples a second can follow within 0.05 degrees: it gets more keys)
            if (r.nodes[(std::size_t)na.node].name != "Bone1") CHECK(na.translation.size() < 61);
            if (na.translation.empty()) continue;
            CHECK(na.translation.front().time == 0.0 && Near(na.translation.back().time, 2.0, 1e-12));
            const std::string& nm = r.nodes[(std::size_t)na.node].name;
            if (nm == "Bone1") {   // PreRotation 10 deg + animated Z rotation 45 deg at t = 0.5 -> 55 deg about Z
                bool keyed = false; for (const fbx::Key4& k : na.rotation) keyed |= k.time == 0.5;   // a file key time
                CHECK(keyed);
                const fbx::Key4 k = KeyAt(na.rotation, 0.5);
                CHECK(Near(std::fabs(k.v[2]), std::sin(27.5 * 3.14159265358979 / 180.0), 1e-6) && Near(std::fabs(k.v[3]), std::cos(27.5 * 3.14159265358979 / 180.0), 1e-6));
                const fbx::Key4& k0 = na.rotation[0];   // t = 0: pre-rotation only
                CHECK(Near(std::fabs(k0.v[2]), std::sin(5.0 * 3.14159265358979 / 180.0), 1e-6));
            } else if (nm == "Bone2") {   // linear 1.0 -> 1.5 over 2 s: two keys, played linearly
                CHECK(na.translation.size() == 2);
                CHECK(Near(KeyAt(na.translation, 1.0).v[1], 0.0125, 1e-8) && Near(KeyAt(na.translation, 2.0).v[1], 0.015, 1e-8));
            } else if (nm == "Bone0") {   // constant steps: 1,1,2 (the jump at 2 s gets a key just before it)
                CHECK(Near(KeyAt(na.scale, 20.0 / 30.0).v[0], 1.0, 1e-9) && Near(KeyAt(na.scale, 2.0).v[0], 2.0, 1e-9));
                CHECK(na.scale.size() >= 3 && Near(na.scale[na.scale.size() - 2].v[0], 1.0, 1e-9) && na.scale[na.scale.size() - 2].time > 1.999 && na.scale[na.scale.size() - 2].time < 2.0 - 1e-4);
            }
        }
        if (c.blends.size() == 1) {   // linear 0 -> 100 over 2 s: its two keys
            CHECK(c.blends[0].mesh == 0 && c.blends[0].channel == 0);
            CHECK(c.blends[0].percent.size() == 2);
            CHECK(Near(KeyAt(c.blends[0].percent, 1.0).v[0], 50.0, 1e-5) && Near(KeyAt(c.blends[0].percent, 2.0).v[0], 100.0, 1e-5));
        }
    }

    RUN_SUITE("fbx scene: pivots, offsets, pre/post rotation, geometric transform");
    fbx::Scene pv;
    CHECK(fbx::LoadFile(Fx("pivot_bin.fbx"), pv, &err));
    if (pv.nodes.size() == 1) {
        // Independent evaluation of T Roff Rp Rpre R Rpost^-1 Rp^-1 Soff Sp S Sp^-1 on a point.
        auto rot = [](int ax, double deg, const double* p, double* o) {
            const double a = deg * 3.14159265358979323846 / 180.0, c = std::cos(a), s = std::sin(a);
            const double x = p[0], y = p[1], z = p[2];
            if (ax == 0) { o[0] = x; o[1] = c * y - s * z; o[2] = s * y + c * z; }
            else if (ax == 1) { o[0] = c * x + s * z; o[1] = y; o[2] = -s * x + c * z; }
            else { o[0] = c * x - s * y; o[1] = s * x + c * y; o[2] = z; }
        };
        double p[3] = {0.3, -0.7, 0.9}, q[3];
        // inner to outer: geometric (scale z2, translate 0.5) then Sp^-1, S, Sp, Soff, Rp^-1, Rpost^-1, R(ZXY), Rpre, Rp, Roff, T
        p[2] *= 2; p[0] += 0.5;
        p[0] -= 0.2; p[1] *= 2; p[0] += 0.2; p[2] += 0.3;
        p[0] -= 0.5; p[1] -= 0.25;
        rot(0, -5, p, q); for (int i = 0; i < 3; ++i) p[i] = q[i];
        // R, order ZXY (4): Z first... "first letter applied first" -> Z, then X, then Y
        rot(2, 35, p, q); rot(0, 15, q, p); rot(1, 25, p, q);
        rot(2, 20, q, p);
        p[0] += 0.5; p[1] += 0.25; p[1] += 0.1;
        p[0] += 1; p[1] += 2; p[2] += 3;
        const fbx::Mat& g2w = pv.nodes[0].geometryToWorld;
        const double in[3] = {0.003, -0.007, 0.009};   // geometry space is in meters (file: cm)
        const double ox = g2w.m[0] * in[0] + g2w.m[1] * in[1] + g2w.m[2] * in[2] + g2w.m[3];
        const double oy = g2w.m[4] * in[0] + g2w.m[5] * in[1] + g2w.m[6] * in[2] + g2w.m[7];
        const double oz = g2w.m[8] * in[0] + g2w.m[9] * in[1] + g2w.m[10] * in[2] + g2w.m[11];
        // file is cm: meters = cm * 0.01
        CHECK(Near(ox, p[0] * 0.01, 1e-9) && Near(oy, p[1] * 0.01, 1e-9) && Near(oz, p[2] * 0.01, 1e-9));
    } else CHECK(false);

    // Found with real files. A bridge polygon (a square with a hole written as one polygon, corners repeated):
    // ear clipping must not give up on the coincident corners; the area is 16 - 4.
    RUN_SUITE("fbx scene: bridge polygon, takes with their own time span, byte-array property");
    {
        fbx::Scene br;
        CHECK(LoadBuilt(Fx("bridge_bin.fbx"), br, &err));
        if (!br.meshes.empty()) CHECK(Near(MeshArea(br.meshes[0]), 12.0 * 0.0001, 1e-9));   // centimetres to metres: x 0.01 on both axes
    }
    {
        // three takes over one set of curves (x: 0 at t=0, 10 at t=1, 0 at t=2): "first" plays [0, 1],
        // "second" [1, 2], "whole" has no span of its own. The header holds a 'c' (byte array) thumbnail.
        fbx::Scene tk;
        CHECK(LoadBuilt(Fx("takes_bin.fbx"), tk, &err));
        CHECK(tk.clips.size() == 3);
        if (tk.clips.size() == 3) {
            const double want[3] = {1.0, 1.0, 2.0};
            for (int c = 0; c < 3; ++c) {
                CHECK(Near(tk.clips[(std::size_t)c].duration, want[c], 1e-9));
                CHECK(tk.clips[(std::size_t)c].nodes.size() == 1);
                if (tk.clips[(std::size_t)c].nodes.size() != 1) continue;
                const auto& keys = tk.clips[(std::size_t)c].nodes[0].translation;
                CHECK(!keys.empty() && Near(keys.front().time, 0.0));
                CHECK(Near(keys.back().time, want[c], 1e-6));
                // x translation in metres: 10 cm at the peak; sample at the middle of each clip
                auto at = [&](double t) { double best = 1e9, v = 0; for (const auto& k : keys) if (std::fabs(k.time - t) < best) { best = std::fabs(k.time - t); v = k.v[0]; } return v; };
                if (c == 0) { CHECK(Near(at(0.0), 0.0, 1e-9)); CHECK(Near(at(1.0), 0.10, 1e-9)); }       // rises 0 -> 10 cm
                if (c == 1) { CHECK(Near(at(0.0), 0.10, 1e-9)); CHECK(Near(at(1.0), 0.0, 1e-9)); }       // starts at the peak, falls back
                if (c == 2) { CHECK(Near(at(1.0), 0.10, 1e-9)); CHECK(Near(at(2.0), 0.0, 1e-9)); }
            }
        }
    }
}

// ---- what the importer made ---------------------------------------------------------------
struct ImportStats {
    int objects = 0, meshes = 0, skinned = 0, joints = 0, verts = 0, tris = 0, clips = 0, tracks = 0, blend = 0;
    double lo[3] = {1e30, 1e30, 1e30}, hi[3] = {-1e30, -1e30, -1e30}, area = 0;
    std::vector<std::string> names;
};
static ImportStats Collect(Scene& s, GameObject* root) {
    ImportStats st;
    for (const auto& up : s.Objects()) {
        GameObject* g = up.get();
        if (!g->IsSelfOrDescendantOf(root)) continue;
        ++st.objects; st.names.push_back(g->name);
        if (auto* mr = g->GetComponent<MeshRenderer>()) {
            ++st.meshes; st.verts += (int)mr->mesh.vertices.size(); st.tris += (int)mr->mesh.triangles.size() / 3;
            const Mat4 w = g->transform->LocalToWorldMatrix();
            for (const Vec3& v : mr->mesh.vertices) {
                const Vec3 p = w.MultiplyPoint(v);
                const double c[3] = {p.x, p.y, p.z};
                for (int k = 0; k < 3; ++k) { st.lo[k] = std::min(st.lo[k], c[k]); st.hi[k] = std::max(st.hi[k], c[k]); }
            }
            for (std::size_t i = 0; i + 2 < mr->mesh.triangles.size(); i += 3) {
                const Vec3 a = w.MultiplyPoint(mr->mesh.vertices[(std::size_t)mr->mesh.triangles[i]]), b = w.MultiplyPoint(mr->mesh.vertices[(std::size_t)mr->mesh.triangles[i + 1]]), c = w.MultiplyPoint(mr->mesh.vertices[(std::size_t)mr->mesh.triangles[i + 2]]);
                const Vec3 n = Vec3::Cross(b - a, c - a);
                st.area += 0.5 * n.Magnitude();
            }
        }
        if (auto* sm = g->GetComponent<SkinnedMesh>()) { ++st.skinned; st.joints += (int)sm->joints.size(); }
        if (g->GetComponent<BlendShapes>()) ++st.blend;
        if (auto* ma = g->GetComponent<ModelAnimator>()) { st.clips += (int)ma->clips.size(); for (auto& c : ma->clips) st.tracks += (int)c.nodes.size(); }
    }
    return st;

}


// ---- cubic Hermite tangents -------------------------------------------------------------------
// A cubic key carries two slopes (value per second): the right (outgoing) slope of the key itself and
// the "next left" (incoming) slope that the FOLLOWING key arrives with. Between key k and key k+1 the
// curve is the Hermite spline  h00 v0 + h10 dt m0 + h01 v1 + h11 dt m1  with m0 = right slope of key k and
// m1 = next-left slope stored on key k (= incoming slope of key k+1).
static double HermiteRef(double v0, double v1, double m0, double m1, double dt, double u) {
    const double u2 = u * u, u3 = u2 * u;
    return (2 * u3 - 3 * u2 + 1) * v0 + (u3 - 2 * u2 + u) * dt * m0 + (-2 * u3 + 3 * u2) * v1 + (u3 - u2) * dt * m1;
}
// Two keys, 0 -> 10 cm over 1 s, user tangents: right slope `a`, next-left slope `b`.
static long long FloatBits(float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return (long long)u; }
// (An ASCII 7.x file prints KeyAttrDataFloat as the integer bit patterns of its floats.)
static std::string HermiteFbx(double a, double b) {
    char buf[2400];
    std::snprintf(buf, sizeof buf,
        "; FBX 7.4.0 project file\nFBXHeaderExtension:  {\n\tFBXHeaderVersion: 1003\n\tFBXVersion: 7400\n}\n"
        "GlobalSettings:  {\n\tVersion: 1000\n\tProperties70:  {\n\t\tP: \"UpAxis\", \"int\", \"Integer\", \"\", 1\n\t\tP: \"UpAxisSign\", \"int\", \"Integer\", \"\", 1\n"
        "\t\tP: \"FrontAxis\", \"int\", \"Integer\", \"\", 2\n\t\tP: \"FrontAxisSign\", \"int\", \"Integer\", \"\", 1\n"
        "\t\tP: \"CoordAxis\", \"int\", \"Integer\", \"\", 0\n\t\tP: \"CoordAxisSign\", \"int\", \"Integer\", \"\", 1\n"
        "\t\tP: \"UnitScaleFactor\", \"double\", \"Number\", \"\", 1.0\n\t}\n}\n"
        "Objects:  {\n"
        "\tModel: 2001, \"Model::M\", \"Null\" {\n\t\tProperties70:  {\n\t\t\tP: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\", 0.0, 0.0, 0.0\n\t\t}\n\t}\n"
        "\tAnimationStack: 2002, \"AnimStack::Take 001\", \"\" {\n\t}\n"
        "\tAnimationLayer: 2003, \"AnimLayer::BaseLayer\", \"\"\n"
        "\tAnimationCurveNode: 2004, \"AnimCurveNode::T\", \"\" {\n\t\tProperties70:  {\n\t\t\tP: \"d|X\", \"Number\", \"\", \"A\", 0.0\n\t\t\tP: \"d|Y\", \"Number\", \"\", \"A\", 0.0\n\t\t\tP: \"d|Z\", \"Number\", \"\", \"A\", 0.0\n\t\t}\n\t}\n"
        "\tAnimationCurve: 2005, \"AnimCurve::x\", \"\" {\n\t\tDefault: 0.0\n\t\tKeyVer: 4008\n"
        "\t\tKeyTime: *2 {\n\t\t\ta: 0,46186158000\n\t\t}\n\t\tKeyValueFloat: *2 {\n\t\t\ta: 0.0,10.0\n\t\t}\n"
        "\t\tKeyAttrFlags: *1 {\n\t\t\ta: 1032\n\t\t}\n\t\tKeyAttrDataFloat: *4 {\n\t\t\ta: %lld,%lld,0,0\n\t\t}\n"
        "\t\tKeyAttrRefCount: *1 {\n\t\t\ta: 2\n\t\t}\n\t}\n}\n"
        "Connections:  {\n\tC: \"OO\", 2001, 0\n\tC: \"OO\", 2003, 2002\n\tC: \"OP\", 2004, 2001, \"Lcl Translation\"\n"
        "\tC: \"OP\", 2005, 2004, \"d|X\"\n\tC: \"OO\", 2004, 2003\n}\n", FloatBits((float)a), FloatBits((float)b));
    return buf;
}

static void HermiteTests() {
    RUN_SUITE("fbx animation: cubic Hermite uses the right key's outgoing and the next key's incoming slope");
    const fs::path file = fs::temp_directory_path() / ("okay_fbx_hermite_" + std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()) + ".fbx");
    // (right slope, next-left slope) in cm/s. 0 -> 10 cm in 1 s:
    //   (0, 20): 10u^2         (flat start, steep arrival)    u = .3 .5 .7 .9  ->  0.9 2.5 4.9 8.1 cm
    //   (20, 0): 20u - 10u^2   (steep start, flat arrival)                     ->  5.1 7.5 9.1 9.9 cm
    //   (0, 0):  10(3u^2-2u^3) (ease in/out)
    const struct { double a, b; } cases[] = {{0, 20}, {20, 0}, {0, 0}};
    const double us[] = {0.3, 0.5, 0.7, 0.9};
    const double hand[2][4] = {{0.9, 2.5, 4.9, 8.1}, {5.1, 7.5, 9.1, 9.9}};
    for (int ci = 0; ci < 3; ++ci) {
        { std::ofstream f(file, std::ios::binary); const std::string t = HermiteFbx(cases[ci].a, cases[ci].b); f.write(t.data(), (std::streamsize)t.size()); }
        // mode 0 checks the curve evaluation itself: the 30 Hz bake samples at exactly these times. Mode 2 is the clip
        // our reader makes by default (keys at the file's times plus added ones), played between its keys.
        fbx::Scene r, rk; std::string err;
        fbx::BuildOptions bake30; bake30.fileKeys = false;
        CHECK(LoadBuilt(file.string(), r, &err, bake30));
        CHECK(r.clips.size() == 1 && r.clips[0].nodes.size() == 1);
        CHECK(LoadBuilt(file.string(), rk, &err));
        CHECK(rk.clips.size() == 1 && rk.clips[0].nodes.size() == 1);
        for (int mode = 0; mode < 3; ++mode) {   // 0 = our reader (baked at 30 Hz), 1 = the importer's clip (was: ufbx through the importer), 2 = our reader's file keys
            std::vector<double> got;
            if (mode == 0) {
                if (r.clips.size() != 1 || r.clips[0].nodes.size() != 1) continue;
                const auto& tr = r.clips[0].nodes[0].translation;
                CHECK(tr.size() == 31);
                for (double u : us) got.push_back(tr[(std::size_t)std::lround(u * 30)].v[0] * 100.0);
            } else if (mode == 2) {
                if (rk.clips.size() != 1 || rk.clips[0].nodes.size() != 1) continue;
                const auto& tr = rk.clips[0].nodes[0].translation;
                CHECK(tr.size() >= 2);   // (a steep cubic: more keys than 30 a second where the 30 Hz bake missed the tolerance)
                for (double u : us) got.push_back(KeyAt(tr, u).v[0] * 100.0);
            } else {
                Scene sc("hermite"); bool ok = false;
                GameObject* root = ImportModelScene(sc, file.string(), &ok);
                CHECK(ok && root);
                auto* ma = root ? root->GetComponent<ModelAnimator>() : nullptr;
                CHECK(ma && ma->clips.size() == 1 && ma->clips[0].nodes.size() == 1);
                if (!ma || ma->clips.size() != 1 || ma->clips[0].nodes.size() != 1) continue;
                for (double u : us) { bool f = false; got.push_back((double)ma->clips[0].nodes[0].clip.Evaluate("position.x", (float)u, f) * 100.0); CHECK(f); }
            }
            for (std::size_t i = 0; i < 4; ++i) {
                const double ref = HermiteRef(0, 10, cases[ci].a, cases[ci].b, 1.0, us[i]);
                // our file keys are within the reader's stated tolerance (1e-5 m + 1e-4 x 0.1 m = 2e-3 cm here); the importer's
                // clip plays the same keys, after its own key reduction (held to the 0.05 cm the ufbx path was held to)
                const double tol = mode == 1 ? 0.05 : mode == 2 ? 2e-3 : 2e-4;
                if (!Near(got[i], ref, tol)) std::cerr << "  (" << (mode == 1 ? "importer" : mode == 2 ? "own keys" : "own") << " a=" << cases[ci].a << " b=" << cases[ci].b << " u=" << us[i] << ") got " << got[i] << " expected " << ref << "\n";
                CHECK(Near(got[i], ref, tol));
                if (ci < 2) CHECK(Near(ref, hand[ci][i], 1e-9));   // the reference itself against the hand-computed values
            }
        }
    }
    std::error_code ec; fs::remove(file, ec);
}

// What the importer gave each mesh object as material (texture files by file name).
struct MatSnap { std::string name, tex, nrm; float col[4], emi[3], metallic, shininess, specular, reflectivity; };
static std::vector<MatSnap> CollectMaterials(Scene& sc, GameObject* root) {
    std::vector<MatSnap> out;
    for (const auto& up : sc.Objects()) {
        if (!up->IsSelfOrDescendantOf(root)) continue;
        auto* mr = up->GetComponent<MeshRenderer>();
        if (!mr) continue;
        MatSnap m;
        m.name = up->name; m.tex = fs::path(mr->texture).filename().string(); m.nrm = fs::path(mr->normalMap).filename().string();
        m.col[0] = mr->color.r; m.col[1] = mr->color.g; m.col[2] = mr->color.b; m.col[3] = mr->color.a;
        m.emi[0] = mr->emissive.r; m.emi[1] = mr->emissive.g; m.emi[2] = mr->emissive.b;
        m.metallic = mr->metallic; m.shininess = mr->shininess; m.specular = mr->specular; m.reflectivity = mr->reflectivity;
        out.push_back(m);
    }
    std::stable_sort(out.begin(), out.end(), [](const MatSnap& x, const MatSnap& y) { return x.name < y.name; });
    return out;
}

// ---- fixed expected values (were: ufbx vs ours) ------------------------------------------------------------------
// Until 2026-11-23 every fixture (and, with OKAY_MODEL_CORPUS, every real FBX file) was imported twice, through ufbx and
// through our reader, and the two were compared. ufbx was then deleted (rule 17). The values our reader gave, on the
// last run where both readers agreed within the tolerances of that comparison, were written down as fixed numbers:
// tests/assets/fbx/expected_import.txt (fixtures) and tests/data/fbx_corpus_expected.txt (real files, relative to
// $OKAY_MODEL_CORPUS). Only what the comparison checked was written: clips are left out for the two files where ufbx
// kept a clip ours drops, colour and glossiness for the 3ds Max PBR files whose textured slots ufbx reported empty.
// The test now holds our importer to those numbers. OKAY_FBX_WRITE_EXPECTED=<file> writes the current values instead
// of checking them (only after a reviewed, intended change of the importer's output).
struct SnapLine { std::string key; std::vector<double> v; std::string text; };
struct FbxSnap { std::vector<SnapLine> lines; };
static void SnapAdd(FbxSnap& s, std::string key, std::vector<double> v, std::string text = std::string()) { s.lines.push_back({std::move(key), std::move(v), std::move(text)}); }

static std::string JoinNames(std::vector<std::string> v, bool sortThem = true) {
    if (sortThem) std::sort(v.begin(), v.end());
    std::string o;
    for (const auto& x : v) { if (!o.empty()) o += '|'; o += x; }
    return o.empty() ? "-" : o;
}

// Everything the old comparison looked at, from our importer (ImportModelScene, ImportModel) and the reader's UV sets.
static bool SnapshotFbx(const std::string& path, bool isReal, FbxSnap& out, std::string* why) {
    const std::string what = fs::path(path).filename().string();
    const bool clipsMayDiffer = what == "box_orphant_embedded_texture.fbx" || what == "box_embedded_texture_fragmented.fbx";
    const bool texturedSlots = what == "maxPbrMaterial_metalRough.fbx" || what == "maxPbrMaterial_specGloss.fbx" || what == "pbr_bin.fbx";
    {   // UV sets (the reader's parts): per mesh with UVs, the set names in order and the distinct corners over all sets
        fbx::Scene s; std::string err;
        if (!LoadBuilt(path, s, &err)) { if (why) *why = "own reader: " + err; return false; }
        int k = 0;
        for (const fbx::Mesh& m : s.meshes) {
            if (!m.hasUV) continue;
            std::set<std::vector<float>> a;
            for (const fbx::Part& p : m.parts)
                for (std::size_t v = 0; v < p.vertices.size(); ++v) {
                    std::vector<float> key{(float)p.vertices[v].cp, p.vertices[v].uv[0], p.vertices[v].uv[1]};
                    for (const auto& x : p.extraUVs) { key.push_back(x[v * 2]); key.push_back(x[v * 2 + 1]); }
                    a.insert(key);
                }
            double sum = 0; for (const auto& key : a) for (std::size_t i = 1; i < key.size(); ++i) sum += key[i];
            SnapAdd(out, "uvsets." + std::to_string(k++), {(double)a.size(), sum}, JoinNames(m.uvSetNames, false));
        }
        SnapAdd(out, "uvmeshes", {(double)k});
    }
    Scene so("own"); bool oko = false;
    GameObject* ro = ImportModelScene(so, path, &oko);
    if (!oko || !ro) { if (why) *why = "scene import failed"; return false; }
    const ImportStats b = Collect(so, ro);
    SnapAdd(out, "counts", {(double)b.objects, (double)b.meshes, (double)b.skinned, (double)b.joints, (double)b.tris, (double)b.blend});
    SnapAdd(out, "verts", {(double)b.verts});
    if (!clipsMayDiffer) SnapAdd(out, "clips", {(double)b.clips, (double)b.tracks});
    SnapAdd(out, "bbox", {b.lo[0], b.lo[1], b.lo[2], b.hi[0], b.hi[1], b.hi[2]});
    SnapAdd(out, "area", {b.area});
    SnapAdd(out, "names", {}, JoinNames(b.names));
    for (const MatSnap& x : CollectMaterials(so, ro)) {
        std::vector<double> v;
        if (!texturedSlots) for (int k = 0; k < 4; ++k) v.push_back(x.col[k]);
        for (int k = 0; k < 3; ++k) v.push_back(x.emi[k]);
        v.push_back(x.metallic);
        if (!texturedSlots) { v.push_back(x.shininess * 0.01); v.push_back(x.specular); v.push_back(x.reflectivity); }
        SnapAdd(out, "mat." + x.name, v, (x.tex.empty() ? "-" : x.tex) + "|" + (x.nrm.empty() ? "-" : x.nrm));
    }
    if (!clipsMayDiffer)
        if (auto* mo = ro->GetComponent<ModelAnimator>())
            for (std::size_t ci = 0; ci < mo->clips.size(); ++ci) {
                auto& co = mo->clips[ci];
                float ownLen = 0; for (auto& x : co.nodes) ownLen = std::max(ownLen, x.clip.Length());
                SnapAdd(out, "clip." + std::to_string(ci), {(double)co.nodes.size()}, co.name.empty() ? "-" : co.name);
                for (auto& no : co.nodes)
                    for (float t : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 1.9f}) {
                        if (t > ownLen + 1e-4f) continue;
                        std::vector<double> v; std::string have;
                        for (const char* tr : {"position.x", "position.y", "position.z", "scale.x", "scale.y", "scale.z", "weights.0"}) {
                            bool f = false; const float a = no.clip.Evaluate(tr, t, f);
                            if (f) { v.push_back(a); have += tr[0] == 'w' ? 'w' : tr[0]; }
                        }
                        bool q = false; (void)no.clip.Evaluate("rotation.qw", t, q);
                        if (q) {
                            Vec3 p, s3; Quat r; no.clip.SampleTRS(t, p, r, s3, AnimationClip::kRot);
                            const double sg = r.w < 0 ? -1.0 : 1.0;   // q and -q are the same turn
                            v.push_back(sg * r.x); v.push_back(sg * r.y); v.push_back(sg * r.z); v.push_back(sg * r.w); have += 'q';
                        }
                        char tk[32]; std::snprintf(tk, sizeof tk, "%.2f", t);
                        SnapAdd(out, "key." + std::to_string(ci) + "." + no.node + "@" + tk, v, have.empty() ? "-" : have);
                    }
            }
    for (const auto& up : so.Objects()) {
        auto* sb = up->GetComponent<SkinnedMesh>();
        if (!sb || !up->IsSelfOrDescendantOf(ro)) continue;
        SnapAdd(out, "skin." + up->name, {(double)sb->inverseBind.size(), (double)sb->bind.vertices.size()}, JoinNames(sb->jointNames, false));
        for (std::size_t i = 0; i < sb->inverseBind.size(); ++i) {
            std::vector<double> v;
            for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) v.push_back(sb->inverseBind[i].at(c, r));
            SnapAdd(out, "ibind." + up->name + "." + std::to_string(i), v);
        }
        // influences as a joint -> weight sum per joint, and a weight-by-position moment (vertex order is our own)
        std::vector<double> perJoint(sb->jointNames.size() + 1, 0.0); double mx = 0, my = 0, mz = 0;
        for (std::size_t i = 0; i < sb->bind.vertices.size() && i < sb->jointWt.size(); ++i)
            for (int k = 0; k < 4; ++k) {
                const float w = sb->jointWt[i][(std::size_t)k];
                if (w <= 0) continue;
                const int j = sb->jointIdx[i][(std::size_t)k];
                perJoint[j >= 0 && (std::size_t)j < sb->jointNames.size() ? (std::size_t)j : sb->jointNames.size()] += w;
                mx += w * (j + 1) * sb->bind.vertices[i].x; my += w * (j + 1) * sb->bind.vertices[i].y; mz += w * (j + 1) * sb->bind.vertices[i].z;
            }
        perJoint.push_back(mx); perJoint.push_back(my); perJoint.push_back(mz);
        SnapAdd(out, "weights." + up->name, perJoint);
    }
    {   // the single-mesh path (ImportModel)
        bool m2 = false; std::string t2;
        Mesh mb = ImportModel(path, &m2, &t2);
        if (!m2) { if (why) *why = "ImportModel failed"; return false; }
        SnapAdd(out, "flat", {(double)mb.vertices.size(), (double)mb.triangles.size(), mb.normals.size() == mb.vertices.size() ? 1.0 : 0.0, mb.uvs.size() == mb.vertices.size() ? 1.0 : 0.0});
    }
    (void)isReal;
    return true;
}

static std::string EscapeKey(const std::string& s) { std::string o; for (char c : s) o += (c == '\t' || c == '\n' || c == '\r') ? '_' : c; return o; }

// One line per value group: file <TAB> key <TAB> text <TAB> numbers (space separated, %.9g).
static void WriteSnap(std::ostream& o, const std::string& file, const FbxSnap& s) {
    for (const SnapLine& l : s.lines) {
        o << EscapeKey(file) << '\t' << EscapeKey(l.key) << '\t' << EscapeKey(l.text.empty() ? "-" : l.text) << '\t';
        for (std::size_t i = 0; i < l.v.size(); ++i) { char b[40]; std::snprintf(b, sizeof b, "%.9g", l.v[i]); o << (i ? " " : "") << b; }
        o << '\n';
    }
}

using SnapTable = std::map<std::string, std::map<std::string, SnapLine>>;   // file -> key -> line
static bool ReadSnapTable(const std::string& path, SnapTable& t) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> col; std::string cur;
        for (char c : line) { if (c == '\t') { col.push_back(cur); cur.clear(); } else cur += c; }
        col.push_back(cur);
        if (col.size() != 4) continue;
        SnapLine l; l.key = col[1]; l.text = col[2];
        std::istringstream ns(col[3]); double d; while (ns >> d) l.v.push_back(d);
        t[col[0]][l.key] = l;
    }
    return true;
}

// The tolerances the ufbx comparison used, now against the recorded values.
static double SnapTolerance(const std::string& key, double expected, bool isReal) {
    auto starts = [&](const char* p) { return key.compare(0, std::strlen(p), p) == 0; };
    if (starts("counts") || starts("clips") || starts("uvmeshes") || starts("clip.") || starts("skin.") || starts("flat")) return 0;
    if (starts("uvsets")) return 1e-3 * (1 + std::fabs(expected));
    if (starts("verts")) return isReal ? 0.005 * expected : 0;
    if (starts("bbox")) return 1e-4 * (1 + std::fabs(expected));
    if (starts("area")) return (isReal ? 2e-3 : 1e-4) * (1 + std::fabs(expected));
    if (starts("mat.")) return 1e-4;
    if (starts("key.")) return isReal ? 1.5e-2 : 2e-3;
    if (starts("ibind.")) return 1e-4 * (1 + std::fabs(expected));
    if (starts("weights.")) return 1e-3 * (1 + std::fabs(expected));
    return 1e-6;
}

static void CheckSnap(const std::string& file, const FbxSnap& got, const std::map<std::string, SnapLine>& want, bool isReal) {
    std::map<std::string, const SnapLine*> have;
    for (const SnapLine& l : got.lines) have[l.key] = &l;
    int bad = 0;
    for (const auto& kv : want) {
        const SnapLine& w = kv.second;
        auto it = have.find(kv.first);
        if (it == have.end()) { if (bad++ < 8) std::cerr << "  (" << file << ") missing " << kv.first << "\n"; continue; }
        const SnapLine& g = *it->second;
        bool ok = g.v.size() == w.v.size() && EscapeKey(g.text.empty() ? "-" : g.text) == w.text;
        for (std::size_t i = 0; ok && i < w.v.size(); ++i) ok = std::fabs(g.v[i] - w.v[i]) <= SnapTolerance(kv.first, w.v[i], isReal);
        if (!ok && bad++ < 8) {
            std::cerr << "  (" << file << ") " << kv.first << ": expected '" << w.text << "'";
            for (double d : w.v) std::cerr << ' ' << d;
            std::cerr << ", got '" << g.text << "'";
            for (double d : g.v) std::cerr << ' ' << d;
            std::cerr << "\n";
        }
    }
    for (const SnapLine& l : got.lines) if (!want.count(l.key) && bad++ < 8) std::cerr << "  (" << file << ") not expected: " << l.key << "\n";
    CHECK(bad == 0);
}

static void ExpectedImportTests() {
    RUN_SUITE("fbx: every fixture (and real file) against the values recorded when ufbx and ours agreed");
    std::vector<std::string> paths;
    for (auto& e : fs::directory_iterator(fs::path(OKAY_SOURCE_DIR) / "tests" / "assets" / "fbx")) if (e.path().extension() == ".fbx") paths.push_back(e.path().string());
    std::sort(paths.begin(), paths.end());
    const std::size_t fixtureCount = paths.size();
    std::string corpusRoot;
    if (const char* root = std::getenv("OKAY_MODEL_CORPUS"); root && *root) {
        corpusRoot = root;
        std::vector<std::string> real;
        for (auto& e : fs::recursive_directory_iterator(root)) {
            std::string x = e.path().extension().string();
            for (char& c : x) c = (char)std::tolower((unsigned char)c);
            // transparentTest.fbx is a damaged file (NUL bytes replaced by spaces); no reader can open it
            if (e.is_regular_file() && x == ".fbx" && e.path().filename() != "transparentTest.fbx") real.push_back(e.path().string());
        }
        std::sort(real.begin(), real.end());
        std::cout << "  " << real.size() << " real FBX files from OKAY_MODEL_CORPUS\n";
        paths.insert(paths.end(), real.begin(), real.end());
    }
    const std::string fixturesFile = std::string(OKAY_SOURCE_DIR) + "/tests/assets/fbx/expected_import.txt";
    const std::string corpusFile = std::string(OKAY_SOURCE_DIR) + "/tests/data/fbx_corpus_expected.txt";
    const char* writeTo = std::getenv("OKAY_FBX_WRITE_EXPECTED");   // a file name: write the fixtures' (or with OKAY_MODEL_CORPUS, the real files') values
    SnapTable fixtures, corpus;
    const bool haveFixtures = ReadSnapTable(fixturesFile, fixtures);
    CHECK(haveFixtures || writeTo);
    const bool haveCorpus = corpusRoot.empty() || ReadSnapTable(corpusFile, corpus);
    CHECK(haveCorpus || writeTo);
    std::ofstream wf, wc;
    if (writeTo && *writeTo) { wf.open(std::string(writeTo) + ".fixtures"); wc.open(std::string(writeTo) + ".corpus"); }
    int files = 0;
    for (std::size_t pi = 0; pi < paths.size(); ++pi) {
        const bool isReal = pi >= fixtureCount;
        const std::string name = isReal ? fs::relative(paths[pi], corpusRoot).generic_string() : fs::path(paths[pi]).filename().string();
        FbxSnap s; std::string why;
        const bool ok = SnapshotFbx(paths[pi], isReal, s, &why);
        if (!ok) std::cerr << "  (" << name << ") " << why << "\n";
        CHECK(ok);
        if (!ok) continue;
        ++files;
        if (writeTo && *writeTo) { WriteSnap(isReal ? wc : wf, name, s); continue; }
        const SnapTable& t = isReal ? corpus : fixtures;
        auto it = t.find(name);
        if (it == t.end()) { std::cerr << "  (" << name << ") no recorded values\n"; CHECK(false); continue; }
        CheckSnap(name, s, it->second, isReal);
    }
    if (!writeTo) {   // every recorded file is still there (a removed fixture must remove its rows)
        for (const auto& kv : fixtures) { const bool there = fs::exists(fs::path(OKAY_SOURCE_DIR) / "tests" / "assets" / "fbx" / kv.first); if (!there) std::cerr << "  recorded fixture missing: " << kv.first << "\n"; CHECK(there); }
        if (!corpusRoot.empty()) for (const auto& kv : corpus) CHECK(fs::exists(fs::path(corpusRoot) / kv.first));
    }
    CHECK(files >= 6);
}

static void OrphanVideoTest() {
    RUN_SUITE("fbx scene: embedded picture in a Video that is not connected to its texture");
    fbx::Scene s; std::string err;
    CHECK(fbx::LoadFile(Fx("orphan_video_bin.fbx"), s, &err));
    CHECK(s.textures.size() == 1);
    if (s.textures.size() == 1) CHECK(s.textures[0].content.size() > 8 && s.textures[0].content[1] == 'P' && s.textures[0].content[2] == 'N');   // the PNG written by the generator
    CHECK(s.materials.size() == 1 && s.materials[0].diffuseTexture == 0);
}

static void PbrTests() {
    RUN_SUITE("fbx scene: PBR property sets (3ds Max, glTF) and the classic reading");
    fbx::Scene s; std::string err;
    CHECK(fbx::LoadFile(Fx("pbr_bin.fbx"), s, &err));
    CHECK(s.materials.size() == 5 && s.textures.size() == 8);
    auto mat = [&](const char* n) -> const fbx::Material* { for (const auto& m : s.materials) if (m.name == n) return &m; return nullptr; };
    const fbx::Material* ph = mat("PhysicalMat");
    CHECK(ph && ph->shaderSet == "3dsmax-physical");
    if (ph) {
        CHECK(ph->hasDiffuse && Near(ph->diffuse[0], 0.2) && Near(ph->diffuse[1], 0.4) && Near(ph->diffuse[2], 0.6));   // not the classic 0.9 grey
        CHECK(ph->hasRoughness && Near(ph->roughness, 0.3) && ph->hasMetalness && Near(ph->metalness, 0.9));
        CHECK(ph->hasEmissive && Near(ph->emissive[0], 0.5) && ph->hasEmissiveFactor && Near(ph->emissiveFactor, 2.0));
        CHECK(ph->diffuseTexture >= 0 && s.textures[(std::size_t)ph->diffuseTexture].name == "PhysicalAlbedo");
        CHECK(ph->normalTexture >= 0 && s.textures[(std::size_t)ph->normalTexture].name == "PhysicalNormal");
    }
    const fbx::Material* mr = mat("MetalRoughMat");
    CHECK(mr && mr->shaderSet == "3dsmax-pbr-metal-rough");
    if (mr) CHECK(mr->hasRoughness && Near(mr->roughness, 0.25) && Near(mr->metalness, 1.0) && mr->normalTexture >= 0 && s.textures[(std::size_t)mr->diffuseTexture].name == "MetalRoughAlbedo");
    const fbx::Material* sg = mat("SpecGlossMat");
    CHECK(sg && sg->shaderSet == "3dsmax-pbr-spec-gloss");
    if (sg) CHECK(sg->hasRoughness && Near(sg->roughness, 0.2) && !sg->hasMetalness);   // glossiness 0.8 -> roughness 0.2
    const fbx::Material* gl = mat("GltfMat");
    CHECK(gl && gl->shaderSet == "gltf");
    if (gl) CHECK(Near(gl->roughness, 0.6) && Near(gl->metalness, 0.5) && gl->hasEmissive && Near(gl->emissive[1], 0.25) && gl->diffuseTexture >= 0 && gl->normalTexture >= 0);
    const fbx::Material* ph2 = mat("PhongMat");   // same class id, but the material says phong: the classic properties win (the usual rule for these sets)
    CHECK(ph2 && ph2->shaderSet == "phong");
    if (ph2) CHECK(Near(ph2->diffuse[0], 0.9) && Near(ph2->diffuse[1], 0.8) && ph2->hasRoughness && Near(ph2->roughness, 1.0 - std::sqrt(25.0) * 0.1));
}

static void UvSetTests() {
    RUN_SUITE("fbx scene: every UV set with its name (uv2_bin / uv2_ascii)");
    // Two quads sharing the edge between control points 1 and 4. Set 0 "UVMap" is per control point (x / 2, y); set 1
    // "Lightmap" gives each quad its own island, so the shared corners differ in set 1 only. The file writes set 1
    // before set 0. Expected values worked out from scripts/gen_fbx_fixtures.py (uv2_scene).
    const float uv0[6][2] = {{0, 0}, {0.5f, 0}, {1, 0}, {0, 1}, {0.5f, 1}, {1, 1}};
    struct Want { unsigned cp; float u, v; };
    const Want want[8] = {{0, 0, 0}, {1, 0.4f, 0}, {4, 0.4f, 0.4f}, {3, 0, 0.4f},
                          {1, 0.6f, 0}, {2, 1, 0}, {5, 1, 0.4f}, {4, 0.6f, 0.4f}};
    for (const char* f : {"uv2_bin.fbx", "uv2_ascii.fbx"}) {
        fbx::Scene s; std::string err;
        CHECK(LoadBuilt(Fx(f), s, &err));
        CHECK(s.meshes.size() == 1);
        if (s.meshes.size() != 1) continue;
        const fbx::Mesh& m = s.meshes[0];
        CHECK(m.hasUV);
        CHECK(m.uvSetNames.size() == 2);
        if (m.uvSetNames.size() == 2) { CHECK(m.uvSetNames[0] == "UVMap"); CHECK(m.uvSetNames[1] == "Lightmap"); }
        CHECK(m.parts.size() == 1);
        if (m.parts.size() != 1) continue;
        const fbx::Part& p = m.parts[0];
        CHECK(p.indices.size() == 12);
        CHECK(p.vertices.size() == 8);   // 6 control points, the 2 shared ones split by set 1
        CHECK(p.extraUVs.size() == 1);
        if (p.extraUVs.size() != 1 || p.extraUVs[0].size() != p.vertices.size() * 2) { CHECK(false); continue; }
        bool set0 = true, found[8] = {};
        for (std::size_t v = 0; v < p.vertices.size(); ++v) {
            const fbx::Corner& c = p.vertices[v];
            if (c.cp >= 6 || !Near(c.uv[0], uv0[c.cp][0], 1e-6) || !Near(c.uv[1], uv0[c.cp][1], 1e-6)) set0 = false;
            for (int w = 0; w < 8; ++w)
                if (want[w].cp == c.cp && Near(p.extraUVs[0][v * 2], want[w].u, 1e-6) && Near(p.extraUVs[0][v * 2 + 1], want[w].v, 1e-6)) found[w] = true;
        }
        CHECK(set0);
        for (int w = 0; w < 8; ++w) CHECK(found[w]);
    }
    {   // a mesh with one UV set: its name, and no extra sets
        fbx::Scene s; std::string err;
        CHECK(LoadBuilt(Fx("cube_zup_cm_bin.fbx"), s, &err));
        if (s.meshes.size() == 1) {
            CHECK(s.meshes[0].uvSetNames.size() == 1 && s.meshes[0].uvSetNames[0] == "UVMap");
            for (const fbx::Part& p : s.meshes[0].parts) CHECK(p.extraUVs.empty());
        }
        fbx::Scene n;
        CHECK(LoadBuilt(Fx("plain_bin.fbx"), n, &err));   // no UVs: no names
        for (const fbx::Mesh& m : n.meshes) CHECK(m.uvSetNames.empty());
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// FBX 6.0 / 6.1 (2006-2009) and 7.0 (2010): tests/assets/fbx/legacy/, written by scripts/gen_fbx_fixtures.py from the
// same scenes as the 7.4 / 7.5 fixtures (cube_scene6 and rig_scene6 there), so each old file must import exactly like
// its newer twin. FBX 5.x is refused with a message that names the version and what to do.
// ---------------------------------------------------------------------------------------------------------------------
static std::string Lx(const char* name) { return Fx("legacy/") + name; }

static void SameNodes(const fbx::Scene& a, const fbx::Scene& b) {
    CHECK(a.nodes.size() == b.nodes.size());
    for (const fbx::SceneNode& n : a.nodes) {
        const fbx::SceneNode* m = FindNode(b, n.name.c_str());
        CHECK(m != nullptr);
        if (!m) { std::cerr << "    no node " << n.name << "\n"; continue; }
        const std::string pa = n.parent >= 0 ? a.nodes[(std::size_t)n.parent].name : std::string("(root)");
        const std::string pb = m->parent >= 0 ? b.nodes[(std::size_t)m->parent].name : std::string("(root)");
        CHECK(pa == pb);
        CHECK((n.mesh >= 0) == (m->mesh >= 0));
        for (int k = 0; k < 16; ++k) CHECK(Near(n.world.m[k], m->world.m[k], 1e-9));
        for (int k = 0; k < 16; ++k) CHECK(Near(n.geometryToWorld.m[k], m->geometryToWorld.m[k], 1e-9));
    }
}
static void SameMeshes(const fbx::Scene& a, const fbx::Scene& b) {
    CHECK(a.meshes.size() == b.meshes.size());
    for (std::size_t i = 0; i < std::min(a.meshes.size(), b.meshes.size()); ++i) {
        const fbx::Mesh& x = a.meshes[i]; const fbx::Mesh& y = b.meshes[i];
        CHECK(x.controlPoints == y.controlPoints);
        CHECK(x.triangleCount() == y.triangleCount());
        CHECK(x.hasUV == y.hasUV);
        CHECK(x.parts.size() == y.parts.size());
        for (std::size_t p = 0; p < std::min(x.parts.size(), y.parts.size()); ++p) {
            const auto cx = x.parts[p].ExpandCorners(); const auto cy = y.parts[p].ExpandCorners();
            CHECK(cx.size() == cy.size());
            bool same = cx.size() == cy.size();
            for (std::size_t c = 0; same && c < cx.size(); ++c)
                for (int k = 0; k < 3; ++k) same = same && Near(cx[c].p[k], cy[c].p[k], 1e-7) && Near(cx[c].n[k], cy[c].n[k], 1e-6) && (k > 1 || Near(cx[c].uv[k], cy[c].uv[k], 1e-7)) && cx[c].cp == cy[c].cp;
            CHECK(same);
        }
        CHECK(x.materials.size() == y.materials.size());
        for (std::size_t s = 0; s < std::min(x.materials.size(), y.materials.size()); ++s) {
            const int ma = x.materials[s], mb = y.materials[s];
            CHECK((ma >= 0) == (mb >= 0));
            if (ma < 0 || mb < 0) continue;
            const fbx::Material& A = a.materials[(std::size_t)ma]; const fbx::Material& B = b.materials[(std::size_t)mb];
            CHECK(A.name == B.name);
            CHECK(A.hasDiffuse == B.hasDiffuse && Near(A.diffuse[0], B.diffuse[0]) && Near(A.diffuse[1], B.diffuse[1]) && Near(A.diffuse[2], B.diffuse[2]));
            CHECK(A.hasEmissive == B.hasEmissive && Near(A.emissive[0], B.emissive[0]));
            CHECK(A.hasRoughness == B.hasRoughness && Near(A.roughness, B.roughness));
            CHECK((A.diffuseTexture >= 0) == (B.diffuseTexture >= 0));
            if (A.diffuseTexture >= 0 && B.diffuseTexture >= 0) {
                const fbx::Texture& ta = a.textures[(std::size_t)A.diffuseTexture]; const fbx::Texture& tb = b.textures[(std::size_t)B.diffuseTexture];
                CHECK(ta.fileName == tb.fileName && ta.relativeFileName == tb.relativeFileName);
                CHECK(ta.content == tb.content);
            }
        }
        // skin
        CHECK(x.clusters.size() == y.clusters.size());
        for (std::size_t c = 0; c < std::min(x.clusters.size(), y.clusters.size()); ++c) {
            const int ba = x.clusters[c].boneNode, bb = y.clusters[c].boneNode;
            CHECK(ba >= 0 && bb >= 0);
            if (ba >= 0 && bb >= 0) CHECK(a.nodes[(std::size_t)ba].name == b.nodes[(std::size_t)bb].name);
            for (int k = 0; k < 16; ++k) CHECK(Near(x.clusters[c].geometryToBone.m[k], y.clusters[c].geometryToBone.m[k], 1e-9));
        }
        CHECK(x.skinWeights.size() == y.skinWeights.size());
        bool weights = x.skinWeights.size() == y.skinWeights.size();
        for (std::size_t v = 0; weights && v < x.skinWeights.size(); ++v) {
            weights = x.skinWeights[v].size() == y.skinWeights[v].size();
            for (std::size_t k = 0; weights && k < x.skinWeights[v].size(); ++k)
                weights = x.skinWeights[v][k].cluster == y.skinWeights[v][k].cluster && Near(x.skinWeights[v][k].weight, y.skinWeights[v][k].weight, 1e-6);
        }
        CHECK(weights);
        // blend shapes
        CHECK(x.channels.size() == y.channels.size());
        for (std::size_t c = 0; c < std::min(x.channels.size(), y.channels.size()); ++c) {
            CHECK(x.channels[c].name == y.channels[c].name);
            CHECK(Near(x.channels[c].weight, y.channels[c].weight));
            CHECK(x.channels[c].indices == y.channels[c].indices);
            CHECK(x.channels[c].dp.size() == y.channels[c].dp.size());
            for (std::size_t k = 0; k < std::min(x.channels[c].dp.size(), y.channels[c].dp.size()); ++k) CHECK(Near(x.channels[c].dp[k], y.channels[c].dp[k], 1e-7));
        }
    }
}
/// The clips at times where the curves agree: every time for linear and constant curves, the key times for cubic ones
/// (`cubicNode`: the 6.x file's cubic keys get their tangents from the neighbouring keys, so between keys they may differ).
static void SameClips(const fbx::Scene& a, const fbx::Scene& b, const char* cubicNode) {
    CHECK(a.clips.size() == b.clips.size());
    for (std::size_t c = 0; c < std::min(a.clips.size(), b.clips.size()); ++c) {
        const fbx::Clip& x = a.clips[c]; const fbx::Clip& y = b.clips[c];
        CHECK(Near(x.duration, y.duration, 1e-9));
        CHECK(Near(x.start, y.start, 1e-9));
        CHECK(x.nodes.size() == y.nodes.size());
        for (const fbx::NodeAnim& na : x.nodes) {
            const std::string& name = a.nodes[(std::size_t)na.node].name;
            const fbx::NodeAnim* nb = nullptr;
            for (const fbx::NodeAnim& o : y.nodes) if (b.nodes[(std::size_t)o.node].name == name) nb = &o;
            CHECK(nb != nullptr);
            if (!nb) continue;
            const bool cubic = cubicNode && name == cubicNode;
            for (int step = 0; step <= 8; ++step) {
                const double t = step * 0.25;
                const bool atKey = step % 2 == 0;
                if (!na.translation.empty() && !nb->translation.empty()) {
                    const fbx::Key3 p = KeyAt(na.translation, t), q = KeyAt(nb->translation, t);
                    for (int k = 0; k < 3; ++k) CHECK(Near(p.v[k], q.v[k], 1e-5));
                }
                if (!na.scale.empty() && !nb->scale.empty()) {
                    const fbx::Key3 p = KeyAt(na.scale, t), q = KeyAt(nb->scale, t);
                    for (int k = 0; k < 3; ++k) CHECK(Near(p.v[k], q.v[k], 1e-5));
                }
                if ((!cubic || atKey) && !na.rotation.empty() && !nb->rotation.empty()) {
                    const fbx::Key4 p = KeyAt(na.rotation, t), q = KeyAt(nb->rotation, t);
                    double d = 0; for (int k = 0; k < 4; ++k) d += p.v[k] * q.v[k];
                    CHECK(std::fabs(d) > 1 - 1e-6);
                }
            }
        }
        CHECK(x.blends.size() == y.blends.size());
        for (std::size_t k = 0; k < std::min(x.blends.size(), y.blends.size()); ++k)
            for (int step = 0; step <= 8; ++step) {
                if (x.blends[k].percent.empty() || y.blends[k].percent.empty()) { CHECK(false); break; }
                CHECK(Near(KeyAt(x.blends[k].percent, step * 0.25).v[0], KeyAt(y.blends[k].percent, step * 0.25).v[0], 1e-4));
            }
    }
}

static void LegacyTests() {
    struct Pair { const char* old; const char* now; const char* cubic; };
    const Pair pairs[] = {
        {"cube61_bin.fbx", "cube_zup_cm_bin.fbx", nullptr}, {"cube61_ascii.fbx", "cube_zup_cm_ascii.fbx", nullptr},
        {"cube60_ascii.fbx", "cube_zup_cm_ascii.fbx", nullptr},
        {"cube70_bin.fbx", "cube_zup_cm_bin.fbx", nullptr}, {"cube70_ascii.fbx", "cube_zup_cm_ascii.fbx", nullptr},
        {"rig61_bin.fbx", "rig_bin75.fbx", "Bone1"}, {"rig61_ascii.fbx", "rig_ascii.fbx", "Bone1"},
    };
    for (const Pair& pr : pairs) {
        RUN_SUITE((std::string("fbx legacy: ") + pr.old + " imports like " + pr.now).c_str());
        fbx::Scene o, n; std::string eo, en;
        const bool okO = LoadBuilt(Lx(pr.old), o, &eo), okN = LoadBuilt(Fx(pr.now), n, &en);
        if (!okO) std::cerr << "    " << pr.old << ": " << eo << "\n";
        CHECK(okO); CHECK(okN);
        if (!okO || !okN) continue;
        CHECK(!o.nodes.empty() && !o.meshes.empty());
        SameNodes(n, o);
        SameMeshes(n, o);
        SameClips(n, o, pr.cubic);
        CHECK(Near(o.settings.appliedScale, n.settings.appliedScale));
        CHECK(o.settings.upAxis == n.settings.upAxis && o.settings.frontSign == n.settings.frontSign);
    }

    RUN_SUITE("fbx legacy: through the importer (scene and single-mesh paths)");
    // Imported from copies in the temp folder: the import writes an embedded picture next to the file (<name>_tex0.png).
    const fs::path tmpDir = fs::temp_directory_path() / ("okay_fbx_legacy_import_" + std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(tmpDir);
    auto Lc = [&](const char* name) {
        const fs::path to = tmpDir / name;
        std::error_code ec; fs::copy_file(Lx(name), to, fs::copy_options::overwrite_existing, ec);
        return to.string();
    };
    for (const char* f : {"cube61_bin.fbx", "cube61_ascii.fbx", "rig61_ascii.fbx"}) {
        Scene s("legacy");
        bool ok = false;
        GameObject* r = ImportModelScene(s, Lc(f), &ok);
        CHECK(r && ok);
        if (r) { const ImportStats st = Collect(s, r); CHECK(st.meshes >= 1 && st.tris >= 12); }
        bool mok = false;
        const Mesh m = ImportModel(Lc(f), &mok);
        CHECK(mok && !m.vertices.empty() && !m.triangles.empty());
    }
    {   // the rig keeps its skin and its clip through the importer
        Scene s("legacy rig");
        bool ok = false;
        GameObject* r = ImportModelScene(s, Lc("rig61_bin.fbx"), &ok);
        CHECK(r && ok);
        if (r) {
            CHECK(r->GetComponent<ModelAnimator>() != nullptr);
            const ImportStats st = Collect(s, r);
            CHECK(st.skinned >= 1 && st.joints == 3 && st.blend >= 1);
        }
    }
    { std::error_code ec; fs::remove_all(tmpDir, ec); }

    RUN_SUITE("fbx legacy: FBX 5.x is refused with the version and what to do");
    for (const char* f : {"old50_ascii.fbx", "old50_bin.fbx"}) {
        fbx::Scene s; std::string err;
        CHECK(!fbx::LoadFile(Lx(f), s, &err));
        std::cout << "  " << f << ": " << err << "\n";
        CHECK(err.find("5.0") != std::string::npos);
        CHECK(err.find("FBX 2011") != std::string::npos);
        Scene sc("old");
        bool ok = true;
        GameObject* r = ImportModelScene(sc, Lx(f), &ok);
        CHECK(!r || !ok);
        bool mok = true;
        ImportModel(Lx(f), &mok);
        CHECK(!mok);
        const std::string why = DescribeModelImportProblem(Lx(f));
        std::cout << "  " << f << " (problem): " << why << "\n";
        CHECK(why.find("FBX 5.0") != std::string::npos);
        CHECK(why.find("FBX 2011") != std::string::npos);
    }

    RUN_SUITE("fbx legacy: a file with no objects fails, it does not import as nothing");
    {
        const fs::path dir = fs::temp_directory_path() / ("okay_fbx_legacy_" + std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(dir);
        for (const char* text : {"; FBX 6.1.0 project file\nFBXHeaderExtension:  {\n\tFBXVersion: 6100\n}\nObjects:  {\n}\nConnections:  {\n}\n",
                                 "; FBX 7.4.0 project file\nObjects:  {\n}\nConnections:  {\n}\n"}) {
            const std::string p = (dir / "empty_objects.fbx").string();
            { std::ofstream o(p, std::ios::binary); o << text; }
            Scene sc("empty");
            bool ok = true;
            GameObject* r = ImportModelScene(sc, p, &ok);
            CHECK(!r || !ok);
            bool mok = true;
            ImportModel(p, &mok);
            CHECK(!mok);
            const std::string why = DescribeModelImportProblem(p);
            std::cout << "  empty_objects.fbx (problem): " << why << "\n";
            CHECK(why.find("no objects") != std::string::npos || why.find("no meshes") != std::string::npos);
        }
        std::error_code ec; fs::remove_all(dir, ec);
    }
}

int main() {
    InflateTests();
    DirectTests();
    HermiteTests();
    PbrTests();
    OrphanVideoTest();
    UvSetTests();
    LegacyTests();
    ExpectedImportTests();
    TEST_MAIN_RESULT();
}
