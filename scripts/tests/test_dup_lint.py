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
                                  {"engine/src/SteerCopy.cpp": "#include \"y.hpp\"\n" + pasted})
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
                                  {"engine/src/SteerCopy.cpp": pasted,
                                   "engine/src/Hash.cpp": "unsigned k = 16777619u;  // dup-allow: matches the file format spec\n"})
        self.assertEqual(code, 0, out)

    def test_dup_allow_block_marker_passes(self):
        pasted = "// dup-allow-begin: generated-style table, checked by a test\n" + BLOCK + "// dup-allow-end\n"
        code, out = self.run_case({"engine/src/Steer.cpp": BLOCK}, {"engine/src/SteerCopy.cpp": pasted})
        self.assertEqual(code, 0, out)

    def test_dup_allow_without_reason_fails(self):
        code, out = self.run_case({"engine/src/A.cpp": "int a;\n"},
                                  {"engine/src/Hash.cpp": "unsigned k = 16777619u;  // dup-allow:\n"})
        self.assertEqual(code, 1, out)
        self.assertIn("needs a reason", out)

    # ---- new names that look like existing ones (warnings only)
    def test_similar_new_name_warns(self):
        base = {"engine/src/Damage.cpp": "bool HealthOf(GameObject* g, float& h, float& m) {\n  return false;\n}\n",
                "engine/src/Json.cpp": "bool ParseJson(const std::string& s, Value& out) {\n  return true;\n}\n"}
        code, out = self.run_case(base, {"engine/src/Mine.cpp": "float GetHealth(GameObject* g) {\n  return 1.0f;\n}\n"
                                                                 "bool JsonParse(const std::string& s, Value& v) {\n  return true;\n}\n"})
        self.assertEqual(code, 0, out)
        self.assertIn("new name GetHealth looks like the existing HealthOf at engine/src/Damage.cpp:1", out)
        self.assertIn("new name JsonParse looks like the existing ParseJson", out)
        self.assertIn("did you mean to reuse".lower(), out.lower())

    def test_get_set_pair_is_not_a_similar_name(self):
        base = {"engine/src/Body.cpp": "float GetFriction(Body* b) {\n  return 0.0f;\n}\n"}
        code, out = self.run_case(base, {"engine/src/Body2.cpp": "void SetFriction(Body* b, float f) {\n}\n"})
        self.assertEqual(code, 0, out)
        self.assertNotIn("WARNING similar name", out)

    # ---- retired (OKAY_DEPRECATED) names
    def test_new_call_to_a_retired_name_fails(self):
        base = {"engine/include/okay/Core/OldHash.hpp": "#pragma once\nOKAY_DEPRECATED(\"use StringId::HashOf\")\nunsigned OldHash(const char* s);\n",
                "engine/src/A.cpp": "int a;\n"}
        code, out = self.run_case(base, {"engine/src/A.cpp": "int a;\nunsigned h = OldHash(\"player\");\n"})
        self.assertEqual(code, 1, out)
        self.assertIn("OldHash was retired", out)

    def test_marking_a_name_retired_in_the_same_branch_passes(self):
        base = {"engine/include/okay/Core/OldHash.hpp": "#pragma once\nunsigned OldHash(const char* s);\n"}
        code, out = self.run_case(base, {"engine/include/okay/Core/OldHash.hpp":
                                         "#pragma once\nOKAY_DEPRECATED(\"use StringId::HashOf\")\nunsigned OldHash(const char* s);\n"})
        self.assertEqual(code, 0, out)

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


    def test_moved_code_is_not_a_new_copy(self):
        """A pure move (code cut from one file and pasted into another in the same branch) adds no copy, even when main
        already has an older copy elsewhere (found on #1141, the editor split)."""
        r = Repo({"editor/main.cpp": "int before;\n" + BLOCK + "int after;\n", "engine/src/Old.cpp": "#include \"o.hpp\"\n" + BLOCK})
        try:
            r.write("editor/main.cpp", "int before;\nint after;\n")
            r.write("editor/Moved.cpp", "#include \"m.hpp\"\n" + BLOCK)
            r.commit("move")
            code, out = r.check()
            self.assertEqual(code, 0, out)
            self.assertNotIn("copy-paste (pasted)", out)
        finally:
            r.close()

REG_PAGE = ("# 6. Physics\n\n## 6. Physics\n\n### 6.1 Bodies\n- **System:** Physics - Moves bodies.\n"
            "  Home: `engine/include/okay/Physics/`, `engine/src/Physics/`. Entry: `engine/include/okay/Physics/P.hpp`.{extra}\n"
            "- **What:** w\n- **Code:** `Physics/P`\n- **Status:** Works\n- **Words:** x\n")
