// Round 20: the implicit copy (and so the move made by push_back(std::move(e))) of a struct with
// a std::string member and a member class that has constructors but no copy constructor. The
// string was copied by its bytes, so the vector's element pointed at freed memory.
#include <string>
#include <utility>
#include <vector>

extern "C" int printf(const char *fmt, ...);

struct V {
    float x = 0, y = 0;
    constexpr V() = default;
    constexpr V(float a, float b) : x(a), y(b) {}
};
enum class K { A, B };
struct Ev {
    K kind = K::A;
    V pos;
    V dir;
    std::string name;
    float a = 0;
};

int main() {
    std::vector<Ev> out;
    for (int i = 0; i < 6; ++i) {
        Ev e;
        e.kind = i % 2 ? K::B : K::A;
        e.pos = V(i * 1.5f, -i * 1.0f);
        e.dir = {0.5f, 0.25f * i};
        e.name = i % 3 == 0 ? "right" : (i % 3 == 1 ? "up" : "");
        e.a = i * 10.0f;
        out.push_back(std::move(e));
    }
    Ev f;
    f.name = "left";
    f.pos = V(3, 4);
    Ev g = f;
    g.name += "!";
    out.push_back(g);
    for (unsigned long i = 0; i < out.size(); ++i)
        printf("%d %.2f %.2f %.2f %.2f [%s] %.1f\n", (int)out[i].kind, out[i].pos.x, out[i].pos.y, out[i].dir.x,
               out[i].dir.y, out[i].name.c_str(), out[i].a);
    printf("[%s] [%s]\n", f.name.c_str(), g.name.c_str());
    return 0;
}
