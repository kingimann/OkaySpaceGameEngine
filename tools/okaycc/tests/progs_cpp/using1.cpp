// Round 19: using X = Y;, using namespace N;, using N::name;
extern "C" int printf(const char *fmt, ...);

namespace geo {
struct Vec {
    int x, y;
};
enum class Side { Left = 3, Right = 7 };
inline int dot(const Vec &a, const Vec &b) { return a.x * b.x + a.y * b.y; }
inline int twice(int v) { return v * 2; }
const int kScale = 5;
namespace deep {
inline int depth() { return 42; }
}  // namespace deep
}  // namespace geo

namespace util {
using Vector = geo::Vec;
using Count = unsigned long;
using geo::twice;
}  // namespace util

using Id = int;
using Callback = int (*)(int);

int apply(Callback f, int v) { return f(v); }

int main() {
    util::Vector v{3, 4};
    util::Count c = 10;
    Id id = 7;
    printf("alias %d %d %d %d\n", v.x, (int)c, id, apply(util::twice, 21));
    {
        using namespace geo;
        Vec w{1, 2};
        printf("ns %d %d %d %d\n", dot(v, w), kScale, (int)Side::Right, deep::depth());
    }
    using geo::deep::depth;
    printf("decl %d %d\n", depth(), util::twice(4));
    return 0;
}
