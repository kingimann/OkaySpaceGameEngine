// Long chains of operators (round 27): each link was checked twice, once as a probe and once for
// real, so the time grew twice over with every link (a 24-term sum took 11 seconds, a 25-link <<
// chain in Components/NetworkSync.hpp several minutes). test_cppdiff also checks the compile time.
// The operands include members of call results (f().x, the hidden object of callmember26).
#include <cstdio>
#include <sstream>
#include <string>
struct Gait { float freq; int steps; std::string name; };
static Gait GaitFor(int s) { return Gait{s * 0.5f, s * 3, s > 1 ? "run" : "walk"}; }
struct V {
    int x, y;
    V operator+(const V &o) const { return V{x + o.x, y + o.y}; }
};
static V operator*(int k, const V &v) { return V{k * v.x, k * v.y}; }
struct C { int anim; int seq; float speed; float angle; std::string tag; };
int main() {
    int a = 1, b = 2, c = 3;
    int s = a + b + c + a + b + c + a + b + c + a + b + c + a + b + c + a + b + c + a + b + c + a + b + c + a + b;
    long t = a * 2 + b * 3 - c + a * b * c + (a + b) * (b + c) + a + b + c + a + b + c + a + b + c + a - b - c;
    std::printf("%d %ld\n", s, t);
    C cc{7, 9, 1.5f, -2.25f, "idle"};
    C *p = &cc;
    std::ostringstream o;
    o << p->anim << ' ' << p->seq << ' ' << p->speed << ' ' << p->angle << ' ' << p->tag << ' ' << p->anim
      << ' ' << GaitFor(2).freq << ' ' << GaitFor(3).steps << ' ' << GaitFor(1).name << ' ' << p->seq << ' ' << a
      << ' ' << b << ' ' << c << ' ' << s << ' ' << t << ' ' << "end";
    std::printf("%s\n", o.str().c_str());
    V v{1, 2};
    V w = v + v + 2 * v + v + 3 * v + v + v + v + 2 * v + v + v + v + v + v + v + v + v + v + v + v;
    std::printf("%d %d\n", w.x, w.y);
    std::string x = "a";
    std::string y = x + "b" + x + "c" + x + GaitFor(2).name + "d" + x + "e" + x + "f" + GaitFor(1).name + x + "g" + x + "h";
    std::printf("%s\n", y.c_str());
    float f = GaitFor(2).freq + GaitFor(3).freq - GaitFor(4).freq * 2 + GaitFor(5).freq + GaitFor(6).freq +
              GaitFor(7).freq + GaitFor(8).freq - GaitFor(9).freq + GaitFor(2).freq + GaitFor(3).freq;
    std::printf("%g\n", f);
    return 0;
}
