// An inline namespace ([namespace.def] p7) and a user-defined string literal ([lex.ext] p5,
// [over.literal]), as in Core/StringId.hpp: "name"_sid calls operator""_sid("name", 4).
#include <cstddef>
#include <cstdio>
#include <string_view>
namespace game {
struct Id {
    unsigned long h;
    static constexpr Id From(std::string_view s) {
        unsigned long x = 1469598103934665603UL;
        for (char c : s) x = (x ^ (unsigned char)c) * 1099511628211UL;
        return Id{x};
    }
};
inline namespace literals {
constexpr Id operator""_id(const char *s, std::size_t n) { return Id::From(std::string_view(s, n)); }
inline int twice(int v) { return 2 * v; }
}  // namespace literals
inline namespace v2 {
int version() { return 2; }
}
int callTwice() { return twice(4); }
}  // namespace game
static std::size_t operator""_len(const char *, std::size_t n) { return n; }
int main() {
    using namespace game::literals;
    game::Id a = "player.jump"_id;
    game::Id b = game::Id::From("player.jump");
    std::printf("%d %lu\n", a.h == b.h ? 1 : 0, a.h % 1000);
    std::printf("%zu %zu\n", "hello"_len, "a" "bc"_len);
    std::printf("%d %d %d\n", game::twice(3), game::version(), game::callTwice());
    std::printf("%d\n", game::literals::twice(5));
    return 0;
}
