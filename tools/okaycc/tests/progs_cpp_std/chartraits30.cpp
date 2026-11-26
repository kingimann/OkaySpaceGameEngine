// std::char_traits<char> (eof, length, compare, to_int_type; Scene/SceneSerializerInternal.hpp:79,
// VisualScript/VsRefs.hpp:56).
#include <cstdio>
#include <sstream>
#include <string>
static bool NumberNext(std::istream& in) {
    in >> std::ws;
    const int c = in.peek();
    if (c == std::char_traits<char>::eof()) return false;
    return c == '-' || c == '+' || c == '.' || (c >= '0' && c <= '9');
}
int main() {
    std::istringstream a("  42"), b("   "), c(" x");
    std::printf("%d %d %d\n", NumberNext(a), NumberNext(b), NumberNext(c));
    const char* prefix = "scene:";
    std::string name = "scene:Main";
    if (name.compare(0, std::char_traits<char>::length(prefix), prefix) == 0)
        name.erase(0, std::char_traits<char>::length(prefix));
    std::printf("%s %d\n", name.c_str(), std::char_traits<char>::compare("abc", "abd", 3) < 0);
    std::printf("%d %d\n", std::char_traits<char>::to_int_type((char)0xff), std::char_traits<char>::eq('a', 'a'));
    return 0;
}
