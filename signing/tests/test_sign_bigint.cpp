// BigInt tests. NOT INDEPENDENTLY REVIEWED. TEST KEY ONLY. Known answers (bigint_vectors.hpp) plus random identities.
#include <cstdint>
#include <random>
#include <string>

#include "bigint_vectors.hpp"
#include "check.hpp"
#include "okay/Sign/BigInt.hpp"

using okay::sign::BigInt;

static BigInt Hex(const char* s) {
    BigInt v;
    CHECK(BigInt::FromHex(s, v));
    return v;
}

static std::mt19937_64 g_rng(12345);   // deterministic: test inputs only, never keys

static BigInt RandomBig(int maxBits) {
    const int bits = 1 + static_cast<int>(g_rng() % static_cast<unsigned>(maxBits));
    std::vector<std::uint8_t> b((static_cast<std::size_t>(bits) + 7) / 8);
    for (auto& x : b) x = static_cast<std::uint8_t>(g_rng());
    // sometimes make long runs of 0x00 / 0xFF limbs: these hit the rare branches of long division
    if (g_rng() % 4 == 0) for (auto& x : b) if (g_rng() % 3 == 0) x = (g_rng() & 1) ? 0xFF : 0x00;
    return BigInt::FromBytes(b);
}

static void KnownAnswers() {
    for (const BigVec& v : kBigVecs) {
        const BigInt a = Hex(v.a), b = Hex(v.b);
        CHECK(BigInt::Add(a, b) == Hex(v.sum));
        BigInt d;
        CHECK(BigInt::Sub(Hex(v.sum), b, d) && d == a);
        CHECK(BigInt::Mul(a, b) == Hex(v.prod));
        BigInt q, r;
        CHECK(BigInt::DivMod(a, b, q, r));
        CHECK(q == Hex(v.quot));
        CHECK(r == Hex(v.rem));
        CHECK(a.ToHex() == v.a);
    }
    for (const ExpVec& v : kExpVecs) {
        BigInt out;
        CHECK(BigInt::ModExp(Hex(v.base), Hex(v.exp), Hex(v.mod), out));
        CHECK(out == Hex(v.result));
    }
    for (const InvVec& v : kInvVecs) {
        BigInt out;
        CHECK(BigInt::ModInverse(Hex(v.a), Hex(v.m), out));
        CHECK(out == Hex(v.inv));
    }
}

static void SmallEdges() {
    const BigInt zero, one = BigInt::FromU64(1);
    BigInt out, q, r;
    CHECK(zero.IsZero() && zero.BitLength() == 0 && zero.ToHex() == "0");
    CHECK(zero.ToBytes() == std::vector<std::uint8_t>{0});
    CHECK(BigInt::FromU64(0xFFFFFFFFFFFFFFFFull).ToHex() == "ffffffffffffffff");
    std::uint64_t u = 0;
    CHECK(BigInt::FromU64(0x123456789ABCDEF0ull).ToU64(u) && u == 0x123456789ABCDEF0ull);
    CHECK(!Hex("10000000000000000").ToU64(u));
    CHECK(!BigInt::Sub(one, BigInt::FromU64(2), out) && out.IsZero());   // below zero
    CHECK(!BigInt::DivMod(one, zero, q, r));                              // divide by zero
    CHECK(!BigInt::ModExp(one, one, zero, out));
    CHECK(BigInt::ModExp(BigInt::FromU64(5), BigInt::FromU64(3), one, out) && out.IsZero());   // mod 1
    CHECK(BigInt::ModExp(BigInt::FromU64(5), zero, BigInt::FromU64(7), out) && out == one);     // x^0 = 1
    CHECK(!BigInt::ModInverse(BigInt::FromU64(6), BigInt::FromU64(9), out));   // gcd 3
    CHECK(!BigInt::ModInverse(BigInt::FromU64(0), BigInt::FromU64(9), out));
    CHECK(!BigInt::ModInverse(BigInt::FromU64(3), one, out));
    CHECK(BigInt::Gcd(BigInt::FromU64(48), BigInt::FromU64(180)) == BigInt::FromU64(12));
    CHECK(BigInt::Gcd(zero, BigInt::FromU64(7)) == BigInt::FromU64(7));
    // Bytes
    const std::vector<std::uint8_t> be{0x00, 0x00, 0x01, 0x02, 0x03};
    const BigInt v = BigInt::FromBytes(be);
    CHECK(v.ToHex() == "10203");
    CHECK((v.ToBytes() == std::vector<std::uint8_t>{1, 2, 3}));
    CHECK((v.ToBytes(5) == be));
    CHECK(v.ToBytes(2).empty());   // does not fit
    CHECK(BigInt::FromBytes(nullptr, 0).IsZero());
    bool ok = BigInt::FromHex("12g4", out);
    CHECK(!ok && out.IsZero());
    CHECK(BigInt::FromHex("ABC", out) && out.ToHex() == "abc");
    CHECK(Hex("100").BitLength() == 9 && Hex("100").Bit(8) && !Hex("100").Bit(7));
    CHECK(BigInt::Compare(Hex("ffffffff"), Hex("100000000")) < 0);
    CHECK(BigInt::ShiftLeft(one, 100).BitLength() == 101);
    CHECK(BigInt::ShiftRight(BigInt::ShiftLeft(Hex("abcdef"), 77), 77) == Hex("abcdef"));
    CHECK(BigInt::ShiftRight(Hex("ff"), 64).IsZero());
}

