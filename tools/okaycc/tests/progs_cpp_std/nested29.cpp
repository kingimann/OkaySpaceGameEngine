// A class nested in another is found by its short name only inside that class, its derived
// classes and its member functions ([class.nest] p1, [class.mem] p7, [class.member.lookup]):
// UISkeleton::Bar no longer makes gx::Bar(..) a cast (Scene/UIGallery.hpp).
#include <cstdio>
#include <map>
#include <string>
struct Vec2 {
    float x, y;
};
struct Skel {
    struct Bar {
        float x, y, w, h;
    };
    Bar b{1, 2, 3, 4};
    float W() const {
        Bar c = b;
        return c.w;
    }
    float H() const;
    template <class T>
    int Count(T k) {
        Entry &e = table[(int)k];  // Entry is declared below
        return ++e.hits;
    }
    struct Entry {
        int hits = 0;
    };
    std::map<int, Entry> table;
};
float Skel::H() const {
    Bar c = b;
    return c.h;
}
struct Derived : Skel {
    Bar more{5, 6, 7, 8};
    float Sum() const { return more.x + b.x; }
};
namespace app {
struct Paths {
    struct Env {
        std::string home;
        static Env Current();
    };
    static int Count() { return 2; }
};
// a member of a nested class defined outside it: Env is the class itself, Paths's members are seen
Paths::Env Paths::Env::Current() {
    Env e;
    e.home = std::string("/home/") + std::to_string(Count());
    return e;
}
}  // namespace app
namespace gx {
inline int Bar(const char *n, Vec2 pos, Vec2 size, int role = 0) {
    return (int)(pos.x + pos.y + size.x * 10 + size.y) + role + (int)std::string(n).size();
}
}  // namespace gx
static void Bind(int v) { std::printf("%d\n", v); }
int main() {
    const float kPad = 1, kTop = 2;
    Bind(gx::Bar("h", {kPad, kTop}, {3.0f - kPad, 4}, 5));
    Skel s;
    Skel::Bar q{5, 6, 7, 8};
    s.Count(3);
    std::printf("%g %g %g %d %g\n", s.W(), s.H(), q.x, s.Count(3L), Derived().Sum());
    std::printf("%s\n", app::Paths::Env::Current().home.c_str());
    return 0;
}
