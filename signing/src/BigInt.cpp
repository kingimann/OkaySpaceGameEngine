// okay::sign::BigInt. NOT INDEPENDENTLY REVIEWED. TEST KEY ONLY. See BigInt.hpp. Written from Knuth TAOCP vol. 2, 4.3.1.
#include "okay/Sign/BigInt.hpp"

#include <algorithm>

namespace okay::sign {

namespace {
constexpr std::uint64_t kBase = 1ull << 32;
}

void BigInt::Trim() {
    while (!limbs_.empty() && limbs_.back() == 0) limbs_.pop_back();
}

BigInt BigInt::FromU64(std::uint64_t v) {
    BigInt r;
    if (v != 0) r.limbs_.push_back(static_cast<std::uint32_t>(v));
    if ((v >> 32) != 0) r.limbs_.push_back(static_cast<std::uint32_t>(v >> 32));
    return r;
}

BigInt BigInt::FromBytes(const std::uint8_t* p, std::size_t n) {
    BigInt r;
    r.limbs_.assign((n + 3) / 4, 0);
    for (std::size_t i = 0; i < n; ++i) {   // byte i from the end is byte i of the little-endian number
        const std::uint8_t b = p[n - 1 - i];
        r.limbs_[i / 4] |= static_cast<std::uint32_t>(b) << (8 * (i % 4));
    }
    r.Trim();
    return r;
}

bool BigInt::FromHex(const std::string& hex, BigInt& out) {
    out = BigInt();
    std::vector<std::uint8_t> bytes;
    std::string h = hex;
    if (h.size() % 2 != 0) h.insert(h.begin(), '0');
    auto val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < h.size(); i += 2) {
        const int a = val(h[i]), b = val(h[i + 1]);
        if (a < 0 || b < 0) return false;
        bytes.push_back(static_cast<std::uint8_t>(a * 16 + b));
    }
    out = FromBytes(bytes);
    return true;
}

std::vector<std::uint8_t> BigInt::ToBytes(std::size_t fixedLen) const {
    const std::size_t need = std::max<std::size_t>(1, (BitLength() + 7) / 8);
    const std::size_t len = fixedLen == 0 ? need : fixedLen;
    if (len < need && !(IsZero())) return {};
    std::vector<std::uint8_t> out(len, 0);
    for (std::size_t i = 0; i < limbs_.size() * 4 && i < len; ++i)
        out[len - 1 - i] = static_cast<std::uint8_t>(limbs_[i / 4] >> (8 * (i % 4)));
    return out;
}

std::string BigInt::ToHex() const {
    static const char* d = "0123456789abcdef";
    const std::vector<std::uint8_t> b = ToBytes();
    std::string s;
    for (std::uint8_t x : b) {
        s.push_back(d[x >> 4]);
        s.push_back(d[x & 15]);
    }
    const std::size_t nz = s.find_first_not_of('0');
    return nz == std::string::npos ? "0" : s.substr(nz);
}

bool BigInt::ToU64(std::uint64_t& out) const {
    out = 0;
    if (limbs_.size() > 2) return false;
    if (limbs_.size() > 0) out = limbs_[0];
    if (limbs_.size() > 1) out |= static_cast<std::uint64_t>(limbs_[1]) << 32;
    return true;
}

std::size_t BigInt::BitLength() const {
    if (limbs_.empty()) return 0;
    std::uint32_t top = limbs_.back();
    std::size_t bits = 0;
    while (top != 0) {
        ++bits;
        top >>= 1;
    }
    return (limbs_.size() - 1) * 32 + bits;
}

bool BigInt::Bit(std::size_t i) const {
    const std::size_t l = i / 32;
    return l < limbs_.size() && ((limbs_[l] >> (i % 32)) & 1u) != 0;
}

int BigInt::Compare(const BigInt& a, const BigInt& b) {
    if (a.limbs_.size() != b.limbs_.size()) return a.limbs_.size() < b.limbs_.size() ? -1 : 1;
    for (std::size_t i = a.limbs_.size(); i-- > 0;)
        if (a.limbs_[i] != b.limbs_[i]) return a.limbs_[i] < b.limbs_[i] ? -1 : 1;
    return 0;
}

BigInt BigInt::Add(const BigInt& a, const BigInt& b) {   // Knuth algorithm A
    const BigInt& x = a.limbs_.size() >= b.limbs_.size() ? a : b;
    const BigInt& y = a.limbs_.size() >= b.limbs_.size() ? b : a;
    BigInt r;
    r.limbs_.resize(x.limbs_.size() + 1);
    std::uint64_t carry = 0;
    for (std::size_t i = 0; i < x.limbs_.size(); ++i) {
        const std::uint64_t s = static_cast<std::uint64_t>(x.limbs_[i]) + (i < y.limbs_.size() ? y.limbs_[i] : 0) + carry;
        r.limbs_[i] = static_cast<std::uint32_t>(s);
        carry = s >> 32;
    }
    r.limbs_[x.limbs_.size()] = static_cast<std::uint32_t>(carry);
    r.Trim();
    return r;
}

