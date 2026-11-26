// Round 32: an array of aggregates given braced lists, where a member has a constructor
// (Scene/UIGallery.hpp:334, `const Blob blobs[] = {{"A", c, portrait ? Vec2{..} : Vec2{..}, {320, 320}}}`):
// each element is aggregate-initialized ([dcl.init.aggr] p4), not built by a constructor it lacks.
#include <cstdio>
#include <string>
struct Color { float r, g, b, a; static Color FromBytes(int r, int g, int b, int a = 255) { return {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f}; } };
struct Vec2 { float x = 0, y = 0; Vec2() = default; Vec2(float a, float b) : x(a), y(b) {} };
static int live = 0;
struct Tracked { int v; Tracked(int x) : v(x) { ++live; } Tracked(const Tracked& o) : v(o.v) { ++live; } ~Tracked() { --live; } };
struct Named { std::string name; Tracked t; int n = 9; };
int main(int argc, char**) {
    bool portrait = argc > 5;
    struct Blob { const char* name; Color col; Vec2 pos, size; };
    const Blob blobs[] = {
        {"A", Color::FromBytes(255, 70, 150), portrait ? Vec2{20, 160} : Vec2{150, 150}, {320, 320}},
        {"B", Color::FromBytes(60, 190, 255), portrait ? Vec2{150, 520} : Vec2{560, 330}, {360, 360}}};
    for (const Blob& b : blobs) std::printf("%s %.2f %g %g %g %g\n", b.name, b.col.g, b.pos.x, b.pos.y, b.size.x, b.size.y);
    {
        Named ns[2] = {{"first", Tracked(1)}, {std::string("second") + "!", 2, 5}};
        for (const Named& n : ns) std::printf("[%s] %d %d\n", n.name.c_str(), n.t.v, n.n);
        std::printf("live %d\n", live);
    }
    std::printf("live %d\n", live);
    return 0;
}
