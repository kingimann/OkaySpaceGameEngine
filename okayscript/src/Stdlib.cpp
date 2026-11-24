// The OkayScript standard natives (Stdlib.hpp). Written for this project with the C++ standard library only.
#include "okayscript/Stdlib.hpp"
#include "okayscript/Interpreter.hpp"
#include <cmath>
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
void Math1(Interpreter& in, const char* name, double (*f)(double)) {
    in.Define(name, [f](Args& a) { return Value{f(a.Number(0))}; }, Arity::Exactly(1));
}
} // namespace

void InstallStandardLibrary(Interpreter& in) {
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
    in.Define("len", [](Args& a) {
        const Value& v = a[0];
        if (v.IsString()) return Value{(double)v.AsString().size()};
        if (auto l = v.AsArray()) return Value{(double)l->size()};
        if (auto m = v.AsMap()) return Value{(double)m->size()};
        a.Fail(std::string("needs text, a list or a map, got ") + v.TypeName());
    }, Arity::Exactly(1));
    in.Define("str", [](Args& a) { return Value{a[0].AsString()}; }, Arity::Exactly(1));
    in.Define("num", [](Args& a) {
        const Value& v = a[0];
        if (v.IsNumber()) return v;
        if (v.IsBool()) return Value{v.AsBool() ? 1.0 : 0.0};
        double d = 0.0;
        if (v.IsString() && Value::ParseFinite(v.AsString(), d)) return Value{d};
        a.Fail("'" + v.AsString() + "' is not a number");
    }, Arity::Exactly(1));
    in.Define("type", [](Args& a) { return Value{std::string(a[0].TypeName())}; }, Arity::Exactly(1));

    Math1(in, "abs", [](double x) { return std::fabs(x); });
    Math1(in, "floor", [](double x) { return std::floor(x); });
    Math1(in, "ceil", [](double x) { return std::ceil(x); });
    Math1(in, "round", [](double x) { return std::round(x); });
    Math1(in, "sin", [](double x) { return std::sin(x); });
    Math1(in, "cos", [](double x) { return std::cos(x); });
    Math1(in, "tan", [](double x) { return std::tan(x); });
    in.Define("sqrt", [](Args& a) {
        const double x = a.Number(0);
        if (x < 0) a.Fail("can't take the square root of a negative number");
        return Value{std::sqrt(x)};
    }, Arity::Exactly(1));
    in.Define("pow", [](Args& a) { return Value{std::pow(a.Number(0), a.Number(1))}; }, Arity::Exactly(2));
    auto minmax = [](bool wantMax) {
        return [wantMax](Args& a) {
            // min(1, 2, 3) or min([1, 2, 3])
            std::vector<Value> items;
            if (a.Size() == 1 && a[0].AsArray()) items = *a[0].AsArray();
            else items = a.All();
            if (items.empty()) a.Fail("needs at least one number");
            double best = 0;
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (!items[i].IsNumber()) a.Fail(std::string("needs numbers, got ") + items[i].TypeName());
                const double x = items[i].AsNumber();
                if (i == 0 || (wantMax ? x > best : x < best)) best = x;
            }
            return Value{best};
        };
    };
    in.Define("min", minmax(false), Arity::AtLeast(1));
    in.Define("max", minmax(true), Arity::AtLeast(1));
    in.Define("clamp", [](Args& a) {
        const double x = a.Number(0), lo = a.Number(1), hi = a.Number(2);
        return Value{x < lo ? lo : (x > hi ? hi : x)};
    }, Arity::Exactly(3));
    in.Define("exit", [](Args& a) -> Value {
        throw ExitRequest{a.Size() > 0 ? (int)a.Number(0) : 0};
    }, Arity::Between(0, 1));
    in.SetGlobal("PI", Value{3.14159265358979323846});
}

} // namespace okayscript
