#include "test_framework.hpp"
#include <Okay.hpp>
#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>

using namespace okay;

int main() {
    RUN_SUITE("random");

    // --- the raw-to-float conversion never reaches 1 ---
    {
        CHECK(Random::ToUnitFloat(~0ull) < 1.0f);
        CHECK(Random::ToUnitFloat((1ull << 53) - 1) < 1.0f);
        CHECK(Random::ToUnitFloat(0) == 0.0f);
        // Every raw value with the top 24 bits all set, including the old rounding-up case.
        CHECK(Random::ToUnitFloat(0xFFFFFFFFFF000000ull) == 1.0f - 1.0f / 16777216.0f);
        CHECK(Random::ToUnitFloat(0xFFFFFFFFFFFFFFFFull >> 11 << 11) < 1.0f);
    }

    // --- 10 million Values in [0, 1); Range(float) never returns max ---
    {
        Random r(12345);
        bool inRange = true;
        for (int i = 0; i < 10000000; ++i) {
            float v = r.Value();
            if (!(v >= 0.0f && v < 1.0f)) { inRange = false; break; }
        }
        CHECK(inRange);

        bool never = true;
        for (int i = 0; i < 200000; ++i) {
            float v = r.Range(0.0f, 1.0f);
            float w = r.Range(1000000.0f, 1000000.5f);   // a narrow range far from zero
            if (!(v < 1.0f) || !(w < 1000000.5f) || w < 1000000.0f) { never = false; break; }
        }
        CHECK(never);
    }

    // --- Range(int) at the extremes ---
    {
        Random r(7);
        bool ok = true;
        for (int i = 0; i < 100000; ++i) {
            int a = r.Range(INT_MIN, INT_MAX);
            if (a < INT_MIN || a >= INT_MAX) ok = false;
            int b = r.Range(-5, 5);
            if (b < -5 || b >= 5) ok = false;
            int c = r.Range(INT_MAX - 3, INT_MAX);
            if (c < INT_MAX - 3 || c >= INT_MAX) ok = false;
            int d = r.Range(INT_MIN, INT_MIN + 3);
            if (d < INT_MIN || d >= INT_MIN + 3) ok = false;
        }
        CHECK(ok);
        CHECK(r.Range(5, 5) == 5);
        CHECK(r.Range(9, 2) == 9);
        // Wide ranges use both halves (the old signed subtraction overflowed).
        bool neg = false, pos = false;
        for (int i = 0; i < 100; ++i) { int a = r.Range(INT_MIN, INT_MAX); (a < 0 ? neg : pos) = true; }
        CHECK(neg && pos);
    }

    // --- distribution buckets ---
    {
        Random r(99);
        int fb[10] = {}, ib[7] = {};
        const int n = 700000;
        for (int i = 0; i < n; ++i) { ++fb[(int)(r.Value() * 10.0f)]; ++ib[r.Range(0, 7)]; }
        bool even = true;
        for (int b : fb) if (std::abs(b - n / 10) > n / 10 * 0.03) even = false;
        for (int b : ib) if (std::abs(b - n / 7) > n / 7 * 0.03) even = false;
        CHECK(even);
    }

    // --- Gaussian mean and spread ---
    {
        Random r(5);
        double sum = 0, sq = 0; const int n = 200000;
        for (int i = 0; i < n; ++i) { double g = r.Gaussian(2.0f, 3.0f); sum += g; sq += g * g; }
        double mean = sum / n, var = sq / n - mean * mean;
        CHECK_NEAR(mean, 2.0, 0.05);
        CHECK_NEAR(std::sqrt(var), 3.0, 0.05);
    }

    // --- state save / restore, Seed, Randomize ---
    {
        Random a(42);
        for (int i = 0; i < 17; ++i) a.NextUInt64();
        Random::State st = a.GetState();
        std::uint64_t first[8];
        for (auto& v : first) v = a.NextUInt64();
        Random b(1);
        b.SetState(st);
        bool same = true;
        for (auto v : first) if (b.NextUInt64() != v) same = false;
        CHECK(same);

        Random c(42), d(42);
        CHECK(c.NextUInt64() == d.NextUInt64());
        c.Seed(43);
        CHECK(c.NextUInt64() != d.NextUInt64());

        Random z; z.SetState({0, 0});               // all-zero state is repaired
        CHECK(z.NextUInt64() != 0 || z.NextUInt64() != 0);

        Random e, f; e.Randomize(); f.Randomize();
        CHECK(e.NextUInt64() != f.NextUInt64());     // different time/address entropy
    }

    // --- Shuffle is a permutation ---
    {
        Random r(3);
        std::vector<int> v(100); std::iota(v.begin(), v.end(), 0);
        r.Shuffle(v);
        std::vector<int> sorted = v; std::sort(sorted.begin(), sorted.end());
        bool perm = true;
        for (int i = 0; i < 100; ++i) if (sorted[i] != i) perm = false;
        CHECK(perm);
        CHECK(v != sorted);                          // and it did move things
        std::vector<int> none, one{7};
        r.Shuffle(none); r.Shuffle(one);
        CHECK(none.empty() && one.size() == 1 && one[0] == 7);
    }

    // --- Weighted ---
    {
        Random r(11);
        std::vector<float> w{0.0f, 1.0f, 0.0f, 3.0f, 0.0f};
        int hits[5] = {};
        for (int i = 0; i < 100000; ++i) { int k = r.Weighted(w); CHECK(k >= 0 && k < 5); ++hits[k]; }
        CHECK(hits[0] == 0 && hits[2] == 0 && hits[4] == 0);
        CHECK_NEAR((double)hits[3] / hits[1], 3.0, 0.2);
        CHECK(r.Weighted({}) == -1);
        CHECK(r.Weighted({0.0f, 0.0f}) == 0);
        CHECK(r.Weighted({-1.0f, 2.0f}) == 1);
    }

    // --- the script functions choose() and shuffle() use the exclusive upper bound correctly ---
    // choose() used to ask for 0..size-2, so it never returned the last element; shuffle() swapped
    // with 0..i-1 instead of 0..i, which only ever produces cyclic orders (never the identity).
    {
        int picked[3] = {0, 0, 0};
        std::map<int, int> orders;
        for (int i = 0; i < 3000; ++i) {
            auto vm = CreateScriptVM("okayscript");
            CHECK(vm->Load("var opts = [1, 2, 3];\nvar pick = choose(opts);\nshuffle(opts);\n"
                           "var order = opts[0] * 100 + opts[1] * 10 + opts[2];", nullptr));
            int pick = (int)vm->GetGlobal("pick").AsFloat();
            if (pick >= 1 && pick <= 3) ++picked[pick - 1];
            ++orders[(int)vm->GetGlobal("order").AsFloat()];
        }
        CHECK(picked[0] > 700 && picked[1] > 700 && picked[2] > 700);   // each about 1000 of 3000
        CHECK(orders.size() == 6);                                       // all six orders appear
        CHECK(orders[123] > 250 && orders[123] < 750);                   // including the identity
    }

    // --- Range(float) over a span wider than a float (FLT_MAX - -FLT_MAX overflowed to infinity and always returned max) ---
    {
        Random r(77);
        float lo = 0.0f, hi = 0.0f;
        bool inRange = true;
        for (int i = 0; i < 2000; ++i) {
            const float v = r.Range(-3.4e38f, 3.4e38f);
            if (!(v >= -3.4e38f && v < 3.4e38f) || !std::isfinite(v)) inRange = false;
            lo = std::min(lo, v); hi = std::max(hi, v);
        }
        CHECK(inRange);
        CHECK(lo < -1.0e38f && hi > 1.0e38f);                 // spread over the span, not stuck at one end
        // a NaN or infinite limit gives a finite answer instead of poisoning the caller
        const float nan = std::numeric_limits<float>::quiet_NaN();
        CHECK(std::isfinite(r.Range(nan, 1.0f)) && std::isfinite(r.Range(0.0f, nan)));
        CHECK(std::isfinite(r.Range(0.0f, std::numeric_limits<float>::infinity())));
        // ordinary ranges still behave (and keep their old values: the common path is untouched)
        for (int i = 0; i < 1000; ++i) { const float v = r.Range(2.0f, 3.0f); CHECK(v >= 2.0f && v < 3.0f); }
        CHECK(r.Range(5.0f, 5.0f) == 5.0f);
    }

    TEST_MAIN_RESULT();
}
