// Round 24: <mutex> in a program of one thread: lock_guard and unique_lock around a counter,
// try_lock on a held and a free mutex, a deferred lock, call_once, a mutex in a function static.
#include <cstdio>
#include <mutex>
static std::mutex &cacheMutex() {
    static std::mutex m;
    return m;
}
static int counter = 0;
static void bump() {
    std::lock_guard<std::mutex> lock(cacheMutex());
    ++counter;
}
int main() {
    bump();
    bump();
    std::mutex m;
    bool first = m.try_lock();
    bool second = m.try_lock();
    m.unlock();
    std::unique_lock<std::mutex> ul(m, std::defer_lock);
    bool before = ul.owns_lock();
    ul.lock();
    bool after = ul.owns_lock();
    ul.unlock();
    std::once_flag once;
    int ran = 0;
    for (int i = 0; i < 3; ++i) std::call_once(once, [&]() { ++ran; });
    printf("%d %d %d %d %d %d\n", counter, first ? 1 : 0, second ? 1 : 0, before ? 1 : 0, after ? 1 : 0, ran);
}
