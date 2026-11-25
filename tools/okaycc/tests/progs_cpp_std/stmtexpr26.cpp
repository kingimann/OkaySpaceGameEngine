// A statement that starts with a type but is an expression ([stmt.ambig]): T().f(..), T{..}.f(..),
// T(x).f(..) (std::vector<std::uint32_t>().swap(keys) in Render/WeldMap.hpp); an alias of an
// enumeration naming its enumerators (using C = CombineMode; C::Minimum); a nested class declared in
// its class and defined after it inside a namespace (class P::It { .. }).
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
namespace a {
namespace b {
class P {
public:
    int v = 3;
    class It;
    It first() const;
};
class P::It {
public:
    It(int x) : k(x) {}
    int k;
};
inline P::It P::first() const { return It(v * 2); }
}  // namespace b
}  // namespace a
struct M {
    std::vector<std::uint32_t> keys;
    std::vector<int> vals;
    void clear() { std::vector<std::uint32_t>().swap(keys); std::vector<int>().swap(vals); }
};
enum class CM { Average, Minimum, Maximum };
struct D { const char *n; CM a, b; };
static D Make(const char *n, CM a = CM::Average, CM b = CM::Average) { return D{n, a, b}; }
static std::vector<D> Build() {
    using C = CM;
    return {Make("x"), Make("y", C::Minimum), Make("z", C::Maximum, C::Minimum)};
}
int main() {
    M m;
    m.keys.push_back(3);
    m.vals.push_back(4);
    m.clear();
    std::printf("%zu %zu\n", m.keys.size(), m.vals.size());
    std::vector<int> v{1, 2};
    std::string("abc").size();
    std::vector<int>{3}.swap(v);
    std::printf("%zu %d\n", v.size(), v[0]);
    for (const D &d : Build()) std::printf("%s %d %d\n", d.n, (int)d.a, (int)d.b);
    a::b::P p;
    std::printf("%d\n", p.first().k);
}
