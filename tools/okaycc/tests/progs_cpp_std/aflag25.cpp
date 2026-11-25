// ATOMIC_FLAG_INIT ([atomics.flag] p4) in a member initializer, a static local and a global.
#include <atomic>
#include <cstdio>
struct Spin {
    std::atomic_flag m_flag = ATOMIC_FLAG_INIT;
    void lock() { while (m_flag.test_and_set(std::memory_order_acquire)) {} }
    void unlock() { m_flag.clear(std::memory_order_release); }
};
inline std::atomic_flag &LockFlag() { static std::atomic_flag f = ATOMIC_FLAG_INIT; return f; }
std::atomic_flag g = ATOMIC_FLAG_INIT;
int main() {
    Spin s;
    s.lock();
    bool was = s.m_flag.test_and_set();
    s.unlock();
    bool after = s.m_flag.test_and_set();
    bool a = LockFlag().test_and_set();
    bool b = LockFlag().test_and_set();
    bool c = g.test_and_set();
    std::printf("%d %d %d %d %d\n", was, after, a, b, c);
}
