// Round 24: an auto return type of a class (auto f() { return std::string("tag"); }); round 23
// destroyed the returned temporary and printed an empty string.
#include <cstdio>
#include <string>
static auto name() { return std::string("tag"); }
int main() { printf("%s\n", name().c_str()); }
