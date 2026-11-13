#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/SkinnedMesh.hpp"
#include "okay/Render/FbxReader.hpp"
#include "okay/Render/ModelImport.hpp"
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

// okay::fbx (our own FBX reader) against hand-computed values, and side by side with the ufbx
// path: every fixture in tests/assets/fbx is imported both ways and compared. The fixtures are made
// by scripts/gen_fbx_fixtures.py.
using namespace okay;
namespace fs = std::filesystem;

static std::string Fx(const char* name) { return std::string(OKAY_SOURCE_DIR) + "/tests/assets/fbx/" + name; }
static std::vector<std::uint8_t> Hex(const char* h) {
    std::vector<std::uint8_t> o;
    for (const char* p = h; p[0] && p[1]; p += 2) { unsigned v; std::sscanf(p, "%2x", &v); o.push_back((std::uint8_t)v); }
    return o;
}
static bool Near(double a, double b, double eps = 1e-5) { return std::fabs(a - b) <= eps; }

static double TriArea(const fbx::Corner* a) {
    const double ux = a[1].p[0] - a[0].p[0], uy = a[1].p[1] - a[0].p[1], uz = a[1].p[2] - a[0].p[2];
    const double vx = a[2].p[0] - a[0].p[0], vy = a[2].p[1] - a[0].p[1], vz = a[2].p[2] - a[0].p[2];
    const double cx = uy * vz - uz * vy, cy = uz * vx - ux * vz, cz = ux * vy - uy * vx;
    return 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
}
static double MeshArea(const fbx::Mesh& m) {
    double s = 0;
    for (const fbx::Part& p : m.parts) for (std::size_t i = 0; i + 2 < p.corners.size(); i += 3) s += TriArea(&p.corners[i]);
    return s;
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
    CHECK(fbx::LoadFile(Fx("cube_zup_cm_bin.fbx"), b, &err));
    CHECK(fbx::LoadFile(Fx("cube_zup_cm_ascii.fbx"), a, &err));
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
        for (const fbx::Corner& c : m.parts[0].corners) for (int k = 0; k < 3; ++k) { lo[k] = std::min(lo[k], c.p[k]); hi[k] = std::max(hi[k], c.p[k]); }
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
        for (const fbx::Corner& c : m.parts[0].corners) if (Near(c.n[2], 1.0, 1e-4)) rawNormal = true;
        CHECK(rawNormal);
        // ascii and binary agree corner by corner
        const auto& ca = a.meshes[0].parts[0].corners; const auto& cb = m.parts[0].corners;
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
    CHECK(fbx::LoadFile(Fx("ngon_bin.fbx"), g, &err));
    if (!g.meshes.empty()) {
        const fbx::Mesh& m = g.meshes[0];
        // L hexagon (area 3) + pentagon + triangle (0.5)
        auto shoelace = [](std::vector<std::pair<double, double>> p) { double s = 0; for (std::size_t i = 0; i < p.size(); ++i) { auto& a = p[i]; auto& b = p[(i + 1) % p.size()]; s += a.first * b.second - b.first * a.second; } return std::fabs(s) * 0.5; };
        const double pent = shoelace({{3, 0}, {4, 0}, {4.5, 1}, {3.5, 1.8}, {2.8, 1}});
        CHECK(Near(MeshArea(m), (3.0 + pent + 0.5) * 1e-4, 1e-9));   // file is in cm
        CHECK(m.triangleCount() == 4 + 3 + 1);
        CHECK(m.parts.size() == 2 && m.parts[0].slot == 0 && m.parts[1].slot == 1);
        if (m.parts.size() == 2) CHECK(m.parts[0].corners.size() / 3 == 4 && m.parts[1].corners.size() / 3 == 4);
        bool allUp = true;   // concave polygon: every triangle must keep the +Z winding
        for (const fbx::Part& p : m.parts) for (std::size_t i = 0; i + 2 < p.corners.size(); i += 3) {
            const fbx::Corner* c = &p.corners[i];
            const double z = (c[1].p[0] - c[0].p[0]) * (c[2].p[1] - c[0].p[1]) - (c[1].p[1] - c[0].p[1]) * (c[2].p[0] - c[0].p[0]);
            if (z <= 0) allUp = false;
        }
        CHECK(allUp);
        CHECK(m.materials.size() == 2 && g.materials[(std::size_t)m.materials[1]].name == "B");
        CHECK(!m.hasUV);
    }

    RUN_SUITE("fbx scene: generated normals, hierarchy, bad polygons");
    fbx::Scene pl;
    CHECK(fbx::LoadFile(Fx("plain_bin.fbx"), pl, &err));
    CHECK(pl.nodes.size() == 2 && pl.roots.size() == 1);
    if (pl.nodes.size() == 2) {
        CHECK(pl.nodes[0].name == "Parent" && pl.nodes[1].name == "Pyr" && pl.nodes[1].parent == 0);
        CHECK(Near(pl.nodes[1].world.m[3], 0.01, 1e-9) && Near(pl.nodes[1].world.m[7], 0.02, 1e-9));
        CHECK(Near(pl.nodes[1].scale[0], 2.0, 1e-6));
        CHECK(pl.nodes[1].mesh == 0 && pl.meshes[0].triangleCount() == 3);   // the 2-corner polygon was skipped
        bool unit = true;
        for (const fbx::Corner& c : pl.meshes[0].parts[0].corners) { const double l = std::sqrt(c.n[0] * c.n[0] + c.n[1] * c.n[1] + c.n[2] * c.n[2]); if (!Near(l, 1.0, 1e-4)) unit = false; }
        CHECK(unit);
        CHECK(!pl.meshes[0].hasUV);
    }

    RUN_SUITE("fbx scene: rig (skin, blend shape, animation)");
    for (const char* name : {"rig_bin75.fbx", "rig_ascii.fbx"}) {
        fbx::Scene r;
        CHECK(fbx::LoadFile(Fx(name), r, &err));
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
            CHECK(na.translation.size() == 61 && na.rotation.size() == 61 && na.scale.size() == 61);
            const std::string& nm = r.nodes[(std::size_t)na.node].name;
            if (nm == "Bone1") {   // PreRotation 10 deg + animated Z rotation 45 deg at t = 0.5 -> 55 deg about Z
                const fbx::Key4& k = na.rotation[15];
                CHECK(Near(k.time, 0.5, 1e-9));
                CHECK(Near(std::fabs(k.v[2]), std::sin(27.5 * 3.14159265358979 / 180.0), 1e-6) && Near(std::fabs(k.v[3]), std::cos(27.5 * 3.14159265358979 / 180.0), 1e-6));
                const fbx::Key4& k0 = na.rotation[0];   // t = 0: pre-rotation only
                CHECK(Near(std::fabs(k0.v[2]), std::sin(5.0 * 3.14159265358979 / 180.0), 1e-6));
            } else if (nm == "Bone2") {   // linear 1.0 -> 1.5 over 2 s
                CHECK(Near(na.translation[30].v[1], 0.0125, 1e-8) && Near(na.translation[60].v[1], 0.015, 1e-8));
            } else if (nm == "Bone0") {   // constant steps: 1,1,2
                CHECK(Near(na.scale[20].v[0], 1.0, 1e-9) && Near(na.scale[60].v[0], 2.0, 1e-9));
            }
        }
        if (c.blends.size() == 1) {
            CHECK(c.blends[0].mesh == 0 && c.blends[0].channel == 0);
            CHECK(Near(c.blends[0].percent[30].v[0], 50.0, 1e-5) && Near(c.blends[0].percent[60].v[0], 100.0, 1e-5));
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
        CHECK(fbx::LoadFile(Fx("bridge_bin.fbx"), br, &err));
        if (!br.meshes.empty()) CHECK(Near(MeshArea(br.meshes[0]), 12.0 * 0.0001, 1e-9));   // centimetres to metres: x 0.01 on both axes
    }
    {
        // three takes over one set of curves (x: 0 at t=0, 10 at t=1, 0 at t=2): "first" plays [0, 1],
        // "second" [1, 2], "whole" has no span of its own. The header holds a 'c' (byte array) thumbnail.
        fbx::Scene tk;
        CHECK(fbx::LoadFile(Fx("takes_bin.fbx"), tk, &err));
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

// ---- ufbx vs ours -----------------------------------------------------------------------
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
        fbx::Scene r; std::string err;
        CHECK(fbx::LoadFile(file.string(), r, &err));
        CHECK(r.clips.size() == 1 && r.clips[0].nodes.size() == 1);
        for (int mode = 0; mode < 2; ++mode) {   // 0 = our reader (baked at 30 Hz), 1 = ufbx through the importer
            std::vector<double> got;
            if (mode == 0) {
                if (r.clips.size() != 1 || r.clips[0].nodes.size() != 1) continue;
                const auto& tr = r.clips[0].nodes[0].translation;
                CHECK(tr.size() == 31);
                for (double u : us) got.push_back(tr[(std::size_t)std::lround(u * 30)].v[0] * 100.0);
            } else {
                fbx::SetOwnReaderEnabled(false);
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
                const double tol = mode ? 0.05 : 2e-4;   // ufbx bakes on its own sample grid, so its 30 Hz keys are a piecewise-linear cut of the same curve
                if (!Near(got[i], ref, tol)) std::cerr << "  (" << (mode ? "ufbx" : "own") << " a=" << cases[ci].a << " b=" << cases[ci].b << " u=" << us[i] << ") got " << got[i] << " expected " << ref << "\n";
                CHECK(Near(got[i], ref, tol));
                if (ci < 2) CHECK(Near(ref, hand[ci][i], 1e-9));   // the reference itself against the hand-computed values
            }
        }
    }
    std::error_code ec; fs::remove(file, ec);
}

