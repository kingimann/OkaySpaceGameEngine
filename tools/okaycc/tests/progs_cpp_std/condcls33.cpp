// Round 33: c ? x : y with operands of two classes, not both lvalues ([expr.cond] p4): a derived
// object converts to a copy of its base part, another class by a converting constructor;
// engine/src/Components/ScriptComponent.cpp (other ? other->tag : std::string()) and
// engine/src/Script/CheckerExpressions.cpp (c ? Fold(x) : std::nullopt).
#include <cstdio>
#include <optional>
#include <string>
static int live = 0;
struct TagString : std::string {
    TagString(const char* s) : std::string(s) {}
};
struct GameObject {
    TagString tag{"player"};
};
struct Value {
    std::string s;
    Value(std::string x) : s(x) { ++live; }
    Value(const Value& o) : s(o.s) { ++live; }
    ~Value() { --live; }
};
static std::optional<int> Fold(int x) { return x * 3; }
static Value Make(GameObject* other) { return Value(other ? other->tag : std::string()); }
int main() {
    GameObject g;
    Value a = Make(&g), b = Make(nullptr);
    std::optional<int> f1 = 2 > 1 ? Fold(4) : std::nullopt;
    std::optional<int> f2 = 2 < 1 ? Fold(4) : std::nullopt;
    std::string t = g.tag.size() > 3 ? std::string("long") : g.tag;
    std::printf("[%s] [%s] %d %d %d [%s]\n", a.s.c_str(), b.s.c_str(), f1 ? *f1 : -1, (int)f2.has_value(), live,
                t.c_str());
    return 0;
}
