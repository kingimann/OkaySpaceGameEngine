// The output-preserving fast paths of our FBX reader (okay::fbx), checked bit for bit against the plain code they
// replaced, on random inputs that include -0, infinities, NaN and huge values (docs/own_fbx_reader.md, "Speed and
// memory"). White-box: this file compiles FbxScene.cpp itself (and test_fbx_exact_numbers.cpp compiles FbxReader.cpp),
// so the internal functions can be called. Standalone: no engine library.
//   - matrix products that drop the terms that are exactly zero (Mul, MulTranslate, MulScale, the one-axis rotations
//     of Euler, LocalMatrix and its short path LocalSimple) against the plain 4x4 products;
//   - curve evaluation that remembers its last key segment, with flags and slopes per attribute set, against the plain
//     search over one copy per key (the storage before);
//   - Base64 four characters at a time against the bit-by-bit decoder;
//   - the ASCII number fast path against FastDecimal and strtod, and ASCII arrays kept as int32 / float32 against the
//     parsed doubles (test_fbx_exact_numbers.cpp).
#include "test_framework.hpp"
#include "../engine/src/Render/FbxScene.cpp"

#include <cstdio>
#include <random>

long FbxExactNumberChecks(long& checks);   // test_fbx_exact_numbers.cpp
long FbxExactArrayChecks(long& checks, long& packed);   // test_fbx_exact_numbers.cpp

using namespace okay::fbx;

namespace ref {   // the plain versions, as they were before the fast paths
// Base64 as it was decoded before (bit by bit into a string), for the four-at-a-time decoder.
std::string Base64Old(std::string_view in) {
    signed char tab[256]; std::memset(tab, -1, sizeof(tab));
    const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 64; ++i) tab[(unsigned char)a[i]] = (signed char)i;
    std::string out; unsigned acc = 0; int bits = 0;
    for (unsigned char c : in) {
        if (tab[c] < 0) continue;
        acc = (acc << 6) | (unsigned)tab[c]; bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((char)((acc >> bits) & 0xFF)); }
    }
    return out;
}
// The curve as it was stored before flags and slopes were kept per attribute set: one copy per key, plain search.
struct OldCurve {
    std::vector<double> t, v; std::vector<int> flags; std::vector<double> sr, sl; bool hasSlopes = false;
    double Eval(double time) const {
        const std::size_t n = t.size();
        if (n == 0) return 0;
        if (time <= t[0] || n == 1) return v[0];
        if (time >= t[n - 1]) return v[n - 1];
        std::size_t lo = 0, hi = n - 1;
        while (hi - lo > 1) { const std::size_t mid = (lo + hi) / 2; if (t[mid] <= time) lo = mid; else hi = mid; }
        hi = lo + 1;
        const double dt = t[hi] - t[lo];
        if (dt <= 0) return v[lo];
        const int f = flags[lo];
        if (f & 0x2) return v[lo];
        const double u = (time - t[lo]) / dt;
        if (f & 0x4) return v[lo] + (v[hi] - v[lo]) * u;
        double m0 = sr.empty() ? 0.0 : sr[lo], m1 = sl.empty() ? 0.0 : sl[lo];
        if ((f & 0x300) && !hasSlopes) { m0 = AutoSlope(lo, f); m1 = AutoSlope(hi, flags[hi]); }
        const double u2 = u * u, u3 = u2 * u;
        return (2 * u3 - 3 * u2 + 1) * v[lo] + (u3 - 2 * u2 + u) * dt * m0 + (-2 * u3 + 3 * u2) * v[hi] + (u3 - u2) * dt * m1;
    }
    double AutoSlope(std::size_t i, int f) const {
        const std::size_t n = t.size();
        if (i == 0 || i + 1 >= n) return 0;
        const double dt = t[i + 1] - t[i - 1];
        if (dt <= 0) return 0;
        if ((f & 0x1000) && ((v[i] >= v[i - 1] && v[i] >= v[i + 1]) || (v[i] <= v[i - 1] && v[i] <= v[i + 1]))) return 0;
        return (v[i + 1] - v[i - 1]) / dt;
    }
};
Mat Mul(const Mat& a, const Mat& b) {
    Mat r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) { double s = 0; for (int k = 0; k < 4; ++k) s += a.m[i * 4 + k] * b.m[k * 4 + j]; r.m[i * 4 + j] = s; }
    return r;
}
Mat Translate(double x, double y, double z) { Mat r; r.m[3] = x; r.m[7] = y; r.m[11] = z; return r; }
Mat ScaleM(double x, double y, double z) { Mat r; r.m[0] = x; r.m[5] = y; r.m[10] = z; return r; }
Mat RotAxis(int axis, double deg) {
    const double a = deg * 3.14159265358979323846 / 180.0, c = std::cos(a), s = std::sin(a);
    Mat r;
    if (axis == 0) { r.m[5] = c; r.m[6] = -s; r.m[9] = s; r.m[10] = c; }
    else if (axis == 1) { r.m[0] = c; r.m[2] = s; r.m[8] = -s; r.m[10] = c; }
    else { r.m[0] = c; r.m[1] = -s; r.m[4] = s; r.m[5] = c; }
    return r;
}
Mat Euler(const double d[3], int order) {
    static const int seq[6][3] = {{0,1,2},{0,2,1},{1,2,0},{1,0,2},{2,0,1},{2,1,0}};
    if (order < 0 || order > 5) order = 0;
    Mat r;
    for (int k = 0; k < 3; ++k) r = Mul(RotAxis(seq[order][k], d[seq[order][k]]), r);
    return r;
}
} // namespace ref

