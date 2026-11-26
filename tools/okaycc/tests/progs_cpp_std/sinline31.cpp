// A static inline data member of a class with constructors copied from another object
// (static inline Vec3 listener = Vec3::Zero, Audio/AudioMixer.hpp), also inside a namespace.
#include <cstdio>
struct V {
    float x, y, z;
    V() : x(0), y(0), z(0) {}
    V(float a, float b, float c) : x(a), y(b), z(c) {}
    static const V Zero;
    static const V One;
};
inline const V V::Zero = V(0, 0, 0);
inline const V V::One{1, 1, 1};
struct Mixer {
    static inline V listener = V::One;
    static inline V right = V(2, 3, 4);
    static inline float gain = 1.0f;
    static void Set(const V& p) { listener = p; }
};
V g = V::One;
namespace eng {
struct W {
    float x, y, z;
    W() : x(0), y(0), z(0) {}
    W(float a, float b, float c) : x(a), y(b), z(c) {}
    static const W Zero;
    static const W One;
};
inline const W W::One{1, 2, 3};
struct Mixer {
    static inline W listener = W::One;
    static inline W right = W(2, 3, 4);
    static inline float gain = 1.0f;
};
}  // namespace eng
static void inNamespace() {
    std::printf("%.1f %.1f %.1f\n", eng::Mixer::listener.y, eng::Mixer::right.z, eng::Mixer::gain);
}
int main() {
    inNamespace();
    std::printf("%.1f %.1f %.1f\n", Mixer::listener.x, Mixer::right.y, g.z);
    Mixer::Set(V(5, 6, 7));
    std::printf("%.1f %.1f\n", Mixer::listener.y, Mixer::gain);
    return 0;
}
