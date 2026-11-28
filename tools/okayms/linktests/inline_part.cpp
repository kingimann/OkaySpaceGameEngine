// The second object of link_inline_static (Round 36): it uses the inline functions of inline_shared.hpp, whose static
// locals are the same objects as the main file's.
#include "inline_shared.hpp"

namespace part {
int AddFromPart(const char* name) {
    Registry::Names().push_back(name);
    ++Registry::Made();
    return static_cast<int>(Registry::Names().size());
}
int CounterFromPart() { return ++Counter(); }
std::string GreetingFromPart() {
    Greeting() += "!";
    return Greeting();
}
}  // namespace part
