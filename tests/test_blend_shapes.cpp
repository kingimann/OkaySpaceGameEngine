#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/BlendShapes.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/SkinnedMesh.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace okay;

// Raw byte blocks -> bufferViews (+ optional accessors) of a data-URI glTF.
struct Builder {
    std::vector<std::uint8_t> buf;
    std::string views, accs;
    int nViews = 0, nAccs = 0;
    int View(const std::vector<std::uint8_t>& bytes) {
        while (buf.size() % 4) buf.push_back(0);
        if (nViews) views += ",";
        views += "{\"buffer\":0,\"byteOffset\":" + std::to_string(buf.size()) + ",\"byteLength\":" + std::to_string(bytes.size()) + "}";
        buf.insert(buf.end(), bytes.begin(), bytes.end());
        return nViews++;
    }
    int Acc(const std::string& body) { if (nAccs) accs += ","; accs += "{" + body + "}"; return nAccs++; }
    int Dense(const std::vector<std::uint8_t>& bytes, int ct, int count, const char* type) {
        int v = View(bytes);
        return Acc("\"bufferView\":" + std::to_string(v) + ",\"componentType\":" + std::to_string(ct) +
                   ",\"count\":" + std::to_string(count) + ",\"type\":\"" + type + "\"");
    }
    static std::vector<std::uint8_t> F(std::initializer_list<float> v) {
        std::vector<std::uint8_t> o;
        for (float f : v) { std::uint8_t t[4]; std::memcpy(t, &f, 4); o.insert(o.end(), t, t + 4); }
        return o;
    }
    static std::vector<std::uint8_t> U16(std::initializer_list<int> v) {
        std::vector<std::uint8_t> o; for (int x : v) { o.push_back(x & 0xFF); o.push_back((x >> 8) & 0xFF); } return o;
    }
    std::string B64() const {
        static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out; unsigned val = 0; int bits = -6;   // unsigned: the shift wraps instead of overflowing (UB)
        for (std::uint8_t c : buf) { val = (val << 8) + c; bits += 8; while (bits >= 0) { out += T[(val >> bits) & 0x3F]; bits -= 6; } }
        if (bits > -6) out += T[((val << 8) >> (bits + 8)) & 0x3F];
        while (out.size() % 4) out += '=';
        return out;
    }
};

static GameObject* FindWith(Scene& s, GameObject* root) {
    for (const auto& up : s.Objects())
        if (up->IsSelfOrDescendantOf(root) && up->GetComponent<BlendShapes>()) return up.get();
    return nullptr;
}

