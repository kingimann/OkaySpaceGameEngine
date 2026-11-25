// Round 23: friend declarations ([class.friend]). "friend class X;" was "expected a field" (17
// engine files stop at Scene/Component.hpp); a friend function defined in its class (a "hidden
// friend" operator, as Core/StringId.hpp has) is a function of the namespace.
#include <cstdio>
#include <map>
namespace ok {
class Scene;
class Id {
public:
    explicit Id(unsigned v) : m_value(v) {}
    unsigned Value() const { return m_value; }
    friend constexpr bool operator==(Id a, Id b) { return a.m_value == b.m_value; }
    friend constexpr bool operator!=(Id a, Id b) { return a.m_value != b.m_value; }
    friend constexpr bool operator<(Id a, Id b) { return a.m_value < b.m_value; }

private:
    friend class Scene;
    friend struct Helper;
    friend void poke(Id &);
    unsigned m_value;
};
class Scene {
public:
    static unsigned Peek(const Id &i) { return i.m_value; }
};
}  // namespace ok
int main() {
    ok::Id a(3), b(5), c(3);
    std::map<ok::Id, int> m;
    m[b] = 1;
    m[a] = 2;
    std::printf("%d %d %d %d\n", (int)(a == c), (int)(a != b), (int)(a < b), (int)ok::Scene::Peek(c));
    std::printf("%u %d\n", m.begin()->first.Value(), m.begin()->second);
    return 0;
}
