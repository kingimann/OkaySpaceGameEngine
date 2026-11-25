#!/usr/bin/env python3
"""scripts/check_pure_move.py: a real move passes; an edited, reordered, lost or extra line fails; rules allow exactly
what they list. Each case is a small git repository made in a temporary folder."""
import os, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOL = os.path.join(ROOT, "scripts", "check_pure_move.py")
fails = 0


def check(cond, what, out=""):
    global fails
    print(("ok   " if cond else "FAIL ") + what + ("" if cond else "\n" + out))
    fails += 0 if cond else 1


BIG = """// main file
#include <string>
namespace app {
int Keep1() { return 1; }

// ---- Section that moves ----
namespace mover {
struct Opts { int a = 1; };
int Work(const Opts& o = {}) {
    int x = o.a;
    x += 2;
    x *= 3;
    return x;
}
int Other() { return Work() + 1; }
} // namespace mover

int Keep2() { return mover::Other(); }
} // namespace app
"""

AFTER_MAIN = """// main file
#include <string>
#include "Mover.hpp"
namespace app {
int Keep1() { return 1; }

int Keep2() { return mover::Other(); }
} // namespace app
"""

HEADER = """#pragma once
// Mover.hpp
namespace app {
namespace mover {
struct Opts { int a = 1; };
int Work(const Opts& o = {});
int Other();
} // namespace mover
} // namespace app
"""

CPP = """// Mover.cpp
#include "Mover.hpp"
namespace app {

// ---- Section that moves ----
namespace mover {
int Work(const Opts& o) {
    int x = o.a;
    x += 2;
    x *= 3;
    return x;
}
int Other() { return Work() + 1; }
} // namespace mover

} // namespace app
"""

RULES = """# test rules
change: int Work(const Opts& o = {}) { ==> int Work(const Opts& o) {
add: ^int Work\\(const Opts& o = \\{\\}\\);$
add: ^int Other\\(\\);$
"""


def git(d, *args):
    subprocess.run(["git", "-C", d] + list(args), check=True, capture_output=True)


def run_case(main_after, cpp, header=HEADER, rules=RULES, head_commit=False, big=BIG):
    with tempfile.TemporaryDirectory() as d:
        git(d, "init", "-q")
        git(d, "config", "user.email", "test@example.invalid")
        git(d, "config", "user.name", "test")
        open(os.path.join(d, "main.cpp"), "w").write(big)
        git(d, "add", ".")
        git(d, "commit", "-q", "-m", "base")
        open(os.path.join(d, "main.cpp"), "w").write(main_after)
        open(os.path.join(d, "Mover.cpp"), "w").write(cpp)
        open(os.path.join(d, "Mover.hpp"), "w").write(header)
        open(os.path.join(d, "move.rules"), "w").write(rules)
        args = [sys.executable, "-I", TOOL, "--repo", d, "--base", "HEAD", "--source", "main.cpp",
                "--dest", "Mover.hpp", "--dest", "Mover.cpp", "--rules", os.path.join(d, "move.rules")]
        if head_commit:
            git(d, "add", ".")
            git(d, "commit", "-q", "-m", "move")
            args[args.index("HEAD")] = "HEAD~1"
            args += ["--head", "HEAD"]
        p = subprocess.run(args, capture_output=True, text=True)
        return p.returncode, p.stdout + p.stderr


code, out = run_case(AFTER_MAIN, CPP)
check(code == 0 and "PURE MOVE" in out, "a clean move with its rules passes (working tree)", out)

code, out = run_case(AFTER_MAIN, CPP, head_commit=True)
check(code == 0, "the same move passes when both sides are commits (--head)", out)

code, out = run_case(AFTER_MAIN, CPP, rules="")
check(code == 1 and "int Work(const Opts& o) {" in out and "int Other();" in out,
      "without the rules the default-argument edit and the declarations are reported", out)

code, out = run_case(AFTER_MAIN, CPP.replace("    x += 2;", "    x += 4;"))
check(code == 1 and "x += 2;" in out and "x += 4;" in out, "an edited moved line fails and both versions are shown", out)

code, out = run_case(AFTER_MAIN, CPP.replace("    x += 2;\n    x *= 3;\n", "    x *= 3;\n    x += 2;\n"))
check(code == 1 and "out of order" in out, "two moved lines swapped fails (order matters)", out)

code, out = run_case(AFTER_MAIN, CPP.replace("    x *= 3;\n", ""))
check(code == 1 and "x *= 3;" in out, "a moved line that never comes back fails", out)

code, out = run_case(AFTER_MAIN, CPP.replace("    return x;\n", "    x -= 1;\n    return x;\n"))
check(code == 1 and "x -= 1;" in out, "a new statement in the moved code fails", out)

code, out = run_case(AFTER_MAIN.replace("int Keep1() { return 1; }", "int Keep1() { return 2; }"), CPP)
check(code == 1 and "return 2;" in out, "an edit elsewhere in the source file fails", out)

code, out = run_case(AFTER_MAIN, CPP.replace("namespace mover {\nint Work", "#if 0\nnamespace mover {\nint Work"))
check(code == 1 and "#if 0" in out, "a preprocessor guard around moved code is not scaffolding", out)

code, out = run_case(AFTER_MAIN, CPP.replace("// Mover.cpp\n", "// Mover.cpp\n// a new comment\n\n"))
check(code == 0, "new comments and blank lines in the new files are allowed", out)

# A blank line before the moved code in the source and one after the moved struct in the header must not pair first
# and push the struct "out of order" (blank lines never anchor a match).
code, out = run_case(AFTER_MAIN, CPP, big=BIG.replace("namespace mover {\nstruct", "namespace mover {\n\nstruct"),
                     header=HEADER.replace("struct Opts { int a = 1; };\n", "// options\nstruct Opts { int a = 1; };\n\n"))
check(code == 0, "a blank line after a struct moved to the header does not hide it", out)

code, out = run_case(AFTER_MAIN, CPP, rules=RULES + "change: int Missing() { ==> int Missing2() {\n")
check(code == 1 and "was not used" in out, "a change rule that matches nothing fails (rules stay exact)", out)

code, out = run_case(AFTER_MAIN, CPP, rules="bogus: x\n")
check(code == 2, "an unknown rule kind is a usage error", out)

print("%d failure(s)" % fails)
sys.exit(1 if fails else 0)
