// Link-and-run test of inline functions' static locals across two objects (Round 36, [dcl.inline] 6), and of inline
// variables and a lambda's static local in an inline function (Round 37): the main file and
// inline_part.cpp both use Registry::Names(), Registry::Made(), Counter() and Greeting() from inline_shared.hpp, and see
// one object each. Before Round 36 okay-cl gave every object its own copy (an internal symbol), so the part's additions
// were not seen here. Built by okay-cl (/O0 /O1 /O2) and by MinGW g++, run under Wine; the printed text must agree.
#include "mini.hpp"
#include "inline_shared.hpp"

int main() {
    RUN_SUITE("inline_static");
    Registry::Names().push_back("main");
    const int n = part::AddFromPart("part");
    CHECK(n == 2);
    CHECK(Registry::Names().size() == 2);
    CHECK(Registry::Names().size() == 2 && Registry::Names()[1] == "part");
    CHECK(Registry::Made() == 101);
    ++Counter();
    const int c = part::CounterFromPart();
    CHECK(c == 2 && Counter() == 2);
    const std::string g = part::GreetingFromPart();
    CHECK(g == "hello there!");
    Greeting() += "?";
    CHECK(Greeting() == "hello there!?");
    CHECK(part::GreetingFromPart() == "hello there!?!");
    // Round 37: inline variables and a lambda's static local in an inline function, changed by the part.
    CHECK(LambdaTicks() == 1);
    const int ticks = part::InlineVarsFromPart();
    CHECK(ticks == 2);
    CHECK(g_inlineCount == 15 && part::g_scale == 2.5f && !Settings::visible && Settings::levels[2] == 30);
    CHECK(part::g_label[0] == 'p');
    std::printf("names %d counter %d greeting %s\n", (int)Registry::Names().size(), Counter(), Greeting().c_str());
    std::printf("inline vars %d %d %d %s ticks %d\n", g_inlineCount, (int)(part::g_scale * 10), Settings::levels[2],
                part::g_label, LambdaTicks());
    TEST_MAIN_RESULT();
}
