// Round 32: names in a function template instance are looked up from the template's namespace
// ([temp.res] p10, [basic.lookup.qual] p1): detail::SortLess<V> and detail::ListOf<V>({..}) inside
// a template in namespace os (okayscript/CoreBuiltins.hpp:742 and :754).
#include <vector>
#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <cstdio>
namespace os {
namespace detail { inline int one() { return 1; } }
namespace detail {
template <class V> bool SortLess(const V& x, const V& y) { return x < y; }
template <class V> V ListOf(std::vector<V> v) { V s = 0; for (auto& x : v) s += x; return s; }
struct Pair { int a, b; };
}
template <class V, class Table> void Install(Table& b) {
    b["sort"] = [](std::vector<V>& a) {
        std::stable_sort(a.begin(), a.end(), detail::SortLess<V>);
        return detail::ListOf<V>({a[0], a[1]}) + detail::one();
    };
}
template <class V> V Pick(bool first) {
    bool (*f)(const V&, const V&) = &detail::SortLess<V>;
    detail::Pair p{3, 4};
    return f(1, 2) && first ? p.a : p.b;
}
}
namespace other {
int Use() { return os::Pick<int>(true) * 10 + os::Pick<int>(false); }
}
int main() {
    std::map<std::string, std::function<int(std::vector<int>&)>> t;
    os::Install<int>(t);
    std::vector<int> v{4, 2, 8};
    int s = t["sort"](v);
    std::printf("%d %d %d %d %d\n", s, v[0], v[1], v[2], other::Use());
    return 0;
}