static void RandomIdentities() {
    for (int i = 0; i < 3000; ++i) {
        const BigInt a = RandomBig(300), b = BigInt::Add(RandomBig(300), BigInt::FromU64(1));   // b != 0
        BigInt t;
        // (a+b)-b == a, a+b == b+a
        CHECK(BigInt::Add(a, b) == BigInt::Add(b, a));
        CHECK(BigInt::Sub(BigInt::Add(a, b), b, t) && t == a);
        // a*b == b*a; (a*b)/b == a with remainder 0
        const BigInt p = BigInt::Mul(a, b);
        CHECK(p == BigInt::Mul(b, a));
        BigInt q, r;
        CHECK(BigInt::DivMod(p, b, q, r) && q == a && r.IsZero());
        // divmod identity q*d + r == n and r < d, for independent n and d
        const BigInt n = RandomBig(600), d = BigInt::Add(RandomBig(280), BigInt::FromU64(1));
        CHECK(BigInt::DivMod(n, d, q, r));
        CHECK(BigInt::Add(BigInt::Mul(q, d), r) == n);
        CHECK(BigInt::Compare(r, d) < 0);
        // distributive: a*(b+c) == a*b + a*c
        const BigInt c = RandomBig(200);
        CHECK(BigInt::Mul(a, BigInt::Add(b, c)) == BigInt::Add(BigInt::Mul(a, b), BigInt::Mul(a, c)));
        // bytes and hex round trip
        CHECK(BigInt::FromBytes(a.ToBytes()) == a);
        CHECK(Hex(a.ToHex().c_str()) == a);
    }
}

// Limbs picked from the edge values 0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF: these make the quotient-digit estimate of long
// division wrong by one often, so the "add back" step runs. The divmod identity must still hold.
static void DivisionEdgeLimbs() {
    const std::uint32_t edge[] = {0u, 1u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu, 0xFFFFFFFEu};
    auto make = [&](int limbs) {
        std::vector<std::uint8_t> b;
        for (int i = 0; i < limbs; ++i) { const std::uint32_t v = edge[g_rng() % 6]; for (int k = 3; k >= 0; --k) b.push_back(static_cast<std::uint8_t>(v >> (8 * k))); }
        return BigInt::FromBytes(b);
    };
    int done = 0;
    for (int i = 0; i < 30000; ++i) {
        const BigInt n = make(2 + static_cast<int>(g_rng() % 7)), d = make(2 + static_cast<int>(g_rng() % 4));
        if (d.IsZero()) continue;
        BigInt q, r;
        CHECK(BigInt::DivMod(n, d, q, r));
        CHECK(BigInt::Add(BigInt::Mul(q, d), r) == n && BigInt::Compare(r, d) < 0);
        ++done;
    }
    CHECK(done > 20000);
}

static void ModExpAgainstSlow() {   // modexp against slow repeated multiplication on small values
    for (int i = 0; i < 400; ++i) {
        const std::uint64_t m = 2 + g_rng() % 1000003, b = g_rng() % 100000, e = g_rng() % 200;
        std::uint64_t slow = 1 % m;
        for (std::uint64_t k = 0; k < e; ++k) slow = (slow * (b % m)) % m;
        BigInt out;
        CHECK(BigInt::ModExp(BigInt::FromU64(b), BigInt::FromU64(e), BigInt::FromU64(m), out));
        CHECK(out == BigInt::FromU64(slow));
    }
    // Wider: repeated multiplication with a big modulus, exponent up to 60.
    for (int i = 0; i < 40; ++i) {
        BigInt m = RandomBig(400);
        if (BigInt::Compare(m, BigInt::FromU64(2)) < 0) continue;
        const BigInt b = RandomBig(400);
        const unsigned e = static_cast<unsigned>(g_rng() % 60);
        BigInt slow = BigInt::FromU64(1), q, r;
        BigInt::DivMod(slow, m, q, r);
        slow = r;
        BigInt bm;
        BigInt::DivMod(b, m, q, bm);
        for (unsigned k = 0; k < e; ++k) { BigInt::DivMod(BigInt::Mul(slow, bm), m, q, r); slow = r; }
        BigInt out;
        CHECK(BigInt::ModExp(b, BigInt::FromU64(e), m, out) && out == slow);
    }
}

static void InverseProperty() {
    for (int i = 0; i < 300; ++i) {
        const BigInt m = BigInt::Add(RandomBig(300), BigInt::FromU64(2));
        const BigInt a = RandomBig(300);
        BigInt inv, q, r;
        const bool coprime = BigInt::Gcd(a, m) == BigInt::FromU64(1);
        const bool got = BigInt::ModInverse(a, m, inv);
        CHECK(got == coprime);
        if (got) {
            BigInt::DivMod(BigInt::Mul(a, inv), m, q, r);
            CHECK(r == BigInt::FromU64(1));
            CHECK(BigInt::Compare(inv, m) < 0);
        }
    }
    // Fermat: a^(p-1) = 1 mod p for the prime 2^127 - 1
    BigInt p, pm1, out;
    BigInt::Sub(BigInt::ShiftLeft(BigInt::FromU64(1), 127), BigInt::FromU64(1), p);
    BigInt::Sub(p, BigInt::FromU64(1), pm1);
    CHECK(BigInt::ModExp(BigInt::FromU64(123456789), pm1, p, out) && out == BigInt::FromU64(1));
}

int main() {
    KnownAnswers();
    SmallEdges();
    RandomIdentities();
    DivisionEdgeLimbs();
    ModExpAgainstSlow();
    InverseProperty();
    FINISH("test_sign_bigint");
}
