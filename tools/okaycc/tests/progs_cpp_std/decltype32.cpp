// Round 32: decltype(C::m) of a non-static data member ([expr.prim.id] p3, [dcl.type.decltype] p1.3),
// also inherited from a class template base, and a data member whose type is written with it
// (engine/src/Scripting/OkayScriptVMInternal.hpp:531, `using BuiltinTable = decltype(Runtime::builtins)`).
#include <cstdio>
#include <string>
#include <map>
namespace lib { template <class V> struct Base { std::map<std::string, V> builtins; int other = 0; }; }
namespace eng {
struct Runtime;
void Init(Runtime& rt);
struct Runtime : lib::Base<int> { int host = 0; double scale = 1.5; };
using Table = decltype(Runtime::builtins);
struct Alias {
    Table* table;
    decltype(Runtime::scale) factor;
    int get(const char* k) const { auto it = table->find(k); return it != table->end() ? it->second : -1; }
};
struct Holder { int a; Table own; decltype(Runtime::host) h; };
}
int main() {
    eng::Runtime r;
    r.builtins["a"] = 5;
    eng::Alias al{&r.builtins, 2.25};
    eng::Holder h;
    h.a = 1;
    h.own["x"] = 7;
    h.h = 3;
    eng::Table t = h.own;
    std::printf("%d %d %g %d %d %d %d\n", al.get("a"), al.get("b"), al.factor, (int)t.size(), t["x"], h.h,
                (int)(sizeof(eng::Holder) > sizeof(eng::Table)));
    return 0;
}
