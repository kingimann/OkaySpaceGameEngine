#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/SkinnedMesh.hpp"
#include "okay/Render/ModelImport.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace okay;

// A tiny glTF writer: raw byte blocks become bufferViews + accessors.
struct GltfBuilder {
    std::vector<std::uint8_t> buf;
    std::string views, accs;
    int nViews = 0;
    int Add(const std::vector<std::uint8_t>& bytes, int compType, int count, const char* type, bool normalized = false) {
        while (buf.size() % 4) buf.push_back(0);
        if (nViews) { views += ","; accs += ","; }
        views += "{\"buffer\":0,\"byteOffset\":" + std::to_string(buf.size()) + ",\"byteLength\":" + std::to_string(bytes.size()) + "}";
        accs += "{\"bufferView\":" + std::to_string(nViews) + ",\"componentType\":" + std::to_string(compType) +
                ",\"count\":" + std::to_string(count) + ",\"type\":\"" + type + "\"" +
                (normalized ? ",\"normalized\":true" : "") + "}";
        buf.insert(buf.end(), bytes.begin(), bytes.end());
        return nViews++;
    }
    static std::vector<std::uint8_t> F(std::initializer_list<float> v) {
        std::vector<std::uint8_t> o;
        for (float f : v) { std::uint8_t t[4]; std::memcpy(t, &f, 4); o.insert(o.end(), t, t + 4); }
        return o;
    }
    static std::vector<std::uint8_t> U16(std::initializer_list<int> v) {
        std::vector<std::uint8_t> o; for (int x : v) { o.push_back(x & 0xFF); o.push_back((x >> 8) & 0xFF); } return o;
    }
    static std::vector<std::uint8_t> U8(std::initializer_list<int> v) {
        std::vector<std::uint8_t> o; for (int x : v) o.push_back((std::uint8_t)x); return o;
    }
    std::string B64() const {
        static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out; int val = 0, bits = -6;
        for (std::uint8_t c : buf) { val = (val << 8) + c; bits += 8; while (bits >= 0) { out += T[(val >> bits) & 0x3F]; bits -= 6; } }
        if (bits > -6) out += T[((val << 8) >> (bits + 8)) & 0x3F];
        while (out.size() % 4) out += '=';
        return out;
    }
};

static float AngleZ(const Quat& q) {   // rotation about Z, degrees (0..360)
    float a = 2.0f * std::atan2(q.z, q.w) * 57.2957795f;
    while (a < 0.0f) a += 360.0f;
    while (a >= 360.0f) a -= 360.0f;
    return a;
}

