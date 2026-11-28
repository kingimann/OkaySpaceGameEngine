// The second object of link_inline_static (Round 36): it uses the inline functions of inline_shared.hpp, whose static
// locals are the same objects as the main file's.
// A lambda before the header (Round 37): this file numbers its lambdas differently from the main file, so the lambda
// in LambdaTicks() is not found by a per-file lambda number.
static int EarlyLambda() {
    auto one = [] { return 1; };
    return one();
}
#include "inline_shared.hpp"

namespace part {
int AddFromPart(const char* name) {
    Registry::Names().push_back(name);
    ++Registry::Made();
    return static_cast<int>(Registry::Names().size());
}
int CounterFromPart() { return ++Counter(); }
int InlineVarsFromPart() {
    g_inlineCount += 10;
    g_scale = 2.5f;
    g_label = "part";
    Settings::visible = false;
    Settings::levels[2] = 30;
    return LambdaTicks() + EarlyLambda() - 1;
}
std::string GreetingFromPart() {
    Greeting() += "!";
    return Greeting();
}
}  // namespace part
