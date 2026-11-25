// Round 24: <locale> with the classic locale only: classic(), copies, comparison, a locale made
// with a program facet (not kept), imbue on string streams, the character tests with a locale.
#include <cstdio>
#include <locale>
#include <sstream>
#include <string>
struct MyFacet : std::locale::facet {
    static std::locale::id id;
    explicit MyFacet(unsigned long r = 0) : std::locale::facet(r) {}
};
std::locale::id MyFacet::id;
int main() {
    const std::locale &c = std::locale::classic();
    std::locale copy = c;
    static const std::locale *mine = new std::locale(std::locale::classic(), new MyFacet(0));
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << 1.5 << " " << 42;
    std::istringstream is("2.25 7");
    is.imbue(*mine);
    double d = 0;
    int n = 0;
    is >> d >> n;
    printf("%s %s %d %.2f %d\n", o.str().c_str(), copy.name().c_str(), copy == c ? 1 : 0, d, n);
    printf("%d %d %c %c\n", std::isspace(' ', c) ? 1 : 0, std::isdigit('x', c) ? 1 : 0, std::toupper('q', c), std::tolower('Q', c));
}
