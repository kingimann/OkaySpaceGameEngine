// Round 21: our own <atomic> (LOCK XADD, XCHG, LOCK CMPXCHG emitted in place) against libstdc++,
// one thread: the values each operation gives back.
#include <atomic>
#include <cstdint>
#include <cstdio>

enum class Mode : int { Off, On, Auto };

struct Node {
    int v;
};

int main() {
    std::atomic<int> a{5};
    int old = a.fetch_add(3);
    int pre = ++a;
    int post = a++;
    a -= 2;
    std::printf("int %d %d %d %d\n", old, pre, post, a.load());
    int expected = 8;
    bool ok = a.compare_exchange_strong(expected, 100);
    bool bad = a.compare_exchange_strong(expected, 7);
    std::printf("cas %d %d %d %d\n", (int)ok, (int)bad, expected, (int)a);
    int swapped = a.exchange(-4);
    std::printf("xchg %d %d\n", swapped, a.load(std::memory_order_relaxed));

    std::atomic<std::uint64_t> big{0xFFFFFFFFULL};
    big.fetch_add(1);
    big |= 0x10;
    big &= ~0x1ULL;
    big.fetch_xor(0xF0);
    std::printf("u64 %llx\n", (unsigned long long)big.load());

    std::atomic<std::uint32_t> u{0xFFFFFFFFu};
    std::uint32_t was = u.fetch_add(2);
    std::uint32_t e2 = 1;
    bool c2 = u.compare_exchange_weak(e2, 0x80000000u);
    std::printf("u32 %x %x %d %x\n", was, u.load(), (int)c2, u.load());

    std::atomic<bool> flag{false};
    bool f0 = flag.exchange(true);
    std::printf("bool %d %d\n", (int)f0, (int)flag.load());
    std::atomic<std::uint8_t> small{250};
    small += 10;
    std::atomic<std::int16_t> mid{-3};
    mid.fetch_sub(5);
    std::printf("small %d mid %d\n", (int)small.load(), (int)mid.load());

    std::atomic<Mode> mode{Mode::Off};
    mode.store(Mode::Auto);
    Mode want = Mode::Auto;
    mode.compare_exchange_strong(want, Mode::On);
    std::printf("mode %d\n", (int)mode.load());

    Node nodes[4] = {{1}, {2}, {3}, {4}};
    std::atomic<Node *> p{nodes};
    Node *q = p.fetch_add(2);
    ++p;
    std::printf("ptr %d %d\n", q->v, p.load()->v);

    std::atomic_flag lock;
    bool t1 = lock.test_and_set();
    bool t2 = lock.test_and_set();
    lock.clear();
    bool t3 = lock.test_and_set();
    std::atomic_thread_fence(std::memory_order_seq_cst);
    std::printf("flag %d %d %d\n", (int)t1, (int)t2, (int)t3);
    std::atomic<float> fl{1.5f};
    float fo = fl.exchange(2.25f);
    std::printf("float %.2f %.2f\n", fo, fl.load());
    return 0;
}
