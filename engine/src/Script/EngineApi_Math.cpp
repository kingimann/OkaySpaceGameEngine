// Typed script API, Mathf: the Bind<Topic> functions of the binding table (see EngineApiInternal.hpp).
#include "EngineApiInternal.hpp"

namespace okay::script::engine_api {

void Ctx::BindMathf() {
    // ---- Mathf ----
    {
        ApiType m; m.name = "Mathf"; m.isStatic = true; m.doc = "Maths helpers.";
        m.staticProps.push_back(ro("PI", tFloat, [](const Value&, CallContext&) { return Value{Mathf::PI}; }, "3.14159..."));
        auto f1 = [&](const char* n, float (*fn)(float), const char* doc) {
            m.staticMethods.push_back(method(n, {tFloat}, {"value"}, tFloat,
                [fn](const Value&, std::vector<Value>& a, CallContext&) { return Value{fn(F(a[0]))}; }, doc));
        };
        f1("Abs", [](float x) { return std::fabs(x); }, "The value without its sign.");
        f1("Sqrt", [](float x) { return std::sqrt(std::max(0.0f, x)); }, "Square root.");
        f1("Sin", [](float x) { return std::sin(x); }, "Sine (radians).");
        f1("Cos", [](float x) { return std::cos(x); }, "Cosine (radians).");
        f1("Tan", [](float x) { return std::tan(x); }, "Tangent (radians).");
        f1("Floor", [](float x) { return std::floor(x); }, "Round down.");
        f1("Ceil", [](float x) { return std::ceil(x); }, "Round up.");
        f1("Round", [](float x) { return std::round(x); }, "Round to the nearest whole number.");
        f1("Sign", [](float x) { return x < 0.0f ? -1.0f : 1.0f; }, "-1 for negative values, else 1.");
        f1("Clamp01", [](float x) { return Mathf::Clamp(x, 0.0f, 1.0f); }, "Keep between 0 and 1.");
        auto f2 = [&](const char* n, float (*fn)(float, float), const char* a, const char* b, const char* doc) {
            m.staticMethods.push_back(method(n, {tFloat, tFloat}, {a, b}, tFloat,
                [fn](const Value&, std::vector<Value>& x, CallContext&) { return Value{fn(F(x[0]), F(x[1]))}; }, doc));
        };
        f2("Min", [](float a, float b) { return std::min(a, b); }, "a", "b", "The smaller value.");
        f2("Max", [](float a, float b) { return std::max(a, b); }, "a", "b", "The larger value.");
        f2("Pow", [](float a, float b) { return std::pow(a, b); }, "value", "power", "value to the power.");
        f2("Atan2", [](float y, float x) { return std::atan2(y, x); }, "y", "x", "Angle of (x, y) in radians.");
        auto f3 = [&](const char* n, float (*fn)(float, float, float), std::vector<std::string> names, const char* doc) {
            m.staticMethods.push_back(method(n, {tFloat, tFloat, tFloat}, std::move(names), tFloat,
                [fn](const Value&, std::vector<Value>& x, CallContext&) { return Value{fn(F(x[0]), F(x[1]), F(x[2]))}; }, doc));
        };
        f3("Clamp", [](float v, float lo, float hi) { return Mathf::Clamp(v, lo, hi); }, {"value", "min", "max"}, "Keep between min and max.");
        f3("Lerp", [](float a, float b, float t) { return a + (b - a) * Mathf::Clamp(t, 0.0f, 1.0f); }, {"a", "b", "t"}, "Blend from a (t = 0) to b (t = 1).");
        f3("MoveTowards", [](float c, float t, float d) { return std::fabs(t - c) <= d ? t : c + (t > c ? d : -d); },
           {"current", "target", "maxDelta"}, "Step from current toward target by at most maxDelta.");
        api.types.push_back(std::move(m));
    }
}

} // namespace okay::script::engine_api
