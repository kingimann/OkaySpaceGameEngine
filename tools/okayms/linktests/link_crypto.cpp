// Link-and-run test for four real engine files: engine/src/Crypto/Sha512.cpp, Blake2b.cpp, ChaCha20.cpp and Util.cpp.
// The known-answer vectors are the engine's own (tests/crypto_rfc_vectors.hpp: RFC 8439, RFC 7693, FIPS 180-4 messages);
// the checks follow tests/test_crypto.cpp for these algorithms. Built by okay-cl (/O0 /O1 /O2) and by MinGW g++, run
// under Wine; the exit code and the printed digests must be the same.
#include "mini.hpp"
#include "okay/Crypto/Crypto.hpp"
#include "crypto_rfc_vectors.hpp"
#include <cstring>
#include <string>
#include <vector>

using namespace okay::crypto;
typedef std::vector<std::uint8_t> Bytes;

static Bytes Hex(const char* s) {
    Bytes out;
    const std::size_t n = std::strlen(s);
    for (std::size_t i = 0; i + 1 < n; i += 2) {
        int hi = s[i] <= '9' ? s[i] - '0' : (s[i] | 32) - 'a' + 10;
        int lo = s[i + 1] <= '9' ? s[i + 1] - '0' : (s[i + 1] | 32) - 'a' + 10;
        out.push_back((std::uint8_t)(hi * 16 + lo));
    }
    return out;
}
static Bytes V(const char* name) {
    for (const auto& kv : okay_crypto_vectors::kVectors)
        if (std::strcmp(kv.name, name) == 0) return Hex(kv.hex);
    CHECK(false);
    return Bytes();
}
static Bytes VN(const char* base, int i, const char* suffix) {
    std::string name = std::string(base) + std::to_string(i) + suffix;
    return V(name.c_str());
}
static void Show(const char* label, const Bytes& b) {
    std::printf("%s ", label);
    for (std::size_t i = 0; i < b.size(); ++i) std::printf("%02x", (unsigned)b[i]);
    std::printf("\n");
}
static std::uint32_t CounterOf(const Bytes& c) { return (std::uint32_t)c[0] << 24 | (std::uint32_t)c[1] << 16 | (std::uint32_t)c[2] << 8 | c[3]; }

int main() {
    RUN_SUITE("crypto");

    // ChaCha20 keystream blocks (RFC 8439 A.1): the engine's vectors cc_blk0 .. cc_blk4.
    for (int k = 0; k < 5; ++k) {
        Bytes key = VN("cc_blk", k, "_key"), nonce = VN("cc_blk", k, "_nonce"), want = VN("cc_blk", k, "_ks");
        Bytes zero(64, 0), out(64, 0);
        CHECK(ChaCha20Xor(out.data(), zero.data(), 64, key.data(), nonce.data(), CounterOf(VN("cc_blk", k, "_ctr"))));
        CHECK(out == want);
        if (k == 0) Show("chacha20 blk0", out);
    }

    // Sha512 (FIPS 180-4 messages): abc, empty, two-block and long messages.
    static const char* names[] = {"abc", "empty", "two", "long"};
    for (int i = 0; i < 4; ++i) {
        std::string msg = std::string("sha_") + names[i] + "_msg";
        std::string dig = std::string("sha512_") + names[i];
        Bytes m = V(msg.c_str()), want = V(dig.c_str()), got(64, 0);
        Sha512Hash(got.data(), m.data(), m.size());
        CHECK(got == want);
        if (i == 0) Show("sha512 abc", got);
        // The same message in two pieces through the streaming class.
        Bytes got2(64, 0);
        Sha512 h;
        const std::size_t cut = m.size() / 3;
        h.Update(m.data(), cut);
        h.Update(m.data() + cut, m.size() - cut);
        h.Final(got2.data());
        CHECK(got2 == want);
    }

    // Blake2b (RFC 7693): "abc" with a 64-byte digest.
    {
        Bytes got(64, 0);
        Blake2bHash(got.data(), 64, (const std::uint8_t*)"abc", 3);
        CHECK(got == V("b2b_abc"));
        Show("blake2b abc", got);
        Bytes got2(64, 0);
        Blake2b b(64);
        b.Update((const std::uint8_t*)"a", 1);
        b.Update((const std::uint8_t*)"bc", 2);
        b.Final(got2.data());
        CHECK(got2 == got);
    }

    // Util: ConstantTimeEqual and MemZero.
    {
        std::uint8_t a[8] = {1, 2, 3, 4, 5, 6, 7, 8}, b[8] = {1, 2, 3, 4, 5, 6, 7, 8}, c[8] = {1, 2, 3, 4, 5, 6, 7, 9};
        CHECK(ConstantTimeEqual(a, b, 8));
        CHECK(!ConstantTimeEqual(a, c, 8));
        MemZero(a, 8);
        CHECK(a[0] == 0 && a[7] == 0 && !ConstantTimeEqual(a, b, 8));
    }
    TEST_MAIN_RESULT();
}
