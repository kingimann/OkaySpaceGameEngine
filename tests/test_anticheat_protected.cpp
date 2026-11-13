#include "test_framework.hpp"
#define OKAY_PROTECTED_TESTING
#include "AntiCheatProtected.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <set>
#include <thread>
#include <vector>

// okay::Protected<T>: round trips, tamper detection (decoy, canary), key rotation,
// thread safety and a rough cost. It defeats naive memory editors only.
namespace {
using okay::Protected;
using okay::ProtectedTamper;

std::atomic<int> g_decoy{0}, g_canary{0};
void OnTamper(ProtectedTamper t, void*) { (t == ProtectedTamper::Decoy ? g_decoy : g_canary)++; }

struct Vec3 { float x, y, z; };
struct Big { std::int64_t a[8]; };

void TestRoundTrip() {
    RUN_SUITE("protected: round trip");
    Protected<int> hp = 100;
    CHECK(hp.Get() == 100);
    hp = 37; CHECK((int)hp == 37);
    hp += 5; CHECK(hp.Get() == 42);
    hp -= 50; CHECK(hp.Get() == -8);
    Protected<float> f(1.5f); CHECK(f.Get() == 1.5f);
    Protected<bool> b(true); CHECK(b.Get()); b = false; CHECK(!b.Get());
    Protected<std::uint8_t> u8(250); CHECK(u8.Get() == 250);
    Protected<std::int64_t> big(-9000000000000ll); CHECK(big.Get() == -9000000000000ll);
    Protected<double> d(3.14159265358979); CHECK(d.Get() == 3.14159265358979);
    Protected<Vec3> v(Vec3{1, 2, 3}); CHECK(v.Get().y == 2 && v.Get().z == 3);
    Big bg{}; for (int i = 0; i < 8; ++i) bg.a[i] = i * 1000003ll;
    Protected<Big> pb(bg); CHECK(pb.Get().a[7] == bg.a[7]);
    CHECK(pb.Get().a[3] == 3000009);
    Protected<int> copy = hp; CHECK(copy.Get() == -8);
    copy = Protected<int>(11); CHECK(copy.Get() == 11);
    hp.Modify([](int& x) { x = x * 2; }); CHECK(hp.Get() == -16);
    CHECK(hp.Verify() && hp.TamperCount() == 0);
    for (int i = -1000; i < 1000; ++i) { hp = i; CHECK(hp.Get() == i); }
}

void TestNotPlainInMemory() {
    RUN_SUITE("protected: the value isn't stored as itself (apart from the decoy), and the key rotates");
    Protected<std::uint32_t> p(0xDEADBEEFu);
    std::uint64_t w = *p.TestWords();
    CHECK((std::uint32_t)w != 0xDEADBEEFu);
    std::set<std::uint64_t> seenWords, seenKeys;
    for (int i = 0; i < 200; ++i) { p = 0xDEADBEEFu; seenWords.insert(*p.TestWords()); seenKeys.insert(*p.TestKey()); }
    CHECK(seenWords.size() > 190);   // same value, different bytes every write
    CHECK(seenKeys.size() > 190);
    CHECK(p.Get() == 0xDEADBEEFu);
}

void TestTamper() {
    RUN_SUITE("protected: tampering is detected");
    okay::SetProtectedTamperHandler(OnTamper);
    g_decoy = g_canary = 0;
    {   // a memory editor finds the plain value (the decoy), edits it: the real value survives
        Protected<int> hp = 100;
        CHECK(hp.TestGetDecoy() == 100);
        hp.TestSetDecoy(999);
        CHECK(hp.Get() == 100);                       // not fooled
        CHECK(hp.TamperCount() == 1 && hp.LastTamper() == ProtectedTamper::Decoy);
        CHECK(g_decoy == 1);
        CHECK(hp.Get() == 100 && hp.TamperCount() == 1);   // repaired: one edit, one event
        CHECK(hp.Verify());
    }
    {   // an editor that freezes the encrypted words to an old value
        Protected<int> hp = 100;
        const std::uint64_t oldWord = *hp.TestWords();
        hp = 50;
        *hp.TestWords() = oldWord;                    // "freeze" at the old bytes
        CHECK(hp.Get() == 0);                         // canary fails: T{}
        CHECK(hp.LastTamper() == ProtectedTamper::Canary);
        CHECK(g_canary >= 1);
        hp = 77; CHECK(hp.Get() == 77 && hp.Verify()); // a legitimate write heals it
    }
    {   // editing the stored words directly
        Protected<float> f(2.0f);
        *f.TestWords() ^= 0x1234;
        CHECK(!f.Verify());
        CHECK(f.LastTamper() == ProtectedTamper::Canary);
    }
    {   // editing the key
        Protected<int> x = 5;
        *x.TestKey() += 1;
        CHECK(!x.Verify());
    }
    {   // editing the canary
        Protected<int> x = 5;
        *x.TestCanary() ^= 1;
        CHECK(!x.Verify());
    }
    {   // a snapshot of the whole object put back (key + words + canary + decoy) is NOT detectable: documented limit
        Protected<int> x = 100;
        std::uint64_t w = *x.TestWords(), k = *x.TestKey(), c = *x.TestCanary();
        x = 1;
        *x.TestWords() = w; *x.TestKey() = k; *x.TestCanary() = c; x.TestSetDecoy(100);
        CHECK(x.Get() == 100);
    }
    CHECK(okay::ProtectedTamperTotal() >= 4);
    okay::SetProtectedTamperHandler(nullptr);
}

void TestThreads() {
    RUN_SUITE("protected: thread safety");
    Protected<std::int64_t> counter = 0;
    Protected<Vec3> pair(Vec3{0, 0, 0});   // x == y must always hold
    std::atomic<bool> torn{false};
    const int kThreads = 4, kIters = 20000;
    std::vector<std::thread> th;
    for (int t = 0; t < kThreads; ++t)
        th.emplace_back([&] {
            for (int i = 0; i < kIters; ++i) {
                counter += 1;
                pair.Modify([](Vec3& v) { v.x += 1; v.y += 1; v.z += 1; });
                const Vec3 s = pair.Get();
                if (s.x != s.y || s.y != s.z) torn = true;
            }
        });
    for (auto& x : th) x.join();
    CHECK(counter.Get() == (std::int64_t)kThreads * kIters);
    CHECK(pair.Get().x == (float)(kThreads * kIters));
    CHECK(!torn);
    CHECK(counter.TamperCount() == 0 && pair.TamperCount() == 0);
}

template <class F> double NsPerOp(F&& f, int n) {
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < n; ++i) f(i);
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::nano>(t1 - t0).count() / n;
}

void TestPerformance() {
    RUN_SUITE("protected: cost (informational numbers)");
    const int N = 2000000;
    volatile int plain = 0;
    Protected<int> p = 0;
    unsigned sink = 0;
    const double plainSet = NsPerOp([&](int i) { plain = i; }, N);
    const double plainGet = NsPerOp([&](int) { sink += plain; }, N);
    const double protSet = NsPerOp([&](int i) { p = i; }, N);
    const double protGet = NsPerOp([&](int) { sink += p.Get(); }, N);
    std::printf("  plain int  set %.2f ns  get %.2f ns\n", plainSet, plainGet);
    std::printf("  Protected  set %.2f ns  get %.2f ns   (sink %u)\n", protSet, protGet, sink);
    CHECK(protSet < 2000 && protGet < 2000);   // generous: catches an accidental O(n), not a slow CI box
}
} // namespace

int main() {
    TestRoundTrip();
    TestNotPlainInMemory();
    TestTamper();
    TestThreads();
    TestPerformance();
    TEST_MAIN_RESULT();
}