SYS_BASE = {"engine/include/okay/Physics/P.hpp": "int p;\n", "engine/src/Physics/P.cpp": "int q;\n", "editor/main.cpp": "int m;\n",
            "docs/feature_map/06-physics.md": REG_PAGE.format(extra="")}


class SystemNamesTest(unittest.TestCase):
    """Rule 19e: one plain name per system, and a new system folder needs a registry entry (the feature map's System lines)."""

    def test_banned_qualifier_in_new_file_fails(self):
        for name in ("OwnSolver.cpp", "ImprovedStep.cpp", "SolverV2.cpp", "classic_step.py", "NewContacts.hpp"):
            code, out = self.run_case(SYS_BASE, {"engine/src/Physics/" + name: "int z;\n"})
            self.assertEqual(code, 1, name + out)
            self.assertIn("system name", out)

    run_case = DupLintTest.run_case

    def test_plain_names_pass(self):
        code, out = self.run_case(SYS_BASE, {"engine/src/Physics/Owner.cpp": "int z;\n", "engine/src/Physics/Renewal.cpp": "int y;\n"})
        self.assertEqual(code, 0, out)

    def test_dup_allow_or_registry_entry_lets_a_qualified_name_through(self):
        code, out = self.run_case(SYS_BASE, {"engine/src/Physics/OwnSolver.cpp": "// dup-allow: replaces the vendored solver, renamed next release\nint z;\n"})
        self.assertEqual(code, 0, out)
        page = REG_PAGE.format(extra=" Home: `engine/src/Physics/OwnSolver.cpp`.")
        code, out = self.run_case(SYS_BASE, {"engine/src/Physics/OwnSolver.cpp": "int z;\n", "docs/feature_map/06-physics.md": page})
        self.assertEqual(code, 0, out)

    def test_new_system_folder_needs_a_registry_home(self):
        code, out = self.run_case(SYS_BASE, {"engine/include/okay/Wind/Wind.hpp": "int w;\n"})
        self.assertEqual(code, 1, out)
        self.assertIn("system folder engine/include/okay/Wind/ is not in the system registry", out)
        page = REG_PAGE + "\n### 6.2 Wind\n- **System:** Wind - Blows.\n  Home: `engine/include/okay/Wind/`. Entry: `engine/include/okay/Wind/Wind.hpp`.\n- **What:** w\n- **Code:** `Wind/Wind`\n- **Status:** Works\n- **Words:** x\n"
        code, out = self.run_case(SYS_BASE, {"engine/include/okay/Wind/Wind.hpp": "int w;\n",
                                             "docs/feature_map/06-physics.md": page.format(extra="")})
        self.assertEqual(code, 0, out)

    def test_new_top_level_folder_and_qualified_folder_fail(self):
        code, out = self.run_case(SYS_BASE, {"newtool/main.cpp": "int t;\n"})
        self.assertEqual(code, 1, out)
        self.assertIn("new top-level folder newtool/", out)
        code, out = self.run_case(SYS_BASE, {"engine/src/Physics/OwnCore/Step.cpp": "int s;\n"})
        self.assertEqual(code, 1, out)
        self.assertIn("new folder engine/src/Physics/OwnCore/ has 'Own'", out)

    def test_new_code_name_with_qualifier_warns(self):
        code, out = self.run_case(SYS_BASE, {"engine/src/Physics/P.cpp": "int q;\nint ImprovedContactSolve(int a, int b) {\n  return a + b;\n}\n"})
        self.assertEqual(code, 0, out)
        self.assertIn("new name ImprovedContactSolve has 'Improved'", out)


