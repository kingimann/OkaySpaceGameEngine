// Round 30 first errors: an unnamed parameter of a class type (Core/EventBus.hpp:114); a pointer to a
// data member of a template parameter, bool Owner::*m (Scene/PropertyBuilder.hpp:55); a nested
// conditional as an lvalue (Physics/ShapeKernel3D.hpp:196); a global and a static inline member of a
// class without constructors copied from another object (Audio/AudioMixer.hpp:71).
#include <cstdio>
#include <functional>
struct Vec3 {
    float x, y, z;
    static const Vec3 Zero;
    static const Vec3 One;
};
const Vec3 Vec3::Zero{0, 0, 0};
const Vec3 Vec3::One{1, 1, 1};
struct Mixer {
    static inline Vec3 listener = Vec3::One;
    static inline float gain = 1.0f;
};
Vec3 gStart = Vec3::One;
struct Bus {
    struct Scoped {};
    template <typename E>
    using Handler = std::function<void(const E&)>;
    template <typename E>
    int Subscribe(Handler<E> h, int priority = 0) {
        h(E());
        return priority;
    }
    template <typename E>
    int Subscribe(Handler<E> h, Scoped, int priority = 0) {
        h(E());
        return priority + 100;
    }
};
struct Comp {
    bool on = false;
    bool off = true;
};
struct PB {
    template <class Owner>
    int AddBool(Owner& o, bool Owner::*m) {
        o.*m = !(o.*m);
        return o.*m ? 1 : 0;
    }
};
int main() {
    Vec3 n{1, 2, 3};
    for (int i = 0; i < 3; ++i) (i == 0 ? n.x : i == 1 ? n.y : n.z) = 9.0f + (float)i;
    Bus b;
    int calls = 0;
    int s = b.Subscribe<int>([&calls](const int&) { ++calls; }, Bus::Scoped{}, 2);
    int t = b.Subscribe<int>([&calls](const int&) { calls += 10; }, 3);
    PB pb;
    Comp c;
    int a1 = pb.AddBool(c, &Comp::on);
    int a2 = pb.AddBool(c, &Comp::off);
    std::printf("%g %g %g %d %d %d\n", n.x, n.y, n.z, s, t, calls);
    std::printf("%d %d %d %d\n", a1, a2, c.on, c.off);
    std::printf("%g %g %g\n", Mixer::listener.y, gStart.z, Mixer::gain);
    return 0;
}
