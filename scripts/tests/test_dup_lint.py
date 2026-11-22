#!/usr/bin/env python3
"""Tests for scripts/dup_lint.py (the duplicate-code guard). Each test builds a tiny throw-away git repository with a
`main` and a branch, runs `dup_lint.py check --base main --head branch` on it and looks at the exit code and output.
No network, no real branches. Run: python3 scripts/tests/test_dup_lint.py"""
import json, os, shutil, subprocess, sys, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(os.path.dirname(HERE), "dup_lint.py")

# A 20-line engine function: long enough (about 230 tokens) to be a real paste.
BLOCK = """\
float SteerTowards(const Vec3& from, const Vec3& to, float maxTurn, float dt, Vec3& outDir) {
    Vec3 delta = to - from;
    float dist = Length(delta);
    if (dist < 0.0001f) {
        outDir = Vec3(0.0f, 0.0f, 1.0f);
        return 0.0f;
    }
    Vec3 want = delta / dist;
    float cosAngle = Clamp(Dot(outDir, want), -1.0f, 1.0f);
    float angle = std::acos(cosAngle);
    float step = maxTurn * dt;
    if (angle <= step) {
        outDir = want;
        return angle;
    }
    float t = step / angle;
    outDir = Normalize(outDir * (1.0f - t) + want * t);
    float speed = SmoothStep(0.0f, 4.0f, dist) * maxTurn;
    return speed + step * 0.5f + angle * 0.25f;
}
"""


def git(cwd, *a):
    subprocess.run(["git", "-C", cwd, "-c", "user.name=t", "-c", "user.email=t@t", *a], check=True,
                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)


class Repo:
    def __init__(self, files, baseline=None):
        self.d = tempfile.mkdtemp(prefix="duplint")
        git(self.d, "init", "-q", "-b", "main")
        for p, t in files.items():
            self.write(p, t)
        if baseline is not None:
            self.write("scripts/dup_baseline.json", json.dumps({"rules": baseline}))
        self.commit("base")
        git(self.d, "checkout", "-q", "-b", "work")

    def write(self, p, t):
        full = os.path.join(self.d, p)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "w") as f:
            f.write(t)

    def commit(self, msg):
        git(self.d, "add", "-A")
        git(self.d, "commit", "-q", "--allow-empty", "-m", msg)

    def check(self):
        p = subprocess.run([sys.executable, "-I", TOOL, "check", "--root", self.d, "--base", "main", "--head", "work", "--jobs", "1"],
                           capture_output=True, text=True)
        return p.returncode, p.stdout + p.stderr

    def close(self):
        shutil.rmtree(self.d, ignore_errors=True)


STRINGID = "engine/include/okay/Core/StringId.hpp"
FNV_LINE = "    for (char c : s) { h ^= (unsigned char)c; h *= 16777619u; }\n"


