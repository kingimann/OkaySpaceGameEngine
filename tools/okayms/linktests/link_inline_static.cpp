// Link-and-run test of inline functions' static locals across two objects (Round 36, [dcl.inline] 6): the main file and
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
    std::printf("names %d counter %d greeting %s\n", (int)Registry::Names().size(), Counter(), Greeting().c_str());
    TEST_MAIN_RESULT();
}
