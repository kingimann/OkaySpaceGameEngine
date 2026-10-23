#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Render/ModelImport.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace okay;

// glTF CUBICSPLINE animation: the in/out tangents shape the curve between keys
// (overshoot, anticipation), so they must survive import. Before, only the key
// values were kept and re-smoothed, so a curve whose keys are equal but whose
// tangents aren't played back as a flat line.

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

static const AnimationClip* NodeClip(const ModelAnimator* ma, const std::string& node) {
    if (!ma || ma->clips.empty()) return nullptr;
    for (const auto& nc : ma->clips[0].nodes) if (nc.node == node) return &nc.clip;
    return nullptr;
}

static float Eval(const AnimationClip* c, const char* track, float t) {
    bool found = false;
    float v = c ? c->Evaluate(track, t, found) : 0.0f;
    return found ? v : -999.0f;
}

int main() {
    RUN_SUITE("gltf_cubicspline");

    GltfBuilder b;
    int pos = b.Add(GltfBuilder::F({0, 0, 0,  1, 0, 0,  0, 1, 0}), 5126, 3, "VEC3");
    int idx = b.Add(GltfBuilder::U16({0, 1, 2}), 5123, 3, "SCALAR");
    int mt  = b.Add(GltfBuilder::F({0, 0, 1,  0, 0, 1,  0, 0, 1}), 5126, 3, "VEC3");   // morph target deltas
    int t01 = b.Add(GltfBuilder::F({0, 1}), 5126, 2, "SCALAR");
    // Translation: both key values are 0, key 0 leaves with slope (4,0,0) and key 1
    // arrives flat. glTF Hermite: x(u) = h10(u) * 1s * 4, so x(0.5) = 0.125 * 4 = 0.5.
    int tv = b.Add(GltfBuilder::F({ 0,0,0,  0,0,0,  4,0,0,     // key 0: in, value, out
                                    0,0,0,  0,0,0,  0,0,0 }),  // key 1
                   5126, 6, "VEC3");
    // Rotation 0 -> 90 deg about Z with zero tangents: an ease-in/out whose midpoint
    // is still 45 deg (and the samples stay unit quaternions).
    const float h = 0.70710678f;
    int rv = b.Add(GltfBuilder::F({ 0,0,0,0,  0,0,0,1,  0,0,0,0,
                                    0,0,0,0,  0,0,h,h,  0,0,0,0 }), 5126, 6, "VEC4");
    // Morph weight 0 -> 1 with an out-tangent of 3 on key 0: overshoots early.
    int wv = b.Add(GltfBuilder::F({ 0, 0, 3,   0, 1, 0 }), 5126, 6, "SCALAR");

    std::string json =
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"buffers\":[{\"byteLength\":" + std::to_string(b.buf.size()) +
            ",\"uri\":\"data:application/octet-stream;base64," + b.B64() + "\"}],"
        "\"bufferViews\":[" + b.views + "],"
        "\"accessors\":[" + b.accs + "],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":" + std::to_string(pos) + "},"
            "\"indices\":" + std::to_string(idx) + ",\"targets\":[{\"POSITION\":" + std::to_string(mt) + "}]}],"
            "\"weights\":[0]}],"
        "\"nodes\":[{\"name\":\"Mover\"},{\"name\":\"Spinner\"},{\"name\":\"Blob\",\"mesh\":0}],"
        "\"animations\":[{\"name\":\"Curvy\",\"samplers\":["
            "{\"input\":" + std::to_string(t01) + ",\"output\":" + std::to_string(tv) + ",\"interpolation\":\"CUBICSPLINE\"},"
            "{\"input\":" + std::to_string(t01) + ",\"output\":" + std::to_string(rv) + ",\"interpolation\":\"CUBICSPLINE\"},"
            "{\"input\":" + std::to_string(t01) + ",\"output\":" + std::to_string(wv) + ",\"interpolation\":\"CUBICSPLINE\"}],"
          "\"channels\":["
            "{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"translation\"}},"
            "{\"sampler\":1,\"target\":{\"node\":1,\"path\":\"rotation\"}},"
            "{\"sampler\":2,\"target\":{\"node\":2,\"path\":\"weights\"}}]}]}";
    const char* path = "/tmp/okay_test_cubicspline.gltf";
    { std::ofstream o(path); o << json; }

    Scene s("cubic");
    bool ok = false;
    GameObject* root = ImportModelScene(s, path, &ok);
    CHECK(ok && root);
    if (!root) { TEST_MAIN_RESULT(); }
    const ModelAnimator* ma = root->GetComponent<ModelAnimator>();
    CHECK(ma && !ma->clips.empty());

    // Translation follows the tangent: up to the Hermite bump, not flat at 0.
    const AnimationClip* mover = NodeClip(ma, "Mover");
    CHECK(mover != nullptr);
    if (mover) {
        CHECK(std::fabs(Eval(mover, "position.x", 0.0f)) < 1e-4f);
        CHECK(std::fabs(Eval(mover, "position.x", 0.5f) - 0.5f) < 0.02f);
        CHECK(std::fabs(Eval(mover, "position.x", 0.25f) - 4.0f * (0.015625f - 0.125f + 0.25f)) < 0.03f);
        CHECK(std::fabs(Eval(mover, "position.x", 1.0f)) < 1e-3f);
        CHECK(!mover->smoothKeys);
    }

    // Rotation: 45 deg at the midpoint, unit length throughout.
    const AnimationClip* spin = NodeClip(ma, "Spinner");
    CHECK(spin != nullptr);
    if (spin) {
        for (float t : {0.1f, 0.3f, 0.5f, 0.7f, 0.9f}) {
            float qz = Eval(spin, "rotation.qz", t), qw = Eval(spin, "rotation.qw", t);
            CHECK(std::fabs(qz * qz + qw * qw - 1.0f) < 0.02f);
        }
        float qz = Eval(spin, "rotation.qz", 0.5f), qw = Eval(spin, "rotation.qw", 0.5f);
        CHECK(std::fabs(2.0f * std::atan2(qz, qw) * 57.2957795f - 45.0f) < 1.0f);
    }

    // Morph weights use the tangent too: w(0.25) = h10*3 + h01 = 0.421875 + 0.15625.
    const AnimationClip* blob = NodeClip(ma, "Blob");
    CHECK(blob != nullptr);
    if (blob) {
        CHECK(std::fabs(Eval(blob, "weights.0", 0.25f) - (0.140625f * 3.0f + 0.15625f)) < 0.03f);
        CHECK(std::fabs(Eval(blob, "weights.0", 1.0f) - 1.0f) < 1e-3f);
    }

    TEST_MAIN_RESULT();
}
