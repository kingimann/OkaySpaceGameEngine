// Native C++ functions: registering one, calling it from script with arguments, its return value, state it changes,
// arity and type errors with the line of the call, callbacks into script, and the Host seam.
#include "TestKit.hpp"
#include <vector>

using namespace okayscript;
using oktest::Run;

namespace {
struct CounterHost : Host {
    double timeScale = 1.0;
    std::vector<std::string> unknown;
    bool ReadName(Runtime&, const std::string& name, Value& out) override {
        if (name == "Time.deltaTime") { out = Value{0.5}; return true; }
        if (name == "Time.timeScale") { out = Value{timeScale}; return true; }
        return false;
    }
    bool WriteName(Runtime&, const std::string& name, const Value& v) override {
        if (name == "Time.timeScale") { timeScale = v.AsNumber(); return true; }
        return false;
    }
    bool CallUnknown(Runtime&, const std::string& name, std::vector<Value>& args, Value& out) override {
        if (name.rfind("host_", 0) != 0) return false;
        unknown.push_back(name);
        out = Value{(double)args.size()};
        return true;
    }
};
} // namespace

int main() {
    TEST("a C++ function called from script: arguments in, value out");
    {
        Interpreter in;
        InstallStandardLibrary(in);
        in.Define("add", [](Args& a) { return Value{a.Number(0) + a.Number(1)}; }, Arity::Exactly(2));
        in.Define("greet", [](Args& a) { return Value{"hello " + a.String(0)}; }, Arity::Exactly(1));
        in.Define("count_args", [](Args& a) { return Value{(double)a.Size()}; });
        CHECK_EQ(Run("print(add(2, 40))\nprint(greet(\"ada\"))\nprint(count_args(), count_args(1, 2, 3))", &in),
                 std::string("42\nhello ada\n0 3\n"));
        CHECK(in.HasNative("add"));
    }
    TEST("a native changes C++ state; script reads it back");
    {
        Interpreter in;
        InstallStandardLibrary(in);
        std::vector<std::string> log;
        int score = 0;
        in.Define("log", [&log](Args& a) { log.push_back(a[0].AsString()); return Value{}; }, Arity::Exactly(1));
        in.Define("add_score", [&score](Args& a) { score += (int)a.Number(0); return Value{(double)score}; }, Arity::Exactly(1));
        CHECK_EQ(Run("log(\"start\")\nfor (var i = 0; i < 3; i++) add_score(5)\nprint(add_score(0))\nlog(\"done\")", &in), std::string("15\n"));
        CHECK_EQ(score, 15);
        CHECK_EQ(log.size(), (std::size_t)2);
        if (log.size() == 2) { CHECK_EQ(log[0], std::string("start")); CHECK_EQ(log[1], std::string("done")); }
    }
    TEST("lists and maps cross the boundary both ways");
    {
        Interpreter in;
        InstallStandardLibrary(in);
        in.Define("make_list", [](Args& a) {
            Value v = Value::MakeArray();
            for (int i = 0; i < (int)a.Number(0); ++i) v.AsArray()->push_back(Value{i * i});
            return v;
        }, Arity::Exactly(1));
        in.Define("sum", [](Args& a) {
            double s = 0;
            for (auto& x : *a.List(0)) s += x.AsNumber();
            return Value{s};
        }, Arity::Exactly(1));
        in.Define("set_key", [](Args& a) { (*a.Map(0))[a.String(1)] = a.Get(2); return Value{}; }, Arity::Exactly(3));
        CHECK_EQ(Run("var xs = make_list(4)\nprint(xs, sum(xs))\nvar m = new Dictionary<string, int>()\nset_key(m, \"hp\", 9)\nprint(m.hp)", &in),
                 std::string("[0, 1, 4, 9] 14\n9\n"));
    }
    TEST("a wrong number of arguments is a runtime error on the calling line");
    {
        Interpreter in;
        InstallStandardLibrary(in);
        in.Define("add", [](Args& a) { return Value{a.Number(0) + a.Number(1)}; }, Arity::Exactly(2));
        in.Define("range_fn", [](Args&) { return Value{}; }, Arity::Between(1, 2));
        in.Define("some", [](Args&) { return Value{}; }, Arity::AtLeast(1));
        CHECK_EQ(Run("print(1)\n\nadd(1, 2, 3)", &in), std::string("1\nERROR runtime 3: add() takes 2 arguments, got 3"));
        CHECK_EQ(Run("function f() {\n  return add(1)\n}\nf()", &in), std::string("ERROR runtime 2: add() takes 2 arguments, got 1"));
        CHECK_EQ(Run("range_fn()", &in), std::string("ERROR runtime 1: range_fn() takes 1 to 2 arguments, got 0"));
        CHECK_EQ(Run("x = 1\nsome()", &in), std::string("ERROR runtime 2: some() takes at least 1 argument, got 0"));
        CHECK_EQ(Run("add(a: 1, b: 2)", &in), std::string("ERROR runtime 1: 'add' doesn't take named arguments"));
    }
    TEST("a wrong argument type is a runtime error on the calling line");
    {
        Interpreter in;
        InstallStandardLibrary(in);
        in.Define("add", [](Args& a) { return Value{a.Number(0) + a.Number(1)}; }, Arity::Exactly(2));
        in.Define("first", [](Args& a) { return a.List(0)->empty() ? Value{} : a.List(0)->front(); }, Arity::Exactly(1));
        CHECK_EQ(Run("var x = 1\nvar y = add(x, \"two\")", &in), std::string("ERROR runtime 2: add(): argument 2 must be a number, got text"));
        CHECK_EQ(Run("first(5)", &in), std::string("ERROR runtime 1: first(): argument 1 must be a list, got a number"));
    }
    TEST("errors a native raises itself, and C++ exceptions, carry the line");
    {
        Interpreter in;
        InstallStandardLibrary(in);
        in.Define("must_be_positive", [](Args& a) {
            if (a.Number(0) <= 0) a.Fail("needs a positive number");
            return a[0];
        }, Arity::Exactly(1));
        in.Define("explode", [](Args&) -> Value { throw std::runtime_error("c++ said no"); });
        CHECK_EQ(Run("must_be_positive(3)\nmust_be_positive(-1)", &in), std::string("ERROR runtime 2: must_be_positive(): needs a positive number"));
        CHECK_EQ(Run("\n\nexplode()", &in), std::string("ERROR runtime 3: explode(): c++ said no"));
        CHECK_EQ(Run("try { explode() } catch (e) { print(\"caught\", e) }", &in), std::string("caught explode(): c++ said no\n"));
    }
    TEST("a native calls back into script (a lambda or a function name)");
    {
        Interpreter in;
        InstallStandardLibrary(in);
        in.Define("twice", [](Args& a) {
            Value once = a.Call(a[0], {a.Get(1)});
            return a.Call(a[0], {once});
        }, Arity::Exactly(2));
        CHECK_EQ(Run("print(twice(x => x * 3, 2))\nfunction inc(n) { return n + 1 }\nprint(twice(\"inc\", 5))", &in), std::string("18\n7\n"));
    }
    TEST("natives are found before script functions; a native can replace another");
    {
        Interpreter in;
        InstallStandardLibrary(in);
        in.Define("v", [](Args&) { return Value{1.0}; });
        in.Define("v", [](Args&) { return Value{2.0}; });
        CHECK_EQ(Run("print(v())", &in), std::string("2\n"));
    }
    TEST("the Host seam answers dotted names and unknown calls");
    {
        Interpreter in;
        InstallStandardLibrary(in);
        CounterHost host;
        in.SetHost(&host);
        CHECK_EQ(Run("print(Time.deltaTime * 4)\nTime.timeScale = 0.25\nprint(Time.timeScale)\nprint(host_spawn(1, 2))", &in),
                 std::string("2\n0.25\n2\n"));
        CHECK_EQ(host.timeScale, 0.25);
        CHECK_EQ(host.unknown.size(), (std::size_t)1);
        CHECK_EQ(Run("not_host()", &in), std::string("ERROR runtime 1: call to undefined function 'not_host'"));
    }
    TEST("the shared core builtins answer as the engine does (never an error)");
    CHECK_EQ(Run("print(abs(-3), sqrt(16), pow(2, 10), floor(2.7), ceil(2.1), round(2.5), min(3, 1), max(4, 9), clamp(15, 0, 10))"),
             std::string("3 4 1024 2 3 3 1 9 10\n"));
    CHECK_EQ(Run("print(round(sin(1.5707964) * 1000), cos(0), tan(0), sign(-2), lerp(0, 10, 0.5), wrap(370, 0, 360))"),
             std::string("1000 1 0 -1 5 10\n"));
    CHECK_EQ(Run("print(len(3, 4), hypot(3, 4), len(5), sqrt(-4), abs(\"x\"), num(\"12abc\"), num(\"abc\"), str(5) + \"!\")"),
             std::string("5 5 5 nan 0 12 0 5!\n"));   // len(x, y) is a 2D vector's length, as in the engine
    CHECK_EQ(Run("print(min(), max(1), clamp(), to_num(\"2.5\") * 2, to_str(7))"), std::string("0 1 0 5 7\n"));
    CHECK_EQ(Run("write(\"a\")\nwrite(\"b\", \"c\")\nprintln(\"!\")"), std::string("ab c!\n"));
    CHECK_EQ(Run("type(1)"), std::string("ERROR runtime 1: call to undefined function 'type'"));   // not in the engine, so not here
    {
        Interpreter in;
        InstallStandardLibrary(in);
        std::istringstream input("first line\nsecond\n");
        in.SetInput(&input);
        CHECK_EQ(Run("var a = input()\nvar b = input(\"> \")\nvar c = input()\nprint(a, \"|\", b, \"|\", c.Length)", &in),
                 std::string("> first line | second | 0\n"));
    }
    {
        Interpreter in;
        InstallStandardLibrary(in);
        int code = -1;
        try { in.Run("exit(4)"); } catch (const ExitRequest& e) { code = e.code; }
        CHECK_EQ(code, 4);
        auto exitCode = [&](const std::string& src) {
            int c = -1;
            try { in.Run(src); } catch (const ExitRequest& e) { c = e.code; }
            return c;
        };
        CHECK_EQ(exitCode("exit(300)"), 255);   // a process exit code is 0..255
        CHECK_EQ(exitCode("exit(-5)"), 0);
        CHECK_EQ(exitCode("exit(99999999)"), 255);
        CHECK_EQ(exitCode("exit(2.9)"), 2);
        CHECK_EQ(exitCode("exit(\"x\")"), 0);
    }
    TEST_RESULT();
}
