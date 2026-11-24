// The OkayScript core instantiated for the engine's own script value (vs::VsValue), the same one the engine's
// OkayScriptVM runs on. Vectors, object references and a Host answering scene-style names, run through the same
// parser and core as the standalone language.
#include "okay/VisualScript/VsValue.hpp"
#include "okayscript/detail/Interpreter.ipp"
#include <iostream>
#include <sstream>

using V = okay::vs::VsValue;
// The engine already instantiates the core for its value (engine/src/Scripting/OkayScriptVM.cpp); this test adds the
// embedding front end on top of that same instantiation.
extern template class okayscript::BasicRuntime<V>;
extern template class okayscript::BasicArgs<V>;
template class okayscript::BasicInterpreter<V>;

static int g_fail = 0, g_checks = 0;
static void Check(bool ok, const std::string& what, const std::string& got) {
    ++g_checks;
    if (!ok) { ++g_fail; std::cerr << "  FAIL: " << what << "  got: " << got << "\n"; }
}

namespace {
struct SceneHost : okayscript::BasicHost<V> {
    okay::Vec3 position{1, 2, 3};
    bool ReadName(okayscript::BasicRuntime<V>&, const std::string& name, V& out) override {
        if (name == "transform.position") { out = V{position}; return true; }
        if (name == "Time.deltaTime") { out = V{0.5f}; return true; }
        return false;
    }
    bool WriteName(okayscript::BasicRuntime<V>&, const std::string& name, const V& v) override {
        if (name == "transform.position") { position = v.AsVec3(); return true; }
        if (name == "transform.position.x") { position.x = v.AsFloat(); return true; }
        return false;
    }
};
} // namespace

int main() {
    okayscript::BasicInterpreter<V> in;
    std::ostringstream out;
    in.Define("print", [&out](okayscript::BasicArgs<V>& a) {
        for (std::size_t i = 0; i < a.Size(); ++i) out << (i ? " " : "") << a[i].AsString();
        out << "\n";
        return V{};
    });
    in.Define("Vector3", [](okayscript::BasicArgs<V>& a) {
        return V{okay::Vec3{(float)a.Number(0), (float)a.Number(1), (float)a.Number(2)}};
    }, okayscript::Arity::Exactly(3));
    SceneHost host;
    in.SetHost(&host);
    auto run = [&](const std::string& src) {
        out.str("");
        try { in.Run(src); } catch (const okayscript::ScriptError& e) { out << "ERROR " << e.line << ": " << e.what(); }
        return out.str();
    };

    std::string r = run("var v = Vector3(1, 2, 2)\nprint(v, v * 2, 2 * v, v + 1, v.magnitude, v.x, -v)");
    Check(r == "(1, 2, 2) (2, 4, 4) (2, 4, 4) (2, 3, 3) 3 1 (-1, -2, -2)\n", "vector maths on VsValue", r);
    r = run("print(v == Vector3(1, 2, 2), v != Vector3(1, 2, 2), v / 0)");
    Check(r == "true false (0, 0, 0)\n", "vector equality and division by zero", r);
    r = run("var p = transform.position\ntransform.position = p + Vector3(0, 0, 1)\ntransform.position.x = 9\nprint(transform.position, Time.deltaTime)");
    Check(r == "(9, 2, 4) 0.5\n", "host names read and written", r);
    Check(host.position.x == 9 && host.position.z == 4, "host state changed", r);
    r = run("var n = 16777217\nprint(n, 7 / 2, 1 / 0, \"a\" + 1)");
    Check(r == "16777216 3.5 0 a1\n", "32-bit float numbers and engine rules", r);
    r = run("function f(a, b = 2) { return a * b }\nvar xs = [3, 1, 2]\nxs.Sort()\nprint(f(4), xs, xs.Select(x => x * 10))");
    Check(r == "8 [1, 2, 3] [10, 20, 30]\n", "functions, lists, lambdas", r);
    r = run("var o = 0\nprint(o ?? \"none\")\nnope()");
    Check(r == "none\nERROR 3: call to undefined function 'nope'", "null and errors with a line", r);
    // Language stage 2 on the engine's value (docs/scripting/okayscript_vm.md): no 64-bit integers there, so 123L and
    // (long)x are its 32-bit floats; closures, finally, generators, map literals and classes work the same.
    r = run("var big = 16777217L\nprint(big, (long)2.9, 7L / 2)");
    Check(r == "16777216 2 3.5\n", "123L is the engine's float (no Int in VsValue)", r);
    r = run("function counter() { var n = 0; return () => { n += 1; return n; }; }\nvar c = counter()\nc()\nprint(c(), {k: 3}.k)");
    Check(r == "2 3\n", "closures and map literals on VsValue", r);
    r = run("function g() { yield 1; yield 2; }\nvar s = 0\nforeach (var x in g()) { s += x; }\n"
            "function f() { try { return 1; } finally { s += 10; } }\nf()\nprint(s)");
    Check(r == "13\n", "generators and finally on VsValue", r);
    r = run("class A { public int val = 1; public int Get() { return val; } }\nclass B : A { public int Get() { return super.Get() + 10; } }\n"
            "print(new B().Get())");
    Check(r == "11\n", "class inheritance and super on VsValue", r);
    std::cout << g_checks << " checks, " << g_fail << " failures\n";
    return g_fail == 0 ? 0 : 1;
}
