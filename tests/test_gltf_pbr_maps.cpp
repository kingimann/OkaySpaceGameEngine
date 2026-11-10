#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace okay;

static std::string B64(const std::vector<std::uint8_t>& d) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out; unsigned val = 0; int bits = -6;
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
    b.push_back((std::uint8_t)(v & 0xFF)); b.push_back((std::uint8_t)((v >> 8) & 0xFF));
}

// A one-triangle glTF whose material points at external images, including the
// packed metallic-roughness and emissive textures.
static void WriteGltf(const std::string& path) {
    std::vector<std::uint8_t> buf;
    putF(buf, 0); putF(buf, 0); putF(buf, 0);
    putF(buf, 1); putF(buf, 0); putF(buf, 0);
    putF(buf, 0); putF(buf, 1); putF(buf, 0);
    putU16(buf, 0); putU16(buf, 1); putU16(buf, 2);
    const std::string json =
        "{"
        "\"asset\":{\"version\":\"2.0\"},"
        "\"images\":["
          "{\"uri\":\"albedo.png\"},"
          "{\"uri\":\"normal.png\"},"
          "{\"uri\":\"mr.png\"},"
          "{\"uri\":\"emissive.png\"},"
          "{\"uri\":\"ao.png\"}"
        "],"
        "\"textures\":["
          "{\"source\":0},{\"source\":1},{\"source\":2},{\"source\":3},{\"source\":4}"
        "],"
        "\"materials\":[{"
          "\"name\":\"Brass\","
          "\"pbrMetallicRoughness\":{"
            "\"baseColorFactor\":[0.8,0.6,0.2,1],"
            "\"metallicFactor\":0.9,"
            "\"roughnessFactor\":0.25,"
            "\"baseColorTexture\":{\"index\":0},"
            "\"metallicRoughnessTexture\":{\"index\":2}"
          "},"
          "\"normalTexture\":{\"index\":1},"
          "\"occlusionTexture\":{\"index\":4},"
          "\"emissiveFactor\":[1,0.4,0],"
          "\"emissiveTexture\":{\"index\":3}"
        "}],"
        "\"buffers\":[{\"byteLength\":" + std::to_string(buf.size()) +
            ",\"uri\":\"data:application/octet-stream;base64," + B64(buf) + "\"}],"
        "\"bufferViews\":["
          "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
          "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6}"
        "],"
        "\"accessors\":["
          "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
          "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}"
        "],"
        "\"meshes\":[{\"primitives\":[{"
          "\"attributes\":{\"POSITION\":0},"
          "\"indices\":1,"
          "\"material\":0"
        "}]}],"
        "\"nodes\":[{\"mesh\":0}],"
        "\"scenes\":[{\"nodes\":[0]}],"
        "\"scene\":0"
        "}";
    std::ofstream o(path);
    o << json;
}

int main() {
    RUN_SUITE("gltf packed maps");
    const std::string path = "/tmp/okay_pbr_maps.gltf";
    WriteGltf(path);
    bool ok = false;
    GameObject* root = nullptr;
    {
        Scene s;
        root = ImportModelScene(s, path, &ok);
        CHECK(ok);
        CHECK(root != nullptr);
        MeshRenderer* mr = nullptr;
        for (const auto& up : s.Objects())
            if (auto* m = up->GetComponent<MeshRenderer>(); m && !m->mesh.vertices.empty()) mr = m;
        CHECK(mr != nullptr);
        CHECK(mr->texture.find("albedo.png") != std::string::npos);
        CHECK(mr->normalMap.find("normal.png") != std::string::npos);
        CHECK(mr->metallicRoughnessMap.find("mr.png") != std::string::npos);
        CHECK(mr->emissiveMap.find("emissive.png") != std::string::npos);
        CHECK(mr->aoMap.find("ao.png") != std::string::npos);
        CHECK_NEAR(mr->metallic, 0.9f, 1e-3f);
        CHECK(mr->emissive.r > 0.5f);
        CHECK(mr->shininess > 4.0f);

        std::string text = SceneSerializer::Serialize(s);
        Scene loaded;
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        MeshRenderer* back = nullptr;
        for (const auto& up : loaded.Objects())
            if (auto* m = up->GetComponent<MeshRenderer>(); m && m->metallicRoughnessMap.find("mr.png") != std::string::npos)
                back = m;
        CHECK(back != nullptr);
        CHECK(back->emissiveMap.find("emissive.png") != std::string::npos);
        CHECK(back->normalMap.find("normal.png") != std::string::npos);
        CHECK(back->aoMap.find("ao.png") != std::string::npos);
        CHECK_NEAR(back->metallic, 0.9f, 1e-3f);
        CHECK_NEAR(back->emissive.r, mr->emissive.r, 1e-3f);
    }
    TEST_MAIN_RESULT();
}