class DupLintTest(unittest.TestCase):
    def run_case(self, base, change, baseline=None):
        r = Repo(base, baseline)
        try:
            for p, t in change.items():
                r.write(p, t)
            r.commit("change")
            return r.check()
        finally:
            r.close()

    # ---- fingerprint rules and the baseline ratchet
    def test_new_fnv_copy_fails(self):
        code, out = self.run_case({"engine/src/A.cpp": "int a;\n"},
                                  {"engine/src/Hash.cpp": "unsigned H(const char* s) {\n unsigned h = 0;\n" + FNV_LINE + " return h;\n}\n"})
        self.assertEqual(code, 1, out)
        self.assertIn("duplicate fnv1a", out)
        self.assertIn("::error file=engine/src/Hash.cpp,line=3", out)

    def test_new_fnv_copy_in_python_fails(self):
        code, out = self.run_case({"scripts/a.py": "x = 1\n"},
                                  {"scripts/h.py": "def fnv(s):\n    h = 2166136261\n    for c in s:\n        h = (h ^ c) * 16777619\n    return h\n"})
        self.assertEqual(code, 1, out)
        self.assertIn("duplicate fnv1a", out)

    def test_canonical_file_passes(self):
        code, out = self.run_case({STRINGID: "#pragma once\n"},
                                  {STRINGID: "#pragma once\nconstexpr unsigned kPrime = 16777619u;\n"})
        self.assertEqual(code, 0, out)

    def test_baseline_hit_passes(self):
        old = "unsigned H(const char* s) {\n unsigned h = 0;\n" + FNV_LINE + " return h;\n}\n"
        code, out = self.run_case({"engine/src/Old.cpp": old}, {"engine/src/Old.cpp": "// touched\n" + old},
                                  baseline={"fnv1a": {"engine/src/Old.cpp": 1}})
        self.assertEqual(code, 0, out)

    def test_count_going_up_fails(self):
        old = "unsigned H(const char* s) {\n unsigned h = 0;\n" + FNV_LINE + " return h;\n}\n"
        code, out = self.run_case({"engine/src/Old.cpp": old}, {"engine/src/Old.cpp": old + "unsigned kP2 = 16777619u;\n"},
                                  baseline={"fnv1a": {"engine/src/Old.cpp": 1}})
        self.assertEqual(code, 1, out)
        self.assertIn("baseline 1", out)

    def test_count_going_down_says_baseline_can_shrink(self):
        old = "unsigned H(const char* s) {\n unsigned h = 0;\n" + FNV_LINE + " return h;\n}\n"
        code, out = self.run_case({"engine/src/Old.cpp": old}, {"engine/src/Old.cpp": "int x;\n"},
                                  baseline={"fnv1a": {"engine/src/Old.cpp": 1}})
        self.assertEqual(code, 0, out)
        self.assertIn("baseline can shrink", out)
        self.assertIn("--update-baseline", out)

    def test_baseline_may_not_grow(self):
        code, out = self.run_case({"engine/src/A.cpp": "int a;\n"},
                                  {"engine/src/A.cpp": "unsigned k = 16777619u;\n",
                                   "scripts/dup_baseline.json": json.dumps({"rules": {"fnv1a": {"engine/src/A.cpp": 1}}})},
                                  baseline={})
        self.assertEqual(code, 1, out)
        self.assertIn("baseline grew", out)

    # ---- copy-paste detector
    def test_pasted_block_fails_even_with_whitespace_and_comment_changes(self):
        pasted = "// my new steering\n" + "\n".join(("        " + l.strip() + "   // tweaked") if i % 3 == 0 else l
                                                    for i, l in enumerate(BLOCK.split("\n")))
        code, out = self.run_case({"engine/src/Steer.cpp": "#include \"x.hpp\"\n" + BLOCK},
                                  {"engine/src/NewSteer.cpp": "#include \"y.hpp\"\n" + pasted})
        self.assertEqual(code, 1, out)
        self.assertIn("copy-paste (pasted)", out)
        self.assertIn("engine/src/Steer.cpp:2-", out)

    def test_pasted_block_between_two_new_files_fails(self):
        code, out = self.run_case({"engine/src/A.cpp": "int a;\n"},
                                  {"engine/src/One.cpp": BLOCK, "tests/test_two.cpp": "// test\n" + BLOCK})
        self.assertEqual(code, 1, out)
        self.assertIn("another block this branch adds", out)

    def test_renamed_variable_paste_warns(self):
        renamed = BLOCK
        for a, b in (("delta", "offs"), ("dist", "len"), ("want", "goal"), ("cosAngle", "ca"), ("angle", "ang"),
                     ("step", "lim"), ("outDir", "dir"), ("speed", "spd"), ("from", "a"), ("to", "b"), ("SteerTowards", "TurnTo")):
            renamed = renamed.replace(a, b)
        code, out = self.run_case({"engine/src/Steer.cpp": BLOCK}, {"engine/src/Turn.cpp": renamed})
        self.assertEqual(code, 0, out)
        self.assertIn("near copy, renamed", out)
        self.assertIn("::warning", out)

    def test_small_change_inside_an_old_duplicate_is_not_blamed(self):
        base = {"engine/src/Steer.cpp": BLOCK, "engine/src/Steer2.cpp": BLOCK}
        code, out = self.run_case(base, {"engine/src/Steer2.cpp": BLOCK.replace("0.25f", "0.3f")})
        self.assertEqual(code, 0, out)
        self.assertNotIn("copy-paste", out)

    # ---- dup-allow
    def test_dup_allow_with_reason_passes(self):
        pasted = BLOCK.replace("float SteerTowards", "// dup-allow: kept until the AI steering merge lands\nfloat SteerTowards", 1)
        code, out = self.run_case({"engine/src/Steer.cpp": BLOCK},
                                  {"engine/src/NewSteer.cpp": pasted,
                                   "engine/src/Hash.cpp": "unsigned k = 16777619u;  // dup-allow: matches the file format spec\n"})
        self.assertEqual(code, 0, out)

    def test_dup_allow_block_marker_passes(self):
        pasted = "// dup-allow-begin: generated-style table, checked by a test\n" + BLOCK + "// dup-allow-end\n"
        code, out = self.run_case({"engine/src/Steer.cpp": BLOCK}, {"engine/src/NewSteer.cpp": pasted})
        self.assertEqual(code, 0, out)

    def test_dup_allow_without_reason_fails(self):
        code, out = self.run_case({"engine/src/A.cpp": "int a;\n"},
                                  {"engine/src/Hash.cpp": "unsigned k = 16777619u;  // dup-allow:\n"})
        self.assertEqual(code, 1, out)
        self.assertIn("needs a reason", out)

    # ---- other languages and the shared-code list
    def test_pasted_shell_block_fails(self):
        sh = "".join(f"if [ -f \"$dir/file{i}.txt\" ]; then\n  cp \"$dir/file{i}.txt\" \"$out/\" || exit {i}\nfi\n" for i in range(8))
        code, out = self.run_case({"scripts/a.sh": "#!/bin/sh\n" + sh}, {"scripts/b.sh": "#!/bin/sh\n# copy\n" + sh})
        self.assertEqual(code, 1, out)
        self.assertIn("copy-paste (pasted)", out)

    def test_shared_code_list_must_name_real_files(self):
        doc = "| Job | File |\n|---|---|\n| hashing | `Core/StringId.hpp` |\n| base64 | `Core/Base64.hpp` (planned; dedup in progress) |\n| noise | `Math/Gone.hpp` |\n"
        code, out = self.run_case({STRINGID: "#pragma once\n"}, {"docs/engine/shared_code.md": doc})
        self.assertEqual(code, 1, out)
        self.assertIn("Math/Gone.hpp", out)
        self.assertNotIn("Base64.hpp", out.split("Math/Gone.hpp")[0][-300:])


if __name__ == "__main__":
    unittest.main(verbosity=2)
