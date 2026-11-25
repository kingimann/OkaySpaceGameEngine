// Tables of string literals and addresses as constant data ([basic.start.static], ISO C 6.6 p9).
#include <cstdio>
static int g = 7;
static int twice(int x) { return 2 * x; }
const char *names[] = {"zero", "one", "two"};
int *ptrs[] = {&g, nullptr, &g};
struct Entry { const char *name; int (*fn)(int); int v; };
static const Entry table[] = {{"twice", twice, 1}, {"none", nullptr, 2}};
inline const char *Name(int e) {
    static const char *k[] = {"Linear", "In Quad", "Out Quad"};
    return k[e];
}
struct Css { const char *name; float x1, y1; double d; short s; };
static void css(const char *k) {
    static const Css t[] = {{"ease", 0.25f, 0.1f, 2.5, 3}, {"ease-in", 0.42f, 0.0f, -1.0, 4}};
    for (const Css &c : t)
        if (c.name[4] == k[4]) std::printf("%s %g %g %g %d\n", c.name, c.x1, c.y1, c.d, c.s);
}
const char *Name2(int e) {
    static const char *const k[] = {"A", "B"};
    return k[e];
}
int main() {
    std::printf("%s %s %s %s\n", names[2], Name(1), Name(2), Name2(1));
    std::printf("%d %d %d\n", *ptrs[0], ptrs[1] == nullptr, *ptrs[2]);
    css("ease");
    css("ease-in");
    std::printf("%s %d %s %d\n", table[0].name, table[0].fn(21), table[1].name, table[1].fn == nullptr);
}
