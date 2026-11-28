// Round 33: init-captures [x = e] and [&r = x] ([expr.prim.lambda.capture] p6);
// engine/include/okay/Backend/Supabase.hpp's [d = DirectSend(..)] (the first error of 22 files).
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
static int live = 0;
struct Tracked {
    std::string name;
    explicit Tracked(std::string n) : name(std::move(n)) { ++live; }
    Tracked(const Tracked& o) : name(o.name) { ++live; }
    ~Tracked() { --live; }
};
struct Direct {
    std::string a, b;
    int n;
    int operator()(int k) const { return (int)a.size() * 100 + (int)b.size() * 10 + n + k; }
};
static Direct Make(std::string a, std::string b, int n) { return Direct{std::move(a), std::move(b), n}; }
struct Functions {
    std::function<int(int)> send_;
    Functions(std::string url, std::string key, int timeout) {
        send_ = [d = Make(std::move(url), std::move(key), timeout)](int q) { return d(q); };
    }
};
int main() {
    std::string x = "abc", y = "de";
    int k = 2;
    auto f = [d = Make(std::move(x), y, k), m = k * 3](int q) { return d(q) + m; };
    std::function<int(int)> g = [s = std::string("hey"), y](int q) { return (int)(s + y).size() + q; };
    int total = 0;
    auto add = [&t = total, step = 5](int times) {
        for (int i = 0; i < times; ++i) t += step;
    };
    add(3);
    auto counter = [n = 10]() mutable { return ++n; };
    counter();
    int c2 = counter();
    {
        Tracked t("obj");
        auto keep = [copy = t, len = t.name.size()] { return copy.name + std::to_string(len); };
        auto nested = [inner = std::string("in")] {
            auto twice = [both = inner + inner] { return both; };
            return twice();
        };
        std::printf("%s %s %d\n", keep().c_str(), nested().c_str(), live > 0);
    }
    Functions fn("https://x", "key", 3);
    std::printf("%d %d %d %d %d live %d\n", f(1), g(4), total, c2, fn.send_(1), live);
    return 0;
}