static void CompareClips(Scene& su, GameObject* ru, Scene& so, GameObject* ro, const std::string& what, double tol = 2e-3) {
    auto* mu = ru->GetComponent<ModelAnimator>();
    auto* mo = ro->GetComponent<ModelAnimator>();
    if (!mu || !mo) { CHECK((mu == nullptr) == (mo == nullptr)); return; }
    (void)su; (void)so;
    CHECK(mu->clips.size() == mo->clips.size());
    for (std::size_t ci = 0; ci < std::min(mu->clips.size(), mo->clips.size()); ++ci) {
        auto& cu = mu->clips[ci]; auto& co = mo->clips[ci];
        CHECK(cu.name == co.name);
        double worst = 0; int compared = 0;
        for (auto& nu : cu.nodes) {
            const ModelAnimator::NodeClip* no = nullptr;
            for (auto& x : co.nodes) if (x.node == nu.node) no = &x;
            if (!no) { std::cerr << "  (" << what << ") ufbx clip has a track for " << nu.node << " that ours lacks\n"; CHECK(false); continue; }
            for (float t : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 1.9f}) {
                // ufbx's bake keeps the keys after the end of a take's time span; ours cuts the clip at the span
                float ownLen = 0; for (auto& x : co.nodes) ownLen = std::max(ownLen, x.clip.Length());
                if (t > ownLen + 1e-4f) continue;
                for (const char* tr : {"position.x", "position.y", "position.z", "scale.x", "scale.y", "scale.z", "weights.0"}) {
                    bool f1 = false, f2 = false;
                    const float a = nu.clip.Evaluate(tr, t, f1), b = no->clip.Evaluate(tr, t, f2);
                    if (f1 != f2 && f1) { /* ufbx has a track ours does not: only constant ones may be missing */ if (std::fabs(a - ((tr[0] == 's') ? 1.0f : 0.0f)) > 1e-3f && tr[0] != 'p') CHECK(false); continue; }
                    if (!f1 || !f2) continue;
                    worst = std::max(worst, (double)std::fabs(a - b)); ++compared;
                }
                bool q1 = false, q2 = false;
                const float ux = nu.clip.Evaluate("rotation.qx", t, q1), uy = nu.clip.Evaluate("rotation.qy", t, q1), uz = nu.clip.Evaluate("rotation.qz", t, q1), uw = nu.clip.Evaluate("rotation.qw", t, q1);
                const float ox = no->clip.Evaluate("rotation.qx", t, q2), oy = no->clip.Evaluate("rotation.qy", t, q2), oz = no->clip.Evaluate("rotation.qz", t, q2), ow = no->clip.Evaluate("rotation.qw", t, q2);
                if (q1 && q2) { worst = std::max(worst, 1.0 - std::fabs((double)ux * ox + (double)uy * oy + (double)uz * oz + (double)uw * ow)); ++compared; }
            }
        }
        if (worst > tol) std::cerr << "  (" << what << ") clip " << cu.name << " worst difference " << worst << "\n";
        CHECK(worst < tol);
        CHECK(compared > 0);
    }
}

