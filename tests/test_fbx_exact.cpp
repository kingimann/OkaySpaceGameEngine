// The output-preserving fast paths of our FBX reader (okay::fbx), checked bit for bit against the plain code they
// replaced, on random inputs that include -0, infinities, NaN and huge values (docs/own_fbx_reader.md, "Speed and
// memory"). White-box: this file compiles FbxScene.cpp itself (and test_fbx_exact_numbers.cpp compiles FbxReader.cpp),
// so the internal functions can be called. Standalone: no engine library.
//   - matrix products that drop the terms that are exactly zero (Mul, MulTranslate, MulScale, the one-axis rotations
//     of Euler, LocalMatrix and its short path LocalSimple) against the plain 4x4 products;
//   - curve evaluation that remembers its last key segment against the plain search;
//   - the ASCII number fast path against FastDecimal (test_fbx_exact_numbers.cpp).
#include "test_framework.hpp"
#include "../engine/src/Render/FbxScene.cpp"

#include <cstdio>
#include <random>

long FbxExactNumberChecks(long& checks);   // test_fbx_exact_numbers.cpp

using namespace okay::fbx;

namespace ref {   // the plain versions, as they were before the fast paths
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

    RUN_SUITE("fbx exact: curve evaluation with a remembered segment");
    long cbad = 0, cn = 0;
    for (int c = 0; c < 2000; ++c) {
        Curve cv; const int nk = 1 + (int)(g() % 40); double tt = -1;
        const bool slopes = g() % 2;
        for (int k = 0; k < nk; ++k) {
            tt += (g() % 4 == 0) ? 0.0 : std::ldexp((double)(g() % 1000), -8);
            cv.t.push_back(tt); cv.v.push_back(rnd(false));
            cv.flags.push_back((int)(g() % 3 == 0 ? 0x2 : g() % 2 ? 0x4 : 0x8 | (g() % 2 ? 0x100 : 0) | (g() % 2 ? 0x1000 : 0)));
            if (slopes) { cv.sr.push_back(rnd(false)); cv.sl.push_back(rnd(false)); }
        }
        cv.hasSlopes = slopes;
        std::size_t h = (std::size_t)-1;
        double time = -2;
        for (int s = 0; s < 300; ++s) {
            time += (g() % 10 == 0) ? -0.3 : std::ldexp((double)(g() % 100), -9);
            const double a1 = cv.Eval(time), a2 = cv.Eval(time, h);
            ++cn; if (std::memcmp(&a1, &a2, 8) != 0) ++cbad;
        }
    }
    CHECK(cn == 600000); CHECK(cbad == 0);

    RUN_SUITE("fbx exact: ASCII numbers, one pass against FastDecimal");
    long nchecks = 0;
    const long nbad = FbxExactNumberChecks(nchecks);
    CHECK(nchecks == 400000); CHECK(nbad == 0);

    std::printf("  matrices %ld differ, translate %ld, scale %ld, euler %ld, local %ld, simple %ld of %ld; curves %ld of %ld; numbers %ld of %ld\n",
                mul, tr, scl, eul, loc, simple, simpleTried, cbad, cn, nbad, nchecks);
    TEST_MAIN_RESULT();
}
