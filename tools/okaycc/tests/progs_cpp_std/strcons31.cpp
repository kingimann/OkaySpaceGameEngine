// std::ifstream from a filesystem::path, std::string(s, pos[, n]) and string::erase(iterator),
// and an explicit constructor taking std::function given a lambda that owns a copy.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
struct Watch {
    std::function<double()> f;
    explicit Watch(std::function<double()> g) : f(std::move(g)) {}
};
Watch make(const std::string& name) {
    return Watch([name] { return (double)name.size(); });
}
int main() {
    std::filesystem::path p = "/proc/self/stat";
    std::ifstream f(p, std::ios::binary);
    std::printf("%d\n", f ? 1 : 0);
    std::string design = "user:wheel";
    std::string tail(design, 5);
    std::string mid(design, 1, 3);
    std::printf("%s %s %.1f\n", tail.c_str(), mid.c_str(), make("abcd").f());
    std::string s = "  xy";
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    std::printf("[%s]\n", s.c_str());
    return 0;
}
