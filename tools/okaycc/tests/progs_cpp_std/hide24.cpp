// Round 24: a member or variable named like a class template (duration beside
// std::chrono::duration) is an ordinary name where no template arguments follow.
#include <chrono>
#include <cstdio>
struct Emitter {
    float duration = 1.5f;
    bool loop = false;
    void Reset() {
        duration = 0.0f;
        duration += 2.0f;
        loop = true;
    }
};
int main() {
    Emitter e;
    e.Reset();
    std::chrono::duration<long> d(3);
    float pair = e.duration;
    printf("%.1f %d %ld %.1f\n", e.duration, e.loop ? 1 : 0, (long)d.count(), pair);
}
