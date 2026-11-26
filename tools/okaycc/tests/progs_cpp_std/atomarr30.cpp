// An array member of std::atomic<T *> with '= {}' (Core/Profiler.hpp: m_live[kLiveDepth] = {}): each
// element is set through operator=(T *) from a null pointer ([expr.ass] p9).
#include <atomic>
#include <cstdio>
struct P {
    static constexpr int kN = 4;
    std::atomic<const char*> live[kN] = {};
    std::atomic<int> depth{0};
    int Live(const char** out) const {
        int n = depth.load(std::memory_order_acquire);
        for (int i = 0; i < n; ++i) out[i] = live[i].load(std::memory_order_relaxed);
        return n;
    }
    void Push(const char* s) { int d = depth.load(); live[d].store(s, std::memory_order_relaxed); depth.store(d + 1); }
};
int main() {
    P p;
    p.Push("a");
    p.Push("bc");
    const char* o[4];
    int n = p.Live(o);
    std::printf("%d %s %s %d\n", n, o[0], o[1], p.live[3].load() == nullptr);
    return 0;
}