static void CompareWithUfbx() {
    RUN_SUITE("fbx: every fixture through ufbx and through our reader");
    int files = 0;
    std::vector<std::string> paths;
    for (auto& e : fs::directory_iterator(fs::path(OKAY_SOURCE_DIR) / "tests" / "assets" / "fbx")) if (e.path().extension() == ".fbx") paths.push_back(e.path().string());
    std::sort(paths.begin(), paths.end());
    // Real exporter files, when OKAY_MODEL_CORPUS names a directory of them (docs/model_corpus.md): the same checks.
    // transparentTest.fbx is a damaged file (NUL bytes replaced by spaces); neither reader can open it.
    std::vector<std::string> real;
    if (const char* root = std::getenv("OKAY_MODEL_CORPUS"); root && *root) {
        for (auto& e : fs::recursive_directory_iterator(root)) {
            std::string x = e.path().extension().string();
            for (char& c : x) c = (char)std::tolower((unsigned char)c);
            if (e.is_regular_file() && x == ".fbx" && e.path().filename() != "transparentTest.fbx") real.push_back(e.path().string());
        }
        std::sort(real.begin(), real.end());
        std::cout << "  " << real.size() << " real FBX files from OKAY_MODEL_CORPUS\n";
    }
    const std::size_t fixtureCount = paths.size();
    paths.insert(paths.end(), real.begin(), real.end());
    for (std::size_t pi = 0; pi < paths.size(); ++pi) {
        const std::string& path = paths[pi];
        const bool isReal = pi >= fixtureCount;
        ++files;
        const std::string what = fs::path(path).filename().string();
        // ufbx keeps a clip for a take whose curves animate only custom properties (a constant pose); ours drops it
        const bool clipsMayDiffer = what == "box_orphant_embedded_texture.fbx" || what == "box_embedded_texture_fragmented.fbx";
        fbx::Scene direct; std::string err;
        const bool ownLoads = fbx::LoadFile(path, direct, &err);
        if (!ownLoads) std::cerr << "  (" << what << ") own reader: " << err << "\n";
        CHECK(ownLoads);

        fbx::SetOwnReaderEnabled(false);
        Scene su("ufbx"); bool oku = false;
        GameObject* ru = ImportModelScene(su, path, &oku);
        fbx::SetOwnReaderEnabled(true);
        Scene so("own"); bool oko = false;
        GameObject* ro = ImportModelScene(so, path, &oko);
        fbx::SetOwnReaderEnabled(false);
        CHECK(oku && ru && oko && ro);
        if (!ru || !ro) continue;
        ImportStats a = Collect(su, ru), b = Collect(so, ro);
        auto same = [&](const char* label, double x, double y, double eps) {
            if (std::fabs(x - y) > eps) std::cerr << "  (" << what << ") " << label << ": ufbx " << x << " vs ours " << y << "\n";
            CHECK(std::fabs(x - y) <= eps);
        };
        same("objects", a.objects, b.objects, 0);
        same("meshes", a.meshes, b.meshes, 0);
        same("skinned meshes", a.skinned, b.skinned, 0);
        same("joints", a.joints, b.joints, 0);
        // ufbx keeps a few more duplicate vertices than we weld on real files
        same("vertices", a.verts, b.verts, isReal ? 0.005 * a.verts : 0);
        same("triangles", a.tris, b.tris, 0);
        same("blend shape hosts", a.blend, b.blend, 0);
        if (!clipsMayDiffer) { same("clips", a.clips, b.clips, 0); same("clip node tracks", a.tracks, b.tracks, 0); }
        for (int k = 0; k < 3; ++k) { same("bbox lo", a.lo[k], b.lo[k], 1e-4 * (1 + std::fabs(a.lo[k]))); same("bbox hi", a.hi[k], b.hi[k], 1e-4 * (1 + std::fabs(a.hi[k]))); }
        same("surface area", a.area, b.area, (isReal ? 2e-3 : 1e-4) * (1 + a.area));   // real files: quads split along the other diagonal differ slightly
        auto na = a.names, nb = b.names; std::sort(na.begin(), na.end()); std::sort(nb.begin(), nb.end());
        CHECK(na == nb);
        if (!clipsMayDiffer) CompareClips(su, ru, so, ro, what, isReal ? 1.5e-2 : 2e-3);   // real files: custom tangent weights are ignored (docs/own_fbx_reader.md)

        // skinned vertex data: weights and inverse binds
        for (const auto& up : su.Objects()) {
            auto* sa = up->GetComponent<SkinnedMesh>();
            if (!sa || !up->IsSelfOrDescendantOf(ru)) continue;
            GameObject* other = nullptr;
            for (const auto& o : so.Objects()) if (o->name == up->name && o->IsSelfOrDescendantOf(ro)) other = o.get();
            auto* sb = other ? other->GetComponent<SkinnedMesh>() : nullptr;
            CHECK(sb != nullptr);
            if (!sb) continue;
            CHECK(sa->jointNames == sb->jointNames);
            CHECK(sa->inverseBind.size() == sb->inverseBind.size());
            for (std::size_t i = 0; i < std::min(sa->inverseBind.size(), sb->inverseBind.size()); ++i)
                for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) CHECK_NEAR(sa->inverseBind[i].at(c, r), sb->inverseBind[i].at(c, r), 1e-4);
            if (!isReal) CHECK(sa->bind.vertices.size() == sb->bind.vertices.size());
            if ((isReal || sa->bind.vertices.size() == sb->bind.vertices.size()) && sa->bind.uvs.size() == sa->bind.vertices.size() && sb->bind.uvs.size() == sb->bind.vertices.size()) {
                // Vertex order may differ (each side dedupes in its own order): match vertices by
                // position + normal + uv, then compare their influences as joint -> weight maps.
                auto key = [isReal](const Mesh& m, std::size_t i) {
                    char buf[160];
                    std::snprintf(buf, sizeof(buf), isReal ? "%.3f,%.3f,%.3f|%.2f,%.2f,%.2f|%.3f,%.3f" : "%.5f,%.5f,%.5f|%.4f,%.4f,%.4f|%.4f,%.4f", m.vertices[i].x, m.vertices[i].y, m.vertices[i].z,
                                  m.normals.size() > i ? m.normals[i].x : 0.f, m.normals.size() > i ? m.normals[i].y : 0.f, m.normals.size() > i ? m.normals[i].z : 0.f, m.uvs[i].x, m.uvs[i].y);
                    return std::string(buf);
                };
                std::unordered_map<std::string, std::size_t> inB;
                for (std::size_t i = 0; i < sb->bind.vertices.size(); ++i) inB[key(sb->bind, i)] = i;
                double worst = 0; std::size_t unmatched = 0;
                for (std::size_t i = 0; i < sa->bind.vertices.size(); ++i) {
                    auto it = inB.find(key(sa->bind, i));
                    if (it == inB.end()) { ++unmatched; continue; }
                    const std::size_t j = it->second;
                    for (int k = 0; k < 4; ++k) {
                        const float wa = sa->jointWt[i][(std::size_t)k];
                        if (wa <= 0) continue;
                        float wb = 0;
                        for (int q = 0; q < 4; ++q) if (sb->jointIdx[j][(std::size_t)q] == sa->jointIdx[i][(std::size_t)k] && sb->jointWt[j][(std::size_t)q] > 0) wb += sb->jointWt[j][(std::size_t)q];
                        worst = std::max(worst, (double)std::fabs(wa - wb));
                    }
                }
                if (unmatched || worst >= 1e-4) std::cerr << "  (" << what << ") skinned part " << up->name << ": " << unmatched << " unmatched vertices, worst weight difference " << worst << "\n";
                CHECK(unmatched == 0);
                CHECK(worst < 1e-4);
            }
        }

        // the single-mesh path (ImportModel) too
        bool m1 = false, m2 = false; std::string t1, t2;
        fbx::SetOwnReaderEnabled(false); Mesh ma = ImportModel(path, &m1, &t1);
        fbx::SetOwnReaderEnabled(true);  Mesh mb = ImportModel(path, &m2, &t2);
        fbx::SetOwnReaderEnabled(false);
        CHECK(m1 && m2);
        if (isReal) CHECK(std::fabs((double)ma.vertices.size() - (double)mb.vertices.size()) <= 0.005 * (double)ma.vertices.size() && ma.triangles.size() == mb.triangles.size());
        else CHECK(ma.vertices.size() == mb.vertices.size() && ma.triangles.size() == mb.triangles.size());
        CHECK(ma.normals.size() == ma.vertices.size() ? mb.normals.size() == mb.vertices.size() : mb.normals.empty());
        CHECK(ma.uvs.size() == ma.vertices.size() ? mb.uvs.size() == mb.vertices.size() : mb.uvs.empty());
    }
    CHECK(files >= 6);
}

int main() {
    InflateTests();
    DirectTests();
    HermiteTests();
    CompareWithUfbx();
    TEST_MAIN_RESULT();
}
