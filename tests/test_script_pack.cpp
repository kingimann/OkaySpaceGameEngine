#include "test_framework.hpp"
#include <okay/Core/ScriptPack.hpp>

using namespace okay::scriptpack;

int main() {
    RUN_SUITE("script_pack");

    if (!Available()) {
        std::printf("  (engine built with no crypto backend; skipping ScriptPack checks)\n");
        TEST_MAIN_RESULT();
    }

    // Round trip.
    {
        const std::string plain = "function start() { var secret = 42; }\n";
        auto key = MakeBuildKey();
        CHECK(key.size() == kKeyBytes);
        std::string packed;
        CHECK(Pack(plain, key, packed));
        CHECK(IsPacked(packed));
        CHECK(packed.find("secret") == std::string::npos);   // not readable in the pack bytes

        std::string recovered, err;
        CHECK(Unpack(packed, key, recovered, &err));
        CHECK(recovered == plain);
    }

    // Tamper detection: a bit-flip anywhere in the body fails to decrypt, and never crashes.
    {
        const std::string plain = "x = 1\n";
        auto key = MakeBuildKey();
        std::string packed;
        CHECK(Pack(plain, key, packed));
        std::string tampered = packed;
        tampered[tampered.size() - 1] ^= 0x01;
        std::string recovered, err;
        CHECK(!Unpack(tampered, key, recovered, &err));
        CHECK(recovered.empty());
        CHECK(!err.empty());
    }

    // Truncated / garbage input never crashes, just fails.
    {
        auto key = MakeBuildKey();
        std::string recovered, err;
        CHECK(!Unpack("OKE1", key, recovered, &err));
        CHECK(!Unpack("not a pack at all", key, recovered, &err));
        CHECK(recovered.empty());
    }

    // Wrong key fails (different build's key doesn't open another build's pack).
    {
        const std::string plain = "y = 2\n";
        auto keyA = MakeBuildKey();
        auto keyB = MakeBuildKey();
        std::string packed, recovered, err;
        CHECK(Pack(plain, keyA, packed));
        CHECK(!Unpack(packed, keyB, recovered, &err));
    }

    // Two builds get different random keys (so a leaked key from one game does not expose
    // another).
    {
        auto keyA = MakeBuildKey();
        auto keyB = MakeBuildKey();
        CHECK(keyA != keyB);
    }

    TEST_MAIN_RESULT();
}
