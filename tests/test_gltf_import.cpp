#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace okay;

// Tiny base64 encoder for the test (the loader has its own decoder).
static std::string B64(const std::vector<std::uint8_t>& d) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out; int val = 0, bits = -6;
    for (std::uint8_t c : d) {
        val = (val << 8) + c; bits += 8;
        while (bits >= 0) { out += T[(val >> bits) & 0x3F]; bits -= 6; }
    }
    if (bits > -6) out += T[((val << 8) >> (bits + 8)) & 0x3F];
    while (out.size() % 4) out += '=';
    return out;
}
static void putF(std::vector<std::uint8_t>& b, float f) {
    std::uint8_t t[4]; std::memcpy(t, &f, 4); b.insert(b.end(), t, t + 4);
}
static void putU16(std::vector<std::uint8_t>& b, std::uint16_t v) {
    b.push_back(v & 0xFF); b.push_back((v >> 8) & 0xFF);
}

// Mesh::LoadGLTF imports a glTF 2.0 triangle (positions + normals + indices) via an
// embedded base64 buffer.
int main() {
    RUN_SUITE("gltf_import");

    // Build a binary buffer: 3 positions (vec3 f32), 3 normals (vec3 f32), 3 indices (u16).
    std::vector<std::uint8_t> buf;
    // positions (offset 0, 36 bytes)
    putF(buf,0); putF(buf,0); putF(buf,0);
    putF(buf,1); putF(buf,0); putF(buf,0);
    putF(buf,0); putF(buf,1); putF(buf,0);
    // normals (offset 36, 36 bytes) — all +Z
    for (int i = 0; i < 3; ++i) { putF(buf,0); putF(buf,0); putF(buf,1); }
    // indices (offset 72, 6 bytes)
    putU16(buf,0); putU16(buf,1); putU16(buf,2);

    std::string b64 = B64(buf);
    std::string json =
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"buffers\":[{\"byteLength\":" + std::to_string(buf.size()) +
            ",\"uri\":\"data:application/octet-stream;base64," + b64 + "\"}],"
        "\"bufferViews\":["
            "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
            "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":36},"
            "{\"buffer\":0,\"byteOffset\":72,\"byteLength\":6}],"
        "\"accessors\":["
            "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
            "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
            "{\"bufferView\":2,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1},\"indices\":2}]}]}";

    const char* path = "/tmp/okay_test_tri.gltf";
    { std::ofstream o(path); o << json; }

    bool ok = false;
    Mesh m = LoadGLTF(path, &ok);
    CHECK(ok);
    CHECK(m.vertices.size() == 3);
    CHECK(m.triangles.size() == 3);
    CHECK(m.normals.size() == 3);
    // Second vertex is (1,0,0); normals are +Z.
    CHECK(std::fabs(m.vertices[1].x - 1.0f) < 1e-4f);
    CHECK(std::fabs(m.normals[0].z - 1.0f) < 1e-4f);
    CHECK(m.triangles[0] == 0 && m.triangles[1] == 1 && m.triangles[2] == 2);

    // ---- Scene import: node hierarchy + mesh + a translation animation ----
    {
        std::vector<std::uint8_t> b;
        // positions (off 0, 36): a triangle
        putF(b,0); putF(b,0); putF(b,0);  putF(b,1); putF(b,0); putF(b,0);  putF(b,0); putF(b,1); putF(b,0);
        // indices (off 36, 6)
        putU16(b,0); putU16(b,1); putU16(b,2);
        // anim times (off 42, 8): 0, 1
        putF(b,0); putF(b,1);
        // anim translations (off 50, 24): (0,0,0) -> (5,0,0)
        putF(b,0); putF(b,0); putF(b,0);  putF(b,5); putF(b,0); putF(b,0);
        std::string e = B64(b);
        std::string j =
            "{\"asset\":{\"version\":\"2.0\"},"
            "\"buffers\":[{\"byteLength\":" + std::to_string(b.size()) +
                ",\"uri\":\"data:application/octet-stream;base64," + e + "\"}],"
            "\"bufferViews\":["
                "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
                "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6},"
                "{\"buffer\":0,\"byteOffset\":42,\"byteLength\":8},"
                "{\"buffer\":0,\"byteOffset\":50,\"byteLength\":24}],"
            "\"accessors\":["
                "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
                "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"},"
                "{\"bufferView\":2,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"},"
                "{\"bufferView\":3,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"}],"
            "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
            "\"nodes\":[{\"name\":\"Root\",\"children\":[1]},{\"name\":\"Arm\",\"mesh\":0}],"
            "\"animations\":[{\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"translation\"}}],"
                "\"samplers\":[{\"input\":2,\"output\":3}]}]}";
        const char* sp = "/tmp/okay_test_scene.gltf";
        { std::ofstream o(sp); o << j; }

        Scene s("gltf");
        bool ok2 = false;
        GameObject* root = ImportModelScene(s, sp, &ok2);
        CHECK(ok2);
        CHECK(root != nullptr);
        GameObject* arm = s.Find("Arm");
        CHECK(arm != nullptr);
        if (arm) {
            CHECK(arm->GetComponent<MeshRenderer>() != nullptr);   // mesh attached
            CHECK(arm->transform->Parent() && arm->transform->Parent()->gameObject == s.Find("Root"));  // parented
            auto* an = arm->GetComponent<Animator>();
            CHECK(an != nullptr);                                  // animation imported
            if (an) {
                an->SetTime(0.5f);                                 // mid clip: lerp 0 -> 5
                CHECK(std::fabs(arm->transform->localPosition.x - 2.5f) < 0.05f);
            }
        }
    }

    TEST_MAIN_RESULT();
}
