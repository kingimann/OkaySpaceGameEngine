// Round 32: a function template parameter that names no template parameter takes no part in deduction and
// accepts its argument through a converting constructor ([temp.deduct.call] p4, [over.ics.user]):
// Localize("hold.drop", keys) with Localize(const std::string &, A &&...) (Components/HoldPrompt.hpp:98).
#include <cstdio>
#include <string>
#include <vector>
#include <utility>
namespace okay {
struct LocArg { std::string s; LocArg(const std::string& v) : s(v) {} LocArg(int v) : s(std::to_string(v)) {} };
class Localization {
public:
    static std::string LocalizeArgs(const std::string& key, const std::vector<LocArg>& args) {
        std::string r = key;
        for (const LocArg& a : args) r += "|" + a.s;
        return r;
    }
    template <class... A> static std::string Localize(const std::string& key, A&&... args) {
        return LocalizeArgs(key, std::vector<LocArg>{LocArg(std::forward<A>(args))...});
    }
};
template <class... A> std::string Localize(const std::string& key, A&&... args) {
    return Localization::Localize(key, std::forward<A>(args)...);
}
struct Prompt {
    bool localized = true;
    std::string Text(const std::string& keys, const std::string alt) {
        std::string k = keys;
        k = localized ? Localize("hold.either", keys, alt) : keys + " or " + alt;
        return localized ? Localize("hold.drop", k) : k + " to drop";
    }
};
}
template <class... A> std::string Loc(const std::string& key, A&&... args) {
    std::string r = key;
    int n = sizeof...(args);
    return r + std::to_string(n);
}
template <class T> int One(const std::string& key, T v) { return (int)key.size() + (int)v; }
int main() {
    okay::Prompt p;
    std::printf("%s %s\n", p.Text("E", "F").c_str(), okay::Localize("n", 3).c_str());
    std::string k = "E", alt = "F";
    std::printf("%s %s %s %d\n", Loc("x", k, alt).c_str(), Loc("y", 3).c_str(), Loc("z").c_str(), One("abc", 2));
    return 0;
}
