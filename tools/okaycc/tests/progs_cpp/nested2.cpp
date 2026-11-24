// A member function of a nested class defined outside both classes (A::Ring::Resize), inside a
// namespace, and an explicit specialization of a class template (template <> struct L<float>).
extern "C" int printf(const char *fmt, ...);
namespace okay {
namespace audio {
class Room {
public:
    struct Ring {
        int buf[8];
        int n = 0;
        void Resize(int k);
        int Sum() const;
    };
    Ring a, b;
    int Total() const;
};
void Room::Ring::Resize(int k) {
    if (k > 8) k = 8;
    n = k;
    for (int i = 0; i < n; ++i) buf[i] = i * k;
}
int Room::Ring::Sum() const {
    int s = 0;
    for (int i = 0; i < n; ++i) s += buf[i];
    return s;
}
int Room::Total() const { return a.Sum() + b.Sum(); }
}  // namespace audio
}  // namespace okay

template <typename T>
struct L {
    static int bits() { return (int)sizeof(T) * 8; }
    static const char *name() { return "generic"; }
};
template <>
struct L<float> {
    static int bits() { return 24; }
    static const char *name() { return "float"; }
};
int main() {
    okay::audio::Room r;
    r.a.Resize(4);
    r.b.Resize(20);
    printf("total %d %d %d\n", r.a.Sum(), r.b.Sum(), r.Total());
    printf("limits %d %s %d %s\n", L<int>::bits(), L<int>::name(), L<float>::bits(), L<float>::name());
    return 0;
}
