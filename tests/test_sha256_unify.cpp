// The ONE SHA-256 (okay/Crypto/Sha256.hpp) against the three OLD copies, which are kept here as reference
// implementations (tests/sha256_old_refs.hpp), plus FIPS 180-4 / RFC 4231 / RFC 7914 vectors, the wrappers the account
// service, the updater, the anti-cheat code and the voice chat still call, and password records written by the OLD code.
// (Python hashlib agreement on thousands of random messages: scripts/sha256_python_diff.py + tests/sha256_compare.cpp.)
#include "test_framework.hpp"
#include <okay/Crypto/Sha256.hpp>
#include <okay/Platform/Account/AccountService.hpp>
#include "sha256_old_refs.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>

namespace fs = std::filesystem;
namespace nw = okay::crypto;
namespace acct = okay::account;
using Bytes = std::vector<std::uint8_t>;

static std::string Hex(const std::uint8_t* p, std::size_t n) { return acct::detail::ToHex(p, n); }
static std::string NewHex(const Bytes& m) { std::uint8_t d[32]; nw::Sha256Hash(d, m.data(), m.size()); return Hex(d, 32); }
static std::string Str(const Bytes& b) { return std::string(b.begin(), b.end()); }
static Bytes Pat(std::size_t n) { Bytes b(n); for (std::size_t i = 0; i < n; ++i) b[i] = (std::uint8_t)((i * 7 + 3) & 255); return b; }
static Bytes Rep(std::uint8_t v, std::size_t n) { return Bytes(n, v); }
static Bytes S(const char* s) { return Bytes(s, s + std::strlen(s)); }

