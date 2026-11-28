// Round 33: in a member function body a member of the class (or of a base) hides a type of the
// same name declared in another namespace ([basic.lookup.unqual] p8, [class.member.lookup]);
// engine/include/okay/Components/ModelAnimator.hpp's Bind(i) beside engine/src/Script's
// enum class Bind (the first error of 9 engine files). Bind(i) was read as a cast to the enum.
#include <cstdint>
#include <cstdio>
#include <vector>
namespace okay {
namespace script {
enum class Bind : std::uint8_t { None, Field, Method };
struct Clip {
    int frames = 3;
};
}  // namespace script
struct Base {
    int Clip(int k) const { return k * 7; }
};
struct Animator : Base {
    std::vector<int> v{4, 5, 6};
    int Fade(int i) const {
        const auto& b = Bind(i);
        return (int)b.size() + b[0] + Clip(2);
    }
    const std::vector<int>& Bind(int) const { return v; }
};
}  // namespace okay
int main() {
    okay::Animator a;
    okay::script::Bind k = okay::script::Bind::Method;
    okay::script::Clip c;
    std::printf("%d %d %d\n", a.Fade(0), (int)k, c.frames);
    return 0;
}
