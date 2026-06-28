#include "test_framework.hpp"
#include "okay/Core/DataPack.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("datapack");

    // Round-trip: pack then unpack restores the exact bytes.
    {
        std::string plain = "okayscene 1\nobject Cube\n  transform 0 0 0\nend\n";
        std::string packed = DataPack::Pack(plain);
        CHECK(DataPack::IsPacked(packed));
        CHECK(!DataPack::IsPacked(plain));
        CHECK(packed != plain);                       // actually obfuscated
        CHECK(packed.find("Cube") == std::string::npos);  // plaintext not visible
        CHECK(DataPack::Unpack(packed) == plain);     // recovered exactly
    }

    // Unpack passes plaintext through unchanged (back-compat with old builds).
    {
        std::string plain = "just some text";
        CHECK(DataPack::Unpack(plain) == plain);
    }

    // Tamper detection: flipping a body byte fails the checksum -> empty.
    {
        std::string packed = DataPack::Pack("secret level data");
        packed[packed.size() - 1] ^= 0x40;            // corrupt the ciphertext
        CHECK(DataPack::Decode(packed, DataPack::DefaultKey()).empty());
    }

    // Wrong key fails the checksum too.
    {
        std::string packed = DataPack::Encode("hello", 111u, 222u);
        CHECK(DataPack::Decode(packed, 999u).empty());
        CHECK(DataPack::Decode(packed, 111u) == "hello");
    }

    // Empty + binary content survive the round-trip.
    {
        CHECK(DataPack::Unpack(DataPack::Pack("")) == "");
        std::string bin; for (int i = 0; i < 256; ++i) bin += (char)i;
        CHECK(DataPack::Unpack(DataPack::Pack(bin)) == bin);
    }

    // End-to-end: a scene serialized, packed, written, then loaded through the
    // engine's file loader (which transparently unpacks) reconstructs the objects.
    {
        Scene s("ENC"); s.physicsEnabled = false;
        s.CreateGameObject("Hero")->AddComponent<MeshRenderer>();
        std::string text = SceneSerializer::Serialize(s);
        std::string path = "/tmp/okay_enc_test.okayscene";
        { std::ofstream f(path, std::ios::binary); f << DataPack::Pack(text); }

        // The file on disk is obfuscated (no readable object name).
        { std::ifstream f(path, std::ios::binary); std::stringstream ss; ss << f.rdbuf();
          CHECK(DataPack::IsPacked(ss.str()));
          CHECK(ss.str().find("Hero") == std::string::npos); }

        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::LoadFromFile(loaded, path, &err));
        CHECK(loaded.Find("Hero") != nullptr);
        std::remove(path.c_str());
    }

    TEST_MAIN_RESULT();
}
