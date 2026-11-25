// Round 24: <variant> ([variant]) as the script values use it: a variant of twelve alternatives
// with monostate first, built from each type, copied, assigned (same and other alternative),
// holds_alternative, get_if, get (and bad_variant_access), index, destruction of a string.
#include <cstdio>
#include <string>
#include <variant>
#include <vector>
struct ObjRef { int id; };
struct ArrayRef { int n; };
struct Vec3 { float x, y, z; };
struct A1 { int a; };
struct A2 { int a; };
struct A3 { int a; };
struct A4 { int a; };
using Value = std::variant<std::monostate, bool, int, float, std::string, Vec3, ObjRef, ArrayRef, A1, A2, A3, A4>;
static const char *kind(const Value &v) {
    if (std::holds_alternative<std::monostate>(v)) return "nil";
    if (std::holds_alternative<bool>(v)) return "bool";
    if (std::holds_alternative<int>(v)) return "int";
    if (std::holds_alternative<float>(v)) return "float";
    if (std::holds_alternative<std::string>(v)) return "string";
    if (std::holds_alternative<ObjRef>(v)) return "obj";
    return "other";
}
int main() {
    std::vector<Value> vals;
    vals.push_back(Value());
    vals.push_back(Value(true));
    vals.push_back(Value(42));
    vals.push_back(Value(2.5f));
    vals.push_back(Value(std::string("hello")));
    ObjRef o;
    o.id = 7;
    vals.push_back(Value(o));
    A4 a4;
    a4.a = 3;
    vals.push_back(Value(a4));
    for (const Value &v : vals) printf("%s/%d ", kind(v), (int)v.index());
    printf("\n");
    Value s = vals[4];
    if (const std::string *p = std::get_if<std::string>(&s)) printf("str %s\n", p->c_str());
    printf("null %d\n", std::get_if<int>(&s) == nullptr ? 1 : 0);
    s = 5;
    printf("now %s %d\n", kind(s), std::get<int>(s));
    std::get<int>(s) = 9;
    s = std::string("again");
    printf("%s %s\n", kind(s), std::get<std::string>(s).c_str());
    Value t = s;
    s = vals[5];
    printf("%s %s id %d\n", kind(t), kind(s), std::get<ObjRef>(s).id);
    try {
        std::get<float>(s);
    } catch (const std::bad_variant_access &e) {
        printf("caught %d\n", e.what() != nullptr);  // the text is the library's own
    }
    printf("size %d\n", (int)std::variant_size<Value>::value);
}
