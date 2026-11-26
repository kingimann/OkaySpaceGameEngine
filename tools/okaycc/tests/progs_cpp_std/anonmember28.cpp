// Round 28: anonymous union members of a class ([class.union.anon] p1: Lua/Lua.hpp:66,
// Render/FbxReader.hpp:92), set in a constructor's initializer list and used by name inside and
// outside the class, and unnamed classes as members with a braced or = initializer.
#include <cstdint>
#include <cstdio>
struct Value {
    int t = 0;
    union {
        bool b;
        std::int64_t i;
        double n;
        void* o;
    };
    Value() : i(0) {}
    static Value Num(double v) {
        Value x;
        x.t = 2;
        x.n = v;
        return x;
    }
    std::int64_t Int() const { return t == 2 ? (std::int64_t)n : i; }
    void SetInt(std::int64_t v) {
        t = 1;
        i = v;
    }
};
struct Prop {
    char type = 'I';
    union {
        std::int64_t i;
        struct {
            float x, y;
        };
    };
    Prop() : i(7) {}
};
struct Holder {
    struct {
        int a;
        float b;
    } s{1, 2.5f};
    struct {
        int c;
    } u = {9};
};
int main() {
    Value v;
    v.SetInt(42);
    Value w = Value::Num(1.5);
    Holder h;
    Prop p;
    std::int64_t before = p.i;
    p.x = 1.0f;
    p.y = 2.0f;
    std::printf("%lld %g %lld %d | %d %g %d %d | %lld %g %g %d\n", (long long)v.Int(), w.n, (long long)w.Int(),
                (int)sizeof(Value), h.s.a, h.s.b, h.u.c, (int)sizeof(Holder), (long long)before, p.x, p.y,
                (int)sizeof(Prop));
    struct {
        unsigned long flags, functions;
        long mode;
    } mwm = {2, 0, 5};
    std::printf("%lu %ld\n", mwm.flags, mwm.mode);
    return 0;
}
