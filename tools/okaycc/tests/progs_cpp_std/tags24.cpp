// Round 24: a class over std::string as engine/include/okay/Scene/Tags.hpp writes it: using
// std::string::operator[] and other base members, member functions marked noexcept, forwarding
// with a pack, decltype(auto) results; a local lambda named like a class template (set).
#include <cstdio>
#include <set>
#include <string>
#include <utility>
class TagString : public std::string {
public:
    int touched = 0;
    TagString() {}
    TagString(const char *s) : std::string(s) {}
    void Touch() { ++touched; }
    template <class... A>
    TagString &append(A &&...a) {
        Touch();
        std::string::append(std::forward<A>(a)...);
        return *this;
    }
    void swap(std::string &o) noexcept {
        Touch();
        std::string::swap(o);
    }
    using std::string::operator[];
    using std::string::begin;
    char &operator[](size_type i) {
        Touch();
        return std::string::operator[](i);
    }
};
int main() {
    TagString t("abc");
    t.append("def");
    t[0] = 'X';
    std::string o("zz");
    t.swap(o);
    int changed = 0;
    float walk = 1.0f;
    auto set = [&](float &v, float to) {
        if (v != to) {
            v = to;
            ++changed;
        }
    };
    set(walk, 2.0f);
    set(walk, 2.0f);
    std::set<int> s;
    s.insert(3);
    printf("%s %s %d %d %.1f %d\n", t.c_str(), o.c_str(), t.touched, changed, walk, (int)s.size());
}
