// A parameter, local or member named like a type or class template is an ordinary name
// ([basic.scope.block], [basic.scope.class]): float seconds beside std::chrono::seconds, a member
// duration beside std::chrono::duration, compared with '<'.
#include <chrono>
#include <cstdio>
struct Tw {
    float delay = 0;
    float duration = 0;
    Tw &SetDelay(float seconds) { delay = seconds > 0 ? (seconds < kMaxSeconds ? seconds : kMaxSeconds) : 0; return *this; }
    float Duration() const { return duration > 1e-4f ? (duration < kMaxSeconds ? duration : kMaxSeconds) : 1e-4f; }
    static constexpr float kMaxSeconds = 100.0f;
};
int main() {
    Tw t;
    t.SetDelay(3);
    t.duration = 500;
    float seconds = 2, milliseconds = 4;
    float r = seconds * milliseconds;
    std::chrono::seconds s(5);
    std::printf("%g %g %g %ld\n", t.delay, t.Duration(), r, (long)s.count());
}
