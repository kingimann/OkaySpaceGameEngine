// Round 33: a class declared in one namespace with the name of another namespace's class: the
// bare name is the new class inside its namespace (also when the namespace is reopened) and the
// old one elsewhere ([basic.scope.namespace], [basic.lookup.unqual], [dcl.type.elab] p2);
// engine/src/Script/ScriptInternal.hpp's script::ArrayObj after JsInternal.hpp's js::ArrayObj.
#include <cstdio>
#include <memory>
#include <vector>
namespace okay {
namespace js {
struct ArrayObj {
    int length = 3;
};
}  // namespace js
namespace script {
struct ArrayObj;
using ArrayRef = std::shared_ptr<ArrayObj>;
struct ArrayObj {
    std::vector<int> items;
};
int Count(const ArrayRef& a) { return (int)a->items.size(); }
}  // namespace script
namespace js {
int Len() {
    ArrayObj a;  // js::ArrayObj again
    return a.length;
}
}  // namespace js
namespace script {
int Twice(ArrayObj& a) { return 2 * (int)a.items.size(); }  // reopened: script::ArrayObj
}  // namespace script
namespace sc = script;
int F(const sc::ArrayRef& a) {
    js::ArrayObj j;
    return sc::Count(a) + j.length;
}
}  // namespace okay
struct Node {
    int v = 1;
};
namespace tree {
struct Node {
    int v = 2, w = 3;
};
int Get() { return Node().w; }
}  // namespace tree
// A function template whose instance is read at the end of the file, inside its namespace
// (engine/include/okay/Core/Inflate.hpp's RawInto after Brotli.hpp's brotli::Options).
namespace brotli {
struct Options {
    int window = 1;
};
}  // namespace brotli
namespace inflate {
struct Options {
    int maxOut = 2, checkTrailer = 3;
};
template <class Vec>
int RawInto(Vec& out, const Options& op) {
    out += op.checkTrailer;
    return out * op.maxOut;
}
inline int Raw(int v) {
    Options o;
    return RawInto(v, o);
}
}  // namespace inflate
int main() {
    auto a = std::make_shared<okay::script::ArrayObj>();
    a->items.push_back(1);
    a->items.push_back(5);
    std::printf("%d %d ", inflate::Raw(4), brotli::Options().window);
    std::printf("%d %d %d %d %d %d\n", okay::F(a), okay::js::Len(), okay::script::Twice(*a), Node().v, tree::Get(),
                tree::Node().v);
    return 0;
}
