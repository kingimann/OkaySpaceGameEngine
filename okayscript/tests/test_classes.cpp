// The C#-style parts of OkayScript, now in the core: plain classes with new, fields, constructors and private
// members; statics shared by every run of a source; events (+= / -=); the C# wrappers that do nothing (using,
// namespace, class Name : Base, attributes, { get; set; }); yield without a coroutine host; diagnostics.
#include "TestKit.hpp"
#include "okayscript/Parser.hpp"

using oktest::Run;
using namespace okayscript;

int main() {
    TEST("plain classes: fields, constructors, methods, this, new");
    CHECK_EQ(Run("class Item {\n  public string name = \"?\";\n  public int count;\n"
                 "  Item(string n, int c) { name = n; count = c; }\n  public int Twice() { return count * 2; }\n}\n"
                 "var a = new Item(\"potion\", 3)\nvar b = a\nb.count = 5\nprint(a.name, a.count, a.Twice(), a == b)"),
             std::string("potion 5 10 true\n"));
    CHECK_EQ(Run("class P { public int x; public void Add(int n) { this.x += n; } }\nvar p = new P()\np.Add(2)\np.Add(3)\nprint(p.x)"),
             std::string("5\n"));
    CHECK_EQ(Run("class S { int secret = 1; }\nvar s = new S()\nprint(s.secret)"),
             std::string("ERROR runtime 3: 'secret' is private on S"));
    CHECK_EQ(Run("class S { public int n; }\nvar s = new S()\ns.n = \"x\""),
             std::string("ERROR runtime 3: 'n' is an int, so it can't hold text (use int.Parse(text))"));
    CHECK_EQ(Run("class S { public int n; }\nvar s = new S(1)"), std::string("ERROR runtime 2: S has no constructor that takes 1 value"));
    CHECK_EQ(Run("class A { public int v = 1; }\nprint(new A() == new A())"), std::string("false\n"));   // objects compare by identity

    TEST("statics are shared by every run of the same source");
    {
        const std::string src = "static int runs = 0;\nruns += 1\nprint(runs)";
        CHECK_EQ(Run(src), std::string("1\n"));
        CHECK_EQ(Run(src), std::string("2\n"));   // a second interpreter running the same text sees the same static
    }

    TEST("events: += a function or a lambda subscribes, -= removes, a number still adds");
    {
        Interpreter in;
        InstallStandardLibrary(in);
        CHECK_EQ(Run("function OnHit(n) { print(\"hit\", n) }\nvar log = x => print(\"log\", x)\nHurt += OnHit\nHurt += log\nscore = 1\nscore += 2\nprint(score)", &in),
                 std::string("3\n"));
        std::vector<Value> args{Value{7.0}};
        std::ostringstream out;
        in.SetOutput(&out);
        in.Rt().FireSubs("Hurt", args);
        CHECK_EQ(out.str(), std::string("hit 7\nlog 7\n"));
        in.SetOutput(nullptr);
        CHECK_EQ(Run("Hurt -= OnHit", &in), std::string(""));
        CHECK(in.Rt().HasSub("Hurt"));
        CHECK_EQ(in.Rt().subs.size(), (std::size_t)1);
    }

    TEST("C# wrappers that do nothing still parse: using, namespace, class with a base, attributes, auto-properties");
    CHECK_EQ(Run("using System;\nnamespace Game {\n  public class Hero : Behaviour {\n    [Header(\"Stats\")] public int hp = 3;\n"
                 "    public int Lives { get; set; } = 2;\n    void Start() { print(hp, Lives); }\n  }\n}\nStart()"),
             std::string("3 2\n"));
    CHECK_EQ(Run("enum State { Idle, Run = 5, Jump }\nState s = State.Jump\nprint(s, (int)State.Run)"), std::string("6 5\n"));

    TEST("top-level statement words are not functions (engine parser bug, fixed here)");
    CHECK_EQ(Run("var k = 2\nswitch (k) { case 2: print(\"two\"); break; }"), std::string("two\n"));
    CHECK_EQ(Run("try { throw \"x\" } catch (e) { print(\"caught\", e) }"), std::string("caught x\n"));

    TEST("yield needs a host that runs coroutines");
    CHECK_EQ(Run("function Co() {\n  yield return null;\n}\nCo()"), std::string("ERROR runtime 2: 'yield return' only works in a coroutine the host runs"));

    TEST("ValidateSource lists every problem with its line");
    {
        auto d = ValidateSource("int a = \"three\";\nvoid F() { return 5; }\nvar = 1\nvar ok = 2;\nprint(ok\n");
        int errors = 0;
        bool typeMistake = false, voidReturn = false;
        for (auto& x : d) {
            if (x.severity == 0) ++errors;
            if (x.line == 1 && x.message.find("can't hold text") != std::string::npos) typeMistake = true;
            if (x.line == 2 && x.message.find("is void") != std::string::npos) voidReturn = true;
        }
        CHECK(errors >= 2);
        CHECK(typeMistake);
        CHECK(voidReturn);
        auto w = ValidateSource("public override void Update() { var x = 5f; }");
        bool noEffect = false;
        for (auto& x : w) if (x.severity == 1) noEffect = true;
        CHECK(noEffect);
        CHECK(ValidateSource("var a = 1\nprint(a)").empty());
    }
    TEST_RESULT();
}