int main() {
    std::mt19937_64 g(7);
    auto rnd = [&](bool wild) -> double {
        switch (g() % (wild ? 12 : 8)) {
            case 0: return 0.0; case 1: return -0.0; case 2: return 1.0; case 3: return (double)(int)(g() % 200) - 100;
            case 4: return std::ldexp((double)(g() >> 11), -53) * 360 - 180; case 5: return std::ldexp((double)(g() >> 11), -60);
            case 6: return -std::ldexp((double)(g() >> 11), -40); case 7: return std::ldexp((double)(g() >> 11), (int)(g() % 40) - 20);
            case 8: return INFINITY; case 9: return -INFINITY; case 10: return NAN; default: return 1e200 * (double)(g() % 3);
        }
    };
    // Bit for bit, except that any NaN equals any NaN: which NaN (sign, payload) a product of NaNs gives depends on the
    // order the compiler happens to give the operands (vectorised or not), so the old code did not define it either.
    auto same = [](const Mat& a, const Mat& b) {
        for (int i = 0; i < 16; ++i) {
            if (std::isnan(a.m[i]) && std::isnan(b.m[i])) continue;
            if (std::memcmp(&a.m[i], &b.m[i], 8) != 0) return false;
        }
        return true;
    };

    RUN_SUITE("fbx exact: matrix fast paths against the plain products");
    long mul = 0, tr = 0, scl = 0, eul = 0, loc = 0, simple = 0, simpleTried = 0;
    for (long it = 0; it < 300000; ++it) {
        const bool wild = (it % 10) == 0;   // one in ten with infinities, NaN and huge values
        Mat a, b;
        for (int i = 0; i < 12; ++i) { a.m[i] = rnd(wild); b.m[i] = rnd(wild); }
        if (g() % 8 == 0) a.m[12 + g() % 4] = rnd(wild);
        if (!same(Mul(a, b), ref::Mul(a, b))) ++mul;
        double t[3] = {rnd(wild), rnd(wild), rnd(wild)};
        if (!same(MulTranslate(a, t[0], t[1], t[2]), ref::Mul(a, ref::Translate(t[0], t[1], t[2])))) ++tr;
        if (!same(MulScale(a, t[0], t[1], t[2]), ref::Mul(a, ref::ScaleM(t[0], t[1], t[2])))) ++scl;
        const int order = (int)(g() % 7);
        if (!same(Euler(t, order), ref::Euler(t, order))) ++eul;
        Builder::XProps x{};
        auto fill = [&](double* v, bool zero) { for (int k = 0; k < 3; ++k) v[k] = zero ? ((g() & 1) ? 0.0 : -0.0) : rnd(wild); };
        fill(x.t, false); fill(x.r, false); fill(x.s, false);
        const bool plain = g() % 2;
        fill(x.roff, plain && g() % 4); fill(x.rp, plain && g() % 4); fill(x.soff, plain && g() % 4); fill(x.sp, plain && g() % 4);
        fill(x.pre, g() % 2); fill(x.post, g() % 2);
        x.order = (int)(g() % 6);
        const Builder::FixedRot fr = Builder::FixedRotations(x);
        const Mat R = Euler(x.r, x.order);
        Mat want = ref::Translate(x.t[0], x.t[1], x.t[2]);
        want = ref::Mul(want, ref::Translate(x.roff[0], x.roff[1], x.roff[2])); want = ref::Mul(want, ref::Translate(x.rp[0], x.rp[1], x.rp[2]));
        want = ref::Mul(want, fr.pre); want = ref::Mul(want, ref::Euler(x.r, x.order)); want = ref::Mul(want, fr.postInv);
        want = ref::Mul(want, ref::Translate(-x.rp[0], -x.rp[1], -x.rp[2])); want = ref::Mul(want, ref::Translate(x.soff[0], x.soff[1], x.soff[2]));
        want = ref::Mul(want, ref::Translate(x.sp[0], x.sp[1], x.sp[2])); want = ref::Mul(want, ref::ScaleM(x.s[0], x.s[1], x.s[2]));
        want = ref::Mul(want, ref::Translate(-x.sp[0], -x.sp[1], -x.sp[2]));
        if (!same(Builder::LocalMatrix(x, fr, R), want)) ++loc;
        if (fr.simple && Small3(x.t) && Small3(x.r) && Small3(x.s)) { ++simpleTried; if (!same(Builder::LocalSimple(x, fr, R), want)) ++simple; }
    }
    CHECK(mul == 0); CHECK(tr == 0); CHECK(scl == 0); CHECK(eul == 0); CHECK(loc == 0);
    CHECK(simpleTried > 10000); CHECK(simple == 0);

    RUN_SUITE("fbx exact: curve evaluation with a remembered segment, flags and slopes per attribute set");
    long cbad = 0, cn = 0;
    for (int c = 0; c < 2000; ++c) {
        Curve cv; ref::OldCurve oc; const int nk = 1 + (int)(g() % 40); double tt = -1;
        // the curve's arrays (a Curve only points at them, as it points into the reader's pools)
        std::vector<double> ct, cvv, cr, cl; std::vector<std::uint32_t> cset; std::vector<int> cflags;
        const bool slopes = g() % 2;
        // attribute sets: one for all keys, a few shared ones, or one per key
        const int shape = (int)(g() % 3);
        const int nsets = shape == 0 ? 1 : shape == 1 ? 1 + (int)(g() % 4) : nk;
        for (int k = 0; k < nsets; ++k) {
            cflags.push_back((int)(g() % 3 == 0 ? 0x2 : g() % 2 ? 0x4 : 0x8 | (g() % 2 ? 0x100 : 0) | (g() % 2 ? 0x1000 : 0)));
            if (slopes) { cr.push_back(rnd(false)); cl.push_back(rnd(false)); }
        }
        for (int k = 0; k < nk; ++k) {
            tt += (g() % 4 == 0) ? 0.0 : std::ldexp((double)(g() % 1000), -8);
            const double val = rnd(false);
            const std::uint32_t st = shape == 0 ? 0u : shape == 1 ? (std::uint32_t)(g() % (std::uint64_t)nsets) : (std::uint32_t)k;
            ct.push_back(tt); cvv.push_back(val);
            if (shape != 0) cset.push_back(st);
            oc.t.push_back(tt); oc.v.push_back(val); oc.flags.push_back(cflags[st]);
            if (slopes) { oc.sr.push_back(cr[st]); oc.sl.push_back(cl[st]); }
        }
        cv.t = {ct.data(), ct.size()}; cv.v = {cvv.data(), cvv.size()}; cv.set = {cset.data(), cset.size()};
        cv.setFlags = {cflags.data(), cflags.size()}; cv.setR = {cr.data(), cr.size()}; cv.setL = {cl.data(), cl.size()};
        cv.hasSlopes = oc.hasSlopes = slopes;
        std::size_t h = (std::size_t)-1;
        double time = -2;
        for (int s = 0; s < 300; ++s) {
            time += (g() % 10 == 0) ? -0.3 : std::ldexp((double)(g() % 100), -9);
            const double a0 = oc.Eval(time), a1 = cv.Eval(time), a2 = cv.Eval(time, h);
            ++cn; if (std::memcmp(&a1, &a2, 8) != 0 || std::memcmp(&a0, &a1, 8) != 0) ++cbad;
        }
    }
    CHECK(cn == 600000); CHECK(cbad == 0);

    RUN_SUITE("fbx exact: Base64 four characters at a time against bit by bit");
    long b64bad = 0, b64n = 0;
    {
        const char* alpha = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int it = 0; it < 20000; ++it) {
            std::string in;
            const int len = (int)(g() % 200);
            for (int k = 0; k < len; ++k) {
                const int kind = (int)(g() % 16);   // mostly the alphabet; line ends, padding, junk bytes now and then
                in += kind == 0 ? '\n' : kind == 1 ? '=' : kind == 2 ? (char)(g() % 256) : alpha[g() % 64];
            }
            std::vector<std::uint8_t> got;
            Base64Decode(in, got);
            const std::string want = ref::Base64Old(in);
            ++b64n;
            if (got.size() != want.size() || (!got.empty() && std::memcmp(got.data(), want.data(), got.size()) != 0)) ++b64bad;
        }
    }
    CHECK(b64n == 20000); CHECK(b64bad == 0);

    RUN_SUITE("fbx exact: ASCII numbers, one pass against FastDecimal");
    long nchecks = 0;
    const long nbad = FbxExactNumberChecks(nchecks);
    CHECK(nchecks == 400000); CHECK(nbad == 0);

    RUN_SUITE("fbx exact: ASCII arrays stored as int32 / float32 read back as the parsed doubles");
    long achecks = 0, apacked = 0;
    const long abad = FbxExactArrayChecks(achecks, apacked);
    std::printf("  %ld values in 3000 arrays (%ld stored as int32 or float32): %ld differ\n", achecks, apacked, abad);
    CHECK(abad == 0); CHECK(achecks > 50000); CHECK(apacked > 800);

    std::printf("  base64: %ld of %ld strings differ\n", b64bad, b64n);
    std::printf("  matrices %ld differ, translate %ld, scale %ld, euler %ld, local %ld, simple %ld of %ld; curves %ld of %ld; numbers %ld of %ld\n",
                mul, tr, scl, eul, loc, simple, simpleTried, cbad, cn, nbad, nchecks);
    TEST_MAIN_RESULT();
}
