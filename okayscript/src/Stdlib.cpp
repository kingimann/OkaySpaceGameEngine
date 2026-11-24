// The standalone OkayScript standard library (Stdlib.hpp). Written for this project with the C++ standard library only.
#include "okayscript/Stdlib.hpp"
#include "okayscript/CoreBuiltins.hpp"
#include "okayscript/detail/Support.hpp"
#include <istream>
#include <ostream>
#include <string>

namespace okayscript {

namespace {
std::string Join(Args& a) {
    std::string s;
    for (std::size_t i = 0; i < a.Size(); ++i) {
        if (i) s += ' ';
        s += a[i].AsString();
    }
    return s;
}
} // namespace

void InstallStandardLibrary(Interpreter& in) {
    InstallMathBuiltins<Value>(in.Rt().builtins);
    InstallConversionBuiltins<Value>(in.Rt().builtins);

    Interpreter* self = &in;
    auto printLine = [self](Args& a) {
        std::ostream& o = self->Output();
        o << Join(a) << '\n';
        o.flush();
        return Value{};
    };
    in.Define("print", printLine);
    in.Define("println", printLine);
    in.Define("write", [self](Args& a) {
        std::ostream& o = self->Output();
        o << Join(a);
        o.flush();
        return Value{};
    });
    in.Define("input", [self](Args& a) {
        if (a.Size() > 0) { self->Output() << a[0].AsString(); self->Output().flush(); }
        std::string line;
        if (!std::getline(self->Input(), line)) return Value{std::string()};
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return Value{line};
    }, Arity::Between(0, 1));
    in.Define("exit", [](Args& a) -> Value {
        // A whole number 0..255 (what a process can end with); NaN and fractions are handled like (int).
        int code = a.Size() > 0 ? detail::SafeInt(a[0].AsFloat()) : 0;
        code = code < 0 ? 0 : (code > 255 ? 255 : code);
        throw ExitRequest{code};
    }, Arity::Between(0, 1));
}

} // namespace okayscript
