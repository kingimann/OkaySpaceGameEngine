// Round 33: std::quoted ([quoted.manip]), std::lgamma and std::tgamma (C 7.12.8), std::llabs
// (C 7.22.6.1), string::capacity, and std::min/std::max from <string> alone (as with g++).
// engine/src/Components/Character.cpp, Core/RandomExtras.cpp, Platform/GLPainter.cpp,
// Core/Undo.cpp, Render/OkayShader.cpp.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>
int main() {
    std::ostringstream os;
    std::string path = "my \"clips\" dir\\x";
    os << std::quoted(std::string("a b")) << ' ' << std::quoted(path) << ' ' << std::quoted("plain", '\'');
    std::printf("%s\n", os.str().c_str());
    std::istringstream is(os.str());
    std::string a, b, c;
    is >> std::quoted(a) >> std::quoted(b) >> std::quoted(c);
    std::printf("[%s] [%s] [%s] %d\n", a.c_str(), b.c_str(), c.c_str(), (int)(b == path));
    std::istringstream plain("word next");
    std::string w;
    plain >> std::quoted(w);
    std::printf("[%s]\n", w.c_str());
    const double xs[] = {0.5, 1.0, 1.5, 2.0, 3.0, 4.5, 7.25, 10.0, 31.0, 171.5, -0.5, -2.5};
    for (double x : xs) std::printf("%.9f ", std::lgamma(x));
    std::printf("\n%.6f %.6f %.6f %d\n", std::tgamma(5.0), std::tgamma(0.5), std::tgamma(-1.5), (int)std::isinf(std::lgamma(-3.0)));
    std::printf("%lld %lld\n", std::llabs(-1234567890123LL), std::llabs(42LL));
    std::string s(40, 'x');
    std::printf("%d %d %d\n", (int)(s.capacity() >= s.size()), std::min<std::size_t>(s.size(), 4) == 4 ? 1 : 0, std::max(3, 9));
    return 0;
}