int main() {
    RUN_SUITE("sha256_unify");

    // ---- FIPS 180-4 / known answers (digests from Python hashlib; the first three are the FIPS examples) ----
    CHECK(NewHex(S("")) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(NewHex(S("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(NewHex(S("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(NewHex(Rep('a', 1000000)) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    const struct { std::size_t n; const char* d; } lens[] = {
        {0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {1, "084fed08b978af4d7d196a7446a86b58009e636b611db16211b65a9aadff29c5"},
        {55, "e7313d333c272e639f790978283f9eb392e843d0f29b7016828bb1daa4aac70b"},
        {56, "4324d65f3c103567f5589c710bc08f8523f929a9272e3af36fc968e52abc6c27"},
        {63, "81c80242132f230c3bd41b3e63bbcff16107339549214a99614ff26664625055"},
        {64, "39e3d7b6b5d075d37d053ad89b24b41bef4f3c29760c84447cab3f3be1882241"},
        {65, "aacca6ff74fdbb296d165a45cecfa04e5127bc008770fbbdd48006f2d2fae95e"},
        {119, "9ce7368e4daf32341631b492e80359dc9f594b48453cd0dd5bf0b19279cc177e"},
        {120, "7836b787757e95e58b3ca5aec90b1b004e8deba1e50e9675af9cabf1a13a04b5"},
        {127, "a8d23e75d936f303d248888d9b165ee543f4cbafcad3c9dd2a79bd84faa11d07"},
        {128, "d2742f1f4ac6bb7ca2b239ee18402ba8b3f9f8e652d2a72973c2b9ba11c08cf6"},
        {1000, "1e9bc38cbf860b9ec31918b065f9b52476c549a782e0e7990bed8ce3868d2371"}};
    for (const auto& l : lens) CHECK(NewHex(Pat(l.n)) == l.d);

    // ---- differential: new vs the three old copies, every length 0..300 plus random lengths, streamed at random cuts ----
    std::mt19937 rng(20261115);
    int diffs = 0;
    auto Compare = [&](const Bytes& m) {
        std::uint8_t d[32]; nw::Sha256Hash(d, m.data(), m.size());
        okay_old::crypto::Sha256 o1; if (!m.empty()) o1.Update(m.data(), m.size()); std::uint8_t d1[32]; o1.Final(d1);
        okay_old::account::Sha256 o2; o2.Update(m.data(), m.size()); const auto d2 = o2.Digest();
        const auto d3 = okay_old::voice::VoiceSha256(Str(m));
        nw::Sha256 st; std::size_t i = 0;                                   // random split points, including empty updates
        while (i < m.size()) { std::size_t n = std::min<std::size_t>(m.size() - i, rng() % 150); st.Update(m.data() + i, n); i += n; }
        std::uint8_t ds[32]; st.Final(ds);
        if (std::memcmp(d, d1, 32) || std::memcmp(d, d2.data(), 32) || std::memcmp(d, d3.data(), 32) || std::memcmp(d, ds, 32)) ++diffs;
    };
    for (std::size_t n = 0; n <= 300; ++n) { Bytes m(n); for (auto& b : m) b = (std::uint8_t)rng(); Compare(m); }
    for (int i = 0; i < 300; ++i) { Bytes m(rng() % 20000); for (auto& b : m) b = (std::uint8_t)rng(); Compare(m); }
    CHECK(diffs == 0);

    // ---- the account wrapper keeps its behaviour: reuse after Digest(), string updates, hex helper ----
    {
        acct::detail::Sha256 h; h.Update("ab"); h.Update(std::string("c"));
        CHECK(Hex(h.Digest().data(), 32) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        h.Update("abc");                                                    // a fresh message after Digest()
        CHECK(Hex(h.Digest().data(), 32) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        CHECK(acct::detail::Sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    }

    // ---- HMAC-SHA256: RFC 4231 test cases 1 to 7 (case 5 is truncated to 128 bits by the RFC) ----
    {
        const struct { Bytes key, msg; const char* mac; std::size_t cmp; } c[] = {
            {Rep(0x0b, 20), S("Hi There"), "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", 32},
            {S("Jefe"), S("what do ya want for nothing?"), "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", 32},
            {Rep(0xaa, 20), Rep(0xdd, 50), "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe", 32},
            {Pat(0), Rep(0xcd, 50), "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b", 32},
            {Rep(0x0c, 20), S("Test With Truncation"), "a3b6167473100ee06e0c796c2955552b", 16},
            {Rep(0xaa, 131), S("Test Using Larger Than Block-Size Key - Hash Key First"), "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54", 32},
            {Rep(0xaa, 131), S("This is a test using a larger than block-size key and a larger than block-size data. The key needs to be hashed before being used by the HMAC algorithm."),
             "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2", 32}};
        int idx = 0;
        for (auto c4 : c) {
            if (idx == 3) for (int i = 1; i <= 25; ++i) c4.key.push_back((std::uint8_t)i);   // case 4's key is 0x01..0x19
            std::uint8_t mac[32]; nw::HmacSha256Mac(mac, c4.key.data(), c4.key.size(), c4.msg.data(), c4.msg.size());
            CHECK(Hex(mac, c4.cmp) == c4.mac);
            const auto old = okay_old::anticheat::HmacSha256(Str(c4.key), Str(c4.msg));          // the hand-written one agrees
            CHECK(Hex(old.data(), c4.cmp) == c4.mac);
            nw::HmacSha256 h(c4.key.data(), c4.key.size());                                     // streaming, split in two
            h.Update(c4.msg.data(), c4.msg.size() / 2); h.Update(c4.msg.data() + c4.msg.size() / 2, c4.msg.size() - c4.msg.size() / 2);
            std::uint8_t m2[32]; h.Final(m2);
            CHECK(Hex(m2, c4.cmp) == c4.mac);
            ++idx;
        }
        // The anticheat wrapper (shared/AntiCheatChecks.hpp) calls okay::crypto::HmacSha256Mac with the same bytes; the
        // old and new HMAC agree on random keys up to 150 bytes (covers the >64-byte key path) and random messages.
        int bad = 0;
        for (int i = 0; i < 300; ++i) {
            Bytes k(rng() % 151), m(rng() % 300); for (auto& b : k) b = (std::uint8_t)rng(); for (auto& b : m) b = (std::uint8_t)rng();
            std::uint8_t a[32]; nw::HmacSha256Mac(a, k.data(), k.size(), m.data(), m.size());
            const auto o = okay_old::anticheat::HmacSha256(Str(k), Str(m));
            if (std::memcmp(a, o.data(), 32)) ++bad;
        }
        CHECK(bad == 0);
    }

    // ---- PBKDF2-HMAC-SHA256: RFC 7914 section 11 (64-byte outputs), and old vs new on random inputs ----
    {
        Bytes dk(64);
        nw::Pbkdf2HmacSha256(dk.data(), 64, (const std::uint8_t*)"passwd", 6, (const std::uint8_t*)"salt", 4, 1);
        CHECK(Hex(dk.data(), 64) == "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783");
        nw::Pbkdf2HmacSha256(dk.data(), 64, (const std::uint8_t*)"Password", 8, (const std::uint8_t*)"NaCl", 4, 80000);
        CHECK(Hex(dk.data(), 64) == "4ddcd8f60b98be21830cee5ef22701f9641a4418d04c0414aeff08876b34ab56a1d425a1225833549adb841b51c9b3176a272bdebba1d078478f62b397f33c8d");
        int bad = 0;
        for (int i = 0; i < 120; ++i) {
            Bytes p(rng() % 151), salt(rng() % 80); for (auto& b : p) b = (std::uint8_t)rng(); for (auto& b : salt) b = (std::uint8_t)rng();
            const std::uint32_t it = 1 + rng() % 400;
            const auto o = okay_old::account::Pbkdf2Sha256(Str(p), salt, it);
            const auto n = acct::detail::Pbkdf2Sha256(Str(p), salt, it);               // the account service's wrapper
            if (o != n) ++bad;
        }
        CHECK(bad == 0);
    }

    // ---- password records written by the OLD code still verify (made with the code that shipped before the merge) ----
    {
        const std::string salt = "00112233445566778899aabbccddeeff";
        CHECK(acct::detail::HashPassword("oldpass1", salt) == "d10f8ed8ee3048a7a07492e7d6b5b01c32481c2dc3084be52ae3034968651c5e");
        CHECK(acct::detail::HashPasswordV2("correct horse battery staple", salt) ==
              "$pbkdf2-sha256$600000$7c0123695eb46911838d4c16fa259d7280c59060c6031130b8269b624faacd02");        // the real cost
        CHECK(acct::detail::HashPasswordV2(std::string(100, 'x') + "!", salt, 1000) ==
              "$pbkdf2-sha256$1000$38d3b1aabcba0d48b5c9cb1a2fb05eef56ea155131ffd182726de7e30b496126");          // key longer than a block
        CHECK(acct::detail::HashPasswordV2("p\xc3\xa4ssw\xc3\xb6rd", salt, 2000) ==
              "$pbkdf2-sha256$2000$0e41c64aada870145b6e5998d5d47a88d3f302e9c7d597e19f80c37767b5f4b1");
        // And through the real login path: a stored account file with records from the old code.
        fs::path dir = fs::temp_directory_path() / ("okay_sha_unify_" + acct::detail::RandomHex(8));
        fs::remove_all(dir); fs::create_directories(dir);
        {
            std::ofstream(dir / "accounts.db")
                << "v1user\t" << salt << "\td10f8ed8ee3048a7a07492e7d6b5b01c32481c2dc3084be52ae3034968651c5e\n"
                << "v2user\t" << salt << "\t$pbkdf2-sha256$600000$7c0123695eb46911838d4c16fa259d7280c59060c6031130b8269b624faacd02\n";
        }
        acct::AccountService svc(dir);
        CHECK(svc.Login("v2user", "correct horse battery staple").ok);
        CHECK(!svc.Login("v2user", "correct horse battery stapl3").ok);
        svc.Logout();
        CHECK(svc.Login("v1user", "oldpass1").ok);                                    // the SHA-256 chain, then upgraded
        CHECK(!svc.Login("v1user", "oldpass2").ok);
        fs::remove_all(dir);
    }

    // ---- guards: PBKDF2 output length cap, FinalWith32 on a partly filled object ----
    {
        std::uint8_t one[1] = {0x55};
        if (sizeof(std::size_t) >= 8) {   // 2^32 blocks of 32 bytes can only be asked for with a 64-bit size_t
            const std::size_t tooBig = (std::size_t)32 * 0xFFFFFFFFull + 1;
            CHECK(!nw::Pbkdf2HmacSha256(one, tooBig, one, 1, one, 1, 1));
            CHECK(one[0] == 0x55);                                          // nothing was written
        }
        CHECK(nw::Pbkdf2HmacSha256(one, 0, one, 1, one, 1, 1));              // zero bytes: fine, nothing to do
        // FinalWith32 / FinalWith32Words on an object with a partial block pending equals hashing prefix || 32 bytes.
        for (std::size_t pre : {0u, 1u, 33u, 64u, 70u, 128u}) {
            Bytes p = Pat(pre), in = Pat(32);
            nw::Sha256 a; a.Update(p.data(), p.size());
            std::uint8_t got[32]; a.FinalWith32(in.data(), got);
            Bytes all = p; all.insert(all.end(), in.begin(), in.end());
            CHECK(Hex(got, 32) == NewHex(all));
        }
    }

    // ---- hygiene and shape ----
    {
        nw::Sha256 s; s.Update((const std::uint8_t*)"secret", 6);
        s.Wipe();                                                                       // abandoned object: state cleared
        std::uint8_t raw[sizeof s]; std::memcpy(raw, &s, sizeof s);
        bool allZero = true; for (std::size_t i = 0; i < sizeof raw; ++i) if (raw[i]) allZero = false;
        CHECK(allZero);
        nw::Sha256 t; t.Update((const std::uint8_t*)"abc", 3); std::uint8_t d[32]; t.Final(d);
        std::memcpy(raw, &t, sizeof t);
        allZero = true; for (std::size_t i = 0; i < sizeof raw; ++i) if (raw[i]) allZero = false;
        CHECK(allZero);                                                                 // Final() wipes the state and the buffer
        std::uint8_t e[32]; nw::Sha256Hash(e, nullptr, 0);                              // one-shot of nothing accepts a null pointer
        CHECK(Hex(e, 32) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    }

    TEST_MAIN_RESULT();
}