int main() {
    RUN_SUITE("model_import_rig");

    GltfBuilder b;
    // Two triangles, one per material, both skinned.
    int pos0 = b.Add(GltfBuilder::F({1, 0, 0,  0, 1, 0,  0, 0, 1}), 5126, 3, "VEC3");
    int pos1 = b.Add(GltfBuilder::F({2, 0, 0,  0, 2, 0,  0, 0, 2}), 5126, 3, "VEC3");
    int idx  = b.Add(GltfBuilder::U16({0, 1, 2}), 5123, 3, "SCALAR");
    // Influences split across JOINTS_0/WEIGHTS_0 and JOINTS_1/WEIGHTS_1, stored as
    // normalized bytes: vertex 0 = 100/255 on joint 0 + 155/255 on joint 1.
    int j0 = b.Add(GltfBuilder::U8({0,0,0,0, 0,0,0,0, 1,0,0,0}), 5121, 3, "VEC4");
    int w0 = b.Add(GltfBuilder::U8({100,0,0,0, 255,0,0,0, 255,0,0,0}), 5121, 3, "VEC4", true);
    int j1 = b.Add(GltfBuilder::U8({1,0,0,0, 0,0,0,0, 0,0,0,0}), 5121, 3, "VEC4");
    int w1 = b.Add(GltfBuilder::U8({155,0,0,0, 0,0,0,0, 0,0,0,0}), 5121, 3, "VEC4", true);
    int ib = b.Add(GltfBuilder::F({1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1,  1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}), 5126, 2, "MAT4");
    // Animation: STEP translation on the first "Bone"; LINEAR rotation on the
    // second, whose last key is stored sign-flipped (-q is the same rotation).
    int tt  = b.Add(GltfBuilder::F({0, 1}), 5126, 2, "SCALAR");
    int tv  = b.Add(GltfBuilder::F({0, 0, 0,  5, 0, 0}), 5126, 2, "VEC3");
    int rt  = b.Add(GltfBuilder::F({0, 1, 2}), 5126, 3, "SCALAR");
    const float h = 0.70710678f;
    int rv  = b.Add(GltfBuilder::F({0, 0, 0, 1,  0, 0, h, h,  0, 0, -1, 0}), 5126, 3, "VEC4");

    auto attrs = [&](int pos, const char* extra) {
        return std::string("{\"attributes\":{\"POSITION\":") + std::to_string(pos) +
               ",\"JOINTS_0\":" + std::to_string(j0) + ",\"WEIGHTS_0\":" + std::to_string(w0) +
               ",\"JOINTS_1\":" + std::to_string(j1) + ",\"WEIGHTS_1\":" + std::to_string(w1) +
               "},\"indices\":" + std::to_string(idx) + extra + "}";
    };
    std::string json =
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"buffers\":[{\"byteLength\":" + std::to_string(b.buf.size()) +
            ",\"uri\":\"data:application/octet-stream;base64," + b.B64() + "\"}],"
        "\"bufferViews\":[" + b.views + "],"
        "\"accessors\":[" + b.accs + "],"
        "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,0,0,1]}},"
                       "{\"pbrMetallicRoughness\":{\"baseColorFactor\":[0,0,1,1]}}],"
        "\"meshes\":[{\"primitives\":[" + attrs(pos0, ",\"material\":0") + "," + attrs(pos1, ",\"material\":1") + "]}],"
        "\"nodes\":[{\"name\":\"Bone\",\"children\":[1]},{\"name\":\"Bone\"},{\"name\":\"Body\",\"mesh\":0,\"skin\":0}],"
        "\"skins\":[{\"joints\":[0,1],\"inverseBindMatrices\":" + std::to_string(ib) + "}],"
        "\"animations\":[{\"name\":\"Wave\",\"samplers\":["
            "{\"input\":" + std::to_string(tt) + ",\"output\":" + std::to_string(tv) + ",\"interpolation\":\"STEP\"},"
            "{\"input\":" + std::to_string(rt) + ",\"output\":" + std::to_string(rv) + ",\"interpolation\":\"LINEAR\"}],"
          "\"channels\":["
            "{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"translation\"}},"
            "{\"sampler\":1,\"target\":{\"node\":1,\"path\":\"rotation\"}}]}]}";
    const char* path = "/tmp/okay_test_rig.gltf";
    { std::ofstream o(path); o << json; }

    Scene s("rig");
    bool ok = false;
    GameObject* root = ImportModelScene(s, path, &ok);
    CHECK(ok && root);
    if (!root) { TEST_MAIN_RESULT(); }

    // Duplicate node names were made unique, and the hierarchy kept.
    GameObject* bone0 = s.Find("Bone");
    GameObject* bone1 = s.Find("Bone_1");
    CHECK(bone0 && bone1);
    if (bone0 && bone1) CHECK(bone1->transform->Parent() == bone0->transform);

    // Multi-material skinned mesh: one skinned part per material, colors kept.
    GameObject* body = s.Find("Body");
    CHECK(body != nullptr);
    std::vector<SkinnedMesh*> parts;
    for (const auto& up : s.Objects())
        if (up->IsSelfOrDescendantOf(body))
            if (auto* sm = up->GetComponent<SkinnedMesh>()) parts.push_back(sm);
    CHECK(parts.size() == 2);
    if (parts.size() == 2) {
        auto* mrA = parts[0]->gameObject->GetComponent<MeshRenderer>();
        auto* mrB = parts[1]->gameObject->GetComponent<MeshRenderer>();
        CHECK(mrA && mrB && std::fabs(mrA->color.r - mrB->color.r) > 0.5f);
        // 8-influence weights: the strongest kept, renormalized, normalized bytes read as 0..1.
        SkinnedMesh* sm = parts[0];
        CHECK(sm->joints.size() == 2 && sm->joints[0] == bone0->transform && sm->joints[1] == bone1->transform);
        const auto& wt = sm->jointWt[0];
        const auto& ji = sm->jointIdx[0];
        CHECK(ji[0] == 1 && std::fabs(wt[0] - 155.0f / 255.0f) < 1e-3f);
        CHECK(ji[1] == 0 && std::fabs(wt[1] - 100.0f / 255.0f) < 1e-3f);
        CHECK(std::fabs(wt[0] + wt[1] + wt[2] + wt[3] - 1.0f) < 1e-4f);
        CHECK(std::fabs(sm->jointWt[1][0] - 1.0f) < 1e-4f);
    }

    // Animation: STEP holds, LINEAR rotation takes the short way despite the sign flip.
    auto* ma = root->GetComponent<ModelAnimator>();
    CHECK(ma && ma->ClipCount() == 1);
    if (ma && bone0 && bone1) {
        ma->loop = false;
        ma->Play("Wave");
        ma->SetTime(0.5f);
        CHECK(std::fabs(bone0->transform->localPosition.x) < 1e-3f);          // held at the first key
        ma->SetTime(1.0f);
        CHECK(std::fabs(bone0->transform->localPosition.x - 5.0f) < 1e-3f);
        ma->SetTime(1.5f);
        CHECK(std::fabs(AngleZ(bone1->transform->localRotation) - 135.0f) < 3.0f);   // between 90 and 180
    }

    // Import report.
    ModelReport rep = AnalyzeModel(s, root);
    CHECK(rep.skinnedMeshes == 2);
    CHECK(rep.bones == 2);
    CHECK(rep.skeletonRoots == 1 && rep.rootBone == "Bone");
    CHECK(rep.maxInfluences == 2);
    CHECK(rep.clips == 1);
    CHECK(rep.warnings.empty());
    CHECK(rep.Summary().find("2 bones") != std::string::npos);

    TEST_MAIN_RESULT();
}