class CrossBranchTest(unittest.TestCase):
    """collision_check's copy check between open branches (dup_lint.cross_branch)."""
    def test_same_block_on_two_branches_and_similar_names(self):
        sys.path.insert(0, os.path.dirname(TOOL))
        import dup_lint as D
        r = Repo({"engine/src/A.cpp": "int a;\n"})
        try:
            git(r.d, "checkout", "-q", "main"); git(r.d, "checkout", "-q", "-b", "one")
            r.write("engine/src/One.cpp", BLOCK + "bool ParseJson(const char* s) {\n  return s != nullptr;\n}\n"); r.commit("one")
            git(r.d, "checkout", "-q", "main"); git(r.d, "checkout", "-q", "-b", "two")
            r.write("engine/src/Two.cpp", "// mine\n" + BLOCK + "bool JsonParse(const char* text) {\n  return text != nullptr;\n}\n"); r.commit("two")
            res = D.cross_branch(r.d, "main", {"one": "one", "two": "two"})
            self.assertEqual(len(res["clones"]), 1, res)
            c = res["clones"][0]
            self.assertEqual({c["a_file"], c["b_file"]}, {"engine/src/One.cpp", "engine/src/Two.cpp"})
            self.assertTrue(any({n["a_name"], n["b_name"]} == {"ParseJson", "JsonParse"} for n in res["names"]), res["names"])
            focus = D.cross_branch(r.d, "main", {"one": "one", "two": "two"}, focus="two")
            self.assertEqual(len(focus["clones"]), 1, focus)
        finally:
            r.close()

    def test_root_level_file_on_a_branch_with_a_slash(self):
        """Bug found by CI on 2026-11-22: a branch named like claude2/release-x that changes a root-level code file
        (CMakeLists.txt) crashed cross_branch, because the "branch\0path" key was read as the path."""
        sys.path.insert(0, os.path.dirname(TOOL))
        import dup_lint as D
        r = Repo({"CMakeLists.txt": "project(x)\n", "engine/src/A.cpp": "int a;\n"})
        try:
            git(r.d, "checkout", "-q", "main"); git(r.d, "checkout", "-q", "-b", "claude2/release-x")
            r.write("CMakeLists.txt", "project(x)\nset(V 2)\n"); r.commit("bump")
            res = D.cross_branch(r.d, "main", {"claude2/release-x": "claude2/release-x"}, focus="claude2/release-x")
            self.assertEqual(res["clones"], [], res)
        finally:
            r.close()


    def test_two_branches_moving_the_same_old_code_are_not_a_collision(self):
        sys.path.insert(0, os.path.dirname(TOOL))
        import dup_lint as D
        r = Repo({"editor/main.cpp": "int before;\n" + BLOCK + "int after;\n"})
        try:
            for br, dest in (("one", "editor/A.cpp"), ("two", "editor/B.hpp")):
                git(r.d, "checkout", "-q", "main"); git(r.d, "checkout", "-q", "-b", br)
                r.write("editor/main.cpp", "int before;\nint after;\n"); r.write(dest, "// moved\n" + BLOCK); r.commit(br)
            res = D.cross_branch(r.d, "main", {"one": "one", "two": "two"})
            self.assertEqual(res["clones"], [], res)
        finally:
            r.close()

class CodeIndexTest(unittest.TestCase):
    def test_public_names_only(self):
        sys.path.insert(0, os.path.dirname(TOOL))
        import gen_code_index as G
        hdr = ("#pragma once\nnamespace okay {\n/// A thing that moves.\nclass Mover {\npublic:\n    void Step(float dt);\n};\n"
               "class Forward;\n/// Clamp a speed.\ninline float ClampSpeed(float v) { return v; }\n"
               "namespace detail {\nint Hidden();\n}\nstruct Pose { float x; };\n}\n")
        got = {(k, n): (ln, d) for k, n, ln, d in G.entries("engine/include/okay/X/M.hpp", hdr)}
        self.assertEqual(got.get(("class", "Mover")), (4, "A thing that moves."))
        self.assertEqual(got.get(("function", "ClampSpeed")), (10, "Clamp a speed."))
        self.assertIn(("struct", "Pose"), got)
        self.assertNotIn(("function", "Step"), got)      # a method
        self.assertNotIn(("function", "Hidden"), got)    # namespace detail
        self.assertNotIn(("class", "Forward"), got)      # a forward declaration


class PrBodyExistingTest(unittest.TestCase):
    def test_existing_code_section(self):
        sys.path.insert(0, os.path.dirname(TOOL))
        import check_pr_body as PB
        self.assertFalse(PB.check_existing("## How to try it\nsteps here\n", ["a.cpp"])[0])
        self.assertFalse(PB.check_existing("## Existing code\n\n<!-- hint -->\n-\n", ["a.cpp"])[0])
        self.assertFalse(PB.check_existing("## Existing code\nnone\n", ["a.cpp"])[0])
        self.assertTrue(PB.check_existing("## Existing code\nnone: searched code_index.md for ragdoll, joint motor\n", ["a.cpp"])[0])
        self.assertTrue(PB.check_existing("## Existing code\nReuses SolveTwoBoneIK (Math/TwoBoneIK.hpp); replaces the IK in HumanoidMotion.cpp\n", ["a.cpp"])[0])
        self.assertTrue(PB.check_existing("## Existing code\nn/a\n", ["docs/x.md"])[0])


if __name__ == "__main__":
    unittest.main(verbosity=2)
