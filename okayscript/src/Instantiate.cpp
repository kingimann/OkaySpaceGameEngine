// The interpreter core instantiated for the standalone value (okayscript::Value), plus the core's non-template parts.
#include "okayscript/detail/Interpreter.ipp"
#include "okayscript/Interpreter.hpp"
#include <unordered_map>

namespace okayscript {

template class BasicRuntime<Value>;
template class BasicArgs<Value>;
template class BasicInterpreter<Value>;

unsigned char TypeKind(const std::string& t) {
    static const std::unordered_map<std::string, unsigned char> k = {
        {"int", KInt}, {"long", KInt}, {"short", KInt}, {"byte", KInt}, {"uint", KInt},
        {"ulong", KInt}, {"ushort", KInt}, {"sbyte", KInt},
        {"float", KFloat}, {"double", KFloat}, {"decimal", KFloat},
        {"bool", KBool}, {"string", KString}};
    auto it = k.find(t);
    return it == k.end() ? (unsigned char)KAny : it->second;
}
const char* KindName(unsigned char k) {
    switch (k & 0x0F) { case KInt: return "an int"; case KFloat: return "a float"; case KBool: return "a bool";
                        case KString: return "a string"; default: return "a variable"; }
}

std::string Arity::Describe() const {
    auto args = [](int n) { return std::to_string(n) + (n == 1 ? " argument" : " arguments"); };
    if (max < 0) return min == 0 ? std::string("any number of arguments") : "at least " + args(min);
    if (min == max) return args(min);
    return std::to_string(min) + " to " + args(max);
}

} // namespace okayscript
