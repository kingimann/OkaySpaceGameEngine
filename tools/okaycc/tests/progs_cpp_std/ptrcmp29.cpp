// Comparing a pointer to a class with a pointer to its base ([expr.eq] p3, [expr.rel] p2), also a
// base after the primary one, and with void * (Components/Animator.hpp: cs[i].get() != this);
// inserting one aggregate with default member initializers from a braced list
// (Animation/AnimationCurve.hpp: m_keys.insert(it, {time, value})).
#include <cstdio>
#include <memory>
#include <vector>
struct Keyframe {
    float time = 0.0f;
    float value = 0.0f;
};
struct C {
    virtual ~C() {}
    int id = 0;
};
struct Other {
    virtual ~Other() {}
    long pad = 1;
};
struct Host : C, Other {
    std::vector<std::unique_ptr<C>> cs;
    int Count() {
        int n = 0;
        for (std::size_t i = 0; i < cs.size(); ++i)
            if (cs[i].get() != this) ++n;
        return n;
    }
};
int main() {
    std::vector<Keyframe> keys;
    float t = 1, v = 2;
    keys.push_back({t, v});
    keys.insert(keys.begin(), {0.5f, 9.0f});
    keys.insert(keys.end(), Keyframe{3.0f, 4.0f});
    Host h;
    h.cs.push_back(std::make_unique<C>());
    std::printf("%zu %g %g %g %d\n", keys.size(), keys[0].value, keys[1].time, keys[2].value, h.Count());
    Host *hp = &h;
    C *cp = &h;
    Other *op = &h;
    void *vp = &h;
    Host other;
    std::printf("%d %d %d %d %d\n", hp == cp, hp == op, op != &other, vp == (void *)hp, hp != op);
    return 0;
}