bool BigInt::Sub(const BigInt& a, const BigInt& b, BigInt& out) {   // Knuth algorithm S
    out = BigInt();
    if (Compare(a, b) < 0) return false;
    BigInt r;
    r.limbs_.resize(a.limbs_.size());
    std::int64_t borrow = 0;
    for (std::size_t i = 0; i < a.limbs_.size(); ++i) {
        std::int64_t d = static_cast<std::int64_t>(a.limbs_[i]) - (i < b.limbs_.size() ? b.limbs_[i] : 0) - borrow;
        borrow = d < 0 ? 1 : 0;
        if (d < 0) d += static_cast<std::int64_t>(kBase);
        r.limbs_[i] = static_cast<std::uint32_t>(d);
    }
    r.Trim();
    out = std::move(r);
    return true;
}

BigInt BigInt::Mul(const BigInt& a, const BigInt& b) {   // Knuth algorithm M
    BigInt r;
    if (a.IsZero() || b.IsZero()) return r;
    r.limbs_.assign(a.limbs_.size() + b.limbs_.size(), 0);
    for (std::size_t j = 0; j < b.limbs_.size(); ++j) {
        std::uint64_t carry = 0;
        for (std::size_t i = 0; i < a.limbs_.size(); ++i) {
            const std::uint64_t t = static_cast<std::uint64_t>(a.limbs_[i]) * b.limbs_[j] + r.limbs_[i + j] + carry;
            r.limbs_[i + j] = static_cast<std::uint32_t>(t);
            carry = t >> 32;
        }
        r.limbs_[j + a.limbs_.size()] = static_cast<std::uint32_t>(carry);
    }
    r.Trim();
    return r;
}

BigInt BigInt::ShiftLeft(const BigInt& a, std::size_t bits) {
    if (a.IsZero()) return a;
    const std::size_t ls = bits / 32, bs = bits % 32;
    BigInt r;
    r.limbs_.assign(a.limbs_.size() + ls + 1, 0);
    for (std::size_t i = 0; i < a.limbs_.size(); ++i) {
        const std::uint64_t v = static_cast<std::uint64_t>(a.limbs_[i]) << bs;
        r.limbs_[i + ls] |= static_cast<std::uint32_t>(v);
        r.limbs_[i + ls + 1] |= static_cast<std::uint32_t>(v >> 32);
    }
    r.Trim();
    return r;
}

BigInt BigInt::ShiftRight(const BigInt& a, std::size_t bits) {
    const std::size_t ls = bits / 32, bs = bits % 32;
    BigInt r;
    if (ls >= a.limbs_.size()) return r;
    r.limbs_.assign(a.limbs_.size() - ls, 0);
    for (std::size_t i = ls; i < a.limbs_.size(); ++i) {
        std::uint64_t v = a.limbs_[i];
        if (i + 1 < a.limbs_.size()) v |= static_cast<std::uint64_t>(a.limbs_[i + 1]) << 32;
        r.limbs_[i - ls] = static_cast<std::uint32_t>(v >> bs);
    }
    r.Trim();
    return r;
}