int main() {
    RUN_SUITE("blend_shapes");

    // ---- The component: weights mix offsets into the mesh; zero = the base.
    {
        Scene s("bs");
        GameObject* g = s.CreateGameObject("Face");
        auto* mr = g->AddComponent<MeshRenderer>();
        mr->mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        mr->mesh.triangles = {0, 1, 2};
        auto* bs = g->AddComponent<BlendShapes>();
        bs->AddShape("smile", {{0, 0, 0}, {0, 0.5f, 0}, {0, 0, 0}});
        bs->AddShape("frown", {{0, 0, 0}, {0, -1.0f, 0}, {0, 0, 0}});
        CHECK(bs->shapes[0].index.size() == 1);            // stored sparse
        bs->SetWeight("smile", 1.0f);
        bs->Apply();
        CHECK(std::fabs(mr->mesh.vertices[1].y - 0.5f) < 1e-5f);
        bs->SetWeight("frown", 0.5f);
        bs->Apply();
        CHECK(std::fabs(mr->mesh.vertices[1].y - 0.0f) < 1e-5f);
        bs->SetWeight("smile", 0.0f); bs->SetWeight("frown", 0.0f);
        bs->Apply();
        CHECK(std::fabs(mr->mesh.vertices[1].y) < 1e-6f && std::fabs(mr->mesh.vertices[1].x - 1.0f) < 1e-6f);
        CHECK(!bs->SetWeight("nope", 1.0f));
    }

    // ---- glTF: morph targets (one stored sparse), names, default weights and
    //      an animated "weights" channel on an unskinned mesh.
    Builder b;
    int pos = b.Dense(Builder::F({0, 0, 0,  1, 0, 0,  0, 1, 0}), 5126, 3, "VEC3");
    int idx = b.Dense(Builder::U16({0, 1, 2}), 5123, 3, "SCALAR");
    int t0  = b.Dense(Builder::F({0, 0, 0,  0, 0, 0,  0, 0, 1}), 5126, 3, "VEC3");    // lifts vertex 2 in z
    // Target 1 as a sparse accessor with no bufferView: zeros + vertex 1 moved 2 in x.
    int spI = b.View(Builder::U16({1}));
    int spV = b.View(Builder::F({2, 0, 0}));
    int t1  = b.Acc("\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"sparse\":{\"count\":1,"
                    "\"indices\":{\"bufferView\":" + std::to_string(spI) + ",\"componentType\":5123},"
                    "\"values\":{\"bufferView\":" + std::to_string(spV) + "}}");
    int at  = b.Dense(Builder::F({0, 1}), 5126, 2, "SCALAR");
    int av  = b.Dense(Builder::F({0, 0,  1, 0.5f}), 5126, 4, "SCALAR");               // 2 keys x 2 targets
    std::string json =
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"buffers\":[{\"byteLength\":" + std::to_string(b.buf.size()) +
            ",\"uri\":\"data:application/octet-stream;base64," + b.B64() + "\"}],"
        "\"bufferViews\":[" + b.views + "],"
        "\"accessors\":[" + b.accs + "],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":" + std::to_string(pos) + "},"
            "\"indices\":" + std::to_string(idx) + ","
            "\"targets\":[{\"POSITION\":" + std::to_string(t0) + "},{\"POSITION\":" + std::to_string(t1) + "}]}],"
            "\"weights\":[0.25,0],\"extras\":{\"targetNames\":[\"Blink\",\"Jaw\"]}}],"
        "\"nodes\":[{\"name\":\"Head\",\"mesh\":0}],"
        "\"animations\":[{\"name\":\"Talk\",\"samplers\":[{\"input\":" + std::to_string(at) +
            ",\"output\":" + std::to_string(av) + "}],"
            "\"channels\":[{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"weights\"}}]}]}";
    const char* path = "/tmp/okay_test_morph.gltf";
    { std::ofstream o(path); o << json; }

    {
        Scene s("morph");
        bool ok = false;
        GameObject* root = ImportModelScene(s, path, &ok);
        CHECK(ok && root);
        if (!root) { TEST_MAIN_RESULT(); }
        GameObject* head = FindWith(s, root);
        CHECK(head != nullptr);
        if (!head) { TEST_MAIN_RESULT(); }
        auto* bs = head->GetComponent<BlendShapes>();
        CHECK(bs->shapes.size() == 2);
        CHECK(bs->Find("Blink") == 0 && bs->Find("Jaw") == 1);
        CHECK(std::fabs(bs->Weight("Blink") - 0.25f) < 1e-5f);                  // mesh.weights default
        CHECK(bs->shapes[1].index.size() == 1 && bs->shapes[1].index[0] == 1);  // sparse read
        CHECK(std::fabs(bs->shapes[1].dPos[0].x - 2.0f) < 1e-5f);
        // An unskinned morph mesh gets a pass-through skin; the default weight is applied.
        auto* sm = head->GetComponent<SkinnedMesh>();
        CHECK(sm != nullptr && sm->joints.size() == 1);
        auto* mr = head->GetComponent<MeshRenderer>();
        CHECK(sm && std::fabs(sm->bind.vertices[2].z) > 1e-6f);               // default weight applied at import
        s.Start();
        s.Update(0.0f);
        // The clip autoplays from t=0, where both weights are 0: the base shape.
        // (An import may rescale the model; `k` is its unit length.)
        const float k = mr->mesh.vertices[1].x;
        CHECK(k > 0.0f && std::fabs(mr->mesh.vertices[2].z) < 1e-5f);

        // The clip drives the weights: at t=1, Blink 1 and Jaw 0.5.
        auto* ma = root->GetComponent<ModelAnimator>();
        CHECK(ma && ma->ClipCount() == 1);
        if (ma) {
            ma->loop = false;
            ma->Play("Talk");
            ma->SetTime(1.0f);
            CHECK(std::fabs(bs->Weight("Blink") - 1.0f) < 1e-3f);
            CHECK(std::fabs(bs->Weight("Jaw") - 0.5f) < 1e-3f);
            ma->SetTime(0.5f);
            CHECK(std::fabs(bs->Weight("Blink") - 0.5f) < 1e-3f);
            ma->SetTime(1.0f);
        }
        bs->Apply();
        sm->Skin();
        CHECK(std::fabs(mr->mesh.vertices[2].z - k) < 1e-4f);                   // Blink 1: +1 unit in z
        CHECK(std::fabs(mr->mesh.vertices[1].x - 2.0f * k) < 1e-4f);            // Jaw 0.5: +1 unit in x
        const float zLift = k * 0.25f;

        // Save / reload: the bind is stored unmorphed, the shapes and weights survive.
        const char* sp = "/tmp/okay_test_morph.scene";
        CHECK(SceneSerializer::SaveToFile(s, sp));
        Scene s2("reload");
        CHECK(SceneSerializer::LoadFromFile(s2, sp));
        GameObject* head2 = s2.Find(head->name);
        CHECK(head2 != nullptr);
        if (head2) {
            auto* bs2 = head2->GetComponent<BlendShapes>();
            auto* sm2 = head2->GetComponent<SkinnedMesh>();
            CHECK(bs2 && sm2 && bs2->shapes.size() == 2);
            if (bs2 && sm2) {
                CHECK(bs2->Find("Jaw") == 1 && std::fabs(bs2->Weight("Blink") - 1.0f) < 1e-3f);
                CHECK(std::fabs(bs2->base.vertices[2].z) < 1e-5f);                // unmorphed base
                CHECK(std::fabs(sm2->bind.vertices[2].z - zLift * 4.0f) < 1e-4f); // weights re-applied
            }
        }
    }

    TEST_MAIN_RESULT();
}
