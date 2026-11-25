// The elements of a std::filesystem::path ([fs.path.itr]) and the walk TexturePath.hpp's Inside does.
#include <filesystem>
#include <cstdio>
#include <string>
namespace fs = std::filesystem;
static void show(const char *s) {
    fs::path p(s);
    std::printf("[%s]:", s);
    int n = 0;
    for (auto it = p.begin(); it != p.end(); ++it, ++n) std::printf(" '%s'%s", it->string().c_str(), it->empty() ? "(e)" : "");
    std::printf(" n=%d\n", n);
}
static bool inside(const fs::path &p, const fs::path &root) {
    auto r = root.begin(), q = p.begin();
    for (; r != root.end(); ++r, ++q) {
        if (r->empty()) continue;
        if (q == p.end() || !(*r == *q)) return false;
    }
    return true;
}
int main() {
    const char *k[] = {"", "/", "a", "a/b", "/a/b", "/a//b/", "a/b/", "//x", "./a/../b"};
    for (const char *s : k) show(s);
    std::printf("%d %d %d %d\n", inside("/proj/a.png", "/proj"), inside("/proj2/a.png", "/proj"),
                inside("/proj/x/y", "/proj/"), inside("/p", "/proj"));
    int count = 0;
    for (const fs::path &e : fs::path("/usr/local/lib")) count += (int)e.string().size();
    std::printf("%d\n", count);
}