bool BigInt::DivMod(const BigInt& a, const BigInt& b, BigInt& q, BigInt& r) {   // Knuth algorithm D
    q = BigInt();
    r = BigInt();
    if (b.IsZero()) return false;
    if (Compare(a, b) < 0) {
        r = a;
        return true;
    }
    const std::size_t n = b.limbs_.size();
    if (n == 1) {   // one-limb divisor: simple short division
        const std::uint64_t d = b.limbs_[0];
        q.limbs_.assign(a.limbs_.size(), 0);
        std::uint64_t rem = 0;
        for (std::size_t i = a.limbs_.size(); i-- > 0;) {
            const std::uint64_t cur = (rem << 32) | a.limbs_[i];
            q.limbs_[i] = static_cast<std::uint32_t>(cur / d);
            rem = cur % d;
        }
        q.Trim();
        r = FromU64(rem);
        return true;
    }
    // D1: scale so the top limb of the divisor has its high bit set.
    std::size_t s = 0;
    for (std::uint32_t t = b.limbs_.back(); (t & 0x80000000u) == 0; t <<= 1) ++s;
    const BigInt vN = ShiftLeft(b, s);
    BigInt uN = ShiftLeft(a, s);
    std::vector<std::uint32_t> v = vN.limbs_;   // exactly n limbs
    std::vector<std::uint32_t> u = uN.limbs_;
    const std::size_t m = a.limbs_.size() - n;
    u.resize(a.limbs_.size() + 1, 0);           // u has m + n + 1 limbs
    std::vector<std::uint32_t> quot(m + 1, 0);
    for (std::size_t jj = m + 1; jj-- > 0;) {   // D2..D7, j = m down to 0
        const std::size_t j = jj;
        // D3: estimate the quotient digit from the top two limbs of the current remainder.
        const std::uint64_t num = (static_cast<std::uint64_t>(u[j + n]) << 32) | u[j + n - 1];
        std::uint64_t qhat = num / v[n - 1];
        std::uint64_t rhat = num % v[n - 1];
        while (qhat >= kBase || qhat * v[n - 2] > ((rhat << 32) | u[j + n - 2])) {
            --qhat;
            rhat += v[n - 1];
            if (rhat >= kBase) break;
        }
        // D4: multiply and subtract qhat * v from u[j .. j+n].
        std::int64_t borrow = 0;
        std::uint64_t carry = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint64_t p = qhat * v[i] + carry;
            carry = p >> 32;
            const std::int64_t t = static_cast<std::int64_t>(u[i + j]) - borrow - static_cast<std::int64_t>(p & 0xFFFFFFFFu);
            u[i + j] = static_cast<std::uint32_t>(t);
            borrow = t < 0 ? 1 : 0;
        }
        const std::int64_t t = static_cast<std::int64_t>(u[j + n]) - borrow - static_cast<std::int64_t>(carry);
        u[j + n] = static_cast<std::uint32_t>(t);
        // D5/D6: if the result went negative, the estimate was one too big: add v back.
        if (t < 0) {
            --qhat;
            std::uint64_t c = 0;
            for (std::size_t i = 0; i < n; ++i) {
                const std::uint64_t sum = static_cast<std::uint64_t>(u[i + j]) + v[i] + c;
                u[i + j] = static_cast<std::uint32_t>(sum);
                c = sum >> 32;
            }
            u[j + n] = static_cast<std::uint32_t>(u[j + n] + c);
        }
        quot[j] = static_cast<std::uint32_t>(qhat);
    }
    q.limbs_ = quot;
    q.Trim();
    BigInt rem;   // D8: the remainder is u[0..n-1], scaled back down
    rem.limbs_.assign(u.begin(), u.begin() + static_cast<std::ptrdiff_t>(n));
    rem.Trim();
    r = ShiftRight(rem, s);
    return true;
}

BigInt BigInt::Gcd(const BigInt& a, const BigInt& b) {
    BigInt x = a, y = b, q, r;
    while (!y.IsZero()) {
        DivMod(x, y, q, r);
        x = y;
        y = r;
    }
    return x;
}

bool BigInt::ModExp(const BigInt& base, const BigInt& exp, const BigInt& m, BigInt& out) {
    out = BigInt();
    if (m.IsZero()) return false;
    BigInt q, r;
    if (m == FromU64(1)) return true;
    DivMod(base, m, q, r);
    const BigInt b = r;
    BigInt result = FromU64(1);
    for (std::size_t i = exp.BitLength(); i-- > 0;) {   // left to right binary method
        DivMod(Mul(result, result), m, q, r);
        result = r;
        if (exp.Bit(i)) {
            DivMod(Mul(result, b), m, q, r);
            result = r;
        }
    }
    out = result;
    return true;
}

bool BigInt::ModInverse(const BigInt& a, const BigInt& m, BigInt& out) {
    out = BigInt();
    if (Compare(m, FromU64(2)) < 0) return false;
    // Extended Euclid on non-negative numbers: keep t0, t1 with t_i * a == r_i (mod m).
    BigInt q, r, r0 = m, r1, t0, t1 = FromU64(1);
    DivMod(a, m, q, r1);
    while (!r1.IsZero()) {
        DivMod(r0, r1, q, r);   // r0 = q*r1 + r
        BigInt qt, qtm, diff;
        DivMod(Mul(q, t1), m, qtm, qt);   // qt = q*t1 mod m
        Sub(Add(t0, m), qt, diff);        // t0 - qt, kept non-negative by adding m first
        BigInt t2, junk;
        DivMod(diff, m, junk, t2);
        r0 = r1;
        r1 = r;
        t0 = t1;
        t1 = t2;
    }
    if (r0 != FromU64(1)) return false;
    out = t0;
    return true;
}

}  // namespace okay::sign
